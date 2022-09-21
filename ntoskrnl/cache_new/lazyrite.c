
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LARGE_INTEGER CcNoDelay        = RTL_CONSTANT_LARGE_INTEGER(0LL);
LARGE_INTEGER CcIdleDelay      = RTL_CONSTANT_LARGE_INTEGER(-10000LL * 1000); // 1 second
LARGE_INTEGER CcFirstDelay     = RTL_CONSTANT_LARGE_INTEGER(-10000LL * 3000); // 3 second
LARGE_INTEGER CcCollisionDelay = RTL_CONSTANT_LARGE_INTEGER(-10000LL * 100);  // 100 ms

LAZY_WRITER LazyWriter;
ULONG CcTotalDirtyPages = 0;
ULONG CcNumberActiveWorkerThreads = 0;
ULONG CcDirtyPagesLastScan = 0;
ULONG CcPagesWrittenLastTime = 0;
ULONG CcPagesYetToWrite;
BOOLEAN CcQueueThrottle = FALSE;

LIST_ENTRY CcFastTeardownWorkQueue;
LIST_ENTRY CcPostTickWorkQueue;
LIST_ENTRY CcRegularWorkQueue;
LIST_ENTRY CcExpressWorkQueue;
LIST_ENTRY CcIdleWorkerThreadList;

extern SHARED_CACHE_MAP_LIST_CURSOR CcLazyWriterCursor;
extern MM_SYSTEMSIZE CcCapturedSystemSize;
extern LIST_ENTRY CcDeferredWrites;
extern ULONG CcDirtyPageTarget;

/* FUNCTIONS ******************************************************************/

VOID
FASTCALL
CcPostWorkQueue(
    _In_ PWORK_QUEUE_ENTRY WorkItem,
    _In_ PLIST_ENTRY WorkQueue)
{
    PWORK_QUEUE_ITEM ThreadToSpawn = NULL;
    PLIST_ENTRY ListEntry;
    KIRQL OldIrql;

    /* First of all, insert the item in the queue */
    OldIrql = KeAcquireQueuedSpinLock(LockQueueWorkQueueLock);
    InsertTailList(WorkQueue, &WorkItem->WorkQueueLinks);

    /* Now, define whether we have to spawn a new work thread.
       We will spawn a new one if:
       - There's no throttle in action
       - There's still at least one idle thread
    */
    if (!CcQueueThrottle && !IsListEmpty(&CcIdleWorkerThreadList))
    {
        /* Get the idle thread */
        ListEntry = RemoveHeadList(&CcIdleWorkerThreadList);
        ThreadToSpawn = CONTAINING_RECORD(ListEntry, WORK_QUEUE_ITEM, List);

        /* We're going to have one more! */
        CcNumberActiveWorkerThreads++;
    }

    KeReleaseQueuedSpinLock(LockQueueWorkQueueLock, OldIrql);

    /* If we have a thread to spawn, do it! */
    if (ThreadToSpawn)
    {
        /* We NULLify it to be consistent with initialization */
        ThreadToSpawn->List.Flink = NULL;
        ExQueueWorkItem(ThreadToSpawn, CriticalWorkQueue);
    }
}

VOID
NTAPI
CcScheduleLazyWriteScanEx(
    _In_ BOOLEAN NoDelay,
    _In_ BOOLEAN PendingTeardown)
{
    DPRINT("CcScheduleLazyWriteScanEx: NoDelay %X, PendingTeardown %X\n", NoDelay, PendingTeardown);

    /* If no delay, immediately start lazy writer, no matter it was already started */
    if (NoDelay)
    {
        LazyWriter.ScanActive = TRUE;

        if (PendingTeardown)
            LazyWriter.PendingTeardown = TRUE;

        KeSetTimer(&LazyWriter.ScanTimer, CcNoDelay, &LazyWriter.ScanDpc);
    }
    /* Otherwise, if it's not running, just wait three seconds to start it */
    else if (!LazyWriter.ScanActive)
    {
        LazyWriter.ScanActive = TRUE;
        KeSetTimer(&LazyWriter.ScanTimer, CcFirstDelay, &LazyWriter.ScanDpc);
    }
    /* Finally, already running, so queue for the next second */
    else
    {
        KeSetTimer(&LazyWriter.ScanTimer, CcIdleDelay, &LazyWriter.ScanDpc);
    }
}

