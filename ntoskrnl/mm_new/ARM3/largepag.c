
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

ULONG MiLargePageRangeIndex;
PMMPTE MiLargePageHyperPte;
LIST_ENTRY MmProcessList;

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
MiInitializeLargePageSupport(VOID)
{
#if _MI_PAGING_LEVELS > 2
    DPRINT1("MiInitializeLargePageSupport: PAE/x64 Not Implemented\n");
    ASSERT(FALSE);
#else
    /* Initialize the large-page hyperspace PTE used for initial mapping */
    MiLargePageHyperPte = MiReserveSystemPtes(1, SystemPteSpace);
    ASSERT(MiLargePageHyperPte);
    MiLargePageHyperPte->u.Long = 0;

    /* Initialize the process tracking list, and insert the system process */
    InitializeListHead(&MmProcessList);
    InsertTailList(&MmProcessList, &PsGetCurrentProcess()->MmProcessLinks);
#endif
}

INIT_FUNCTION
VOID
NTAPI
MiSyncCachedRanges(VOID)
{
    ULONG ix;

    /* Scan every range */
    for (ix = 0; ix < MiLargePageRangeIndex; ix++)
    {
        UNIMPLEMENTED_DBGBREAK("No support for large pages\n");
    }
}

/* EOF */
