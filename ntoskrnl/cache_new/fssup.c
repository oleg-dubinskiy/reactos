
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

PFILE_OBJECT
NTAPI
CcGetFileObjectFromBcb(PVOID Bcb)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PFILE_OBJECT
NTAPI
CcGetFileObjectFromSectionPtrs(IN PSECTION_OBJECT_POINTERS SectionObjectPointer)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
CcInitializeCacheMap(IN PFILE_OBJECT FileObject,
                     IN PCC_FILE_SIZES FileSizes,
                     IN BOOLEAN PinAccess,
                     IN PCACHE_MANAGER_CALLBACKS Callbacks,
                     IN PVOID LazyWriteContext)
{
    UNIMPLEMENTED_DBGBREAK();
}

BOOLEAN
NTAPI
CcPurgeCacheSection(IN PSECTION_OBJECT_POINTERS SectionObjectPointer,
                    IN OPTIONAL PLARGE_INTEGER FileOffset,
                    IN ULONG Length,
                    IN BOOLEAN UninitializeCacheMaps)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

VOID
NTAPI
CcSetDirtyPageThreshold(IN PFILE_OBJECT FileObject,
                        IN ULONG DirtyPageThreshold)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcSetFileSizes(IN PFILE_OBJECT FileObject,
               IN PCC_FILE_SIZES FileSizes)
{
    UNIMPLEMENTED_DBGBREAK();
}

BOOLEAN
NTAPI
CcUninitializeCacheMap(IN PFILE_OBJECT FileObject,
                       IN OPTIONAL PLARGE_INTEGER TruncateSize,
                       IN OPTIONAL PCACHE_UNINITIALIZE_EVENT UninitializeEvent)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
CcZeroData(IN PFILE_OBJECT FileObject,
           IN PLARGE_INTEGER StartOffset,
           IN PLARGE_INTEGER EndOffset,
           IN BOOLEAN Wait)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

INIT_FUNCTION
BOOLEAN
NTAPI
CcInitializeCacheManager(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

/* EOF */
