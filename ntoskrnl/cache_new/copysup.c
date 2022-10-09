
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

extern LIST_ENTRY CcDeferredWrites;
extern ULONG CcTotalDirtyPages;
extern ULONG CcDirtyPageThreshold;
extern PFN_NUMBER MmAvailablePages;
extern ULONG MmThrottleTop;
extern ULONG MmThrottleBottom;
extern MMPFNLIST MmModifiedPageListHead;
extern KSPIN_LOCK CcDeferredWriteSpinLock;
extern LARGE_INTEGER CcIdleDelay;

/* FUNCTIONS ******************************************************************/

LONG
CcCopyReadExceptionFilter(
    _In_ PEXCEPTION_POINTERS ExceptionInfo,
    _Out_ NTSTATUS* OutStatus)
{
    DPRINT1("CcCopyReadExceptionFilter: FIXME! ExceptionInfo %p\n", ExceptionInfo);
    ASSERT(FALSE);


    ASSERT(!NT_SUCCESS(*OutStatus));

    return EXCEPTION_EXECUTE_HANDLER;
}

BOOLEAN
NTAPI
CcMapAndCopy(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PVOID Buffer,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG CopyFlags,
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER ValidDataLength,
    _In_ BOOLEAN Wait)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
CcCanIWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG BytesToWrite,
    _In_ BOOLEAN Wait,
    _In_ UCHAR Retrying)
{
    PFSRTL_COMMON_FCB_HEADER FsContext;
    PSHARED_CACHE_MAP SharedMap;
    DEFERRED_WRITE DeferredWrite;
    KEVENT Event;
    ULONG Size;
    ULONG Pages;
    KIRQL OldIrql;
    BOOLEAN IsSmallThreshold = FALSE;

    DPRINT("CcCanIWrite: FileObject %p, BytesToWrite %X\n", FileObject, BytesToWrite);

    if (FileObject->Flags & FO_WRITE_THROUGH)
        return TRUE;

    if (IoIsFileOriginRemote(FileObject) && Retrying < 0xFD)
        return TRUE;

    if (BytesToWrite < 0x40000)
        Size = BytesToWrite;
    else
        Size = 0x40000;

    Pages = ((Size + (PAGE_SIZE - 1)) / PAGE_SIZE);

    FsContext = FileObject->FsContext;

    if (Retrying >= 0xFE || (FsContext->Flags & FSRTL_FLAG_LIMIT_MODIFIED_PAGES))
    {
        if (Retrying != 0xFF)
            OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);

        if (FileObject->SectionObjectPointer)
        {
            SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;

            if (SharedMap && SharedMap->DirtyPageThreshold && SharedMap->DirtyPages)
            {
                if (SharedMap->DirtyPageThreshold < (SharedMap->DirtyPages + Pages))
                   IsSmallThreshold = TRUE;
            }
        }

        if (Retrying != 0xFF)
          KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
    }

    if ((Retrying || IsListEmpty(&CcDeferredWrites)) &&
        CcDirtyPageThreshold > (CcTotalDirtyPages + Pages))
    {
        if (MmAvailablePages > MmThrottleTop && !IsSmallThreshold)
            return TRUE;

        if (MmModifiedPageListHead.Total < 1000 &&
            MmAvailablePages > MmThrottleBottom &&
            !IsSmallThreshold)
        {
            return TRUE;
        }
    }

    if (!Wait)
        return FALSE;

    if (IsListEmpty(&CcDeferredWrites))
    {
        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
        CcScheduleLazyWriteScan(TRUE);
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    DeferredWrite.NodeTypeCode = NODE_TYPE_DEFERRED_WRITE;
    DeferredWrite.NodeByteSize = sizeof(DEFERRED_WRITE);

    DeferredWrite.FileObject = FileObject;
    DeferredWrite.BytesToWrite = BytesToWrite;
    DeferredWrite.Event = &Event;
    DeferredWrite.LimitModifiedPages = ((FsContext->Flags & FSRTL_FLAG_LIMIT_MODIFIED_PAGES) != 0);

    if (Retrying)
        ExInterlockedInsertHeadList(&CcDeferredWrites, &DeferredWrite.DeferredWriteLinks, &CcDeferredWriteSpinLock);
    else
        ExInterlockedInsertTailList(&CcDeferredWrites, &DeferredWrite.DeferredWriteLinks, &CcDeferredWriteSpinLock);

    do
        CcPostDeferredWrites();
    while (KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, &CcIdleDelay) != STATUS_SUCCESS);

    return TRUE;
}

