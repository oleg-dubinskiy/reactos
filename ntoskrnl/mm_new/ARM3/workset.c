
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

#if (_MI_PAGING_LEVELS == 2)
  ULONG MiMaximumWorkingSet = ((ULONG_PTR)MI_USER_PROBE_ADDRESS / PAGE_SIZE);
#else
 #error FIXME
#endif

SIZE_T MmMinimumWorkingSetSize;
SIZE_T MmMaximumWorkingSetSize;
SIZE_T MmPagesAboveWsMinimum;

extern PVOID MmHyperSpaceEnd;

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
MiEliminateWorkingSetEntry(
    _In_ ULONG WorkingSetIndex,
    _In_ PMMPTE Pte,
    _In_ PMMPFN Pfn,
    _In_ PMMSUPPORT WorkingSet,
    _In_ BOOLEAN Parameter5)
{
    MMPTE TempPte;
    MMPTE PreviousPte;
    ULONG PageFrameIndex;
    KIRQL OldIrql;

    DPRINT("MiEliminateWorkingSetEntry: %X, %p [%X], %p, %p, %X\n", WorkingSetIndex, Pte, *Pte, Pfn, WorkingSet, Parameter5);

    TempPte.u.Long = Pte->u.Long;
    ASSERT(TempPte.u.Hard.Valid == 1);

    PageFrameIndex = TempPte.u.Hard.PageFrameNumber;
    ASSERT(Pfn == MI_PFN_ELEMENT(PageFrameIndex));
    ASSERT(MI_IS_PFN_DELETED(Pfn) == 0);

    if (TempPte.u.Hard.Writable)
    {
        ASSERT(TempPte.u.Hard.Dirty == 1);
    }

    if (Pfn->u3.e1.PrototypePte)
    {
        DPRINT1("MiEliminateWorkingSetEntry: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        ASSERT(Pfn->u2.ShareCount == 1); // FIXME

        TempPte.u.Soft.Valid = 0;
        TempPte.u.Soft.Transition = 1;
        TempPte.u.Soft.Prototype = 0;
        TempPte.u.Soft.Protection = Pfn->OriginalPte.u.Soft.Protection;
    }

    OldIrql = MiLockPfnDb(APC_LEVEL);

    if (!Parameter5)
    {
        DPRINT1("MiEliminateWorkingSetEntry: FIXME\n");
        ASSERT(FALSE);
    }

    if (!Pfn->u3.e1.PrototypePte)
    {
        //ASSERT(Pfn->u1.WsIndex != 0);
        Pfn->u1.WsIndex = 0;
    }
    else
    {
        DPRINT1("MiEliminateWorkingSetEntry: FIXME\n");
        ASSERT(FALSE);
    }

    PreviousPte = *Pte;

    ASSERT(PreviousPte.u.Hard.Valid == 1);
    ASSERT((TempPte).u.Hard.Valid == 0);
    Pte->u.Long = TempPte.u.Long;

    DPRINT("MiEliminateWorkingSetEntry: %p [%X], [%X]\n", Pte, *Pte, PreviousPte.u.Long);

    //FIXME: KeFlushSingleTb()

    ASSERT(PreviousPte.u.Hard.Valid == 1);
    ASSERT(KeGetCurrentIrql() > APC_LEVEL);

    if (!Pfn->u3.e1.Modified && PreviousPte.u.Hard.Dirty)
    {
        ASSERT(Pfn->u3.e1.Rom == 0);
        Pfn->u3.e1.Modified = 1;

        if (!Pfn->OriginalPte.u.Soft.Prototype && !Pfn->u3.e1.WriteInProgress)
        {
            MiReleasePageFileSpace(Pfn->OriginalPte);
            Pfn->OriginalPte.u.Soft.PageFileHigh = 0;
        }
    }

    if (!Pfn->u3.e1.PrototypePte &&
        PreviousPte.u.Hard.Dirty &&
        (PsGetCurrentProcess()->Flags & 0x8000))
    {
        DPRINT1("MiEliminateWorkingSetEntry: FIXME\n");
        ASSERT(FALSE);
    }

    MiDecrementPfnShare(Pfn, PageFrameIndex);
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    return TRUE;
}

BOOLEAN
NTAPI
MiRemovePageFromWorkingSet(
    _In_ PMMPTE Pte,
    _In_ PMMPFN Pfn,
    _In_ PMMSUPPORT WorkingSet)
{
    ULONG WorkingSetIndex;

    DPRINT("MiRemovePageFromWorkingSet: %p, %p, %p\n", Pte, Pfn, WorkingSet);

    WorkingSetIndex = 0;//MiLocateWsle(..);
    MiEliminateWorkingSetEntry(WorkingSetIndex, Pte, Pfn, WorkingSet, 1);

    return FALSE;
}

NTSTATUS
NTAPI
MiEmptyWorkingSet(
    _In_ PMMSUPPORT WorkingSet,
    _In_ BOOLEAN IsParam2)
{
    if (WorkingSet->VmWorkingSetList == MmWorkingSetList &&
        PsGetCurrentProcess()->VmDeleted)
    {
        DPRINT("MiEmptyWorkingSet: STATUS_PROCESS_IS_TERMINATING. %p, %X\n", WorkingSet, IsParam2);
        return STATUS_PROCESS_IS_TERMINATING;
    }

    UNIMPLEMENTED;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmAdjustWorkingSetSizeEx(
    SIZE_T WorkingSetMinimumInBytes,
    SIZE_T WorkingSetMaximumInBytes,
    BOOLEAN IsSystemCache,
    BOOLEAN IsIncreaseOkay,
    ULONG Flags,
    BOOLEAN* OutIsAddMinSize)
{
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS Process;
    PMMSUPPORT WorkingSet;
    PMMWSL WorkingSetList;
    SIZE_T MinWorkingSetSize;
    SIZE_T MaxWorkingSetSize;
    SSIZE_T Delta;
    ULONG TrimAge;
    BOOLEAN LimitIsGood = TRUE;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("MmAdjustWorkingSetSizeEx: %IX, %IX, %X\n", WorkingSetMinimumInBytes, WorkingSetMaximumInBytes, IsSystemCache);

    *OutIsAddMinSize = FALSE;

    if (IsSystemCache)
    {
        Process = NULL;
        WorkingSet = &MmSystemCacheWs;
    }
    else
    {
        Process = (PEPROCESS)Thread->Tcb.ApcState.Process;
        WorkingSet = &Process->Vm;
    }

    /* Check for special case: empty the working set */
    if (WorkingSetMinimumInBytes == -1 && WorkingSetMaximumInBytes == -1)
        return MiEmptyWorkingSet(WorkingSet, TRUE);

    /* Assume success */
    Status = STATUS_SUCCESS;

    //MmLockPagableSectionByHandle(ExPageLockHandle);

    /* Lock the working set */
    if (IsSystemCache)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        MiLockProcessWorkingSet(Process, Thread);

        if (Process->VmDeleted)
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_PROCESS_IS_TERMINATING\n");
            Status = STATUS_PROCESS_IS_TERMINATING;
            goto Cleanup;
        }
    }

    /* Calculate the actual minimum and maximum working set size to set */
    if (WorkingSetMinimumInBytes)
        MinWorkingSetSize = (WorkingSetMinimumInBytes / PAGE_SIZE);
    else
        MinWorkingSetSize = WorkingSet->MinimumWorkingSetSize;

    if (WorkingSetMaximumInBytes)
        MaxWorkingSetSize = (WorkingSetMaximumInBytes / PAGE_SIZE);
    else
        MaxWorkingSetSize = WorkingSet->MaximumWorkingSetSize;

    /* Check if the new minimum exceeds the new maximum */
    if (MinWorkingSetSize > MaxWorkingSetSize)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT. %IX, %IX\n", MinWorkingSetSize, MaxWorkingSetSize);
        Status = STATUS_BAD_WORKING_SET_LIMIT;
        goto Cleanup;
    }

    /* Check if the new maximum exceeds the global maximum */
    if (MaxWorkingSetSize > MmMaximumWorkingSetSize)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_WORKING_SET_LIMIT_RANGE\n");
        MaxWorkingSetSize = MmMaximumWorkingSetSize;
        Status = STATUS_WORKING_SET_LIMIT_RANGE;
    }

    /* Check if the new minimum is below the global minimum */
    if (MinWorkingSetSize < MmMinimumWorkingSetSize)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_WORKING_SET_LIMIT_RANGE\n");
        MinWorkingSetSize = MmMinimumWorkingSetSize;
        Status = STATUS_WORKING_SET_LIMIT_RANGE;
    }

    /* Check if the new minimum exceeds the new maximum */
    if (MinWorkingSetSize > MmMaximumWorkingSetSize)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_WORKING_SET_LIMIT_RANGE\n");
        MinWorkingSetSize = MmMaximumWorkingSetSize;
        Status = STATUS_WORKING_SET_LIMIT_RANGE;
    }

    WorkingSetList = WorkingSet->VmWorkingSetList;

    if (MaxWorkingSetSize <= (WorkingSetList->FirstDynamic + 8))
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT\n");
        Status = STATUS_BAD_WORKING_SET_LIMIT;
        goto Cleanup;
    }

    if ((Flags & 4) ||
        (WorkingSet->Flags.MinimumWorkingSetHard && !(Flags & 8)) ||
        (Flags & 1) ||
        (WorkingSet->Flags.MaximumWorkingSetHard && !(Flags & 2)))
    {
        if (MaxWorkingSetSize <= (MinWorkingSetSize + 8) ||
            (IsSystemCache && MaxWorkingSetSize < 0x1000))
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT\n");
            Status = STATUS_BAD_WORKING_SET_LIMIT;
            goto Cleanup;
        }
    }

    /* Calculate the minimum WS size adjustment and check if we increase */
    Delta = (MinWorkingSetSize - WorkingSet->MinimumWorkingSetSize);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    if (Delta > 0)
    {
        *OutIsAddMinSize = TRUE;

        /* Is increasing ok? */
        if (!IsIncreaseOkay)
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: Privilege for WS size increase not held\n");
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            Status = STATUS_PRIVILEGE_NOT_HELD;
            goto Cleanup;
        }

        /* Check if the number of available pages is large enough */
        if ((SPFN_NUMBER)((Delta / (PAGE_SIZE / sizeof(ULONG)))) > (SPFN_NUMBER)(MmAvailablePages - 0x80))
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: Not enough available pages\n");
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        /* Check if there are enough resident available pages */
        if (Delta > (MmResidentAvailablePages - MmSystemLockPagesCount - 0x100))
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: Not enough resident pages\n");
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    /* Update resident available pages */
    if (Delta)
        InterlockedExchangeAddSizeT(&MmResidentAvailablePages, -Delta);

    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    if (MaxWorkingSetSize < WorkingSetList->LastInitializedWsle &&
        MaxWorkingSetSize < WorkingSet->WorkingSetSize)
    {
        if (WorkingSetList->FirstDynamic + 8 >= MaxWorkingSetSize)
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT\n");
            InterlockedExchangeAddSizeT(&MmResidentAvailablePages, Delta);
            Status = STATUS_BAD_WORKING_SET_LIMIT;
            goto Cleanup;
        }

        for (TrimAge = 2; ; TrimAge--)
        {
            ASSERT(TrimAge <= 2); // MI_PASS0_TRIM_AGE

            DPRINT1("MmAdjustWorkingSetSizeEx: FIXME MiTrimWorkingSet()\n");
            ASSERT(FALSE);
            //MiTrimWorkingSet((WorkingSet->WorkingSetSize - MaxWorkingSetSize), WorkingSet, TrimAge);

            if (WorkingSet->WorkingSetSize <= MaxWorkingSetSize)
                break;

            if (!TrimAge)
            {
                DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT\n");
                LimitIsGood = FALSE;
                Status = STATUS_BAD_WORKING_SET_LIMIT;
                break;
            }
        }
    }

    if ((LONG)WorkingSet->WorkingSetSize > (LONG)WorkingSet->MinimumWorkingSetSize)
        InterlockedExchangeAddSizeT(&MmPagesAboveWsMinimum, -((LONG)WorkingSet->WorkingSetSize - (LONG)WorkingSet->MinimumWorkingSetSize));

    if ((LONG)WorkingSet->WorkingSetSize > (LONG)MinWorkingSetSize)
        InterlockedExchangeAddSizeT(&MmPagesAboveWsMinimum, ((LONG)WorkingSet->WorkingSetSize - (LONG)MinWorkingSetSize));

    if (LimitIsGood)
    {
        WorkingSet->MaximumWorkingSetSize = MaxWorkingSetSize;
        WorkingSet->MinimumWorkingSetSize = MinWorkingSetSize;

        if (Flags)
        {
            OldIrql = MiAcquireExpansionLock();

            if (Flags & 4)
                WorkingSet->Flags.MinimumWorkingSetHard = 1;
            else if (Flags & 8)
                WorkingSet->Flags.MinimumWorkingSetHard = 0;

            if (Flags & 1)
                WorkingSet->Flags.MaximumWorkingSetHard = 1;
            else if (Flags & 2)
                WorkingSet->Flags.MaximumWorkingSetHard = 0;

            MiReleaseExpansionLock(OldIrql);
        }
    }
    else
    {
        InterlockedExchangeAddSizeT(&MmResidentAvailablePages, Delta);
    }

    ASSERT((WorkingSetList->FirstFree <= WorkingSetList->LastInitializedWsle) || (WorkingSetList->FirstFree == 0xFFFFFFF)); // WSLE_NULL_INDEX

