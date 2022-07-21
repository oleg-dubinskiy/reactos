
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "ARM3/miarm.h"

/* GLOBALS ********************************************************************/

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

/* FUNCTIONS ******************************************************************/

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


/* EOF */
