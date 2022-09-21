
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
BOOLEAN CcQueueThrottle = FALSE;

LIST_ENTRY CcFastTeardownWorkQueue;
LIST_ENTRY CcPostTickWorkQueue;
LIST_ENTRY CcRegularWorkQueue;
LIST_ENTRY CcExpressWorkQueue;
LIST_ENTRY CcIdleWorkerThreadList;

extern LIST_ENTRY CcDeferredWrites;

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
    UNIMPLEMENTED_DBGBREAK();
}

VOID
FASTCALL
CcWriteBehind(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ IO_STATUS_BLOCK* OutIoStatus)
{
    UNIMPLEMENTED_DBGBREAK();
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
    UNIMPLEMENTED_DBGBREAK();
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