BOOLEAN
NTAPI
CcCopyRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _Out_ PVOID Buffer,
    _Out_ IO_STATUS_BLOCK* OutIoStatus)
{
    PETHREAD Thread = PsGetCurrentThread();
    PSHARED_CACHE_MAP SharedMap;
    PPRIVATE_CACHE_MAP PrivateMap;
    LARGE_INTEGER BeyondLastByte;
    LARGE_INTEGER Offset;
    PKPRCB Prcb;
    PVOID CacheAddress;
    PVACB TempVacb;
    PVACB Vacb;
    ULONG InputLength = Length;
    ULONG ActivePage;
    ULONG ReadLength;
    ULONG ReadPages;
    ULONG CopyLength;
    ULONG ReceivedLength;
    ULONG RemainingLength;
    ULONG OldReadClusterSize;
    UCHAR OldForwardClusterOnly;
    BOOLEAN IsPageNotResident = FALSE;
    BOOLEAN IsVacbLocked;
    NTSTATUS Status;

    DPRINT("CcCopyRead: FileObject %p, FileOffset %I64X, Length %X, Wait %X\n",
           FileObject, (FileOffset ? FileOffset->QuadPart : 0), Length, Wait);

    OldForwardClusterOnly = Thread->ForwardClusterOnly;
    OldReadClusterSize = Thread->ReadClusterSize;

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;
    PrivateMap = FileObject->PrivateCacheMap;

    ASSERT((FileOffset->QuadPart + Length) <= SharedMap->FileSize.QuadPart);

    if (PrivateMap->Flags.ReadAheadEnabled && !PrivateMap->ReadAheadLength[1])
        CcScheduleReadAhead(FileObject, FileOffset, Length);

    Offset.QuadPart = FileOffset->QuadPart;
    Prcb = KeGetCurrentPrcb();

    if (Wait)
        Prcb->CcCopyReadWait++;
    else
        Prcb->CcCopyReadNoWait++;

    CcGetActiveVacb(SharedMap, &Vacb, &ActivePage, &IsVacbLocked);

    if (Vacb)
    {
        if ((ULONG)(Offset.QuadPart / VACB_MAPPING_GRANULARITY) == (ActivePage >> 6))
        {
            ReadLength = VACB_MAPPING_GRANULARITY - (Offset.LowPart & (VACB_MAPPING_GRANULARITY - 1));

            if (SharedMap->NeedToZero)
            {
                DPRINT1("CcCopyRead: FIXME\n");
                ASSERT(FALSE);
            }

            CacheAddress = (PVOID)((ULONG_PTR)Vacb->BaseAddress + (Offset.LowPart & (VACB_MAPPING_GRANULARITY - 1)));

            if (ReadLength > Length)
                ReadLength = Length;

            ReadPages = (COMPUTE_PAGES_SPANNED(CacheAddress, ReadLength) - 1);

            _SEH2_TRY
            {
                if (!ReadPages)
                {
                    Thread->ForwardClusterOnly = 1;
                    Thread->ReadClusterSize = 0;

                    IsPageNotResident = !MmCheckCachedPageState(CacheAddress, FALSE);

                    RtlCopyMemory(Buffer, CacheAddress, ReadLength);
                    Buffer = (PVOID)((ULONG_PTR)Buffer + ReadLength);
                }
                else
                {
                    RemainingLength = ReadLength;

                    while (RemainingLength)
                    {
                        CopyLength = ((((ULONG_PTR)CacheAddress + 1 + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1)) - (ULONG_PTR)CacheAddress);

                        if (CopyLength > RemainingLength)
                            CopyLength = RemainingLength;

                        Thread->ForwardClusterOnly = 1;

                        if (ReadPages <= MM_MAXIMUM_READ_CLUSTER_SIZE)
                            Thread->ReadClusterSize = ReadPages;
                        else
                            Thread->ReadClusterSize = MM_MAXIMUM_READ_CLUSTER_SIZE;

                        IsPageNotResident |= !MmCheckCachedPageState(CacheAddress, FALSE);

                        RtlCopyMemory(Buffer, CacheAddress, CopyLength);

                        RemainingLength -= CopyLength;
                        Buffer = (PVOID)((ULONG_PTR)Buffer + CopyLength);
                        CacheAddress = (PVOID)((ULONG_PTR)CacheAddress + CopyLength);
                        ReadPages--;
                    }
                }
            }
            _SEH2_EXCEPT(CcCopyReadExceptionFilter(_SEH2_GetExceptionInformation(), &Status))
            {
                DPRINT1("CcCopyRead: FIXME. Status %X\n", Status);
                ASSERT(FALSE);
            }
            _SEH2_END;

            Offset.QuadPart += ReadLength;
            Length -= ReadLength;
        }

        if (Length)
        {
            DPRINT1("CcCopyRead: FIXME. Length %X\n", Length);
            ASSERT(FALSE);
        }
        else
        {
            CcSetActiveVacb(SharedMap, &Vacb, ActivePage, IsVacbLocked);
        }
    }

    while (Length)
    {
        if (Wait)
        {
            CacheAddress = CcGetVirtualAddress(SharedMap, Offset, &TempVacb, &ReceivedLength);
            BeyondLastByte.QuadPart = (Offset.QuadPart + ReceivedLength);
        }
        else
        {
            DPRINT1("CcCopyRead: FIXME\n");
            ASSERT(FALSE);
        }

        if (ReceivedLength > Length)
            ReceivedLength = Length;

        _SEH2_TRY
        {
            ReadPages = (COMPUTE_PAGES_SPANNED(CacheAddress, ReceivedLength) - 1);
            if (!ReadPages)
            {
                Thread->ForwardClusterOnly = 1;
                Thread->ReadClusterSize = 0;
                IsPageNotResident |= !MmCheckCachedPageState(CacheAddress, FALSE);

                RtlCopyMemory(Buffer, CacheAddress, ReceivedLength);

                Buffer = (PVOID)((ULONG_PTR)Buffer + ReceivedLength);
            }
            else
            {
                RemainingLength = ReceivedLength;

                while (RemainingLength)
                {
                    CopyLength = ((((ULONG_PTR)CacheAddress + 1 + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1)) - (ULONG_PTR)CacheAddress);

                    if (CopyLength > RemainingLength)
                        CopyLength = RemainingLength;

                    Thread->ForwardClusterOnly = 1;

                    if (ReadPages <= MM_MAXIMUM_READ_CLUSTER_SIZE)
                        Thread->ReadClusterSize = ReadPages;
                    else
                        Thread->ReadClusterSize = MM_MAXIMUM_READ_CLUSTER_SIZE;

                    IsPageNotResident |= !MmCheckCachedPageState(CacheAddress, FALSE);

                    RtlCopyMemory(Buffer, CacheAddress, CopyLength);

                    RemainingLength -= CopyLength;
                    Buffer = (PVOID)((ULONG_PTR)Buffer + CopyLength);
                    CacheAddress = (PVOID)((ULONG_PTR)CacheAddress + CopyLength);
                    ReadPages--;
                }
            }
        }
        _SEH2_EXCEPT(CcCopyReadExceptionFilter(_SEH2_GetExceptionInformation(), &Status))
        {
            DPRINT1("CcCopyRead: FIXME. Status %X\n", Status);
            ASSERT(FALSE);
        }
        _SEH2_END;

        Length -= ReceivedLength;

        if (Wait)
        {
            if (!Length)
            {
                CcSetActiveVacb(SharedMap, &TempVacb, (Offset.QuadPart / PAGE_SIZE), FALSE);
                break;
            }

            CcFreeVirtualAddress(TempVacb);
        }
        else
        {
            ASSERT(FALSE);
        }

        Offset.QuadPart = BeyondLastByte.QuadPart;
    }

    Thread->ForwardClusterOnly = OldForwardClusterOnly;
    Thread->ReadClusterSize = OldReadClusterSize;

    if (IsPageNotResident &&
        !(FileObject->Flags & FO_RANDOM_ACCESS) &&
        !PrivateMap->Flags.ReadAheadEnabled)
    {
        InterlockedOr((PLONG)&PrivateMap->UlongFlags, 0x20000);
        CcScheduleReadAhead(FileObject, FileOffset, InputLength);
    }

    PrivateMap->FileOffset1.QuadPart = PrivateMap->FileOffset2.QuadPart;
    PrivateMap->BeyondLastByte1.QuadPart = PrivateMap->BeyondLastByte2.QuadPart;

    PrivateMap->FileOffset2.QuadPart = FileOffset->QuadPart;
    PrivateMap->BeyondLastByte2.QuadPart = (FileOffset->QuadPart + InputLength);

    OutIoStatus->Status = STATUS_SUCCESS;
    OutIoStatus->Information = InputLength;

    return TRUE;
}

