
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

ULONG MmFrontOfList;

#if (_MI_PAGING_LEVELS == 2)
  ULONG MiMaximumWorkingSet = ((ULONG_PTR)MI_USER_PROBE_ADDRESS / PAGE_SIZE);
#else
 #error FIXME
#endif

PMMPTE MmFirstFreeSystemCache;
PMMPTE MmLastFreeSystemCache;
PMMPTE MmSystemCachePteBase;
PMMWSLE MmSystemCacheWsle;

extern PVOID MmSystemCacheStart;
extern PVOID MmSystemCacheEnd;
extern ULONG MmSecondaryColorMask;
extern PMMWSL MmSystemCacheWorkingSetList;
extern PFN_NUMBER MmResidentAvailablePages;
extern PFN_NUMBER MmAvailablePages;
extern volatile LONG KiTbFlushTimeStamp;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
MiInitializeSystemCache(
    _In_ ULONG MinimumWorkingSetSize,
    _In_ ULONG MaximumWorkingSetSize)
{
    PMMPTE CacheWsListPte;
    PMMPTE CachePte;
    MMPTE TempPte;
    PFN_NUMBER PageFrameIndex;
    ULONG_PTR HashStart;
    ULONG VacbCount;
    ULONG MinWsSize;
    ULONG WsleIndex;
    ULONG Color;
    ULONG Count;
    ULONG Size;
    ULONG ix;
    KIRQL OldIrql;

    DPRINT("MiInitializeSystemCache: Minimum %X, Maximum %X, MmSystemCacheStart %p, MmSystemCacheEnd %p\n",
           MinimumWorkingSetSize, MaximumWorkingSetSize, MmSystemCacheStart, MmSystemCacheEnd);

    /* Initialize the PTE and PFN for Working Set */

    Color = MI_GET_NEXT_COLOR();

    TempPte.u.Long = ValidKernelPte.u.Long;
    CacheWsListPte = MiAddressToPte(MmSystemCacheWorkingSetList);

    DPRINT("MiInitializeSystemCache: MmSystemCacheWorkingSetList %p, CacheWsListPte %p\n",
           MmSystemCacheWorkingSetList, CacheWsListPte);

    ASSERT(CacheWsListPte->u.Long == 0);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    PageFrameIndex = MiRemoveZeroPage(Color);
    TempPte.u.Hard.PageFrameNumber = PageFrameIndex;

    MiInitializePfnAndMakePteValid(PageFrameIndex, CacheWsListPte, TempPte);
    MmResidentAvailablePages--;

    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Initialize the Working Set */

    MmSystemCacheWs.VmWorkingSetList = MmSystemCacheWorkingSetList;
    MmSystemCacheWs.WorkingSetSize = 0;

#if (_MI_PAGING_LEVELS == 2)
    MmSystemCacheWsle = (PMMWSLE)&MmSystemCacheWorkingSetList->UsedPageTableEntries[0];
#else
 #error FIXME
#endif

    MmSystemCacheWorkingSetList->Wsle = MmSystemCacheWsle;

    MmSystemCacheWorkingSetList->FirstFree = 1;
    MmSystemCacheWorkingSetList->FirstDynamic = 1;
    MmSystemCacheWorkingSetList->NextSlot = 1;

    MmSystemCacheWorkingSetList->HashTable = NULL;
    MmSystemCacheWorkingSetList->HashTableSize = 0;

#if (_MI_PAGING_LEVELS == 2)
    WsleIndex = (MI_MAX_PAGES - MiMaximumWorkingSet);
    HashStart = ((ULONG_PTR)PAGE_ALIGN(&MmSystemCacheWorkingSetList->Wsle[WsleIndex]) + PAGE_SIZE);
    MmSystemCacheWorkingSetList->HashTableStart = (PVOID)HashStart;
#else
 #error FIXME
#endif

    MmSystemCacheWorkingSetList->HighestPermittedHashAddress = MmSystemCacheStart;

    Count = ((ULONG_PTR)MmSystemCacheWorkingSetList + PAGE_SIZE);
    Count -= ((ULONG_PTR)MmSystemCacheWsle / sizeof(PMMWSLE));
    MinWsSize = Count - 1;

    MmSystemCacheWorkingSetList->LastEntry = MinWsSize;
    MmSystemCacheWorkingSetList->LastInitializedWsle = MinWsSize;

    if (MaximumWorkingSetSize <= MinWsSize)
        MaximumWorkingSetSize = (MinWsSize + (PAGE_SIZE / sizeof(PMMWSLE)));

    MmSystemCacheWs.MinimumWorkingSetSize = MinWsSize;
    MmSystemCacheWs.MaximumWorkingSetSize = MaximumWorkingSetSize;

    // FIXME init Wsles

    /* Add the Cache Ptes in list */

#if defined(_X86_)
    Size = ((ULONG_PTR)MmSystemCacheEnd - (ULONG_PTR)MmSystemCacheStart + 1);
    VacbCount = COMPUTE_PAGES_SPANNED(MmSystemCacheStart, Size);
    VacbCount /= MM_PAGES_PER_VACB;
#else
 #error FIXME
#endif

    MmSystemCachePteBase = MI_SYSTEM_PTE_BASE;

    CachePte = MiAddressToPte(MmSystemCacheStart);
    MmFirstFreeSystemCache = CachePte;

    DPRINT("MiInitializeSystemCache: MmFirstFreeSystemCache %p [%p], VacbCount %X\n", MmFirstFreeSystemCache, MmFirstFreeSystemCache->u.Long, VacbCount);

    for (ix = 0; ix < VacbCount; ix++)
    {
        CachePte->u.List.NextEntry = (ULONG)((CachePte + MM_PAGES_PER_VACB) - MmSystemCachePteBase);
        CachePte += MM_PAGES_PER_VACB;
    }

    CachePte -= MM_PAGES_PER_VACB;
    CachePte->u.List.NextEntry = MM_EMPTY_PTE_LIST;

    MmLastFreeSystemCache = CachePte;

    //FIXME MiAllowWorkingSetExpansion();
}

