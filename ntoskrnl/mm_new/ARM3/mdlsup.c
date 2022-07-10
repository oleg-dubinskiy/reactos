
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

BOOLEAN MmTrackPtes;
BOOLEAN MmTrackLockedPages;

/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmAdvanceMdl(
    _In_ PMDL Mdl,
    _In_ ULONG NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PMDL
NTAPI
MmAllocatePagesForMdl(
    _In_ PHYSICAL_ADDRESS LowAddress,
    _In_ PHYSICAL_ADDRESS HighAddress,
    _In_ PHYSICAL_ADDRESS SkipBytes,
    _In_ SIZE_T TotalBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PMDL
NTAPI
MmAllocatePagesForMdlEx(
    _In_ PHYSICAL_ADDRESS LowAddress,
    _In_ PHYSICAL_ADDRESS HighAddress,
    _In_ PHYSICAL_ADDRESS SkipBytes,
    _In_ SIZE_T TotalBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG Flags)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmBuildMdlForNonPagedPool(
    _In_ PMDL Mdl)
{
    UNIMPLEMENTED_DBGBREAK();
}

PMDL
NTAPI
MmCreateMdl(
    _In_ PMDL Mdl,
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmFreePagesFromMdl(
    _In_ PMDL Mdl)
{
    UNIMPLEMENTED_DBGBREAK();
}

PVOID
NTAPI
MmMapLockedPages(
    _In_ PMDL Mdl,
    _In_ KPROCESSOR_MODE AccessMode)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
MmMapLockedPagesSpecifyCache(
    _In_ PMDL Mdl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ PVOID BaseAddress,
    _In_ ULONG BugCheckOnFailure,
    _In_ MM_PAGE_PRIORITY Priority)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
MmMapLockedPagesWithReservedMapping(
    _In_ PVOID MappingAddress,
    _In_ ULONG PoolTag,
    _In_ PMDL MemoryDescriptorList,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmMapMemoryDumpMdl(
    _In_ PMDL Mdl)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmPrefetchPages(
    _In_ ULONG NumberOfLists,
    _In_ PREAD_LIST* ReadLists)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
MmProbeAndLockPages(
    _In_ PMDL Mdl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmProbeAndLockProcessPages(
    _Inout_ PMDL MemoryDescriptorList,
    _In_ PEPROCESS Process,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmProbeAndLockSelectedPages(
    _Inout_ PMDL MemoryDescriptorList,
    _In_ LARGE_INTEGER PageList[],
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmProtectMdlSystemAddress(
    _In_ PMDL MemoryDescriptorList,
    _In_ ULONG NewProtect)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

SIZE_T
NTAPI
MmSizeOfMdl(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

VOID
NTAPI
MmUnlockPages(
    _In_ PMDL Mdl)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmUnmapLockedPages(
    _In_ PVOID BaseAddress,
    _In_ PMDL Mdl)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmUnmapReservedMapping(
    _In_ PVOID BaseAddress,
    _In_ ULONG PoolTag,
    _In_ PMDL MemoryDescriptorList)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