VOID
NTAPI
CcScheduleLazyWriteScan(
    _In_ BOOLEAN NoDelay)
{
    CcScheduleLazyWriteScanEx(NoDelay, FALSE);
}

VOID
NTAPI
CcScanDpc(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PGENERAL_LOOKASIDE LookasideList;
    PWORK_QUEUE_ENTRY WorkItem;
    PLIST_ENTRY WorkQueue;
    KIRQL OldIrql;

    DPRINT("CcScanDpc: Dpc %X\n", Dpc);

    /* Allocate a work item */
    LookasideList = Prcb->PPLookasideList[5].P;
    LookasideList->TotalAllocates++;

    WorkItem = (PWORK_QUEUE_ENTRY)InterlockedPopEntrySList(&LookasideList->ListHead);
    if (!WorkItem)
    {
        LookasideList->AllocateMisses++;
        LookasideList = Prcb->PPLookasideList[5].L;
        LookasideList->TotalAllocates++;

        WorkItem = (PWORK_QUEUE_ENTRY)InterlockedPopEntrySList(&LookasideList->ListHead);
        if (!WorkItem)
        {
            LookasideList->AllocateMisses++;
            WorkItem = (PWORK_QUEUE_ENTRY)LookasideList->Allocate(LookasideList->Type,
                                                                  LookasideList->Size,
                                                                  LookasideList->Tag);
        }
    }

    if (!WorkItem)
    {
        DPRINT1("CcScanDpc: WorkQueue is NULL!\n");

        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
        LazyWriter.ScanActive = FALSE;
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        return;
    }

    WorkItem->Function = LazyWriteScan;

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    if (LazyWriter.PendingTeardown)
    {
        WorkQueue = &CcFastTeardownWorkQueue;
        LazyWriter.PendingTeardown = 0;
    }
    else
    {
        WorkQueue = &CcRegularWorkQueue;
    }

    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

    /* And post it, it will be for lazy write */
    CcPostWorkQueue(WorkItem, WorkQueue);
}

LONG
CcExceptionFilter(
    _In_ NTSTATUS Status)
{
    LONG Result;

    DPRINT1("CcExceptionFilter: Status %X\n", Status);

    if (FsRtlIsNtstatusExpected(Status))
        Result = EXCEPTION_EXECUTE_HANDLER;
    else
        Result = EXCEPTION_CONTINUE_SEARCH;

    return Result;
}