BOOLEAN
NTAPI
CcCopyWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ PVOID Buffer)
{
    PSHARED_CACHE_MAP SharedMap;
    PFSRTL_ADVANCED_FCB_HEADER Fcb;
    LARGE_INTEGER ValidDataLength;
    LARGE_INTEGER fileOffset;
    LARGE_INTEGER size;
    PVOID StartAddress;
    PVOID EndAddress;
    PVOID NeedToZero;
    PVACB Vacb;
    ULONG ActivePage;
    ULONG CopyFlags;
    ULONG length;
    BOOLEAN IsVacbLocked;
    BOOLEAN Result;
    KIRQL OldIrql;

    DPRINT("CcCopyWrite: %p, [%I64X], %X, %X, %p\n",
           FileObject, (FileOffset ? FileOffset->QuadPart : 0ll), Length, Wait, Buffer);

    if ((FileObject->Flags & FO_WRITE_THROUGH) && !Wait)
        return FALSE;

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;
    fileOffset = *FileOffset;

    CcGetActiveVacb(SharedMap, &Vacb, &ActivePage, &IsVacbLocked);

    if (Vacb)
    {
        if (ActivePage == (ULONG)(fileOffset.QuadPart / PAGE_SIZE) &&
            Length && !(FileObject->Flags & FO_WRITE_THROUGH))
        {
            length = (PAGE_SIZE - (fileOffset.LowPart & (PAGE_SIZE - 1)));

            if (length > Length)
                length = Length;

//            _SEH2_TRY
{
            StartAddress = Add2Ptr(Vacb->BaseAddress, (fileOffset.LowPart & (0x40000 - 1)));

            if (SharedMap->NeedToZero)
            {
                KeAcquireSpinLock(&SharedMap->ActiveVacbSpinLock, &OldIrql);

                NeedToZero = SharedMap->NeedToZero;

                if (NeedToZero && Vacb == SharedMap->NeedToZeroVacb)
                {
                    EndAddress = Add2Ptr(StartAddress, length);

                    if ((ULONG_PTR)EndAddress > (ULONG_PTR)NeedToZero)
                    {
                        if ((ULONG_PTR)StartAddress > (ULONG_PTR)NeedToZero)
                            RtlZeroMemory(NeedToZero, ((ULONG_PTR)StartAddress - (ULONG_PTR)NeedToZero));

                        SharedMap->NeedToZero = EndAddress;
                    }
                }

                KeReleaseSpinLock(&SharedMap->ActiveVacbSpinLock, OldIrql);
            }

            RtlCopyMemory(StartAddress, Buffer, length);
}
//            _SEH2_EXCEPT()
{
}
//            _SEH2_END;

            Buffer = Add2Ptr(Buffer, length);
            fileOffset.QuadPart += length;

            Length -= length;

            if (!Length)
            {
                CcSetActiveVacb(SharedMap, &Vacb, ActivePage, TRUE);
                return TRUE;
            }

            IsVacbLocked = TRUE;
        }

        CcFreeActiveVacb(SharedMap, Vacb, ActivePage, IsVacbLocked);
    }
    else if (SharedMap->NeedToZero)
    {
        CcFreeActiveVacb(SharedMap, NULL, 0, FALSE);
    }

    CopyFlags = 2;

    if (!(fileOffset.LowPart & (PAGE_SIZE - 1)) && Length >= PAGE_SIZE)
        CopyFlags |= 1;

    if (!((fileOffset.LowPart + Length) & (PAGE_SIZE - 1)))
        CopyFlags |= 4;


    Fcb = (PFSRTL_ADVANCED_FCB_HEADER)FileObject->FsContext;

    if (Fcb->Flags & FSRTL_FLAG_ADVANCED_HEADER)
    {
        ExAcquireFastMutex(Fcb->FastMutex);
        ValidDataLength.QuadPart = Fcb->ValidDataLength.QuadPart;
        ExReleaseFastMutex(Fcb->FastMutex);
    }
    else
    {
        ValidDataLength.QuadPart = Fcb->ValidDataLength.QuadPart;
    }

    size.HighPart = fileOffset.HighPart;
    size.LowPart = (fileOffset.LowPart & ~(PAGE_SIZE - 1));

    size.QuadPart = (ValidDataLength.QuadPart - size.QuadPart);

    if (size.QuadPart > 0)
    {
        if (size.QuadPart <= PAGE_SIZE)
            CopyFlags |= 4;
    }
    else
    {
        CopyFlags |= (1 | 4);
    }

    Result = CcMapAndCopy(SharedMap, Buffer, &fileOffset, Length, CopyFlags, FileObject, &ValidDataLength, Wait);

    return Result;
}

