
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PVACB CcVacbs;
PVACB CcBeyondVacbs;
LIST_ENTRY CcVacbLru;
LIST_ENTRY CcVacbFreeList;

extern SHARED_CACHE_MAP_LIST_CURSOR CcDirtySharedCacheMapList;
extern ULONG CcTotalDirtyPages;
extern LAZY_WRITER LazyWriter;

/* FUNCTIONS ******************************************************************/

PLIST_ENTRY
NTAPI
CcGetBcbListHeadLargeOffset(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LONGLONG FileOffset,
    _In_ BOOLEAN Flag3)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PLIST_ENTRY
NTAPI
CcGetBcbListHead(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LONGLONG FileOffset,
    _In_ BOOLEAN Flag3)
{
    PLIST_ENTRY BcbLists;
    ULONG SizeOfVacbs;

    DPRINT("CcGetBcbListHead: %p, [%I64X], %X\n", SharedMap, FileOffset, Flag3);

    if (SharedMap->SectionSize.QuadPart <= 0x200000)
        return &SharedMap->BcbList;

    if (!(SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE))
        return &SharedMap->BcbList;

    if (SharedMap->SectionSize.QuadPart > CACHE_OVERALL_SIZE)
        return CcGetBcbListHeadLargeOffset(SharedMap, FileOffset, Flag3);

    if (SharedMap->SectionSize.QuadPart <= FileOffset)
        return &SharedMap->BcbList;

    SizeOfVacbs = ((SharedMap->SectionSize.LowPart / VACB_MAPPING_GRANULARITY) * sizeof(PVACB));

    BcbLists = Add2Ptr(SharedMap->Vacbs, SizeOfVacbs);

    return &BcbLists[FileOffset / BCB_MAPPING_GRANULARITY];
}

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
    PLIST_ENTRY BcbEntry;
    ULONG NewSize;
    ULONG AllocSize;
    BOOLEAN IsExtended = FALSE;
    BOOLEAN IsBcbList = FALSE;

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

    AllocSize = NewSize;

    if (NewSize == (CC_DEFAULT_NUMBER_OF_VACBS * sizeof(PVACB)))
    {
        NewVacbs = SharedMap->InitialVacbs;
    }
    else
    {
        if (NewSize <= 0x200)
        {
            if ((SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE) && AllocationSize.QuadPart > 0x200000)
            {
                /* Add size for BcbLists */
                AllocSize += ((AllocSize + (sizeof(LIST_ENTRY) - 1)) & ~(sizeof(LIST_ENTRY) - 1));
                IsBcbList = TRUE;
            }

            if (NewSize == 0x200)
            {
                AllocSize += sizeof(LIST_ENTRY);
                IsExtended = TRUE;
            }
        }
        else
        {
            IsExtended = TRUE;
            DPRINT1("CcCreateVacbArray: FIXME! NewSize %X\n", NewSize);
            ASSERT(FALSE);
        }

        NewVacbs = ExAllocatePoolWithTag(NonPagedPool, AllocSize, 'pVcC');
        if (!NewVacbs)
        {
            DPRINT1("CcCreateVacbArray: STATUS_INSUFFICIENT_RESOURCES\n");
            SharedMap->Status = STATUS_INSUFFICIENT_RESOURCES;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    RtlZeroMemory(NewVacbs, NewSize);

    if (IsExtended)
    {
        AllocSize -= sizeof(LIST_ENTRY);
        RtlZeroMemory(Add2Ptr(NewVacbs, AllocSize), sizeof(LIST_ENTRY));
    }

    if (IsBcbList)
    {
        DPRINT1("CcCreateVacbArray: NewSize %X, AllocSize %X\n", NewSize, AllocSize);

        for (BcbEntry = Add2Ptr(NewVacbs, NewSize);
             BcbEntry < (PLIST_ENTRY)Add2Ptr(NewVacbs, AllocSize);
             BcbEntry++)
        {
            InsertHeadList(&SharedMap->BcbList, BcbEntry);
        }
    }

    SharedMap->SectionSize.QuadPart = AllocationSize.QuadPart;
    SharedMap->Vacbs = NewVacbs;

    return STATUS_SUCCESS;
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

VOID
NTAPI
CcUnmapVacb(
    _In_ PVACB Vacb,
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ BOOLEAN FrontOfList)
{
    ASSERT(SharedMap != NULL);
    ASSERT(Vacb->BaseAddress != NULL);

    DPRINT("CcUnmapVacb: Vacb %p, SharedMap %p, FrontOfList %X\n", Vacb, SharedMap, FrontOfList);

    if (FrontOfList && (SharedMap->Flags & SHARE_FL_SEQUENTIAL_ONLY))
        FrontOfList = TRUE;
    else
        FrontOfList = FALSE;

    MmUnmapViewInSystemCache(Vacb->BaseAddress, SharedMap->Section, FrontOfList);

    Vacb->BaseAddress = NULL;
}

BOOLEAN
NTAPI
CcUnmapVacbArray(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN FrontOfList)
{
    KLOCK_QUEUE_HANDLE LockHandle;
    LARGE_INTEGER Start;
    LARGE_INTEGER End;
    PVACB Vacb;
    BOOLEAN LockMode = FALSE;

    DPRINT("CcUnmapVacbArray: %p, %I64X, %X, %X\n", SharedMap, (FileOffset ? FileOffset->QuadPart : 0), Length, FrontOfList);

    if (!SharedMap->Vacbs)
        return TRUE;

    if (FileOffset)
    {
        Start.QuadPart = FileOffset->QuadPart & ~(VACB_MAPPING_GRANULARITY - 1);

        if (Length)
            End.QuadPart = (FileOffset->QuadPart + Length);
        else
            End.QuadPart = SharedMap->SectionSize.QuadPart;
    }
    else
    {
        Start.QuadPart = 0;
        End.QuadPart = SharedMap->SectionSize.QuadPart;
    }

    if (SharedMap->SectionSize.QuadPart > (2 * _1MB) &&
        (SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE))
    {
        LockMode = TRUE;
    }

    ExAcquirePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);

    if (LockMode)
    {
        KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);
        KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
    }
    else
    {
        LockHandle.OldIrql = KeAcquireQueuedSpinLock(LockQueueVacbLock);
    }

    while (Start.QuadPart < End.QuadPart)
    {
        DPRINT("CcUnmapVacbArray: Start %I64X, End %I64X\n", Start.QuadPart, End.QuadPart);

        if (Start.QuadPart < SharedMap->SectionSize.QuadPart)
        {
            if (SharedMap->SectionSize.QuadPart <= CACHE_OVERALL_SIZE)
            {
                Vacb = SharedMap->Vacbs[Start.LowPart / VACB_MAPPING_GRANULARITY];
            }
            else
            {
                DPRINT("CcUnmapVacbArray: FIXME CcGetVacbLargeOffset()\n");
                ASSERT(FALSE);Vacb = NULL;
            }

            if (Vacb)
            {
                if (Vacb->Overlay.ActiveCount)
                {
                    if (LockMode)
                    {
                        KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
                        KeReleaseInStackQueuedSpinLock(&LockHandle);
                    }
                    else
                    {
                        KeReleaseQueuedSpinLock(LockQueueVacbLock, LockHandle.OldIrql);
                    }

                    ExReleasePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);
                    return FALSE;
                }

                SetVacb(SharedMap, Start, NULL);

                Vacb->Overlay.ActiveCount++;
                Vacb->SharedCacheMap = NULL;

                if (!LockMode)
                {
                    KeReleaseQueuedSpinLock(LockQueueVacbLock, LockHandle.OldIrql);
                }
                else
                {
                    KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
                    KeReleaseInStackQueuedSpinLock(&LockHandle);
                }

                CcUnmapVacb(Vacb, SharedMap, FrontOfList);

                if (LockMode)
                {
                    KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);
                    KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
                }
                else
                {
                    LockHandle.OldIrql = KeAcquireQueuedSpinLock(LockQueueVacbLock);
                }

                Vacb->Overlay.ActiveCount--;

                RemoveEntryList(&Vacb->LruList);
                InsertHeadList(&CcVacbFreeList, &Vacb->LruList);
            }
            else
            {
                DPRINT("CcUnmapVacbArray: Vacb == NULL\n");
            }
        }

        Start.QuadPart += VACB_MAPPING_GRANULARITY;
    }

    if (LockMode)
    {
        KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
        KeReleaseInStackQueuedSpinLock(&LockHandle);
    }
    else
    {
        KeReleaseQueuedSpinLock(LockQueueVacbLock, LockHandle.OldIrql);
    }

    ExReleasePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);

    DPRINT("CcUnmapVacbArray: FIXME CcDrainVacbLevelZone()\n");

    return TRUE;
}

