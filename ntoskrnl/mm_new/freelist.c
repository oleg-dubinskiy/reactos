
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "ARM3/miarm.h"

/* GLOBALS ********************************************************************/

#define ASSERT_IS_ROS_PFN(x)  ASSERT(MI_IS_ROS_PFN(x) == TRUE);

PMMPFN MmPfnDatabase;

PFN_NUMBER MmAvailablePages;
PFN_NUMBER MmResidentAvailablePages;
PFN_NUMBER MmResidentAvailableAtInit;

SIZE_T MmTotalCommittedPages;
SIZE_T MmSharedCommit;
SIZE_T MmDriverCommit;
SIZE_T MmProcessCommit;
SIZE_T MmPagedPoolCommit;
SIZE_T MmPeakCommitment;
SIZE_T MmtotalCommitLimitMaximum;

extern SIZE_T MmSystemCommitReserve;
extern SIZE_T MmTotalCommitLimit;
extern SIZE_T MmTotalCommitLimitMaximum;

static RTL_BITMAP MiUserPfnBitMap;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
MiInitializeUserPfnBitmap(VOID)
{
    PVOID Bitmap;
    ULONG Size;

    /* Allocate enough buffer for the PFN bitmap and align it on 32-bits */
    Size = ((((MmHighestPhysicalPage + 1) + 0x1F) / 0x20) * 4);

    Bitmap = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_MM);
    if (!Bitmap)
    {
        DPRINT1("MiInitializeUserPfnBitmap: Allocate failed\n");
        ASSERT(Bitmap);
    }

    /* Initialize it and clear all the bits to begin with */
    RtlInitializeBitMap(&MiUserPfnBitMap, Bitmap, ((ULONG)MmHighestPhysicalPage + 1));
    RtlClearAllBits(&MiUserPfnBitMap);
}

PFN_NUMBER
NTAPI
MmGetLRUFirstUserPage(VOID)
{
    ULONG Position;
    KIRQL OldIrql;

    /* Find the first user page */
    OldIrql = MiLockPfnDb(APC_LEVEL);
    Position = RtlFindSetBits(&MiUserPfnBitMap, 1, 0);
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    if (Position == 0xFFFFFFFF)
        return 0;

    /* Return it */
    ASSERT(Position != 0);
    ASSERT_IS_ROS_PFN(MiGetPfnEntry(Position));

    return Position;
}

PFN_NUMBER
NTAPI
MmGetLRUNextUserPage(
    PFN_NUMBER PreviousPfn)
{
    ULONG Position;
    KIRQL OldIrql;

    /* Find the next user page */
    OldIrql = MiLockPfnDb(APC_LEVEL);
    Position = RtlFindSetBits(&MiUserPfnBitMap, 1, ((ULONG)PreviousPfn + 1));
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    if (Position == 0xFFFFFFFF)
        return 0;

    /* Return it */
    ASSERT(Position != 0);
    ASSERT_IS_ROS_PFN(MiGetPfnEntry(Position));

    return Position;
}

BOOLEAN
NTAPI
MiIsPfnFree(
    _In_ PMMPFN Pfn)
{
    /* Must be a free or zero page, with no references, linked */
    return ((Pfn->u3.e1.PageLocation <= StandbyPageList) &&
            (Pfn->u1.Flink) &&
            (Pfn->u2.Blink) &&
            !(Pfn->u3.e2.ReferenceCount));
}

BOOLEAN
NTAPI
MiIsPfnInUse(
    _In_ PMMPFN Pfn)
{
    /* Standby list or higher, unlinked, and with references */
    return !MiIsPfnFree(Pfn);
}

BOOLEAN
NTAPI
MiChargeCommitment(
    _In_ SIZE_T QuotaCharge,
    _In_ PEPROCESS Process)
{
    SIZE_T OldMmTotalCommittedPages;
    ULONG ix;

    DPRINT("MiChargeCommitment: Process %p, QuotaCharge %IX\n", Process, QuotaCharge);

    ASSERT((SSIZE_T)QuotaCharge > 0);

    if (ExpInitializationPhase > 1)
    {
        for (ix = 0; ix < KeNumberProcessors; ix++)
        {
            ASSERT(KiProcessorBlock[ix]->IdleThread != &(PsGetCurrentThread()->Tcb));
        }
    }

    while (TRUE)
    {
        OldMmTotalCommittedPages = MmTotalCommittedPages;

        if ((MmTotalCommittedPages + QuotaCharge) <= (MmTotalCommitLimit - MmSystemCommitReserve))
            goto Compare;

        if ((MmTotalCommitLimitMaximum - MmTotalCommitLimit) <= 100)
        {
            DPRINT1("MiChargeCommitment: FIXME MiTrimSegmentCache()\n");
            ASSERT(FALSE);

            if (MmTotalCommitLimit >= MmTotalCommitLimitMaximum)
            {
                DPRINT1("MiChargeCommitment: FIXME MiCauseOverCommitPopup()\n");
                ASSERT(FALSE);
                return FALSE;
            }
        }

        DPRINT1("MiChargeCommitment: %IX, %IX, %IX\n", MmTotalCommittedPages, MmTotalCommitLimit, MmSystemCommitReserve);
        DPRINT1("MiChargeCommitment: FIXME MiIssuePageExtendRequest()\n");
        ASSERT(FALSE);

Compare:
        if (OldMmTotalCommittedPages == InterlockedCompareExchange((PLONG)&MmTotalCommittedPages,
                                                                   (MmTotalCommittedPages + QuotaCharge),
                                                                   MmTotalCommittedPages))
        {
            break;
        }
    }

    if (MmPeakCommitment < MmTotalCommittedPages)
        MmPeakCommitment = MmTotalCommittedPages;

    if (MmTotalCommittedPages <= ((MmTotalCommitLimit / 10) * 9))
        return TRUE;

    DPRINT1("MiChargeCommitment: %IX, %IX\n", MmTotalCommittedPages, MmTotalCommitLimit);

    if (MmTotalCommitLimit < MmTotalCommitLimitMaximum)
    {
        DPRINT1("MiChargeCommitment: FIXME MiIssuePageExtendRequestNoWait()\n");
        ASSERT(FALSE);
    }
    else if (MmTotalCommitLimit >= (MmTotalCommitLimitMaximum - 100))
    {
        DPRINT1("MiChargeCommitment: FIXME MiTrimSegmentCache()\n");
        ASSERT(FALSE);
    }

    return TRUE;
}

/* EOF */
