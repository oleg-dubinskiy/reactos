
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/

#if DBG && defined(KDBG)

BOOLEAN
ExpKdbgExtPool(
    ULONG Argc,
    PCHAR Argv[])
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
ExpKdbgExtPoolUsed(
    ULONG Argc,
    PCHAR Argv[])
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
ExpKdbgExtPoolFind(
    ULONG Argc,
    PCHAR Argv[])
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
ExpKdbgExtIrpFind(
    ULONG Argc,
    PCHAR Argv[])
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

#endif // DBG && KDBG

/* EOF */
