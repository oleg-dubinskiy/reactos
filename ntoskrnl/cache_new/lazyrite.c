
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

LIST_ENTRY CcFastTeardownWorkQueue;
LIST_ENTRY CcPostTickWorkQueue;
LIST_ENTRY CcRegularWorkQueue;
LIST_ENTRY CcExpressWorkQueue;
LIST_ENTRY CcIdleWorkerThreadList;

/* FUNCTIONS ******************************************************************/

VOID
FASTCALL
CcPostWorkQueue(
    _In_ PWORK_QUEUE_ENTRY WorkItem,
    _In_ PLIST_ENTRY WorkQueue)
{
    UNIMPLEMENTED_DBGBREAK();
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

VOID
NTAPI
CcWorkerThread(
    _In_ PVOID Parameter)
{
    UNIMPLEMENTED_DBGBREAK();
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
