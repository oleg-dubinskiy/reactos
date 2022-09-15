
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PVACB CcVacbs;
PVACB CcBeyondVacbs;
LIST_ENTRY CcVacbLru;
LIST_ENTRY CcVacbFreeList;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
CcInitializeVacbs(VOID)
{
    PVACB CurrentVacb;
    ULONG CcNumberVacbs;
    ULONG SizeOfVacbs;

    CcNumberVacbs = ((MmSizeOfSystemCacheInPages / (VACB_MAPPING_GRANULARITY / PAGE_SIZE)) - 2);
    SizeOfVacbs = (CcNumberVacbs * sizeof(VACB));

    DPRINT("CcInitializeVacbs: MmSizeOfSystemCacheInPages %X, CcNumberVacbs %X\n",
           MmSizeOfSystemCacheInPages, CcNumberVacbs);

    CcVacbs = ExAllocatePoolWithTag(NonPagedPool, SizeOfVacbs, 'aVcC');
    if (!CcVacbs)
    {
        DPRINT1("CcInitializeVacbs: allocate VACBs failed\n");
        return;
    }

    RtlZeroMemory(CcVacbs, SizeOfVacbs);

    CcBeyondVacbs = &CcVacbs[CcNumberVacbs];

    InitializeListHead(&CcVacbLru);
    InitializeListHead(&CcVacbFreeList);

    for (CurrentVacb = CcVacbs; CurrentVacb < CcBeyondVacbs; CurrentVacb++)
        InsertTailList(&CcVacbFreeList, &CurrentVacb->LruList);
}

NTSTATUS
NTAPI
CcCreateVacbArray(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LARGE_INTEGER AllocationSize)
{
    PVACB* NewVacbs;
    ULONG NewSize;

    DPRINT("CcCreateVacbArray: SharedMap %p AllocationSize %I64X\n", SharedMap, AllocationSize.QuadPart);

    if ((ULONGLONG)AllocationSize.QuadPart >= (4ull * _1TB))
    {
        DPRINT1("CcCreateVacbArray: STATUS_SECTION_TOO_BIG\n");
        return STATUS_SECTION_TOO_BIG;
    }

    if ((ULONGLONG)AllocationSize.QuadPart >= (4ull * _1GB))
    {
        NewSize = 0xFFFFFFFF;
        DPRINT("CcCreateVacbArray: NewSize %X\n", NewSize);
    }
    else if (AllocationSize.LowPart <= (VACB_MAPPING_GRANULARITY * sizeof(PVACB)))
    {
        NewSize = (CC_DEFAULT_NUMBER_OF_VACBS * sizeof(PVACB));
        DPRINT("CcCreateVacbArray: NewSize %X\n", NewSize);
    }
    else
    {
        NewSize = ((AllocationSize.LowPart / VACB_MAPPING_GRANULARITY) * sizeof(PVACB));
        DPRINT("CcCreateVacbArray: NewSize %X\n", NewSize);
    }


    if (NewSize == (CC_DEFAULT_NUMBER_OF_VACBS * sizeof(PVACB)))
    {
        NewVacbs = SharedMap->InitialVacbs;
    }
    else
    {
        DPRINT1("CcCreateVacbArray: FIXME! NewSize %X\n", NewSize);
        ASSERT(FALSE);
    }

    RtlZeroMemory(NewVacbs, NewSize);

    SharedMap->SectionSize.QuadPart = AllocationSize.QuadPart;
    SharedMap->Vacbs = NewVacbs;

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
CcUnmapVacbArray(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN FrontOfList)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

VOID
NTAPI
SetVacb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LARGE_INTEGER SectionOffset,
    _In_ PVACB Vacb)
{
    DPRINT("SetVacb: %p, %I64X, %p\n", SharedMap, SectionOffset.QuadPart, Vacb);

    if (SharedMap->SectionSize.QuadPart <= CACHE_OVERALL_SIZE)
    {
        SharedMap->Vacbs[SectionOffset.LowPart / VACB_MAPPING_GRANULARITY] = Vacb;
        return;
    }

    DPRINT1("SetVacb: FIXME CcSetVacbLargeOffset()\n");
    ASSERT(FALSE);
}

