
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

KGUARDED_MUTEX MmSectionCommitMutex;
KGUARDED_MUTEX MmSectionBasedMutex;
KEVENT MmCollidedFlushEvent;
PVOID MmHighSectionBase;
LIST_ENTRY MmUnusedSubsectionList;
MMSESSION MmSession;
MM_AVL_TABLE MmSectionBasedRoot;

CHAR MmUserProtectionToMask1[16] =
{
    0,
    MM_NOACCESS,
    MM_READONLY,
    (CHAR)MM_INVALID_PROTECTION,
    MM_READWRITE,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    MM_WRITECOPY,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION
};

CHAR MmUserProtectionToMask2[16] =
{
    0,
    MM_EXECUTE,
    MM_EXECUTE_READ,
    (CHAR)MM_INVALID_PROTECTION,
    MM_EXECUTE_READWRITE,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    MM_EXECUTE_WRITECOPY,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION,
    (CHAR)MM_INVALID_PROTECTION
};

ULONG MmCompatibleProtectionMask[8] =
{
    PAGE_NOACCESS,
    PAGE_NOACCESS | PAGE_READONLY | PAGE_WRITECOPY,
    PAGE_NOACCESS | PAGE_EXECUTE,
    PAGE_NOACCESS | PAGE_READONLY | PAGE_WRITECOPY | PAGE_EXECUTE | PAGE_EXECUTE_READ,
    PAGE_NOACCESS | PAGE_READONLY | PAGE_WRITECOPY | PAGE_READWRITE,
    PAGE_NOACCESS | PAGE_READONLY | PAGE_WRITECOPY,
    PAGE_NOACCESS | PAGE_READONLY | PAGE_WRITECOPY | PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_WRITECOPY | PAGE_READWRITE | PAGE_EXECUTE_READWRITE,
    PAGE_NOACCESS | PAGE_READONLY | PAGE_WRITECOPY | PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_WRITECOPY
};

extern PVOID MiSessionViewStart;   // 0xBE000000
extern SIZE_T MmSessionViewSize;
extern PVOID MiSystemViewStart;
extern SIZE_T MmSystemViewSize;
extern LARGE_INTEGER MmHalfSecond;
extern SIZE_T MmSharedCommit;
extern SIZE_T MmTotalCommittedPages;
extern MMPDE ValidKernelPde;
extern PFN_NUMBER MmAvailablePages;
extern ULONG MmSecondaryColorMask;

/* FUNCTIONS ******************************************************************/

