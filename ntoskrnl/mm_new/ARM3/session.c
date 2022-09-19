
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

/* Number of initial session IDs */
#define MI_INITIAL_SESSION_IDS  64

/* Number of session data and tag pages */
#define MI_SESSION_DATA_PAGES_MAXIMUM (MM_ALLOCATION_GRANULARITY / PAGE_SIZE)
#define MI_SESSION_TAG_PAGES_MAXIMUM  (MM_ALLOCATION_GRANULARITY / PAGE_SIZE)

PMM_SESSION_SPACE MmSessionSpace;
KGUARDED_MUTEX MiSessionIdMutex;
RTL_BITMAP MiSessionWideVaBitMap;
PRTL_BITMAP MiSessionIdBitmap;
LIST_ENTRY MiSessionWsList;
LIST_ENTRY MmWorkingSetExpansionHead;
PFN_NUMBER MiSessionDataPages;
PFN_NUMBER MiSessionTagPages;
PFN_NUMBER MiSessionTagSizePages;
PFN_NUMBER MiSessionBigPoolPages;
PFN_NUMBER MiSessionCreateCharge;
KSPIN_LOCK MmExpansionLock;
PETHREAD MiExpansionLockOwner;
LONG MmSessionDataPages;
volatile LONG MiSessionLeaderExists;

extern PVOID MiSessionImageStart;
extern PVOID MiSessionImageEnd;
extern PFN_NUMBER MmLowestPhysicalPage;
extern ULONG MmSecondaryColorMask;
extern MMPTE ValidKernelPdeLocal;
extern MMPTE ValidKernelPteLocal;
extern PVOID MiSessionSpaceWs;
extern SIZE_T MmSessionSize;

/* FUNCTIONS ******************************************************************/

LCID
NTAPI
MmGetSessionLocaleId(VOID)
{
    PEPROCESS Process;

    DPRINT("MmGetSessionLocaleId()\n");
    PAGED_CODE();

    /* Get the current process */
    Process = PsGetCurrentProcess();

    /* Check if it's NOT the Session Leader */
    if (!Process->Vm.Flags.SessionLeader)
    {
        /* Make sure it has a valid Session */
        if (Process->Session)
            /* Get the Locale ID */
            return ((PMM_SESSION_SPACE)Process->Session)->LocaleId;
    }

    /* Not a session leader, return the default */
    return PsDefaultThreadLocaleId;
}

_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
MmSetSessionLocaleId(
    _In_ LCID LocaleId)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmSessionDelete(
    _In_ ULONG SessionId)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
MiSessionLeader(
    _In_ PEPROCESS Process)
{
    KIRQL OldIrql;

    /* Set the flag while under the expansion lock */
    OldIrql = MiAcquireExpansionLock();
    Process->Vm.Flags.SessionLeader = TRUE;
    MiReleaseExpansionLock(OldIrql);
}

