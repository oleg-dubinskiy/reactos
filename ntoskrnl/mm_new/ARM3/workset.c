
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

#if (_MI_PAGING_LEVELS == 2)
  ULONG MiMaximumWorkingSet = ((ULONG_PTR)MI_USER_PROBE_ADDRESS / PAGE_SIZE);
#else
 #error FIXME
#endif

SIZE_T MmMinimumWorkingSetSize = 0x14;
SIZE_T MmMaximumWorkingSetSize;
SIZE_T MmPagesAboveWsMinimum;

ULONG MmPagedPoolPage;
ULONG MmSystemCachePage;
ULONG MmSystemCodePage;
ULONG MmSystemDriverPage;

extern PVOID MmHyperSpaceEnd;
extern LIST_ENTRY MmWorkingSetExpansionHead;
extern ULONG MmTransitionSharedPages;
extern ULONG MmTransitionSharedPagesPeak;
extern PVOID MmSystemCacheStart;
extern PVOID MmSystemCacheEnd;
extern PMMWSL MmSystemCacheWorkingSetList;
extern PMMWSLE MmSystemCacheWsle;
extern PMM_SESSION_SPACE MmSessionSpace;
extern PVOID MmPagedPoolStart;
extern PVOID MmPagedPoolEnd;
extern PVOID MmSpecialPoolStart;
extern PVOID MmSpecialPoolEnd;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
MiDoReplacement(
    _In_ PMMSUPPORT WorkSet,
    _In_ ULONG InFlags)
{
    PMMWSL WsList;
    ULONG TrimmedCount = 0;
    ULONG Growth = 1;
    ULONG flags = (InFlags & ~2);
    KIRQL OldIrql;

    DPRINT("MiDoReplacement: WorkSet %p, InFlags %X\n", WorkSet, InFlags);

    if (WorkSet->WorkingSetSize < WorkSet->MinimumWorkingSetSize)
    {
        goto Exit;
    }

    WsList = WorkSet->VmWorkingSetList;

    while (WorkSet->Flags.ForceTrim && InFlags != 2)
    {
        DPRINT1("MiDoReplacement: FIXME\n");
        ASSERT(FALSE);
    }

    ASSERT(WorkSet->WorkingSetSize <= (WsList->LastInitializedWsle + 1));

    if (WorkSet->Flags.MaximumWorkingSetHard &&
        WorkSet->WorkingSetSize >= WorkSet->MaximumWorkingSetSize)
    {
        DPRINT1("MiDoReplacement: FIXME MiReplaceWorkingSetEntry()\n");
        ASSERT(FALSE);
        return;
    }

    if (flags != 1)
    {
        if (MmAvailablePages >= 10000)
            goto Exit;

        if (MmAvailablePages)
        {
            if (WorkSet->GrowthSinceLastEstimate <= (((0x1E * MmAvailablePages * MmAvailablePages) / 0x1000) + 1))
                goto Exit;

            if (PsGetCurrentThread()->MemoryMaker)
                goto Exit;
        }
    }
   
    //MiReplacing = 1;

    if (flags != 1 && PsGetCurrentThread()->MemoryMaker)
    {
        if (TrimmedCount > 1)
        {
            OldIrql = MiAcquireExpansionLock();
            WorkSet->Flags.ForceTrim = 1;
            MiReleaseExpansionLock(OldIrql);
        }

        goto Exit;
    }

    DPRINT1("MiDoReplacement: FIXME MiReplaceWorkingSetEntry()\n");
    ASSERT(FALSE);

    Growth = 0;

Exit:

    WorkSet->GrowthSinceLastEstimate += Growth;
}

BOOLEAN
NTAPI
MiAddWorkingSetPage(
    _In_ PMMSUPPORT WorkSet)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

