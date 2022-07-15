
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "ARM3/miarm.h"

/* TYPES ********************************************************************/

typedef struct _MM_ALLOCATION_REQUEST
{
    PFN_NUMBER Page;
    LIST_ENTRY ListEntry;
    KEVENT Event;
} MM_ALLOCATION_REQUEST, *PMM_ALLOCATION_REQUEST;

#define MI_ASSERT_PFN_LOCK_HELD() ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL)

/* GLOBALS ********************************************************************/

MM_MEMORY_CONSUMER MiMemoryConsumers[MC_MAXIMUM];

static ULONG MiMinimumAvailablePages;
static ULONG MiNrTotalPages;
static LIST_ENTRY AllocationListHead;
static KSPIN_LOCK AllocationListLock;
static ULONG MiMinimumPagesPerRun;

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
MmInitializeBalancer(
    ULONG NrAvailablePages,
    ULONG NrSystemPages)
{
    memset(MiMemoryConsumers, 0, sizeof(MiMemoryConsumers));

    InitializeListHead(&AllocationListHead);
    KeInitializeSpinLock(&AllocationListLock);

    MiNrTotalPages = NrAvailablePages;

    /* Set up targets. */
    MiMinimumAvailablePages = 0x100;
    MiMinimumPagesPerRun = 0x100;

    if ((NrAvailablePages + NrSystemPages) >= 0x2000)
    {
        MiMemoryConsumers[MC_CACHE].PagesTarget = (NrAvailablePages / 4 * 3);
    }
    else if ((NrAvailablePages + NrSystemPages) >= 0x1000)
    {
        MiMemoryConsumers[MC_CACHE].PagesTarget = (NrAvailablePages / 3 * 2);
    }
    else
    {
        MiMemoryConsumers[MC_CACHE].PagesTarget = (NrAvailablePages / 8);
    }

    MiMemoryConsumers[MC_USER].PagesTarget = (NrAvailablePages - MiMinimumAvailablePages);
}

BOOLEAN
MmRosNotifyAvailablePage(
    PFN_NUMBER Page)
{
    PLIST_ENTRY Entry;
    //PMM_ALLOCATION_REQUEST Request;
    //PMMPFN Pfn;

    /* Make sure the PFN lock is held */
    MI_ASSERT_PFN_LOCK_HELD();

    if (!MiMinimumAvailablePages)
    {
        /* Dirty way to know if we were initialized. */
        return FALSE;
    }

    Entry = ExInterlockedRemoveHeadList(&AllocationListHead, &AllocationListLock);
    if (!Entry)
        return FALSE;

    DPRINT1("MmRosNotifyAvailablePage: FIXME!\n");
    ASSERT(FALSE);
#if 0
    Request = CONTAINING_RECORD(Entry, MM_ALLOCATION_REQUEST, ListEntry);

    MiZeroPhysicalPage(Page);
    Request->Page = Page;

    Pfn = MiGetPfnEntry(Page);
    ASSERT(Pfn->u3.e2.ReferenceCount == 0);
    Pfn->u3.e2.ReferenceCount = 1;
    Pfn->u3.e1.PageLocation = ActiveAndValid;

    /* This marks the PFN as a ReactOS PFN */
    Pfn->u4.AweAllocation = TRUE;

    /* Allocate the extra ReactOS Data and zero it out */
    Pfn->u1.SwapEntry = 0;
    Pfn->RmapListHead = NULL;

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
#endif
    return TRUE;
}

VOID
NTAPI
MmRebalanceMemoryConsumers(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
