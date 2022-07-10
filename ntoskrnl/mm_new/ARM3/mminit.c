
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

/* The read cluster size */
ULONG MmReadClusterSize = 7;

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
BOOLEAN
NTAPI
MmArmInitSystem(
    _In_ ULONG Phase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

/* EOF */