PVACB
NTAPI
CcGetVacbMiss(
     _In_ PSHARED_CACHE_MAP SharedMap,
     _In_ LARGE_INTEGER FileOffset,
     _In_ PKLOCK_QUEUE_HANDLE LockHandle,
     _In_ BOOLEAN IsMmodifiedNoWrite)
{
    PVACB Vacb;
    PVACB OutVacb;
    LARGE_INTEGER ViewSize;
    LARGE_INTEGER SectionOffset;
    NTSTATUS Status;

    DPRINT("CcGetVacbMiss: %p, %I64X, %X\n", SharedMap, FileOffset.QuadPart, IsMmodifiedNoWrite);

    SectionOffset = FileOffset;
    SectionOffset.LowPart -= (FileOffset.LowPart & (VACB_MAPPING_GRANULARITY - 1));

    if (!(SharedMap->Flags & SHARE_FL_RANDOM_ACCESS) &&
        !(SectionOffset.LowPart & (0x80000 - 1)) &&
        SectionOffset.QuadPart >= 0x100000)
    {
        if (IsMmodifiedNoWrite)
        {
            KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
            KeReleaseInStackQueuedSpinLock(LockHandle);
        }
        else
        {
            KeReleaseQueuedSpinLock(LockQueueVacbLock, LockHandle->OldIrql);
        }

        ExReleasePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);
        
        ViewSize.QuadPart = (SectionOffset.QuadPart - 0x100000);
        CcUnmapVacbArray(SharedMap, &ViewSize, 0x100000, TRUE);

        ExAcquirePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);

        if (IsMmodifiedNoWrite)
        {
            KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, LockHandle);
            KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
        }
        else
        {
            LockHandle->OldIrql = KeAcquireQueuedSpinLock(LockQueueVacbLock);
        }
    }

    if (!IsListEmpty(&CcVacbFreeList))
    {
        Vacb = CONTAINING_RECORD(CcVacbFreeList.Flink, VACB, LruList);

        RemoveEntryList(&Vacb->LruList);
        InsertTailList(&CcVacbLru, &Vacb->LruList);
    }
    else
    {
        DPRINT1("CcGetVacbMiss: FIXME\n");
        ASSERT(FALSE);
    }

    if (Vacb->SharedCacheMap)
    {
        SetVacb(Vacb->SharedCacheMap, Vacb->Overlay.FileOffset, NULL);
        Vacb->SharedCacheMap = NULL;
    }

    Vacb->Overlay.ActiveCount = 1;
    SharedMap->VacbActiveCount++;

    if (IsMmodifiedNoWrite)
    {
        KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
        KeReleaseInStackQueuedSpinLock(LockHandle);
    }
    else
    {
        KeReleaseQueuedSpinLock(LockQueueVacbLock, LockHandle->OldIrql);
    }

    if (Vacb->BaseAddress)
    {
        DPRINT("CcGetVacbMiss: FIXME CcDrainVacbLevelZone()\n");
        ASSERT(FALSE);
    }

    ViewSize.QuadPart = (SharedMap->SectionSize.QuadPart - SectionOffset.QuadPart);

    if (ViewSize.HighPart || (ViewSize.LowPart > VACB_MAPPING_GRANULARITY))
        ViewSize.LowPart = VACB_MAPPING_GRANULARITY;

    _SEH2_TRY
    {
        Status = MmMapViewInSystemCache(SharedMap->Section,
                                        &Vacb->BaseAddress,
                                        &SectionOffset,
                                        &ViewSize.LowPart);
        if (!NT_SUCCESS(Status))
        {
            Vacb->BaseAddress = NULL;
            Status = FsRtlNormalizeNtstatus(Status, STATUS_UNEXPECTED_MM_MAP_ERROR);
            RtlRaiseStatus(Status);
        }

        if (SharedMap->SectionSize.QuadPart <= CACHE_OVERALL_SIZE)
        {
            if (IsMmodifiedNoWrite)
            {
                KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, LockHandle);
                KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
            }
            else
            {
                LockHandle->OldIrql = KeAcquireQueuedSpinLock(LockQueueVacbLock);
            }
        }
        else
        {
            DPRINT1("CcGetVacbMiss: FIXME CcPrefillVacbLevelZone()\n");
            ASSERT(FALSE);
        }
    }
    _SEH2_FINALLY
    {
        if (_SEH2_AbnormalTermination())
        {
            if (Vacb->BaseAddress)
            {
                DPRINT1("CcGetVacbMiss: FIXME CcUnmapVacb()\n");
                ASSERT(FALSE);
            }

            ExReleasePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);
            LockHandle->OldIrql = KeAcquireQueuedSpinLock(LockQueueVacbLock);

            ASSERT((Vacb->Overlay.ActiveCount) != 0);
            Vacb->Overlay.ActiveCount--;

            ASSERT((SharedMap->VacbActiveCount) != 0);
            SharedMap->VacbActiveCount--;

            if (SharedMap->WaitOnActiveCount)
                KeSetEvent(SharedMap->WaitOnActiveCount, 0, FALSE);

            ASSERT(Vacb->SharedCacheMap == NULL);

            RemoveEntryList(&Vacb->LruList);
            InsertHeadList(&CcVacbFreeList, &Vacb->LruList);

            KeReleaseQueuedSpinLock(LockQueueVacbLock, LockHandle->OldIrql);
        }
    }
    _SEH2_END;

    if (SharedMap->SectionSize.QuadPart <= CACHE_OVERALL_SIZE)
    {
        OutVacb = SharedMap->Vacbs[SectionOffset.LowPart / VACB_MAPPING_GRANULARITY];
    }
    else
    {
        DPRINT1("CcGetVacbMiss: FIXME CcGetVacbLargeOffset()\n");
        ASSERT(FALSE);
    }

    if (!OutVacb)
    {
        OutVacb = Vacb;
        OutVacb->SharedCacheMap = SharedMap;

        OutVacb->Overlay.FileOffset.QuadPart = SectionOffset.QuadPart;
        OutVacb->Overlay.ActiveCount = 1;

        SetVacb(SharedMap, SectionOffset, OutVacb);
    }
    else
    {
        DPRINT1("CcGetVacbMiss: FIXME CcUnmapVacb()\n");
        ASSERT(FALSE);
    }

    return OutVacb;
}

