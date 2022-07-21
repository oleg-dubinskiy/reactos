
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/

ULONG
NTAPI
MmGetPageProtect(PEPROCESS Process, PVOID Address)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

VOID
NTAPI
MmSetPageProtect(PEPROCESS Process, PVOID Address, ULONG flProtect)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
MmInitGlobalKernelPageDirectory(VOID)
{
    DPRINT1("MmInitGlobalKernelPageDirectory: FIXME\n");
    ASSERT(FALSE);
}


/* EOF */