VOID
FASTCALL
CcPerformReadAhead(
    _In_ PFILE_OBJECT FileObject)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PSHARED_CACHE_MAP SharedMap;
    PPRIVATE_CACHE_MAP PrivateMap;
    PVACB Vacb = NULL;
    PVOID CacheAddress;
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER readAheadOffset[2];
    ULONG readAheadLength[2];
    ULONG IsPageNotResident = 0;
    ULONG ReceivedLength;
    ULONG NumberOfPages;
    ULONG Length;
    ULONG ix;
    ULONG OldReadClusterSize;
    UCHAR OldForwardClusterOnly;
    KIRQL OldIrql;
    BOOLEAN LengthIsZero;
    BOOLEAN IsFinished = FALSE;
    BOOLEAN IsReadAhead = FALSE;
    BOOLEAN IsReadAheadLock = FALSE;

    DPRINT("CcPerformReadAhead: FileObject %p\n", FileObject);

    /* Save previous values */
    OldForwardClusterOnly = CurrentThread->ForwardClusterOnly;
    OldReadClusterSize = CurrentThread->ReadClusterSize;

    //_SEH2_TRY

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;

    while (TRUE)
    {
        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

        PrivateMap = FileObject->PrivateCacheMap;
        if (PrivateMap)
        {
            KeAcquireSpinLockAtDpcLevel(&PrivateMap->ReadAheadSpinLock);

            LengthIsZero = (!(PrivateMap->ReadAheadLength[0] | PrivateMap->ReadAheadLength[1]));

            readAheadOffset[0].QuadPart = PrivateMap->ReadAheadOffset[0].QuadPart;
            readAheadOffset[1].QuadPart = PrivateMap->ReadAheadOffset[1].QuadPart;

            readAheadLength[0] = PrivateMap->ReadAheadLength[0];
            readAheadLength[1] = PrivateMap->ReadAheadLength[1];

            PrivateMap->ReadAheadLength[0] = 0;
            PrivateMap->ReadAheadLength[1] = 0;

            KeReleaseSpinLockFromDpcLevel(&PrivateMap->ReadAheadSpinLock);
        }

        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        IsReadAheadLock = (*SharedMap->Callbacks->AcquireForReadAhead)(SharedMap->LazyWriteContext, TRUE);

        if (!PrivateMap || LengthIsZero || !IsReadAheadLock)
            break;

        for (ix = 0; ix <= 1; ix++)
        {
            FileOffset = readAheadOffset[ix];
            Length = readAheadLength[ix];

            if (Length && FileOffset.QuadPart <= SharedMap->FileSize.QuadPart)
            {
                IsReadAhead = TRUE;

                if (SharedMap->FileSize.QuadPart <= (FileOffset.QuadPart + (LONGLONG)Length))
                {
                    Length = (SharedMap->FileSize.QuadPart - FileOffset.QuadPart);
                    IsFinished = TRUE;
                }

                if (Length > 0x800000)
                    Length = 0x800000;

                while (Length)
                {
                    CacheAddress = CcGetVirtualAddress(SharedMap, FileOffset, &Vacb, &ReceivedLength);

                    if (ReceivedLength > Length)
                        ReceivedLength = Length;

                    for (NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(CacheAddress, ReceivedLength);
                         NumberOfPages;
                         NumberOfPages--)
                    {
                        CurrentThread->ForwardClusterOnly = 1;

                        if (NumberOfPages > 0x10)
                            CurrentThread->ReadClusterSize = 0xF;
                        else
                            CurrentThread->ReadClusterSize = (NumberOfPages - 1);

                        IsPageNotResident |= !MmCheckCachedPageState(CacheAddress, FALSE);

                        CacheAddress = (PVOID)((ULONG_PTR)CacheAddress + PAGE_SIZE);
                    }

                    FileOffset.QuadPart += ReceivedLength;
                    Length -= ReceivedLength;

                    CcFreeVirtualAddress(Vacb);
                    Vacb = NULL;
                }
            }
        }

        (*SharedMap->Callbacks->ReleaseFromReadAhead)(SharedMap->LazyWriteContext);

        IsReadAheadLock = FALSE;
    }

    //_SEH2_FINALLY

    /* Restore claster variables */
    CurrentThread->ForwardClusterOnly = OldForwardClusterOnly;
    CurrentThread->ReadClusterSize = OldReadClusterSize;

    if (Vacb)
        CcFreeVirtualAddress(Vacb);

    if (IsReadAheadLock)
        (*SharedMap->Callbacks->ReleaseFromReadAhead)(SharedMap->LazyWriteContext);

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    PrivateMap = FileObject->PrivateCacheMap;
    if (PrivateMap)
    {
        KeAcquireSpinLockAtDpcLevel(&PrivateMap->ReadAheadSpinLock);

        RtlInterlockedAndBits(&PrivateMap->UlongFlags, ~SHARE_FL_WAITING_TEARDOWN);

        if (IsFinished && (FileObject->Flags & FO_SEQUENTIAL_ONLY))
            PrivateMap->ReadAheadOffset[1].QuadPart = 0;

        if (IsReadAhead && !IsPageNotResident)
            RtlInterlockedAndBits(&PrivateMap->UlongFlags, ~0x20000);

        KeReleaseSpinLockFromDpcLevel(&PrivateMap->ReadAheadSpinLock);
    }

    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
    ObDereferenceObject(FileObject);
    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    SharedMap->OpenCount--;
    SharedMap->Flags &= ~SHARE_FL_READ_AHEAD;

    if (!SharedMap->OpenCount &&
        !(SharedMap->Flags & SHARE_FL_WRITE_QUEUED) &&
        !SharedMap->DirtyPages)
    {
        ASSERT(FALSE);
    }

    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
}

