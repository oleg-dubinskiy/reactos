
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
#define NDEBUG
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
extern LARGE_INTEGER CcNoDelay;

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
CcFindBcb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER EndFileOffset,
    _Out_ PCC_BCB* OutBcb)
{
    PLIST_ENTRY NextBcbList;
    PCC_BCB Bcb;
    BOOLEAN Result = FALSE;

    DPRINT("CcFindBcb: %p, [%I64X], [%I64X]\n",
           SharedMap, (FileOffset ? FileOffset->QuadPart : 0ll), (EndFileOffset ? EndFileOffset->QuadPart : 0ll));

    NextBcbList = CcGetBcbListHead(SharedMap, (FileOffset->QuadPart + BCB_MAPPING_GRANULARITY), TRUE);

    DPRINT("CcFindBcb: NextBcbList %p\n", NextBcbList);

    Bcb = CONTAINING_RECORD(NextBcbList->Flink, CC_BCB, Link);

    DPRINT("CcFindBcb: Bcb %p, NodeTypeCode %X\n", Bcb, Bcb->NodeTypeCode);

    if (!FileOffset->HighPart && !Bcb->BeyondLastByte.HighPart)
    {
        while (Bcb->NodeTypeCode == NODE_TYPE_BCB)
        {
            if (FileOffset->LowPart >= Bcb->BeyondLastByte.LowPart)
                break;

            DPRINT1("CcFindBcb: FIXME\n");
            ASSERT(FALSE);
        }
    }
    else
    {
        while (Bcb->NodeTypeCode == NODE_TYPE_BCB)
        {
            if (FileOffset->QuadPart >= Bcb->BeyondLastByte.QuadPart)
                break;

            DPRINT1("CcFindBcb: FIXME\n");
            ASSERT(FALSE);
        }
    }

    *OutBcb = Bcb;

    return Result;
}

PCC_BCB
NTAPI
CcAllocateInitializeBcb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PCC_BCB NextBcb,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER Length)
{
    PCC_BCB Bcb;

    DPRINT("CcAllocateInitializeBcb: %p, [%I64X]\n", SharedMap, (FileOffset ? FileOffset->QuadPart : 0ll));

    Bcb = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Bcb), 'cBcC');
    if (!Bcb)
    {
        DPRINT1("CcAllocateInitializeBcb: ExAllocatePoolWithTag() failed\n");
        return NULL;
    }

    RtlZeroMemory(Bcb, sizeof(*Bcb));

    if (!SharedMap)
        return Bcb;

    Bcb->NodeTypeCode = NODE_TYPE_BCB;
    Bcb->PinCount++;

    Bcb->Length = Length->LowPart;
    Bcb->FileOffset.QuadPart = FileOffset->QuadPart;
    Bcb->BeyondLastByte.QuadPart = (FileOffset->QuadPart + Length->QuadPart);

    ExInitializeResourceLite(&Bcb->BcbResource);
    Bcb->SharedCacheMap = SharedMap;

    KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);

    InsertTailList(&NextBcb->Link, &Bcb->Link);

    ASSERT((SharedMap->SectionSize.QuadPart < CACHE_OVERALL_SIZE) ||
           (CcFindBcb(SharedMap, FileOffset, &Bcb->BeyondLastByte, &NextBcb) && (Bcb == NextBcb)));

    if (SharedMap->SectionSize.QuadPart > CACHE_OVERALL_SIZE &&
        (SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE))
    {
        DPRINT1("CcAllocateInitializeBcb: FIXME\n");
        ASSERT(FALSE);
    }

    KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[4]);

    if (SharedMap->Flags & 2)
    {
        DPRINT1("CcAllocateInitializeBcb: FIXME\n");
        ASSERT(FALSE);
    }

    return Bcb;
}

