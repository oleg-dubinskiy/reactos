
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY CcCleanSharedCacheMapList;

/* FUNCTIONS ******************************************************************/

PSHARED_CACHE_MAP
NTAPI
CcCreateSharedCacheMap(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCC_FILE_SIZES FileSizes,
    _In_ BOOLEAN PinAccess,
    _In_ PCACHE_MANAGER_CALLBACKS Callbacks,
    _In_ PVOID LazyWriteContext)
{
    PSHARED_CACHE_MAP SharedMap;

    SharedMap = ExAllocatePoolWithTag(NonPagedPool, sizeof(SHARED_CACHE_MAP), 'cScC');
    if (!SharedMap)
    {
        DPRINT1("CcCreateSharedCacheMap: STATUS_INSUFFICIENT_RESOURCES\n");
        return SharedMap;
    }

    RtlZeroMemory(SharedMap, sizeof(SHARED_CACHE_MAP));

    SharedMap->NodeTypeCode = NODE_TYPE_SHARED_MAP;
    SharedMap->NodeByteSize = sizeof(SHARED_CACHE_MAP);
    SharedMap->FileObject = FileObject;

    /* Set new file size for the file */
    SharedMap->FileSize.QuadPart = FileSizes->FileSize.QuadPart;

    /* Set new valid data length for the file */
    SharedMap->ValidDataLength.QuadPart = FileSizes->ValidDataLength.QuadPart;
    SharedMap->ValidDataGoal.QuadPart = FileSizes->ValidDataLength.QuadPart;

    KeInitializeSpinLock(&SharedMap->ActiveVacbSpinLock);
    KeInitializeSpinLock(&SharedMap->BcbSpinLock);
    ExInitializePushLock((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);

    if (PinAccess)
        SharedMap->Flags |= SHARE_FL_PIN_ACCESS;

    if (FileObject->Flags & FO_SEQUENTIAL_ONLY)
        SharedMap->Flags |= SHARE_FL_SEQUENTIAL_ONLY;

    SharedMap->Callbacks = Callbacks;
    SharedMap->LazyWriteContext = LazyWriteContext;

    InitializeListHead(&SharedMap->BcbList);
    InitializeListHead(&SharedMap->PrivateList);

    return SharedMap;
}

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
    DPRINT("CcInitializeCacheManager()\n");

    InitializeListHead(&CcCleanSharedCacheMapList);

    CcInitializeVacbs();

    return TRUE;
}

/* EOF */
