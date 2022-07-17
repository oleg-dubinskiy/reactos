
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

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


/* EOF */
