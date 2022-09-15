
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
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

/* EOF */