BOOLEAN
NTAPI
CcAcquireByteRangeForWrite(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PLARGE_INTEGER Offset,
    _In_ ULONG Length,
    _Out_ PLARGE_INTEGER OutFileOffset,
    _Out_ ULONG* OutLength,
    _Out_ PVOID* OutParam6)
{
    KLOCK_QUEUE_HANDLE LockHandle;
    PCC_BCB Bcb;
    BOOLEAN IsFlag = FALSE;
    BOOLEAN Result = FALSE;

    DPRINT("CcAcquireByteRangeForWrite: SharedMap %p, Length %X\n", SharedMap, Length);

    OutFileOffset->QuadPart = 0;
    *OutLength = 0;

    KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);

    if (SharedMap->Mbcb)
    {
        DPRINT1("CcAcquireByteRangeForWrite: FIXME\n");
        ASSERT(FALSE);
    }

    while (TRUE)
    {
        Bcb = CONTAINING_RECORD(SharedMap->BcbList.Blink, CC_BCB, Link);

        if (SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE)
        {
            DPRINT1("CcAcquireByteRangeForWrite: FIXME\n");
            ASSERT(FALSE);
        }

        while (&Bcb->Link != &SharedMap->BcbList)
        {
            if (Bcb->NodeTypeCode != NODE_TYPE_BCB)
            {
                Bcb = CONTAINING_RECORD(Bcb->Link.Blink, CC_BCB, Link);
                continue;
            }

            if (Offset && ((Offset->QuadPart + Length) <= Bcb->FileOffset.QuadPart))
                break;

            if (*OutLength == 0)
            {
                if (!Bcb->Reserved1[0] ||
                    (Offset && (Offset->QuadPart >= Bcb->BeyondLastByte.QuadPart)) ||
                    (!Offset && (Bcb->FileOffset.QuadPart < SharedMap->BeyondLastFlush)))
                {
                    Bcb = CONTAINING_RECORD(Bcb->Link.Blink, CC_BCB, Link);
                    continue;
                }

                DPRINT1("CcAcquireByteRangeForWrite: FIXME\n");
                ASSERT(FALSE);
            }
            else
            {
                if (!Bcb->Reserved1[0])
                    break;

                if (Bcb->FileOffset.QuadPart != (OutFileOffset->QuadPart + *OutLength))
                    break;

                if ((*OutLength + Bcb->Length) > 0x10000)
                    break;

                if (Bcb->PinCount)
                    break;

                if (!(Bcb->FileOffset.QuadPart & (0x2000000 - 1)))
                    break;
            }

            Bcb->PinCount++;

            KeReleaseInStackQueuedSpinLock(&LockHandle);

            if ((SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE) &&
                !(SharedMap->Flags & 2))
            {
                DPRINT1("CcAcquireByteRangeForWrite: FIXME\n");
                ASSERT(FALSE);
            }
            else
            {
                DPRINT1("CcAcquireByteRangeForWrite: FIXME\n");
                ASSERT(FALSE);
            }

            DPRINT1("CcAcquireByteRangeForWrite: FIXME\n");
            ASSERT(FALSE);
        }

        if (IsFlag)
        {
            DPRINT1("CcAcquireByteRangeForWrite: FIXME\n");
            ASSERT(FALSE);
        }

        if (*OutLength)
        {
            DPRINT1("CcAcquireByteRangeForWrite: FIXME\n");
            ASSERT(FALSE);

            break;
        }

        if (!SharedMap->BeyondLastFlush || Offset)
            break;

        SharedMap->BeyondLastFlush = 0;
    }

    KeReleaseInStackQueuedSpinLock(&LockHandle);

    if (*OutLength)
        Result = TRUE;

    return Result;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
