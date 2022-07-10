
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmAddPhysicalMemory(
    _In_ PPHYSICAL_ADDRESS StartAddress,
    _Inout_ PLARGE_INTEGER NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PPHYSICAL_MEMORY_RANGE
NTAPI
MmGetPhysicalMemoryRanges(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

NTSTATUS
NTAPI
MmMarkPhysicalMemoryAsBad(
    _In_ PPHYSICAL_ADDRESS StartAddress,
    _Inout_ PLARGE_INTEGER NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmMarkPhysicalMemoryAsGood(
    _In_ PPHYSICAL_ADDRESS StartAddress,
    _Inout_ PLARGE_INTEGER NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmRemovePhysicalMemory(
    _In_ PPHYSICAL_ADDRESS StartAddress,
    _Inout_ PLARGE_INTEGER NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
