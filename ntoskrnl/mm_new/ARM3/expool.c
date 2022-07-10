
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

#undef ExAllocatePoolWithQuota
#undef ExAllocatePoolWithQuotaTag

/* GLOBALS ********************************************************************/

ULONG ExpNumberOfPagedPools;
POOL_DESCRIPTOR NonPagedPoolDescriptor;
PPOOL_DESCRIPTOR ExpPagedPoolDescriptor[16 + 1];
PPOOL_TRACKER_TABLE PoolTrackTable;

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
ExGetPoolTagInfo(
    _In_ PSYSTEM_POOLTAG_INFORMATION SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Inout_ PULONG ReturnLength OPTIONAL)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
ExQueryPoolUsage(
    _Out_ PULONG PagedPoolPages,
    _Out_ PULONG NonPagedPoolPages,
    _Out_ PULONG PagedPoolAllocs,
    _Out_ PULONG PagedPoolFrees,
    _Out_ PULONG PagedPoolLookasideHits,
    _Out_ PULONG NonPagedPoolAllocs,
    _Out_ PULONG NonPagedPoolFrees,
    _Out_ PULONG NonPagedPoolLookasideHits)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ExReturnPoolQuota(
    _In_ PVOID P)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ExpCheckPoolAllocation(
    PVOID P,
    POOL_TYPE PoolType,
    ULONG Tag)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
ExAllocatePool(
    POOL_TYPE PoolType,
    SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
ExAllocatePoolWithQuota(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
ExAllocatePoolWithQuotaTag(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
ExAllocatePoolWithTag(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
ExAllocatePoolWithTagPriority(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag,
    _In_ EX_POOL_PRIORITY Priority)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
ExFreePool(
    PVOID P)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ExFreePoolWithTag(
    _In_ PVOID P,
    _In_ ULONG TagToFree)
{
    UNIMPLEMENTED_DBGBREAK();
}

SIZE_T
NTAPI
ExQueryPoolBlockSize(
    _In_ PVOID PoolBlock,
    _Out_ PBOOLEAN QuotaCharged)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

/* EOF */