VOID
FASTCALL 
MiInsertWsleHash(
    _In_ ULONG WsIndex,
    _In_ PMMSUPPORT WorkSet)
{
    DPRINT("MiInsertWsleHash: WsIndex %p, WorkSet %p\n", WsIndex, WorkSet);
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MiRemoveWsleFromFreeList(
    _In_ ULONG WsIndex,
    _In_ PMMWSLE Wsle,
    _In_ PMMWSL WsList)
{
    DPRINT1("MiRemoveWsleFromFreeList: %X, %p, %p\n", WsIndex, Wsle, WsList);
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MiRepointWsleHashIndex(
    MMWSLE Wsle,
    PMMWSL WsList,
    ULONG HashIndex)
{
    DPRINT1("MiRepointWsleHashIndex: %X, %p, %p\n", Wsle.u1.Long, WsList, HashIndex);
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MiSwapWslEntries(
    _In_ ULONG WsIndex1,
    _In_ ULONG WsIndex2,
    _In_ PMMSUPPORT WorkSet,
    _In_ BOOLEAN Param4)
{
    PVOID Address;
    PMMPTE Pte;
    PMMPFN Pfn;
    PMMWSL WsList;
    PMMWSLE Wsle;
    PMMWSLE_HASH Table;
    MMWSLE Wsle1;
    MMWSLE Wsle2;
    ULONG Hash;
    NTSTATUS Status;

    DPRINT("MiSwapWslEntries: %X, %X, %p, %X\n", WsIndex1, WsIndex2, WorkSet, Param4);

    WsList = WorkSet->VmWorkingSetList;
    Wsle = WsList->Wsle;
    Table = WsList->HashTable;

    Wsle1 = Wsle[WsIndex1];
    ASSERT(Wsle1.u1.e1.Valid != 0);

    Wsle2 = Wsle[WsIndex2];
    if (!Wsle2.u1.e1.Valid)
    {
        MiRemoveWsleFromFreeList(WsIndex2, Wsle, WsList);

        Wsle[WsIndex2].u1.Long = Wsle1.u1.Long;

        Pte = MiAddressToPte(Wsle1.u1.VirtualAddress);
        if (!(Pte->u.Hard.Valid))
        {
            Status = MiCheckPdeForPagedPool(Wsle1.u1.VirtualAddress);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("KeBugCheckEx()\n");
                ASSERT(FALSE);
            }

            ASSERT(Pte->u.Hard.Valid == 1);
        }

        if (Wsle1.u1.e1.Direct)
        {
            Pfn = MI_PFN_ELEMENT(Pte->u.Hard.PageFrameNumber);
            ASSERT(Pfn->u1.WsIndex == WsIndex1);
            Pfn->u1.WsIndex = WsIndex2;
        }
        else if (Table)
        {
            MiRepointWsleHashIndex(Wsle1, WsList, WsIndex2);
        }

        ASSERT((WsList->FirstFree <= WsList->LastInitializedWsle) || (WsList->FirstFree == WSLE_NULL_INDEX));

        Wsle[WsIndex1].u1.Long = (WsList->FirstFree << MM_FREE_WSLE_SHIFT);
        WsList->FirstFree = WsIndex1;

        ASSERT((WsList->FirstFree <= WsList->LastInitializedWsle) || (WsList->FirstFree == WSLE_NULL_INDEX));

        return;
    }

    Wsle[WsIndex1].u1.Long = Wsle2.u1.Long;

    Pte = MiAddressToPte(Wsle2.u1.VirtualAddress);
    if (!Pte->u.Hard.Valid)
    {
        Status = MiCheckPdeForPagedPool(Wsle2.u1.VirtualAddress);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("KeBugCheckEx()\n");
            ASSERT(FALSE);
        }

        ASSERT(Pte->u.Hard.Valid == 1);
    }

    if (Wsle2.u1.e1.Direct)
    {
        Pfn = MI_PFN_ELEMENT(Pte->u.Hard.PageFrameNumber);
        ASSERT(Pfn->u1.WsIndex == WsIndex2);
        Pfn->u1.WsIndex = WsIndex1;
    }
    else if (Table)
    {
        if (Param4)
        {
            Address = (PVOID)(Wsle2.u1.Long & (0xFFFFF000 | 0x1));

            for (Hash = 0; Hash < WsList->HashTableSize; Hash++)
            {
                ASSERT(Table[Hash].Key != Address);
            }
        }
        else
        {
            MiRepointWsleHashIndex(Wsle2, WsList, WsIndex1);
        }
    }

    Wsle[WsIndex2].u1.Long = Wsle1.u1.Long;

    Pte = MiAddressToPte(Wsle1.u1.VirtualAddress);
    if (!Pte->u.Hard.Valid)
    {
        Status = MiCheckPdeForPagedPool(Wsle1.u1.VirtualAddress);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("KeBugCheckEx()\n");
            ASSERT(FALSE);
        }

        ASSERT(Pte->u.Hard.Valid == 1);
    }

    if (Wsle1.u1.e1.Direct)
    {
        Pfn = MI_PFN_ELEMENT(Pte->u.Hard.PageFrameNumber);
        ASSERT(Pfn->u1.WsIndex == WsIndex1);
        Pfn->u1.WsIndex = WsIndex2;
    }
    else if (Table)
    {
        MiRepointWsleHashIndex(Wsle1, WsList, WsIndex2);
    }
}

