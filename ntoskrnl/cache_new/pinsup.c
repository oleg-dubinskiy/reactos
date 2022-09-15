
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
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
    _In_ BOOLEAN IsNoWrite)
{
    DPRINT("CcUnpinFileDataEx: Bcb %p, IsNoWrite %X\n", Bcb, IsNoWrite);

    if (Bcb->NodeTypeCode != NODE_TYPE_BCB)
    {
        PVACB Vacb = (PVACB)Bcb;

        ASSERT((Vacb >= CcVacbs) && (Vacb < CcBeyondVacbs));
        ASSERT(Vacb->SharedCacheMap->NodeTypeCode == NODE_TYPE_SHARED_MAP);

        CcFreeVirtualAddress(Vacb);

        return;
    }

    ASSERT(FALSE);
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
        DPRINT1("CcMapData: failed то map\n");
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
             BaseAddress = (PVOID)((ULONG_PTR)BaseAddress + PAGE_SIZE))
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
    *OutBcb = (PVOID)((ULONG_PTR)Bcb + 1);

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
CcPinRead(IN PFILE_OBJECT FileObject,
          IN PLARGE_INTEGER FileOffset,
          IN ULONG Length,
          IN ULONG Flags,
          OUT PVOID *Bcb,
          OUT PVOID *Buffer)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
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
CcUnpinData(IN PVOID Bcb)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcUnpinDataForThread(IN PVOID Bcb,
                     IN ERESOURCE_THREAD ResourceThreadId)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