VOID
NTAPI
CcPostDeferredWrites(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
FASTCALL
CcWriteBehind(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ IO_STATUS_BLOCK* OutIoStatus)
{
    KLOCK_QUEUE_HANDLE LockHandle;
    PVACB ActiveVacb = NULL;
    ULONG ActivePage;
    ULONG TargetPages;
    KIRQL OldIrql;
    BOOLEAN IsVacbLocked = FALSE;
    BOOLEAN IsCancelWait = FALSE;
    NTSTATUS Status;

    DPRINT("CcWriteBehind: SharedMap %p, OutIoStatus %p\n", SharedMap, OutIoStatus);

    if (!(*SharedMap->Callbacks->AcquireForLazyWrite)(SharedMap->LazyWriteContext, TRUE))
    {
        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
        SharedMap->Flags &= ~0x20;
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        OutIoStatus->Status = STATUS_FILE_LOCK_CONFLICT;
        return;
    }

    KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);
    KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueMasterLock]);

    if (SharedMap->DirtyPages <= 1 || !SharedMap->OpenCount)
    {
        KeAcquireSpinLockAtDpcLevel(&SharedMap->ActiveVacbSpinLock);

        ActiveVacb = SharedMap->ActiveVacb;
        if (ActiveVacb)
        {
            ActivePage = SharedMap->ActivePage;
            SharedMap->ActiveVacb = 0;
            IsVacbLocked = ((SharedMap->Flags & SHARE_FL_VACB_LOCKED) != 0);
        }

        KeReleaseSpinLockFromDpcLevel(&SharedMap->ActiveVacbSpinLock);
    }

    SharedMap->OpenCount++;

    if (SharedMap->Mbcb)
    {
        TargetPages = SharedMap->Mbcb->DirtyPages;

        if (ActiveVacb)
            TargetPages++;

        if (TargetPages > CcPagesYetToWrite)
            SharedMap->Mbcb->PagesToWrite = CcPagesYetToWrite;
        else
            SharedMap->Mbcb->PagesToWrite = TargetPages;
    }


    KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueMasterLock]);
    KeReleaseInStackQueuedSpinLock(&LockHandle);

    if (ActiveVacb)
        CcFreeActiveVacb(SharedMap, ActiveVacb, ActivePage, IsVacbLocked);

    CcFlushCache(SharedMap->FileObject->SectionObjectPointer, &CcNoDelay, 1, OutIoStatus);

    (*SharedMap->Callbacks->ReleaseFromLazyWrite)(SharedMap->LazyWriteContext);

    if (!NT_SUCCESS(OutIoStatus->Status) && OutIoStatus->Status != STATUS_VERIFY_REQUIRED)
    {
        DPRINT1("CcWriteBehind: OutIoStatus->Status %X\n", OutIoStatus->Status);
        ASSERT(FALSE);

        IsCancelWait = TRUE;
    }

    if (!NT_SUCCESS(OutIoStatus->Status) &&
        OutIoStatus->Status != STATUS_VERIFY_REQUIRED &&
        OutIoStatus->Status != STATUS_FILE_LOCK_CONFLICT &&
        OutIoStatus->Status != STATUS_ENCOUNTERED_WRITE_IN_PROGRESS)
    {
        DPRINT1("CcWriteBehind: OutIoStatus->Status %X\n", OutIoStatus->Status);
        ASSERT(FALSE);
    }
    else
    {
        if (!IsListEmpty(&CcDeferredWrites))
            CcPostDeferredWrites();
    }

    KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);

    Status = STATUS_SUCCESS;

    if (SharedMap->Flags & (0x400 | 0x8000))
    {
        DPRINT1("CcWriteBehind: FIXME\n");
        ASSERT(FALSE);
    }

    KeReleaseInStackQueuedSpinLock(&LockHandle);

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
    SharedMap->OpenCount--;
    LockHandle.OldIrql = OldIrql;

    if (IsCancelWait && (SharedMap->Flags & SHARE_FL_WAITING_TEARDOWN))
    {
        DPRINT1("CcWriteBehind: FIXME CcCancelMmWaitForUninitializeCacheMap()\n");
        ASSERT(FALSE);
    }

    if (SharedMap->OpenCount)
        goto Exit;

    if (NT_SUCCESS(Status) ||
        (Status != STATUS_INSUFFICIENT_RESOURCES &&
         Status != STATUS_VERIFY_REQUIRED &&
         Status != STATUS_FILE_LOCK_CONFLICT &&
         Status != STATUS_ENCOUNTERED_WRITE_IN_PROGRESS))
    {
        KeReleaseQueuedSpinLock(LockQueueMasterLock, LockHandle.OldIrql);
        FsRtlAcquireFileExclusive(SharedMap->FileObject);

        LockHandle.OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

        if (!SharedMap->OpenCount)
        {
            if (!SharedMap->DirtyPages ||
               (!SharedMap->FileSize.QuadPart && !(SharedMap->Flags & SHARE_FL_PIN_ACCESS)))
            {
                CcDeleteSharedCacheMap(SharedMap, LockHandle.OldIrql, TRUE);
                OutIoStatus->Information = 0;
                return;
            }
        }

        KeReleaseQueuedSpinLock(LockQueueMasterLock, LockHandle.OldIrql);
        FsRtlReleaseFile(SharedMap->FileObject);
        LockHandle.OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
    }
    else if (!SharedMap->DirtyPages)
    {
        DPRINT1("CcWriteBehind: FIXME\n");
        ASSERT(FALSE);
    }

