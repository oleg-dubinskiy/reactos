
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

MM_MEMORY_CONSUMER MiMemoryConsumers[MC_MAXIMUM];

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
MmInitializeBalancer(
    ULONG NrAvailablePages,
    ULONG NrSystemPages)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
