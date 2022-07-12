
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

PMM_SESSION_SPACE MmSessionSpace;

/* FUNCTIONS ******************************************************************/

LCID
NTAPI
MmGetSessionLocaleId(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
MmSetSessionLocaleId(
    _In_ LCID LocaleId)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmSessionDelete(
    _In_ ULONG SessionId)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmSessionCreate(
    _Out_ PULONG SessionId)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PVOID
NTAPI
MmGetSessionById(
    _In_ ULONG SessionId)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
MmAttachSession(
    _Inout_ PVOID SessionEntry,
    _Out_ PKAPC_STATE ApcState)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
MmQuitNextSession(
    _Inout_ PVOID SessionEntry)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmDetachSession(
    _In_ PVOID OpaqueSession,
    _In_ PRKAPC_STATE ApcState)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

ULONG
NTAPI
MmGetSessionId(
    _In_ PEPROCESS Process)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

BOOLEAN
NTAPI
MmIsSessionAddress(
    _In_ PVOID Address)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}


/* EOF */
