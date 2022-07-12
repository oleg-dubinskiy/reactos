
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

MM_PAGED_POOL_INFO MmPagedPoolInfo;
KGUARDED_MUTEX MmPagedPoolMutex;
SIZE_T MmAllocatedNonPagedPool;
ULONG MmSpecialPoolTag;
ULONG MmConsumedPoolPercentage;
BOOLEAN MmProtectFreedNonPagedPool;

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
MiInitializeNonPagedPool(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
MiInitializeNonPagedPoolThresholds(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
MmAllocateMappingAddress(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG PoolTag)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmFreeMappingAddress(
    _In_ PVOID BaseAddress,
    _In_ ULONG PoolTag)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