CcFlushCache(IN PSECTION_OBJECT_POINTERS SectionObjectPointers,
             IN PLARGE_INTEGER FileOffset OPTIONAL,
             IN ULONG Length,
             OUT IO_STATUS_BLOCK* OutIoStatus OPTIONAL)
{
    PSHARED_CACHE_MAP SharedMap;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER fileSize;
    PFSRTL_COMMON_FCB_HEADER FcbHeader;
    ULONG Flags;
    ULONG Size;
    BOOLEAN IsSaveStatus = FALSE;
    BOOLEAN IsLazyWrite = FALSE;
    BOOLEAN PendingTeardown = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (!OutIoStatus)
        OutIoStatus = &ioStatus;

    OutIoStatus->Status = STATUS_SUCCESS;
    OutIoStatus->Information = 0;

    if (FileOffset == &CcNoDelay)
    {
        FileOffset = NULL;
        IsLazyWrite = TRUE;
        OutIoStatus->Status = STATUS_VERIFY_REQUIRED;
        Flags = 2;
    }
    else
    {
        Flags = 1;
    }

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    SharedMap = SectionObjectPointers->SharedCacheMap;

    DPRINT("CcFlushCache: SharedMap %p, Length %X\n", SharedMap, Length);

    if (SharedMap)
    {
        if (IsLazyWrite && (SharedMap->Flags & SHARE_FL_WAITING_TEARDOWN))
        {
            Flags &= ~2;
            PendingTeardown = TRUE;
        }

        if (SharedMap->Flags & 0x2000)
        {
            if (!((ULONG_PTR)FileOffset & 1))
            {
                KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
                return;
            }

            FileOffset = (PLARGE_INTEGER)((ULONG_PTR)FileOffset ^ 1);
        }

        if (SharedMap->FileObject->DeviceObject &&
            SharedMap->FileObject->DeviceObject->DeviceType == FILE_DEVICE_CD_ROM)
        {
            Flags &= ~2;
        }
    }

    if (FileOffset && !Length)
    {
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
        return;
    }

    if (SharedMap)
    {
        SharedMap->OpenCount++;

        if (SharedMap->NeedToZero || SharedMap->ActiveVacb)
        {
            DPRINT1("CcFlushCache: FIXME\n");
            ASSERT(FALSE);
        }
    }

    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

    FcbHeader = SharedMap->FileObject->FsContext;

    if (!SharedMap ||
        (FcbHeader->Flags & FSRTL_FLAG_USER_MAPPED_FILE) ||
        (SharedMap->Flags & 0x20000))
    {
        if (!IsLazyWrite)
        {
            MmFlushSection(SectionObjectPointers, FileOffset, Length, OutIoStatus, 1);

            ASSERT(OutIoStatus->Status != STATUS_ENCOUNTERED_WRITE_IN_PROGRESS);

            if (!NT_SUCCESS(OutIoStatus->Status) &&
                OutIoStatus->Status != STATUS_VERIFY_REQUIRED &&
                OutIoStatus->Status != STATUS_FILE_LOCK_CONFLICT &&
                OutIoStatus->Status != STATUS_ENCOUNTERED_WRITE_IN_PROGRESS)
            {
                IsSaveStatus = TRUE;
                Status = OutIoStatus->Status;
            }
        }
    }

    if (!SharedMap)
        goto Exit;

    if (!IsLazyWrite && !FileOffset)
        SharedMap->ValidDataLength = SharedMap->ValidDataGoal;

    if (FileOffset)
        fileSize.QuadPart = FileOffset->QuadPart;

    if (Length)
        Size = Length;
    else
        Size = 1;

    while (TRUE)
    {
        PLARGE_INTEGER offset;
        PVOID Param6;
        LARGE_INTEGER fileOffset;
        ULONG length;

        if (!SharedMap->PagesToWrite && IsLazyWrite && !PendingTeardown)
            break;

        if (!SharedMap->FileSize.QuadPart && !(SharedMap->Flags & SHARE_FL_PIN_ACCESS))
            break;

        if (!IsLazyWrite || PendingTeardown)
            length = Size;
        else
            length = 0;

        if (!IsLazyWrite || PendingTeardown)
            offset = (FileOffset ? &fileSize : NULL);
        else
            offset = NULL;

        if (!CcAcquireByteRangeForWrite(SharedMap, offset, length, &fileOffset, &Size, &Param6))
            break;

        DPRINT1("CcFlushCache: FIXME\n");
        ASSERT(FALSE);
    }

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

    SharedMap->OpenCount--;

    if (!SharedMap->OpenCount &&
        !(SharedMap->Flags & SHARE_FL_WRITE_QUEUED) &&
        !SharedMap->DirtyPages)
    {
        DPRINT1("CcFlushCache: FIXME\n");
        ASSERT(FALSE);
    }

    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

Exit:

    if (IsSaveStatus)
        OutIoStatus->Status = Status;
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
