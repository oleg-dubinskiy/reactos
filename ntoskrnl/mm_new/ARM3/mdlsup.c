
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
    PVOID Base;
    PMMPTE Pte;
    PPFN_NUMBER MdlPages;
    PPFN_NUMBER EndPage;
    PFN_NUMBER Pfn;
    PFN_NUMBER PageCount;

    /* Sanity checks */
    ASSERT(Mdl->ByteCount != 0);
    ASSERT((Mdl->MdlFlags & (MDL_PAGES_LOCKED | MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL | MDL_PARTIAL)) == 0);

    /* We know the MDL isn't associated to a process now */
    Mdl->Process = NULL;

    /* Get page and VA information */
    MdlPages = (PPFN_NUMBER)(Mdl + 1);
    Base = Mdl->StartVa;

    /* Set the system address and now get the page count */
    Mdl->MappedSystemVa = (PVOID)((ULONG_PTR)Base + Mdl->ByteOffset);

    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Mdl->MappedSystemVa, Mdl->ByteCount);
    ASSERT(PageCount != 0);

    /* Loop the PTEs */
    Pte = MiAddressToPte(Base);
    EndPage = (MdlPages + PageCount);

    do
    {
        /* Write the PFN */
        Pfn = PFN_FROM_PTE(Pte++);
        *MdlPages++ = Pfn;
    }
    while (MdlPages < EndPage);

    /* Set the nonpaged pool flag */
    Mdl->MdlFlags |= MDL_SOURCE_IS_NONPAGED_POOL;

    /* Check if this is an I/O mapping */
    if (!MiGetPfnEntry(Pfn))
        Mdl->MdlFlags |= MDL_IO_SPACE;
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