PVACB
NTAPI
CcGetVacbMiss(
     _In_ PSHARED_CACHE_MAP SharedMap,
     _In_ LARGE_INTEGER FileOffset,
     _In_ PKLOCK_QUEUE_HANDLE LockHandle,
     _In_ BOOLEAN LockMode)
{
    PVACB Vacb;
    PVACB OutVacb;
    LARGE_INTEGER ViewSize;
    LARGE_INTEGER SectionOffset;
    NTSTATUS Status;

    DPRINT("CcGetVacbMiss: %p, %I64X, %X\n", SharedMap, FileOffset.QuadPart, LockMode);

    SectionOffset = FileOffset;
    SectionOffset.LowPart -= (FileOffset.LowPart & (VACB_MAPPING_GRANULARITY - 1));

    if (!(SharedMap->Flags & SHARE_FL_RANDOM_ACCESS) &&
        !(SectionOffset.LowPart & (0x80000 - 1)) &&
        SectionOffset.QuadPart >= CC_VACBS_DEFAULT_MAPPING_SIZE)
    {
        if (LockMode)
        {
            KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
            KeReleaseInStackQueuedSpinLock(LockHandle);
        }
        else
        {
            KeReleaseQueuedSpinLock(LockQueueVacbLock, LockHandle->OldIrql);
        }

        ExReleasePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);
        
        ViewSize.QuadPart = (SectionOffset.QuadPart - CC_VACBS_DEFAULT_MAPPING_SIZE);
        CcUnmapVacbArray(SharedMap, &ViewSize, CC_VACBS_DEFAULT_MAPPING_SIZE, TRUE);

        ExAcquirePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);

        if (LockMode)
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

    if (LockMode)
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
            if (LockMode)
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
    BOOLEAN LockMode = FALSE;

    DPRINT("CcGetVirtualAddress: SharedMap %p, Offset %I64X\n", SharedMap, FileOffset.QuadPart);

    /* Calculate the offset in VACB */
    VacbOffset = (FileOffset.LowPart & (VACB_MAPPING_GRANULARITY - 1));

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    /* Lock */
    ExAcquirePushLockShared((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);

    if (SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE)
    {
        LockMode = TRUE;
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
        Vacb = CcGetVacbMiss(SharedMap, FileOffset, &LockHandle, LockMode);
    }

    /* Updating lists */
    RemoveEntryList(&Vacb->LruList);
    InsertTailList(&CcVacbLru, &Vacb->LruList);

    /* Unlock */
    if (!LockMode)
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

PVOID
NTAPI
CcGetVirtualAddressIfMapped(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LONGLONG FileOffset,
    _Out_ PVACB* OutVacb,
    _Out_ ULONG* OutLength)
{
    PVOID Address = NULL;
    PVACB Vacb;
    ULONG VacbOffset;
    KIRQL OldIrql;

    DPRINT("CcGetVirtualAddressIfMapped: SharedMap %p, Offset %I64X\n", SharedMap, FileOffset);

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    /* Calculate the offset in VACB */
    VacbOffset = (FileOffset & (VACB_MAPPING_GRANULARITY - 1));

    *OutLength = (VACB_MAPPING_GRANULARITY - VacbOffset);

    /* Lock */
    ExAcquirePushLockExclusive((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);
    OldIrql = KeAcquireQueuedSpinLock(LockQueueVacbLock);

    ASSERT(FileOffset <= SharedMap->SectionSize.QuadPart);

    /* Get pointer to Vacb */
    if ((SharedMap->SectionSize.QuadPart <= CACHE_OVERALL_SIZE))
    {
        /* Size of file < 32 MB*/
        Vacb = SharedMap->Vacbs[(ULONG)FileOffset >> VACB_OFFSET_SHIFT];
    }
    else
    {
        /* This file is large (more than 32 MB) */
        DPRINT1("CcGetVirtualAddress: FIXME CcGetVacbLargeOffset\n");
        ASSERT(FALSE); Vacb = NULL;
    }

    *OutVacb = Vacb;

    if (Vacb)
    {
        /* Increment counters */
        if (!(Vacb->Overlay.ActiveCount))
            SharedMap->VacbActiveCount++;

        Vacb->Overlay.ActiveCount++;

        /* Updating lists */
        RemoveEntryList(&Vacb->LruList);
        InsertTailList(&CcVacbLru, &Vacb->LruList);

        /* Add an offset to the base */
        Address = Add2Ptr(Vacb->BaseAddress, VacbOffset);
    }

    /* Unlock */
    KeReleaseQueuedSpinLock(LockQueueVacbLock, OldIrql);
    ExReleasePushLockExclusive((PEX_PUSH_LOCK)&SharedMap->VacbPushLock);

    /* Return the virtual address */
    return Address;
}

VOID
NTAPI
CcFreeVirtualAddress(
    _In_ PVACB Vacb)
{
    PSHARED_CACHE_MAP SharedMap;
    KIRQL OldIrql;

    DPRINT("CcFreeVirtualAddress: Vacb %p\n", Vacb);

    OldIrql = KeAcquireQueuedSpinLock(LockQueueVacbLock);

    ASSERT((Vacb->Overlay.ActiveCount) != 0);
    Vacb->Overlay.ActiveCount--;

    if (Vacb->Overlay.ActiveCount)
        goto Finish;

    SharedMap = Vacb->SharedCacheMap;
    if (!SharedMap)
    {
        ASSERT(Vacb->BaseAddress == NULL);

        RemoveEntryList(&Vacb->LruList);
        InsertHeadList(&CcVacbFreeList, &Vacb->LruList);

        KeReleaseQueuedSpinLock(LockQueueVacbLock, OldIrql);
        return;
    }

    ASSERT((SharedMap->VacbActiveCount) != 0);
    SharedMap->VacbActiveCount--;

    if (SharedMap->WaitOnActiveCount)
        KeSetEvent(SharedMap->WaitOnActiveCount, 0, FALSE);

Finish:

    RemoveEntryList(&Vacb->LruList);
    InsertTailList(&CcVacbLru, &Vacb->LruList);

    KeReleaseQueuedSpinLock(LockQueueVacbLock, OldIrql);
    return;
}

VOID
NTAPI
CcGetActiveVacb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _Out_ PVACB* OutVacb,
    _Out_ ULONG* OutActivePage,
    _Out_ BOOLEAN* OutIsVacbLocked)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&SharedMap->ActiveVacbSpinLock, &OldIrql);

    *OutVacb = SharedMap->ActiveVacb;
    DPRINT("CcGetActiveVacb: ActiveVacb %p\n", SharedMap->ActiveVacb);

    if (*OutVacb)
    {
        SharedMap->ActiveVacb = NULL;

        *OutActivePage = SharedMap->ActivePage;
        *OutIsVacbLocked = ((SharedMap->Flags & SHARE_FL_VACB_LOCKED) == SHARE_FL_VACB_LOCKED);
    }

    KeReleaseSpinLock(&SharedMap->ActiveVacbSpinLock, OldIrql);
}

