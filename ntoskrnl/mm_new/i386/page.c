
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <mm_new/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

const
ULONG
MmProtectToPteMask[32] =
{
    //
    // These are the base MM_ protection flags
    //
    0,
    PTE_READONLY            | PTE_ENABLE_CACHE,
    PTE_EXECUTE             | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_ENABLE_CACHE,
    PTE_READWRITE           | PTE_ENABLE_CACHE,
    PTE_WRITECOPY           | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_ENABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_ENABLE_CACHE,
    //
    // These OR in the MM_NOCACHE flag
    //
    0,
    PTE_READONLY            | PTE_DISABLE_CACHE,
    PTE_EXECUTE             | PTE_DISABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_DISABLE_CACHE,
    PTE_READWRITE           | PTE_DISABLE_CACHE,
    PTE_WRITECOPY           | PTE_DISABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_DISABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_DISABLE_CACHE,
    //
    // These OR in the MM_DECOMMIT flag, which doesn't seem supported on x86/64/ARM
    //
    0,
    PTE_READONLY            | PTE_ENABLE_CACHE,
    PTE_EXECUTE             | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_ENABLE_CACHE,
    PTE_READWRITE           | PTE_ENABLE_CACHE,
    PTE_WRITECOPY           | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_ENABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_ENABLE_CACHE,
    //
    // These OR in the MM_NOACCESS flag, which seems to enable WriteCombining?
    //
    0,
    PTE_READONLY            | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE             | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_READ        | PTE_WRITECOMBINED_CACHE,
    PTE_READWRITE           | PTE_WRITECOMBINED_CACHE,
    PTE_WRITECOPY           | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_WRITECOMBINED_CACHE,
};

const
ULONG MmProtectToValue[32] =
{
    PAGE_NOACCESS,
    PAGE_READONLY,
    PAGE_EXECUTE,
    PAGE_EXECUTE_READ,
    PAGE_READWRITE,
    PAGE_WRITECOPY,
    PAGE_EXECUTE_READWRITE,
    PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_NOCACHE | PAGE_READONLY,
    PAGE_NOCACHE | PAGE_EXECUTE,
    PAGE_NOCACHE | PAGE_EXECUTE_READ,
    PAGE_NOCACHE | PAGE_READWRITE,
    PAGE_NOCACHE | PAGE_WRITECOPY,
    PAGE_NOCACHE | PAGE_EXECUTE_READWRITE,
    PAGE_NOCACHE | PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_GUARD | PAGE_READONLY,
    PAGE_GUARD | PAGE_EXECUTE,
    PAGE_GUARD | PAGE_EXECUTE_READ,
    PAGE_GUARD | PAGE_READWRITE,
    PAGE_GUARD | PAGE_WRITECOPY,
    PAGE_GUARD | PAGE_EXECUTE_READWRITE,
    PAGE_GUARD | PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_WRITECOMBINE | PAGE_READONLY,
    PAGE_WRITECOMBINE | PAGE_EXECUTE,
    PAGE_WRITECOMBINE | PAGE_EXECUTE_READ,
    PAGE_WRITECOMBINE | PAGE_READWRITE,
    PAGE_WRITECOMBINE | PAGE_WRITECOPY,
    PAGE_WRITECOMBINE | PAGE_EXECUTE_READWRITE,
    PAGE_WRITECOMBINE | PAGE_EXECUTE_WRITECOPY
};

extern ULONG MmProcessColorSeed;
extern ULONG MmSecondaryColorMask;
extern MMPTE ValidKernelPteLocal;
extern LIST_ENTRY MmProcessList;
extern PVOID MmHyperSpaceEnd;

/* FUNCTIONS ******************************************************************/

ULONG
NTAPI
MmGetPageProtect(PEPROCESS Process, PVOID Address)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

VOID
NTAPI
MmSetPageProtect(PEPROCESS Process, PVOID Address, ULONG flProtect)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
MmInitGlobalKernelPageDirectory(VOID)
{
    ;/* Nothing to do here */
}

