
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
MmAllocateContiguousMemory(
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
MmAllocateContiguousMemorySpecifyCache(
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS LowestAcceptableAddress OPTIONAL,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
    _In_ PHYSICAL_ADDRESS BoundaryAddressMultiple OPTIONAL,
    _In_ MEMORY_CACHING_TYPE CacheType OPTIONAL)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmFreeContiguousMemory(
    _In_ PVOID BaseAddress)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmFreeContiguousMemorySpecifyCache(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
