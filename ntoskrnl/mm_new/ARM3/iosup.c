
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

LOGICAL
NTAPI
MmIsIoSpaceActive(
    _In_ PHYSICAL_ADDRESS StartAddress,
    _In_ SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

PVOID
NTAPI
MmMapIoSpace(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
MmMapVideoDisplay(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmUnmapIoSpace(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmUnmapVideoDisplay(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
