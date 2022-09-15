
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


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

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
CcCanIWrite(IN PFILE_OBJECT FileObject,
            IN ULONG BytesToWrite,
            IN BOOLEAN Wait,
            IN UCHAR Retrying)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
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
        CcScheduleReadAhead(FileObject, FileOffset, Length);
    }

    PrivateMap->FileOffset1.QuadPart = PrivateMap->FileOffset2.QuadPart;
    PrivateMap->BeyondLastByte1.QuadPart = PrivateMap->BeyondLastByte2.QuadPart;

    PrivateMap->FileOffset2.QuadPart = FileOffset->QuadPart;
    PrivateMap->BeyondLastByte2.QuadPart = FileOffset->QuadPart + Length;

    OutIoStatus->Status = STATUS_SUCCESS;
    OutIoStatus->Information = Length;

    return TRUE;
}

BOOLEAN
NTAPI
CcCopyWrite(IN PFILE_OBJECT FileObject,
            IN PLARGE_INTEGER FileOffset,
            IN ULONG Length,
            IN BOOLEAN Wait,
            IN PVOID Buffer)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
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
CcFastCopyRead(IN PFILE_OBJECT FileObject,
               IN ULONG FileOffset,
               IN ULONG Length,
               IN ULONG PageCount,
               OUT PVOID Buffer,
               OUT PIO_STATUS_BLOCK IoStatus)
{
    UNIMPLEMENTED_DBGBREAK();
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