VOID
NTAPI
MiUpdateWsle(
    _Out_ ULONG* OutWsIndex,
    _In_ PVOID Address,
    _In_ PMMSUPPORT WorkSet,
    _In_ PMMPFN Pfn,
    _In_ MMWSLE InWsle)
{
    PMMWSL WsList;
    PMMWSLE Wsle;
    MMWSLE OldWsle;
    ULONG WsIndex;
    ULONG PfnIndex;
    ULONG FreeIndex = 0;

    DPRINT("MiUpdateWsle: %p, %p, %p, %X, %X\n", OutWsIndex, Address, WorkSet, Pfn, InWsle);

    if (WorkSet == &MmSystemCacheWs)
    {
        ASSERT((PsGetCurrentThread()->OwnsSystemWorkingSetExclusive) ||
               (PsGetCurrentThread()->OwnsSystemWorkingSetShared));
    }
    else if (WorkSet->Flags.SessionSpace)
    {
        ASSERT((PsGetCurrentThread()->OwnsSessionWorkingSetExclusive) ||
               (PsGetCurrentThread()->OwnsSessionWorkingSetShared));
    }
    else
    {
        ASSERT((PsGetCurrentThread()->OwnsProcessWorkingSetExclusive) ||
               (PsGetCurrentThread()->OwnsProcessWorkingSetShared));
    }

    WsList = WorkSet->VmWorkingSetList;
    WsIndex = *OutWsIndex;

    //DPRINT1("MiUpdateWsle: %p, %p, %p, %X, %p, %X\n", Address, WorkSet, Pfn, InWsle, WsList, WsIndex);

    ASSERT(WsIndex >= WsList->FirstDynamic);
    Wsle = WsList->Wsle;

    if (WsList == MmSystemCacheWorkingSetList) // MM_SYSTEM_SPACE_START
    {
        ASSERT((Address < (PVOID)0xC0000000) || (Address >= (PVOID)MmSystemCacheWorkingSetList));

        if (Address >= MmPagedPoolStart && Address <= MmPagedPoolEnd)
        {
            MmPagedPoolPage++;
        }
        else if ((Address >= MmSystemCacheStart && Address <= MmSystemCacheEnd)) // || (Address >= MiSystemCacheStartExtra && Address <= MiSystemCacheEndExtra))
        {
            MmSystemCachePage++;
        }
        else if (Address <= MmSpecialPoolEnd && Address >= MmSpecialPoolStart)
        {
            MmPagedPoolPage++;
        }
        //else if (Address < MiLowestSystemPteVirtualAddress)
        //    MmSystemCodePage++;
        //else
        //    MmSystemDriverPage++;
    }
    else
    {
        ASSERT((Address < (PVOID)MmSystemCacheWorkingSetList) || (MI_IS_SESSION_ADDRESS(Address)));
    }

    if (Pfn->u1.WsIndex <= WsList->LastInitializedWsle)
    {
        ASSERT((PAGE_ALIGN(Address) != PAGE_ALIGN(Wsle[Pfn->u1.WsIndex].u1.VirtualAddress)) ||
               (Wsle[Pfn->u1.WsIndex].u1.e1.Valid == 0));
    }

    if (!Pfn->u1.WsIndex)
    {
        if (!Pfn->u3.e1.PrototypePte)
            Pfn->u1.WsIndex = WsIndex;

        if (!Pfn->u3.e1.PrototypePte || !InterlockedCompareExchange((PLONG)&Pfn->u1.WsIndex, WsIndex, 0))
        {
            Wsle[WsIndex].u1.VirtualAddress = PAGE_ALIGN(Address);
            Wsle[WsIndex].u1.Long |= InWsle.u1.Long;
            Wsle[WsIndex].u1.e1.Valid = 1;
            Wsle[WsIndex].u1.e1.Direct = 1;
            return;
        }
    }

    Wsle[WsIndex].u1.VirtualAddress = PAGE_ALIGN(Address);
    Wsle[WsIndex].u1.e1.Valid = 1;

    PfnIndex = Pfn->u1.WsIndex;

    if (PfnIndex >= WsList->LastInitializedWsle ||
        PfnIndex <= WsList->FirstDynamic ||
        PfnIndex == WsIndex)
    {
        goto Exit;
    }

    if (Wsle[PfnIndex].u1.e1.Valid)
    {
        if (Wsle[PfnIndex].u1.e1.Direct)
        {
            MiSwapWslEntries(PfnIndex, WsIndex, WorkSet, 1);
            WsIndex = PfnIndex;
        }

        goto Exit1;
    }

    ASSERT(WsList->FirstFree >= WsList->FirstDynamic);
    ASSERT(WsIndex >= WsList->FirstDynamic);

    if (WsList->FirstFree == PfnIndex)
    {
        WsList->FirstFree = WsIndex;
        OldWsle.u1.Long = Wsle[WsIndex].u1.Long;
        Wsle[WsIndex].u1.Long = Wsle[PfnIndex].u1.Long;
        Wsle[PfnIndex].u1.Long = OldWsle.u1.Long;

        WsIndex = PfnIndex;

        ASSERT(((Wsle[WsList->FirstFree].u1.Long >> MM_FREE_WSLE_SHIFT) <= WsList->LastInitializedWsle) ||
               ((Wsle[WsList->FirstFree].u1.Long >> MM_FREE_WSLE_SHIFT) == WSLE_NULL_INDEX));

        goto Exit1;
    }

    if (Wsle[PfnIndex - 1].u1.e1.Valid)
    {
        if (!Wsle[PfnIndex + 1].u1.e1.Valid && (Wsle[PfnIndex + 1].u1.Long >> MM_FREE_WSLE_SHIFT) == PfnIndex)
            FreeIndex = (PfnIndex + 1);
    }
    else
    {
        if ((Wsle[PfnIndex - 1].u1.Long >> MM_FREE_WSLE_SHIFT) == PfnIndex)
            FreeIndex = (PfnIndex - 1);
    }

    if (!FreeIndex)
        goto Exit1;

    OldWsle.u1.Long = Wsle[WsIndex].u1.Long;
    Wsle[FreeIndex].u1.Long = WsIndex << MM_FREE_WSLE_SHIFT;
    Wsle[WsIndex].u1.Long = Wsle[PfnIndex].u1.Long;
    Wsle[PfnIndex].u1.Long = OldWsle.u1.Long;

    WsIndex = PfnIndex;

    ASSERT(((Wsle[FreeIndex].u1.Long >> MM_FREE_WSLE_SHIFT) <= WsList->LastInitializedWsle) ||
           ((Wsle[FreeIndex].u1.Long >> MM_FREE_WSLE_SHIFT) == WSLE_NULL_INDEX));

Exit1:

    *OutWsIndex = WsIndex;

    if (WsIndex > WsList->LastEntry)
        WsList->LastEntry = WsIndex;

Exit:

    ASSERT(Wsle[WsIndex].u1.e1.Valid == 1);
    ASSERT(Wsle[WsIndex].u1.e1.Direct != 1);

    Wsle[WsIndex].u1.Long |= InWsle.u1.Long;
    Wsle[WsIndex].u1.e1.Hashed = 0;

    WsList->NonDirectCount++;

    if (WsIndex == Pfn->u1.WsIndex)
        return;

    if (WsList->HashTable)
        MiInsertWsleHash(WsIndex, WorkSet);
}

