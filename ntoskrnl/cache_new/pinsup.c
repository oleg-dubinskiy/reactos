
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

extern PVACB CcVacbs;
extern PVACB CcBeyondVacbs;

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
CcMapDataCommon(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _Out_ PVOID* OutBcb,
    _Out_ PVOID* OutBuffer)
{
    PSHARED_CACHE_MAP SharedMap;
    ULONG ReceivedLength;
    PVOID Bcb;

    DPRINT("CcMapDataCommon: %p, %I64X, %X, %X\n", FileObject, (FileOffset ? FileOffset->QuadPart : 0), Length, Flags);

    if (Flags & MAP_WAIT)
    {
        CcMapDataWait++;

        SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;
        *OutBuffer = CcGetVirtualAddress(SharedMap, *FileOffset, (PVACB *)&Bcb, &ReceivedLength);

        ASSERT(ReceivedLength >= Length);
        goto Exit;
    }

    CcMapDataNoWait++;

    DPRINT1("CcMapDataCommon: FIXME! Flags %X\n", Flags);
    ASSERT(FALSE);

Exit:

    *OutBcb = Bcb;
    return TRUE;
}

VOID
NTAPI
CcUnpinFileDataEx(
    _In_ PCC_BCB Bcb,
    _In_ BOOLEAN IsNoWrite,
    _In_ ULONG Type)
{
    KLOCK_QUEUE_HANDLE LockHandle;
    PSHARED_CACHE_MAP SharedMap;

    DPRINT("CcUnpinFileDataEx: Bcb %p, IsNoWrite %X, Type %X\n", Bcb, IsNoWrite, Type);

    if (Bcb->NodeTypeCode != NODE_TYPE_BCB)
    {
        PVACB Vacb = (PVACB)Bcb;

        ASSERT((Vacb >= CcVacbs) && (Vacb < CcBeyondVacbs));
        ASSERT(Vacb->SharedCacheMap->NodeTypeCode == NODE_TYPE_SHARED_MAP);

        CcFreeVirtualAddress(Vacb);

        return;
    }

    SharedMap = Bcb->SharedCacheMap;

    if (!(SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE) || Type == 1)
        IsNoWrite = TRUE;

    KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);

    switch (Type)
    {
        case 0:
        case 1:
        {
            ASSERT(Bcb->PinCount > 0);
            Bcb->PinCount--;
            break;
        }
        case 2:
        {
            DPRINT1("CcUnpinFileDataEx: FIXME\n");
            ASSERT(FALSE);
            break;
        }
        default:
        {
            DPRINT1("CcUnpinFileDataEx: BugCheck() FIXME\n");
            ASSERT(FALSE);
            //BugCheck();
        }
    }

    if (!Bcb->PinCount)
    {
        if (Bcb->Reserved1[0])
        {
            DPRINT1("CcUnpinFileDataEx: FIXME\n");
            ASSERT(FALSE);
        }
        else
        {
            KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[4]);

            RemoveEntryList(&Bcb->Link);

            if (SharedMap->SectionSize.QuadPart > CACHE_OVERALL_SIZE &&
                (SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE))
            {
                DPRINT1("CcUnpinFileDataEx: FIXME\n");
                ASSERT(FALSE);
            }

            KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[4]);

            if (Bcb->BaseAddress)
                CcFreeVirtualAddress(Bcb->Vacb);

            if (!IsNoWrite)
                ExReleaseResourceLite(&Bcb->BcbResource);

            ASSERT(Bcb->BcbResource.ActiveCount == 0);

            KeReleaseInStackQueuedSpinLock(&LockHandle);

            CcDeallocateBcb(Bcb);
        }
    }
    else
    {
        DPRINT1("CcUnpinFileDataEx: FIXME\n");
        ASSERT(FALSE);
    }
}

