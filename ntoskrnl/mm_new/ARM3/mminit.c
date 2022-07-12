
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

#undef MmSystemRangeStart

/* GLOBALS ********************************************************************/

/* 0 - on paging, 1 - off paging, 2 - undocumented */
UCHAR MmDisablePagingExecutive = 1;

BOOLEAN Mm64BitPhysicalAddress = FALSE;
POBJECT_TYPE MmSectionObjectType = NULL;
ULONG_PTR MmSubsectionBase;

/* These are all registry-configurable, but by default,
   the memory manager will figure out the most appropriate values.
*/
ULONG MmMaximumNonPagedPoolPercent;
SIZE_T MmSizeOfNonPagedPoolInBytes;
SIZE_T MmMaximumNonPagedPoolInBytes;
PFN_NUMBER MmMaximumNonPagedPoolInPages; /* Some of the same values, in pages */

/* These numbers describe the discrete equation components of the nonpaged pool sizing algorithm.
   They are described on http://support.microsoft.com/default.aspx/kb/126402/ja
   along with the algorithm that uses them, which is implemented later below.
*/
SIZE_T MmMinimumNonPagedPoolSize = 256 * 1024;
ULONG MmMinAdditionNonPagedPoolPerMb = 32 * 1024;
SIZE_T MmDefaultMaximumNonPagedPool = 1024 * 1024;
ULONG MmMaxAdditionNonPagedPoolPerMb = 400 * 1024;

/* The memory layout (and especially variable names) of the NT kernel mode components
   can be a bit hard to twig, especially when it comes to the non paged area.

   There are really two components to the non-paged pool:
   - The initial nonpaged pool, sized dynamically up to a maximum.
   - The expansion nonpaged pool, sized dynamically up to a maximum.

   The initial nonpaged pool is physically continuous for performance,
   and immediately follows the PFN database, typically sharing the same PDE.
   It is a very small resource (32MB on a 1GB system), and capped at 128MB.

   Right now we call this the "ARM3 Nonpaged Pool"
   and it begins somewhere after the PFN database (which starts at 0xB0000000).

   The expansion nonpaged pool, on the other hand, can grow much bigger (400MB for a 1GB system).
   On ARM3 however, it is currently capped at 128MB.

   The address where the initial nonpaged pool starts is aptly named MmNonPagedPoolStart,
   and it describes a range of MmSizeOfNonPagedPoolInBytes bytes.

   Expansion nonpaged pool starts at an address described by the variable called MmNonPagedPoolExpansionStart,
   and it goes on for MmMaximumNonPagedPoolInBytes minus MmSizeOfNonPagedPoolInBytes bytes,
   always reaching MmNonPagedPoolEnd (because of the way it's calculated) at 0xFFBE0000.

   Initial nonpaged pool is allocated and mapped early-on during boot, but what about the expansion nonpaged pool?
   It is instead composed of special pages which belong to what are called System PTEs.
   These PTEs are the matter of a later discussion, but they are also considered part of the "nonpaged" OS,
   due to the fact that they are never paged out -- once an address is described by a System PTE,
   it is always valid, until the System PTE is torn down.

   System PTEs are actually composed of two "spaces", the system space proper, and the nonpaged pool expansion space.
   The latter, as we've already seen, begins at MmNonPagedPoolExpansionStart.
   Based on the number of System PTEs that the system will support,
   the remaining address space below this address is used to hold the system space PTEs.
   This address, in turn, is held in the variable named MmNonPagedSystemStart,
   which itself is never allowed to go below 0xEB000000 (thus creating an upper bound on the number of System PTEs).

   This means that 330MB are reserved for total nonpaged system VA,
   on top of whatever the initial nonpaged pool allocation is.

   The following URLs, valid as of April 23rd, 2008, support this evidence:
   http://www.cs.miami.edu/~burt/journal/NT/memory.html
   http://www.ditii.com/2007/09/28/windows-memory-management-x86-virtual-address-space/
*/
PVOID MmNonPagedSystemStart;
PVOID MmNonPagedPoolStart;
PVOID MmNonPagedPoolExpansionStart;
PVOID MmNonPagedPoolEnd = MI_NONPAGED_POOL_END;

/* This is where paged pool starts by default */
PVOID MmPagedPoolStart = MI_PAGED_POOL_START;
PVOID MmPagedPoolEnd;

