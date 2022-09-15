
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


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

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
CcMapData(IN PFILE_OBJECT FileObject,
          IN PLARGE_INTEGER FileOffset,
          IN ULONG Length,
          IN ULONG Flags,
          OUT PVOID *BcbResult,
          OUT PVOID *Buffer)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
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
