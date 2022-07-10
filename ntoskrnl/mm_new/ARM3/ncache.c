
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
MmAllocateNonCachedMemory(
    _In_ SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmFreeNonCachedMemory(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
