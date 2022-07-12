
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include <mm_new/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

/* FIXME: These should be PTE_GLOBAL */
MMPTE ValidKernelPde = {{ PTE_VALID | PTE_READWRITE | PTE_DIRTY | PTE_ACCESSED }};
MMPTE ValidKernelPte = {{ PTE_VALID | PTE_READWRITE | PTE_DIRTY | PTE_ACCESSED }};

/* The same, but for local pages */
MMPTE ValidKernelPdeLocal = {{ PTE_VALID | PTE_READWRITE | PTE_DIRTY | PTE_ACCESSED }};
MMPTE ValidKernelPteLocal = {{ PTE_VALID | PTE_READWRITE | PTE_DIRTY | PTE_ACCESSED }};

extern PMMCOLOR_TABLES MmFreePagesByColor[FreePageList + 1];
extern PMEMORY_ALLOCATION_DESCRIPTOR MxFreeDescriptor;
extern MEMORY_ALLOCATION_DESCRIPTOR MxOldFreeDescriptor;
extern PMM_SESSION_SPACE MmSessionSpace;
extern PMMPTE MiSessionImagePteStart;
extern PMMPTE MiSessionImagePteEnd;
extern PMMPTE MiSessionBasePte;
extern PMMPTE MiSessionLastPte;
extern PVOID MmSessionBase;
extern PVOID MiSessionImageStart;
extern PVOID MiSessionImageEnd;
extern PVOID MiSessionPoolEnd;     // 0xBE000000
extern PVOID MiSessionPoolStart;   // 0xBD000000
extern PVOID MiSessionViewStart;   // 0xBE000000
extern PVOID MiSessionSpaceWs;
extern PVOID MiSessionSpaceEnd;
extern SIZE_T MmSessionSize;
extern SIZE_T MmSessionImageSize;
extern SIZE_T MmSessionViewSize;
extern SIZE_T MmSessionPoolSize;
extern SIZE_T MmSystemViewSize;
extern PVOID MiSystemViewStart;
extern PFN_NUMBER MiNumberOfFreePages;
extern PVOID MmNonPagedPoolStart;
extern SIZE_T MmMaximumNonPagedPoolInBytes;
extern SIZE_T MmSizeOfNonPagedPoolInBytes;
extern ULONG MmNumberOfSystemPtes;
extern PVOID MmNonPagedSystemStart;
extern PVOID MmNonPagedPoolExpansionStart;
extern SIZE_T MmSizeOfPagedPoolInBytes;
extern ULONG_PTR MxPfnAllocation;
extern PFN_NUMBER MmMaximumNonPagedPoolInPages;
extern PFN_NUMBER MmAvailablePages;
extern PMMPTE MmFirstReservedMappingPte;
extern PMMPTE MmLastReservedMappingPte;
extern PMMPTE MiFirstReservedZeroingPte;
extern PMMWSL MmWorkingSetList;
extern ULONG MiLastVadBit;
extern ULONG MmSecondaryColors;

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
MiInitializeSessionSpaceLayout(VOID)
{
    /* Set the size of session view, pool, and image */
    MmSessionSize = MI_SESSION_SIZE;
    MmSessionViewSize = MI_SESSION_VIEW_SIZE;
    MmSessionPoolSize = MI_SESSION_POOL_SIZE;
    MmSessionImageSize = MI_SESSION_IMAGE_SIZE;

    /* Set the size of system view */
    MmSystemViewSize = MI_SYSTEM_VIEW_SIZE;

    /* This is where it all ends */
    MiSessionImageEnd = (PVOID)PTE_BASE;

    /* This is where we will load Win32k.sys and the video driver */
    MiSessionImageStart = (PVOID)((ULONG_PTR)MiSessionImageEnd - MmSessionImageSize);

    /* So the view starts right below the session working set (itself below the image area) */
    MiSessionViewStart = (PVOID)((ULONG_PTR)MiSessionImageEnd -
                                 MmSessionImageSize -
                                 MI_SESSION_WORKING_SET_SIZE -
                                 MmSessionViewSize);
    /* Session pool follows */
    MiSessionPoolEnd = MiSessionViewStart;
    MiSessionPoolStart = (PVOID)((ULONG_PTR)MiSessionPoolEnd - MmSessionPoolSize);

    /* And it all begins here */
    MmSessionBase = MiSessionPoolStart;

    /* Sanity check that our math is correct */
    ASSERT((ULONG_PTR)MmSessionBase + MmSessionSize == PTE_BASE);

    /* Session space ends wherever image session space ends */
    MiSessionSpaceEnd = MiSessionImageEnd;

    /* System view space ends at session space, so now that we know where this is,
       we can compute the base address of system view space itself.
    */
    MiSystemViewStart = (PVOID)((ULONG_PTR)MmSessionBase - MmSystemViewSize);

    /* Compute the PTE addresses for all the addresses we carved out */
    MiSessionImagePteStart = MiAddressToPte(MiSessionImageStart);
    MiSessionImagePteEnd = MiAddressToPte(MiSessionImageEnd);
    MiSessionBasePte = MiAddressToPte(MmSessionBase);
    MiSessionSpaceWs = (PVOID)((ULONG_PTR)MiSessionViewStart + MmSessionViewSize);
    MiSessionLastPte = MiAddressToPte(MiSessionSpaceEnd);

    /* Initialize session space */
    MmSessionSpace = (PMM_SESSION_SPACE)((ULONG_PTR)MmSessionBase +
                                         MmSessionSize -
                                         MmSessionImageSize -
                                         MM_ALLOCATION_GRANULARITY);
}

