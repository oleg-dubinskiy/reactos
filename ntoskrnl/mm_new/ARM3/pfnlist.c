
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

MMPFNLIST MmZeroedPageListHead = {0, ZeroedPageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmFreePageListHead = {0, FreePageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmStandbyPageListHead = {0, StandbyPageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmStandbyPageListByPriority[8];
MMPFNLIST MmModifiedPageListHead = {0, ModifiedPageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmModifiedPageListByColor[1] = {{0, ModifiedPageList, LIST_HEAD, LIST_HEAD}};
MMPFNLIST MmModifiedNoWritePageListHead = {0, ModifiedNoWritePageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmBadPageListHead = {0, BadPageList, LIST_HEAD, LIST_HEAD};
MMPFNLIST MmRomPageListHead = {0, StandbyPageList, LIST_HEAD, LIST_HEAD};

extern PMMCOLOR_TABLES MmFreePagesByColor[FreePageList + 1];
extern PFN_NUMBER MmLowestPhysicalPage;
extern ULONG MmSecondaryColorMask;
extern KEVENT MmZeroingPageEvent;
extern PFN_NUMBER MmAvailablePages;
extern PFN_NUMBER MmLowMemoryThreshold;
extern PFN_NUMBER MmHighMemoryThreshold;
extern PKEVENT MiLowMemoryEvent;
extern PKEVENT MiHighMemoryEvent;

/* FUNCTIONS ******************************************************************/

static
VOID
MiIncrementAvailablePages(
    VOID)
{
    /* Increment available pages */
    MmAvailablePages++;

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

PFN_NUMBER
NTAPI
MiRemoveAnyPage(
    _In_ ULONG Color)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

/* HACK for keeping legacy Mm alive */
extern BOOLEAN MmRosNotifyAvailablePage(PFN_NUMBER PageFrameIndex);

VOID
NTAPI
MiZeroPhysicalPage(
    _In_ PFN_NUMBER PageFrameIndex)
{
    UNIMPLEMENTED_DBGBREAK();
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

/* EOF */