Exit:

    if (OutIoStatus->Information != 0x8A5E)
        SharedMap->Flags &= ~0x20;

    KeReleaseQueuedSpinLock(LockQueueMasterLock, LockHandle.OldIrql);
}

BOOLEAN
NTAPI
IsGoToNextMap(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ ULONG TargetPages)
{
    BOOLEAN Skip = FALSE;

    if (SharedMap->Flags & (0x20 | 0x800))
        return TRUE;

    if ((SharedMap->OpenCount || SharedMap->DirtyPages) &&
        SharedMap->FileSize.QuadPart)
    {
        Skip = TRUE;
    }

    if (!SharedMap->DirtyPages && Skip)
        return TRUE;

    if (SharedMap->Flags & SHARE_FL_WAITING_TEARDOWN)
        return FALSE;

    if (!TargetPages && Skip)
        return TRUE;

    SharedMap->LazyWritePassCount++;

    if ((SharedMap->LazyWritePassCount & 0xF) &&
        (SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE) &&
        CcCapturedSystemSize != MmSmallSystem &&
        SharedMap->DirtyPages < 0x40 &&
        Skip)
    {
        return TRUE;
    }

    if ((SharedMap->FileObject->Flags & 0x8000) &&
        SharedMap->OpenCount &&
        CcCanIWrite(SharedMap->FileObject, 0x40000, 0, 0xFF) &&
        Skip)
    {
        return TRUE;
    }

    return FALSE;
}