PVOID
NTAPI
CcGetVirtualAddress(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LARGE_INTEGER FileOffset,
    _Out_ PVACB* OutVacb,
    _Out_ ULONG* OutReceivedLength)
{
    PVACB Vacb;
    KLOCK_QUEUE_HANDLE LockHandle;
    ULONG VacbOffset;
    BOOLEAN IsMmodifiedNoWrite = FALSE;

    DPRINT("CcGetVirtualAddress: SharedMap %p, Offset %I64X\n", SharedMap, FileOffset.QuadPart);

    /* Calculate the offset in VACB */
    VacbOffset = (FileOffset.LowPart & (VACB_MAPPING_GRANULARITY - 1));

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    /* Lock */
    ExAcquirePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);

    if (SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE)
    {
        IsMmodifiedNoWrite = TRUE;
        KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);
        KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
    }
    else
    {
        LockHandle.OldIrql = KeAcquireQueuedSpinLock(LockQueueVacbLock);
    }

    ASSERT(FileOffset.QuadPart <= SharedMap->SectionSize.QuadPart);

    /* Get pointer to Vacb */
    if (SharedMap->SectionSize.QuadPart <= CACHE_OVERALL_SIZE)
    {
        /* Size of file < 32 MB*/
        Vacb = SharedMap->Vacbs[FileOffset.LowPart / VACB_MAPPING_GRANULARITY];
    }
    else
    {
        /* This file is large (more than 32 MB) */
        DPRINT1("CcGetVirtualAddress: FIXME CcGetVacbLargeOffset\n");
        ASSERT(FALSE);
        Vacb = NULL;
    }

    if (Vacb)
    {
        /* Increment counters */
        if (!Vacb->Overlay.ActiveCount)
            SharedMap->VacbActiveCount++;

        Vacb->Overlay.ActiveCount++;
    }
    else
    {
        /* Vacb not found */
        Vacb = CcGetVacbMiss(SharedMap, FileOffset, &LockHandle, IsMmodifiedNoWrite);
    }

    /* Updating lists */
    RemoveEntryList(&Vacb->LruList);
    InsertTailList(&CcVacbLru, &Vacb->LruList);

    /* Unlock */
    if (!IsMmodifiedNoWrite)
    {
        KeReleaseQueuedSpinLock(LockQueueVacbLock, LockHandle.OldIrql);
    }
    else
    {
        KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
        KeReleaseInStackQueuedSpinLock(&LockHandle);
    }

    ExReleasePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);

    *OutVacb = Vacb;
    *OutReceivedLength = (VACB_MAPPING_GRANULARITY - VacbOffset);

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
    ASSERT(Vacb->BaseAddress != NULL);

    /* Add an offset to the base and return the virtual address */
    return (PVOID)((ULONG_PTR)Vacb->BaseAddress + VacbOffset);
}

VOID
NTAPI
CcFreeVirtualAddress(
    _In_ PVACB Vacb)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