VOID
NTAPI
CcSetActiveVacb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _Inout_ PVACB* OutVacb,
    _In_ ULONG ActivePage,
    _In_ BOOLEAN IsVacbLocked)
{
    KIRQL OldIrql;

    DPRINT("CcSetActiveVacb: %p, %p, %X, %X\n", SharedMap, *OutVacb, ActivePage, IsVacbLocked);

    if (IsVacbLocked)
    {
        OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
        KeAcquireSpinLockAtDpcLevel(&SharedMap->ActiveVacbSpinLock);
    }
    else
    {
        KeAcquireSpinLock(&SharedMap->ActiveVacbSpinLock, &OldIrql);
    }

    DPRINT("CcSetActiveVacb: ActiveVacb %p\n", SharedMap->ActiveVacb);

    if (SharedMap->ActiveVacb)
        goto Exit;

    if (IsVacbLocked == ((SharedMap->Flags & SHARE_FL_VACB_LOCKED) != 0))
    {
        SharedMap->ActiveVacb = *OutVacb;
        SharedMap->ActivePage = ActivePage;

        *OutVacb = NULL;
        goto Exit;
    }

    if (!IsVacbLocked)
       goto Exit;

    SharedMap->ActiveVacb = *OutVacb;
    SharedMap->ActivePage = ActivePage;

    *OutVacb = NULL;

    SharedMap->Flags |= SHARE_FL_VACB_LOCKED;

    CcTotalDirtyPages++;
    SharedMap->DirtyPages++;

    if (SharedMap->DirtyPages != 1)
       goto Exit;

    RemoveEntryList(&SharedMap->SharedCacheMapLinks);
    InsertTailList(&CcDirtySharedCacheMapList.SharedCacheMapLinks, &SharedMap->SharedCacheMapLinks);

    if (!LazyWriter.ScanActive)
    {
        LazyWriter.ScanActive = 1;

        DPRINT1("CcSetActiveVacb: FIXME LazyWriter\n");
        ASSERT(FALSE);

        return;
    }

Exit:

    if (IsVacbLocked)
    {
        KeReleaseSpinLockFromDpcLevel(&SharedMap->ActiveVacbSpinLock);
        KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
    }
    else
    {
        KeReleaseSpinLock(&SharedMap->ActiveVacbSpinLock, OldIrql);
    }

    if (*OutVacb)
        CcFreeActiveVacb(SharedMap, *OutVacb, ActivePage, IsVacbLocked);
}