BOOLEAN
NTAPI
CcPinFileData(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN IsNoWrite,
    _In_ BOOLEAN Flag5,
    _In_ ULONG PinFlags,
    _Out_ PCC_BCB* OutBcb,
    _Out_ PVOID* OutBuffer,
    _Out_ LARGE_INTEGER* OutBeyondLastByte)
{
    KLOCK_QUEUE_HANDLE LockHandle;
    PSHARED_CACHE_MAP SharedMap;
    PVACB Vacb = NULL;
    PCC_BCB Bcb = NULL;
    LARGE_INTEGER EndFileOffset;
    LARGE_INTEGER offset;
    LARGE_INTEGER length;
    ULONG ActivePage;
    ULONG ReceivedLength;
    ULONG MapFlags = 0;
    BOOLEAN IsLocked;
    BOOLEAN IsVacbLocked;
    BOOLEAN Result = FALSE;

    DPRINT("CcPinFileData: %p, [%I64X], %X, %X, %X, %X\n",
           FileObject, (FileOffset ? FileOffset->QuadPart : 0ll), Length, IsNoWrite, Flag5, PinFlags);

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;

    CcGetActiveVacb(SharedMap, &Vacb, &ActivePage, &IsVacbLocked);

    if (Vacb || SharedMap->NeedToZero)
    {
        CcFreeActiveVacb(SharedMap, Vacb, ActivePage, IsVacbLocked);
        Vacb = NULL;
    }

    ASSERT((FileOffset->QuadPart + (LONGLONG)Length) <= SharedMap->SectionSize.QuadPart);

    *OutBcb = NULL;
    *OutBuffer = NULL;

    if (PinFlags & PIN_NO_READ)
    {
        DPRINT1("CcPinFileData: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        *OutBuffer = CcGetVirtualAddress(SharedMap, *FileOffset, &Vacb, &ReceivedLength);
    }

    KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);
    IsLocked = TRUE;

    //_SEH2_TRY

    EndFileOffset.QuadPart = (FileOffset->QuadPart + Length);

    if (CcFindBcb(SharedMap, FileOffset, &EndFileOffset, &Bcb))
    {
        DPRINT1("CcPinFileData: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        if (PinFlags & PIN_IF_BCB)
        {
            Bcb = NULL;
            Result = FALSE;
            goto Finish;
        }

        offset.QuadPart = FileOffset->QuadPart;
        length.QuadPart = (EndFileOffset.QuadPart - offset.QuadPart);

        length.LowPart += (offset.LowPart & (PAGE_SIZE - 1));
        ReceivedLength += (offset.LowPart & (PAGE_SIZE - 1));

        if ((!IsNoWrite && !(SharedMap->Flags & SHARE_FL_PIN_ACCESS)) || Flag5)
        {
            if (!(offset.LowPart & (PAGE_SIZE - 1)) && Length >= PAGE_SIZE)
                MapFlags = 3;
            else
                MapFlags = 2;

            if (!(length.LowPart & (PAGE_SIZE - 1)))
                MapFlags |= 4;
        }

        if (!(SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE))
            IsNoWrite = TRUE;

        *OutBuffer = ((PCHAR)*OutBuffer - (FileOffset->LowPart & (PAGE_SIZE - 1)));

        offset.LowPart &= ~(PAGE_SIZE - 1);

        length.LowPart = ROUND_TO_PAGES(length.LowPart);

        if (length.LowPart > ReceivedLength)
            length.LowPart = ReceivedLength;

        Bcb = CcAllocateInitializeBcb(SharedMap, Bcb, &offset, &length);

        if (PinFlags & PIN_WAIT)
        {
            if (!Bcb)
            {
                KeReleaseInStackQueuedSpinLock(&LockHandle);
                IsLocked = FALSE;

                DPRINT1("CcPinFileData: STATUS_INSUFFICIENT_RESOURCES\n");
                RtlRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
            }

            if (!IsNoWrite)
            {
                /* For writing need acquire Bcb */
                if (PinFlags & PIN_EXCLUSIVE)
                    ExAcquireResourceExclusiveLite(&Bcb->BcbResource, TRUE);
                else
                    ExAcquireSharedStarveExclusive(&Bcb->BcbResource, TRUE);
            }

            KeReleaseInStackQueuedSpinLock(&LockHandle);
            IsLocked = FALSE;

            if (PinFlags & PIN_NO_READ)
            {
                Result = TRUE;
                goto Finish;
            }

            CcMapAndRead(SharedMap, &offset, length.LowPart, MapFlags, TRUE, *OutBuffer);

            KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);
            if (!Bcb->BaseAddress)
            {
                Bcb->BaseAddress = *OutBuffer;
                Bcb->Vacb = Vacb;
                Vacb = NULL;
            }
            KeReleaseInStackQueuedSpinLock(&LockHandle);

            *OutBuffer = Add2Ptr(Bcb->BaseAddress, (FileOffset->LowPart - Bcb->FileOffset.LowPart));

            Result = TRUE;
            goto Finish;
        }
        else
        {
            DPRINT1("CcPinFileData: FIXME\n");
            ASSERT(FALSE);
        }
    }

Finish:

    if ((PinFlags & PIN_NO_READ) && (PinFlags & PIN_EXCLUSIVE) && Bcb && Bcb->BaseAddress)
    {
        CcFreeVirtualAddress(Bcb->Vacb);

        Bcb->BaseAddress = NULL;
        Bcb->Vacb = NULL;
    }

    //_SEH2_FINALLY

    if (IsLocked)
        KeReleaseInStackQueuedSpinLock(&LockHandle);

    if (Vacb)
        CcFreeVirtualAddress(Vacb);

    if (PinFlags & PIN_NO_READ)
    {
        DPRINT1("CcPinFileData: FIXME\n");
        ASSERT(FALSE);
    }

    if (Result)
    {
        *OutBcb = Bcb;
        OutBeyondLastByte->QuadPart = Bcb->BeyondLastByte.QuadPart;
    }
    else
    {
        *OutBuffer = NULL;

        if (Bcb)
            CcUnpinFileDataEx(Bcb, IsNoWrite, 0);
    }

    return Result;

    //_SEH2_END;
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
CcMapData(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _Out_ PVOID* OutBcb,
    _Out_ PVOID* OutBuffer)
{
    PETHREAD Thread = PsGetCurrentThread();
    PVOID BaseAddress;
    PVOID Bcb;
    ULONG NumberOfPages;
    ULONG OldReadClusterSize;
    ULONG Size;
    UCHAR OldForwardClusterOnly;
    UCHAR Probe;
    BOOLEAN Result;

    DPRINT("CcMapData: %p, %I64X, %X, %X\n", FileObject, (FileOffset ? FileOffset->QuadPart : 0), Length, Flags);
   
    /* Save previous values */
    OldForwardClusterOnly = Thread->ForwardClusterOnly;
    OldReadClusterSize = Thread->ReadClusterSize;

    /* Maps a file to a buffer */
    Result = CcMapDataCommon(FileObject, FileOffset, Length, Flags, &Bcb, OutBuffer);
    if (!Result)
    {
        DPRINT1("CcMapData: failed �� map\n");
        return Result;
    }

    /* Check flags */
    if (Flags & MAP_NO_READ)
        goto Exit;

    /* Calculates count pages */
    Size = (Length + BYTE_OFFSET(FileOffset->LowPart));
    NumberOfPages = ((Size + (PAGE_SIZE - 1)) / PAGE_SIZE);

    /* If the pages are not in memory, PageFault() will read them */
    _SEH2_TRY
    {
        for (BaseAddress = *OutBuffer;
             NumberOfPages;
             BaseAddress = Add2Ptr(BaseAddress, PAGE_SIZE))
        {
            /* Claster variables used in MiResolveMappedFileFault() */
            Thread->ForwardClusterOnly = 1;
            NumberOfPages--;

            if (NumberOfPages <= MM_MAXIMUM_READ_CLUSTER_SIZE)
                Thread->ReadClusterSize = NumberOfPages;
            else
                Thread->ReadClusterSize = MM_MAXIMUM_READ_CLUSTER_SIZE;

            /* Test address */
            *(PUCHAR)&Probe = *(PUCHAR)BaseAddress;
        }
    }
    _SEH2_FINALLY
    {
        /* Restore claster variables */
        Thread->ForwardClusterOnly = OldForwardClusterOnly;
        Thread->ReadClusterSize = OldReadClusterSize;

        if (_SEH2_AbnormalTermination() && Bcb)
        {
            /* Releases cached file data that has been mapped or pinned */
            DPRINT1("CcMapData: FIXME CcUnpinFileDataEx()\n");
            ASSERT(FALSE);
        }
    }
    _SEH2_END;

Exit:

    /* Windows does this */
    *OutBcb = Add2Ptr(Bcb, 1);

    return TRUE;
}

BOOLEAN
NTAPI
CcPinMappedData(IN PFILE_OBJECT FileObject,
                IN PLARGE_INTEGER FileOffset,
                IN ULONG Length,
                IN ULONG Flags,
                IN OUT PVOID *Bcb)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
CcPinRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _Out_ PVOID* OutBcb,
    _Out_ PVOID* OutBuffer)
{
    PSHARED_CACHE_MAP SharedMap;
    LARGE_INTEGER fileOffset;
    LARGE_INTEGER EndOffset;
    LARGE_INTEGER CurrentOffset;
    PVOID Buffer;
    PVOID Bcb = NULL;
    PVOID* pBcb = &Bcb;
    BOOLEAN Result = FALSE;

    fileOffset.QuadPart = FileOffset->QuadPart;
    EndOffset.QuadPart = (fileOffset.QuadPart + Length);

    DPRINT("CcPinRead: %p, [%I64X], %X, %X\n", FileObject, fileOffset.QuadPart, Length, Flags);

    if (Flags & PIN_WAIT)
        CcPinReadWait++;
    else
        CcPinReadNoWait++;

    SharedMap = FileObject->SectionObjectPointer->SharedCacheMap;

    //_SEH2_TRY

    do
    {
        if (Bcb)
        {
            DPRINT1("CcPinRead: FIXME\n");
            ASSERT(FALSE);
        }

        if (!CcPinFileData(FileObject,
                           &fileOffset,
                           Length,
                           !(SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE),
                           0,
                           Flags,
                           (PCC_BCB *)pBcb,
                           &Buffer,
                           &CurrentOffset))
        {
            Result = FALSE;
            goto Exit;
        }
    }
    while (CurrentOffset.QuadPart < EndOffset.QuadPart);

    *OutBcb = Bcb;

    if (pBcb == &Bcb)
        *OutBuffer = Buffer;

    Result = TRUE;

Exit:

    //_SEH2_FINALLY

    if (!Result && Bcb)
        CcUnpinData(Bcb);

    return Result;

    //_SEH2_END;
}

BOOLEAN
NTAPI
CcPreparePinWrite(IN PFILE_OBJECT FileObject,
                  IN PLARGE_INTEGER FileOffset,
                  IN ULONG Length,
                  IN BOOLEAN Zero,
                  IN ULONG Flags,
                  OUT PVOID *Bcb,
                  OUT PVOID *Buffer)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

VOID
NTAPI
CcSetBcbOwnerPointer(IN PVOID Bcb,
                     IN PVOID OwnerPointer)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcUnpinData(
    _In_ PVOID InBcb)
{
    PCC_BCB Bcb = InBcb;
    BOOLEAN IsNoWrite;

    DPRINT("CcUnpinData: Bcb %p\n", Bcb);

    if ((ULONG_PTR)Bcb & 1)
    {
        IsNoWrite = TRUE;
        Bcb = (PCC_BCB)((ULONG_PTR)Bcb & ~(1));

        CcUnpinFileDataEx(Bcb, IsNoWrite, 0);

        return;
    }

    ASSERT(FALSE);
}

VOID
NTAPI
CcUnpinDataForThread(IN PVOID Bcb,
                     IN ERESOURCE_THREAD ResourceThreadId)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
