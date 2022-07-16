
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG MiLargePageRangeIndex;

/* FUNCTIONS ******************************************************************/

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