VOID
NTAPI
CcDeferWrite(IN PFILE_OBJECT FileObject,
             IN PCC_POST_DEFERRED_WRITE PostRoutine,
             IN PVOID Context1,
             IN PVOID Context2,
             IN ULONG BytesToWrite,
             IN BOOLEAN Retrying)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcFastCopyRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG FileOffset,
    _In_ ULONG Length,
    _In_ ULONG PageCount,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PKPRCB Prcb = KeGetCurrentPrcb();
    PSHARED_CACHE_MAP SharedMap;
    PPRIVATE_CACHE_MAP PrivateMap;
    PVOID CacheAddress;
    PVACB ActiveVacb;
    PVACB Vacb;
    LARGE_INTEGER fileOffset;
    LARGE_INTEGER Offset;
    ULONG BytesCopied = Length;
    ULONG Granularity = VACB_MAPPING_GRANULARITY;
    ULONG ActivePage;
    ULONG ReceivedLength;
    ULONG CopySize;
    ULONG pageCount;
    ULONG size;
    ULONG PartialLength;
    ULONG NextOffset;
    ULONG OldReadClusterSize;
    UCHAR OldForwardClusterOnly;
    BOOLEAN IsVacbLocked;
    ULONG IsSchedule = 0;

    DPRINT("CcFastCopyRead: FileObject %p\n", FileObject);

    OldReadClusterSize = CurrentThread->ReadClusterSize;
    OldForwardClusterOnly = CurrentThread->ForwardClusterOnly;

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;
    PrivateMap = FileObject->PrivateCacheMap;

    ASSERT((FileOffset + Length) <= SharedMap->FileSize.LowPart);

    fileOffset.QuadPart = FileOffset;

    if (PrivateMap->Flags.ReadAheadEnabled && !PrivateMap->ReadAheadLength[1])
        CcScheduleReadAhead(FileObject, &fileOffset, Length);

    Prcb->CcCopyReadWait++;

    CcGetActiveVacb(SharedMap, &ActiveVacb, &ActivePage, &IsVacbLocked);

    if (ActiveVacb)
    {
        if ((FileOffset / Granularity) == (ActivePage / (Granularity / PAGE_SIZE)))
        {
            if (SharedMap->NeedToZero)
                CcFreeActiveVacb(SharedMap, NULL, 0, FALSE);

            CacheAddress = (PVOID)(((ULONG_PTR)ActiveVacb->BaseAddress + (FileOffset & (Granularity - 1))));
            CopySize = (Granularity - (FileOffset & (Granularity - 1)));

            if (CopySize > Length)
                CopySize = Length;

            pageCount = ((((ULONG_PTR)CacheAddress & (PAGE_SIZE - 1)) + CopySize + (PAGE_SIZE - 1)) / PAGE_SIZE) - 1;

//            _SEH2_TRY
            {
                if (!pageCount)
                {
                    CurrentThread->ForwardClusterOnly = 1;
                    CurrentThread->ReadClusterSize = 0;

                    IsSchedule = !MmCheckCachedPageState(CacheAddress, FALSE);

                    RtlCopyMemory(Buffer, CacheAddress, CopySize);

                    Buffer = (PVOID)((ULONG_PTR)Buffer + CopySize);
                }
                else
                {
                    for (size = CopySize; size; size -= PartialLength)
                    {
                        PartialLength = (ULONG)((((ULONG_PTR)CacheAddress + PAGE_SIZE) & ~(PAGE_SIZE - 1)) - (ULONG_PTR)CacheAddress);

                        if (PartialLength > size)
                            PartialLength = size;

                        CurrentThread->ForwardClusterOnly = 1;

                        if (pageCount <= 0xF)
                            CurrentThread->ReadClusterSize = pageCount;
                        else
                            CurrentThread->ReadClusterSize = 0xF;

                        IsSchedule |= !MmCheckCachedPageState(CacheAddress, FALSE);

                        RtlCopyMemory(Buffer, CacheAddress, PartialLength);

                        CacheAddress = (PVOID)((ULONG_PTR)CacheAddress + PartialLength);
                        Buffer = (PVOID)((ULONG_PTR)Buffer + PartialLength);
                        pageCount--;
                    }
                }
            }
//            _SEH2_EXCEPT()
            {
            }
//            _SEH2_END

            FileOffset += CopySize;
            Length -= CopySize;
        }

        if (Length)
            CcFreeActiveVacb(SharedMap, ActiveVacb, ActivePage, IsVacbLocked);
        else
            CcSetActiveVacb(SharedMap, &ActiveVacb, ActivePage, IsVacbLocked);
    }

    Offset.HighPart = 0;

    for (Offset.LowPart = FileOffset; Length; Offset.LowPart = NextOffset)
    {
        CacheAddress = CcGetVirtualAddress(SharedMap, Offset, &Vacb, &ReceivedLength);

        NextOffset = (Offset.LowPart + ReceivedLength);

        if (ReceivedLength > Length)
            ReceivedLength = Length;

//        _SEH2_TRY
        {
            pageCount = ((((ULONG_PTR)CacheAddress & (PAGE_SIZE - 1)) + ReceivedLength + (PAGE_SIZE - 1)) / PAGE_SIZE) - 1;

            if (!pageCount)
            {
                CurrentThread->ForwardClusterOnly = 1;
                CurrentThread->ReadClusterSize = 0;

                IsSchedule |= !MmCheckCachedPageState(CacheAddress, FALSE);

                RtlCopyMemory(Buffer, CacheAddress, ReceivedLength);

                Buffer = (PVOID)((ULONG_PTR)Buffer + ReceivedLength);
            }
            else
            {
                for (size = ReceivedLength; size; size -= PartialLength)
                {
                    PartialLength = ((ULONG)((ULONG_PTR)CacheAddress + PAGE_SIZE) & ~(PAGE_SIZE - 1)) - (ULONG_PTR)CacheAddress;

                    if (PartialLength > size)
                        PartialLength = size;

                    CurrentThread->ForwardClusterOnly = 1;

                    if (pageCount <= 0xF)
                        CurrentThread->ReadClusterSize = pageCount;
                    else
                        CurrentThread->ReadClusterSize = 0xF;

                    IsSchedule |= !MmCheckCachedPageState(CacheAddress, FALSE);

                    RtlCopyMemory(Buffer, CacheAddress, PartialLength);

                    CacheAddress = (PVOID)((ULONG_PTR)CacheAddress + PartialLength);
                    Buffer = (PVOID)((ULONG_PTR)Buffer + PartialLength);
                    pageCount--;
                }
            }
        }
//        _SEH2_EXCEPT()
        {
        }
//        _SEH2_END

        Length -= ReceivedLength;
        if (!Length)
        {
            CcSetActiveVacb(SharedMap, &Vacb, (Offset.LowPart / PAGE_SIZE), 0);
            break;
        }

        CcFreeVirtualAddress(Vacb);
    }

    CurrentThread->ReadClusterSize = OldReadClusterSize;
    CurrentThread->ForwardClusterOnly = OldForwardClusterOnly;

    if (IsSchedule &&
        !(FileObject->Flags & FO_RANDOM_ACCESS) &&
        !(PrivateMap->UlongFlags & 0x20000))
    {
        InterlockedOr((PLONG)&PrivateMap->UlongFlags, 0x20000);
        CcScheduleReadAhead(FileObject, &fileOffset, BytesCopied);
    }

    PrivateMap->FileOffset1.LowPart = PrivateMap->FileOffset2.LowPart;
    PrivateMap->BeyondLastByte1.LowPart = PrivateMap->BeyondLastByte2.LowPart;


    PrivateMap->FileOffset2.LowPart = fileOffset.LowPart;
    PrivateMap->BeyondLastByte2.LowPart = fileOffset.LowPart + BytesCopied;

    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = BytesCopied;
}

VOID
NTAPI
CcFastCopyWrite(IN PFILE_OBJECT FileObject,
                IN ULONG FileOffset,
                IN ULONG Length,
                IN PVOID Buffer)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
