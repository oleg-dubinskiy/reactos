
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
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
    PEPROCESS CurrentProcess;

    PAGED_CODE();

    /* Get the current process and check if it is in a session */
    CurrentProcess = PsGetCurrentProcess();

    if (!CurrentProcess->Vm.Flags.SessionLeader && CurrentProcess->Session)
        /* Set the session locale Id */
        ((PMM_SESSION_SPACE)CurrentProcess->Session)->LocaleId = LocaleId;
    else
        /* Set the default locale */
        PsDefaultThreadLocaleId = LocaleId;
}

NTSTATUS
NTAPI
MmSessionDelete(
    _In_ ULONG SessionId)
{
    PEPROCESS Process = PsGetCurrentProcess();

    DPRINT1("MmSessionDelete: SessionId %X\n", SessionId);

    /* Process must be in a session */
    if (!(Process->Flags & PSF_PROCESS_IN_SESSION_BIT))
    {
        DPRINT1("MmSessionDelete: Not in a session!\n");
        return STATUS_UNABLE_TO_FREE_VM;
    }

    /* It must be the session leader */
    if (!Process->Vm.Flags.SessionLeader)
    {
        DPRINT1("MmSessionDelete: Not a session leader!\n");
        return STATUS_UNABLE_TO_FREE_VM;
    }

    /* Remove one reference count */
    KeEnterCriticalRegion();

    /* FIXME: Do it */

    KeLeaveCriticalRegion();

    /* All done */
    return STATUS_SUCCESS;
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
        if (MmAvailablePages < 0x80)
            MiEnsureAvailablePageOrWait(NULL, OldIrql);

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

    if (MmAvailablePages < 0x80)
        MiEnsureAvailablePageOrWait(NULL, OldIrql);

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
MiSessionCreateInternal(
    _Out_ ULONG* OutSessionId)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PFN_NUMBER TagPage[MI_SESSION_TAG_PAGES_MAXIMUM];
    PFN_NUMBER DataPage[MI_SESSION_DATA_PAGES_MAXIMUM];
    PMM_SESSION_SPACE SessionGlobal;
    PMMPDE Pde;
    PMMPDE PageTables;
    PMMPTE Pte;
    PMMPTE SessionPte;
    MMPDE TempPde;
    MMPTE TempPte;
    PFN_NUMBER SessionPageDirIndex;
    ULONG NewFlags;
    ULONG Flags;
    ULONG Size;
    ULONG Color;
    ULONG ix;
    KIRQL OldIrql;
    BOOLEAN Result;
    NTSTATUS Status;

    DPRINT("MiSessionCreateInternal()\n");

    /* This should not exist yet */
    ASSERT(MmIsAddressValid(MmSessionSpace) == FALSE);

    /* Loop so we can set the session-is-creating flag */
    Flags = Process->Flags;

    while (TRUE)
    {
        /* Check if it's already set */
        if (Flags & PSF_SESSION_CREATION_UNDERWAY_BIT)
        {
            /* Bail out */
            DPRINT1("MiSessionCreateInternal: Lost session race\n");
            return STATUS_ALREADY_COMMITTED;
        }

        /* Now try to set it */
        NewFlags = InterlockedCompareExchange((PLONG)&Process->Flags,
                                              (Flags | PSF_SESSION_CREATION_UNDERWAY_BIT),
                                              Flags);
        if (NewFlags == Flags)
            break;

        /* It changed, try again */
        Flags = NewFlags;
    }

    /* Now we should own the flag */
    ASSERT(Process->Flags & PSF_SESSION_CREATION_UNDERWAY_BIT);

    /* Session space covers everything from 0xA0000000 to 0xC0000000.
       Allocate enough page tables to describe the entire region
    */
    Size = (0x20000000 / PDE_MAPPED_VA) * sizeof(MMPTE);

    PageTables = ExAllocatePoolWithTag(NonPagedPool, Size, 'tHmM');
    ASSERT(PageTables != NULL);

    RtlZeroMemory(PageTables, Size);

    /* Lock the session ID creation mutex */
    KeAcquireGuardedMutex(&MiSessionIdMutex);

    /* Allocate a new Session ID */
    *OutSessionId = RtlFindClearBitsAndSet(MiSessionIdBitmap, 1, 0);
    if (*OutSessionId == 0xFFFFFFFF)
    {
        /* We ran out of session IDs, we should expand */
        DPRINT1("MiSessionCreateInternal: Too many sessions created. Expansion not yet supported\n");
        ExFreePoolWithTag(PageTables, 'tHmM');
        return STATUS_NO_MEMORY;
    }

    /* Unlock the session ID creation mutex */
    KeReleaseGuardedMutex(&MiSessionIdMutex);

    /* Reserve the global PTEs */
    SessionPte = MiReserveSystemPtes(MiSessionDataPages, SystemPteSpace);
    ASSERT(SessionPte != NULL);

    /* Acquire the PFN lock while we set everything up */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* Loop the global PTEs */
    TempPte = ValidKernelPte;

    for (ix = 0; ix < MiSessionDataPages; ix++)
    {
        if (MmAvailablePages < 0x80)
            MiEnsureAvailablePageOrWait(NULL, OldIrql);

        /* Get a zeroed colored zero page */
        MI_SET_USAGE(MI_USAGE_INIT_MEMORY);
        Color = MI_GET_NEXT_COLOR();

        DataPage[ix] = MiRemoveZeroPageSafe(Color);
        if (!DataPage[ix])
        {
            /* No zero pages, grab a free one */
            DataPage[ix] = MiRemoveAnyPage(Color);

            /* Zero it outside the PFN lock */
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            MiZeroPhysicalPage(DataPage[ix]);
            OldIrql = MiLockPfnDb(APC_LEVEL);
        }

        /* Fill the PTE out */
        TempPte.u.Hard.PageFrameNumber = DataPage[ix];
        MI_WRITE_VALID_PTE((SessionPte + ix), TempPte);
    }

    /* Set the pointer to global space */
    SessionGlobal = MiPteToAddress(SessionPte);

    if (MmAvailablePages < 0x80)
        MiEnsureAvailablePageOrWait(NULL, OldIrql);

    /* Get a zeroed colored zero page */
    MI_SET_USAGE(MI_USAGE_INIT_MEMORY);
    Color = MI_GET_NEXT_COLOR();

    SessionPageDirIndex = MiRemoveZeroPageSafe(Color);
    if (!SessionPageDirIndex)
    {
        /* No zero pages, grab a free one */
        SessionPageDirIndex = MiRemoveAnyPage(Color);

        /* Zero it outside the PFN lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        MiZeroPhysicalPage(SessionPageDirIndex);
        OldIrql = MiLockPfnDb(APC_LEVEL);
    }

    /* Fill the PTE out */
    TempPde = ValidKernelPdeLocal;
    TempPde.u.Hard.PageFrameNumber = SessionPageDirIndex;

    /* Setup, allocate, fill out the MmSessionSpace PTE */
    Pde = MiAddressToPde(MmSessionSpace);
    ASSERT(Pde->u.Long == 0);
    MI_WRITE_VALID_PDE(Pde, TempPde);

    MiInitializePfnForOtherProcess(SessionPageDirIndex, Pde, SessionPageDirIndex);
    ASSERT(MI_PFN_ELEMENT(SessionPageDirIndex)->u1.WsIndex == 0);

     /* Loop all the local PTEs for it */
    TempPte = ValidKernelPteLocal;
    Pte = MiAddressToPte(MmSessionSpace);

    for (ix = 0; ix < MiSessionDataPages; ix++)
    {
        /* And fill them out */
        TempPte.u.Hard.PageFrameNumber = DataPage[ix];
        MiInitializePfnAndMakePteValid(DataPage[ix], Pte + ix, TempPte);
        ASSERT(MI_PFN_ELEMENT(DataPage[ix])->u1.WsIndex == 0);
    }

     /* Finally loop all of the session pool tag pages */
    for (ix = 0; ix < MiSessionTagPages; ix++)
    {
        if (MmAvailablePages < 0x80)
            MiEnsureAvailablePageOrWait(NULL, OldIrql);

        /* Grab a zeroed colored page */
        MI_SET_USAGE(MI_USAGE_INIT_MEMORY);
        Color = MI_GET_NEXT_COLOR();

        TagPage[ix] = MiRemoveZeroPageSafe(Color);
        if (!TagPage[ix])
        {
            /* No zero pages, grab a free one */
            TagPage[ix] = MiRemoveAnyPage(Color);

            /* Zero it outside the PFN lock */
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            MiZeroPhysicalPage(TagPage[ix]);
            OldIrql = MiLockPfnDb(APC_LEVEL);
        }

        /* Fill the PTE out */
        TempPte.u.Hard.PageFrameNumber = TagPage[ix];

        MiInitializePfnAndMakePteValid(TagPage[ix],
                                       (Pte + MiSessionDataPages + ix),
                                       TempPte);
    }

    /* PTEs have been setup, release the PFN lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Fill out the session space structure now */
    MmSessionSpace->GlobalVirtualAddress = SessionGlobal;
    MmSessionSpace->ReferenceCount = 1;
    MmSessionSpace->ResidentProcessCount = 1;
    MmSessionSpace->u.LongFlags = 0;
    MmSessionSpace->SessionId = *OutSessionId;
    MmSessionSpace->LocaleId = PsDefaultSystemLocaleId;
    MmSessionSpace->SessionPageDirectoryIndex = SessionPageDirIndex;
    MmSessionSpace->Color = Color;
    MmSessionSpace->NonPageablePages = MiSessionCreateCharge;
    MmSessionSpace->CommittedPages = MiSessionCreateCharge;
#ifndef _M_AMD64
    MmSessionSpace->PageTables = PageTables;
    MmSessionSpace->PageTables[Pde - MiAddressToPde(MmSessionBase)] = *Pde;
#endif

    InitializeListHead(&MmSessionSpace->ImageList);

    DPRINT1("MiSessionCreateInternal: Session %X is ready to go: %p, %p, %X, %p\n", *OutSessionId, MmSessionSpace, SessionGlobal, SessionPageDirIndex, PageTables);

    /* Initialize session pool */
    //Status = MiInitializeSessionPool();
    Status = STATUS_SUCCESS;
    ASSERT(NT_SUCCESS(Status) == TRUE);

    /* Initialize system space */
    Result = MiInitializeSystemSpaceMap(&SessionGlobal->Session);
    ASSERT(Result == TRUE);

    /* Initialize the process list, make sure the workign set list is empty */
    ASSERT(SessionGlobal->WsListEntry.Flink == NULL);
    ASSERT(SessionGlobal->WsListEntry.Blink == NULL);

    InitializeListHead(&SessionGlobal->ProcessList);

    /* We're done, clear the flag */
    ASSERT(Process->Flags & PSF_SESSION_CREATION_UNDERWAY_BIT);
    PspClearProcessFlag(Process, PSF_SESSION_CREATION_UNDERWAY_BIT);

    /* Insert the process into the session  */
    ASSERT(Process->Session == NULL);
    ASSERT(SessionGlobal->ProcessReferenceToSession == 0);
    SessionGlobal->ProcessReferenceToSession = 1;

    /* We're done */
    InterlockedIncrement(&MmSessionDataPages);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmSessionCreate(
    _Out_ PULONG SessionId)
{
    PEPROCESS Process = PsGetCurrentProcess();
    ULONG SessionLeaderExists;
    NTSTATUS Status;

    DPRINT("MmSessionCreate()\n");

    /* Fail if the process is already in a session */
    if (Process->Flags & PSF_PROCESS_IN_SESSION_BIT)
    {
        DPRINT1("MmSessionCreate: Process already in session\n");
        return STATUS_ALREADY_COMMITTED;
    }

    /* Check if the process is already the session leader */
    if (!Process->Vm.Flags.SessionLeader)
    {
        /* Atomically set it as the leader */
        SessionLeaderExists = InterlockedCompareExchange(&MiSessionLeaderExists, 1, 0);
        if (SessionLeaderExists)
        {
            DPRINT1("MmSessionCreate: Session leader race\n");
            return STATUS_INVALID_SYSTEM_SERVICE;
        }

        /* Do the work required to upgrade him */
        MiSessionLeader(Process);
    }

    /* Create the session */
    KeEnterCriticalRegion();

    Status = MiSessionCreateInternal(SessionId);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmSessionCreate: Status %X\n", Status);
        KeLeaveCriticalRegion();
        return Status;
    }

    /* Set up the session working set */
    Status = MiSessionInitializeWorkingSetList();
    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        //MiDereferenceSession();
        ASSERT(FALSE);

        DPRINT1("MmSessionCreate: Status %X\n", Status);
        KeLeaveCriticalRegion();
        return Status;
    }

    /* All done */
    KeLeaveCriticalRegion();

    /* Set and assert the flags, and return */
    MmSessionSpace->u.Flags.Initialized = 1;
    PspSetProcessFlag(Process, PSF_PROCESS_IN_SESSION_BIT);

    ASSERT(MiSessionLeaderExists == 1);

    return Status;
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

VOID
NTAPI
MiDereferenceSessionFinal(VOID)
{
    PMM_SESSION_SPACE SessionGlobal;
    KIRQL OldIrql;

    /* Get the pointer to the global session address */
    SessionGlobal = MmSessionSpace->GlobalVirtualAddress;

    /* Acquire the expansion lock */
    OldIrql = MiAcquireExpansionLock();

    /* Set delete pending flag, so that processes can no longer attach to this session
       and the last process that detaches sets the AttachEvent
    */
    ASSERT(SessionGlobal->u.Flags.DeletePending == 0);
    SessionGlobal->u.Flags.DeletePending = 1;

    /* Check if we have any attached processes */
    if (SessionGlobal->AttachCount)
    {
        /* Initialize the event (it's not in use yet!) */
        KeInitializeEvent(&SessionGlobal->AttachEvent, NotificationEvent, FALSE);

        /* Release the expansion lock for the wait */
        MiReleaseExpansionLock(OldIrql);

        /* Wait for the event to be set due to the last process detach */
        KeWaitForSingleObject(&SessionGlobal->AttachEvent, WrVirtualMemory, KernelMode, FALSE, NULL);

        /* Reacquire the expansion lock */
        OldIrql = MiAcquireExpansionLock();

        /* Makes sure we still have the delete flag and no attached processes */
        ASSERT(MmSessionSpace->u.Flags.DeletePending == 1);
        ASSERT(MmSessionSpace->AttachCount == 0);
    }

    /* Check if the session is in the workingset expansion list */
    if (SessionGlobal->Vm.WorkingSetExpansionLinks.Flink)
    {
        /* Remove the session from the list and zero the list entry */
        RemoveEntryList(&SessionGlobal->Vm.WorkingSetExpansionLinks);
        SessionGlobal->Vm.WorkingSetExpansionLinks.Flink = 0;
    }

    /* Check if the session is in the workingset list */
    if (SessionGlobal->WsListEntry.Flink)
    {
        /* Remove the session from the list and zero the list entry */
        RemoveEntryList(&SessionGlobal->WsListEntry);
        SessionGlobal->WsListEntry.Flink = NULL;
    }

    /* Release the expansion lock */
    MiReleaseExpansionLock(OldIrql);

    /* Check for a win32k unload routine */
    if (SessionGlobal->Win32KDriverUnload)
        /* Call it */
        SessionGlobal->Win32KDriverUnload(NULL);
}

VOID
NTAPI
MiReleaseProcessReferenceToSessionDataPage(
    _In_ PMM_SESSION_SPACE SessionGlobal)
{
    PFN_NUMBER PageFrameIndex[MI_SESSION_DATA_PAGES_MAXIMUM];
    PMMPTE Pte;
    PMMPFN Pfn;
    ULONG SessionId;
    ULONG ix;
    KIRQL OldIrql;

    /* Is there more than just this reference? If so, bail out */
    if (InterlockedDecrement(&SessionGlobal->ProcessReferenceToSession))
        return;

    /* Get the session ID */
    SessionId = SessionGlobal->SessionId;

    DPRINT1("MiReleaseProcessReferenceToSessionDataPage: Last process in session %X going down!!!\n", SessionId);

    /* Free the session page tables */
    ExFreePoolWithTag(SessionGlobal->PageTables, 'tHmM');
    ASSERT(!MI_IS_PHYSICAL_ADDRESS(SessionGlobal));

    /* Capture the data page PFNs */
    Pte = MiAddressToPte(SessionGlobal);

    for (ix = 0; ix < MiSessionDataPages; ix++)
        PageFrameIndex[ix] = PFN_FROM_PTE(Pte + ix);

    /* Release them */
    MiReleaseSystemPtes(Pte, MiSessionDataPages, SystemPteSpace);

    /* Mark them as deleted */
    for (ix = 0; ix < MiSessionDataPages; ix++)
    {
        Pfn = MI_PFN_ELEMENT(PageFrameIndex[ix]);
        MI_SET_PFN_DELETED(Pfn);
    }

    /* Loop every data page and drop a reference count */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    for (ix = 0; ix < MiSessionDataPages; ix++)
    {
        /* Sanity check that the page is correct, then decrement it */
        Pfn = MI_PFN_ELEMENT(PageFrameIndex[ix]);

        ASSERT(Pfn->u2.ShareCount == 1);
        ASSERT(Pfn->u3.e2.ReferenceCount == 1);

        MiDecrementShareCount(Pfn, PageFrameIndex[ix]);
    }

    /* Done playing with pages, release the lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Decrement the number of data pages */
    InterlockedDecrement(&MmSessionDataPages);

    /* Free this session ID from the session bitmap */
    KeAcquireGuardedMutex(&MiSessionIdMutex);

    ASSERT(RtlCheckBit(MiSessionIdBitmap, SessionId));
    RtlClearBit(MiSessionIdBitmap, SessionId);

    KeReleaseGuardedMutex(&MiSessionIdMutex);
}

VOID
NTAPI
MiDereferenceSession(VOID)
{
    PMM_SESSION_SPACE SessionGlobal;
    PEPROCESS Process;
    ULONG ReferenceCount;
    ULONG SessionId;

    /* Sanity checks */
    ASSERT(PsGetCurrentProcess()->ProcessInSession ||
           (!MmSessionSpace->u.Flags.Initialized &&
            PsGetCurrentProcess()->Vm.Flags.SessionLeader &&
            MmSessionSpace->ReferenceCount == 1));

    /* The session bit must be set */
    SessionId = MmSessionSpace->SessionId;

    ASSERT(RtlCheckBit(MiSessionIdBitmap, SessionId));

    /* Get the current process */
    Process = PsGetCurrentProcess();

    /* Decrement the process count */
    InterlockedDecrement(&MmSessionSpace->ResidentProcessCount);

    /* Decrement the reference count and check if was the last reference */
    ReferenceCount = InterlockedDecrement(&MmSessionSpace->ReferenceCount);
    if (!ReferenceCount)
        /* No more references left, kill the session completely */
        MiDereferenceSessionFinal();

    /* Check if tis is the session leader or the last process in the session */
    if (!Process->Vm.Flags.SessionLeader && ReferenceCount)
        goto Exit;

    /* Get the global session address before we kill the session mapping */
    SessionGlobal = MmSessionSpace->GlobalVirtualAddress;

    /* Delete all session PDEs and flush the TB */
    RtlZeroMemory(MiAddressToPde(MmSessionBase), BYTES_TO_PAGES(MmSessionSize) * sizeof(MMPDE));

    KeFlushEntireTb(FALSE, FALSE);

    /* Is this the session leader? */
    if (Process->Vm.Flags.SessionLeader)
    {
        /* Clean up the references here. */
        ASSERT(Process->Session == NULL);
        MiReleaseProcessReferenceToSessionDataPage(SessionGlobal);
    }

Exit:

    /* Reset the current process' session flag */
    RtlInterlockedClearBits(&Process->Flags, PSF_PROCESS_IN_SESSION_BIT);
}

VOID
NTAPI
MiSessionRemoveProcess(VOID)
{
    PEPROCESS CurrentProcess = PsGetCurrentProcess();
    KIRQL OldIrql;

    DPRINT("MiSessionRemoveProcess()\n");

    /* If the process isn't already in a session, or if it's the leader... */
    if (!(CurrentProcess->Flags & PSF_PROCESS_IN_SESSION_BIT) || CurrentProcess->Vm.Flags.SessionLeader)
        /* Then there's nothing to do */
        return;

    /* Sanity check */
    ASSERT(MmIsAddressValid(MmSessionSpace) == TRUE);

    /* Acquire the expansion lock while touching the session */
    OldIrql = MiAcquireExpansionLock();

    /* Remove the process from the list */
    RemoveEntryList(&CurrentProcess->SessionProcessLinks);

    /* Release the lock again */
    MiReleaseExpansionLock(OldIrql);

    /* Dereference the session */
    MiDereferenceSession();
}

/* EOF */
