
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

SHARED_CACHE_MAP_LIST_CURSOR CcDirtySharedCacheMapList;
SHARED_CACHE_MAP_LIST_CURSOR CcLazyWriterCursor;
GENERAL_LOOKASIDE CcTwilightLookasideList;

LIST_ENTRY CcCleanSharedCacheMapList;

extern LIST_ENTRY CcRegularWorkQueue;
extern LIST_ENTRY CcExpressWorkQueue;
extern LIST_ENTRY CcFastTeardownWorkQueue;
extern LIST_ENTRY CcPostTickWorkQueue;
extern LARGE_INTEGER CcCollisionDelay;
extern LAZY_WRITER LazyWriter;

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

VOID
NTAPI
CcDeleteSharedCacheMap(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ KIRQL OldIrql,
    _In_ BOOLEAN IsReleaseFile)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcWaitForUninitializeCacheMap(
    _In_ PFILE_OBJECT FileObject)
{
    PSHARED_CACHE_MAP SharedMap;
    CACHE_UNINITIALIZE_EVENT event;
    PCACHE_UNINITIALIZE_EVENT Event;
    LARGE_INTEGER Timeout;
    BOOLEAN IsWait = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("CcWaitForUninitializeCacheMap: FileObject %p\n", FileObject);

    if (!FileObject->SectionObjectPointer->SharedCacheMap)
        return;

    KeInitializeEvent(&event.Event, NotificationEvent, FALSE);

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;

    if (SharedMap && 
        (!SharedMap->OpenCount || IsListEmpty(&SharedMap->PrivateList)))
    {
        SharedMap->Flags |= 0x10000;
        event.Next = SharedMap->UninitializeEvent;
        SharedMap->UninitializeEvent = (PCACHE_UNINITIALIZE_EVENT)((ULONG_PTR)&event | 1);

        IsWait = TRUE; 
        CcScheduleLazyWriteScanEx(TRUE, TRUE);
    }

    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

    if (!IsWait)
        return;

    Timeout.QuadPart = 600 * (-10000ll * 1000); // 600 seconds (10 m)

    Status = KeWaitForSingleObject(&event.Event,  Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_TIMEOUT)
        return;

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;
    if (!SharedMap)
    {
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
        KeWaitForSingleObject(&event.Event,  Executive, KernelMode, FALSE, NULL);
        return;
    }

    Event = CONTAINING_RECORD(&SharedMap->UninitializeEvent, CACHE_UNINITIALIZE_EVENT, Next);

    while (Event->Next != NULL)
    {
        if (Event->Next == (PCACHE_UNINITIALIZE_EVENT)((ULONG_PTR)&event | 1))
        {
            Event->Next = event.Next;
            break;
        }

        Event = (PCACHE_UNINITIALIZE_EVENT)((ULONG_PTR)(Event->Next) & ~1);
    }

    SharedMap->Flags &= ~0x10000;
    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
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

            Status = CcCreateVacbArray(SharedMap, fileSizes.AllocationSize);
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
CcPurgeCacheSection(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_ OPTIONAL PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN UninitializeCacheMaps)
{
    PSHARED_CACHE_MAP SharedMap;
    PPRIVATE_CACHE_MAP PrivateMap;
    PVACB Vacb = NULL;
    BOOLEAN IsFullPurge;
    BOOLEAN Result;
    KIRQL OldIrql;

    DPRINT("CcPurgeCacheSection: %p, %p, %X, %X\n", SectionObjectPointer, FileOffset, Length, UninitializeCacheMaps);

    ASSERT(!UninitializeCacheMaps || (Length == 0) || (Length >= (2 * PAGE_SIZE)));

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    SharedMap = SectionObjectPointer->SharedCacheMap;
    if (SharedMap)
    {
        if (SharedMap->Flags & 0x2000)
        {
            if (!((ULONG_PTR)FileOffset & 1))
            {
               KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
               return TRUE;
            }

            FileOffset = (PLARGE_INTEGER)((ULONG_PTR)FileOffset ^ 1);
        }

        SharedMap->OpenCount++;
        KeAcquireSpinLockAtDpcLevel(&SharedMap->ActiveVacbSpinLock);

        Vacb = SharedMap->ActiveVacb;

        if (SharedMap->ActiveVacb)
        {
            SharedMap->ActiveVacb = NULL;
        }

        KeReleaseSpinLockFromDpcLevel(&SharedMap->ActiveVacbSpinLock);
    }

    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

    if (Vacb)
    {
        DPRINT1("CcPurgeCacheSection: FIXME\n");
        ASSERT(FALSE);
    }

    if (SharedMap)
    {
        if (UninitializeCacheMaps)
        {
            while (!IsListEmpty(&SharedMap->PrivateList))
            {
                PrivateMap = CONTAINING_RECORD(SharedMap->PrivateList.Flink, PRIVATE_CACHE_MAP, PrivateLinks);
                CcUninitializeCacheMap(PrivateMap->FileObject, NULL, NULL);
            }
        }

        while (SharedMap->Vacbs && !CcUnmapVacbArray(SharedMap, FileOffset, Length, FALSE))
        {
            DPRINT1("CcPurgeCacheSection: FIXME\n");
            ASSERT(FALSE);
        }
    }

    while (TRUE)
    {
        if (SharedMap && FileOffset)
            IsFullPurge = TRUE;
        else
            IsFullPurge = FALSE;

        Result = MmPurgeSection(SectionObjectPointer, FileOffset, Length, IsFullPurge);
        if (Result)
            break;

        if (Length)
            break;

        if (!MmCanFileBeTruncated(SectionObjectPointer, FileOffset))
            break;

        KeDelayExecutionThread(KernelMode, FALSE, &CcCollisionDelay);
    }

    if (SharedMap)
    {
        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

        SharedMap->OpenCount--;

        if (!SharedMap->OpenCount &&
            !(SharedMap->Flags & SHARE_FL_WRITE_QUEUED) &&
            !SharedMap->DirtyPages)
        {
            DPRINT1("CcPurgeCacheSection: FIXME\n");
            ASSERT(FALSE);
        }

        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
    }

    return Result;
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
CcSetFileSizes(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCC_FILE_SIZES FileSizes)
{
    PSHARED_CACHE_MAP SharedMap;
    LARGE_INTEGER ValidDataLength;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER FileSize;
    PVACB ActiveVacb;
    KIRQL OldIrql;

    DPRINT("CcSetFileSizes: FileObject %p FileSizes %X\n", FileObject, FileSizes);

    FileSize = FileSizes->FileSize;
    AllocationSize = FileSizes->AllocationSize;
    ValidDataLength = FileSizes->ValidDataLength;

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;

    if (!SharedMap || !SharedMap->Section)
    {
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        if (BYTE_OFFSET(FileSize.LowPart))
        {
            DPRINT1("CcSetFileSizes: FIXME MmFlushSection()\n");
            ASSERT(FALSE);
        }

        CcPurgeCacheSection(FileObject->SectionObjectPointer, &FileSize, 0, FALSE);
        return;
    }

    if (AllocationSize.QuadPart > SharedMap->SectionSize.QuadPart)
    {
        SharedMap->OpenCount++;
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        AllocationSize.QuadPart += (0x100000 - 1);
        AllocationSize.LowPart &= ~(0x100000 - 1);

        DPRINT1("CcSetFileSizes: FIXME MmFlushSection()\n");
        ASSERT(FALSE);
    }

    SharedMap->OpenCount++;

    if (FileSize.QuadPart < SharedMap->ValidDataGoal.QuadPart ||
        FileSize.QuadPart < SharedMap->FileSize.QuadPart)
    {
        KeAcquireSpinLockAtDpcLevel(&SharedMap->ActiveVacbSpinLock);

        ActiveVacb = SharedMap->ActiveVacb;

        if (SharedMap->ActiveVacb)
        {
            SharedMap->ActiveVacb = NULL;
        }

        KeReleaseSpinLockFromDpcLevel(&SharedMap->ActiveVacbSpinLock);

        if (ActiveVacb || SharedMap->NeedToZero)
        {
            KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
            DPRINT1("CcSetFileSizes: FIXME\n");
            ASSERT(FALSE);
        }
    }

    if (SharedMap->ValidDataLength.QuadPart != MAXLONGLONG)
    {
        if (SharedMap->ValidDataLength.QuadPart > FileSize.QuadPart)
            SharedMap->ValidDataLength.QuadPart = FileSize.QuadPart;

        SharedMap->ValidDataGoal.QuadPart = ValidDataLength.QuadPart;
    }

    if (FileSize.QuadPart < SharedMap->FileSize.QuadPart &&
        !(SharedMap->Flags & SHARE_FL_PIN_ACCESS) &&
        !SharedMap->VacbActiveCount)
    {
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        DPRINT1("CcSetFileSizes: FIXME\n");
        ASSERT(FALSE);
    }

    SharedMap->OpenCount--;
    SharedMap->FileSize.QuadPart = FileSize.QuadPart;

    if (!SharedMap->OpenCount &&
        !(SharedMap->Flags & SHARE_FL_WRITE_QUEUED) &&
        !SharedMap->DirtyPages)
    {
        DPRINT1("CcSetFileSizes: FIXME\n");
        ASSERT(FALSE);
    }

    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
}

BOOLEAN
NTAPI
CcUninitializeCacheMap(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER TruncateSize OPTIONAL,
    _In_ PCACHE_UNINITIALIZE_EVENT Event OPTIONAL)
{
    PSHARED_CACHE_MAP SharedMap;
    PPRIVATE_CACHE_MAP PrivateMap;
    PVACB Vacb = NULL;
    ULONG ActivePage;
    BOOLEAN Result = FALSE;
    KIRQL OldIrql;

    DPRINT("CcUninitializeCacheMap: %p [%wZ], %p\n", FileObject, &FileObject->FileName, FileObject->SectionObjectPointer);

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;
    PrivateMap = FileObject->PrivateCacheMap;

    if (PrivateMap)
    {
        ASSERT(PrivateMap->FileObject == FileObject);

        SharedMap->OpenCount--;
        RemoveEntryList(&PrivateMap->PrivateLinks);

        if (PrivateMap == &SharedMap->PrivateCacheMap)
        {
            PrivateMap->NodeTypeCode = 0;
            PrivateMap = NULL;
        }

        FileObject->PrivateCacheMap = NULL;
    }

    if (!SharedMap)
    {
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        if (TruncateSize &&
            !TruncateSize->QuadPart &&
            FileObject->SectionObjectPointer->DataSectionObject)
        {
            CcPurgeCacheSection(FileObject->SectionObjectPointer, TruncateSize, 0, FALSE);
        }

        if (Event)
            KeSetEvent(&Event->Event, 0, FALSE);

        goto Exit;
    }

    if (TruncateSize)
    {
        if (!TruncateSize->QuadPart && SharedMap->FileSize.QuadPart)
            SharedMap->Flags |= SHARE_FL_TRUNCATE_SIZE;
        else if (IsListEmpty(&SharedMap->PrivateList))
            SharedMap->FileSize.QuadPart = TruncateSize->QuadPart;
    }

    if (SharedMap->OpenCount)
    {
        if (Event)
        {
            if (IsListEmpty(&SharedMap->PrivateList))
            {
                Event->Next = SharedMap->UninitializeEvent;
                SharedMap->UninitializeEvent = Event;
            }
            else
            {
                KeSetEvent(&Event->Event, 0, FALSE);
            }
        }

        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        Result = FALSE;
        goto Exit;
    }

    if (SharedMap->Flags & 0x2000)
    {
        SharedMap->Flags = (SharedMap->Flags & ~(0x2000 | 0x2));

        DPRINT1("CcUninitializeCacheMap: FIXME MmEnableModifiedWriteOfSection()\n");
        ASSERT(FALSE);
    }

    ASSERT(IsListEmpty(&SharedMap->PrivateList));

    if (Event)
    {
        Event->Next = SharedMap->UninitializeEvent;
        SharedMap->UninitializeEvent = Event;
    }

    if ((SharedMap->Flags & SHARE_FL_PIN_ACCESS) || Event)
    {
        if (!(SharedMap->Flags & SHARE_FL_WRITE_QUEUED) &&
            !SharedMap->DirtyPages &&
            !(SharedMap->Flags & 0x8000))
        {
            CcDeleteSharedCacheMap(SharedMap, OldIrql, FALSE);
            Result = TRUE;
            goto Exit;
        }
    }

    if (!(SharedMap->Flags & SHARE_FL_WRITE_QUEUED))
    {
        RemoveEntryList(&SharedMap->SharedCacheMapLinks);
        InsertTailList(&CcDirtySharedCacheMapList.SharedCacheMapLinks, &SharedMap->SharedCacheMapLinks);
    }

    LazyWriter.OtherWork = 1;

    if (!LazyWriter.ScanActive)
        CcScheduleLazyWriteScan(FALSE);

    KeAcquireSpinLockAtDpcLevel(&SharedMap->ActiveVacbSpinLock);

    Vacb = SharedMap->ActiveVacb;
    if (Vacb)
    {
        SharedMap->ActiveVacb = NULL;
        ActivePage = SharedMap->ActivePage;
    }

    KeReleaseSpinLockFromDpcLevel(&SharedMap->ActiveVacbSpinLock);
    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

    DPRINT("CcUninitializeCacheMap: SharedMap %p, Vacb %p\n", SharedMap, Vacb);

    if (Vacb)
    {
        BOOLEAN IsVacbLocked = ((SharedMap->Flags & SHARE_FL_VACB_LOCKED) != 0);
        CcFreeActiveVacb(Vacb->SharedCacheMap, Vacb, ActivePage, IsVacbLocked);
    }

Exit:

    if (PrivateMap)
        ExFreePoolWithTag(PrivateMap, 'cPcC');

    return Result;
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

    InitializeListHead(&CcDirtySharedCacheMapList.SharedCacheMapLinks);
    CcDirtySharedCacheMapList.Flags = 0x800;

    InsertTailList(&CcDirtySharedCacheMapList.SharedCacheMapLinks, &CcLazyWriterCursor.SharedCacheMapLinks);
    CcLazyWriterCursor.Flags = 0x800;

    InitializeListHead(&CcFastTeardownWorkQueue);
    InitializeListHead(&CcExpressWorkQueue);
    InitializeListHead(&CcRegularWorkQueue);
    InitializeListHead(&CcPostTickWorkQueue);

    RtlZeroMemory(&LazyWriter, sizeof(LazyWriter));

    InitializeListHead(&LazyWriter.WorkQueue);
    KeInitializeDpc(&LazyWriter.ScanDpc, CcScanDpc, NULL);
    KeInitializeTimer(&LazyWriter.ScanTimer);


    CcInitializeVacbs();

    return TRUE;
}

/* EOF */
