
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmAdjustWorkingSetSize(
    _In_ SIZE_T WorkingSetMinimumInBytes,
    _In_ SIZE_T WorkingSetMaximumInBytes,
    _In_ ULONG SystemCache,
    _In_ BOOLEAN IncreaseOkay)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmCreateMirror(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;    
}

BOOLEAN
NTAPI
MmIsAddressValid(
    _In_ PVOID VirtualAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
MmIsNonPagedSystemAddressValid(
    _In_ PVOID VirtualAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
MmIsRecursiveIoFault(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
MmIsThisAnNtAsSystem(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
MmMapUserAddressesToPage(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ PVOID PageAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;    
}

MM_SYSTEMSIZE
NTAPI
MmQuerySystemSize(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

BOOLEAN
NTAPI
MmSetAddressRangeModified(
    _In_ PVOID Address,
    _In_ SIZE_T Length)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
MmSetBankedSection(    
    _In_ HANDLE ProcessHandle,
    _In_ PVOID VirtualAddress,
    _In_ ULONG BankLength,
    _In_ BOOLEAN ReadWriteBank,
    _In_ PVOID BankRoutine,
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;    
}

/* EOF */