ULONG
NTAPI
MiMakeProtectionMask(IN ULONG Protect)
{
    ULONG Mask1;
    ULONG Mask2;
    ULONG ProtectMask;

    DPRINT("MiMakeProtectionMask: Protect %X\n", Protect);

    /* PAGE_EXECUTE_WRITECOMBINE is theoretically the maximum */
    if (Protect >= (PAGE_WRITECOMBINE * 2))
        return MM_INVALID_PROTECTION;

    /* Windows API protection mask can be understood as two bitfields,
       differing by whether or not execute rights are being requested
    */
    Mask1 = Protect & 0xF;
    Mask2 = (Protect >> 4) & 0xF;

    /* Check which field is there */
    if (!Mask1)
    {
        /* Mask2 must be there, use it to determine the PTE protection */
        if (!Mask2)
            return MM_INVALID_PROTECTION;

        ProtectMask = MmUserProtectionToMask2[Mask2];
    }
    else
    {
        /* Mask2 should not be there, use Mask1 to determine the PTE mask */
        if (Mask2)
            return MM_INVALID_PROTECTION;

        ProtectMask = MmUserProtectionToMask1[Mask1];
    }

    /* Make sure the final mask is a valid one */
    if (ProtectMask == MM_INVALID_PROTECTION)
        return MM_INVALID_PROTECTION;

    /* Check for PAGE_GUARD option */
    if (Protect & PAGE_GUARD)
    {
        /* It's not valid on no-access, nocache, or writecombine pages */
        if (ProtectMask == MM_NOACCESS || (Protect & (PAGE_NOCACHE | PAGE_WRITECOMBINE)))
            /* Fail such requests */
            return MM_INVALID_PROTECTION;

        /* This actually turns on guard page in this scenario! */
        ProtectMask |= MM_GUARDPAGE;
    }

    /* Check for nocache option */
    if (Protect & PAGE_NOCACHE)
    {
        /* The earlier check should've eliminated this possibility */
        ASSERT((Protect & PAGE_GUARD) == 0);

        /* Check for no-access page or write combine page */
        if (ProtectMask == MM_NOACCESS || (Protect & PAGE_WRITECOMBINE))
            /* Such a request is invalid */
            return MM_INVALID_PROTECTION;

        /* Add the PTE flag */
        ProtectMask |= MM_NOCACHE;
    }

    /* Check for write combine option */
    if (Protect & PAGE_WRITECOMBINE)
    {
        /* The two earlier scenarios should've caught this */
        ASSERT((Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0);

        /* Don't allow on no-access pages */
        if (ProtectMask == MM_NOACCESS)
            return MM_INVALID_PROTECTION;

        /* This actually turns on write-combine in this scenario! */
        ProtectMask |= MM_NOACCESS;
    }

    /* Return the final MM PTE protection mask */
    return ProtectMask;
}

NTSTATUS
NTAPI
MmGetFileNameForAddress(
    _In_ PVOID Address,
    _Out_ PUNICODE_STRING ModuleName)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmGetFileNameForSection(
    _In_ PVOID Section,
    _Out_ POBJECT_NAME_INFORMATION* ModuleName)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PFILE_OBJECT
NTAPI
MmGetFileObjectForSection(
    _In_ PVOID SectionObject)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmGetImageInformation (
    _Out_ PSECTION_IMAGE_INFORMATION ImageInformation)
{
    UNIMPLEMENTED_DBGBREAK();
}

BOOLEAN
NTAPI
MiInitializeSystemSpaceMap(
    _In_ PMMSESSION InputSession OPTIONAL)
{
    PMMSESSION Session;
    PVOID ViewStart;
    SIZE_T AllocSize;
    SIZE_T BitmapSize;
    SIZE_T Size;
    POOL_TYPE PoolType;

    DPRINT("MiInitializeSystemSpaceMap: InputSession %p\n", InputSession);

    /* Check if this a session or system space */
    if (InputSession)
    {
        /* Use the input session */
        Session = InputSession;
        ViewStart = MiSessionViewStart;
        Size = MmSessionViewSize;
    }
    else
    {
        /* Use the system space "session" */
        Session = &MmSession;
        ViewStart = MiSystemViewStart;
        Size = MmSystemViewSize;
    }

    /* Initialize the system space lock */
    Session->SystemSpaceViewLockPointer = &Session->SystemSpaceViewLock;
    KeInitializeGuardedMutex(Session->SystemSpaceViewLockPointer);

    /* Set the start address */
    Session->SystemSpaceViewStart = ViewStart;

    /* Create a bitmap to describe system space */
    BitmapSize = sizeof(RTL_BITMAP);
    BitmapSize += ((((Size / MI_SYSTEM_VIEW_BUCKET_SIZE) + 0x1F) / 0x20) * sizeof(ULONG));

    Session->SystemSpaceBitMap = ExAllocatePoolWithTag(NonPagedPool, BitmapSize, TAG_MM);
    ASSERT(Session->SystemSpaceBitMap);

    RtlInitializeBitMap(Session->SystemSpaceBitMap,
                        (PULONG)(Session->SystemSpaceBitMap + 1),
                        (ULONG)(Size / MI_SYSTEM_VIEW_BUCKET_SIZE));

    /* Set system space fully empty to begin with */
    RtlClearAllBits(Session->SystemSpaceBitMap);

    /* Set default hash flags */
    Session->SystemSpaceHashSize = 0x1F;
    Session->SystemSpaceHashKey = (Session->SystemSpaceHashSize - 1);
    Session->SystemSpaceHashEntries = 0;

    /* Calculate how much space for the hash views we'll need */
    AllocSize = (sizeof(MMVIEW) * Session->SystemSpaceHashSize);
    ASSERT(AllocSize < PAGE_SIZE);

    /* Allocate and zero the view table */
    PoolType = (Session == &MmSession ? NonPagedPool : PagedPool);

    Session->SystemSpaceViewTable = ExAllocatePoolWithTag(PoolType, AllocSize, TAG_MM);
    ASSERT(Session->SystemSpaceViewTable != NULL);

    RtlZeroMemory(Session->SystemSpaceViewTable, AllocSize);

    return TRUE;
}

VOID
NTAPI
MiCheckControlArea(
    _In_ PCONTROL_AREA ControlArea,
    _In_ KIRQL OldIrql)
{
    UNIMPLEMENTED_DBGBREAK();
}

PMMPTE
NTAPI
MiGetProtoPteAddressExtended(
    _In_ PMMVAD Vad,
    _In_ ULONG_PTR Vpn)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

NTSTATUS
NTAPI
MiCreatePagingFileMap(
    _Out_ PSEGMENT* Segment,
    _In_ PULONGLONG MaximumSize,
    _In_ ULONG ProtectionMask,
    _In_ ULONG AllocationAttributes)
{
    PCONTROL_AREA ControlArea;
    PSEGMENT NewSegment;
    PSUBSECTION Subsection;
    PMMPTE SectionProto;
    MMPTE TempPte;
    ULONGLONG SizeLimit;
    PFN_COUNT ProtoCount;

    PAGED_CODE();
    DPRINT("MiCreatePagingFileMap: MaximumSize %I64X, Protection %X, AllocAttributes %X\n",
           (!MaximumSize ? 0ull : (*MaximumSize)), ProtectionMask, AllocationAttributes);

    /* Pagefile-backed sections need a known size */
    if (!(*MaximumSize))
    {
        DPRINT1("MiCreatePagingFileMap: STATUS_INVALID_PARAMETER_4 \n");
        return STATUS_INVALID_PARAMETER_4;
    }

    /* Calculate the maximum size possible, given the section protos we'll need */
    SizeLimit = (((MAXULONG_PTR - sizeof(SEGMENT)) / sizeof(MMPTE)) * PAGE_SIZE);

    /* Fail if this size is too big */
    if (*MaximumSize > SizeLimit)
    {
        DPRINT1("MiCreatePagingFileMap: SizeLimit %I64X, STATUS_SECTION_TOO_BIG\n", SizeLimit);
        return STATUS_SECTION_TOO_BIG;
    }

    /* Calculate how many section protos will be needed */
    ProtoCount = (PFN_COUNT)((*MaximumSize + (PAGE_SIZE - 1)) / PAGE_SIZE);

    if (AllocationAttributes & SEC_COMMIT)
    {
        /* For commited memory, we must have a valid protection mask */
        ASSERT(ProtectionMask != 0);

        DPRINT("MiCreatePagingFileMap: FIXME MiChargeCommitment \n");

        /* No large pages in ARM3 yet */
        if (AllocationAttributes & SEC_LARGE_PAGES)
        {
            if (!(KeFeatureBits & KF_LARGE_PAGE))
            {
                DPRINT1("MiCreatePagingFileMap: STATUS_NOT_SUPPORTED \n");
                return STATUS_NOT_SUPPORTED;
            }

            DPRINT1("MiCreatePagingFileMap: AllocationAttributes & SEC_LARGE_PAGES\n");
            ASSERT(FALSE);
        }
    }

    /* The segment contains all the section protos, allocate it in paged pool */
    NewSegment = ExAllocatePoolWithTag(PagedPool, (sizeof(SEGMENT) + (ProtoCount - 1) * sizeof(MMPTE)), 'tSmM');
    if (!NewSegment)
    {
        DPRINT1("MiCreatePagingFileMap: STATUS_INSUFFICIENT_RESOURCES \n");
        ASSERT(FALSE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *Segment = NewSegment;

    /* Now allocate the control area, which has the subsection structure */
    ControlArea = ExAllocatePoolWithTag(NonPagedPool, (sizeof(CONTROL_AREA) + sizeof(SUBSECTION)), 'aCmM');
    if (!ControlArea)
    {
        DPRINT1("MiCreatePagingFileMap: STATUS_INSUFFICIENT_RESOURCES \n");
        ExFreePoolWithTag(NewSegment, 'tSmM');
        ASSERT(FALSE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* And zero it out, filling the basic segmnet pointer and reference fields */
    RtlZeroMemory(ControlArea, (sizeof(CONTROL_AREA) + sizeof(SUBSECTION)));

    ControlArea->Segment = NewSegment;
    ControlArea->NumberOfSectionReferences = 1;
    ControlArea->NumberOfUserReferences = 1;

    /* Convert allocation attributes to control area flags */
    if (AllocationAttributes & SEC_BASED)
        ControlArea->u.Flags.Based = 1;

    if (AllocationAttributes & SEC_RESERVE)
        ControlArea->u.Flags.Reserve = 1;

    if (AllocationAttributes & SEC_COMMIT)
        ControlArea->u.Flags.Commit = 1;

    /* The subsection follows, write the mask, PTE count and point back to the CA */
    Subsection = (PSUBSECTION)(ControlArea + 1);
    Subsection->ControlArea = ControlArea;
    Subsection->PtesInSubsection = ProtoCount;
    Subsection->u.SubsectionFlags.Protection = ProtectionMask;

    /* Zero out the segment's section protos, and link it with the control area */
    SectionProto = &NewSegment->ThePtes[0];

    RtlZeroMemory(NewSegment, sizeof(SEGMENT));

    NewSegment->PrototypePte = SectionProto;
    NewSegment->ControlArea = ControlArea;

    /* Save some extra accounting data for the segment as well */
    NewSegment->u1.CreatingProcess = PsGetCurrentProcess();
    NewSegment->SizeOfSegment = (ProtoCount * PAGE_SIZE);
    NewSegment->TotalNumberOfPtes = ProtoCount;
    NewSegment->NonExtendedPtes = ProtoCount;

    /* The subsection's base address is the first section proto in the segment */
    Subsection->SubsectionBase = SectionProto;

    /* Start with an empty PTE, unless this is a commit operation */
    TempPte.u.Long = 0;

    if (AllocationAttributes & SEC_COMMIT)
    {
        /* In which case, write down the protection mask in the section protos */
        TempPte.u.Soft.Protection = ProtectionMask;

        /* For accounting, also mark these pages as being committed */
        NewSegment->NumberOfCommittedPages = ProtoCount;

        InterlockedExchangeAdd((volatile PLONG)&MmSharedCommit, ProtoCount);

        if (AllocationAttributes & SEC_LARGE_PAGES)
        {
            /* No large pages in ARM3 yet */
            DPRINT1("MiCreatePagingFileMap: AllocationAttributes & SEC_LARGE_PAGES\n");
            ASSERT(FALSE);
        }
    }

    /* The template PTE itself for the segment should also have the mask set */
    NewSegment->SegmentPteTemplate.u.Soft.Protection = ProtectionMask;

    /* Write out the section protos, for now they're simply demand zero */
    if (AllocationAttributes & SEC_LARGE_PAGES)
        return STATUS_SUCCESS;

    /* Write out the section protos, for now they're simply demand zero */
  #if defined (_WIN64) || defined (_X86PAE_)
    RtlFillMemoryUlonglong(SectionProto, (ProtoCount * sizeof(MMPTE)), TempPte.u.Long);
  #else
    RtlFillMemoryUlong(SectionProto, (ProtoCount * sizeof(MMPTE)), TempPte.u.Long);
  #endif

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MiCheckPurgeAndUpMapCount(
    _In_ PCONTROL_AREA ControlArea,
    _In_ BOOLEAN FailIfSystemViews)
{
    NTSTATUS Status;
    KIRQL OldIrql;

    DPRINT("MiCheckPurgeAndUpMapCount: ControlArea %p, FailIfSystemViews %X\n", ControlArea, FailIfSystemViews);

    if (FailIfSystemViews)
    {
        ASSERT(ControlArea->u.Flags.Image != 0);
    }

    /* Lock the PFN database */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* State not yet supported */
    if (ControlArea->u.Flags.BeingPurged)
    {
        DPRINT("MiCheckPurgeAndUpMapCount: FIXME! ControlArea->u.Flags.BeingPurged\n");
        ASSERT(FALSE);
    }

    /* Increase the reference counts */
    ControlArea->NumberOfMappedViews++;
    ControlArea->NumberOfUserReferences++;

    ASSERT(ControlArea->NumberOfSectionReferences != 0);

    if (FailIfSystemViews &&
        ControlArea->u.Flags.ImageMappedInSystemSpace &&
        KeGetPreviousMode() != KernelMode)
    {
        /* Release the PFN lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        DPRINT1("MiCheckPurgeAndUpMapCount: STATUS_CONFLICTING_ADDRESSES\n");
        Status = STATUS_CONFLICTING_ADDRESSES;
    }
    else
    {
        /* Increase the reference counts */
        ControlArea->NumberOfMappedViews++;
        ControlArea->NumberOfUserReferences++;

        ASSERT(ControlArea->NumberOfSectionReferences != 0);

        /* Release the PFN lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        Status = STATUS_SUCCESS;
    }

    return Status;
}

VOID
NTAPI
MiDereferenceControlArea(
     _In_ PCONTROL_AREA ControlArea)
{
    UNIMPLEMENTED_DBGBREAK();
}

PVOID
NTAPI
MiInsertInSystemSpace(
    _In_ PMMSESSION Session,
    _In_ ULONG Buckets,
    _In_ PCONTROL_AREA ControlArea)
{
    PMMVIEW OldTable;
    PVOID Base;
    POOL_TYPE PoolType;
    ULONG Entry;
    ULONG Hash;
    ULONG HashSize;
    ULONG ix;

    PAGED_CODE();
    DPRINT("MiInsertInSystemSpace: Session %p, Buckets %X, ControlArea %p\n", Session, Buckets, ControlArea);

    /* Stay within 4GB */
    ASSERT(Buckets < MI_SYSTEM_VIEW_BUCKET_SIZE);

    /* Lock system space */
    KeAcquireGuardedMutex(Session->SystemSpaceViewLockPointer);

    /* Check if we're going to exhaust hash entries */
    if ((Session->SystemSpaceHashEntries + 8) > Session->SystemSpaceHashSize)
    {
        /* Double the hash size */
        HashSize = (Session->SystemSpaceHashSize * 2);

        /* Save the old table and allocate a new one */
        OldTable = Session->SystemSpaceViewTable;
        PoolType = (Session == &MmSession ? NonPagedPool : PagedPool);

        Session->SystemSpaceViewTable = ExAllocatePoolWithTag(PoolType, (HashSize * sizeof(MMVIEW)), TAG_MM);

        if (!Session->SystemSpaceViewTable)
        {
            /* Failed to allocate a new table, keep the old one for now */
            Session->SystemSpaceViewTable = OldTable;
        }
        else
        {
            /* Clear the new table and set the new ahsh and key */
            RtlZeroMemory(Session->SystemSpaceViewTable, (HashSize * sizeof(MMVIEW)));

            Session->SystemSpaceHashSize = HashSize;
            Session->SystemSpaceHashKey = (Session->SystemSpaceHashSize - 1);

            /* Loop the old table */
            for (ix = 0; ix < (Session->SystemSpaceHashSize / 2); ix++)
            {
                /* Check if the entry was valid */
                if (OldTable[ix].Entry)
                {
                    /* Re-hash the old entry and search for space in the new table */
                    Hash = ((OldTable[ix].Entry >> 16) % Session->SystemSpaceHashKey);

                    while (Session->SystemSpaceViewTable[Hash].Entry)
                    {
                        /* Loop back at the beginning if we had an overflow */
                        if (++Hash >= Session->SystemSpaceHashSize)
                            Hash = 0;
                    }

                    /* Write the old entry in the new table */
                    Session->SystemSpaceViewTable[Hash] = OldTable[ix];
                }
            }

            /* Free the old table */
            ExFreePool(OldTable);
        }
    }

    /* Check if we ran out */
    if (Session->SystemSpaceHashEntries == Session->SystemSpaceHashSize)
    {
        DPRINT1("MiInsertInSystemSpace: Ran out of system view hash entries\n");
        KeReleaseGuardedMutex(Session->SystemSpaceViewLockPointer);
        return NULL;
    }

    /* Find space where to map this view */
    ix = RtlFindClearBitsAndSet(Session->SystemSpaceBitMap, Buckets, 0);
    if (ix == 0xFFFFFFFF)
    {
        /* Out of space, fail */
        DPRINT1("MiInsertInSystemSpace: Out of system view space\n");
        Session->BitmapFailures++;
        KeReleaseGuardedMutex(Session->SystemSpaceViewLockPointer);
        return NULL;
    }

    /* Compute the base address */
    Base = (PVOID)((ULONG_PTR)Session->SystemSpaceViewStart + (ix * MI_SYSTEM_VIEW_BUCKET_SIZE));

    /* Get the hash entry for this allocation */
    Entry = (((ULONG_PTR)Base & ~(MI_SYSTEM_VIEW_BUCKET_SIZE - 1)) + Buckets);
    Hash = ((Entry >> 16) % Session->SystemSpaceHashKey);

    /* Loop hash entries until a free one is found */
    while (Session->SystemSpaceViewTable[Hash].Entry)
    {
        /* Unless we overflow, in which case loop back at hash o */
        if (++Hash >= Session->SystemSpaceHashSize)
            Hash = 0;
    }

    /* Add this entry into the hash table */
    Session->SystemSpaceViewTable[Hash].Entry = Entry;
    Session->SystemSpaceViewTable[Hash].ControlArea = ControlArea;

    /* Hash entry found, increment total and return the base address */
    Session->SystemSpaceHashEntries++;

    KeReleaseGuardedMutex(Session->SystemSpaceViewLockPointer);
    return Base;
}

VOID
NTAPI
MiFillSystemPageDirectory(
    _In_ PVOID Base,
    _In_ SIZE_T NumberOfBytes)
{
  #if (_MI_PAGING_LEVELS <= 3)
    PFN_NUMBER ParentPage;
  #endif
    PMMPDE Pde;
    PMMPDE LastPde;
    PMMPDE SystemMapPde;
    MMPDE TempPde;
    PFN_NUMBER PageFrameIndex;
    KIRQL OldIrql;

    PAGED_CODE();
    DPRINT("MiFillSystemPageDirectory: Base %p, NumberOfBytes %X\n", Base, NumberOfBytes);

    /* Find the PDEs needed for this mapping */
    Pde = MiAddressToPde(Base);
    LastPde = MiAddressToPde((PVOID)((ULONG_PTR)Base + NumberOfBytes - 1));

  #if (_MI_PAGING_LEVELS <= 3)
    /* Find the system double-mapped PDE that describes this mapping */
    SystemMapPde = &MmSystemPagePtes[MiGetPdeOffset(Pde)];
  #else
    /* We don't have a double mapping */
    SystemMapPde = Pde;
  #endif

    /* Use the PDE template and loop the PDEs */
    TempPde = ValidKernelPde;

    for (; Pde <= LastPde; Pde++, SystemMapPde++)
    {
        /* Check if we don't already have this PDE mapped */
        if (SystemMapPde->u.Hard.Valid)
            continue;

        /* Lock the PFN database */
        OldIrql = MiLockPfnDb(APC_LEVEL);

        /* Check if we don't already have this PDE mapped */
        if (SystemMapPde->u.Hard.Valid)
        {
            /* Release the lock and keep going with the next PDE */
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            continue;
        }

        if (MmAvailablePages < 0x80)
        {
            DPRINT1("MiFillSystemPageDirectory: MmAvailablePages %X\n", MmAvailablePages);
            DPRINT1("MiFillSystemPageDirectory: FIXME MiEnsureAvailablePageOrWait()\n");
            ASSERT(FALSE);
        }

        //DPRINT("MiFillSystemPageDirectory: FIXME MiChargeCommitmentCantExpand()\n");

        MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
        MI_SET_PROCESS2(PsGetCurrentProcess()->ImageFileName);

        /* Grab a page for it */
        PageFrameIndex = MiRemoveZeroPage(MI_GET_NEXT_COLOR());
        ASSERT(PageFrameIndex);

        TempPde.u.Hard.PageFrameNumber = PageFrameIndex;

      #if (_MI_PAGING_LEVELS <= 3)
        /* Initialize its PFN entry, with the parent system page directory page table */
        ParentPage = MmSystemPageDirectory[MiGetPdIndex(Pde)];
        MiInitializePfnForOtherProcess(PageFrameIndex, (PMMPTE)Pde, ParentPage);
      #else
        MiInitializePfnAndMakePteValid(PageFrameIndex, Pde, TempPde);
      #endif

        /* Make the system PDE entry valid */
        MI_WRITE_VALID_PDE(SystemMapPde, TempPde);

        /* The system PDE entry might be the PDE itself, so check for this */
        if (!Pde->u.Hard.Valid)
            /* It's different, so make the real PDE valid too */
            MI_WRITE_VALID_PDE(Pde, TempPde);

        /* Release the lock and keep going with the next PDE */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
    }
}

NTSTATUS
NTAPI
MiSessionCommitPageTables(
    _In_ PVOID StartVa,
    _In_ PVOID EndVa)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MiAddViewsForSectionWithPfn(
    _In_ PMSUBSECTION StartMappedSubsection,
    _In_ ULONGLONG LastPteOffset)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MiAddMappedPtes(
    _In_ PMMPTE FirstPte,
    _In_ PFN_NUMBER PteCount,
    _In_ PCONTROL_AREA ControlArea)
{
    PSUBSECTION Subsection;
    PMMPTE Pte;
    PMMPTE LastPte;
    PMMPTE SectionProto;
    PMMPTE LastProto;
    MMPTE TempPte;
    NTSTATUS Status;

    DPRINT("MiAddMappedPtes: FirstPte %X, PteCount %X\n", FirstPte, PteCount);

    if (!ControlArea->u.Flags.GlobalOnlyPerSession && !ControlArea->u.Flags.Rom)
        Subsection = (PSUBSECTION)&ControlArea[1];
    else
        Subsection = (PSUBSECTION)((PLARGE_CONTROL_AREA)ControlArea + 1);

    /* Sanity checks */
    ASSERT(PteCount != 0);
    ASSERT(ControlArea->NumberOfMappedViews >= 1);
    ASSERT(ControlArea->NumberOfUserReferences >= 1);
    ASSERT(ControlArea->NumberOfSectionReferences != 0);
    ASSERT(ControlArea->u.Flags.BeingCreated == 0);
    ASSERT(ControlArea->u.Flags.BeingDeleted == 0);
    ASSERT(ControlArea->u.Flags.BeingPurged == 0);

    if (ControlArea->FilePointer &&
        !ControlArea->u.Flags.Image &&
        !ControlArea->u.Flags.PhysicalMemory)
    {
        Status = MiAddViewsForSectionWithPfn((PMSUBSECTION)Subsection, PteCount);
        if (!NT_SUCCESS (Status))
        {
            DPRINT1("MiAddMappedPtes: Status %X\n", Status);
            return Status;
        }
    }

    /* Get the PTEs for the actual mapping */
    Pte = FirstPte;
    LastPte = (FirstPte + PteCount);

    /* Get the section protos that desribe the section mapping in the subsection */
    SectionProto = Subsection->SubsectionBase;
    LastProto = &Subsection->SubsectionBase[Subsection->PtesInSubsection];

    /* Loop the PTEs for the mapping */
    for (; Pte < LastPte; Pte++, SectionProto++)
    {
        /* We may have run out of section protos in this subsection */
        if (SectionProto >= LastProto)
        {
            Subsection = Subsection->NextSubsection;
            SectionProto = Subsection->SubsectionBase;
            LastProto = &Subsection->SubsectionBase[Subsection->PtesInSubsection];
        }

        /* The PTE should be completely clear */
        ASSERT(Pte->u.Long == 0);

        /* Build the section proto and write it */
        MI_MAKE_PROTOTYPE_PTE(&TempPte, SectionProto);
        MI_WRITE_INVALID_PTE(Pte, TempPte);
    }

    /* No failure path */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MiMapViewInSystemSpace(
    _In_ PVOID SectionObject,
    _In_ PMMSESSION Session,
    _Out_ PVOID* MappedBase,
    _Out_ PSIZE_T ViewSize)
{
    PSECTION Section = SectionObject;
    PCONTROL_AREA ControlArea;
    PVOID Base;
    ULONG Buckets;
    ULONG SectionSize;
    SIZE_T SizeOfBuckets;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("MiMapViewInSystemSpace: Section %p, Session %p, MappedBase %p, ViewSize %I64X\n",
           Section, Session, (!MappedBase ? NULL : *MappedBase), (!ViewSize ? 0 : (ULONGLONG)*ViewSize));

    /* Get the control area */
    ControlArea = Section->Segment->ControlArea;

    /* Increase the reference and map count on the control area, no purges yet */
    Status = MiCheckPurgeAndUpMapCount(ControlArea, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MiMapViewInSystemSpace: Status %X\n", Status);
        return Status;
    }

    /* Get the section size at creation time */
    SectionSize = Section->SizeOfSection.LowPart;

    /* If the caller didn't specify a view size, assume the whole section */
    if (*ViewSize)
    {
        /* Check if the caller wanted a larger section than the view */
        if (*ViewSize > SectionSize)
        {
            /* Fail */
            DPRINT1("MiMapViewInSystemSpace: View is too large\n");
            ASSERT(FALSE);
            MiDereferenceControlArea(ControlArea);
            return STATUS_INVALID_VIEW_SIZE;
        }
    }
    else
    {
        DPRINT("MiMapViewInSystemSpace: SectionSize %X\n", SectionSize);
        ASSERT(SectionSize != 0);
        *ViewSize = SectionSize;
    }

    /* Get the number of 64K buckets required for this mapping */
    Buckets = (ULONG)(*ViewSize / MM_ALLOCATION_GRANULARITY);

    if (*ViewSize & (MM_ALLOCATION_GRANULARITY - 1))
        Buckets++;

    /* Check if the view is more than 4GB large */
    if (Buckets >= MM_ALLOCATION_GRANULARITY)
    {
        /* Fail */
        DPRINT1("MiMapViewInSystemSpace: View is too large\n");
        MiDereferenceControlArea(ControlArea);
        return STATUS_INVALID_VIEW_SIZE;
    }

    /* Insert this view into system space and get a base address for it */
    Base = MiInsertInSystemSpace(Session, Buckets, ControlArea);
    if (!Base)
    {
        /* Fail */
        DPRINT1("MiMapViewInSystemSpace: Out of system space\n");
        MiDereferenceControlArea(ControlArea);
        return STATUS_NO_MEMORY;
    }

    SizeOfBuckets = (Buckets * MM_ALLOCATION_GRANULARITY);

    /* What's the underlying session? */
    if (Session == &MmSession)
    {
        /* Create the PDEs needed for this mapping, and double-map them if needed */
        MiFillSystemPageDirectory(Base, SizeOfBuckets);
    }
    else
    {
        /* Create the PDEs needed for this mapping */
        Status = MiSessionCommitPageTables(Base, (PVOID)((ULONG_PTR)Base + SizeOfBuckets));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MiMapViewInSystemSpace: Status %X\n", Status);
            goto ErrorExit;
        }
    }

    /* Create the actual section protos for this mapping */
    Status = MiAddMappedPtes(MiAddressToPte(Base), BYTES_TO_PAGES(*ViewSize), ControlArea);
    if (NT_SUCCESS(Status))
    {
        *MappedBase = Base;
        return STATUS_SUCCESS;
    }

    DPRINT1("MiMapViewInSystemSpace: Status %X\n", Status);

ErrorExit:

    ASSERT(FALSE);
    MiDereferenceControlArea(ControlArea);
    return Status;
}

BOOLEAN
NTAPI
MiIsProtectionCompatible(
    _In_ ULONG SectionPageProtection,
    _In_ ULONG NewSectionPageProtection)
{
    ULONG ProtectionMask;
    ULONG CompatibleMask;

    DPRINT("MiIsProtectionCompatible: %X, %X\n", SectionPageProtection, NewSectionPageProtection);

    /* Calculate the protection mask and make sure it's valid */
    ProtectionMask = MiMakeProtectionMask(SectionPageProtection);
    if (ProtectionMask == MM_INVALID_PROTECTION)
    {
        DPRINT1("MiIsProtectionCompatible: ProtectionMask == MM_INVALID_PROTECTION\n");
        return FALSE;
    }

    /* Calculate the compatible mask */
    CompatibleMask = MmCompatibleProtectionMask[ProtectionMask & 0x7] |
                     PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE;

    /* See if the mapping protection is compatible with the create protection */
    return ((CompatibleMask | NewSectionPageProtection) == CompatibleMask);
}

VOID
NTAPI
MiInsertPhysicalViewAndRefControlArea(
    _In_ PEPROCESS Process,
    _In_ PCONTROL_AREA ControlArea,
    _In_ PMM_PHYSICAL_VIEW PhysicalView)
{
    KIRQL OldIrql;

    DPRINT("MiInsertPhysicalViewAndRefControlArea: %p, %p, %p\n", Process, ControlArea, PhysicalView);

    ASSERT(PhysicalView->Vad->u.VadFlags.VadType == VadDevicePhysicalMemory);
    ASSERT(Process->PhysicalVadRoot != NULL);

    MiInsertVad((PMMVAD)PhysicalView, Process->PhysicalVadRoot);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    ControlArea->NumberOfMappedViews++;
    ControlArea->NumberOfUserReferences++;
    ASSERT(ControlArea->NumberOfSectionReferences != 0);

    MiUnlockPfnDb(OldIrql, APC_LEVEL);
}

NTSTATUS
NTAPI
MiMapViewOfPhysicalSection(
    _In_ PCONTROL_AREA ControlArea,
    _In_ PEPROCESS Process,
    _Inout_ PVOID* BaseAddress,
    _In_ PLARGE_INTEGER SectionOffset,
    _Inout_ PSIZE_T ViewSize,
    _In_ ULONG ProtectionMask,
    _In_ ULONG_PTR ZeroBits,
    _In_ ULONG AllocationType)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MiMapViewOfImageSection(
    _In_ PCONTROL_AREA ControlArea,
    _In_ PEPROCESS Process,
    _Inout_ PVOID* OutBaseAddress,
    _Inout_ LARGE_INTEGER* OutSectionOffset,
    _Inout_ SIZE_T* OutViewSize,
    _In_ PSECTION Section,
    _In_ SECTION_INHERIT InheritDisposition,
    _In_ ULONG ZeroBits,
    _In_ ULONG AllocationType,
    _In_ SIZE_T ImageCommitment)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MiMapViewOfDataSection(
    _In_ PCONTROL_AREA ControlArea,
    _In_ PEPROCESS Process,
    _Inout_ PVOID* BaseAddress,
    _Inout_ PLARGE_INTEGER SectionOffset,
    _Inout_ PSIZE_T ViewSize,
    _In_ PSECTION Section,
    _In_ SECTION_INHERIT InheritDisposition,
    _In_ ULONG ProtectionMask,
    _In_ SIZE_T CommitSize,
    _In_ ULONG_PTR ZeroBits,
    _In_ ULONG AllocationType)
{
    PSEGMENT Segment = ControlArea->Segment;
    PMMEXTEND_INFO ExtendInfo;
    PSUBSECTION Subsection;
    PMMADDRESS_NODE Parent;
    PETHREAD Thread;
    PMMVAD Vad;
    PMMPTE Pte;
    PMMPTE LastPte;
    MMPTE TempPte;
    ULONGLONG PteOffset;
    ULONGLONG LastPteOffset;
    LARGE_INTEGER TotalNumberOfPtes;
    ULONG_PTR StartAddress;
    ULONG_PTR EndAddress;
    ULONG_PTR HighestAddress;
    ULONG Granularity = MM_ALLOCATION_GRANULARITY;
    ULONG VadSize;
    ULONG QuotaCharge = 0;
    ULONG QuotaExcess = 0;
    BOOLEAN IsLargePages;
    BOOLEAN IsFindEnabled;
    NTSTATUS Status;

    DPRINT("MiMapViewOfDataSection: %p, %I64X, %IX, %p, %p, %X, %X, %X, %X\n",
           Section, (SectionOffset ? SectionOffset->QuadPart : 0), (ViewSize ? *ViewSize : 0),
           ControlArea, Process, ZeroBits, CommitSize, AllocationType, ProtectionMask);

    /* One can only reserve a file-based mapping, not shared memory! */
    if ((AllocationType & MEM_RESERVE) && !ControlArea->FilePointer)
    {
        DPRINT1("MiMapViewOfDataSection: STATUS_INVALID_PARAMETER_9\n");
        return STATUS_INVALID_PARAMETER_9;
    }

    /* ALlow being less restrictive */
    if (AllocationType & MEM_DOS_LIM)
    {
        if (!(*BaseAddress) || (AllocationType & MEM_RESERVE))
        {
            DPRINT1("MiMapViewOfDataSection: STATUS_INVALID_PARAMETER_3\n");
            return STATUS_INVALID_PARAMETER_3;
        }

        Granularity = PAGE_SIZE;
    }

    /* First, increase the map count. No purging is supported yet */
    Status = MiCheckPurgeAndUpMapCount(ControlArea, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MiMapViewOfDataSection: Status %X\n", Status);
        return Status;
    }

    /* Check if the caller specified the view size */
    if (*ViewSize)
    {
        /* A size was specified, align it */
        *ViewSize += (SectionOffset->LowPart & (Granularity - 1));

        /* Align the offset as well to make this an aligned map */
        SectionOffset->LowPart &= ~(Granularity - 1);
    }
    else
    {
        /* The caller did not, so pick aligned view size based on the offset */
        SectionOffset->LowPart &= ~(Granularity - 1);
        *ViewSize = (SIZE_T)(Section->SizeOfSection.QuadPart - SectionOffset->QuadPart);
    }

    /* We must be dealing with aligned offset. This is a Windows ASSERT */
    ASSERT((SectionOffset->LowPart & (Granularity - 1)) == 0);

    /* It's illegal to try to map more than overflows a LONG_PTR */
    if (*ViewSize >= MAXLONG_PTR)
    {
        DPRINT1("MiMapViewOfDataSection: STATUS_INVALID_VIEW_SIZE\n");
        MiDereferenceControlArea(ControlArea);
        return STATUS_INVALID_VIEW_SIZE;
    }

    /* Windows ASSERTs for this flag */
    ASSERT(ControlArea->u.Flags.GlobalOnlyPerSession == 0);

    /* Get the subsection. */
    if (ControlArea->u.Flags.Rom)
    {
        Subsection = (PSUBSECTION)((PLARGE_CONTROL_AREA)ControlArea + 1);
        DPRINT("MiMapViewOfDataSection: Subsection %X\n", Subsection);
    }
    else
    {
        Subsection = (PSUBSECTION)(ControlArea + 1);
        DPRINT("MiMapViewOfDataSection: Subsection %X\n", Subsection);
    }

    /* Within this section, figure out which PTEs will describe the view */
    TotalNumberOfPtes.LowPart = Segment->TotalNumberOfPtes;
    TotalNumberOfPtes.HighPart = Segment->SegmentFlags.TotalNumberOfPtes4132;

    /* The offset must be in this segment's PTE chunk and it must be valid. */
    PteOffset = (SectionOffset->QuadPart / PAGE_SIZE);
    if (PteOffset >= (ULONGLONG)TotalNumberOfPtes.QuadPart)
    {
        MiDereferenceControlArea(ControlArea);
        return STATUS_INVALID_VIEW_SIZE;
    }

    LastPteOffset = ((SectionOffset->QuadPart + *ViewSize + (PAGE_SIZE - 1)) / PAGE_SIZE);
    ASSERT(LastPteOffset >= PteOffset);

    /* Subsection must contain these PTEs */
    while (PteOffset >= (ULONGLONG)Subsection->PtesInSubsection)
    {
        PteOffset -= Subsection->PtesInSubsection;
        LastPteOffset -= Subsection->PtesInSubsection;
        Subsection = Subsection->NextSubsection;
        ASSERT(Subsection != NULL);
    }

    if (ControlArea->FilePointer)
    {
        Status = MiAddViewsForSectionWithPfn((PMSUBSECTION)Subsection, LastPteOffset);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MiMapViewOfDataSection: Status %X\n", Status);
            MiDereferenceControlArea(ControlArea);
            return Status;
        }
    }

    /* Windows ASSERTs for this too -- there must be a subsection base address */
    ASSERT(Subsection->SubsectionBase != NULL);

    /* Compute how much commit space the segment will take */
    if (!ControlArea->FilePointer &&
        CommitSize &&
        Segment->NumberOfCommittedPages < (ULONGLONG)TotalNumberOfPtes.QuadPart)
    {
        DPRINT1("MiMapViewOfDataSection: FIXME. CommitSize %X\n", CommitSize);
        ASSERT(FALSE);
    }

    if (Segment->SegmentFlags.LargePages)
    {
        /* ARM3 does not currently support large pages */
        ASSERT(Segment->SegmentFlags.LargePages == 0);
        IsLargePages = TRUE;
        DbgBreakPoint();
    }
    else
    {
        IsLargePages = FALSE;
    }

    /* Is it SEC_BASED, or did the caller manually specify an address? */
    if (*BaseAddress || Section->Address.StartingVpn)
    {
        if (*BaseAddress)
            /* Just align what the caller gave us */
            StartAddress = ALIGN_DOWN_BY(*BaseAddress, Granularity);
        else
            /* It is a SEC_BASED mapping, use the address that was generated */
            StartAddress = (Section->Address.StartingVpn + SectionOffset->LowPart);

        if ((ULONG_PTR)StartAddress & (0x400000 - 1))
            IsLargePages = FALSE;

        EndAddress = ((StartAddress + *ViewSize - 1) | (PAGE_SIZE - 1));

        if (MiCheckForConflictingVadExistence(Process, StartAddress, EndAddress))
        {
            DPRINT1("MiMapViewOfDataSection: STATUS_CONFLICTING_ADDRESSES\n");
            Status = STATUS_CONFLICTING_ADDRESSES;
            goto ErrorExit;
        }
    }
    else
    {
        /* No StartAddress. Find empty address range. */
        IsFindEnabled = TRUE;

        if ((AllocationType & MEM_TOP_DOWN) || Process->VmTopDown)
        {
            HighestAddress = (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;

            if (ZeroBits)
            {
                HighestAddress = ((ULONG_PTR)MI_HIGHEST_SYSTEM_ADDRESS >> ZeroBits);
                if (HighestAddress > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
                    HighestAddress = (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;
            }

            if (IsLargePages)
            {
                DPRINT1("MiMapViewOfDataSection: FIXME. IsLargePages TRUE\n");
                ASSERT(FALSE);

                Status = MiFindEmptyAddressRangeDownTree(*ViewSize, HighestAddress, 0x400000, &Process->VadRoot, &StartAddress, &Parent);

                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("MiMapViewOfDataSection: Status %X\n", Status);
                    IsLargePages = FALSE;
                }
                else
                {
                    IsFindEnabled = FALSE;
                }
            }

            if (IsFindEnabled)
            {
                Status = MiFindEmptyAddressRangeDownTree(*ViewSize, HighestAddress, Granularity, &Process->VadRoot, &StartAddress, &Parent);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("MiMapViewOfDataSection: Status %X\n", Status);
                    goto ErrorExit;
                }
            }
        }
        else
        {
            if (IsLargePages)
            {
                Status = MiFindEmptyAddressRange(*ViewSize, 0x400000, ZeroBits, &StartAddress);

                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("MiMapViewOfDataSection: Status %X\n", Status);
                    IsLargePages = FALSE;
                }
                else
                {
                    IsFindEnabled = FALSE;
                }
            }

            if (IsFindEnabled)
            {
                Status = MiFindEmptyAddressRange(*ViewSize, Granularity, ZeroBits, &StartAddress);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("MiMapViewOfDataSection: Status %X\n", Status);
                    goto ErrorExit;
                }
            }
        }

        EndAddress = ((StartAddress + *ViewSize - 1) | (PAGE_SIZE - 1));

        if (ZeroBits && (EndAddress > ((ULONG_PTR)MI_HIGHEST_SYSTEM_ADDRESS >> ZeroBits)))
        {
            DPRINT1("MiMapViewOfDataSection: STATUS_NO_MEMORY\n");
            Status = STATUS_NO_MEMORY;
            goto ErrorExit;
        }
    }

    /* A VAD can now be allocated. Do so and zero it out */
    if (AllocationType & MEM_RESERVE)
    {
        VadSize = sizeof(MMVAD_LONG);
        Vad = ExAllocatePoolWithTag(NonPagedPool, VadSize, 'ldaV');
    }
    else
    {
        VadSize = sizeof(MMVAD);
        Vad = ExAllocatePoolWithTag(NonPagedPool, VadSize, ' daV');
    }

    if (!Vad)
    {
        DPRINT1("MiMapViewOfDataSection: STATUS_INSUFFICIENT_RESOURCES\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ErrorExit;
    }

    RtlZeroMemory(Vad, VadSize);

    /* Write all the data required in the VAD for handling a fault */
    Vad->ControlArea = ControlArea;
    Vad->u.VadFlags.Protection = ProtectionMask;
    Vad->u2.VadFlags2.FileOffset = (ULONG)(SectionOffset->QuadPart >> 16);
    Vad->u2.VadFlags2.Inherit = (InheritDisposition == ViewShare);
    Vad->u2.VadFlags2.CopyOnWrite = Section->u.Flags.CopyOnWrite;

    if ((AllocationType & SEC_NO_CHANGE) || Section->u.Flags.NoChange)
    {
        /* Setting the flag */
        Vad->u.VadFlags.NoChange = 1;
        Vad->u2.VadFlags2.SecNoChange = 1;
    }

    if (AllocationType & MEM_RESERVE)
    {
        Vad->u2.VadFlags2.LongVad = 1;

        KeAcquireGuardedMutexUnsafe(&MmSectionBasedMutex);

        ExtendInfo = Segment->ExtendInfo;

        if (ExtendInfo)
        {
            ExtendInfo->ReferenceCount++;
        }
        else
        {
            ExtendInfo = ExAllocatePoolWithTag(NonPagedPool, sizeof(MMEXTEND_INFO), 'xCmM');
            if (!ExtendInfo)
            {
                KeReleaseGuardedMutexUnsafe(&MmSectionBasedMutex);

                DPRINT1("MiMapViewOfDataSection: STATUS_INSUFFICIENT_RESOURCES\n");
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto ErrorExit;
            }

            ExtendInfo->ReferenceCount = 1;
            ExtendInfo->CommittedSize = Segment->SizeOfSegment;
            Segment->ExtendInfo = ExtendInfo;
        }

        if (ExtendInfo->CommittedSize < (ULONGLONG)Section->SizeOfSection.QuadPart)
            ExtendInfo->CommittedSize = (ULONGLONG)Section->SizeOfSection.QuadPart;

        KeReleaseGuardedMutexUnsafe(&MmSectionBasedMutex);

        Vad->u2.VadFlags2.ExtendableFile = 1;

        ASSERT(((PMMVAD_LONG)Vad)->u4.ExtendedInfo == NULL);
        ((PMMVAD_LONG)Vad)->u4.ExtendedInfo = ExtendInfo;
    }

    if ((ProtectionMask & MM_WRITECOPY) == MM_WRITECOPY)
        Vad->u.VadFlags.CommitCharge = BYTES_TO_PAGES(EndAddress - StartAddress);

    /* Finally, write down the first and last prototype PTE */
    Vad->StartingVpn = (StartAddress / PAGE_SIZE);
    Vad->EndingVpn = (EndAddress / PAGE_SIZE);

    Vad->FirstPrototypePte = &Subsection->SubsectionBase[PteOffset];

    PteOffset += (Vad->EndingVpn - Vad->StartingVpn);

    if (PteOffset >= Subsection->PtesInSubsection)
        Vad->LastContiguousPte = &Subsection->SubsectionBase[Subsection->PtesInSubsection - 1 + Subsection->UnusedPtes];
    else
        Vad->LastContiguousPte = &Subsection->SubsectionBase[PteOffset];

    /* Make sure the prototype PTE ranges make sense, this is a Windows ASSERT */
    ASSERT(Vad->FirstPrototypePte <= Vad->LastContiguousPte);

    /* Check if anything was committed */
    if (QuotaCharge)
    {
        DPRINT1("MiMapViewOfDataSection: FIXME MiChargeCommitment()\n");
        ASSERT(FALSE);
    }

    ASSERT(Vad->FirstPrototypePte <= Vad->LastContiguousPte);
    Thread = PsGetCurrentThread();

    /* Insert the VAD charges */
    Status = MiInsertVadCharges(Vad, Process);
    if (!NT_SUCCESS(Status))
    {
        if (ControlArea->FilePointer)
        {
            DPRINT1("MiMapViewOfDataSection: FIXME MiRemoveViewsFromSectionWithPfn()\n");
            ASSERT(FALSE);
        }

        MiDereferenceControlArea(ControlArea);

        if (AllocationType & MEM_RESERVE)
            ExFreePoolWithTag(Vad, 'ldaV');
        else
            ExFreePoolWithTag(Vad, ' daV');

        if (QuotaCharge)
        {
            ASSERT((SSIZE_T)(QuotaCharge) >= 0);
            ASSERT(MmTotalCommittedPages >= (QuotaCharge));
            InterlockedExchangeAdd((volatile PLONG)&MmTotalCommittedPages, -QuotaCharge);
        }

        DPRINT1("MiMapViewOfDataSection: Status %X\n", Status);
        return Status;
    }

    /* Insert the VAD */
    MiLockProcessWorkingSetUnsafe(Process, Thread);
    MiInsertVad(Vad, &Process->VadRoot);
    MiUnlockProcessWorkingSetUnsafe(Process, Thread);

    if (IsLargePages)
    {
        DPRINT1("MiMapViewOfDataSection: FIXME MiMapLargePageSection()\n");
        ASSERT(FALSE);
    }

    /* Windows stores this for accounting purposes, do so as well */
    if (!ControlArea->FilePointer && !Segment->u2.FirstMappedVa)
        Segment->u2.FirstMappedVa = (PVOID)StartAddress;

    if (AllocationType & MEM_RESERVE)
    {
        ASSERT((EndAddress - StartAddress) <= (((ULONGLONG)Segment->SizeOfSegment + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1)));
    }

    /* Check if anything was committed */
    if (QuotaCharge)
    {
        /* Set the start and end PTE addresses, and pick the template PTE */
        Pte = Vad->FirstPrototypePte;
        LastPte = (Pte + BYTES_TO_PAGES(CommitSize));
        TempPte = Segment->SegmentPteTemplate;

        /* Acquire the commit lock and loop all prototype PTEs to be committed */
        KeAcquireGuardedMutexUnsafe(&MmSectionCommitMutex);

        for (; Pte < LastPte; Pte++)
        {
            /* Test the PTE */
            if (Pte->u.Long)
                /* The PTE is valid, so skip it */
                QuotaExcess++;
            else
                /* Write the invalid PTE */
                MI_WRITE_INVALID_PTE(Pte, TempPte);
        }

        /* Now check how many pages exactly we committed, and update accounting */
        ASSERT(QuotaCharge >= QuotaExcess);
        QuotaCharge -= QuotaExcess;
        Segment->NumberOfCommittedPages += QuotaCharge;
        ASSERT(Segment->NumberOfCommittedPages <= (ULONGLONG)TotalNumberOfPtes.QuadPart);

        /* Now that we're done, release the lock */
        KeReleaseGuardedMutexUnsafe(&MmSectionCommitMutex);
        InterlockedExchangeAdd((volatile PLONG)&MmSharedCommit, QuotaCharge);

        if (QuotaExcess)
        {
            ASSERT((SSIZE_T)(QuotaCharge) >= 0);
            ASSERT(MmTotalCommittedPages >= (QuotaCharge));
            InterlockedExchangeAdd((volatile PLONG)&MmTotalCommittedPages, -QuotaExcess);
        }
    }

    /* Finally, let the caller know where, and for what size, the view was mapped */
    *ViewSize = (EndAddress - StartAddress + 1);
    *BaseAddress = (PVOID)StartAddress;

    Process->VirtualSize += *ViewSize;

    if (Process->VirtualSize > Process->PeakVirtualSize)
        Process->PeakVirtualSize = Process->VirtualSize;

    if ((ProtectionMask == MM_READWRITE || ProtectionMask == MM_EXECUTE_READWRITE) && ControlArea->FilePointer)
        InterlockedIncrement((volatile PLONG)&ControlArea->WritableUserReferences);

    DPRINT("MiMapViewOfDataSection: %p,  %p, %I64X, %IX\n",
           Section, *BaseAddress, (SectionOffset ? SectionOffset->QuadPart : 0), *ViewSize);

    return STATUS_SUCCESS;

ErrorExit:

    if (ControlArea->FilePointer)
    {
        DPRINT1("MiMapViewOfDataSection: FIXME MiRemoveViewsFromSectionWithPfn()\n");
        ASSERT(FALSE);
    }

    MiDereferenceControlArea(ControlArea);

    if (Vad)
    {
        if (AllocationType & MEM_RESERVE)
            ExFreePoolWithTag(Vad, 'ldaV');
        else
            ExFreePoolWithTag(Vad, ' daV');
    }

    DPRINT("MiMapViewOfDataSection: *BaseAddress %p, *ViewSize %p, Status %X\n", *BaseAddress, *ViewSize, Status);
    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
MmCanFileBeTruncated(
    _In_ PSECTION_OBJECT_POINTERS SectionPointer,
    _In_ PLARGE_INTEGER NewFileSize)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
MmCommitSessionMappedView(
    _In_ PVOID MappedBase,
    _In_ SIZE_T ViewSize)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmCreateSection(
    _Out_ PVOID* SectionObject,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
    _In_ PLARGE_INTEGER InputMaximumSize,
    _In_ ULONG SectionPageProtection,
    _In_ ULONG AllocationAttributes,
    _In_ HANDLE FileHandle OPTIONAL,
    _In_ PFILE_OBJECT FileObject OPTIONAL)
{
    PCONTROL_AREA ControlArea;
    PSEGMENT NewSegment = NULL;
    PSUBSECTION Subsection;
    SECTION Section;
    PSECTION NewSection;
    ULONG ProtectionMask;
    ULONG SubsectionSize;
    ULONG NonPagedCharge;
    ULONG PagedCharge;
    KIRQL OldIrql;
    BOOLEAN UserRefIncremented = FALSE;
    BOOLEAN FileLock = FALSE;
    BOOLEAN IgnoreFileSizing = FALSE; // TRUE if CC call (FileObject != NULL)
    BOOLEAN IsSectionSizeChanged = FALSE;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;

    DPRINT("MmCreateSection: %X, %X, %I64X, %X, %X, %p, %p\n", DesiredAccess, ObjectAttributes,
           InputMaximumSize->QuadPart, SectionPageProtection, AllocationAttributes, FileHandle, FileObject);

    /* Make the same sanity checks that the Nt interface should've validated */
    ASSERT((AllocationAttributes & ~(SEC_COMMIT | SEC_RESERVE | SEC_BASED | SEC_LARGE_PAGES |
                                     SEC_IMAGE | SEC_NOCACHE | SEC_NO_CHANGE)) == 0);

    ASSERT((AllocationAttributes & (SEC_COMMIT | SEC_RESERVE | SEC_IMAGE)) != 0);

    ASSERT(!((AllocationAttributes & SEC_IMAGE) &&
             (AllocationAttributes & (SEC_COMMIT | SEC_RESERVE | SEC_NOCACHE | SEC_NO_CHANGE))));

    ASSERT(!((AllocationAttributes & SEC_COMMIT) && (AllocationAttributes & SEC_RESERVE)));

    ASSERT(!((SectionPageProtection & PAGE_NOCACHE) ||
             (SectionPageProtection & PAGE_WRITECOMBINE) ||
             (SectionPageProtection & PAGE_GUARD) ||
             (SectionPageProtection & PAGE_NOACCESS)));

    /* Convert section flag to page flag */
    if (AllocationAttributes & SEC_NOCACHE)
        SectionPageProtection |= PAGE_NOCACHE;

    /* Check to make sure the protection is correct. Nt* does this already */
    ProtectionMask = MiMakeProtectionMask(SectionPageProtection);
    if (ProtectionMask == MM_INVALID_PROTECTION)
    {
        DPRINT1("MmCreateSection: STATUS_INVALID_PAGE_PROTECTION\n");
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    /* Check if this is going to be a data or image backed file section */
    if (FileHandle || FileObject)
    {
        /* These cannot be mapped with large pages */
        if (AllocationAttributes & SEC_LARGE_PAGES)
        {
            DPRINT1("MmCreateSection: STATUS_INVALID_PARAMETER_6\n");
            return STATUS_INVALID_PARAMETER_6;
        }

        DPRINT("MmCreateSection: FileHandle %p, FileObject %p\n", FileHandle, FileObject);

        if (FileHandle)
        {
            ASSERT(FALSE);
        }
        else
        {
            ASSERT(FALSE);
        }


    }
    else
    {
        /* If FileHandle and FileObject are null, then this is a pagefile-backed section. */
        if (AllocationAttributes & SEC_IMAGE)
        {
            /* A handle must be supplied with SEC_IMAGE, as this is the no-handle path */
            DPRINT1("MmCreateSection: STATUS_INVALID_FILE_FOR_SECTION\n");
            return STATUS_INVALID_FILE_FOR_SECTION;
        }

        if (AllocationAttributes & SEC_LARGE_PAGES)
        {
            /* Not yet supported */
            ASSERT((AllocationAttributes & SEC_LARGE_PAGES) == 0);

            if (!(AllocationAttributes & SEC_COMMIT))
            {
                DPRINT1("MmCreateSection: STATUS_INVALID_PARAMETER_6\n");
                return STATUS_INVALID_PARAMETER_6;
            }

            if (!SeSinglePrivilegeCheck(SeLockMemoryPrivilege, KeGetCurrentThread()->PreviousMode))
            {
                DPRINT1("MmCreateSection: STATUS_PRIVILEGE_NOT_HELD\n");
                return STATUS_PRIVILEGE_NOT_HELD;
            }
        }

        /* So this must be a pagefile-backed section, create the mappings needed */
        Status = MiCreatePagingFileMap(&NewSegment, (PULONGLONG)InputMaximumSize, ProtectionMask, AllocationAttributes);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MmCreateSection: Status %X\n", Status);
            return Status;
        }

        /* Set the size here, and read the control area */
        Section.SizeOfSection.QuadPart = NewSegment->SizeOfSegment;
        DPRINT("MmCreateSection: Section.SizeOfSection.QuadPart %I64X\n", Section.SizeOfSection.QuadPart);

        ControlArea = NewSegment->ControlArea;

        /* MiCreatePagingFileMap increments user references */
        UserRefIncremented = TRUE;
    }

    DPRINT("MmCreateSection: NewSegment %p\n", NewSegment);

    /* Did we already have a segment? */
    if (!NewSegment)
    {
        ASSERT(FALSE);
    }

    /* Check if we locked the file earlier */
    if (FileLock)
    {
        ASSERT(FALSE);
    }

    /* Set the initial section object data */
    Section.InitialPageProtection = SectionPageProtection;

    /* The mapping created a control area and segment, save the flags */
    Section.Segment = NewSegment;
    Section.u.LongFlags = ControlArea->u.LongFlags;

    /* Check if this is a user-mode read-write non-image file mapping */
    if (!FileObject &&
        (SectionPageProtection & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
        !ControlArea->u.Flags.Image &&
        ControlArea->FilePointer)
    {
        /* Add a reference and set the flag */
        Section.u.Flags.UserWritable = 1;
        InterlockedIncrement((volatile PLONG)&ControlArea->WritableUserReferences);
    }

    /* Check for image mappings or page file mappings */
    if (ControlArea->u.Flags.Image || !ControlArea->FilePointer)
    {
        /* Charge the segment size, and allocate a subsection */
        PagedCharge = (sizeof(SECTION) + NewSegment->TotalNumberOfPtes * sizeof(MMPTE));
        SubsectionSize = sizeof(SUBSECTION);
    }
    else
    {
        /* Charge nothing, and allocate a mapped subsection */
        PagedCharge = 0;
        SubsectionSize = sizeof(MSUBSECTION);
    }

    /* Check type and charge a CA and the get subsection pointer */
    if (ControlArea->u.Flags.GlobalOnlyPerSession || ControlArea->u.Flags.Rom)
    {
        NonPagedCharge = sizeof(LARGE_CONTROL_AREA);
        Subsection = (PSUBSECTION)((PLARGE_CONTROL_AREA)ControlArea + 1);
    }
    else
    {
        NonPagedCharge = sizeof(CONTROL_AREA);
        Subsection = (PSUBSECTION)(ControlArea + 1);
    }

    do
    {
        NonPagedCharge += SubsectionSize;
        Subsection = Subsection->NextSubsection;
    }
    while (Subsection);

    /* Create the actual section object, with enough space for the prototypes */
    Status = ObCreateObject(PreviousMode,
                            MmSectionObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(SECTION),
                            PagedCharge,
                            NonPagedCharge,
                            (PVOID*)&NewSection);
    if (!NT_SUCCESS(Status))
    {
        /* Check if this is a user-mode read-write non-image file mapping */
        if (!FileObject &&
            (SectionPageProtection & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
            !ControlArea->u.Flags.Image &&
            ControlArea->FilePointer)
        {
            /* Remove a reference and check the flag */
            ASSERT(Section.u.Flags.UserWritable == 1);
            InterlockedDecrement((volatile PLONG)&ControlArea->WritableUserReferences);
        }

//ErrorExit:

        /* Check if we locked and set the IRP */
        if (FileLock)
        {
            ASSERT(FALSE);
        }

        /* Check if a user reference was added */
        if (UserRefIncremented)
        {
            /* Acquire the PFN lock while we change counters */
            OldIrql = MiLockPfnDb(APC_LEVEL);

            /* Decrement the accounting counters */
            ControlArea->NumberOfSectionReferences--;

            if (!IgnoreFileSizing)
            {
                ASSERT((LONG)ControlArea->NumberOfUserReferences > 0);
                ControlArea->NumberOfUserReferences--;
            }

            /* Check if we should destroy the CA and release the lock */
            MiCheckControlArea(ControlArea, OldIrql);
        }

        /* Return the failure code */
        DPRINT1("MmCreateSection: Status %X\n", Status);
        return Status;
    }

    /* NOTE: Past this point, all failures will be handled by Ob upon ref->0 */

    /* Now copy the local section object from the stack into this new object */
    RtlCopyMemory(NewSection, &Section, sizeof(SECTION));
    NewSection->Address.StartingVpn = 0;

    if (!IgnoreFileSizing)
    {
        /* Not CC call */
        NewSection->u.Flags.UserReference = 1;

        if (AllocationAttributes & SEC_NO_CHANGE)
            NewSection->u.Flags.NoChange = 1;

        if (!(SectionPageProtection & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            NewSection->u.Flags.CopyOnWrite = 1;

        /* Is this a "based" allocation, in which all mappings are identical? */
        if (AllocationAttributes & SEC_BASED)
        {
            ASSERT(FALSE);
        }
    }

    /* Write flag if this a CC call */
    ControlArea->u.Flags.WasPurged |= IgnoreFileSizing;

    if ((ControlArea->u.Flags.WasPurged && !IgnoreFileSizing) &&
        (!IsSectionSizeChanged || ((ULONGLONG)NewSection->SizeOfSection.QuadPart > NewSection->Segment->SizeOfSegment)))
    {
        DPRINT1("MmCreateSection: FIXME MmExtendSection \n");
        ASSERT(FALSE);
    }

    /* Return the object and the creation status */
    *SectionObject = NewSection;

    DPRINT("MmCreateSection: NewSection %p NewSegment %p\n", NewSection, NewSegment);
    return Status;
}

BOOLEAN
NTAPI
MmDisableModifiedWriteOfSection(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

ULONG
NTAPI
MmDoesFileHaveUserWritableReferences(
    _In_ PSECTION_OBJECT_POINTERS SectionPointer)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

BOOLEAN
NTAPI
MmFlushImageSection(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_ MMFLUSH_TYPE FlushType)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
MmForceSectionClosed(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_ BOOLEAN DelayClose)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
MmMapViewInSessionSpace(
    _In_ PVOID Section,
    _Out_ PVOID* MappedBase,
    _Inout_ PSIZE_T ViewSize)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmMapViewInSystemSpace(
    _In_ PVOID Section,
    _Out_ PVOID* MappedBase,
    _Out_ PSIZE_T ViewSize)
{
    PAGED_CODE();
    DPRINT("MmMapViewInSystemSpace: Section %p, MappedBase %p, ViewSize %I64X\n",
           Section, (MappedBase ? *MappedBase : 0), (ViewSize ? (ULONGLONG)(*ViewSize) : 0ull));

    return MiMapViewInSystemSpace((PSECTION)Section, &MmSession, MappedBase, ViewSize);
}

NTSTATUS
NTAPI
MmMapViewOfSection(
    _In_ PVOID SectionObject,
    _In_ PEPROCESS Process,
    _Inout_ PVOID* BaseAddress,
    _In_ ULONG_PTR ZeroBits,
    _In_ SIZE_T CommitSize,
    _Inout_ PLARGE_INTEGER SectionOffset OPTIONAL,
    _Inout_ PSIZE_T ViewSize,
    _In_ SECTION_INHERIT InheritDisposition,
    _In_ ULONG AllocationType,
    _In_ ULONG Protect)
{
    ULONG64 CalculatedViewSize;
    PCONTROL_AREA ControlArea;
    PSECTION Section;
    ULONG ProtectionMask;
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("MmMapViewOfSection: %p, %p, %X, %X, %X, %X\n",
           SectionObject, Process, ZeroBits, CommitSize, AllocationType, Protect);

    /* Get Section */
    Section = (PSECTION)SectionObject;

    if (!Section->u.Flags.Image)
    {
        if (!MiIsProtectionCompatible(Section->InitialPageProtection, Protect))
        {
            DPRINT1("MmMapViewOfSection: return STATUS_SECTION_PROTECTION\n");
            ASSERT(FALSE); // MiDbgBreakPointEx();
            return STATUS_SECTION_PROTECTION;
        }
    }


    /* Check if the offset and size would cause an overflow */
    if (((ULONG64)SectionOffset->QuadPart + *ViewSize) < (ULONG64)SectionOffset->QuadPart)
    {
        DPRINT1("MmMapViewOfSection: Section offset overflows\n");
        return STATUS_INVALID_VIEW_SIZE;
    }

    /* Check if the offset and size are bigger than the section itself */
    if (((ULONG64)SectionOffset->QuadPart + *ViewSize) > (ULONG64)Section->SizeOfSection.QuadPart &&
        !(AllocationType & MEM_RESERVE))
    {
        DPRINT1("MmMapViewOfSection: Section offset is larger than section\n");
        return STATUS_INVALID_VIEW_SIZE;
    }

    /* Check if the caller did not specify a view size */
    if (!(*ViewSize))
    {
        /* Compute it for the caller */
        CalculatedViewSize = (Section->SizeOfSection.QuadPart - SectionOffset->QuadPart);

        /* Check if it's larger than 4GB or overflows into kernel-mode */
        if (!NT_SUCCESS(RtlULongLongToSIZET(CalculatedViewSize, ViewSize)) ||
            (((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS - (ULONG_PTR)*BaseAddress) < CalculatedViewSize))
        {
            DPRINT1("MmMapViewOfSection: Section view won't fit\n");
            return STATUS_INVALID_VIEW_SIZE;
        }
    }

    /* Check if the commit size is larger than the view size */
    if (CommitSize > *ViewSize && !(AllocationType & MEM_RESERVE))
    {
        DPRINT1("MmMapViewOfSection: Attempting to commit more than the view itself\n");
        return STATUS_INVALID_PARAMETER_5;
    }

    /* Check if the view size is larger than the section */
    if (*ViewSize > (ULONG64)Section->SizeOfSection.QuadPart && !(AllocationType & MEM_RESERVE))
    {
        DPRINT1("MmMapViewOfSection: The view is larger than the section\n");
        return STATUS_INVALID_VIEW_SIZE;
    }

    /* Compute and validate the protection */
    if (AllocationType & MEM_RESERVE &&
        (!(Section->InitialPageProtection & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))))
    {
        DPRINT1("MmMapViewOfSection: STATUS_SECTION_PROTECTION\n");
        return STATUS_SECTION_PROTECTION;
    }

    if (Section->u.Flags.NoCache)
        Protect = ((Protect & ~PAGE_WRITECOMBINE) | PAGE_NOCACHE);

    if (Section->u.Flags.WriteCombined)
        Protect = ((Protect & ~PAGE_NOCACHE) | PAGE_WRITECOMBINE);

    /* Compute and validate the protection mask */
    ProtectionMask = MiMakeProtectionMask(Protect);
    if (ProtectionMask == MM_INVALID_PROTECTION)
    {
        DPRINT1("MmMapViewOfSection: The protection is invalid\n");
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    /* Get the control area */
    ControlArea = Section->Segment->ControlArea;

    /* Start by attaching to the current process if needed */
    if (PsGetCurrentProcess() != Process)
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    /* Lock the process address space */
    KeAcquireGuardedMutex(&Process->AddressCreationLock);

    if (Process->VmDeleted)
    {
        DPRINT1("MmMapViewOfSection: STATUS_PROCESS_IS_TERMINATING\n");
        Status = STATUS_PROCESS_IS_TERMINATING;
        goto Exit;
    }

    /* Do the actual mapping */

    if (ControlArea->u.Flags.PhysicalMemory)
    {
        Status = MiMapViewOfPhysicalSection(ControlArea,
                                            Process,
                                            BaseAddress,
                                            SectionOffset,
                                            ViewSize,
                                            ProtectionMask,
                                            ZeroBits,
                                            AllocationType);
        goto Exit;
    }

    if (ControlArea->u.Flags.Image)
    {
        if (AllocationType & MEM_RESERVE)
        {
            DPRINT1("MmMapViewOfSection: STATUS_INVALID_PARAMETER_9\n");
            Status = STATUS_INVALID_PARAMETER_9;
            goto Exit;
        }

        if (Protect & PAGE_WRITECOMBINE)
        {
            DPRINT1("MmMapViewOfSection: STATUS_INVALID_PARAMETER_10\n");
            Status = STATUS_INVALID_PARAMETER_10;
            goto Exit;
        }

        Status = MiMapViewOfImageSection(ControlArea,
                                         Process,
                                         BaseAddress,
                                         SectionOffset,
                                         ViewSize,
                                         Section,
                                         InheritDisposition,
                                         ZeroBits,
                                         AllocationType,
                                         Section->Segment->u1.ImageCommitment);
        goto Exit;
    }

    if (Protect & PAGE_WRITECOMBINE)
    {
        DPRINT1("MmMapViewOfSection: STATUS_INVALID_PARAMETER_10\n");
        Status = STATUS_INVALID_PARAMETER_10;
        goto Exit;
    }

    Status = MiMapViewOfDataSection(ControlArea,
                                    Process,
                                    BaseAddress,
                                    SectionOffset,
                                    ViewSize,
                                    Section,
                                    InheritDisposition,
                                    ProtectionMask,
                                    CommitSize,
                                    ZeroBits,
                                    AllocationType);
Exit:

    /* Release the address space lock */
    KeReleaseGuardedMutex(&Process->AddressCreationLock);

    /* Detach if needed, then return status */
    if (Attached)
        KeUnstackDetachProcess(&ApcState);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmMapViewOfSection: Status %X\n", Status);
    }

    return Status;
}

NTSTATUS
NTAPI
MmUnmapViewInSessionSpace(
    _In_ PVOID MappedBase)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmUnmapViewInSystemSpace(
    _In_ PVOID MappedBase)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmUnmapViewOfSection(
    _In_ PEPROCESS Process,
    _In_ PVOID BaseAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtAreMappedFilesTheSame(
    _In_ PVOID File1MappedAsAnImage,
    _In_ PVOID File2MappedAsFile)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtCreateSection(
    _Out_ PHANDLE SectionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
    _In_ PLARGE_INTEGER MaximumSize OPTIONAL,
    _In_ ULONG SectionPageProtection OPTIONAL,
    _In_ ULONG AllocationAttributes,
    _In_ HANDLE FileHandle OPTIONAL)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER SafeMaximumSize;
    PCONTROL_AREA ControlArea;
    PSECTION SectionObject;
    PFILE_OBJECT FileObject;
    HANDLE Handle;
    ULONG MaximumRetry = 3;
    ULONG ix;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("NtCreateSection: %X, %X, %p [%I64X], %X, %X, %p\n", DesiredAccess, ObjectAttributes, MaximumSize,
           (MaximumSize ? MaximumSize->QuadPart : 0), SectionPageProtection, AllocationAttributes, FileHandle);

    /* Check for non-existing flags */
    if (AllocationAttributes & ~(SEC_COMMIT | SEC_RESERVE | SEC_BASED | SEC_LARGE_PAGES |
                                 SEC_IMAGE | SEC_NOCACHE | SEC_NO_CHANGE))
    {
        DPRINT1("NtCreateSection: Bogus allocation attribute %X\n", AllocationAttributes);
        return STATUS_INVALID_PARAMETER_6;
    }

    /* Check for no allocation type */
    if (!(AllocationAttributes & (SEC_COMMIT | SEC_RESERVE | SEC_IMAGE)))
    {
        DPRINT1("NtCreateSection: Missing allocation type in allocation attributes\n");
        return STATUS_INVALID_PARAMETER_6;
    }

    /* Check for image allocation with invalid attributes */
    if ((AllocationAttributes & SEC_IMAGE) &&
        (AllocationAttributes & (SEC_COMMIT | SEC_RESERVE | SEC_LARGE_PAGES | SEC_NOCACHE | SEC_NO_CHANGE)))
    {
        DPRINT1("NtCreateSection: Image allocation with invalid attributes\n");
        return STATUS_INVALID_PARAMETER_6;
    }

    /* Check for allocation type is both commit and reserve */
    if ((AllocationAttributes & SEC_COMMIT) && (AllocationAttributes & SEC_RESERVE))
    {
        DPRINT1("NtCreateSection: Commit and reserve in the same time\n");
        return STATUS_INVALID_PARAMETER_6;
    }

    /* Now check for valid protection */
    if ((SectionPageProtection & PAGE_NOCACHE) ||
        (SectionPageProtection & PAGE_WRITECOMBINE) ||
        (SectionPageProtection & PAGE_GUARD) ||
        (SectionPageProtection & PAGE_NOACCESS))
    {
        DPRINT1("NtCreateSection: Sections don't support these protections\n");
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    /* Use a maximum size of zero, if none was specified */
    if (!MaximumSize)
        SafeMaximumSize.QuadPart = 0;

    /* Check for user-mode caller */
    if (PreviousMode != KernelMode)
    {
        /* Enter SEH */
        _SEH2_TRY
        {
            /* Safely check user-mode parameters */
            if (MaximumSize)
                SafeMaximumSize = ProbeForReadLargeInteger(MaximumSize);

            MaximumSize = &SafeMaximumSize;
            ProbeForWriteHandle(SectionHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        if (MaximumSize)
            SafeMaximumSize.QuadPart = MaximumSize->QuadPart;
    }

    for (ix = 0; ; ix++)
    {
        ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

        /* Try create the section */
        Status = MmCreateSection((PVOID *)&SectionObject,
                                 DesiredAccess,
                                 ObjectAttributes,
                                 &SafeMaximumSize,
                                 SectionPageProtection,
                                 AllocationAttributes,
                                 FileHandle,
                                 NULL);

        ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

        if (NT_SUCCESS(Status))
            break;

        if (Status == STATUS_FILE_LOCK_CONFLICT && ix < MaximumRetry)
        {
            DPRINT1("NtCreateSection: ix %X\n", ix);
            KeDelayExecutionThread(KernelMode, FALSE, &MmHalfSecond);
            continue;
        }

        DPRINT1("NtCreateSection: ix %X, Status %X\n", ix, Status);
        return Status;
    }

    ControlArea = SectionObject->Segment->ControlArea;
    if (ControlArea)
    {
        FileObject = ControlArea->FilePointer;
        if (FileObject)
        {
            DPRINT("NtCreateSection: FIXME CcZeroEndOfLastPage!\n");
            //CcZeroEndOfLastPage(FileObject);
        }
    }

    /* Now insert the object */
    Status = ObInsertObject(SectionObject, NULL, DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateSection: Status %X\n", Status);
        return Status;
    }

    /* Enter SEH */
    _SEH2_TRY
    {
        /* Return the handle safely */
        *SectionHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Nothing here */
    }
    _SEH2_END;

    /* Return the status */
    return Status;
}

NTSTATUS
NTAPI
NtOpenSection(
    _Out_ PHANDLE SectionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("NtOpenSection: Access %X, ObjectName '%wZ'\n", DesiredAccess, ObjectAttributes->ObjectName);

    /* Check for user-mode caller */
    if (PreviousMode != KernelMode)
    {
        /* Enter SEH */
        _SEH2_TRY
        {
            /* Safely check user-mode parameters */
            ProbeForWriteHandle(SectionHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* Try opening the object */
    Status = ObOpenObjectByName(ObjectAttributes,
                                MmSectionObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);

    /* Enter SEH */
    _SEH2_TRY
    {
        /* Return the handle safely */
        *SectionHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Nothing here */
    }
    _SEH2_END;

    /* Return the status */
    return Status;
}

NTSTATUS
NTAPI
NtMapViewOfSection(
    _In_ HANDLE SectionHandle,
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* BaseAddress,
    _In_ ULONG_PTR ZeroBits,
    _In_ SIZE_T CommitSize,
    _Inout_ PLARGE_INTEGER SectionOffset OPTIONAL,
    _Inout_ PSIZE_T ViewSize,
    _In_ SECTION_INHERIT InheritDisposition,
    _In_ ULONG AllocationType,
    _In_ ULONG Protect)
{
    PEPROCESS CurrentProcess;
    PEPROCESS Process;
    PSECTION Section;
    PVOID SafeBaseAddress;
    LARGE_INTEGER SafeSectionOffset;
    SIZE_T SafeViewSize;
    ACCESS_MASK DesiredAccess;
    ULONG ProtectionMask;
    KPROCESSOR_MODE PreviousMode;
    NTSTATUS Status;
#if defined(_M_IX86) || defined(_M_AMD64)
    static const ULONG ValidAllocationType = (MEM_TOP_DOWN | MEM_LARGE_PAGES | MEM_DOS_LIM | SEC_NO_CHANGE | MEM_RESERVE);
#else
    static const ULONG ValidAllocationType = (MEM_TOP_DOWN | MEM_LARGE_PAGES | SEC_NO_CHANGE | MEM_RESERVE);
#endif

    DPRINT("NtMapViewOfSection: SectionHandle %p, ProcessHandle %p, ZeroBits %X, CommitSize %X, AllocationType %X, Protect %X\n",
           SectionHandle, ProcessHandle, ZeroBits, CommitSize, AllocationType, Protect);

    if (ZeroBits > MI_MAX_ZERO_BITS)
    {
        DPRINT1("NtMapViewOfSection: Invalid zero bits\n");
        return STATUS_INVALID_PARAMETER_4;
    }

    /* Check for invalid inherit disposition */
    if (InheritDisposition > ViewUnmap || InheritDisposition < ViewShare)
    {
        DPRINT1("NtMapViewOfSection: Invalid inherit disposition\n");
        return STATUS_INVALID_PARAMETER_8;
    }

    /* Allow only valid allocation types */
    if (AllocationType & ~ValidAllocationType)
    {
        DPRINT1("NtMapViewOfSection: Invalid allocation type\n");
        return STATUS_INVALID_PARAMETER_9;
    }

    /* Convert the protection mask, and validate it */
    ProtectionMask = MiMakeProtectionMask(Protect);
    if (ProtectionMask == MM_INVALID_PROTECTION)
    {
        DPRINT1("NtMapViewOfSection: Invalid page protection\n");
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    /* Now convert the protection mask into desired section access mask */
    DesiredAccess = MmMakeSectionAccess[ProtectionMask & 0x7];

    CurrentProcess = (PEPROCESS)PsGetCurrentThread()->Tcb.ApcState.Process;
    PreviousMode = PsGetCurrentThread()->Tcb.PreviousMode;

    /* Enter SEH */
    _SEH2_TRY
    {
        /* Check for unsafe parameters */
        if (PreviousMode != KernelMode)
        {
            /* Probe the parameters */
            ProbeForWritePointer(BaseAddress);
            ProbeForWriteSize_t(ViewSize);
        }

        /* Check if a section offset was given */
        if (SectionOffset)
        {
            /* Check for unsafe parameters and capture section offset */
            if (PreviousMode != KernelMode)
                ProbeForWriteLargeInteger(SectionOffset);

            SafeSectionOffset = *SectionOffset;
        }
        else
        {
            SafeSectionOffset.QuadPart = 0;
        }

        /* Capture the other parameters */
        SafeBaseAddress = *BaseAddress;
        SafeViewSize = *ViewSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Return the exception code */
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* Check for kernel-mode address */
    if (SafeBaseAddress > MM_HIGHEST_VAD_ADDRESS)
    {
        DPRINT1("NtMapViewOfSection: Kernel base not allowed\n");
        return STATUS_INVALID_PARAMETER_3;
    }

    /* Check for range entering kernel-mode */
    if (((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS - (ULONG_PTR)SafeBaseAddress) < SafeViewSize)
    {
        DPRINT1("NtMapViewOfSection: Overflowing into kernel base not allowed\n");
        return STATUS_INVALID_PARAMETER_3;
    }

    /* Check for invalid zero bits */
    if (((ULONG_PTR)SafeBaseAddress + SafeViewSize) > (0xFFFFFFFF >> ZeroBits)) // FIXME
    {
        DPRINT1("NtMapViewOfSection: Invalid zero bits\n");
        return STATUS_INVALID_PARAMETER_4;
    }

    /* Reference the process */
    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_VM_OPERATION,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID *)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtMapViewOfSection: Status %X\n", Status);
        return Status;
    }

    /* Reference the section */
    Status = ObReferenceObjectByHandle(SectionHandle,
                                       DesiredAccess,
                                       MmSectionObjectType,
                                       PreviousMode,
                                       (PVOID *)&Section,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtMapViewOfSection: Status %X\n", Status);
        ObDereferenceObject(Process);
        return Status;
    }

    if (Section->Segment->ControlArea->u.Flags.PhysicalMemory)
    {
        SafeSectionOffset.LowPart = (ULONG)PAGE_ALIGN(SafeSectionOffset.LowPart);

        if (PreviousMode == UserMode &&
            (SafeSectionOffset.QuadPart + SafeViewSize) > ((ULONGLONG)MmHighestPhysicalPage * PAGE_SIZE))
        {
            DPRINT1("NtMapViewOfSection: Denying map past highest physical page.\n");
            Status = STATUS_INVALID_PARAMETER_6;
            goto Exit;
        }
    }
    else if (!(AllocationType & MEM_DOS_LIM))
    {
        /* Check for non-allocation-granularity-aligned BaseAddress */
        if ((ULONG_PTR)SafeBaseAddress & (MM_ALLOCATION_GRANULARITY - 1))
        {
            DPRINT1("NtMapViewOfSection: BaseAddress is not at 64-kilobyte address boundary.\n");
            Status = STATUS_MAPPED_ALIGNMENT;
            goto Exit;
        }

        /* Do the same for the section offset */
        if (SectionOffset && (SafeSectionOffset.LowPart & (MM_ALLOCATION_GRANULARITY - 1)))
        {
            DPRINT1("NtMapViewOfSection: SectionOffset is not at 64-kilobyte address boundary.\n");
            Status = STATUS_MAPPED_ALIGNMENT;
            goto Exit;
        }
    }

    /* Now do the actual mapping */
    Status = MmMapViewOfSection(Section,
                                Process,
                                &SafeBaseAddress,
                                ZeroBits,
                                CommitSize,
                                &SafeSectionOffset,
                                &SafeViewSize,
                                InheritDisposition,
                                AllocationType,
                                Protect);

    /* Return data only on success */
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtMapViewOfSection: Status %X\n", Status);

        if (Status == STATUS_CONFLICTING_ADDRESSES &&
            Section->Segment->ControlArea->u.Flags.Image &&
            Process == CurrentProcess)
        {
            DbgkMapViewOfSection(Section,
                                 SafeBaseAddress,
                                 SafeSectionOffset.LowPart,
                                 SafeViewSize);
        }
        goto Exit;
    }

    /* Check if this is an image for the current process */
    if (Section->Segment->ControlArea->u.Flags.Image &&
        Process == CurrentProcess &&
        Status != STATUS_IMAGE_NOT_AT_BASE)
    {
        /* Notify the debugger */
        DbgkMapViewOfSection(Section,
                             SafeBaseAddress,
                             SafeSectionOffset.LowPart,
                             SafeViewSize);
    }

    /* Enter SEH */
    _SEH2_TRY
    {
        /* Return parameters to user */
        *BaseAddress = SafeBaseAddress;
        *ViewSize = SafeViewSize;

        if (SectionOffset)
            *SectionOffset = SafeSectionOffset;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Nothing to do */
    }
    _SEH2_END;

Exit:

    /* Dereference all objects and return status */
    ObDereferenceObject(Section);
    ObDereferenceObject(Process);

    return Status;
}

NTSTATUS
NTAPI
NtUnmapViewOfSection(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtExtendSection(
    _In_ HANDLE SectionHandle,
    _Inout_ PLARGE_INTEGER NewMaximumSize)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtQuerySection(
    _In_ HANDLE SectionHandle,
    _In_ SECTION_INFORMATION_CLASS SectionInformationClass,
    _Out_ PVOID SectionInformation,
    _In_ SIZE_T SectionInformationLength,
    _Out_opt_ PSIZE_T ResultLength)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
