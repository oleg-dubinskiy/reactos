
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY PsLoadedModuleList;
LIST_ENTRY MmLoadedUserImageList;
ERESOURCE PsLoadedModuleResource;
ULONG_PTR PsNtosImageBase;
KMUTANT MmSystemLoadLock;

PVOID MmUnloadedDrivers;
PVOID MmLastUnloadedDrivers;

BOOLEAN MmMakeLowMemory;
BOOLEAN MmEnforceWriteProtection = FALSE; // FIXME: should be TRUE, but that would cause CORE-16387 & CORE-16449

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
MmUnloadSystemImage(
    _In_ PVOID ImageHandle)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmLoadSystemImage(
    _In_ PUNICODE_STRING FileName,
    _In_ PUNICODE_STRING NamePrefix OPTIONAL,
    _In_ PUNICODE_STRING LoadedName OPTIONAL,
    _In_ ULONG Flags,
    _Out_ PVOID* ModuleObject,
    _Out_ PVOID* ImageBaseAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
MmChangeKernelResourceSectionProtection(
    _In_ ULONG_PTR ProtectionMask)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

VOID
NTAPI
MmFreeDriverInitialization(
    _In_ PVOID DriverSection)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MiResolveImageReferences(
    _In_ PVOID ImageBase,
    _In_ PUNICODE_STRING ImageFileDirectory,
    _In_ PUNICODE_STRING NamePrefix OPTIONAL,
    _Out_ PCHAR* MissingApi,
    _Out_ PWCHAR* MissingDriver,
    _Out_ PLOAD_IMPORTS* LoadImports)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmCallDllInitialize(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PLIST_ENTRY ListHead)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
MmMakeKernelResourceSectionWritable(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmCheckSystemImage(
    _In_ HANDLE ImageHandle,
    _In_ BOOLEAN PurgeSection)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
MmGetSystemRoutineAddress(
    _In_ PUNICODE_STRING SystemRoutineName)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
MmPageEntireDriver(
    _In_ PVOID AddressWithinSection)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmResetDriverPaging(
    _In_ PVOID AddressWithinSection)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