ULONG
NTAPI
MiAllocateWsle(
    _In_ PMMSUPPORT WorkSet,
    _In_ PMMPTE Pte,
    _In_ PMMPFN Pfn,
    _In_ MMWSLE InWsle)
{
    PMMWSL WsList;
    PMMWSLE Wsle;
    PVOID Address;
    ULONG WsIndex;

    DPRINT("MiAllocateWsle: WorkSet %p, Pte %p, Pfn %X, InWsle %X\n", WorkSet, Pte, Pfn, InWsle);

    WsList = WorkSet->VmWorkingSetList;
    ASSERT(WsList != NULL);

    Wsle = WsList->Wsle;
    ASSERT(Wsle != NULL);

    //DPRINT("MiAllocateWsle: %p, %p, %X, %X, %X\n", WsList, Wsle, WsList->FirstFree, WsList->FirstDynamic, WsList->LastInitializedWsle);

    WorkSet->PageFaultCount++;
    InterlockedIncrement(&KeGetCurrentPrcb()->MmPageFaultCount);

    if ((ULONG_PTR)Pfn & 1)
    {
        MiDoReplacement(WorkSet, 2);

        if (WsList->FirstFree == WSLE_NULL_INDEX)
        {
            return 0;
        }

        Pfn = (PMMPFN)((ULONG_PTR)Pfn & ~1);
    }
    else
    {
        MiDoReplacement(WorkSet, 0);

        if (WsList->FirstFree == WSLE_NULL_INDEX)
        {
            if (!MiAddWorkingSetPage(WorkSet))
            {
                MiDoReplacement(WorkSet, 1);

                if (WsList->FirstFree == WSLE_NULL_INDEX)
                {
                    //MiWsleFailures++;
                    return 0;
                }
            }
        }
    }

    ASSERT(WsList->FirstFree <= WsList->LastInitializedWsle);
    ASSERT(WsList->FirstFree >= WsList->FirstDynamic);

    WsIndex = WsList->FirstFree;
    WsList->FirstFree = (Wsle[WsIndex].u1.Long / 0x10);

    ASSERT((WsList->FirstFree <= WsList->LastInitializedWsle) || (WsList->FirstFree == WSLE_NULL_INDEX));
    ASSERT(WorkSet->WorkingSetSize <= (WsList->LastInitializedWsle + 1));

    WorkSet->WorkingSetSize++;

    if (WorkSet->WorkingSetSize > WorkSet->MinimumWorkingSetSize)
        InterlockedIncrementSizeT(&MmPagesAboveWsMinimum);

    if (WorkSet->PeakWorkingSetSize < WorkSet->WorkingSetSize)
        WorkSet->PeakWorkingSetSize = WorkSet->WorkingSetSize;

    if (WorkSet == &MmSystemCacheWs)
    {
        if (MmTransitionSharedPagesPeak < (WorkSet->WorkingSetSize + MmTransitionSharedPages))
            MmTransitionSharedPagesPeak = (WorkSet->WorkingSetSize + MmTransitionSharedPages);
    }

    if (WsIndex > WsList->LastEntry)
        WsList->LastEntry = WsIndex;

    ASSERT(Wsle[WsIndex].u1.e1.Valid == 0);

    Address = MiPteToAddress(Pte);

    MiUpdateWsle(&WsIndex, Address, WorkSet, Pfn, InWsle);

    if (InWsle.u1.Long)
        Wsle[WsIndex].u1.Long |= InWsle.u1.Long;

    if ((Address >= MmSystemCacheStart && Address <= MmSystemCacheEnd))
        //FIXME: || (Address >= MiSystemCacheStartExtra && Address <= MiSystemCacheEndExtra))
    {
        ASSERT(MmSystemCacheWsle[WsIndex].u1.e1.Protection == MM_ZERO_ACCESS);
    }

    return WsIndex;
}

VOID
FASTCALL 
MiReleaseWsle(
    _In_ ULONG WsIndex,
    _In_ PMMSUPPORT WorkSet)
{
    PMMWSL WsList;
    PMMWSLE Wsle;

    DPRINT("MiReleaseWsle: WsIndex %X, WorkSet %p\n", WsIndex, WorkSet);

    WsList = WorkSet->VmWorkingSetList;
    Wsle = WsList->Wsle;

    if (WorkSet == &MmSystemCacheWs)
    {
        ASSERT(PsGetCurrentThread()->OwnsSystemWorkingSetExclusive ||
               PsGetCurrentThread()->OwnsSystemWorkingSetShared);
    }
    else if (WorkSet->Flags.SessionSpace)
    {
        ASSERT(PsGetCurrentThread()->OwnsSessionWorkingSetExclusive ||
               PsGetCurrentThread()->OwnsSessionWorkingSetShared);
    }
    else
    {
        ASSERT(PsGetCurrentThread()->OwnsProcessWorkingSetExclusive ||
               PsGetCurrentThread()->OwnsProcessWorkingSetShared);
    }

    ASSERT(WsIndex <= WsList->LastInitializedWsle);

    ASSERT((WsList->FirstFree <= WsList->LastInitializedWsle) ||
           (WsList->FirstFree == WSLE_NULL_INDEX));

    Wsle[WsIndex].u1.Long = (WsList->FirstFree * 0x10);
    WsList->FirstFree = WsIndex;

    ASSERT((WsList->FirstFree <= WsList->LastInitializedWsle) ||
           (WsList->FirstFree == WSLE_NULL_INDEX));

    if (WorkSet->WorkingSetSize > WorkSet->MinimumWorkingSetSize)
        InterlockedDecrementSizeT(&MmPagesAboveWsMinimum);

    WorkSet->WorkingSetSize--;
}