/* And this is its default size */
SIZE_T MmSizeOfPagedPoolInBytes = MI_MIN_INIT_PAGED_POOLSIZE;
PFN_NUMBER MmSizeOfPagedPoolInPages = (MI_MIN_INIT_PAGED_POOLSIZE / PAGE_SIZE);

/* Session space starts at 0xBFFFFFFF and grows downwards
   By default, it includes an 8MB image area where we map win32k and video card drivers,
   followed by a 4MB area containing the session's working set.
   This is then followed by a 20MB mapped view area and finally by the session's paged pool, by default 16MB.

   On a normal system, this results in session space occupying the region from 0xBD000000 to 0xC0000000

   See miarm.h for the defines that determine the sizing of this region.
   On an NT system, some of these can be configured through the registry, but we don't support that yet.
*/
PVOID MiSessionSpaceEnd;    // 0xC0000000
PVOID MiSessionImageEnd;    // 0xC0000000
PVOID MiSessionImageStart;  // 0xBF800000
PVOID MiSessionSpaceWs;
PVOID MiSessionViewStart;   // 0xBE000000
PVOID MiSessionPoolEnd;     // 0xBE000000
PVOID MiSessionPoolStart;   // 0xBD000000
PVOID MmSessionBase;        // 0xBD000000
SIZE_T MmSessionSize;
SIZE_T MmSessionViewSize;
SIZE_T MmSessionPoolSize;
SIZE_T MmSessionImageSize;

/* These are the PTE addresses of the boundaries carved out above */
PMMPTE MiSessionImagePteStart;
PMMPTE MiSessionImagePteEnd;
PMMPTE MiSessionBasePte;
PMMPTE MiSessionLastPte;

/* The system view space, on the other hand, is where sections that are memory mapped into "system space" end up.
   By default, it is a 16MB region, but we hack it to be 32MB for ReactOS
*/
PVOID MiSystemViewStart;
SIZE_T MmSystemViewSize;

#if (_MI_PAGING_LEVELS <= 3)
  /* A copy of the system page directory (the page directory associated with the
     System process) is kept (double-mapped) by the manager in order to lazily
     map paged pool PDEs into external processes when they fault on a paged pool
     address.
  */
  PFN_NUMBER MmSystemPageDirectory[PD_COUNT];
  PMMPDE MmSystemPagePtes;
#endif

/* The system cache starts right after hyperspace.
   The first few pages are for keeping track of the system working set list.
   This should be 0xC0C00000 -- the cache itself starts at 0xC1000000
*/
PMMWSL MmSystemCacheWorkingSetList = (PVOID)MI_SYSTEM_CACHE_WS_START;

/* Windows NT seems to choose between 7000, 11000 and 50000
   On systems with more than 32MB, this number is then doubled, and further aligned up to a PDE boundary (4MB).
*/
PFN_COUNT MmNumberOfSystemPtes;

/* This is how many pages the PFN database will take up
   In Windows, this includes the Quark Color Table, but not in ARM3
*/
PFN_NUMBER MxPfnAllocation;

/* Unlike the old ReactOS Memory Manager,
   ARM3 (and Windows) does not keep track of pages that are not actually valid physical memory,
   such as ACPI reserved regions, BIOS address ranges, or holes in physical memory address space
   which could indicate device-mapped I/O memory.

   In fact, the lack of a PFN entry for a page usually indicates that this is I/O space instead.

   A bitmap, called the PFN bitmap, keeps track of all page frames by assigning a bit to each.
   If the bit is set, then the page is valid physical RAM.
*/
RTL_BITMAP MiPfnBitMap;

/* This structure describes the different pieces of RAM-backed address space */
PPHYSICAL_MEMORY_DESCRIPTOR MmPhysicalMemoryBlock;

/* This is where we keep track of the most basic physical layout markers */
PFN_NUMBER MmLowestPhysicalPage = -1;
PFN_NUMBER MmHighestPhysicalPage = -1;
PFN_COUNT MmNumberOfPhysicalPages;

/* The total number of pages mapped by the boot loader, which include the kernel HAL, boot drivers,
   registry, NLS files and other loader data structures is kept track of here.
   This depends on "LoaderPagesSpanned" being correct when coming from the loader.

   This number is later aligned up to a PDE boundary.
*/
SIZE_T MmBootImageSize;

