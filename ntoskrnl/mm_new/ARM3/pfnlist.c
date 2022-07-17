
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

#if DBG
#define ASSERT_LIST_INVARIANT(x) \
  do { \
      ASSERT(((x)->Total == 0 && \
              (x)->Flink == LIST_HEAD && \
              (x)->Blink == LIST_HEAD) || \
             ((x)->Total != 0 && \
              (x)->Flink != LIST_HEAD && \
              (x)->Blink != LIST_HEAD)); \
  } while (0)
#else
  #define ASSERT_LIST_INVARIANT(x)
#endif

/* GLOBALS ********************************************************************/

BOOLEAN MmDynamicPfn;
BOOLEAN MmMirroring;
ULONG MmSystemPageColor;
ULONG MmStandbyRePurposed;

MMPFNLIST MmZeroedPageListHead = {0, ZeroedPageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmFreePageListHead = {0, FreePageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmStandbyPageListHead = {0, StandbyPageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmStandbyPageListByPriority[8];
MMPFNLIST MmModifiedPageListHead = {0, ModifiedPageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmModifiedPageListByColor[1] = {{0, ModifiedPageList, LIST_HEAD, LIST_HEAD}};
MMPFNLIST MmModifiedNoWritePageListHead = {0, ModifiedNoWritePageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmBadPageListHead = {0, BadPageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmRomPageListHead = {0, StandbyPageList, LIST_HEAD, LIST_HEAD};

PMMPFNLIST MmPageLocationList[] =
{
    &MmZeroedPageListHead,
    &MmFreePageListHead,
    &MmStandbyPageListHead,
    &MmModifiedPageListHead,
    &MmModifiedNoWritePageListHead,
    &MmBadPageListHead,
    NULL,
    NULL
};

extern PMMCOLOR_TABLES MmFreePagesByColor[FreePageList + 1];
extern PFN_NUMBER MmLowestPhysicalPage;
extern ULONG MmSecondaryColorMask;
extern KEVENT MmZeroingPageEvent;
extern PFN_NUMBER MmAvailablePages;
extern PFN_NUMBER MmLowMemoryThreshold;
extern PFN_NUMBER MmHighMemoryThreshold;
extern PKEVENT MiLowMemoryEvent;
extern PKEVENT MiHighMemoryEvent;
extern ULONG MmSecondaryColors;
extern PFN_NUMBER MmMinimumFreePages;
extern PVOID MmPagedPoolEnd;
extern ULONG MmFrontOfList;
extern MMPTE DemandZeroPte;

/* FUNCTIONS ******************************************************************/

static
VOID
MiIncrementAvailablePages(
    VOID)
{
    /* Increment available pages */
    MmAvailablePages++;
    //DPRINT("MiIncrementAvailablePages: MmAvailablePages %X\n", MmAvailablePages);

    /* Check if we've reached the configured low memory threshold */
    if (MmAvailablePages == MmLowMemoryThreshold)
    {
        /* Clear the event, because now we're ABOVE the threshold */
        KeClearEvent(MiLowMemoryEvent);
    }
    else if (MmAvailablePages == MmHighMemoryThreshold)
    {
        /* Otherwise check if we reached the high threshold and signal the event */
        KeSetEvent(MiHighMemoryEvent, 0, FALSE);
    }
}

static
VOID
MiDecrementAvailablePages(
    VOID)
{
    ASSERT(MmAvailablePages > 0);

    /* See if we hit any thresholds */
    if (MmAvailablePages == MmHighMemoryThreshold)
        /* Clear the high memory event */
        KeClearEvent(MiHighMemoryEvent);
    else if (MmAvailablePages == MmLowMemoryThreshold)
        /* Signal the low memory event */
        KeSetEvent(MiLowMemoryEvent, 0, FALSE);

    /* One less page */
    MmAvailablePages--;

    if (MmAvailablePages >= MmMinimumFreePages)
        return;

    /* FIXME: Should wake up the MPW and working set manager, if we had one */
    DPRINT1("MiDecrementAvailablePages: MmAvailablePages %X\n", MmAvailablePages);
    DPRINT1("MiDecrementAvailablePages: FIXME MiObtainFreePages()\n");

    /* Call RosMm and see if it can release any pages for us */
    DPRINT1("MiDecrementAvailablePages: call MmRebalanceMemoryConsumers()\n");
    MmRebalanceMemoryConsumers();
}

PFN_NUMBER
NTAPI
MiRemovePageByColor(
    _In_ PFN_NUMBER PageIndex,
    _In_ ULONG Color)
{
    PMMCOLOR_TABLES ColorTable;
    PMMPFNLIST ListHead;
    MMLISTS ListName;
    PFN_NUMBER OldFlink;
    PFN_NUMBER OldBlink;
    PMMPFN Pfn;
    USHORT pageColor;
    USHORT cacheAttribute;

    /* Make sure PFN lock is held */
    MI_ASSERT_PFN_LOCK_HELD();
    //ASSERT(MmPfnOwner == KeGetCurrentThread());

    /* Get the PFN entry */
    Pfn = MI_PFN_ELEMENT (PageIndex);

    /* Capture data for later */
    pageColor = Pfn->u3.e1.PageColor;
    cacheAttribute = Pfn->u3.e1.CacheAttribute;

    DPRINT("MiRemovePageByColor: FIXME PPerfGlobalGroupMask and MiMirroringActive\n");

    /* Could be either on free or zero list */
    ListHead = MmPageLocationList[Pfn->u3.e1.PageLocation];
    ListName = ListHead->ListName;

    /* Remove a page */
    ListHead->Total--;

    /* Get the forward and back pointers */
    OldFlink = Pfn->u1.Flink;
    OldBlink = Pfn->u2.Blink;

    /* Check if the next entry is the list head */
    if (OldFlink != LIST_HEAD)
        /* It is not, so set the backlink of the actual entry, to our backlink */
        MI_PFN_ELEMENT(OldFlink)->u2.Blink = OldBlink;
    else
        /* Set the list head's backlink instead */
        ListHead->Blink = OldBlink;

    /* Check if the back entry is the list head */
    if (OldBlink != LIST_HEAD)
        /* It is not, so set the backlink of the actual entry, to our backlink */
        MI_PFN_ELEMENT(OldBlink)->u1.Flink = OldFlink;
    else
        ListHead->Flink = OldFlink;

    ASSERT(Pfn->u3.e1.RemovalRequested == 0);
    ASSERT(Pfn->u3.e1.Rom == 0);

    /* Zero flags but restore color and cache */
    Pfn->u3.e2.ShortFlags = 0;
    Pfn->u3.e1.PageColor = pageColor;
    Pfn->u3.e1.CacheAttribute = cacheAttribute;

    /* We are not on a list anymore */
    Pfn->u1.Flink = 0;
    Pfn->u2.Blink = 0;

    /* Get the first page on the color list */
    ASSERT(Color < MmSecondaryColors);

    ColorTable = &MmFreePagesByColor[ListName][Color];
    ASSERT(ColorTable->Count >= 1);

    /* Set the forward link to whoever we were pointing to */
    ColorTable->Flink = Pfn->OriginalPte.u.Long;

    /* Get the first page on the color list */
    if (ColorTable->Flink == LIST_HEAD)
        /* This is the beginning of the list, so set the sentinel value */
        ColorTable->Blink = (PVOID)LIST_HEAD;
    else
        /* The list is empty, so we are the first page */
        MI_PFN_ELEMENT(ColorTable->Flink)->u4.PteFrame = COLORED_LIST_HEAD;

    /* One less page */
    ColorTable->Count--;

    /* Decrement number of available pages */
    MiDecrementAvailablePages();

    return PageIndex;
}

PFN_NUMBER
NTAPI
MiRemovePageFromList(
    _In_ PMMPFNLIST ListHead)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

PFN_NUMBER
NTAPI
MiRemoveAnyPage(
    _In_ ULONG Color)
{
    PFN_NUMBER PageIndex;
    PMMPFNLIST StandbyListHead;
    PMMPFN Pfn;

    //DPRINT("MiRemoveAnyPage: Color %X\n", Color);

    /* Make sure PFN lock is held and we have pages */
    MI_ASSERT_PFN_LOCK_HELD();
    //ASSERT(MmPfnOwner == KeGetCurrentThread());

    ASSERT(MmAvailablePages != 0);
    ASSERT(Color < MmSecondaryColors);

    /* Check the colored free list */
    PageIndex = MmFreePagesByColor[FreePageList][Color].Flink;
    if (PageIndex != LIST_HEAD)
    {
        /* Sanity checks */
        Pfn = MI_PFN_ELEMENT(PageIndex);
        ASSERT(Pfn->u3.e1.PageLocation == FreePageList);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        /* Remove the page */
        PageIndex = MiRemovePageByColor(PageIndex, Color);

        ASSERT(Pfn == MI_PFN_ELEMENT(PageIndex));
        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        ASSERT(Pfn->u2.ShareCount == 0);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        return PageIndex;
    }

    /* Check the colored zero list */
    PageIndex = MmFreePagesByColor[ZeroedPageList][Color].Flink;
    if (PageIndex != LIST_HEAD)
    {
        /* Sanity checks */
        Pfn = MI_PFN_ELEMENT(PageIndex);
        ASSERT(Pfn->u3.e1.PageLocation == ZeroedPageList);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        /* Remove the page */
        PageIndex = MiRemovePageByColor(PageIndex, Color);

        ASSERT(Pfn == MI_PFN_ELEMENT(PageIndex));
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        return PageIndex;
    }

    /* Check the free list */
    PageIndex = MmFreePageListHead.Flink;
    if (PageIndex != LIST_HEAD)
    {
        /* Sanity checks */
        Pfn = MI_PFN_ELEMENT(PageIndex);
        ASSERT(Pfn->u3.e1.PageLocation == FreePageList);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        /* Remove the page */
        Color = (PageIndex & MmSecondaryColorMask);
        PageIndex = MiRemovePageByColor(PageIndex, Color);

        ASSERT(Pfn == MI_PFN_ELEMENT(PageIndex));
        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        ASSERT(Pfn->u2.ShareCount == 0);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        return PageIndex;
    }

    ASSERT(MmFreePageListHead.Total == 0);

    /* Check the zero list */
    PageIndex = MmZeroedPageListHead.Flink;
    if (PageIndex != LIST_HEAD)
    {
        Pfn = MI_PFN_ELEMENT(PageIndex);

        /* Remove the page */
        Color = (PageIndex & MmSecondaryColorMask);
        PageIndex = MiRemovePageByColor(PageIndex, Color);

        /* Sanity checks */
        ASSERT(Pfn == MI_PFN_ELEMENT(PageIndex));
        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        ASSERT(Pfn->u2.ShareCount == 0);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        return PageIndex;
    }

    ASSERT(MmZeroedPageListHead.Total == 0);

    /* Check the standby list */
    for (StandbyListHead = &MmStandbyPageListByPriority[0];
         StandbyListHead < &MmStandbyPageListByPriority[8];
         StandbyListHead++)
    {
        if (StandbyListHead->Total)
        {
            /* Remove the page */
            PageIndex = MiRemovePageFromList(StandbyListHead);
            break;
        }
    }

    /* Sanity checks */
    ASSERT(StandbyListHead < &MmStandbyPageListByPriority[8]);
    ASSERT((MI_PFN_ELEMENT(PageIndex))->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

    MmStandbyRePurposed++;

    // ? MiMirroringActive

    /* Sanity checks */
    Pfn = MI_PFN_ELEMENT(PageIndex);
    ASSERT(Pfn->u3.e2.ReferenceCount == 0);
    ASSERT(Pfn->u2.ShareCount == 0);

    return PageIndex;
}

PFN_NUMBER
NTAPI
MiRemoveZeroPage(
    _In_ ULONG Color)
{
    PMMCOLOR_TABLES FreePagesByColor;
    PMMPFNLIST ListHead;
    PFN_NUMBER PageIndex;
    PFN_NUMBER Page;
    PMMPFN Pfn;

    DPRINT("MiRemoveZeroPage: Color %X\n", Color);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(MmPfnOwner == KeGetCurrentThread());
    ASSERT(MmAvailablePages != 0);

    FreePagesByColor = MmFreePagesByColor[ZeroedPageList];

    ASSERT(Color < MmSecondaryColors);

    PageIndex = FreePagesByColor[Color].Flink;
    if (PageIndex != -1)
    {
        Pfn = MI_PFN_ELEMENT(PageIndex);

        ASSERT((Pfn->u3.e1.PageLocation == ZeroedPageList) ||
              ((Pfn->u3.e1.PageLocation == FreePageList) &&
               (FreePagesByColor == MmFreePagesByColor[FreePageList])));

        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        Page = MiRemovePageByColor(PageIndex, Color);

        ASSERT(Pfn == MI_PFN_ELEMENT(Page));
        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        ASSERT(Pfn->u2.ShareCount == 0);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);//0x1FFEDCB

        return Page;
    }

    PageIndex = MmZeroedPageListHead.Flink;
    if (PageIndex != -1)
    {
        Pfn = MI_PFN_ELEMENT(PageIndex);

        ASSERT(Pfn->u3.e1.PageLocation == ZeroedPageList);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        Page = MiRemovePageByColor(PageIndex, PageIndex & MmSecondaryColorMask);

        ASSERT(Pfn == MI_PFN_ELEMENT(Page));
        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        ASSERT(Pfn->u2.ShareCount == 0);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        return Page;
    }

    FreePagesByColor = MmFreePagesByColor[FreePageList];

    PageIndex = FreePagesByColor[Color].Flink;
    if (PageIndex != -1)
    {
        Pfn = MI_PFN_ELEMENT(PageIndex);

        ASSERT(Pfn->u3.e1.PageLocation == FreePageList);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        Page = MiRemovePageByColor(PageIndex, Color);

        ASSERT(Pfn == MI_PFN_ELEMENT(Page));
        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        ASSERT(Pfn->u2.ShareCount == 0);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        Pfn = MI_PFN_ELEMENT(Page);
        MiZeroPhysicalPage(Page);

        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        ASSERT(Pfn->u2.ShareCount == 0);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        return Page;
    }

    PageIndex = MmFreePageListHead.Flink;
    if (PageIndex != -1)
    {
        Pfn = MI_PFN_ELEMENT(PageIndex);
        Page = MiRemovePageByColor(PageIndex, PageIndex & MmSecondaryColorMask);

        ASSERT(Pfn == MI_PFN_ELEMENT(Page));
        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        ASSERT(Pfn->u2.ShareCount == 0);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        Pfn = MI_PFN_ELEMENT(Page);
        MiZeroPhysicalPage(Page);

        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        ASSERT(Pfn->u2.ShareCount == 0);
        ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

        return Page;
    }

    ASSERT(MmZeroedPageListHead.Total == 0);
    ASSERT(MmFreePageListHead.Total == 0);

    for (ListHead = &MmStandbyPageListByPriority[0];
         ListHead < &MmStandbyPageListByPriority[8];
         ListHead++)
    {
        if (ListHead->Total)
        {
            Page = MiRemovePageFromList(ListHead);
            break;
        }
    }

    ASSERT(ListHead < &MmStandbyPageListByPriority[8]);
    ASSERT((MI_PFN_ELEMENT(Page))->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

    MmStandbyRePurposed++;

    // ? MiMirroringActive

    Pfn = MI_PFN_ELEMENT(Page);
    MiZeroPhysicalPage(Page);

    ASSERT(Pfn->u3.e2.ReferenceCount == 0);
    ASSERT(Pfn->u2.ShareCount == 0);
    ASSERT(Pfn->u4.PteFrame != MI_MAGIC_AWE_PTEFRAME);

    return Page;
}

/* HACK for keeping legacy Mm alive */
extern BOOLEAN MmRosNotifyAvailablePage(PFN_NUMBER PageFrameIndex);

VOID
NTAPI
MiZeroPhysicalPage(
    _In_ PFN_NUMBER PageFrameIndex)
{
    KIRQL OldIrql;
    PVOID VirtualAddress;
    PEPROCESS Process = PsGetCurrentProcess();

    /* Map in hyperspace, then wipe it using XMMI or MEMSET */
    VirtualAddress = MiMapPageInHyperSpace(Process, PageFrameIndex, &OldIrql);
    ASSERT(VirtualAddress);

    KeZeroPages(VirtualAddress, PAGE_SIZE);

    MiUnmapPageInHyperSpace(Process, VirtualAddress, OldIrql);
}

VOID
NTAPI
MiInsertPageInFreeList(
    _In_ PFN_NUMBER PageFrameIndex)
{
    PMMCOLOR_TABLES ColorTable;
    PMMPFNLIST ListHead;
    PFN_NUMBER LastPage;
    PMMPFN Pfn;
    ULONG Color;
    PMMPFN Blink;

    //DPRINT("MiInsertPageInFreeList: PageFrameIndex %X\n", PageFrameIndex);

    /* Make sure the page index is valid */
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT((PageFrameIndex != 0) &&
           (PageFrameIndex <= MmHighestPhysicalPage) &&
           (PageFrameIndex >= MmLowestPhysicalPage));

    /* Get the PFN entry */
    Pfn = MI_PFN_ELEMENT(PageFrameIndex);

    /* Sanity checks that a right kind of page is being inserted here */
    ASSERT(Pfn->u4.MustBeCached == 0);
    ASSERT(Pfn->u3.e1.Rom != 1);
    ASSERT(Pfn->u3.e1.RemovalRequested == 0);
    ASSERT(Pfn->u4.VerifierAllocation == 0);
    ASSERT(Pfn->u3.e2.ReferenceCount == 0);

    /* HACK HACK HACK : Feed the page to legacy Mm */
    if (MmRosNotifyAvailablePage(PageFrameIndex))
    {
        DPRINT1("Legacy Mm eating ARM3 page!.\n");
        return;
    }

    /* Get the free page list and increment its count */
    ListHead = &MmFreePageListHead;
    ASSERT_LIST_INVARIANT(ListHead);

    ListHead->Total++;

    /* Get the last page on the list */
    LastPage = ListHead->Blink;
    if (LastPage != LIST_HEAD)
        /* Link us with the previous page, so we're at the end now */
        MI_PFN_ELEMENT(LastPage)->u1.Flink = PageFrameIndex;
    else
        /* The list is empty, so we are the first page */
        ListHead->Flink = PageFrameIndex;

    /* Now make the list head point back to us (since we go at the end) */
    ListHead->Blink = PageFrameIndex;
    ASSERT_LIST_INVARIANT(ListHead);

    /* And initialize our own list pointers */
    Pfn->u1.Flink = LIST_HEAD;
    Pfn->u2.Blink = LastPage;

    /* Set the list name and default priority */
    Pfn->u3.e1.PageLocation = FreePageList;
    Pfn->u4.Priority = 3;

    /* Clear some status fields */
    Pfn->u4.InPageError = 0;
    Pfn->u4.AweAllocation = 0;

    /* Increment number of available pages */
    MiIncrementAvailablePages();

    /* Get the page color */
    Color = PageFrameIndex & MmSecondaryColorMask;

    /* Get the first page on the color list */
    ColorTable = &MmFreePagesByColor[FreePageList][Color];
    if (ColorTable->Flink == LIST_HEAD)
    {
        /* The list is empty, so we are the first page */
        Pfn->u4.PteFrame = COLORED_LIST_HEAD;
        ColorTable->Flink = PageFrameIndex;
    }
    else
    {
        /* Get the previous page */
        Blink = (PMMPFN)ColorTable->Blink;

        /* Make it link to us, and link back to it */
        Blink->OriginalPte.u.Long = PageFrameIndex;
        Pfn->u4.PteFrame = MiGetPfnEntryIndex(Blink);
    }

    /* Now initialize our own list pointers */
    ColorTable->Blink = Pfn;

    /* This page is now the last */
    Pfn->OriginalPte.u.Long = LIST_HEAD;

    /* And increase the count in the colored list */
    ColorTable->Count++;

    /* Notify zero page thread if enough pages are on the free list now */
    if (ListHead->Total >= 8) // FIXME
        /* Set the event */
        KeSetEvent(&MmZeroingPageEvent, IO_NO_INCREMENT, FALSE);

  #if MI_TRACE_PFNS
    Pfn->PfnUsage = MI_USAGE_FREE_PAGE;
    RtlZeroMemory(Pfn->ProcessName, 16);
  #endif
}

VOID
NTAPI
MiInitializePfnForOtherProcess(
    _In_ PFN_NUMBER PageFrameIndex,
    _In_ PVOID PteAddress,
    _In_ PFN_NUMBER PteFrame)
{
    PMMPFN Pfn;

    /* Setup the PTE */
    Pfn = MI_PFN_ELEMENT(PageFrameIndex);
    Pfn->PteAddress = PteAddress;

    /* Make this a software PTE */
    MI_MAKE_SOFTWARE_PTE(&Pfn->OriginalPte, MM_READWRITE);

    /* Setup the page */
    ASSERT(Pfn->u3.e2.ReferenceCount == 0);

    Pfn->u3.e2.ReferenceCount = 1;
    Pfn->u2.ShareCount = 1;
    Pfn->u3.e1.PageLocation = ActiveAndValid;
    Pfn->u3.e1.Modified = TRUE;
    Pfn->u4.InPageError = FALSE;

    /* Did we get a PFN for the page table */
    if (!PteFrame)
        return;

    /* Store it */
    Pfn->u4.PteFrame = PteFrame;

    /* Increase its share count so we don't get rid of it */
    Pfn = MI_PFN_ELEMENT(PteFrame);
    Pfn->u2.ShareCount++;
}

VOID
NTAPI
MiInitializePfn(
    _In_ PFN_NUMBER PageFrameIndex,
    _In_ PMMPTE Pte,
    _In_ BOOLEAN Modified)
{
    PMMPFN Pfn;
    PMMPTE Pde;
    NTSTATUS Status;

    MI_ASSERT_PFN_LOCK_HELD();

    /* Setup the PTE */
    Pfn = MI_PFN_ELEMENT(PageFrameIndex);
    Pfn->PteAddress = Pte;

    /* Check if this PFN is part of a valid address space */
    if (Pte->u.Hard.Valid == 1)
    {
        /* Only valid from MmCreateProcessAddressSpace path */
        ASSERT(PsGetCurrentProcess()->Vm.WorkingSetSize == 0);

        /* Make this a demand zero PTE */
        MI_MAKE_SOFTWARE_PTE(&Pfn->OriginalPte, MM_READWRITE);
    }
    else
    {
        /* Copy the PTE data */
        Pfn->OriginalPte = *Pte;

        ASSERT(!((Pfn->OriginalPte.u.Soft.Prototype == 0) &&
                 (Pfn->OriginalPte.u.Soft.Transition == 1)));
    }

    /* Otherwise this is a fresh page -- set it up */
    ASSERT(Pfn->u3.e2.ReferenceCount == 0);
    Pfn->u3.e2.ReferenceCount = 1;
    Pfn->u2.ShareCount = 1;
    Pfn->u3.e1.PageLocation = ActiveAndValid;
    ASSERT(Pfn->u3.e1.Rom == 0);
    Pfn->u3.e1.Modified = Modified;

    /* Get the page table for the PTE */
    Pde = MiAddressToPte(Pte);

    if (!Pde->u.Hard.Valid)
    {
        /* Make sure the PDE gets paged in properly */
        DPRINT1("MiInitializePfn: call MiCheckPdeForPagedPool(%p)\n", Pte);

        Status = MiCheckPdeForPagedPool(Pte);
        if (!NT_SUCCESS(Status))
        {
            /* Crash */
            ASSERT(FALSE);KeBugCheckEx(MEMORY_MANAGEMENT,
                         0x61940,
                         (ULONG_PTR)Pte,
                         (ULONG_PTR)Pde->u.Long,
                         (ULONG_PTR)MiPteToAddress(Pte));
        }
    }

    /* Get the PFN for the page table */
    PageFrameIndex = PFN_FROM_PTE(Pde);
    ASSERT(PageFrameIndex != 0);
    Pfn->u4.PteFrame = PageFrameIndex;

    /* Increase its share count so we don't get rid of it */
    Pfn = MI_PFN_ELEMENT(PageFrameIndex);
    Pfn->u2.ShareCount++;
}

VOID
NTAPI
MiInitializePfnAndMakePteValid(
    _In_ PFN_NUMBER PageFrameIndex,
    _In_ PMMPTE Pte,
    _In_ MMPTE TempPte)
{
    PMMPFN Pfn;
    PMMPTE Pde;
    NTSTATUS Status;

    MI_ASSERT_PFN_LOCK_HELD();

    /* PTE must be invalid */
    ASSERT(Pte->u.Hard.Valid == 0);

    /* Setup the PTE */
    Pfn = MI_PFN_ELEMENT(PageFrameIndex);
    Pfn->PteAddress = Pte;
    Pfn->OriginalPte = DemandZeroPte;

    /* Otherwise this is a fresh page -- set it up */
    ASSERT(Pfn->u3.e2.ReferenceCount == 0);
    Pfn->u3.e2.ReferenceCount++;
    Pfn->u2.ShareCount++;
    Pfn->u3.e1.PageLocation = ActiveAndValid;
    ASSERT(Pfn->u3.e1.Rom == 0);
    Pfn->u3.e1.Modified = 1;

    /* Get the page table for the PTE */
    Pde = MiAddressToPte(Pte);
    if (!Pde->u.Hard.Valid)
    {
        /* Make sure the PDE gets paged in properly */
        DPRINT1("MiCheckPdeForPagedPool(Address %p)\n", Pte);

        Status = MiCheckPdeForPagedPool(Pte);
        if (!NT_SUCCESS(Status))
        {
            /* Crash */
            ASSERT(FALSE);
            KeBugCheckEx(MEMORY_MANAGEMENT,
                         0x61940,
                         (ULONG_PTR)Pte,
                         (ULONG_PTR)Pde->u.Long,
                         (ULONG_PTR)MiPteToAddress(Pte));
        }
    }

    /* Get the PFN for the page table */
    PageFrameIndex = PFN_FROM_PTE(Pde);
    ASSERT(PageFrameIndex != 0);
    Pfn->u4.PteFrame = PageFrameIndex;

    /* Increase its share count so we don't get rid of it */
    Pfn = MI_PFN_ELEMENT(PageFrameIndex);
    Pfn->u2.ShareCount++;

    /* Write valid PTE */
    MI_WRITE_VALID_PTE(Pte, TempPte);
}

VOID
NTAPI
MiInsertPageInList(
    _In_ PMMPFNLIST ListHead,
    _In_ PFN_NUMBER PageFrameIndex)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
FASTCALL
MiInsertStandbyListAtFront(
    _In_ PFN_NUMBER PageFrameIndex)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MiDecrementReferenceCount(
    _In_ PMMPFN Pfn,
    _In_ PFN_NUMBER PageFrameIndex)
{
    /* PFN lock must be held */
    MI_ASSERT_PFN_LOCK_HELD();

    /* Sanity checks on the page */
    if (PageFrameIndex > MmHighestPhysicalPage ||
        Pfn != MI_PFN_ELEMENT(PageFrameIndex) ||
        Pfn->u3.e2.ReferenceCount == 0 ||
        Pfn->u3.e2.ReferenceCount >= 2500)
    {
        DPRINT1("PageFrameIndex=0x%lx, MmHighestPhysicalPage=0x%lx\n", PageFrameIndex, MmHighestPhysicalPage);
        DPRINT1("Pfn=%p, Element=%p, RefCount=%u\n", Pfn, MI_PFN_ELEMENT(PageFrameIndex), Pfn->u3.e2.ReferenceCount);

        ASSERT(PageFrameIndex <= MmHighestPhysicalPage);
        ASSERT(Pfn == MI_PFN_ELEMENT(PageFrameIndex));
        ASSERT(Pfn->u3.e2.ReferenceCount != 0);
        ASSERT(Pfn->u3.e2.ReferenceCount < 2500);
    }

    /* Dereference the page, bail out if it's still alive */
    InterlockedDecrement16((PSHORT)&Pfn->u3.e2.ReferenceCount);
    if (Pfn->u3.e2.ReferenceCount)
        return;

    /* Nobody should still have reference to this page */
    if (Pfn->u2.ShareCount)
        /* Otherwise something's really wrong */
        ASSERT(FALSE);KeBugCheckEx(PFN_LIST_CORRUPT, 7, PageFrameIndex, Pfn->u2.ShareCount, 0);

    /* And it should be lying on some page list */
    ASSERT(Pfn->u3.e1.PageLocation != ActiveAndValid);

    /* Did someone set the delete flag? */
    if (MI_IS_PFN_DELETED(Pfn))
    {
        /* Insert it into the free list, there's nothing left to do */
        MiInsertPageInFreeList(PageFrameIndex);
        return;
    }

    /* Check to see which list this page should go into */
    if (Pfn->u3.e1.Modified == 1)
    {
        /* Push it into the modified page list */
        MiInsertPageInList(&MmModifiedPageListHead, PageFrameIndex);
        return;
    }

    /* Otherwise, insert this page into the standby list */
    ASSERT(Pfn->u3.e1.RemovalRequested == 0);

    if (!MmFrontOfList)
        MiInsertPageInList(&MmStandbyPageListHead, PageFrameIndex);
    else
        MiInsertStandbyListAtFront(PageFrameIndex);

}

VOID
NTAPI
MiDecrementShareCount(
    _In_ PMMPFN Pfn,
    _In_ PFN_NUMBER PageFrameIndex)
{
    PMMPTE Pte;
    MMPTE TempPte;

    ASSERT(PageFrameIndex > 0);
    ASSERT(MI_PFN_ELEMENT(PageFrameIndex) != NULL);
    ASSERT(Pfn == MI_PFN_ELEMENT(PageFrameIndex));
    ASSERT(MI_IS_ROS_PFN(Pfn) == FALSE);

    /* Page must be in-use */
    if ((Pfn->u3.e1.PageLocation != ActiveAndValid) &&
        (Pfn->u3.e1.PageLocation != StandbyPageList))
    {
        /* Otherwise we have PFN corruption */
        ASSERT(FALSE);
        KeBugCheckEx(PFN_LIST_CORRUPT,
                     0x99,
                     PageFrameIndex,
                     Pfn->u3.e1.PageLocation,
                     0);
    }

    /* Page should at least have one reference */
    ASSERT(Pfn->u3.e2.ReferenceCount != 0);

    /* Check if the share count is now 0 */
    ASSERT(Pfn->u2.ShareCount < 0xF000000);

    if (--Pfn->u2.ShareCount)
        return;

    /* Was this a prototype PTE? */
    if (Pfn->u3.e1.PrototypePte)
    {
        /* Grab the PTE address and make sure it's in prototype pool */
        Pte = Pfn->PteAddress;
        ASSERT((Pte >= (PMMPTE)MmPagedPoolStart) && (Pte <= (PMMPTE)MmPagedPoolEnd));

        /* The PTE that backs it should also be valdi */
        Pte = MiAddressToPte(Pte);
        ASSERT(Pte->u.Hard.Valid == 1);

        /* Get the original prototype PTE and turn it into a transition PTE */
        Pte = Pfn->PteAddress;
        TempPte = *Pte;
        TempPte.u.Soft.Transition = 1;
        TempPte.u.Soft.Valid = 0;
        TempPte.u.Soft.Prototype = 0;
        TempPte.u.Soft.Protection = Pfn->OriginalPte.u.Soft.Protection;

        MI_WRITE_INVALID_PTE(Pte, TempPte);

        DPRINT("Marking PTE: %p as transition (%p - %lx)\n", Pte, Pfn, MiGetPfnEntryIndex(Pfn));
    }

    /* Put the page in transition */
    Pfn->u3.e1.PageLocation = TransitionPage;

    /* PFN lock must be held */
    MI_ASSERT_PFN_LOCK_HELD();

    if (Pfn->u3.e2.ReferenceCount != 1)
    {
        /* Otherwise, just drop the reference count */
        InterlockedDecrement16((PSHORT)&Pfn->u3.e2.ReferenceCount);
        return;
    }

    /* Is there still a PFN for this page? */
    if (!MI_IS_PFN_DELETED(Pfn))
    {
        /* PFN not yet deleted, drop a ref count */
        MiDecrementReferenceCount(Pfn, PageFrameIndex);
        return;
    }

    /* Clear the last reference */
    Pfn->u3.e2.ReferenceCount = 0;
    ASSERT(Pfn->OriginalPte.u.Soft.Prototype == 0);

    /* Mark the page temporarily as valid, we're going to make it free soon */
    Pfn->u3.e1.PageLocation = ActiveAndValid;

    /* Bring it back into the free list */
    MiInsertPageInFreeList(PageFrameIndex);
}

/* EOF */