ULONG
NTAPI
MiAddValidPageToWorkingSet(
    _In_ PVOID Address,
    _In_ PMMPTE Pte,
    _In_ PMMPFN Pfn,
    _In_ MMWSLE Wsle)
{
    PMMSUPPORT WorkSet;

    ASSERT(MI_IS_PAGE_TABLE_ADDRESS(Pte));
    ASSERT(Pte->u.Hard.Valid == 1);

    if ((Address < MmSessionBase || Address >= MiSessionSpaceEnd) &&
        (Address < (PVOID)MiSessionBasePte || Address >= (PVOID)MiSessionLastPte))
    {
        if (Address <= MmHighestUserAddress ||
            (Address >= (PVOID)PTE_BASE && Address <= MmHyperSpaceEnd))
        {
            WorkSet = &PsGetCurrentProcess()->Vm;
        }
        else
        {
            WorkSet = &MmSystemCacheWs;
        }
    }
    else
    {
        WorkSet = &MmSessionSpace->Vm;
    }

    return MiAllocateWsle(WorkSet, Pte, Pfn, Wsle);
}

VOID
NTAPI
MiAllowWorkingSetExpansion(
    _In_ PMMSUPPORT WorkSet)
{
    KIRQL OldIrql;

    DPRINT1("MiAllowWorkingSetExpansion: %p\n", WorkSet);

    ASSERT(WorkSet->WorkingSetExpansionLinks.Flink == NULL);
    ASSERT(WorkSet->WorkingSetExpansionLinks.Blink == NULL);

  #if defined(ONE_CPU)
    OldIrql = KeRaiseIrqlToDpcLevel();
  #else
    OldIrql = MiAcquireExpansionLock();
  #endif

    InsertTailList(&MmWorkingSetExpansionHead, &WorkSet->WorkingSetExpansionLinks);

  #if defined(ONE_CPU)
    KeLowerIrql(OldIrql);
  #else
    MiReleaseExpansionLock(OldIrql);
  #endif

}

VOID
NTAPI
MiUnlinkWorkingSet(
    _In_ PMMSUPPORT WorkSet)
{
    KEVENT Event;
    KIRQL OldIrql;

    OldIrql = MiAcquireExpansionLock();

    if (WorkSet->WorkingSetExpansionLinks.Flink == MM_WS_NOT_LISTED)
    {
        MiReleaseExpansionLock(OldIrql);
        return;
    }

    if (WorkSet->WorkingSetExpansionLinks.Flink != (PLIST_ENTRY)1)
    {
        RemoveEntryList(&WorkSet->WorkingSetExpansionLinks);
        WorkSet->WorkingSetExpansionLinks.Flink = MM_WS_NOT_LISTED;
        MiReleaseExpansionLock(OldIrql);
        return;
    }

    //WorkSet->WorkingSetExpansionLinks.Flink == 1

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    WorkSet->WorkingSetExpansionLinks.Blink = (PLIST_ENTRY)&Event;

    KeEnterCriticalRegion();
    MiReleaseExpansionLock(OldIrql);
    KeWaitForSingleObject(&Event, WrVirtualMemory, KernelMode, FALSE, NULL);
    KeLeaveCriticalRegion();

    ASSERT(WorkSet->WorkingSetExpansionLinks.Flink == MM_WS_NOT_LISTED);
}

