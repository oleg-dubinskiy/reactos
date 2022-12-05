
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/


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

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmAdjustWorkingSetSize(
    _In_ SIZE_T WorkingSetMinimumInBytes,
    _In_ SIZE_T WorkingSetMaximumInBytes,
    _In_ ULONG SystemCache,
    _In_ BOOLEAN IncreaseOkay)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
