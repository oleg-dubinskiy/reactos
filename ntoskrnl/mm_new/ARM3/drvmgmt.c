
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

MM_DRIVER_VERIFIER_DATA MmVerifierData;
WCHAR MmVerifyDriverBuffer[512] = {0};
ULONG MmVerifyDriverBufferLength = sizeof(MmVerifyDriverBuffer);
ULONG MmVerifyDriverBufferType = REG_NONE;
ULONG MmVerifyDriverLevel = -1;
PVOID MmTriageActionTaken;
PVOID KernelVerifier;

/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmAddVerifierThunks(
    _In_ PVOID ThunkBuffer,
    _In_ ULONG ThunkBufferSize)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

LOGICAL
NTAPI
MmIsDriverVerifying(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
MmIsVerifierEnabled(
    _Out_ PULONG VerifierFlags)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PVOID
NTAPI
MmLockPageableDataSection(
    _In_ PVOID AddressWithinSection)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmLockPageableSectionByHandle(
    _In_ PVOID ImageSectionHandle)
{
    UNIMPLEMENTED_DBGBREAK();
}

ULONG
NTAPI
MmTrimAllSystemPageableMemory(
    _In_ ULONG PurgeTransitionList)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

VOID
NTAPI
MmUnlockPageableImageSection(
    _In_ PVOID ImageSectionHandle)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