Cleanup:

    if (IsSystemCache)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        MiUnlockProcessWorkingSet(Process, Thread);
    }

    //MmUnlockPagableImageSection(ExPageLockHandle);
    return Status;
}

VOID
NTAPI
MiInitializeWorkingSetList(
    _In_ PEPROCESS CurrentProcess)
{
    PMMPFN Pfn1;
    PMMPTE sysPte;
    MMPTE tempPte;

    /* Setup some bogus list data */
    MmWorkingSetList->LastEntry = CurrentProcess->Vm.MinimumWorkingSetSize;
    MmWorkingSetList->HashTable = NULL;
    MmWorkingSetList->HashTableSize = 0;
    MmWorkingSetList->NumberOfImageWaiters = 0;
    MmWorkingSetList->Wsle = (PVOID)(ULONG_PTR)0xDEADBABEDEADBABEULL;
    MmWorkingSetList->VadBitMapHint = 1;
    MmWorkingSetList->HashTableStart = (PVOID)(ULONG_PTR)0xBADAB00BBADAB00BULL;
    MmWorkingSetList->HighestPermittedHashAddress = (PVOID)(ULONG_PTR)0xCAFEBABECAFEBABEULL;
    MmWorkingSetList->FirstFree = 1;
    MmWorkingSetList->FirstDynamic = 2;
    MmWorkingSetList->NextSlot = 3;
    MmWorkingSetList->LastInitializedWsle = 4;

    /* The rule is that the owner process is always in the FLINK of the PDE's PFN entry */
    Pfn1 = MiGetPfnEntry(CurrentProcess->Pcb.DirectoryTableBase[0] >> PAGE_SHIFT);
    ASSERT(Pfn1->u4.PteFrame == MiGetPfnEntryIndex(Pfn1));
    Pfn1->u1.Event = (PKEVENT)CurrentProcess;

    /* Map the process working set in kernel space */
    sysPte = MiReserveSystemPtes(1, SystemPteSpace);
    MI_MAKE_HARDWARE_PTE_KERNEL(&tempPte, sysPte, MM_READWRITE, CurrentProcess->WorkingSetPage);
    MI_WRITE_VALID_PTE(sysPte, tempPte);
    CurrentProcess->Vm.VmWorkingSetList = MiPteToAddress(sysPte);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmAdjustWorkingSetSize(
    _In_ SIZE_T WorkingSetMinimumInBytes,
    _In_ SIZE_T WorkingSetMaximumInBytes,
    _In_ ULONG SystemCache,
    _In_ BOOLEAN IncreaseOkay)
{
    BOOLEAN dummy;

    return MmAdjustWorkingSetSizeEx(WorkingSetMinimumInBytes,
                                    WorkingSetMaximumInBytes,
                                    SystemCache,
                                    IncreaseOkay,
                                    0,
                                    &dummy);
}

/* EOF */
