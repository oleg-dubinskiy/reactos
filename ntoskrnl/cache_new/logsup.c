
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

LARGE_INTEGER
NTAPI
CcGetDirtyPages(IN PVOID LogHandle,
                IN PDIRTY_PAGE_ROUTINE DirtyPageRoutine,
                IN PVOID Context1,
                IN PVOID Context2)
{
    LARGE_INTEGER Result = {{0}};
    UNIMPLEMENTED_DBGBREAK();
    return Result;
}

LARGE_INTEGER
NTAPI
CcGetLsnForFileObject(IN PFILE_OBJECT FileObject,
                      OUT OPTIONAL PLARGE_INTEGER OldestLsn)
{
    LARGE_INTEGER Result = {{0}};
    UNIMPLEMENTED_DBGBREAK();
    return Result;
}

BOOLEAN
NTAPI
CcIsThereDirtyData(IN PVPB Vpb)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

VOID
NTAPI
CcSetAdditionalCacheAttributes(IN PFILE_OBJECT FileObject,
                               IN BOOLEAN DisableReadAhead,
                               IN BOOLEAN DisableWriteBehind)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcSetLogHandleForFile(IN PFILE_OBJECT FileObject,
                      IN PVOID LogHandle,
                      IN PFLUSH_TO_LSN FlushToLsnRoutine)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