VOID
NTAPI
CcFreeActiveVacb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PVACB Vacb,
    _In_ ULONG ActivePage,
    _In_ BOOLEAN IsVacbLocked)
{
    PVOID NeedToZero;
    PVACB NeedToZeroVacb;
    LARGE_INTEGER Offset;
    ULONG Size;
    KIRQL OldIrql;

    DPRINT("CcFreeActiveVacb: %p, %X, %X, %X\n", SharedMap, Vacb, ActivePage, IsVacbLocked);

    if (SharedMap->NeedToZero)
    {
        KeAcquireSpinLock(&SharedMap->ActiveVacbSpinLock, &OldIrql);
        NeedToZero = SharedMap->NeedToZero;

        if (NeedToZero)
        {
            Size = (PAGE_SIZE - ((((ULONG_PTR)NeedToZero - 1) & (PAGE_SIZE - 1)) + 1));
            RtlZeroMemory(NeedToZero, Size);

            NeedToZeroVacb = SharedMap->NeedToZeroVacb;
            ASSERT(NeedToZeroVacb != NULL);

            SharedMap->NeedToZero = NULL;
        }
        else
        {
            NeedToZeroVacb = Vacb;
        }

        KeReleaseSpinLock(&SharedMap->ActiveVacbSpinLock, OldIrql);

        if (NeedToZero)
        {
            DPRINT("CcFreeActiveVacb: FIXME MmUnlockCachedPage\n");
            ASSERT(FALSE);
        }
    }

    if (!Vacb)
        return;

    if (!IsVacbLocked)
    {
        CcFreeVirtualAddress(Vacb);
        return;
    }

    Offset.QuadPart = (ActivePage * PAGE_SIZE);
    CcSetDirtyInMask(SharedMap, &Offset, PAGE_SIZE);

    OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
    KeAcquireSpinLockAtDpcLevel(&SharedMap->ActiveVacbSpinLock);

    if (!SharedMap->ActiveVacb && !(SharedMap->Flags & SHARE_FL_VACB_LOCKED))
    {
        SharedMap->Flags = (SharedMap->Flags & ~SHARE_FL_VACB_LOCKED);

        CcTotalDirtyPages--;
        SharedMap->DirtyPages--;
    }

    KeReleaseSpinLockFromDpcLevel(&SharedMap->ActiveVacbSpinLock);
    KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);

    CcFreeVirtualAddress(Vacb);
}