/* These three variables keep track of the core separation of address space
   that exists between kernel mode and user mode.
*/
ULONG_PTR MmUserProbeAddress;
PVOID MmHighestUserAddress;
PVOID MmSystemRangeStart;

/* And these store the respective highest PTE/PDE address */
PMMPTE MiHighestUserPte;
PMMPDE MiHighestUserPde;

/* These variables define the system cache address space */
PVOID MmSystemCacheStart = (PVOID)MI_SYSTEM_CACHE_START;
PVOID MmSystemCacheEnd;
PFN_NUMBER MmSizeOfSystemCacheInPages;
MMSUPPORT MmSystemCacheWs;
ULONG MmSystemCacheDirtyPageThreshold = 0;

/* This is where hyperspace ends (followed by the system cache working set) */
PVOID MmHyperSpaceEnd;

/* Page coloring algorithm data */
ULONG MmSecondaryColors;
ULONG MmSecondaryColorMask;

/* Actual (registry-configurable) size of a GUI thread's stack */
ULONG MmLargeStackSize = KERNEL_LARGE_STACK_SIZE;

/* Before we have a PFN database, memory comes straight from our physical memory blocks,
   which is nice because it's guaranteed contiguous and also because once we take a page from here,
   the system doesn't see it anymore.
   However, once the fun is over, those pages must be re-integrated back into PFN society life,
   and that requires us keeping a copy of the original layout so that we can parse it later.
*/
PMEMORY_ALLOCATION_DESCRIPTOR MxFreeDescriptor;
MEMORY_ALLOCATION_DESCRIPTOR MxOldFreeDescriptor;

/* For each page's worth bytes of L2 cache in a given set/way line,
   the zero and free lists are organized in what is called a "color".

   This array points to the two lists,
   so it can be thought of as a multi-dimensional array of MmFreePagesByColor[2][MmSecondaryColors].
   Since the number is dynamic, we describe the array in pointer form instead.

   On a final note, the color tables themselves are right after the PFN database.
*/
C_ASSERT(FreePageList == 1);
PMMCOLOR_TABLES MmFreePagesByColor[FreePageList + 1];

/* An event used in Phase 0 before the rest of the system is ready to go */
KEVENT MiTempEvent;

/* All the events used for memory threshold notifications */
PKEVENT MiLowMemoryEvent;
PKEVENT MiHighMemoryEvent;
PKEVENT MiLowPagedPoolEvent;
PKEVENT MiHighPagedPoolEvent;
PKEVENT MiLowNonPagedPoolEvent;
PKEVENT MiHighNonPagedPoolEvent;

/* The actual thresholds themselves, in page numbers */
PFN_NUMBER MmLowMemoryThreshold;
PFN_NUMBER MmHighMemoryThreshold;
PFN_NUMBER MiLowPagedPoolThreshold;
PFN_NUMBER MiHighPagedPoolThreshold;
PFN_NUMBER MiLowNonPagedPoolThreshold;
PFN_NUMBER MiHighNonPagedPoolThreshold;

/* This number determines how many free pages must exist, at minimum,
   until we start trimming working sets and flushing modified pages to obtain more free pages.

   This number changes if the system detects that this is a server product
*/
PFN_NUMBER MmMinimumFreePages = 26;

/* This number indicates how many pages we consider to be a low limit of having "plenty" of free memory.
   It is doubled on systems that have more than 63MB of memory
*/
PFN_NUMBER MmPlentyFreePages = 400;

/* These values store the type of system this is (small, med, large) and if server */
ULONG MmProductType;
MM_SYSTEMSIZE MmSystemSize;

/* These values store the cache working set minimums and maximums, in pages

   The minimum value is boosted on systems with more than 24MB of RAM,
   and cut down to only 32 pages on embedded (<24MB RAM) systems.

   An extra boost of 2MB is given on systems with more than 33MB of RAM.
*/
PFN_NUMBER MmSystemCacheWsMinimum = 288;
PFN_NUMBER MmSystemCacheWsMaximum = 350;

/* FIXME: Move to cache/working set code later */
BOOLEAN MmLargeSystemCache;

/* This value determines in how many fragments/chunks the subsection prototype PTEs
   should be allocated when mapping a section object.
   It is configurable in the registry through the MapAllocationFragment parameter.

   The default is 64KB on systems with more than 1GB of RAM,
   32KB on systems with more than 256MB of RAM, and 16KB on systems with less than 256MB of RAM.

   The maximum it can be set to is 2MB, and the minimum is 4KB.
*/
SIZE_T MmAllocationFragment;

