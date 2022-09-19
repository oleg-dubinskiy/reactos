
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG CcDataFlushes;
ULONG CcDataPages;
ULONG CcFastMdlReadNotPossible;
ULONG CcFastMdlReadWait;
ULONG CcFastReadNotPossible;
ULONG CcFastReadNoWait;
ULONG CcFastReadResourceMiss;
ULONG CcFastReadWait;
ULONG CcLazyWriteIos;
ULONG CcLazyWritePages;
ULONG CcMapDataWait;
ULONG CcMapDataNoWait;
ULONG CcPinMappedDataCount;
ULONG CcPinReadWait;
ULONG CcPinReadNoWait;
ULONG CcIdleDelayTick;

extern LIST_ENTRY CcExpressWorkQueue;

/* FUNCTIONS ******************************************************************/


/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
CcFlushCache(IN PSECTION_OBJECT_POINTERS SectionObjectPointer,
             IN OPTIONAL PLARGE_INTEGER FileOffset,
             IN ULONG Length,
             OUT OPTIONAL PIO_STATUS_BLOCK IoStatus)
{
    UNIMPLEMENTED_DBGBREAK();
}

LARGE_INTEGER
NTAPI
CcGetFlushedValidData(IN PSECTION_OBJECT_POINTERS SectionObjectPointer,
                      IN BOOLEAN CcInternalCaller)
{
    LARGE_INTEGER Result = {{0}};
    UNIMPLEMENTED_DBGBREAK();
    return Result;
}

PVOID
NTAPI
CcRemapBcb(IN PVOID Bcb)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
CcRepinBcb(IN PVOID Bcb)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcScheduleReadAhead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length)
{
    PSHARED_CACHE_MAP SharedMap;
    PPRIVATE_CACHE_MAP PrivateMap;
    PWORK_QUEUE_ENTRY WorkItem;
    PGENERAL_LOOKASIDE LookasideList;
    PKPRCB Prcb;
    LARGE_INTEGER NewOffset;
    LARGE_INTEGER EndNewOffset;
    //LARGE_INTEGER FileOffset1;
    LARGE_INTEGER FileOffset2;
    ULONG ReadAheadLength;
    KIRQL OldIrql;
    BOOLEAN IsFlag = FALSE;

    DPRINT("CcScheduleReadAhead: %p, [%p], %X\n", FileObject, (FileOffset ? FileOffset->QuadPart : 0), Length);

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;
    PrivateMap = FileObject->PrivateCacheMap;

    /* If file isn't cached, or if read ahead is disabled, this is no op */
    if (!PrivateMap)
    {
        DPRINT("CcScheduleReadAhead: PrivateMap is NULL\n");
        return;
    }

    if (!SharedMap)
    {
        DPRINT("CcScheduleReadAhead: SharedMap is NULL\n");
        return;
    }

    if (SharedMap->Flags & READAHEAD_DISABLED)
    {
        DPRINT("CcScheduleReadAhead: READAHEAD_DISABLED\n");
        return;
    }

    /* Compute the offset we'll reach */
    NewOffset.QuadPart = FileOffset->QuadPart;
    EndNewOffset.QuadPart = FileOffset->QuadPart + (LONGLONG)Length;

    ReadAheadLength = (Length + PrivateMap->ReadAheadMask) & ~PrivateMap->ReadAheadMask;

    FileOffset2.QuadPart = EndNewOffset.QuadPart + (LONGLONG)ReadAheadLength;
    FileOffset2.LowPart &= ~PrivateMap->ReadAheadMask;

    /* Lock read ahead spin lock */
    KeAcquireSpinLock(&PrivateMap->ReadAheadSpinLock, &OldIrql);

    /* Easy case: the file is sequentially read */
    if (FileObject->Flags & FO_SEQUENTIAL_ONLY)
    {
        DPRINT1("CcScheduleReadAhead: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        if (NewOffset.HighPart == PrivateMap->BeyondLastByte2.HighPart &&
            (NewOffset.LowPart & ~7) == (PrivateMap->BeyondLastByte2.LowPart & ~7) &&
            PrivateMap->FileOffset2.HighPart == PrivateMap->BeyondLastByte1.HighPart &&
            (PrivateMap->FileOffset2.LowPart & ~7) == (PrivateMap->BeyondLastByte1.LowPart & ~7))
        {
            if (!FileOffset->QuadPart &&
                PrivateMap->ReadAheadMask > (PAGE_SIZE - 1) &&
                PrivateMap->ReadAheadMask >= (Length + (PAGE_SIZE - 1)))
            {
                FileOffset2.QuadPart = ROUND_TO_PAGES(Length);
            }
            else
            {
                FileOffset2.QuadPart = (EndNewOffset.QuadPart + ReadAheadLength);
                FileOffset2.LowPart &= ~PrivateMap->ReadAheadMask;
            }

            if (FileOffset2.QuadPart != PrivateMap->ReadAheadOffset[1].QuadPart)
            {
                ASSERT(FileOffset2.HighPart >= 0);

                PrivateMap->ReadAheadOffset[1] = FileOffset2;
                PrivateMap->ReadAheadLength[1] = ReadAheadLength;

                IsFlag = TRUE;
            }
        }
        else if ((NewOffset.QuadPart - PrivateMap->FileOffset2.QuadPart) ==
                 (PrivateMap->FileOffset2.QuadPart - PrivateMap->FileOffset1.QuadPart))
        {
            DPRINT1("CcScheduleReadAhead: FIXME\n");
            ASSERT(FALSE);
        }
    }

    if (!IsFlag || PrivateMap->Flags.ReadAheadActive)
    {
        KeReleaseSpinLock(&PrivateMap->ReadAheadSpinLock, OldIrql);
        return;
    }

    RtlInterlockedSetBits(&PrivateMap->UlongFlags, PRIVATE_CACHE_MAP_READ_AHEAD_ACTIVE);

    KeReleaseSpinLock(&PrivateMap->ReadAheadSpinLock, OldIrql);

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
        KeAcquireSpinLock(&PrivateMap->ReadAheadSpinLock, &OldIrql);
        RtlInterlockedAndBits(&PrivateMap->UlongFlags, ~PRIVATE_CACHE_MAP_READ_AHEAD_ACTIVE);
        KeReleaseSpinLock(&PrivateMap->ReadAheadSpinLock, OldIrql);

        DPRINT1("CcScheduleReadAhead: WorkItem is NULL\n");
        return;
    }

    ObReferenceObject(FileObject);

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
    SharedMap->OpenCount++;
    SharedMap->Flags |= 0x4000;
    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

    WorkItem->Function = ReadAhead;
    WorkItem->Parameters.Read.FileObject = FileObject;

    CcPostWorkQueue(WorkItem, &CcExpressWorkQueue);
}