VOID
NTAPI
CcLazyWriteScan(VOID)
{
    PSHARED_CACHE_MAP FirstMap = NULL;
    PSHARED_CACHE_MAP SharedMap;
    PGENERAL_LOOKASIDE LookasideList;
    PWORK_QUEUE_ENTRY WorkItem;
    PLIST_ENTRY ListEntry;
    PLIST_ENTRY MapLinks;
    PKPRCB Prcb;
    LIST_ENTRY PostWorkList;
    ULONG TargetPages;
    ULONG NextTargetPages;
    ULONG DirtyPages;
    ULONG counter = 0;
    BOOLEAN IsNoPagesToWrite = FALSE;
    BOOLEAN IsDubleScan = FALSE;
    KIRQL OldIrql;

    DPRINT("CcLazyWriteScan()\n");

    //_SEH2_TRY // FIXME

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    if (!CcTotalDirtyPages && !LazyWriter.OtherWork)
    {
        if (!IsListEmpty(&CcDeferredWrites))
        {
            KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
            CcPostDeferredWrites();
            CcScheduleLazyWriteScan(FALSE);
        }
        else
        {
            LazyWriter.ScanActive = 0;
            KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
        }

        return;
    }

    InitializeListHead(&PostWorkList);

    while (!IsListEmpty(&CcPostTickWorkQueue))
    {
        ListEntry = RemoveHeadList(&CcPostTickWorkQueue);
        InsertTailList(&PostWorkList, ListEntry);
    }

    LazyWriter.OtherWork = FALSE;

    if (CcTotalDirtyPages > 8)
        TargetPages = (CcTotalDirtyPages / 8);
    else
        TargetPages = CcTotalDirtyPages;

    DirtyPages = (CcTotalDirtyPages + CcPagesWrittenLastTime);

    if (CcDirtyPagesLastScan < DirtyPages)
        NextTargetPages = (DirtyPages - CcDirtyPagesLastScan);
    else
        NextTargetPages = 0;

    NextTargetPages += (CcTotalDirtyPages - TargetPages);

    if (NextTargetPages > CcDirtyPageTarget)
        TargetPages += (NextTargetPages - CcDirtyPageTarget);

    CcDirtyPagesLastScan = CcTotalDirtyPages;
    CcPagesWrittenLastTime = TargetPages;
    CcPagesYetToWrite = TargetPages;

    SharedMap = CONTAINING_RECORD(CcLazyWriterCursor.SharedCacheMapLinks.Flink, SHARED_CACHE_MAP, SharedCacheMapLinks);

    while (SharedMap != FirstMap)
    {
        MapLinks = &SharedMap->SharedCacheMapLinks;

        if (MapLinks == &CcLazyWriterCursor.SharedCacheMapLinks)
            break;

        if (!FirstMap)
            FirstMap = SharedMap;

        if (IsGoToNextMap(SharedMap, TargetPages))
        {
            counter++;
            if (counter >= 20)
            {
                DPRINT1("CcLazyWriteScan: FIXME\n");
                ASSERT(FALSE);
            }

            goto NextMap;
        }

        SharedMap->PagesToWrite = SharedMap->DirtyPages;

        if ((SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE) &&
            SharedMap->DirtyPages >= 0x40 &&
            CcCapturedSystemSize != MmSmallSystem)
        {
            SharedMap->PagesToWrite /= 8;
        }

        if (!IsNoPagesToWrite)
        {
            if (TargetPages > SharedMap->PagesToWrite)
            {
                TargetPages -= SharedMap->PagesToWrite;
            }
            else if ((SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE) ||
                     (FirstMap == SharedMap && !(SharedMap->LazyWritePassCount & 0xF)))
            {
                TargetPages = 0;
                IsNoPagesToWrite = TRUE;

                IsDubleScan = TRUE;
            }
            else
            {
                RemoveEntryList(&CcLazyWriterCursor.SharedCacheMapLinks);
                InsertTailList(MapLinks, &CcLazyWriterCursor.SharedCacheMapLinks);

                TargetPages = 0;
                IsNoPagesToWrite = TRUE;
            }
        }

        SharedMap->Flags |= 0x20;
        SharedMap->DirtyPages++;

        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

        Prcb = KeGetCurrentPrcb();

        LookasideList = Prcb->PPLookasideList[5].P;
        LookasideList->TotalAllocates++;

        WorkItem = (PWORK_QUEUE_ENTRY)InterlockedPopEntrySList(&LookasideList->ListHead);
        if (!WorkItem)
        {
            LookasideList->AllocateMisses++;

            LookasideList = Prcb->PPLookasideList[5].L;
            LookasideList->TotalAllocates++;

            WorkItem = (PWORK_QUEUE_ENTRY)InterlockedPopEntrySList(&LookasideList->ListHead);
            if (!WorkItem)
            {
                LookasideList->AllocateMisses++;
                WorkItem = LookasideList->Allocate(LookasideList->Type, LookasideList->Size, LookasideList->Tag);
            }
        }

        if (!WorkItem)
        {
            OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
            SharedMap->Flags &= ~0x20;
            SharedMap->DirtyPages--;
            break;
        }

        WorkItem->Function = WriteBehind;
        WorkItem->Parameters.Write.SharedCacheMap = SharedMap;

        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

        SharedMap->DirtyPages--;

        if (SharedMap->Flags & SHARE_FL_WAITING_TEARDOWN)
        {
            SharedMap->WriteBehindWorkQueueEntry = (PVOID)((ULONG_PTR)WorkItem | 1);
            CcPostWorkQueue(WorkItem, &CcFastTeardownWorkQueue);
        }
        else
        {
            SharedMap->WriteBehindWorkQueueEntry = WorkItem;
            CcPostWorkQueue(WorkItem, &CcRegularWorkQueue);
        }

        counter = 0;

NextMap:

        SharedMap = CONTAINING_RECORD(MapLinks->Flink, SHARED_CACHE_MAP, SharedCacheMapLinks);

        if (IsDubleScan)
        {
            RemoveEntryList(&CcLazyWriterCursor.SharedCacheMapLinks);
            InsertHeadList(MapLinks, &CcLazyWriterCursor.SharedCacheMapLinks);

            IsDubleScan = FALSE;
        }
    }

    while (!IsListEmpty(&PostWorkList))
    {
        PWORK_QUEUE_ENTRY workItem;
        PLIST_ENTRY entry;

        entry = RemoveHeadList(&PostWorkList);
        workItem = CONTAINING_RECORD(entry, WORK_QUEUE_ENTRY, WorkQueueLinks);

        CcPostWorkQueue(workItem, &CcRegularWorkQueue);
    }

    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

    if (!IsListEmpty(&CcDeferredWrites))
        CcPostDeferredWrites();

    CcScheduleLazyWriteScan(FALSE);

    //_SEH2_EXCEPT()
    //_SEH2_END;
}