NTSTATUS
NTAPI
MmMapViewInSystemCache(
    _In_ PVOID SectionObject,
    _Out_ PVOID* OutBase,
    _In_ PLARGE_INTEGER SectionOffset,
    _In_ PULONG CapturedViewSize)
{
    PSECTION Section = SectionObject;
    PCONTROL_AREA ControlArea;
    PSUBSECTION Subsection;
    ULONGLONG OffsetInPages;
    ULONGLONG LastPage;
    PMMPTE Pte;
    PMMPTE LastPte;
    PMMPTE SectionProto;
    PMMPTE LastProto;
    MMPTE ProtoPte;
    ULONG SizeInPages;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("MmMapViewInSystemCache: %p, [%p], [%I64X], [%X]\n", Section, (OutBase ? *OutBase : NULL),
           (SectionOffset ? SectionOffset->QuadPart : 0), (CapturedViewSize ? *CapturedViewSize : 0));

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(*CapturedViewSize <= VACB_MAPPING_GRANULARITY);
    ASSERT((SectionOffset->LowPart & (VACB_MAPPING_GRANULARITY - 1)) == 0);

    if (Section->u.Flags.Image)
    {
        DPRINT1("MmMapViewInSystemCache: return STATUS_NOT_MAPPED_DATA\n");
        return STATUS_NOT_MAPPED_DATA;
    }

    ASSERT(*CapturedViewSize != 0);

    ControlArea = Section->Segment->ControlArea;
    ASSERT(ControlArea->u.Flags.GlobalOnlyPerSession == 0);

    if (ControlArea->u.Flags.Rom)
        Subsection = (PSUBSECTION)((PLARGE_CONTROL_AREA)ControlArea + 1);
    else
        Subsection = (PSUBSECTION)((PCONTROL_AREA)ControlArea + 1);

    OffsetInPages = (SectionOffset->QuadPart / PAGE_SIZE);
    SizeInPages = BYTES_TO_PAGES(*CapturedViewSize);
    LastPage = (OffsetInPages + SizeInPages);

    while (OffsetInPages >= (ULONGLONG)Subsection->PtesInSubsection)
    {
        OffsetInPages -= Subsection->PtesInSubsection;
        LastPage -= Subsection->PtesInSubsection;
        Subsection = Subsection->NextSubsection;

        DPRINT("MmMapViewInSystemCache: OffsetInPages %I64X, LastPage %I64X\n", OffsetInPages, LastPage);
    }

    OldIrql = MiLockPfnDb(APC_LEVEL);

    ASSERT(ControlArea->u.Flags.BeingCreated == 0);
    ASSERT(ControlArea->u.Flags.BeingDeleted == 0);
    ASSERT(ControlArea->u.Flags.BeingPurged == 0);

    if (MmFirstFreeSystemCache == (PMMPTE)MM_EMPTY_LIST)
    {
        DPRINT1("MmMapViewInSystemCache: return STATUS_NO_MEMORY\n");
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        return STATUS_NO_MEMORY;
    }

    Pte = MmFirstFreeSystemCache;
    ASSERT(Pte->u.Hard.Valid == 0);

    MmFirstFreeSystemCache = (MmSystemCachePteBase + Pte->u.List.NextEntry);
    ASSERT(MmFirstFreeSystemCache <= MiAddressToPte(MmSystemCacheEnd));

    ControlArea->NumberOfMappedViews++;
    ControlArea->NumberOfSystemCacheViews++;

    ASSERT(ControlArea->NumberOfSectionReferences != 0);

    if (ControlArea->FilePointer)
    {
        Status = MiAddViewsForSection((PMSUBSECTION)Subsection, LastPage, OldIrql);

        ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

        if (!NT_SUCCESS(Status))
        {
            Pte->u.List.NextEntry = MM_EMPTY_PTE_LIST;
            Pte[1].u.List.NextEntry = KiTbFlushTimeStamp;

            OldIrql = MiLockPfnDb(APC_LEVEL);

            MmLastFreeSystemCache->u.List.NextEntry = (Pte - MmSystemCachePteBase);
            MmLastFreeSystemCache = Pte;

            ControlArea->NumberOfMappedViews--;
            ControlArea->NumberOfSystemCacheViews--;

            MiCheckControlArea(ControlArea, OldIrql);

            DPRINT("MmMapViewInSystemCache: return %X\n", Status);
            return Status;
        }
    }
    else
    {
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
    }

    if (Pte->u.List.NextEntry == MM_EMPTY_PTE_LIST)
    {
        DPRINT1("FIXME KeBugCheckEx()\n");
        ASSERT(FALSE);
    }

    DPRINT("MmMapViewInSystemCache: FIXME Flush Tb\n");

    *OutBase = MiPteToAddress(Pte);
    DPRINT("MmMapViewInSystemCache: Pte %p, *OutBase %p\n", Pte, *OutBase);

    Pte[1].u.List.NextEntry = 0;
    LastPte = &Pte[SizeInPages];

    SectionProto = &Subsection->SubsectionBase[OffsetInPages];
    LastProto = &Subsection->SubsectionBase[Subsection->PtesInSubsection];

    for (; Pte < LastPte; Pte++, SectionProto++)
    {
        if (SectionProto >= LastProto)
        {
            if (!Subsection->NextSubsection)
            {
                DPRINT("MmMapViewInSystemCache: Subsection %p\n", Subsection);
                break;
            }

            Subsection = Subsection->NextSubsection;

            SectionProto = Subsection->SubsectionBase;
            LastProto = &SectionProto[Subsection->PtesInSubsection];
        }

        MI_MAKE_PROTOTYPE_PTE(&ProtoPte, SectionProto);
        MI_WRITE_INVALID_PTE(Pte, ProtoPte);
    }

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
MmCheckCachedPageState(
    _In_ PVOID CacheAddress,
    _In_ BOOLEAN Param2)
{
    PETHREAD Thread;
    PMMPFN CachePfn;
    PMMPFN ProtoPfn;
    PMMPTE CachePte;
    PMMPTE SectionProto;
    PMMPTE ProtoPte;
    PMMPTE CachePde;
    MMPTE TempProto;
    MMPTE TempPte;
    PFN_NUMBER ProtoPageNumber;
    ULONG Protection;
    KIRQL OldIrql;

    DPRINT("MmCheckCachedPageState: CacheAddress %p, Param2 %X\n", CacheAddress, Param2);

    CachePte = MiAddressToPte(CacheAddress);

    if (CachePte->u.Hard.Valid)
    {
        DPRINT("MmCheckCachedPageState: return TRUE\n");
        return TRUE;
    }

    Thread = PsGetCurrentThread();
    MiLockWorkingSet(Thread, &MmSystemCacheWs);

    if (CachePte->u.Hard.Valid)
    {
        MiUnlockWorkingSet(Thread, &MmSystemCacheWs);
        DPRINT("MmCheckCachedPageState: return TRUE\n");
        return TRUE;
    }

    ASSERT(CachePte->u.Soft.Prototype == 1);

    SectionProto = MiGetProtoPtr(CachePte);
    ProtoPte = MiAddressToPte(SectionProto);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    ASSERT(CachePte->u.Hard.Valid == 0);
    ASSERT(CachePte->u.Soft.Prototype == 1);

    if (!ProtoPte->u.Hard.Valid)
    {
        DPRINT1("MmCheckCachedPageState: FIXME\n");
        ASSERT(FALSE);
    }

    TempProto.u.Long = SectionProto->u.Long;
    ProtoPageNumber = TempProto.u.Hard.PageFrameNumber;

    if (SectionProto->u.Hard.Valid)
    {
        ProtoPfn = MI_PFN_ELEMENT(ProtoPageNumber);
        TempPte.u.Long = TempProto.u.Long;
    }
    else if (TempProto.u.Soft.Transition && !TempProto.u.Soft.Prototype)
    {
        ProtoPfn = MI_PFN_ELEMENT(ProtoPageNumber);

        if (ProtoPfn->u3.e1.ReadInProgress || ProtoPfn->u4.InPageError)
        {
            DPRINT("MmCheckCachedPageState: return TRUE\n");
            goto Exit;
        }

        if (MmAvailablePages < 0x80)
        {
            if (!PsGetCurrentThread()->MemoryMaker || !MmAvailablePages)
            {
                DPRINT("MmCheckCachedPageState: return TRUE\n");
                goto Exit;
            }
        }

        MiUnlinkPageFromList(ProtoPfn);
        InterlockedIncrement16((PSHORT)&ProtoPfn->u3.e2.ReferenceCount);
        ProtoPfn->u3.e1.PageLocation = ActiveAndValid;

        Protection = ProtoPfn->OriginalPte.u.Soft.Protection;
        Protection &= ~(MM_NOCACHE | MM_WRITECOMBINE);

        if (ProtoPfn->u3.e1.CacheAttribute != MiCached)
        {
            if (ProtoPfn->u3.e1.CacheAttribute == MiNonCached)
                Protection |= MM_NOCACHE;
            else if (ProtoPfn->u3.e1.CacheAttribute == MiWriteCombined)
                Protection |= MM_WRITECOMBINE;
        }

        MI_MAKE_HARDWARE_PTE_KERNEL(&TempPte, (MiHighestUserPte + 1), Protection, ProtoPageNumber);
        MI_WRITE_VALID_PTE(SectionProto, TempPte);

        CachePfn = MI_PFN_ELEMENT(ProtoPfn->u4.PteFrame);
    }
    else
    {
        if (!Param2 || MmAvailablePages < 0x80)
        {
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            MiUnlockWorkingSet(Thread, &MmSystemCacheWs);

            MmAccessFault(0, CacheAddress, KernelMode, NULL);

            DPRINT("MmCheckCachedPageState: return FALSE\n");
            return FALSE;
        }

        DPRINT1("MmCheckCachedPageState: FIXME\n");
        ASSERT(FALSE);

    }

    ProtoPfn->u2.ShareCount++;

    CachePde = MiAddressToPte(CachePte);

    CachePfn = MI_PFN_ELEMENT(CachePde->u.Hard.PageFrameNumber);
    CachePfn->u2.ShareCount++;

    TempPte.u.Hard.Owner = 0;
    MI_WRITE_VALID_PTE(CachePte, TempPte);

    DPRINT("MmCheckCachedPageState: return TRUE\n");

Exit:

    MiUnlockPfnDb(OldIrql, APC_LEVEL);
    MiUnlockWorkingSet(Thread, &MmSystemCacheWs);

    return TRUE;
}

VOID
NTAPI
MmUnmapViewInSystemCache(
    _In_ PVOID BaseAddress,
    _In_ PVOID SectionObject,
    _In_ ULONG FrontOfList)
{
    MMPTE CacheChunkPte[MM_PAGES_PER_VACB];
    PSECTION Section = SectionObject;
    PFILE_OBJECT FileObject;
    PCONTROL_AREA ControlArea;
    PMSUBSECTION Subsection = NULL;
    PETHREAD Thread;
    PMMPTE Pte;
    PMMPTE StartPte;
    PMMPTE LastPte;
    PMMPTE SectionProto = NULL;
    PMMPTE ProtoPde;
    MMPTE TempPte;
    MMPTE TempProto;
    PMMPFN StartPfn;
    PMMPFN Pfn;
    PFN_NUMBER StartPageNumber;
    PFN_NUMBER PageNumber;
    ULONG Idx;
    ULONG ix;
    BOOLEAN IsLocked = FALSE;
    KIRQL OldIrql;

    DPRINT("MmUnmapViewInSystemCache: BaseAddress %p, Section %p, FrontOfList %X\n", BaseAddress, Section, FrontOfList);

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    Thread = PsGetCurrentThread();

    StartPte = MiAddressToPte(BaseAddress);
    LastPte = (StartPte + MM_PAGES_PER_VACB);

    DPRINT("MmUnmapViewInSystemCache: StartPte %p, LastPte %p\n", StartPte, LastPte);

    StartPageNumber = MiAddressToPte(StartPte)->u.Hard.PageFrameNumber;
    StartPfn = MI_PFN_ELEMENT(StartPageNumber);

    ControlArea = Section->Segment->ControlArea;
    FileObject = ControlArea->FilePointer;

    ASSERT((ControlArea->u.Flags.Image == 0) &&
           (ControlArea->u.Flags.PhysicalMemory == 0) &&
           (ControlArea->u.Flags.GlobalOnlyPerSession == 0));

    for (Pte = StartPte, Idx = 0; Pte < LastPte; )
    {
        TempPte.u.Long = Pte->u.Long;
        CacheChunkPte[Idx] = TempPte;

        if (TempPte.u.Hard.Valid)
        {
            if (!IsLocked)
            {
                IsLocked = TRUE;
                MiLockWorkingSet(Thread, &MmSystemCacheWs);
                continue;
            }

            Pfn = MI_PFN_ELEMENT(TempPte.u.Hard.PageFrameNumber);

            DPRINT("MmUnmapViewInSystemCache: FIXME MiTerminateWsle()\n");

            if (FileObject)
            {
                ASSERT(Pfn->u3.e1.PrototypePte);
                ASSERT(Pfn->OriginalPte.u.Soft.Prototype);

                SectionProto = Pfn->PteAddress;

                if (!Subsection)
                    Subsection = (PMSUBSECTION)MiSubsectionPteToSubsection(&Pfn->OriginalPte);
            }

            goto Next;
        }

        if (!TempPte.u.Soft.Prototype)
        {
            ULONG size = ((LastPte - Pte) * sizeof(MMPTE));

            ASSERT(TempPte.u.Long == 0);
            ASSERT(RtlCompareMemoryUlong(Pte, size, 0) == size);

            break;
        }

        if (!FileObject)
            goto Next;

        SectionProto = MiGetProtoPtr(&TempPte);

        if (Subsection)
            goto Next;

        ProtoPde = MiAddressToPte(SectionProto);

        OldIrql = MiLockPfnDb(APC_LEVEL);

        if (!ProtoPde->u.Hard.Valid)
        {
            if (IsLocked)
            {
                DPRINT1("MmUnmapViewInSystemCache: FIXME MiMakeSystemAddressValidPfnSystemWs \n");
                ASSERT(FALSE);
            }
            else
            {
                DPRINT1("MmUnmapViewInSystemCache: FIXME MiMakeSystemAddressValidPfn \n");
                ASSERT(FALSE);
            }
        }

        TempProto.u.Long = SectionProto->u.Long;

        if (TempProto.u.Hard.Valid)
        {
            PageNumber = TempProto.u.Hard.PageFrameNumber;
            Pfn = MI_PFN_ELEMENT(PageNumber);
            SectionProto = &Pfn->OriginalPte;
        }
        else if (TempProto.u.Soft.Transition && !TempProto.u.Soft.Prototype)
        {
            PageNumber = TempProto.u.Trans.PageFrameNumber;
            Pfn = MI_PFN_ELEMENT(PageNumber);
            SectionProto = &Pfn->OriginalPte;
        }
        else
        {
            ASSERT(TempProto.u.Soft.Prototype == 1);
        }

        Subsection = (PMSUBSECTION)MiSubsectionPteToSubsection(SectionProto);

        MiUnlockPfnDb(OldIrql, APC_LEVEL);
Next:
        MI_ERASE_PTE(Pte);

        BaseAddress = Add2Ptr(BaseAddress, PAGE_SIZE);
        Idx++;
        Pte++;
    }

    if (IsLocked)
        MiUnlockWorkingSet(Thread, &MmSystemCacheWs);

    StartPte->u.List.NextEntry = MM_EMPTY_PTE_LIST;
    StartPte[1].u.List.NextEntry = KiTbFlushTimeStamp;

    if (Subsection && Subsection->ControlArea != ControlArea)
    {
        DPRINT1("MmUnmapViewInSystemCache: FIXME KeBugCheckEx()\n");
        ASSERT(FALSE);
    }

    OldIrql = MiLockPfnDb(APC_LEVEL);

    MmLastFreeSystemCache->u.List.NextEntry = (StartPte - MmSystemCachePteBase);
    MmLastFreeSystemCache = StartPte;

    MmFrontOfList = FrontOfList;

    for (ix = 0; ix < Idx; ix++)
    {
        if (!CacheChunkPte[ix].u.Hard.Valid)
            continue;

        PageNumber = CacheChunkPte[ix].u.Hard.PageFrameNumber;
        Pfn = MI_PFN_ELEMENT(PageNumber);

        ASSERT(KeGetCurrentIrql() > APC_LEVEL);

        if (!Pfn->u3.e1.Modified &&
            CacheChunkPte[ix].u.Hard.Dirty)
        {
            ASSERT(Pfn->u3.e1.Rom == 0);
            Pfn->u3.e1.Modified = 1;

            if (!Pfn->OriginalPte.u.Soft.Prototype &&
                !Pfn->u3.e1.WriteInProgress)
            {
                DPRINT1("MmUnmapViewInSystemCache: FIXME MiReleasePageFileSpace \n");
                ASSERT(FALSE);
                Pfn->OriginalPte.u.Soft.PageFileHigh = 0;
            }
        }

        if (StartPfn->u2.ShareCount == 1)
        {
            MiDecrementShareCount(StartPfn, StartPageNumber);
        }
        else
        {
            ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
            ASSERT(MmPfnOwner == KeGetCurrentThread());
            ASSERT(StartPageNumber > 0);

            ASSERT(MI_PFN_ELEMENT(StartPageNumber) == StartPfn);
            ASSERT(StartPfn->u2.ShareCount != 0);

            if (StartPfn->u3.e1.PageLocation != ActiveAndValid &&
                StartPfn->u3.e1.PageLocation != StandbyPageList)
            {
                DPRINT1("MmUnmapViewInSystemCache: FIXME\n");
                ASSERT(FALSE);
            }

            StartPfn->u2.ShareCount--;
            ASSERT(StartPfn->u2.ShareCount < 0xF000000);
        }

        if (ControlArea->NumberOfMappedViews == 1)
        {
            ASSERT(Pfn->u2.ShareCount == 1);
        }

        if (Pfn->u2.ShareCount == 1)
        {
            MiDecrementShareCount(Pfn, PageNumber);
        }
        else
        {
            ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
            ASSERT(MmPfnOwner == KeGetCurrentThread());
            ASSERT(PageNumber > 0);

            ASSERT(MI_PFN_ELEMENT(PageNumber) == Pfn);
            ASSERT(Pfn->u2.ShareCount != 0);

            if (Pfn->u3.e1.PageLocation != ActiveAndValid &&
                Pfn->u3.e1.PageLocation != StandbyPageList)
            {
                DPRINT1("MmUnmapViewInSystemCache: FIXME\n");
                ASSERT(FALSE);
            }

            Pfn->u2.ShareCount--;
            ASSERT(Pfn->u2.ShareCount < 0xF000000);
        }
    }

    MmFrontOfList = 0;

    while (Subsection)
    {
        ASSERT(SectionProto != NULL);
        ASSERT((Subsection->NumberOfMappedViews >= 1) ||
               (Subsection->u.SubsectionFlags.SubsectionStatic == 1));

        MiRemoveViewsFromSection(Subsection, Subsection->PtesInSubsection);

        if (SectionProto >= Subsection->SubsectionBase &&
            SectionProto < &Subsection->SubsectionBase[Subsection->PtesInSubsection])
        {
            break;
        }

        Subsection = (PMSUBSECTION)Subsection->NextSubsection;
        if (!Subsection)
        {
            DPRINT1("MmUnmapViewInSystemCache: FIXME KeBugCheckEx()\n");
            ASSERT(FALSE);
        }

        ASSERT(Subsection->ControlArea == ControlArea);
    }

    ControlArea->NumberOfMappedViews--;
    ControlArea->NumberOfSystemCacheViews--;

    MiCheckControlArea(ControlArea, OldIrql);
}

NTSTATUS
NTAPI
MmCopyToCachedPage(
    _In_ PVOID SystemCacheAddress,
    _In_ PVOID InBuffer,
    _In_ ULONG Offset,
    _In_ ULONG CountInBytes,
    _In_ BOOLEAN IsNeedZeroPage)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PMI_PAGE_SUPPORT_BLOCK PageBlock = NULL;
    PCONTROL_AREA ControlArea;
    PSUBSECTION Subsection;
    PMMPFN Pfn = NULL;
    PMMPFN pfn;
    PMMPTE Pte;
    PMMPTE Proto = NULL;
    PMMPTE ProtoPte;
    MMPTE TempPte;
    MMPTE TempProto;
    PFN_NUMBER PageNumber;
    ULONG Color;
    BOOLEAN IsForceIrpComplete = FALSE;
    BOOLEAN NewPage = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("MmCopyToCachedPage: %p, %p, %X, %X, %X\n", SystemCacheAddress, InBuffer, Offset, CountInBytes, IsNeedZeroPage);

    ASSERT(((ULONG_PTR)SystemCacheAddress & (PAGE_SIZE - 1)) == 0);
    ASSERT((CountInBytes + Offset) <= PAGE_SIZE);
    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    Pte = MiAddressToPte(SystemCacheAddress);
    if (Pte->u.Hard.Valid)
        goto Finish;

    _SEH2_TRY
    {
        ProbeForReadChar(InBuffer);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("MmCopyToCachedPage: FIXME\n");
        ASSERT(FALSE);
        return _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    MiLockWorkingSet(CurrentThread, &MmSystemCacheWs);

    if (Pte->u.Hard.Valid)
    {
        MiUnlockWorkingSet(CurrentThread, &MmSystemCacheWs);
        goto Finish;
    }

    ASSERT(Pte->u.Soft.Prototype == 1);

    Proto = MiGetProtoPtr(Pte);
    ProtoPte = MiAddressToPte(Proto);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    ASSERT(Pte->u.Hard.Valid == 0);

    while (TRUE)
    {
        if (Pte->u.Hard.Valid)
        {
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            MiUnlockWorkingSet(CurrentThread, &MmSystemCacheWs);
            goto Finish;
        }

        ASSERT(Pte->u.Soft.Prototype == 1);

        if (!ProtoPte->u.Hard.Valid)
        {
            DPRINT1("MmCopyToCachedPage: FIXME\n");
            ASSERT(FALSE);

            //MiMakeSystemAddressValidPfnSystemWs(..);

            if (Pte->u.Hard.Valid)
            {
                MiUnlockPfnDb(OldIrql, APC_LEVEL);
                MiUnlockWorkingSet(CurrentThread, &MmSystemCacheWs);
                goto Finish;
            }
        }

        TempProto = *Proto;
        DPRINT("MmCopyToCachedPage: %p, %p\n", Proto, TempProto);

        if (TempProto.u.Hard.Valid)
            break;

        if (TempProto.u.Soft.Transition && !TempProto.u.Soft.Prototype)
        {
            ASSERT((SPFN_NUMBER)MmAvailablePages >= 0);

            if (MmAvailablePages >= 2)
                break;

            DPRINT1("MmCopyToCachedPage: FIXME MiEnsureAvailablePageOrWait()\n");
            ASSERT(FALSE);
        }
        else
        {
            if (MmAvailablePages < 0x80)
            {
                DPRINT1("MmCopyToCachedPage: FIXME MiEnsureAvailablePageOrWait()\n");
                ASSERT(FALSE);
            }

            if (MmAvailablePages >= 0x80 /*|| !MiEnsureAvailablePageOrWait(..)*/)
            {
                if (IsNeedZeroPage)
                    break;

                PageBlock = MiGetInPageSupportBlock(OldIrql, &Status);
                if (PageBlock)
                    break;

                Status = STATUS_SUCCESS;
            }
        }
    }

    if (TempProto.u.Hard.Valid)
    {
        DPRINT1("MmCopyToCachedPage: FIXME\n");
        ASSERT(FALSE);
    }
    else if (TempProto.u.Soft.Transition && !TempProto.u.Soft.Prototype)
    {
        DPRINT1("MmCopyToCachedPage: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        if (IsNeedZeroPage)
        {
            Color = MI_GET_NEXT_COLOR();

            PageNumber = MiRemoveZeroPageSafe(Color);
            if (!PageNumber)
            {
                PageNumber = MiRemoveAnyPage(Color);
                MiZeroPhysicalPage(PageNumber);
            }
                
            Pfn = MI_PFN_ELEMENT(PageNumber);

            ASSERT(Pfn->u2.ShareCount == 0);
            ASSERT(Pfn->u3.e2.ReferenceCount == 0);

            MiInitializePfn(PageNumber, Proto, 1);
    
            Pfn->u3.e1.PrototypePte = 1;
            Pfn->u1.Event = NULL;

            Subsection = MiSubsectionPteToSubsection(Proto);

            ControlArea = Subsection->ControlArea;
            ControlArea->NumberOfPfnReferences++;
    
            MI_MAKE_HARDWARE_PTE(&TempPte, NULL, Pfn->OriginalPte.u.Soft.Protection, PageNumber);

            MI_MAKE_DIRTY_PAGE(&TempPte);
            MI_WRITE_VALID_PTE(Proto, TempPte);

            ASSERT(PageBlock == NULL);
            ASSERT(NewPage == FALSE);
        }
        else
        {
            DPRINT1("MmCopyToCachedPage: FIXME\n");
            ASSERT(FALSE);
        }
    }

    pfn = MI_PFN_ELEMENT(MiAddressToPte(Pte)->u.Hard.PageFrameNumber);
    pfn->u2.ShareCount++;

    TempPte.u.Hard.Owner = 0;

    //DPRINT("MmCopyToCachedPage: Proto %p, TempPte %X\n", Proto, TempPte);

    MI_WRITE_VALID_PTE(Pte, TempPte);

    ASSERT(Pfn->u3.e2.ReferenceCount != 0);
    ASSERT(Pfn->PteAddress == Proto);

    MiUnlockPfnDb(OldIrql, APC_LEVEL);
    MiUnlockWorkingSet(CurrentThread, &MmSystemCacheWs);

    if (!PageBlock)
        goto Finish;

    MiFreeInPageSupportBlock(PageBlock);

    if (IsForceIrpComplete)
    {
        ASSERT(OldIrql == PASSIVE_LEVEL);
        ASSERT(CurrentThread->ActiveFaultCount == 0);

        DPRINT1("MmCopyToCachedPage: FIXME IoRetryIrpCompletions()\n");
        ASSERT(FALSE);
    }

    if (Status == STATUS_INSUFFICIENT_RESOURCES)
    {
        ASSERT(NewPage == FALSE);
        Status = STATUS_SUCCESS;
    }

Finish:

    if (!NewPage)
    {
        PVOID CopyBuffer;
        ULONG OldReadClusterSize;
        UCHAR OldForwardClusterOnly;

        /* Save previous values */
        OldForwardClusterOnly = CurrentThread->ForwardClusterOnly;
        OldReadClusterSize = CurrentThread->ReadClusterSize;

        CurrentThread->ForwardClusterOnly = 1;
        CurrentThread->ReadClusterSize = 0;

        CopyBuffer = Add2Ptr(SystemCacheAddress, Offset);
        ASSERT(Status == STATUS_SUCCESS);

        //_SEH2_TRY
        {
            RtlCopyBytes(CopyBuffer, InBuffer, CountInBytes);
        }
        //_SEH2_EXCEPT()
        {
            //FIXME
        }
        //_SEH2_END;
    
        /* Restore claster variables */
        CurrentThread->ForwardClusterOnly = OldForwardClusterOnly;
        CurrentThread->ReadClusterSize = OldReadClusterSize;
    }

    DPRINT("MmCopyToCachedPage: Status %X\n", Status);
    return Status;
}

/* EOF */