BOOLEAN
NTAPI
MmCreateProcessAddressSpace(
    _In_ ULONG MinWs,
    _In_ PEPROCESS Process,
    _Out_ PULONG_PTR DirectoryTableBase)
{
    PFN_NUMBER PdeIndex;
    PFN_NUMBER HyperIndex;
    PFN_NUMBER WsListIndex;
    PFN_NUMBER VadBitmapIndex;
    PMMPDE SystemTable;
    PMMPDE HyperTable;
    PMMPTE Pte;
    MMPTE TempPte;
    MMPTE PdePte;
    PMMPFN Pfn;
    ULONG PdeOffset;
    ULONG Color;
    KIRQL OldIrql;

    DPRINT("MmCreateProcessAddressSpace: MinWs %X, Process %p\n", MinWs, Process);

    if (!MiChargeCommitment(4, NULL))
    {
        DPRINT1("MmCreateProcessAddressSpace: MiChargeCommitment() failed\n");
        return FALSE;
    }

    /* Choose a process color */
    Process->NextPageColor = (USHORT)RtlRandom(&MmProcessColorSeed);

    /* Setup the hyperspace lock */
    KeInitializeSpinLock(&Process->HyperSpaceLock);

    /* Lock PFN database */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    if ((PFN_NUMBER)MinWs >= (MmResidentAvailablePages - MmSystemLockPagesCount))
    {
        MiUnlockPfnDb(OldIrql, APC_LEVEL);

        ASSERT(MmTotalCommittedPages >= 4);
        InterlockedExchangeAddSizeT(&MmTotalCommittedPages, -4);

        DPRINT1("MmCreateProcessAddressSpace: %X, %IX, %IX\n", MinWs, MmResidentAvailablePages, MmSystemLockPagesCount);
        return FALSE;
    }

    InterlockedExchangeAddSizeT(&MmResidentAvailablePages, -MinWs);

    if (MmAvailablePages < 0x80)
        MiEnsureAvailablePageOrWait(NULL, OldIrql);

    /* Get a zero page for the PDE, if possible */
    Color = MI_GET_NEXT_PROCESS_COLOR(Process);
    MI_SET_USAGE(MI_USAGE_PAGE_DIRECTORY);

    PdeIndex = MiRemoveZeroPageSafe(Color);
    if (!PdeIndex)
    {
        /* No zero pages, grab a free one */
        PdeIndex = MiRemoveAnyPage(Color);

        /* Zero it outside the PFN lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        MiZeroPhysicalPage(PdeIndex);
        OldIrql = MiLockPfnDb(APC_LEVEL);
    }

    if (MmAvailablePages < 0x80)
        MiEnsureAvailablePageOrWait(NULL, OldIrql);

    /* Get a zero page for hyperspace, if possible */
    MI_SET_USAGE(MI_USAGE_PAGE_DIRECTORY);
    Color = MI_GET_NEXT_PROCESS_COLOR(Process);

    HyperIndex = MiRemoveZeroPageSafe(Color);
    if (!HyperIndex)
    {
        /* No zero pages, grab a free one */
        HyperIndex = MiRemoveAnyPage(Color);

        /* Zero it outside the PFN lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        MiZeroPhysicalPage(HyperIndex);
        OldIrql = MiLockPfnDb(APC_LEVEL);
    }

    if (MmAvailablePages < 0x80)
        MiEnsureAvailablePageOrWait(NULL, OldIrql);

    /* Get a zero page for VadBitmap, if possible */
    MI_SET_USAGE(MI_USAGE_PAGE_DIRECTORY);
    Color = MI_GET_NEXT_PROCESS_COLOR(Process);

    VadBitmapIndex = MiRemoveZeroPageSafe(Color);
    if (!VadBitmapIndex)
    {
        /* No zero pages, grab a free one */
        VadBitmapIndex = MiRemoveAnyPage(Color);

        /* Zero it outside the PFN lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        MiZeroPhysicalPage(VadBitmapIndex);
        OldIrql = MiLockPfnDb(APC_LEVEL);
    }

    if (MmAvailablePages < 0x80)
        MiEnsureAvailablePageOrWait(NULL, OldIrql);

    /* Get a zero page for the woring set list, if possible */
    MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
    Color = MI_GET_NEXT_PROCESS_COLOR(Process);

    WsListIndex = MiRemoveZeroPageSafe(Color);
    if (!WsListIndex)
    {
        /* No zero pages, grab a free one */
        WsListIndex = MiRemoveAnyPage(Color);

        /* Zero it outside the PFN lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        MiZeroPhysicalPage(WsListIndex);
    }
    else
    {
        /* Release the PFN lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
    }

    /* Switch to phase 1 initialization */
    ASSERT(Process->AddressSpaceInitialized == 0);
    Process->AddressSpaceInitialized = 1;

    /* Set the base directory pointers */
    Process->WorkingSetPage = WsListIndex;

    DirectoryTableBase[0] = (PdeIndex * PAGE_SIZE);
    DirectoryTableBase[1] = (HyperIndex * PAGE_SIZE);

    /* Make sure we don't already have a page directory setup */
    ASSERT(Process->Pcb.DirectoryTableBase[0] == 0);

    /* Get a PTE to map hyperspace */
    Pte = MiReserveSystemPtes(1, SystemPteSpace);
    ASSERT(Pte != NULL);

    /* Build it */
    MI_MAKE_HARDWARE_PTE_KERNEL(&PdePte, Pte, MM_READWRITE, HyperIndex);

    /* Set it dirty and map it */
    MI_MAKE_DIRTY_PAGE(&PdePte);
    MI_WRITE_VALID_PTE(Pte, PdePte);

    /* Now get hyperspace's page table */
    HyperTable = MiPteToAddress(Pte);

    /* Write the PTE/PDE entry for the Vad bitmap index */
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = VadBitmapIndex;
    PdeOffset = MiAddressToPteOffset(MI_VAD_BITMAP);
    HyperTable[PdeOffset] = TempPte;

    /* Now write the PTE/PDE entry for the working set list index itself */
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = WsListIndex;
    PdeOffset = MiAddressToPteOffset(MmWorkingSetList);
    HyperTable[PdeOffset] = TempPte;

    /* Let go of the system PTE */
    MiReleaseSystemPtes(Pte, 1, SystemPteSpace);

    /* Save the PTE address of the page directory itself */
    Pfn = MiGetPfnEntry(PdeIndex);
    Pfn->PteAddress = (PMMPTE)PDE_BASE;

    /* Insert us into the Mm process list */
    OldIrql = MiAcquireExpansionLock();
    InsertTailList(&MmProcessList, &Process->MmProcessLinks);
    MiReleaseExpansionLock(OldIrql);

    /* Get a PTE to map the page directory */
    Pte = MiReserveSystemPtes(1, SystemPteSpace);
    ASSERT(Pte != NULL);

    /* Build it */
    MI_MAKE_HARDWARE_PTE_KERNEL(&PdePte, Pte, MM_READWRITE, PdeIndex);

    /* Set it dirty and map it */
    MI_MAKE_DIRTY_PAGE(&PdePte);
    MI_WRITE_VALID_PTE(Pte, PdePte);

    /* Now get the page directory (which we'll double map, so call it a page table */
    SystemTable = MiPteToAddress(Pte);

    /* Copy all the kernel mappings */
    PdeOffset = MiAddressToPdeOffset(MmSystemRangeStart);

    RtlCopyMemory(&SystemTable[PdeOffset],
                  MiAddressToPde(MmSystemRangeStart),
                  PAGE_SIZE - PdeOffset * sizeof(MMPTE));

    /* Now write the PTE/PDE entry for hyperspace itself */
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = HyperIndex;
    PdeOffset = MiAddressToPdeOffset(HYPER_SPACE);
    SystemTable[PdeOffset] = TempPte;

    /* Sanity check */
    PdeOffset++;
    ASSERT(MiAddressToPdeOffset(MmHyperSpaceEnd) >= PdeOffset);

    /* Now do the x86 trick of making the PDE a page table itself */
    PdeOffset = MiAddressToPdeOffset(PTE_BASE);
    TempPte.u.Hard.PageFrameNumber = PdeIndex;
    SystemTable[PdeOffset] = TempPte;

    /* Let go of the system PTE */
    MiReleaseSystemPtes(Pte, 1, SystemPteSpace);

    /* Add the process to the session */
    MiSessionAddProcess(Process);
    return TRUE;
}

/* EOF */