BOOLEAN
NTAPI
MiEliminateWorkingSetEntry(
    _In_ ULONG WorkingSetIndex,
    _In_ PMMPTE Pte,
    _In_ PMMPFN Pfn,
    _In_ PMMSUPPORT WorkingSet,
    _In_ BOOLEAN Parameter5)
{
    MMPTE TempPte;
    MMPTE PreviousPte;
    ULONG PageFrameIndex;
    KIRQL OldIrql;

    DPRINT("MiEliminateWorkingSetEntry: %X, %p [%X], %p, %p, %X\n", WorkingSetIndex, Pte, *Pte, Pfn, WorkingSet, Parameter5);

    TempPte.u.Long = Pte->u.Long;
    ASSERT(TempPte.u.Hard.Valid == 1);

    PageFrameIndex = TempPte.u.Hard.PageFrameNumber;
    ASSERT(Pfn == MI_PFN_ELEMENT(PageFrameIndex));
    ASSERT(MI_IS_PFN_DELETED(Pfn) == 0);

    if (TempPte.u.Hard.Writable)
    {
        ASSERT(TempPte.u.Hard.Dirty == 1);
    }

    if (Pfn->u3.e1.PrototypePte)
    {
        DPRINT1("MiEliminateWorkingSetEntry: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        ASSERT(Pfn->u2.ShareCount == 1); // FIXME

        TempPte.u.Soft.Valid = 0;
        TempPte.u.Soft.Transition = 1;
        TempPte.u.Soft.Prototype = 0;
        TempPte.u.Soft.Protection = Pfn->OriginalPte.u.Soft.Protection;
    }

    OldIrql = MiLockPfnDb(APC_LEVEL);

    if (!Parameter5)
    {
        DPRINT1("MiEliminateWorkingSetEntry: FIXME\n");
        ASSERT(FALSE);
    }

    if (!Pfn->u3.e1.PrototypePte)
    {
        //ASSERT(Pfn->u1.WsIndex != 0);
        Pfn->u1.WsIndex = 0;
    }
    else
    {
        DPRINT1("MiEliminateWorkingSetEntry: FIXME\n");
        ASSERT(FALSE);
    }

    PreviousPte = *Pte;

    ASSERT(PreviousPte.u.Hard.Valid == 1);
    ASSERT((TempPte).u.Hard.Valid == 0);
    Pte->u.Long = TempPte.u.Long;

    DPRINT("MiEliminateWorkingSetEntry: %p [%X], [%X]\n", Pte, *Pte, PreviousPte.u.Long);

    //FIXME: KeFlushSingleTb()

    ASSERT(PreviousPte.u.Hard.Valid == 1);
    ASSERT(KeGetCurrentIrql() > APC_LEVEL);

    if (!Pfn->u3.e1.Modified && PreviousPte.u.Hard.Dirty)
    {
        ASSERT(Pfn->u3.e1.Rom == 0);
        Pfn->u3.e1.Modified = 1;

        if (!Pfn->OriginalPte.u.Soft.Prototype && !Pfn->u3.e1.WriteInProgress)
        {
            MiReleasePageFileSpace(Pfn->OriginalPte);
            Pfn->OriginalPte.u.Soft.PageFileHigh = 0;
        }
    }

    if (!Pfn->u3.e1.PrototypePte &&
        PreviousPte.u.Hard.Dirty &&
        (PsGetCurrentProcess()->Flags & 0x8000))
    {
        DPRINT1("MiEliminateWorkingSetEntry: FIXME\n");
        ASSERT(FALSE);
    }

    MiDecrementPfnShare(Pfn, PageFrameIndex);
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    return TRUE;
}

BOOLEAN
NTAPI
MiRemovePageFromWorkingSet(
    _In_ PMMPTE Pte,
    _In_ PMMPFN Pfn,
    _In_ PMMSUPPORT WorkingSet)
{
    ULONG WorkingSetIndex;

    DPRINT("MiRemovePageFromWorkingSet: %p, %p, %p\n", Pte, Pfn, WorkingSet);

    WorkingSetIndex = 0;//MiLocateWsle(..);
    MiEliminateWorkingSetEntry(WorkingSetIndex, Pte, Pfn, WorkingSet, 1);

    return FALSE;
}

NTSTATUS
NTAPI
MiEmptyWorkingSet(
    _In_ PMMSUPPORT WorkingSet,
    _In_ BOOLEAN IsParam2)
{
    if (WorkingSet->VmWorkingSetList == MmWorkingSetList &&
        PsGetCurrentProcess()->VmDeleted)
    {
        DPRINT("MiEmptyWorkingSet: STATUS_PROCESS_IS_TERMINATING. %p, %X\n", WorkingSet, IsParam2);
        return STATUS_PROCESS_IS_TERMINATING;
    }

    UNIMPLEMENTED;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmAdjustWorkingSetSizeEx(
    SIZE_T WorkingSetMinimumInBytes,
    SIZE_T WorkingSetMaximumInBytes,
    BOOLEAN IsSystemCache,
    BOOLEAN IsIncreaseOkay,
    ULONG Flags,
    BOOLEAN* OutIsAddMinSize)
{
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS Process;
    PMMSUPPORT WorkingSet;
    PMMWSL WorkingSetList;
    SIZE_T MinWorkingSetSize;
    SIZE_T MaxWorkingSetSize;
    SSIZE_T Delta;
    ULONG TrimAge;
    BOOLEAN LimitIsGood = TRUE;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("MmAdjustWorkingSetSizeEx: %IX, %IX, %X\n", WorkingSetMinimumInBytes, WorkingSetMaximumInBytes, IsSystemCache);

    *OutIsAddMinSize = FALSE;

    if (IsSystemCache)
    {
        Process = NULL;
        WorkingSet = &MmSystemCacheWs;
    }
    else
    {
        Process = (PEPROCESS)Thread->Tcb.ApcState.Process;
        WorkingSet = &Process->Vm;
    }

    /* Check for special case: empty the working set */
    if (WorkingSetMinimumInBytes == -1 && WorkingSetMaximumInBytes == -1)
        return MiEmptyWorkingSet(WorkingSet, TRUE);

    /* Assume success */
    Status = STATUS_SUCCESS;

    //MmLockPagableSectionByHandle(ExPageLockHandle);

    /* Lock the working set */
    if (IsSystemCache)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        MiLockProcessWorkingSet(Process, Thread);

        if (Process->VmDeleted)
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_PROCESS_IS_TERMINATING\n");
            Status = STATUS_PROCESS_IS_TERMINATING;
            goto Cleanup;
        }
    }

    /* Calculate the actual minimum and maximum working set size to set */
    if (WorkingSetMinimumInBytes)
        MinWorkingSetSize = (WorkingSetMinimumInBytes / PAGE_SIZE);
    else
        MinWorkingSetSize = WorkingSet->MinimumWorkingSetSize;

    if (WorkingSetMaximumInBytes)
        MaxWorkingSetSize = (WorkingSetMaximumInBytes / PAGE_SIZE);
    else
        MaxWorkingSetSize = WorkingSet->MaximumWorkingSetSize;

    /* Check if the new minimum exceeds the new maximum */
    if (MinWorkingSetSize > MaxWorkingSetSize)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT. %IX, %IX\n", MinWorkingSetSize, MaxWorkingSetSize);
        Status = STATUS_BAD_WORKING_SET_LIMIT;
        goto Cleanup;
    }

    /* Check if the new maximum exceeds the global maximum */
    if (MaxWorkingSetSize > MmMaximumWorkingSetSize)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_WORKING_SET_LIMIT_RANGE\n");
        MaxWorkingSetSize = MmMaximumWorkingSetSize;
        Status = STATUS_WORKING_SET_LIMIT_RANGE;
    }

    /* Check if the new minimum is below the global minimum */
    if (MinWorkingSetSize < MmMinimumWorkingSetSize)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_WORKING_SET_LIMIT_RANGE\n");
        MinWorkingSetSize = MmMinimumWorkingSetSize;
        Status = STATUS_WORKING_SET_LIMIT_RANGE;
    }

    /* Check if the new minimum exceeds the new maximum */
    if (MinWorkingSetSize > MmMaximumWorkingSetSize)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_WORKING_SET_LIMIT_RANGE\n");
        MinWorkingSetSize = MmMaximumWorkingSetSize;
        Status = STATUS_WORKING_SET_LIMIT_RANGE;
    }

    WorkingSetList = WorkingSet->VmWorkingSetList;

    if (MaxWorkingSetSize <= (WorkingSetList->FirstDynamic + 8))
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT\n");
        Status = STATUS_BAD_WORKING_SET_LIMIT;
        goto Cleanup;
    }

    if ((Flags & 4) ||
        (WorkingSet->Flags.MinimumWorkingSetHard && !(Flags & 8)) ||
        (Flags & 1) ||
        (WorkingSet->Flags.MaximumWorkingSetHard && !(Flags & 2)))
    {
        if (MaxWorkingSetSize <= (MinWorkingSetSize + 8) ||
            (IsSystemCache && MaxWorkingSetSize < 0x1000))
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT\n");
            Status = STATUS_BAD_WORKING_SET_LIMIT;
            goto Cleanup;
        }
    }

    /* Calculate the minimum WS size adjustment and check if we increase */
    Delta = (MinWorkingSetSize - WorkingSet->MinimumWorkingSetSize);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    if (Delta > 0)
    {
        *OutIsAddMinSize = TRUE;

        /* Is increasing ok? */
        if (!IsIncreaseOkay)
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: Privilege for WS size increase not held\n");
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            Status = STATUS_PRIVILEGE_NOT_HELD;
            goto Cleanup;
        }

        /* Check if the number of available pages is large enough */
        if ((SPFN_NUMBER)((Delta / (PAGE_SIZE / sizeof(ULONG)))) > (SPFN_NUMBER)(MmAvailablePages - 0x80))
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: Not enough available pages\n");
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        /* Check if there are enough resident available pages */
        if (Delta > (MmResidentAvailablePages - MmSystemLockPagesCount - 0x100))
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: Not enough resident pages\n");
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    /* Update resident available pages */
    if (Delta)
        InterlockedExchangeAddSizeT(&MmResidentAvailablePages, -Delta);

    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    if (MaxWorkingSetSize < WorkingSetList->LastInitializedWsle &&
        MaxWorkingSetSize < WorkingSet->WorkingSetSize)
    {
        if (WorkingSetList->FirstDynamic + 8 >= MaxWorkingSetSize)
        {
            DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT\n");
            InterlockedExchangeAddSizeT(&MmResidentAvailablePages, Delta);
            Status = STATUS_BAD_WORKING_SET_LIMIT;
            goto Cleanup;
        }

        for (TrimAge = 2; ; TrimAge--)
        {
            ASSERT(TrimAge <= 2); // MI_PASS0_TRIM_AGE

            DPRINT1("MmAdjustWorkingSetSizeEx: FIXME MiTrimWorkingSet()\n");
            ASSERT(FALSE);
            //MiTrimWorkingSet((WorkingSet->WorkingSetSize - MaxWorkingSetSize), WorkingSet, TrimAge);

            if (WorkingSet->WorkingSetSize <= MaxWorkingSetSize)
                break;

            if (!TrimAge)
            {
                DPRINT1("MmAdjustWorkingSetSizeEx: STATUS_BAD_WORKING_SET_LIMIT\n");
                LimitIsGood = FALSE;
                Status = STATUS_BAD_WORKING_SET_LIMIT;
                break;
            }
        }
    }

    if ((LONG)WorkingSet->WorkingSetSize > (LONG)WorkingSet->MinimumWorkingSetSize)
        InterlockedExchangeAddSizeT(&MmPagesAboveWsMinimum, -((LONG)WorkingSet->WorkingSetSize - (LONG)WorkingSet->MinimumWorkingSetSize));

    if ((LONG)WorkingSet->WorkingSetSize > (LONG)MinWorkingSetSize)
        InterlockedExchangeAddSizeT(&MmPagesAboveWsMinimum, ((LONG)WorkingSet->WorkingSetSize - (LONG)MinWorkingSetSize));

    if (LimitIsGood)
    {
        WorkingSet->MaximumWorkingSetSize = MaxWorkingSetSize;
        WorkingSet->MinimumWorkingSetSize = MinWorkingSetSize;

        if (Flags)
        {
            OldIrql = MiAcquireExpansionLock();

            if (Flags & 4)
                WorkingSet->Flags.MinimumWorkingSetHard = 1;
            else if (Flags & 8)
                WorkingSet->Flags.MinimumWorkingSetHard = 0;

            if (Flags & 1)
                WorkingSet->Flags.MaximumWorkingSetHard = 1;
            else if (Flags & 2)
                WorkingSet->Flags.MaximumWorkingSetHard = 0;

            MiReleaseExpansionLock(OldIrql);
        }
    }
    else
    {
        InterlockedExchangeAddSizeT(&MmResidentAvailablePages, Delta);
    }

    ASSERT((WorkingSetList->FirstFree <= WorkingSetList->LastInitializedWsle) || (WorkingSetList->FirstFree == WSLE_NULL_INDEX));