VOID
NTAPI
CcSetDirtyPinnedData(IN PVOID BcbVoid,
                     IN OPTIONAL PLARGE_INTEGER Lsn)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcSetReadAheadGranularity(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG Granularity)
{
    PPRIVATE_CACHE_MAP PrivateMap;

    PrivateMap = FileObject->PrivateCacheMap;
    PrivateMap->ReadAheadMask = (Granularity - 1);
}

VOID
NTAPI
CcUnpinRepinnedBcb(IN PVOID Bcb,
                   IN BOOLEAN WriteThrough,
                   OUT PIO_STATUS_BLOCK IoStatus)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
CcPfInitializePrefetcher(VOID)
{
    DPRINT1("CcPfInitializePrefetcher: FIXME \n");

    /* Notify debugger */
    //DbgPrintEx(DPFLTR_PREFETCHER_ID, DPFLTR_TRACE_LEVEL, "CCPF: InitializePrefetecher()\n");

    /* Setup the Prefetcher Data */
    //InitializeListHead(&CcPfGlobals.ActiveTraces);
    //InitializeListHead(&CcPfGlobals.CompletedTraces);
    //ExInitializeFastMutex(&CcPfGlobals.CompletedTracesLock);

    /* FIXME: Setup the rest of the prefetecher */
    //ASSERT(FALSE);
}

#if DBG && defined(KDBG)
BOOLEAN
ExpKdbgExtFileCache(ULONG Argc, PCHAR Argv[])
{
    DPRINT1("ExpKdbgExtFileCache: ... \n");
    UNIMPLEMENTED;
    return FALSE;
}

BOOLEAN
ExpKdbgExtDefWrites(ULONG Argc, PCHAR Argv[])
{
    DPRINT1("ExpKdbgExtDefWrites: ... \n");
    UNIMPLEMENTED;
    return FALSE;
}
#endif

/* EOF */
