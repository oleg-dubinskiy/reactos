
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

/* Number of initial session IDs */
#define MI_INITIAL_SESSION_IDS  64

/* Number of session data and tag pages */
#define MI_SESSION_DATA_PAGES_MAXIMUM (MM_ALLOCATION_GRANULARITY / PAGE_SIZE)
#define MI_SESSION_TAG_PAGES_MAXIMUM  (MM_ALLOCATION_GRANULARITY / PAGE_SIZE)

PMM_SESSION_SPACE MmSessionSpace;
KGUARDED_MUTEX MiSessionIdMutex;
RTL_BITMAP MiSessionWideVaBitMap;
PRTL_BITMAP MiSessionIdBitmap;
LIST_ENTRY MiSessionWsList;
LIST_ENTRY MmWorkingSetExpansionHead;
PFN_NUMBER MiSessionDataPages;
PFN_NUMBER MiSessionTagPages;
PFN_NUMBER MiSessionTagSizePages;
PFN_NUMBER MiSessionBigPoolPages;
PFN_NUMBER MiSessionCreateCharge;

extern PVOID MiSessionImageStart;
extern PVOID MiSessionImageEnd;
extern PFN_NUMBER MmLowestPhysicalPage;

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
    /* Check if it is in range */
    return (MI_IS_SESSION_ADDRESS(Address) ? TRUE : FALSE);
}

VOID
NTAPI
MiInitializeSessionWideAddresses(VOID)
{
    PULONG BitMapBuffer;
    ULONG SizeOfBitMap;
    ULONG Size;

    DPRINT("MiInitializeSessionWideAddresses()\n");

    SizeOfBitMap = (ULONG)((ULONG_PTR)MiSessionImageEnd - (ULONG_PTR)MiSessionImageStart);
    SizeOfBitMap /= PAGE_SIZE;
    Size = (((SizeOfBitMap + 0x1F) / 0x20) * sizeof(ULONG));

    BitMapBuffer = ExAllocatePoolWithTag(PagedPool, Size, '  mM');
    if (!BitMapBuffer)
    {
        KeBugCheckEx(0x7D, MmNumberOfPhysicalPages, MmLowestPhysicalPage, MmHighestPhysicalPage, 0x301);
    }

    RtlInitializeBitMap(&MiSessionWideVaBitMap, BitMapBuffer, SizeOfBitMap);
    RtlClearAllBits(&MiSessionWideVaBitMap);
}

/* EOF */
