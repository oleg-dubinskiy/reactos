
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
CcInitializeCacheMap(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCC_FILE_SIZES FileSizes,
    _In_ BOOLEAN PinAccess,
    _In_ PCACHE_MANAGER_CALLBACKS Callbacks,
    _In_ PVOID LazyWriteContext)
{
    PCACHE_UNINITIALIZE_EVENT Event;
    PCACHE_UNINITIALIZE_EVENT NextEvent;
    PSHARED_CACHE_MAP NewSharedMap = NULL;
    PSHARED_CACHE_MAP SharedMap;
    PPRIVATE_CACHE_MAP PrivateMap;
    CC_FILE_SIZES fileSizes;
    PVOID NewMap = NULL;
    BOOLEAN IsLocked = FALSE;
    BOOLEAN IsNewSharedMap = FALSE;
    BOOLEAN IsSectionInit = FALSE;
    BOOLEAN IsReferenced = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("CcInitializeCacheMap: File %p, '%wZ'\n", FileObject, &FileObject->FileName);

    RtlCopyMemory(&fileSizes, FileSizes, sizeof(fileSizes));

    DPRINT("CcInitializeCacheMap: AllocationSize %I64X, FileSize %I64X, ValidDataLength %I64X\n",
           fileSizes.AllocationSize.QuadPart, fileSizes.FileSize.QuadPart, fileSizes.ValidDataLength.QuadPart);

    if (!fileSizes.AllocationSize.QuadPart)
        fileSizes.AllocationSize.QuadPart = 1;

    if (FileObject->WriteAccess)
    {
        fileSizes.AllocationSize.QuadPart += (0x100000 - 1);
        fileSizes.AllocationSize.LowPart &= ~(0x100000 - 1);
    }
    else
    {
        fileSizes.AllocationSize.QuadPart += (VACB_MAPPING_GRANULARITY - 1);
        fileSizes.AllocationSize.LowPart &= ~(VACB_MAPPING_GRANULARITY - 1);
    }

    while (TRUE)
    {
        if (!FileObject->SectionObjectPointer->SharedCacheMap)
        {
            NewSharedMap = CcCreateSharedCacheMap(FileObject, &fileSizes, PinAccess, Callbacks, LazyWriteContext);
            if (!NewSharedMap)
            {
                DPRINT1("CcInitializeCacheMap: STATUS_INSUFFICIENT_RESOURCES\n");
                RtlRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
                return;
            }

            NewMap = NewSharedMap;
        }

        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

        if (FileObject->PrivateCacheMap)
        {
            KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

            if (NewSharedMap)
                ExFreePoolWithTag(NewSharedMap, 'cScC');

            return;
        }

        SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;

        if (SharedMap)
        {
            if (!(FileObject->Flags & FO_SEQUENTIAL_ONLY))
                SharedMap->Flags &= ~SHARE_FL_SEQUENTIAL_ONLY;

            break;
        }
        else if (NewSharedMap)
        {
            InsertTailList(&CcCleanSharedCacheMapList, &NewSharedMap->SharedCacheMapLinks);

            IsNewSharedMap = TRUE;
            SharedMap = NewSharedMap;
            NewMap = NULL;

            FileObject->SectionObjectPointer->SharedCacheMap = SharedMap;
            ObReferenceObject(FileObject);
            break;
        }
        else
        {
            KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
        }
    }

    if (FileObject->Flags & FO_RANDOM_ACCESS)
        SharedMap->Flags |= SHARE_FL_RANDOM_ACCESS;

    SharedMap->Flags &= ~0x10;

    if (SharedMap->Vacbs)
    {
        if (!(SharedMap->Flags & SHARE_FL_SECTION_INIT))
        {
            SharedMap->OpenCount++;
            KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
            IsLocked = FALSE;
            IsReferenced = TRUE;
        }
    }
    else if (!(SharedMap->Flags & SHARE_FL_SECTION_INIT)) // SharedMap->Vacbs == NULL
    {
        SharedMap->OpenCount++;
        SharedMap->Flags |= SHARE_FL_SECTION_INIT;

        if (SharedMap->CreateEvent)
            KeInitializeEvent(SharedMap->CreateEvent, NotificationEvent, FALSE);

        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
        IsLocked = FALSE;

        IsReferenced = TRUE;
        IsSectionInit = TRUE;

        if (SharedMap->Section)
        {
            if (fileSizes.AllocationSize.QuadPart > SharedMap->SectionSize.QuadPart)
            {
                NTSTATUS status;

                DPRINT1("CcInitializeCacheMap: FIXME MmExtendSection()\n");
                ASSERT(FALSE);
                status = STATUS_NOT_IMPLEMENTED;//MmExtendSection(SharedMap->Section, &fileSizes.AllocationSize, 1);
                if (!NT_SUCCESS(status))
                {
                    DPRINT1("CcInitializeCacheMap: Status %X\n", Status);
                    Status = FsRtlNormalizeNtstatus(status, STATUS_UNEXPECTED_MM_EXTEND_ERR);
                    goto Finish;
                }
            }

            DPRINT1("CcInitializeCacheMap: FIXME CcExtendVacbArray()\n");
            ASSERT(FALSE);
            Status = STATUS_NOT_IMPLEMENTED;//CcExtendVacbArray(SharedMap, fileSizes.AllocationSize);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("CcInitializeCacheMap: Status %X\n", Status);
                goto Finish;
            }
        }
        else
        {
            PFSRTL_COMMON_FCB_HEADER Fcb;

            SharedMap->Status = MmCreateSection(&SharedMap->Section,
                                                (SECTION_QUERY | SECTION_MAP_WRITE | SECTION_MAP_READ),
                                                NULL,
                                                &fileSizes.AllocationSize,
                                                PAGE_READWRITE,
                                                SEC_COMMIT,
                                                NULL,
                                                FileObject);
            if (!NT_SUCCESS(SharedMap->Status))
            {
                DPRINT1("CcInitializeCacheMap: SharedMap->Status %X\n", SharedMap->Status);
                SharedMap->Section = NULL;
                Status = FsRtlNormalizeNtstatus(SharedMap->Status, STATUS_UNEXPECTED_MM_CREATE_ERR);
                goto Finish;
            }

            ObDeleteCapturedInsertInfo(SharedMap->Section);

            Fcb = FileObject->FsContext;

            if (!(Fcb->Flags2 & FSRTL_FLAG2_DO_MODIFIED_WRITE) && !FileObject->FsContext2)
            {
                DPRINT1("CcInitializeCacheMap: FIXME MmDisableModifiedWriteOfSection()\n");
                ASSERT(FALSE);
                //MmDisableModifiedWriteOfSection(FileObject->SectionObjectPointer);

                OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
                SharedMap->Flags |= SHARE_FL_MODIFIED_NO_WRITE;
                KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
            }

            DPRINT1("CcInitializeCacheMap: FIXME CcCreateVacbArray()\n");
            ASSERT(FALSE);
            Status = STATUS_NOT_IMPLEMENTED;//CcCreateVacbArray(SharedMap, fileSizes.AllocationSize);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("CcInitializeCacheMap: Status %X\n", Status);
                goto Finish;
            }
        }

        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

        SharedMap->Flags &= ~SHARE_FL_SECTION_INIT;

        if (SharedMap->CreateEvent)
            KeSetEvent(SharedMap->CreateEvent, 0, FALSE);

        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        IsSectionInit = FALSE;
    }
    else // !(SharedMap->Vacbs) && (SharedMap->Flags & SHARE_FL_SECTION_INIT)
    {
        if (!SharedMap->CreateEvent)
        {
            SharedMap->CreateEvent = ExAllocatePoolWithTag(NonPagedPool, sizeof(KEVENT), 'vEcC');
            if (!SharedMap->CreateEvent)
            {
                KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
                IsLocked = FALSE;

                DPRINT1("CcInitializeCacheMap: STATUS_INSUFFICIENT_RESOURCES\n");
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Finish;
            }

            KeInitializeEvent(SharedMap->CreateEvent, NotificationEvent, FALSE);
        }

        SharedMap->OpenCount++;

        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
        IsLocked = FALSE;
        IsReferenced = TRUE;

        KeWaitForSingleObject(SharedMap->CreateEvent, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(SharedMap->Status))
        {
            DPRINT1("CcInitializeCacheMap: SharedMap->Status %X\n", SharedMap->Status);
            Status = FsRtlNormalizeNtstatus(SharedMap->Status, STATUS_UNEXPECTED_MM_CREATE_ERR);
            goto Finish;
        }
    }

    if (NewMap)
    {
        ExFreePoolWithTag(NewMap, 'cScC');
        NewMap = NULL;
    }

    PrivateMap = &SharedMap->PrivateCacheMap;
    if (PrivateMap->NodeTypeCode)
    {
        NewMap = ExAllocatePoolWithTag(NonPagedPool, sizeof(PRIVATE_CACHE_MAP), 'cPcC');
        if (!NewMap)
        {
            DPRINT1("CcInitializeCacheMap: STATUS_INSUFFICIENT_RESOURCES\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Finish;
        }
    }

    while (TRUE)
    {
        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
        IsLocked = TRUE;

        if (FileObject->PrivateCacheMap)
        {
            ASSERT(SharedMap->OpenCount > 1);
            SharedMap->OpenCount--;
            SharedMap = NULL;
            IsReferenced = FALSE;
            break;
        }

        if (PrivateMap->NodeTypeCode)
        {
            if (!NewMap)
            {
                KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
                IsLocked = FALSE;

                NewMap = ExAllocatePoolWithTag(NonPagedPool, sizeof(PRIVATE_CACHE_MAP), 'cPcC');
                if (!NewMap)
                {
                    DPRINT1("CcInitializeCacheMap: STATUS_INSUFFICIENT_RESOURCES\n");
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                continue;
            }

            PrivateMap = NewMap;
            NewMap = NULL;
        }

        RtlZeroMemory(PrivateMap, sizeof(*PrivateMap));

        PrivateMap->NodeTypeCode = NODE_TYPE_PRIVATE_MAP;
        PrivateMap->FileObject = FileObject;
        FileObject->PrivateCacheMap = PrivateMap;

        PrivateMap->ReadAheadMask = (PAGE_SIZE - 1);
        KeInitializeSpinLock(&PrivateMap->ReadAheadSpinLock);

        InsertTailList(&SharedMap->PrivateList, &PrivateMap->PrivateLinks);
        IsReferenced = FALSE;

        break;
    }

Finish:

    if (IsReferenced)
    {
        if (!IsLocked)
            OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

        if (IsSectionInit)
        {
            if (SharedMap->CreateEvent)
                KeSetEvent(SharedMap->CreateEvent, 0, FALSE);

            SharedMap->Flags &= ~SHARE_FL_SECTION_INIT;
        }

        SharedMap->OpenCount--;

        if (!SharedMap->OpenCount &&
            !(SharedMap->Flags & SHARE_FL_WRITE_QUEUED) &&
            !SharedMap->DirtyPages)
        {
            DPRINT1("CcInitializeCacheMap: FIXME CcDeleteSharedCacheMap()\n");
            ASSERT(FALSE);
            //CcDeleteSharedCacheMap(SharedMap, OldIrql, FALSE);
        }
        else
        {
            KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
        }

        IsLocked = FALSE;
        goto Exit;
    }

    if (!SharedMap)
        goto Exit;

    if (!IsLocked)
    {
        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
        IsLocked = TRUE;
    }

    if (!IsNewSharedMap &&
        !SharedMap->DirtyPages &&
        SharedMap->OpenCount)
    {
        if (KdDebuggerEnabled && !KdDebuggerNotPresent &&
            !SharedMap->OpenCount &&
            !SharedMap->DirtyPages)
        {
            DPRINT1("CC: SharedMap->OpenCount == 0 && DirtyPages == 0 && going onto CleanList!\n");
            DbgBreakPoint();
        }

        RemoveEntryList(&SharedMap->SharedCacheMapLinks);
        InsertTailList(&CcCleanSharedCacheMapList, &SharedMap->SharedCacheMapLinks);
    }

    for (Event = SharedMap->UninitializeEvent; Event; Event = NextEvent)
    {
        Event = (PCACHE_UNINITIALIZE_EVENT)((ULONG_PTR)Event & ~(1));
        NextEvent = Event->Next;
        KeSetEvent(&Event->Event, 0, FALSE);
    }

    SharedMap->Flags &= ~SHARE_FL_WAITING_TEARDOWN;
    SharedMap->UninitializeEvent = NULL;

Exit:

    if (IsLocked)
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

    if (NewMap)
        ExFreePoolWithTag(NewMap, 'cScC');

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CcInitializeCacheMap: Status %X\n", Status);
        RtlRaiseStatus(Status);
    }
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
