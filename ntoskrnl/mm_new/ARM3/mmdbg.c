
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PVOID MiDebugMapping = MI_DEBUG_MAPPING;
PMMPTE MmDebugPte = NULL;

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
MmDbgCopyMemory(
    _In_ ULONG64 Address,
    _In_ PVOID Buffer,
    _In_ ULONG Size,
    _In_ ULONG Flags)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