/* These two values track how much virtual memory can be committed, and when expansion should happen. */
// FIXME: They should be moved elsewhere since it's not an "init" setting?
SIZE_T MmTotalCommitLimit;
SIZE_T MmTotalCommitLimitMaximum;

/* These values tune certain user parameters. They have default values set here,
   as well as in the code, and can be overwritten by registry settings.
*/
SIZE_T MmHeapSegmentReserve = 1 * _1MB;
SIZE_T MmHeapSegmentCommit = 2 * PAGE_SIZE;
SIZE_T MmHeapDeCommitTotalFreeThreshold = 64 * _1KB;
SIZE_T MmHeapDeCommitFreeBlockThreshold = PAGE_SIZE;
SIZE_T MmMinimumStackCommitInBytes = 0;

/* Internal setting used for debugging memory descriptors */
BOOLEAN MiDbgEnableMdDump = FALSE;

/* Number of memory descriptors in the loader block */
ULONG MiNumberDescriptors = 0;

/* Number of free pages in the loader block */
PFN_NUMBER MiNumberOfFreePages = 0;

/* Timeout value for critical sections (2.5 minutes) */
ULONG MmCritsectTimeoutSeconds = 150; // NT value: 720 * 60 * 60; (30 days)
LARGE_INTEGER MmCriticalSectionTimeout;
LARGE_INTEGER MmShortTime  = {{(-10 * 10000), -1}};  // 10 ms
LARGE_INTEGER MmHalfSecond = {{(-500 * 10000), -1}}; // 0.5 second
LARGE_INTEGER MmOneSecond = {{(-1000 * 10000), -1}}; // 1 second

/* Throttling limits for Cc (in pages)
   Above top, we don't throttle
   Above bottom, we throttle depending on the amount of modified pages
   Otherwise, we throttle!
*/
ULONG MmThrottleTop;
ULONG MmThrottleBottom;

/* If "/3GB" key in the "Boot.ini" - user address spaces enlarge up to 3 GB. */
ULONG MmVirtualBias = 0;

/* For debugging */
PKTHREAD MmPfnOwner;

/* The maximal index in a Vad bitmap array (MI_VAD_BITMAP). */
ULONG MiLastVadBit = 1;

/* The read cluster size */
ULONG MmReadClusterSize = 7;

/* The page support stack S-LIST */
SLIST_HEADER MmInPageSupportSListHead;

extern MMPFNLIST MmStandbyPageListByPriority[8];
extern LIST_ENTRY MmLoadedUserImageList;
extern KGUARDED_MUTEX MmPagedPoolMutex;
extern KGUARDED_MUTEX MmSectionCommitMutex;
extern KGUARDED_MUTEX MmSectionBasedMutex;
extern KMUTANT MmSystemLoadLock;
extern KEVENT MmZeroingPageEvent;
extern KEVENT MmCollidedFlushEvent;
extern SLIST_HEADER MmDeadStackSListHead;
extern SLIST_HEADER MmInPageSupportSListHead;
extern PVOID MmHighSectionBase;
extern ULONG MmSpecialPoolTag;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
MmDumpArmPfnDatabase(
    _In_ BOOLEAN StatusOnly)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
PPHYSICAL_MEMORY_DESCRIPTOR
NTAPI
MmInitializeMemoryLimits(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PBOOLEAN IncludeType)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