VOID
NTAPI
CcWorkerThread(
    _In_ PVOID Parameter)
{
    PWORK_QUEUE_ITEM WorkItem = Parameter;
    PGENERAL_LOOKASIDE LookasideList;
    PSHARED_CACHE_MAP SharedMap;
    PWORK_QUEUE_ENTRY WorkEntry;
    PLIST_ENTRY Entry;
    PKPRCB Prcb;
    IO_STATUS_BLOCK IoStatus;
    KIRQL OldIrql;
    BOOLEAN DropThrottle = FALSE;
    BOOLEAN WritePerformed = FALSE;

    DPRINT("CcWorkerThread: WorkItem %p\n", WorkItem);

    IoStatus.Status = STATUS_SUCCESS;
    IoStatus.Information = 0;

    /* Loop till we have jobs */
    while (TRUE)
    {
        /* Lock queues */
        OldIrql = KeAcquireQueuedSpinLock(LockQueueWorkQueueLock);

        /* If we have to touch throttle, reset it now! */
        if (DropThrottle)
        {
            CcQueueThrottle = FALSE;
            DropThrottle = FALSE;
        }

        if (IoStatus.Information == 0x8A5E)
        {
            ASSERT(Entry);

            if (WorkEntry->Function == WriteBehind)
            {
                SharedMap = WorkEntry->Parameters.Write.SharedCacheMap;
                ASSERT(Entry != &CcFastTeardownWorkQueue);
                SharedMap->WriteBehindWorkQueueEntry = WorkEntry;
            }

            InsertTailList(Entry, &WorkEntry->WorkQueueLinks);
            IoStatus.Information = 0;
        }

        /* Check if we have write to do */
        if (!IsListEmpty(&CcFastTeardownWorkQueue))
        {
            Entry = &CcFastTeardownWorkQueue;
            WorkEntry = CONTAINING_RECORD(Entry->Flink, WORK_QUEUE_ENTRY, WorkQueueLinks);

            ASSERT((WorkEntry->Function == LazyWriteScan) ||
                   (WorkEntry->Function == WriteBehind)); 
        }
        /* If not, check read queues */
        else if (!IsListEmpty(&CcExpressWorkQueue))
        {
            Entry = &CcExpressWorkQueue;
        }
        else if (!IsListEmpty(&CcRegularWorkQueue))
        {
            Entry = &CcRegularWorkQueue;
        }
        else
        {
            break;
        }

        /* Get our work item, if someone is waiting for us to finish
           and we're not the only thread in queue then, quit running to let the others do
           and throttle so that noone starts till current activity is over
        */
        WorkEntry = CONTAINING_RECORD(Entry->Flink, WORK_QUEUE_ENTRY, WorkQueueLinks);

        if (WorkEntry->Function == SetDone && CcNumberActiveWorkerThreads > 1)
        {
            CcQueueThrottle = TRUE;
            break;
        }

        if (WorkEntry->Function == WriteBehind)
            WorkEntry->Parameters.Write.SharedCacheMap->WriteBehindWorkQueueEntry = NULL;

        /* Remove current entry */
        RemoveHeadList(Entry);

        /* Unlock queues */
        KeReleaseQueuedSpinLock(LockQueueWorkQueueLock, OldIrql);

        /* And handle it */
        _SEH2_TRY
        {
            switch (WorkEntry->Function)
            {
                case ReadAhead:
                {
                    CcPerformReadAhead(WorkEntry->Parameters.Read.FileObject);
                    break;
                }
                case WriteBehind:
                {
                    WritePerformed = TRUE;
                    PsGetCurrentThread()->MemoryMaker = 1;

                    CcWriteBehind(WorkEntry->Parameters.Write.SharedCacheMap, &IoStatus);

                    if (!NT_SUCCESS(IoStatus.Status))
                        WritePerformed = FALSE;

                    PsGetCurrentThread()->MemoryMaker = 0;
                    break;
                }
                case LazyWriteScan:
                {
                    CcLazyWriteScan();
                    break;
                }
                case SetDone:
                {
                    KeSetEvent(WorkEntry->Parameters.Event.Event, IO_NO_INCREMENT, FALSE);
                    DropThrottle = TRUE;
                    break;
                }
            }
        }
        _SEH2_EXCEPT(CcExceptionFilter(_SEH2_GetExceptionCode()))
        {
            if (WorkEntry->Function == WriteBehind)
                PsGetCurrentThread()->MemoryMaker = 0;
        }
        _SEH2_END;


        /* Handle for WriteBehind */
        if (IoStatus.Information == 0x8A5E)
            continue;

        /* Release the current element and continue */

        Prcb = KeGetCurrentPrcb();

        LookasideList = Prcb->PPLookasideList[5].P;
        LookasideList->TotalFrees++;

        if (LookasideList->ListHead.Depth < LookasideList->Depth)
        {
            InterlockedPushEntrySList(&LookasideList->ListHead, (PSINGLE_LIST_ENTRY)WorkEntry);
            continue;
        }

        LookasideList->FreeMisses++;

        LookasideList = Prcb->PPLookasideList[5].L;
        LookasideList->TotalFrees++;

        if (LookasideList->ListHead.Depth < LookasideList->Depth)
        {
            InterlockedPushEntrySList(&LookasideList->ListHead, (PSINGLE_LIST_ENTRY)WorkEntry);
            continue;
        }

        LookasideList->FreeMisses++;
        LookasideList->Free(WorkEntry);
    }

    /* Our thread is available again */
    InsertTailList(&CcIdleWorkerThreadList, &WorkItem->List);

    /* One less worker */
    CcNumberActiveWorkerThreads--;

    /* Unlock queues */
    KeReleaseQueuedSpinLock(LockQueueWorkQueueLock, OldIrql);

    /* If there are pending write openations and we have at least 20 dirty pages */
    if (!IsListEmpty(&CcDeferredWrites) && CcTotalDirtyPages >= 20)
    {
        /* And if we performed a write operation previously,
           then stress the system a bit and reschedule a scan to find stuff to write
        */
        if (WritePerformed)
            CcLazyWriteScan();
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
CcWaitForCurrentLazyWriterActivity(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