Cleanup:

    if (IsSystemCache)
    {
        DPRINT1("MmAdjustWorkingSetSizeEx: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        MiUnlockProcessWorkingSet(Process, Thread);
    }

    //MmUnlockPagableImageSection(ExPageLockHandle);
    return Status;
}

VOID
NTAPI
MiInitializeWorkingSetList(
    _In_ PEPROCESS CurrentProcess)
{
    PMMWSLE FirstWsle;
    PMMWSLE CurrentWsle;
    PMMPFN Pfn;
    ULONG WorkSetSize;
    ULONG WsleCount;
    ULONG ix;

    DPRINT1("MiInitializeWorkingSetList: %p, %p, %p, %p\n", CurrentProcess, &CurrentProcess->Vm, CurrentProcess->Vm.VmWorkingSetList, MmWorkingSetList);

    CurrentWsle = FirstWsle = Add2Ptr(MmWorkingSetList, sizeof(MMWSL));

    MmWorkingSetList->LastEntry = CurrentProcess->Vm.MinimumWorkingSetSize;
    MmWorkingSetList->Wsle = FirstWsle;
    MmWorkingSetList->VadBitMapHint = 1;
    MmWorkingSetList->NumberOfImageWaiters = 0;
    MmWorkingSetList->HashTable = NULL;
    MmWorkingSetList->HashTableSize = 0;
    MmWorkingSetList->HashTableStart = (PVOID)((PCHAR)PAGE_ALIGN(&FirstWsle[MiMaximumWorkingSet]) + PAGE_SIZE);
    MmWorkingSetList->HighestPermittedHashAddress = Add2Ptr(MmHyperSpaceEnd, 1);

    CurrentWsle->u1.VirtualAddress = (PVOID)(PDE_BASE);
    CurrentWsle->u1.e1.Valid = 1;
    CurrentWsle->u1.e1.LockedInWs = 1;
    CurrentWsle->u1.e1.Direct = 1;

    Pfn = MiGetPfnEntry(MiAddressToPte((PVOID)PDE_BASE)->u.Hard.PageFrameNumber);

    ASSERT(Pfn->u1.WsIndex == 0);
    ASSERT(Pfn->u4.PteFrame == (ULONG_PTR)MiGetPfnEntryIndex(Pfn));

    Pfn->u1.Event = (PVOID)CurrentProcess;
    Pfn->u1.WsIndex = (CurrentWsle - FirstWsle);

    CurrentWsle++;

    CurrentWsle->u1.VirtualAddress = (PVOID)(MiAddressToPte(HYPER_SPACE));
    CurrentWsle->u1.e1.Valid = 1;
    CurrentWsle->u1.e1.LockedInWs = 1;
    CurrentWsle->u1.e1.Direct = 1;

    Pfn = MiGetPfnEntry(MiAddressToPte(CurrentWsle->u1.VirtualAddress)->u.Hard.PageFrameNumber);
    ASSERT(Pfn->u1.WsIndex == 0);
    Pfn->u1.WsIndex = (CurrentWsle - FirstWsle);

    CurrentWsle++;

    CurrentWsle->u1.VirtualAddress = (PVOID)(MI_VAD_BITMAP);
    CurrentWsle->u1.e1.Valid = 1;
    CurrentWsle->u1.e1.LockedInWs = 1;
    CurrentWsle->u1.e1.Direct = 1;

    Pfn = MiGetPfnEntry(MiAddressToPte((PVOID)(MI_VAD_BITMAP))->u.Hard.PageFrameNumber);
    ASSERT(Pfn->u1.WsIndex == 0);
    Pfn->u1.WsIndex = (CurrentWsle - FirstWsle);

    CurrentWsle++;

    CurrentWsle->u1.VirtualAddress = (PVOID)(MmWorkingSetList);
    CurrentWsle->u1.e1.Valid = 1;
    CurrentWsle->u1.e1.LockedInWs = 1;
    CurrentWsle->u1.e1.Direct = 1;

    Pfn = MiGetPfnEntry(MiAddressToPte((PVOID)(MmWorkingSetList))->u.Hard.PageFrameNumber);
    ASSERT(Pfn->u1.WsIndex == 0);
    Pfn->u1.WsIndex = (CurrentWsle - FirstWsle);

    CurrentWsle++;

    WsleCount = ((PAGE_SIZE - BYTE_OFFSET(FirstWsle)) / sizeof(MMWSLE));

    WorkSetSize = (CurrentWsle - FirstWsle);
    CurrentProcess->Vm.WorkingSetSize = WorkSetSize;

    MmWorkingSetList->FirstFree = WorkSetSize;
    MmWorkingSetList->FirstDynamic = WorkSetSize;
    MmWorkingSetList->NextSlot = WorkSetSize;

    ix = (WorkSetSize + 1);
    do
    {
        CurrentWsle->u1.Long = (ix * 0x10);
        CurrentWsle++;
        ix++;
    }
    while (ix <= WsleCount);

    CurrentWsle--;
    CurrentWsle->u1.Long = 0xFFFFFFF0;

    MmWorkingSetList->LastInitializedWsle = (WsleCount - 1);
    DPRINT1("MiInitializeWorkingSetList: FirstFree %X, LastInitializedWsle %X\n", MmWorkingSetList->FirstFree, MmWorkingSetList->LastInitializedWsle);
}

ULONG
NTAPI
MiLocateWsle(
    _In_ PVOID Address,
    _In_ PMMWSL WsList,
    _In_ ULONG InWsIndex,
    _In_ BOOLEAN Parametr4)
{
    DPRINT1("MiLocateWsle: Address %p, WsList %p, InWsIndex %X, Parametr4 %X\n", Address, WsList, InWsIndex, Parametr4);
    UNIMPLEMENTED;
    //ASSERT(FALSE);
    return 0;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmAdjustWorkingSetSize(
    _In_ SIZE_T WorkingSetMinimumInBytes,
    _In_ SIZE_T WorkingSetMaximumInBytes,
    _In_ ULONG SystemCache,
    _In_ BOOLEAN IncreaseOkay)
{
    BOOLEAN dummy;

    return MmAdjustWorkingSetSizeEx(WorkingSetMinimumInBytes,
                                    WorkingSetMaximumInBytes,
                                    SystemCache,
                                    IncreaseOkay,
                                    0,
                                    &dummy);
}

/* EOF */