NTSTATUS
NTAPI
CcExtendVacbArray(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LARGE_INTEGER AllocationSize)
{
    KLOCK_QUEUE_HANDLE LockHandle;
    PVACB* OldVacbs;
    PVACB* NewVacbs;
    PLIST_ENTRY HeadList;
    PLIST_ENTRY OldEntry;
    PLIST_ENTRY BcbEntry;
    LARGE_INTEGER SectionNewSize;
    LARGE_INTEGER Size;
    ULONG AllocSize;
    ULONG NewVacbsSize;
    ULONG OldVacbsSize;
    BOOLEAN IsExtendSection = FALSE;
    BOOLEAN IsBcbList = FALSE;

    DPRINT1("CcExtendVacbArray: SharedMap %X, AllocationSize %I64X\n", SharedMap, AllocationSize.QuadPart);

    if (AllocationSize.HighPart & ~(PAGE_SIZE - 1))
    {
        DPRINT1("CcExtendVacbArray: STATUS_SECTION_TOO_BIG\n");
        return STATUS_SECTION_TOO_BIG;
    }

    if ((SharedMap->Flags & SHARE_FL_MODIFIED_NO_WRITE) && AllocationSize.QuadPart > 0x200000)
        IsBcbList = TRUE;

    DPRINT1("CcExtendVacbArray: SectionSize %I64X, IsBcbList %X\n", SharedMap->SectionSize.QuadPart, IsBcbList);

    if (AllocationSize.QuadPart <= SharedMap->SectionSize.QuadPart)
        return STATUS_SUCCESS;

    if (SharedMap->SectionSize.QuadPart < CACHE_OVERALL_SIZE)
    {
        if (AllocationSize.QuadPart >= CACHE_OVERALL_SIZE)
        {
            SectionNewSize.QuadPart = CACHE_OVERALL_SIZE;
            IsExtendSection = TRUE;
        }
        else
        {
            SectionNewSize.QuadPart = AllocationSize.QuadPart;
        }

        if (SectionNewSize.HighPart)
            NewVacbsSize = 0xFFFFFFFF;
        else if (SectionNewSize.LowPart <= CC_VACBS_DEFAULT_MAPPING_SIZE)
            NewVacbsSize = (CC_DEFAULT_NUMBER_OF_VACBS * sizeof(PVACB));
        else
            NewVacbsSize = ((SectionNewSize.LowPart / VACB_MAPPING_GRANULARITY) * sizeof(PVACB));

        if (SharedMap->SectionSize.HighPart)
            OldVacbsSize = 0xFFFFFFFF;
        else if (SharedMap->SectionSize.LowPart <= CC_VACBS_DEFAULT_MAPPING_SIZE)
            OldVacbsSize = (CC_DEFAULT_NUMBER_OF_VACBS * sizeof(PVACB));
        else
            OldVacbsSize = ((SharedMap->SectionSize.LowPart / VACB_MAPPING_GRANULARITY) * sizeof(PVACB));

        if (NewVacbsSize > OldVacbsSize)
        {
            KIRQL OldIrql;

            AllocSize = NewVacbsSize;

            if (IsBcbList)
            {
                /* Add size for BcbLists */
                AllocSize += ((AllocSize + (sizeof(LIST_ENTRY) - 1)) & ~(sizeof(LIST_ENTRY) - 1));
                DPRINT1("CcExtendVacbArray: NewVacbsSize %X, AllocSize %X\n", NewVacbsSize, AllocSize);
            }

            if (IsExtendSection)
                AllocSize += sizeof(LIST_ENTRY);

            NewVacbs = ExAllocatePoolWithTag(NonPagedPool, AllocSize, 'pVcC');
            if (!NewVacbs)
            {
                DPRINT1("CcExtendVacbArray: STATUS_INSUFFICIENT_RESOURCES\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            if (IsBcbList)
            {
                KeAcquireInStackQueuedSpinLock(&SharedMap->BcbSpinLock, &LockHandle);
                KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
            }
            else
            {
                OldIrql = KeAcquireQueuedSpinLock(LockQueueVacbLock);
            }

            if (SharedMap->Vacbs)
                memcpy(NewVacbs, SharedMap->Vacbs, OldVacbsSize);
            else
                OldVacbsSize = 0;

            RtlZeroMemory(Add2Ptr(NewVacbs, OldVacbsSize), (NewVacbsSize - OldVacbsSize));

            if (IsExtendSection)
            {
                AllocSize -= sizeof(LIST_ENTRY);
                RtlZeroMemory((PVOID)((ULONG_PTR)NewVacbs + AllocSize), sizeof(LIST_ENTRY));
            }

            if (IsBcbList)
            {
                Size.QuadPart = 0;
                BcbEntry = Add2Ptr(NewVacbs, NewVacbsSize);

                if (SharedMap->SectionSize.QuadPart > 0x200000 && SharedMap->Vacbs)
                {
                    OldEntry = Add2Ptr(SharedMap->Vacbs, OldVacbsSize);

                    while (Size.QuadPart < SharedMap->SectionSize.QuadPart)
                    {
                        HeadList = OldEntry->Flink;

                        RemoveEntryList(OldEntry);
                        InsertTailList(HeadList, BcbEntry);

                        Size.QuadPart += BCB_MAPPING_GRANULARITY;
                        OldEntry++;
                        BcbEntry++;
                    }
                }
                else
                {
                    PCC_BCB Bcb;

                    for (HeadList = SharedMap->BcbList.Blink;
                         HeadList != &SharedMap->BcbList;
                         HeadList = HeadList->Blink)
                    {
                        while (TRUE)
                        {
                            Bcb = CONTAINING_RECORD(HeadList, CC_BCB, Link);

                            if (Size.QuadPart > Bcb->FileOffset.QuadPart)
                                break;

                            InsertHeadList(HeadList, BcbEntry);

                            Size.QuadPart += BCB_MAPPING_GRANULARITY;
                            BcbEntry++;
                        }
                    }
                }

                while (Size.QuadPart < SectionNewSize.QuadPart)
                {
                    InsertHeadList(&SharedMap->BcbList, BcbEntry);
                    Size.QuadPart += BCB_MAPPING_GRANULARITY;
                    BcbEntry++;
                }
            }

            OldVacbs = SharedMap->Vacbs;
            SharedMap->Vacbs = NewVacbs;
            SharedMap->SectionSize.QuadPart = SectionNewSize.QuadPart;

            if (IsBcbList)
            {
                KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueVacbLock]);
                KeReleaseInStackQueuedSpinLock(&LockHandle);
            }
            else
            {
                KeReleaseQueuedSpinLock(LockQueueVacbLock, OldIrql);
            }

            if (OldVacbs != SharedMap->InitialVacbs && OldVacbs)
                ExFreePool(OldVacbs);
        }

        SharedMap->SectionSize.QuadPart = SectionNewSize.QuadPart;
    }

    if (AllocationSize.QuadPart <= SharedMap->SectionSize.QuadPart)
        return STATUS_SUCCESS;

    DPRINT1("CcExtendVacbArray: FIXME\n");
    ASSERT(FALSE);

//Exit:

    SharedMap->SectionSize.QuadPart = AllocationSize.QuadPart;

    return STATUS_SUCCESS;
}

/* EOF */