INIT_FUNCTION
VOID
NTAPI
MmFreeLoaderBlock(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
MiDbgDumpMemoryDescriptors(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MiScanMemoryDescriptors(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;
    PFN_NUMBER PageFrameIndex;
    PFN_NUMBER FreePages = 0;
    PLIST_ENTRY ListEntry;

    /* Loop the memory descriptors */
    for (ListEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
         ListEntry != &LoaderBlock->MemoryDescriptorListHead;
         ListEntry = ListEntry->Flink)
    {
        /* Get the descriptor */
        Descriptor = CONTAINING_RECORD(ListEntry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

        DPRINT("MD Type: %lx Base: %lx Count: %lx\n",
               Descriptor->MemoryType, Descriptor->BasePage, Descriptor->PageCount);

        /* Count this descriptor */
        MiNumberDescriptors++;

        /* Check if this is invisible memory */
        if ((Descriptor->MemoryType == LoaderFirmwarePermanent) ||
            (Descriptor->MemoryType == LoaderSpecialMemory) ||
            (Descriptor->MemoryType == LoaderHALCachedMemory) ||
            (Descriptor->MemoryType == LoaderBBTMemory))
        {
            continue; /* Skip this descriptor */
        }

        /* Check if this is bad memory */
        if (Descriptor->MemoryType != LoaderBad)
            /* Count this in the total of pages */
            MmNumberOfPhysicalPages += (PFN_COUNT)Descriptor->PageCount;

        /* Check if this is the new lowest page */
        if (Descriptor->BasePage < MmLowestPhysicalPage)
            /* Update the lowest page */
            MmLowestPhysicalPage = Descriptor->BasePage;

        /* Check if this is the new highest page */
        PageFrameIndex = (Descriptor->BasePage + Descriptor->PageCount);

        if (PageFrameIndex > MmHighestPhysicalPage)
            /* Update the highest page */
            MmHighestPhysicalPage = (PageFrameIndex - 1);

        /* Check if this is free memory */
        if ((Descriptor->MemoryType == LoaderFree) ||
            (Descriptor->MemoryType == LoaderLoadedProgram) ||
            (Descriptor->MemoryType == LoaderFirmwareTemporary) ||
            (Descriptor->MemoryType == LoaderOsloaderStack))
        {
            /* Count it too free pages */
            MiNumberOfFreePages += Descriptor->PageCount;

            /* Check if this is the largest memory descriptor */
            if (Descriptor->PageCount > FreePages)
            {
                /* Remember it */
                MxFreeDescriptor = Descriptor;
                FreePages = Descriptor->PageCount;
            }
        }
    }

    /* Save original values of the free descriptor, since it'll be altered by early allocations */
    MxOldFreeDescriptor = *MxFreeDescriptor;
}

INIT_FUNCTION
VOID
NTAPI
MiComputeColorInformation(VOID)
{
    ULONG L2Associativity;

    /* Check if no setting was provided already */
    if (!MmSecondaryColors)
    {
        /* Get L2 cache information */
        L2Associativity = KeGetPcr()->SecondLevelCacheAssociativity;

        /* The number of colors is the number of cache bytes by set/way */
        MmSecondaryColors = KeGetPcr()->SecondLevelCacheSize;

        if (L2Associativity)
            MmSecondaryColors /= L2Associativity;
    }

    /* Now convert cache bytes into pages */
    MmSecondaryColors >>= PAGE_SHIFT;

    if (!MmSecondaryColors)
    {
        /* If there was no cache data from the KPCR, use the default colors */
        MmSecondaryColors = MI_SECONDARY_COLORS;
    }
    else
    {
        /* Otherwise, make sure there aren't too many colors */
        if (MmSecondaryColors > MI_MAX_SECONDARY_COLORS)
            /* Set the maximum */
            MmSecondaryColors = MI_MAX_SECONDARY_COLORS;

        /* Make sure there aren't too little colors */
        if (MmSecondaryColors < MI_MIN_SECONDARY_COLORS)
            /* Set the default */
            MmSecondaryColors = MI_SECONDARY_COLORS;

        /* Finally make sure the colors are a power of two */
        if (MmSecondaryColors & (MmSecondaryColors - 1))
            /* Set the default */
            MmSecondaryColors = MI_SECONDARY_COLORS;
    }

    /* Compute the mask and store it */
    MmSecondaryColorMask = (MmSecondaryColors - 1);
    KeGetCurrentPrcb()->SecondaryColorMask = MmSecondaryColorMask;
}

INIT_FUNCTION
PFN_NUMBER
NTAPI
MxGetNextPage(
    _In_ PFN_NUMBER PageCount)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

INIT_FUNCTION
BOOLEAN
NTAPI
MmArmInitSystem(
    _In_ ULONG Phase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    BOOLEAN IncludeType[LoaderMaximum];
    //PPHYSICAL_MEMORY_RUN Run;
    PFN_NUMBER PageCount;
    //PVOID Bitmap;
    ULONG SystemCacheSizeInPages;
    ULONG ix;
  #if defined(_X86_)
    ULONG BiasPages;
  #endif
  #if DBG
    PMMPTE TestPte;
    PMMPTE Pte;
    MMPTE TempPte;
    ULONG jx;

    DPRINT1("MmArmInitSystem: Phase %X, LoaderBlock %X\n", Phase, LoaderBlock);

    /* Dump memory descriptors */
    if (MiDbgEnableMdDump)
        MiDbgDumpMemoryDescriptors();
  #endif

    /* Instantiate memory that we don't consider RAM/usable.
       We use the same exclusions that Windows does, in order to try to be compatible with WinLDR-style booting.
    */
    for (ix = 0; ix < LoaderMaximum; ix++)
        IncludeType[ix] = TRUE;

    IncludeType[LoaderBad] = FALSE;
    IncludeType[LoaderFirmwarePermanent] = FALSE;
    IncludeType[LoaderSpecialMemory] = FALSE;
    IncludeType[LoaderBBTMemory] = FALSE;

    if (Phase == 0)
    {
        /* Count physical pages on the system */
        MiScanMemoryDescriptors(LoaderBlock);

        /* Initialize the phase 0 temporary event */
        KeInitializeEvent(&MiTempEvent, NotificationEvent, FALSE);

        /* Set all the events to use the temporary event for now */
        MiLowMemoryEvent = &MiTempEvent;
        MiHighMemoryEvent = &MiTempEvent;
        MiLowPagedPoolEvent = &MiTempEvent;
        MiHighPagedPoolEvent = &MiTempEvent;
        MiLowNonPagedPoolEvent = &MiTempEvent;
        MiHighNonPagedPoolEvent = &MiTempEvent;

        /* Default throttling limits for Cc.
           May be ajusted later on depending on system type.
        */
        MmThrottleTop = 450;
        MmThrottleBottom = 127;

        /* Define the basic user vs. kernel address space separation */
        MmSystemRangeStart = (PVOID)MI_DEFAULT_SYSTEM_RANGE_START;
        MmUserProbeAddress = (ULONG_PTR)MI_USER_PROBE_ADDRESS;
        MmHighestUserAddress = (PVOID)MI_HIGHEST_USER_ADDRESS;

        /* Highest PTE and PDE based on the addresses above */
        MiHighestUserPte = MiAddressToPte(MmHighestUserAddress);
        MiHighestUserPde = MiAddressToPde(MmHighestUserAddress);

        /* Get the size of the boot loader's image allocations and then round that region up to a PDE size,
           so that any PDEs we might create for whatever follows are separate from the PDEs
           that boot loader might've already created (and later, we can blow all that away if we want to).
        */
        MmBootImageSize = KeLoaderBlock->Extension->LoaderPagesSpanned;
        MmBootImageSize *= PAGE_SIZE;
        MmBootImageSize = ((MmBootImageSize + (PDE_MAPPED_VA - 1)) & ~(PDE_MAPPED_VA - 1));

        ASSERT((MmBootImageSize % PDE_MAPPED_VA) == 0);

      #if defined(_X86_)
        /* Enlargement user address space not yet implemented. */
        MmVirtualBias = LoaderBlock->u.I386.VirtualBias;
        ASSERT(MmVirtualBias == 0);
      #endif

        /* Initialize session space address layout */
        MiInitializeSessionSpaceLayout();

        /* Set the based section highest address */
        MmHighSectionBase = (PVOID)((ULONG_PTR)MmHighestUserAddress - 0x800000);

        /* Calculate size of system cache */
        SystemCacheSizeInPages = ((ULONG_PTR)MI_PAGED_POOL_START - (ULONG_PTR)MmSystemCacheStart);
        SystemCacheSizeInPages /= PAGE_SIZE;

      #if defined(_X86_)
        if (MmSizeOfPagedPoolInBytes == (SIZE_T)(-1) && MmVirtualBias == 0)
        {
            /* The registry has requested the maximum possible size of the paged pool.
               Increase paged pool size due to the size of the system cache.
            */
            BiasPages = (SystemCacheSizeInPages / 3 + (PTE_PER_PAGE - 1)) & ~(PTE_PER_PAGE - 1);
            SystemCacheSizeInPages -= BiasPages;

            MmPagedPoolStart = (PVOID)((ULONG_PTR)MmPagedPoolStart - (BiasPages * PAGE_SIZE));
        }

        /* Setting variables if enabled bias */
        if (MmVirtualBias)
        {
            /* Not yet implemented. FIXME. */
            ASSERT(MmVirtualBias == 0);
        }        
      #endif

      #if DBG

        /* Prototype PTEs are assumed to be in paged pool, so check if the math works */
        Pte = (PMMPTE)MmPagedPoolStart;
        MI_MAKE_PROTOTYPE_PTE(&TempPte, Pte);
        TestPte = MiGetProtoPtr(&TempPte);
        ASSERT(Pte == TestPte);

        /* Try the last nonpaged pool address */
        Pte = (PMMPTE)MI_NONPAGED_POOL_END;
        MI_MAKE_PROTOTYPE_PTE(&TempPte, Pte);
        TestPte = MiGetProtoPtr(&TempPte);
        ASSERT(Pte == TestPte);

        /* Try a bunch of random addresses near the end of the address space */
        Pte = (PMMPTE)((ULONG_PTR)MI_HIGHEST_SYSTEM_ADDRESS - 0x37FFF);

        for (jx = 0; jx < 20; jx += 1)
        {
            MI_MAKE_PROTOTYPE_PTE(&TempPte, Pte);
            TestPte = MiGetProtoPtr(&TempPte);
            ASSERT(Pte == TestPte);
            Pte++;
        }

        /* Subsection PTEs are always in nonpaged pool, pick a random address to try */
        Pte = (PMMPTE)((ULONG_PTR)MI_NONPAGED_POOL_END - _1MB);
        MI_MAKE_SUBSECTION_PTE(&TempPte, Pte);
        TestPte = MiSubsectionPteToSubsection(&TempPte);
        ASSERT(Pte == TestPte);

      #endif

        /* Loop all 8 standby lists */
        for (ix = 0; ix < 8; ix++)
        {
            /* Initialize them */
            MmStandbyPageListByPriority[ix].Total = 0;
            MmStandbyPageListByPriority[ix].ListName = StandbyPageList;
            MmStandbyPageListByPriority[ix].Flink = MM_EMPTY_LIST;
            MmStandbyPageListByPriority[ix].Blink = MM_EMPTY_LIST;
        }

        /* Initialize the user mode image list */
        InitializeListHead(&MmLoadedUserImageList);

        /* Initialize critical section timeout value (relative time is negative) */
        MmCriticalSectionTimeout.QuadPart = MmCritsectTimeoutSeconds * (-10000000LL);

        /* Initialize the paged pool mutex and the section commit mutex */
        KeInitializeGuardedMutex(&MmPagedPoolMutex);
        KeInitializeGuardedMutex(&MmSectionCommitMutex);
        KeInitializeGuardedMutex(&MmSectionBasedMutex);

        /* Initialize the Loader Lock */
        KeInitializeMutant(&MmSystemLoadLock, FALSE);

        /* Set up the zero page event */
        KeInitializeEvent(&MmZeroingPageEvent, NotificationEvent, FALSE);
        KeInitializeEvent(&MmCollidedFlushEvent, NotificationEvent, FALSE);

        /* Initialize the dead stack S-LIST */
        InitializeSListHead(&MmDeadStackSListHead);

        /* Initialize the page support stack S-LIST */
        InitializeSListHead(&MmInPageSupportSListHead);

        /* Check if this is a machine with less than 19MB of RAM */
        PageCount = MmNumberOfPhysicalPages;

        if (PageCount < MI_MIN_PAGES_FOR_SYSPTE_TUNING)
        {
            /* Use the very minimum of system PTEs */
            MmNumberOfSystemPtes = 7000;
        }
        else
        {
            /* Use the default */
            MmNumberOfSystemPtes = 11000;

            if (PageCount > MI_MIN_PAGES_FOR_SYSPTE_BOOST)
            {
                /* Double the amount of system PTEs */
                MmNumberOfSystemPtes <<= 1;
            }
            if (PageCount > MI_MIN_PAGES_FOR_SYSPTE_BOOST_BOOST)
            {
                /* Double the amount of system PTEs */
                MmNumberOfSystemPtes <<= 1;
            }
            if (MmSpecialPoolTag != 0 && MmSpecialPoolTag != -1)
            {
                /* Add some extra PTEs for special pool */
                MmNumberOfSystemPtes += 0x6000;
            }
        }

        DPRINT("System PTE count has been tuned to %lu (%lu bytes)\n",
               MmNumberOfSystemPtes, (MmNumberOfSystemPtes * PAGE_SIZE));

        /* Check if no values are set for the heap limits */
        if (!MmHeapSegmentReserve)
            MmHeapSegmentReserve = 2 * _1MB;

        if (!MmHeapSegmentCommit)
            MmHeapSegmentCommit = 2 * PAGE_SIZE;

        if (MmHeapDeCommitTotalFreeThreshold == 0)
            MmHeapDeCommitTotalFreeThreshold = 64 * _1KB;

        if (MmHeapDeCommitFreeBlockThreshold == 0)
            MmHeapDeCommitFreeBlockThreshold = PAGE_SIZE;

        /* Initialize the working set lock */
        ExInitializePushLock(&MmSystemCacheWs.WorkingSetMutex);

        /* Set commit limit */
        MmTotalCommitLimit = (2 * _1GB) >> PAGE_SHIFT;
        MmTotalCommitLimitMaximum = MmTotalCommitLimit;

        /* Has the allocation fragment been setup? */
        if (!MmAllocationFragment)
        {
            /* Use the default value */
            MmAllocationFragment = MI_ALLOCATION_FRAGMENT;

            if (PageCount < ((256 * _1MB) / PAGE_SIZE))
            {
                /* On memory systems with less than 256MB, divide by 4 */
                MmAllocationFragment = MI_ALLOCATION_FRAGMENT / 4;
            }
            else if (PageCount < (_1GB / PAGE_SIZE))
            {
                /* On systems with less than 1GB, divide by 2 */
                MmAllocationFragment = MI_ALLOCATION_FRAGMENT / 2;
            }
        }
        else
        {
            /* Convert from 1KB fragments to pages */
            MmAllocationFragment *= _1KB;
            MmAllocationFragment = ROUND_TO_PAGES(MmAllocationFragment);

            /* Don't let it past the maximum */
            MmAllocationFragment = min(MmAllocationFragment, MI_MAX_ALLOCATION_FRAGMENT);

            /* Don't let it too small either */
            MmAllocationFragment = max(MmAllocationFragment, MI_MIN_ALLOCATION_FRAGMENT);
        }

        /* Check for kernel stack size that's too big */
        if (MmLargeStackSize > (KERNEL_LARGE_STACK_SIZE / _1KB))
        {
            /* Sanitize to default value */
            MmLargeStackSize = KERNEL_LARGE_STACK_SIZE;
        }
        else
        {
            /* Take the registry setting, and convert it into bytes */
            MmLargeStackSize *= _1KB;

            /* Now align it to a page boundary */
            MmLargeStackSize = PAGE_ROUND_UP(MmLargeStackSize);

            /* Sanity checks */
            ASSERT(MmLargeStackSize <= KERNEL_LARGE_STACK_SIZE);
            ASSERT((MmLargeStackSize & (PAGE_SIZE - 1)) == 0);

            /* Make sure it's not too low */
            if (MmLargeStackSize < KERNEL_STACK_SIZE)
                MmLargeStackSize = KERNEL_STACK_SIZE;
        }

        /* Compute color information (L2 cache-separated paging lists) */
        MiComputeColorInformation();

        /* Calculate the number of bytes for the PFN database then add the color tables and convert to pages */
        MxPfnAllocation = (MmHighestPhysicalPage + 1) * sizeof(MMPFN);
        MxPfnAllocation += (MmSecondaryColors * sizeof(MMCOLOR_TABLES) * 2);
        MxPfnAllocation >>= PAGE_SHIFT;

        /* We have to add one to the count here, because in the process of shifting down to the page size,
           we actually ended up getting the lower aligned size (so say, 0x5FFFF bytes is now 0x5F pages).
           Later on, we'll shift this number back into bytes,
           which would cause us to end up with only 0x5F000 bytes -- when we actually want to have 0x60000 bytes.
        */
        MxPfnAllocation++;

        /* Initialize the platform-specific parts */
        MiInitMachineDependent(LoaderBlock);

        ASSERT(FALSE);if(IncludeType[LoaderBad]){;}

    }
    else if (Phase == 2)
    {
        DPRINT1("MmInitSystem: FIXME MiEnablePagingTheExecutive\n");
        //MiEnablePagingTheExecutive();
        return TRUE;
    }
    else
    {
        ASSERT(Phase == 0 || Phase == 2);
    }

    /* Always return success for now */
    return TRUE;
}

/* EOF */