INIT_FUNCTION
VOID
NTAPI
MiComputeNonPagedPoolVa(
    _In_ ULONG FreePages)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
MiMapPfnDatabase(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
MiInitializeColorTables(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
MiInitializePfnDatabase(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
NTSTATUS
NTAPI
MiInitMachineDependent(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PVOID NonPagedPoolExpansionVa;
    PFN_NUMBER PageFrameIndex;
    PMMPTE StartPde;
    PMMPTE EndPde;
    PMMPTE Pte;
    PMMPTE LastPte;
    MMPTE TempPde;
    MMPTE TempPte;
    PMMPFN Pfn;
    SIZE_T NonPagedSystemSize;
    ULONG Flags;
    KIRQL OldIrql;

  #if defined(_GLOBAL_PAGES_ARE_AWESOME_)
    /* Check for global bit */
    if (KeFeatureBits & KF_GLOBAL_PAGE)
    {
        /* Set it on the template PTE and PDE */
        ValidKernelPte.u.Hard.Global = TRUE;
        ValidKernelPde.u.Hard.Global = TRUE;
    }
  #endif

    /* Now templates are ready */
    TempPte = ValidKernelPte;
    TempPde = ValidKernelPde;

    /* Set CR3 for the system process */
    Pte = MiAddressToPde(PDE_BASE);
    PageFrameIndex = (PFN_FROM_PTE(Pte) << PAGE_SHIFT);
    PsGetCurrentProcess()->Pcb.DirectoryTableBase[0] = PageFrameIndex;

    /* Blow away user-mode */
    StartPde = MiAddressToPde(0);
    EndPde = MiAddressToPde(KSEG0_BASE);
    RtlZeroMemory(StartPde, ((EndPde - StartPde) * sizeof(MMPTE)));

    /* Compute non paged pool limits and size */
    MiComputeNonPagedPoolVa(MiNumberOfFreePages);

    /* Now calculate the nonpaged pool expansion VA region */
    MmNonPagedPoolStart = (PVOID)((ULONG_PTR)MmNonPagedPoolEnd - MmMaximumNonPagedPoolInBytes + MmSizeOfNonPagedPoolInBytes);
    MmNonPagedPoolStart = (PVOID)PAGE_ALIGN(MmNonPagedPoolStart);

    DPRINT("NP Pool has been tuned to: %lu bytes and %lu bytes\n",
           MmSizeOfNonPagedPoolInBytes, MmMaximumNonPagedPoolInBytes);

    NonPagedPoolExpansionVa = MmNonPagedPoolStart;

    /* Now calculate the nonpaged system VA region,
       which includes the nonpaged pool expansion (above) and the system PTEs.
       Note that it is then aligned to a PDE boundary (4MB).
    */
    NonPagedSystemSize = ((MmNumberOfSystemPtes + 1) * PAGE_SIZE);
    MmNonPagedSystemStart = (PVOID)((ULONG_PTR)MmNonPagedPoolStart - NonPagedSystemSize);
    MmNonPagedSystemStart = (PVOID)((ULONG_PTR)MmNonPagedSystemStart & ~(PDE_MAPPED_VA - 1));

    /* Don't let it go below the minimum */
    if (MmNonPagedSystemStart < (PVOID)0xEB000000)
    {
        /* This is a hard-coded limit in the Windows NT address space */
        MmNonPagedSystemStart = (PVOID)0xEB000000;

        /* Reduce the amount of system PTEs to reach this point */
        MmNumberOfSystemPtes = ((ULONG_PTR)MmNonPagedPoolStart - (ULONG_PTR)MmNonPagedSystemStart) >> PAGE_SHIFT;
        MmNumberOfSystemPtes--;

        ASSERT(MmNumberOfSystemPtes > 1000);
    }

    /* Check if size of the paged pool is so large that it overflows into nonpaged pool */
    if (MmSizeOfPagedPoolInBytes > ((ULONG_PTR)MmNonPagedSystemStart - (ULONG_PTR)MmPagedPoolStart))
    {
        /* We need some recalculations here */
        DPRINT1("Paged pool is too big!\n");
    }

    /* Normally, the PFN database should start after the loader images.
       This is already the case in ReactOS, but for now we want to co-exist with the old memory manager,
       so we'll create a "Shadow PFN Database" instead, and arbitrarly start it at 0xB0000000.
    */
    MmPfnDatabase = (PVOID)0xB0000000;
    ASSERT(((ULONG_PTR)MmPfnDatabase & (PDE_MAPPED_VA - 1)) == 0);

    /* Non paged pool comes after the PFN database */
    MmNonPagedPoolStart = (PVOID)((ULONG_PTR)MmPfnDatabase + (MxPfnAllocation << PAGE_SHIFT));

    /* Now we actually need to get these many physical pages.
       Nonpaged pool is actually also physically contiguous (but not the expansion)
    */
    PageFrameIndex = MxGetNextPage(MxPfnAllocation + (MmSizeOfNonPagedPoolInBytes >> PAGE_SHIFT));
    ASSERT(PageFrameIndex != 0);

    DPRINT("PFN DB PA PFN begins at: %lx\n", PageFrameIndex);
    DPRINT("NP PA PFN begins at: %lx\n", (PageFrameIndex + MxPfnAllocation));

    /* Convert nonpaged pool size from bytes to pages */
    MmMaximumNonPagedPoolInPages = (MmMaximumNonPagedPoolInBytes >> PAGE_SHIFT);

    /* Now we need some pages to create the page tables
       for the NP system VA which includes system PTEs and expansion NP
    */
    StartPde = MiAddressToPde(MmNonPagedSystemStart);
    EndPde = MiAddressToPde((PVOID)((ULONG_PTR)MmNonPagedPoolEnd - 1));

    while (StartPde <= EndPde)
    {
        /* Get a page */
        TempPde.u.Hard.PageFrameNumber = MxGetNextPage(1);
        MI_WRITE_VALID_PTE(StartPde, TempPde);

        /* Zero out the page table */
        Pte = MiPteToAddress(StartPde);
        RtlZeroMemory(Pte, PAGE_SIZE);

        StartPde++;
    }

    /* Now we need pages for the page tables which will map initial NP */
    StartPde = MiAddressToPde(MmPfnDatabase);
    EndPde = MiAddressToPde((PVOID)((ULONG_PTR)MmNonPagedPoolStart + MmSizeOfNonPagedPoolInBytes - 1));
    while (StartPde <= EndPde)
    {
        /* Get a page */
        TempPde.u.Hard.PageFrameNumber = MxGetNextPage(1);
        MI_WRITE_VALID_PTE(StartPde, TempPde);

        /* Zero out the page table */
        Pte = MiPteToAddress(StartPde);
        RtlZeroMemory(Pte, PAGE_SIZE);

        StartPde++;
    }

    MmSubsectionBase = (ULONG_PTR)MmNonPagedPoolStart;

    /* Now remember where the expansion starts */
    MmNonPagedPoolExpansionStart = NonPagedPoolExpansionVa;

    /* Last step is to actually map the nonpaged pool */
    Pte = MiAddressToPte(MmNonPagedPoolStart);
    LastPte = MiAddressToPte((PVOID)((ULONG_PTR)MmNonPagedPoolStart + MmSizeOfNonPagedPoolInBytes - 1));

    while (Pte <= LastPte)
    {
        /* Use one of our contigous pages */
        TempPte.u.Hard.PageFrameNumber = PageFrameIndex++;
        MI_WRITE_VALID_PTE(Pte++, TempPte);
    }

    /* Sanity check: make sure we have properly defined the system PTE space */
    ASSERT(MiAddressToPte(MmNonPagedSystemStart) < MiAddressToPte(MmNonPagedPoolExpansionStart));

    /* Now go ahead and initialize the nonpaged pool */
    MiInitializeNonPagedPool();
    MiInitializeNonPagedPoolThresholds();

    /* Map the PFN database pages */
    MiMapPfnDatabase(LoaderBlock);

    /* Initialize the color tables */
    MiInitializeColorTables();

    /* Build the PFN Database */
    MiInitializePfnDatabase(LoaderBlock);
    MmInitializeBalancer(MmAvailablePages, 0);

    /* Reset the descriptor back so we can create the correct memory blocks */
    *MxFreeDescriptor = MxOldFreeDescriptor;

    /* Initialize the nonpaged pool */
    InitializePool(NonPagedPool, 0);

    /* We PDE-aligned the nonpaged system start VA, so haul some extra PTEs! */
    Pte = MiAddressToPte(MmNonPagedSystemStart);

    MmNumberOfSystemPtes = (MiAddressToPte(MmNonPagedPoolExpansionStart) - Pte);
    MmNumberOfSystemPtes--;

    DPRINT("Final System PTE count: %lu (%lu bytes)\n", MmNumberOfSystemPtes, MmNumberOfSystemPtes * PAGE_SIZE);

    /* Create the system PTE space */
    MiInitializeSystemPtes(Pte, MmNumberOfSystemPtes, SystemPteSpace);

    /* Get the PDE For hyperspace */
    StartPde = MiAddressToPde(HYPER_SPACE);

    /* Lock PFN database */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* Allocate a page for hyperspace and create it */
    MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
    MI_SET_PROCESS2("Kernel");
    PageFrameIndex = MiRemoveAnyPage(0);
    TempPde = ValidKernelPdeLocal;
    TempPde.u.Hard.PageFrameNumber = PageFrameIndex;
    MI_WRITE_VALID_PTE(StartPde, TempPde);

    /* Flush the TLB */
    KeFlushCurrentTb();

    /* Release the lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Zero out the page table now */
    Pte = MiAddressToPte(HYPER_SPACE);
    RtlZeroMemory(Pte, PAGE_SIZE);

    /* Setup the mapping PTEs */
    MmFirstReservedMappingPte = MiAddressToPte(MI_MAPPING_RANGE_START);
    MmLastReservedMappingPte = MiAddressToPte(MI_MAPPING_RANGE_END);
    MmFirstReservedMappingPte->u.Hard.PageFrameNumber = MI_HYPERSPACE_PTES;

    /* Set the working set address */
    MmWorkingSetList = (PVOID)MI_WORKING_SET_LIST;

    /* Reserve system PTEs for zeroing PTEs and clear them */
    MiFirstReservedZeroingPte = MiReserveSystemPtes(MI_ZERO_PTES + 1, SystemPteSpace);
    RtlZeroMemory(MiFirstReservedZeroingPte, (MI_ZERO_PTES + 1) * sizeof(MMPTE));

    /* Set the counter to maximum to boot with */
    MiFirstReservedZeroingPte->u.Hard.PageFrameNumber = MI_ZERO_PTES;

    /* Lock PFN database */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* Reset the ref/share count so that MmInitializeProcessAddressSpace works */
    Pfn = MiGetPfnEntry(PFN_FROM_PTE(MiAddressToPde(PDE_BASE)));
    Pfn->u2.ShareCount = 0;
    Pfn->u3.e2.ReferenceCount = 0;

    /* Get a page for the working set list */
    MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
    MI_SET_PROCESS2("Kernel WS List");
    PageFrameIndex = MiRemoveAnyPage(0);
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = PageFrameIndex;

    /* Map the working set list */
    Pte = MiAddressToPte(MmWorkingSetList);
    MI_WRITE_VALID_PTE(Pte, TempPte);

    /* Zero it out, and save the frame index */
    RtlZeroMemory(MiPteToAddress(Pte), PAGE_SIZE);
    PsGetCurrentProcess()->WorkingSetPage = PageFrameIndex;

    /* Map the VAD bitmap */
    Pte = MiAddressToPte(MI_VAD_BITMAP);

    /* Lock PFN database */
    OldIrql = MiLockPfnDb(APC_LEVEL);
    PageFrameIndex = MiRemoveAnyPage(0);
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = PageFrameIndex;
    MI_WRITE_VALID_PTE(Pte, TempPte);

    /* Zero it out, and save maximal bit index */
    RtlZeroMemory(MI_VAD_BITMAP, PAGE_SIZE);
    MiLastVadBit = ((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS / (64 * _1KB));

    /* Check for Pentium LOCK errata */
    if (KiI386PentiumLockErrataPresent)
    {
        /* Mark the 1st IDT page as Write-Through to prevent a lockup on a F00F instruction.
           See http://www.rcollins.org/Errata/Dec97/F00FBug.html
        */
        Pte = MiAddressToPte(KeGetPcr()->IDT);
        Pte->u.Hard.WriteThrough = 1;
    }

    /* Initialize the bogus address space */
    Flags = 0;
    MmInitializeProcessAddressSpace(PsGetCurrentProcess(), NULL, NULL, &Flags, NULL);

    /* Make sure the color lists are valid */
    ASSERT(MmFreePagesByColor[0] < (PMMCOLOR_TABLES)PTE_BASE);

    StartPde = MiAddressToPde(MmFreePagesByColor[0]);
    ASSERT(StartPde->u.Hard.Valid == 1);

    Pte = MiAddressToPte(MmFreePagesByColor[0]);
    ASSERT(Pte->u.Hard.Valid == 1);

    LastPte = MiAddressToPte((ULONG_PTR)&MmFreePagesByColor[1][MmSecondaryColors] - 1);
    ASSERT(LastPte->u.Hard.Valid == 1);

    /* Loop the color list PTEs */
    for (; Pte <= LastPte; Pte++)
    {
        /* Get the PFN entry */
        Pfn = MiGetPfnEntry(PFN_FROM_PTE(Pte));
        if (Pfn->u3.e2.ReferenceCount)
            continue;

        /* Fill it out */
        Pfn->u4.PteFrame = PFN_FROM_PTE(StartPde);
        Pfn->PteAddress = Pte;
        Pfn->u2.ShareCount++;
        Pfn->u3.e2.ReferenceCount = 1;
        Pfn->u3.e1.PageLocation = ActiveAndValid;
        Pfn->u3.e1.CacheAttribute = MiCached;
    }

    return STATUS_SUCCESS;
}

/* EOF */