NTSTATUS
NTAPI
MiSessionInitializeWorkingSetList(VOID)
{
    PMM_SESSION_SPACE SessionGlobal;
    PMMWSL WorkingSetList;
    PMMPDE Pde;
    PMMPTE Pte;
    MMPDE TempPde;
    MMPTE TempPte;
    PFN_NUMBER PageFrameIndex;
    ULONG Color;
    ULONG Index;
    KIRQL OldIrql;
    BOOLEAN AllocatedPageTable;

    DPRINT("MiSessionInitializeWorkingSetList()\n");

    /* Get pointers to session global and the session working set list */
    SessionGlobal = MmSessionSpace->GlobalVirtualAddress;
    WorkingSetList = (PMMWSL)MiSessionSpaceWs;

    /* Fill out the two pointers */
    MmSessionSpace->Vm.VmWorkingSetList = WorkingSetList;
    MmSessionSpace->Wsle = (PMMWSLE)WorkingSetList->UsedPageTableEntries;

    /* Get the PDE for the working set, and check if it's already allocated */
    Pde = MiAddressToPde(WorkingSetList);
    if (Pde->u.Hard.Valid)
    {
        /* Nope, we'll have to do it */
      #ifndef _M_ARM
        ASSERT(Pde->u.Hard.Global == 0);
      #endif

        AllocatedPageTable = FALSE;
    }
    else
    {
        /* Yep, that makes our job easier */
        AllocatedPageTable = TRUE;
    }

    /* Get the PTE for the working set */
    Pte = MiAddressToPte(WorkingSetList);

    /* Initialize the working set lock, and lock the PFN database */
    ExInitializePushLock(&SessionGlobal->Vm.WorkingSetMutex);
    //MmLockPageableSectionByHandle(ExPageLockHandle);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* Check if we need a page table */
    if (AllocatedPageTable)
    {
        /* Get a zeroed colored zero page */
        MI_SET_USAGE(MI_USAGE_INIT_MEMORY);
        Color = MI_GET_NEXT_COLOR();

        PageFrameIndex = MiRemoveZeroPageSafe(Color);
        if (!PageFrameIndex)
        {
            /* No zero pages, grab a free one */
            PageFrameIndex = MiRemoveAnyPage(Color);

            /* Zero it outside the PFN lock */
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            MiZeroPhysicalPage(PageFrameIndex);
            OldIrql = MiLockPfnDb(APC_LEVEL);
        }

        /* Write a valid PDE for it */
        TempPde = ValidKernelPdeLocal;
        TempPde.u.Hard.PageFrameNumber = PageFrameIndex;
        MI_WRITE_VALID_PDE(Pde, TempPde);

        /* Add this into the list */
        Index = (((ULONG_PTR)WorkingSetList - (ULONG_PTR)MmSessionBase) >> 22);

#ifndef _M_AMD64
        MmSessionSpace->PageTables[Index] = TempPde;
#endif
        /* Initialize the page directory page, and now zero the working set list itself */
        MiInitializePfnForOtherProcess(PageFrameIndex, Pde, MmSessionSpace->SessionPageDirectoryIndex);
        KeZeroPages(Pte, PAGE_SIZE);
    }

    /* Get a zeroed colored zero page */
    MI_SET_USAGE(MI_USAGE_INIT_MEMORY);
    Color = MI_GET_NEXT_COLOR();

    PageFrameIndex = MiRemoveZeroPageSafe(Color);
    if (!PageFrameIndex)
    {
        /* No zero pages, grab a free one */
        PageFrameIndex = MiRemoveAnyPage(Color);

        /* Zero it outside the PFN lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        MiZeroPhysicalPage(PageFrameIndex);
        OldIrql = MiLockPfnDb(APC_LEVEL);
    }

    /* Write a valid PTE for it */
    TempPte = ValidKernelPteLocal;
    MI_MAKE_DIRTY_PAGE(&TempPte);
    TempPte.u.Hard.PageFrameNumber = PageFrameIndex;

    /* Initialize the working set list page */
    MiInitializePfnAndMakePteValid(PageFrameIndex, Pte, TempPte);

    /* Now we can release the PFN database lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Fill out the working set structure */
    MmSessionSpace->Vm.Flags.SessionSpace = 1;
    MmSessionSpace->Vm.MinimumWorkingSetSize = 0x14;
    MmSessionSpace->Vm.MaximumWorkingSetSize = 0x180;

    WorkingSetList->LastEntry = 0x14;
    WorkingSetList->HashTable = NULL;
    WorkingSetList->HashTableSize = 0;
    WorkingSetList->Wsle = MmSessionSpace->Wsle;

    /* Acquire the expansion lock while touching the session */
    OldIrql = MiAcquireExpansionLock();

    /* Handle list insertions */
    ASSERT(SessionGlobal->WsListEntry.Flink == NULL);
    ASSERT(SessionGlobal->WsListEntry.Blink == NULL);
    InsertTailList(&MiSessionWsList, &SessionGlobal->WsListEntry);

    ASSERT(SessionGlobal->Vm.WorkingSetExpansionLinks.Flink == NULL);
    ASSERT(SessionGlobal->Vm.WorkingSetExpansionLinks.Blink == NULL);
    InsertTailList(&MmWorkingSetExpansionHead, &SessionGlobal->Vm.WorkingSetExpansionLinks);

    /* Release the lock again */
    MiReleaseExpansionLock(OldIrql);

    /* All done, return */
    //MmUnlockPageableImageSection(ExPageLockHandle);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmSessionCreate(
    _Out_ PULONG SessionId)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PVOID
NTAPI
MmGetSessionById(
    _In_ ULONG SessionId)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
MmAttachSession(
    _Inout_ PVOID SessionEntry,
    _Out_ PKAPC_STATE ApcState)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
MmQuitNextSession(
    _Inout_ PVOID SessionEntry)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmDetachSession(
    _In_ PVOID OpaqueSession,
    _In_ PRKAPC_STATE ApcState)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

ULONG
NTAPI
MmGetSessionId(
    _In_ PEPROCESS Process)
{
    PMM_SESSION_SPACE SessionGlobal;

    DPRINT("MmGetSessionId: Process %X\n", Process);

    /* The session leader is always session zero */
    if (Process->Vm.Flags.SessionLeader == 1)
        return 0;

    /* Otherwise, get the session global, and read the session ID from it */
    SessionGlobal = (PMM_SESSION_SPACE)Process->Session;

    if (!SessionGlobal)
        return 0;

    return SessionGlobal->SessionId;
}

BOOLEAN
NTAPI
MmIsSessionAddress(
    _In_ PVOID Address)
{
    /* Check if it is in range */
    return (MI_IS_SESSION_ADDRESS(Address) ? TRUE : FALSE);
}

VOID
NTAPI
MiInitializeSessionWideAddresses(VOID)
{
    PULONG BitMapBuffer;
    ULONG SizeOfBitMap;
    ULONG Size;

    DPRINT("MiInitializeSessionWideAddresses()\n");

    SizeOfBitMap = (ULONG)((ULONG_PTR)MiSessionImageEnd - (ULONG_PTR)MiSessionImageStart);
    SizeOfBitMap /= PAGE_SIZE;
    Size = (((SizeOfBitMap + 0x1F) / 0x20) * sizeof(ULONG));

    BitMapBuffer = ExAllocatePoolWithTag(PagedPool, Size, '  mM');
    if (!BitMapBuffer)
    {
        KeBugCheckEx(0x7D, MmNumberOfPhysicalPages, MmLowestPhysicalPage, MmHighestPhysicalPage, 0x301);
    }

    RtlInitializeBitMap(&MiSessionWideVaBitMap, BitMapBuffer, SizeOfBitMap);
    RtlClearAllBits(&MiSessionWideVaBitMap);
}

VOID
NTAPI
MiInitializeSessionWsSupport(VOID)
{
    DPRINT("MiInitializeSessionWsSupport()\n");

    /* Initialize the list heads */
    InitializeListHead(&MiSessionWsList);
    InitializeListHead(&MmWorkingSetExpansionHead);
}

VOID
NTAPI
MiInitializeSessionIds(VOID)
{
    PFN_NUMBER TotalPages;
    ULONG BitmapSize;
    ULONG Size;

    DPRINT("MiInitializeSessionIds()\n");

    /* Setup the total number of data pages needed for the structure */
    MiSessionDataPages = (ROUND_TO_PAGES(sizeof(MM_SESSION_SPACE)) >> PAGE_SHIFT);
    ASSERT(MiSessionDataPages <= (MI_SESSION_DATA_PAGES_MAXIMUM - 3));

    TotalPages = (MI_SESSION_DATA_PAGES_MAXIMUM - MiSessionDataPages);

    /* Setup the number of pages needed for session pool tags */
    MiSessionTagSizePages = 2;
    MiSessionBigPoolPages = 1;
    MiSessionTagPages = (MiSessionTagSizePages + MiSessionBigPoolPages);

    ASSERT(MiSessionTagPages <= TotalPages);
    ASSERT(MiSessionTagPages < MI_SESSION_TAG_PAGES_MAXIMUM);

    /* Total pages needed for a session (FIXME: Probably different on PAE/x64) */
    MiSessionCreateCharge = (1 + MiSessionDataPages + MiSessionTagPages);

    /* Initialize the lock */
    KeInitializeGuardedMutex(&MiSessionIdMutex);

    /* Allocate the bitmap */
    BitmapSize = (((MI_INITIAL_SESSION_IDS + 0x1F) / 0x20) * sizeof(ULONG));
    Size = sizeof(RTL_BITMAP) + BitmapSize;

    MiSessionIdBitmap = ExAllocatePoolWithTag(PagedPool, Size, TAG_MM);
    if (!MiSessionIdBitmap)
    {
        /* Die if we couldn't allocate the bitmap */
        KeBugCheckEx(INSTALL_MORE_MEMORY,
                     MmNumberOfPhysicalPages,
                     MmLowestPhysicalPage,
                     MmHighestPhysicalPage,
                     0x200);
        return;
    }

    /* Free all the bits */
    RtlInitializeBitMap(MiSessionIdBitmap, (PVOID)(MiSessionIdBitmap + 1), MI_INITIAL_SESSION_IDS);
    RtlClearAllBits(MiSessionIdBitmap);
}

VOID
NTAPI
MiSessionAddProcess(
    _In_ PEPROCESS NewProcess)
{
    PMM_SESSION_SPACE SessionGlobal;
    KIRQL OldIrql;

    /* The current process must already be in a session */
    if (!(PsGetCurrentProcess()->Flags & PSF_PROCESS_IN_SESSION_BIT))
        return;

    /* Sanity check */
    ASSERT(MmIsAddressValid(MmSessionSpace) == TRUE);

    /* Get the global session */
    SessionGlobal = MmSessionSpace->GlobalVirtualAddress;

    /* Increment counters */
    InterlockedIncrement((PLONG)&SessionGlobal->ReferenceCount);
    InterlockedIncrement(&SessionGlobal->ResidentProcessCount);
    InterlockedIncrement(&SessionGlobal->ProcessReferenceToSession);

    /* Set the session pointer */
    ASSERT(NewProcess->Session == NULL);
    NewProcess->Session = SessionGlobal;

    /* Acquire the expansion lock while touching the session */
    OldIrql = MiAcquireExpansionLock();

    /* Insert it into the process list */
    InsertTailList(&SessionGlobal->ProcessList, &NewProcess->SessionProcessLinks);

    /* Release the lock again */
    MiReleaseExpansionLock(OldIrql);

    /* Set the flag */
    PspSetProcessFlag(NewProcess, PSF_PROCESS_IN_SESSION_BIT);
}

/* EOF */
