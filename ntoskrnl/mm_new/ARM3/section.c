
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

#define MI_PE_HEADER_MAX_PAGES 2
#define MI_MAX_IMAGE_SECTIONS  16

#define MI_PE_HEADER_MAX_SIZE (sizeof(IMAGE_NT_HEADERS) + MI_MAX_IMAGE_SECTIONS * sizeof(IMAGE_SECTION_HEADER))

typedef struct _MI_PE_HEADER_MDL
{
    MDL Mdl;
    PFN_NUMBER PageNumber[MI_PE_HEADER_MAX_PAGES];
} MI_PE_HEADER_MDL, *PMI_PE_HEADER_MDL;

KGUARDED_MUTEX MmSectionCommitMutex;
KGUARDED_MUTEX MmSectionBasedMutex;
KEVENT MmCollidedFlushEvent;
PVOID MmHighSectionBase;
LIST_ENTRY MmUnusedSubsectionList;
MMSESSION MmSession;
MM_AVL_TABLE MmSectionBasedRoot;
ULONG MmUnusedSegmentCount = 0;
ULONG MmUnusedSubsectionCount = 0;
ULONG MmUnusedSubsectionCountPeak = 0;
SIZE_T MiUnusedSubsectionPagedPool;
//SIZE_T MiUnusedSubsectionPagedPoolPeak;

ACCESS_MASK MmMakeSectionAccess[8] =
{
    SECTION_MAP_READ,
    SECTION_MAP_READ,
    SECTION_MAP_EXECUTE,
    SECTION_MAP_EXECUTE | SECTION_MAP_READ,
    SECTION_MAP_WRITE,
    SECTION_MAP_READ,
    SECTION_MAP_EXECUTE | SECTION_MAP_WRITE,
    SECTION_MAP_EXECUTE | SECTION_MAP_READ
};

ACCESS_MASK MmMakeFileAccess[8] =
{
    FILE_READ_DATA,
    FILE_READ_DATA,
    FILE_EXECUTE,
    FILE_EXECUTE | FILE_READ_DATA,
    FILE_WRITE_DATA | FILE_READ_DATA,
    FILE_READ_DATA,
    FILE_EXECUTE | FILE_WRITE_DATA | FILE_READ_DATA,
    FILE_EXECUTE | FILE_READ_DATA
};

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

CHAR MmImageProtectionArray[16] =
{
    MM_NOACCESS,
    MM_EXECUTE,
    MM_READONLY,
    MM_EXECUTE_READ,
    MM_WRITECOPY,
    MM_EXECUTE_WRITECOPY,
    MM_WRITECOPY,
    MM_EXECUTE_WRITECOPY,
    MM_NOACCESS,
    MM_EXECUTE,
    MM_READONLY,
    MM_EXECUTE_READ,
    MM_READWRITE,
    MM_EXECUTE_READWRITE,
    MM_READWRITE,
    MM_EXECUTE_READWRITE
};

extern MI_PFN_CACHE_ATTRIBUTE MiPlatformCacheAttributes[2][MmMaximumCacheType];
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
extern SIZE_T MmAllocationFragment;

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
MiConvertStaticSubsections(
    _In_ PCONTROL_AREA ControlArea)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MiSegmentDelete(
    _In_ PSEGMENT Segment)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MiRemoveImageSectionObject(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_CONTROL_AREA ControlArea)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MiCheckControlArea(
    _In_ PCONTROL_AREA ControlArea,
    _In_ KIRQL OldIrql)
{
    PEVENT_COUNTER PurgeEvent = NULL;
    ULONG CheckFlag = 0;

    DPRINT("MiCheckControlArea: ControlArea %p, OldIrql %X\n", ControlArea, OldIrql);

    MI_ASSERT_PFN_LOCK_HELD();
    ASSERT(MmPfnOwner == KeGetCurrentThread());

    /* Check if this is the last reference or view */
    if (!ControlArea->NumberOfMappedViews &&
        !ControlArea->NumberOfSectionReferences)
    {
        /* There should be no more user references either */
        ASSERT(ControlArea->NumberOfUserReferences == 0);

        if (ControlArea->FilePointer)
        {
            if (ControlArea->NumberOfPfnReferences)
            {
                if (!ControlArea->DereferenceList.Flink)
                {
                    MI_ASSERT_PFN_LOCK_HELD();
                    ASSERT(MmPfnOwner == KeGetCurrentThread());

                    if (!ControlArea->u.Flags.Image &&
                        ControlArea->FilePointer &&
                        !ControlArea->u.Flags.PhysicalMemory)
                    {
                        MiConvertStaticSubsections(ControlArea);
                    }

                    DPRINT("MiCheckControlArea: FIXME MmUnusedSegmentList\n");

                    MmUnusedSegmentCount++;
                }

                if (ControlArea->u.Flags.DeleteOnClose)
                    CheckFlag = 1;

                if (ControlArea->u.Flags.GlobalMemory)
                {
                    ASSERT(ControlArea->u.Flags.Image == 1);

                    ControlArea->u.Flags.BeingPurged = 1;
                    ControlArea->NumberOfMappedViews = 1;

                    DPRINT("MiCheckControlArea: FIXME MiPurgeImageSection\n");
                    ASSERT(FALSE);

                    ControlArea->u.Flags.BeingPurged = 0;

                    ControlArea->NumberOfMappedViews--;

                    if (!ControlArea->NumberOfMappedViews &&
                        !ControlArea->NumberOfSectionReferences &&
                        !ControlArea->NumberOfPfnReferences)
                    {
                        CheckFlag |= 2;

                        ControlArea->u.Flags.BeingDeleted = 1;
                        ControlArea->u.Flags.FilePointerNull = 1;

                        DPRINT("MiCheckControlArea: FIXME MiRemoveImageSectionObject\n");
                        ASSERT(FALSE);
                    }
                    else
                    {
                        PurgeEvent = ControlArea->WaitingForDeletion;
                        ControlArea->WaitingForDeletion = 0;
                    }
                }

                if (CheckFlag == 1)
                {
                    ControlArea->u.Flags.BeingDeleted = 1;
                    ControlArea->NumberOfMappedViews = 1;
                }
            }
            else
            {
                ControlArea->u.Flags.BeingDeleted = 1;

                CheckFlag = 2;

                ASSERT(ControlArea->u.Flags.FilePointerNull == 0);
                ControlArea->u.Flags.FilePointerNull = 1;

                if (ControlArea->u.Flags.Image)
                {
                    DPRINT("MiCheckControlArea: FIXME MiRemoveImageSectionObject\n");
                    ASSERT(FALSE);
                }
                else
                {
                    ASSERT(((PCONTROL_AREA)(ControlArea->FilePointer->SectionObjectPointer->DataSectionObject)) != NULL);
                    ControlArea->FilePointer->SectionObjectPointer->DataSectionObject = NULL;
                }
            }
        }
        else
        {
            ControlArea->u.Flags.BeingDeleted = 1;
            CheckFlag = 2;
        }
    }
    else
    {
        /* Check if waiting for deletion */
        if (ControlArea->WaitingForDeletion)
        {
            /* Get event */
            PurgeEvent = ControlArea->WaitingForDeletion;
            ControlArea->WaitingForDeletion = NULL;
        }
    }

    /* Release the PFN lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    if (!CheckFlag)
    {
        if (PurgeEvent)
            KeSetEvent(&PurgeEvent->Event, 0, FALSE);

        /* Not yet supported */
        DPRINT1("MiCheckControlArea: FIXME MmUnusedSegmentCleanup\n");
        //ASSERT(FALSE);

        return;
    }

    /* No more user write references at all */
    ASSERT(ControlArea->WritableUserReferences == 0);
    ASSERT(PurgeEvent == NULL);

    if (CheckFlag & 2)
    {
        /* Delete the segment if needed */
        MiSegmentDelete(ControlArea->Segment);
        return;
    }

    /* Clean the section */
    DPRINT("MiCheckControlArea: FIXME MiCleanSection\n");
    ASSERT(FALSE);
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
    PMM_PHYSICAL_VIEW PhysicalView;
    PMMADDRESS_NODE Parent;
    PETHREAD Thread;
    PMMVAD_LONG VadLong;
    ULONG_PTR StartingAddress;
    ULONG_PTR EndingAddress;
    ULONG_PTR TmpStartingAddress;
    ULONG_PTR HighestAddress;
    PMMPTE Pde;
    PMMPTE LastPte;
    PMMPTE Pte;
    MMPTE TempPte;
    PFN_NUMBER StartPageNumber;
    PFN_NUMBER CurrentPageNumber;
    PFN_COUNT PagesCount;
    PMMPFN Pfn;
    PVOID UsedAddress;
    MI_PFN_CACHE_ATTRIBUTE CacheAttribute;
    MEMORY_CACHING_TYPE InputCacheType;
    ULONG SizeOfRange;
    ULONG Offset;
    BOOLEAN IsIoMapping;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("MiMapViewOfPhysicalSection: %p, %p, Base [%p], Offset [%I64X], ViewSize [%I64X], %X, %X, %X\n",
           ControlArea, Process, (BaseAddress ? *BaseAddress : NULL), (SectionOffset ? SectionOffset->QuadPart : 0),
           (ViewSize ? (ULONGLONG)*ViewSize : 0), ProtectionMask, ZeroBits, AllocationType);

    if (AllocationType & (MEM_RESERVE | MEM_LARGE_PAGES))
    {
        DPRINT1("MiMapViewOfPhysicalSection: STATUS_INVALID_PARAMETER_9\n");
        return STATUS_INVALID_PARAMETER_9;
    }

    if ((ProtectionMask & MM_PROTECT_SPECIAL) == MM_GUARDPAGE ||
        ProtectionMask == MM_NOACCESS)
    {
        DPRINT1("MiMapViewOfPhysicalSection: STATUS_INVALID_PAGE_PROTECTION\n");
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    Offset = SectionOffset->LowPart & (MM_ALLOCATION_GRANULARITY - 1);

    if (*BaseAddress)
    {
        StartingAddress = (ALIGN_DOWN_BY(*BaseAddress, MM_ALLOCATION_GRANULARITY) + Offset);
        EndingAddress = ((StartingAddress + *ViewSize - 1) | (PAGE_SIZE - 1));

        DPRINT("MiMapViewOfPhysicalSection: %p, %p, %p\n", *BaseAddress, StartingAddress, EndingAddress);

        if (MiCheckForConflictingVadExistence(Process, StartingAddress, EndingAddress))
        {
            DPRINT1("MiMapViewOfPhysicalSection: STATUS_CONFLICTING_ADDRESSES\n");
            return STATUS_CONFLICTING_ADDRESSES;
        }
    }
    else
    {
        ASSERT(SectionOffset->HighPart == 0);

        SizeOfRange = (Offset + *ViewSize);

        if ((AllocationType & MEM_TOP_DOWN) || Process->VmTopDown)
        {
            if (ZeroBits)
            {
                HighestAddress = ((ULONG_PTR)MI_HIGHEST_SYSTEM_ADDRESS >> ZeroBits);

                if (HighestAddress > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
                    HighestAddress = (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;
            }
            else
            {
                HighestAddress = (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;
            }

            Status = MiFindEmptyAddressRangeDownTree(SizeOfRange,
                                                     HighestAddress,
                                                     MM_ALLOCATION_GRANULARITY,
                                                     &Process->VadRoot,
                                                     &TmpStartingAddress,
                                                     &Parent);
        }
        else
        {
            Status = MiFindEmptyAddressRange(SizeOfRange, MM_ALLOCATION_GRANULARITY, ZeroBits, &TmpStartingAddress);
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MiMapViewOfPhysicalSection: Status %X\n", Status);
            return Status;
        }

        StartingAddress = (TmpStartingAddress + Offset);
        EndingAddress = (StartingAddress + *ViewSize - 1) | (PAGE_SIZE - 1);

        DPRINT1("MiMapViewOfPhysicalSection: %p, %p, %p\n", TmpStartingAddress, StartingAddress, EndingAddress);

        if (ZeroBits && (EndingAddress > ((ULONG_PTR)MI_HIGHEST_SYSTEM_ADDRESS >> ZeroBits)))
        {
            DPRINT1("MiMapViewOfPhysicalSection: STATUS_NO_MEMORY\n");
            return STATUS_NO_MEMORY;
        }
    }

    Pde = MiAddressToPde((PVOID)StartingAddress);
    Pte = MiAddressToPte((PVOID)StartingAddress);
    LastPte = MiAddressToPte((PVOID)EndingAddress);

    StartPageNumber = (PFN_NUMBER)(SectionOffset->QuadPart / PAGE_SIZE);
    MI_MAKE_HARDWARE_PTE_USER(&TempPte, Pte, ProtectionMask, StartPageNumber);

    if (TempPte.u.Hard.Write)
        MI_MAKE_DIRTY_PAGE(&TempPte);

    /* Is IO mapping */
    IsIoMapping = TRUE;

    if (StartPageNumber <= MmHighestPhysicalPage) // should be MmHighestPossiblePhysicalPage
    {
        if (MiGetPfnEntry(StartPageNumber))
            /* Is MEMORY mapping */
            IsIoMapping = FALSE;
    }

    InputCacheType = MmCached;

    if ((ProtectionMask & MM_WRITECOMBINE) == MM_WRITECOMBINE && (ProtectionMask & MM_PROTECT_ACCESS))
        InputCacheType = MmWriteCombined;
    else if ((ProtectionMask & MM_NOCACHE) == MM_NOCACHE)
        InputCacheType = MmNonCached;

    ASSERT(InputCacheType <= MmWriteCombined);

    if (IsIoMapping)
        CacheAttribute = MiPlatformCacheAttributes[IsIoMapping][InputCacheType];
    else
        CacheAttribute = MiPlatformCacheAttributes[IsIoMapping][InputCacheType];

    PagesCount = (LastPte - Pte + 1);

    if (CacheAttribute != MiCached)
    {
        if (CacheAttribute == MiWriteCombined)
        {
            // FIXME MiWriteCombiningPtes
            {
                TempPte.u.Hard.CacheDisable = 1;
                TempPte.u.Hard.WriteThrough = 0;
            }

        }
        else if (CacheAttribute == MiNonCached)
        {
            TempPte.u.Hard.CacheDisable = 1;
            TempPte.u.Hard.WriteThrough = 1;
        }

        for (CurrentPageNumber = StartPageNumber;
             CurrentPageNumber < (StartPageNumber + PagesCount);
             CurrentPageNumber++)
        {
            DPRINT1("MiMapViewOfPhysicalSection: FIXME MiMustFrameBeCached\n");
            ASSERT(FALSE);
        }
    }

    if (!Process->PhysicalVadRoot)
    {
        if (!MiCreatePhysicalVadRoot(Process, FALSE))
        {
            DPRINT1("MiMapViewOfPhysicalSection: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    PhysicalView = ExAllocatePoolWithTag(NonPagedPool, sizeof(MM_PHYSICAL_VIEW), 'vpmM');
    if (!PhysicalView)
    {
        DPRINT1("MiMapViewOfPhysicalSection: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    VadLong = ExAllocatePoolWithTag(NonPagedPool, sizeof(MMVAD_LONG), 'ldaV');
    if (!VadLong)
    {
        DPRINT1("MiMapViewOfPhysicalSection: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(PhysicalView, 'vpmM');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(VadLong, sizeof(MMVAD_LONG));

    VadLong->ControlArea = ControlArea;
    VadLong->StartingVpn = (StartingAddress / PAGE_SIZE);
    VadLong->EndingVpn = (EndingAddress / PAGE_SIZE);

    VadLong->u.VadFlags.VadType = VadDevicePhysicalMemory;
    VadLong->u.VadFlags.Protection = ProtectionMask;

    VadLong->u2.VadFlags2.Inherit = 0;
    VadLong->u2.VadFlags2.LongVad = 1;

    VadLong->LastContiguousPte = (PMMPTE)StartPageNumber;
    VadLong->FirstPrototypePte = (PMMPTE)StartPageNumber;

    PhysicalView->Vad = (PMMVAD)VadLong;
    PhysicalView->StartingVpn = VadLong->StartingVpn;
    PhysicalView->EndingVpn = VadLong->EndingVpn;
    PhysicalView->VadType = VadDevicePhysicalMemory;

    DPRINT1("MiMapViewOfPhysicalSection: FIXME MiCheckCacheAttributes\n");
    //ASSERT(FALSE);

    Thread = PsGetCurrentThread();

    Status = MiInsertVadCharges((PMMVAD)VadLong, Process);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MiMapViewOfPhysicalSection: Status %X\n", Status);
        ExFreePoolWithTag(PhysicalView, 'vpmM');
        ExFreePoolWithTag(VadLong, 'ldaV');
        return Status;
    }

    MiLockProcessWorkingSetUnsafe(Process, Thread);
    MiInsertVad((PMMVAD)VadLong, &Process->VadRoot);

    if (CacheAttribute != MiCached)
    {
        DPRINT1("MiMapViewOfPhysicalSection: FIXME\n");
        //MiFlushType[32]++;
        KeFlushEntireTb(TRUE, TRUE);
        KeInvalidateAllCaches();
    }

    MiMakePdeExistAndMakeValid(Pde, Process, MM_NOIRQL);

    Pfn = MI_PFN_ELEMENT(Pde->u.Hard.PageFrameNumber);
    UsedAddress = (PVOID)StartingAddress;

    while (Pte <= LastPte)
    {
        if (!MiIsPteOnPdeBoundary(Pte))
        {
            Pde = MiAddressToPte(Pte);
            MiMakePdeExistAndMakeValid(Pde, Process, MM_NOIRQL);

            Pfn = MI_PFN_ELEMENT(Pde->u.Hard.PageFrameNumber);
            UsedAddress = MiPteToAddress(Pte);
        }

        MiIncrementPageTableReferences(UsedAddress);

        DPRINT("MiMapViewOfPhysicalSection: [%d] Address %p, RefCount %X\n",
               MiAddressToPdeOffset(UsedAddress), UsedAddress, MiQueryPageTableReferences(UsedAddress));

        ASSERT(Pte->u.Long == 0);
        MI_WRITE_VALID_PTE(Pte, TempPte);

        OldIrql = MiLockPfnDb(APC_LEVEL);
        Pfn->u2.ShareCount++;
        MiUnlockPfnDb(OldIrql, APC_LEVEL);

        Pte++;
        TempPte.u.Hard.PageFrameNumber++;
    }

    MiInsertPhysicalViewAndRefControlArea(Process, ControlArea, PhysicalView);
    MiUnlockProcessWorkingSetUnsafe(Process, Thread);

    *BaseAddress = (PVOID)StartingAddress;
    *ViewSize = (EndingAddress - StartingAddress + 1);

    DPRINT("MiMapViewOfPhysicalSection: Base %p, ViewSize %p\n", *BaseAddress, *ViewSize);

    Process->VirtualSize += *ViewSize;

    if (Process->VirtualSize > Process->PeakVirtualSize)
        Process->PeakVirtualSize = Process->VirtualSize;

    return STATUS_SUCCESS;
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

PSUBSECTION
NTAPI
MiLocateSubsection(
    _In_ PMMVAD Vad,
    _In_ ULONG_PTR Vpn)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MiDecrementSubsections(
    _In_ PSUBSECTION FirstSubsection,
    _In_ PSUBSECTION LastSubsection)
{
    PMSUBSECTION MappedSubsection;
    PSUBSECTION subsection;

    DPRINT("MiDecrementSubsections: FirstSubsection %p, LastSubsection %p\n", FirstSubsection, LastSubsection);

    ASSERT((FirstSubsection->ControlArea->u.Flags.Image == 0) &&
           (FirstSubsection->ControlArea->FilePointer != NULL) &&
           (FirstSubsection->ControlArea->u.Flags.PhysicalMemory == 0));

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(MmPfnOwner == KeGetCurrentThread());

    for (subsection = FirstSubsection; ; subsection = subsection->NextSubsection)
    {
        MappedSubsection = (PMSUBSECTION)subsection;

        ASSERT(MappedSubsection->DereferenceList.Flink == NULL);

        ASSERT(((LONG_PTR)MappedSubsection->NumberOfMappedViews >= 1) ||
               (MappedSubsection->u.SubsectionFlags.SubsectionStatic == 1));

        MappedSubsection->NumberOfMappedViews--;

        if (!MappedSubsection->NumberOfMappedViews &&
            !MappedSubsection->u.SubsectionFlags.SubsectionStatic)
        {
            InsertTailList(&MmUnusedSubsectionList, &MappedSubsection->DereferenceList);
            FreePoolForSubsectionPtes(MappedSubsection->PtesInSubsection + MappedSubsection->UnusedPtes);
        }

        if ((LastSubsection && subsection == LastSubsection) ||
            !subsection->NextSubsection)
        {
            break;
        }
    }
}

VOID
NTAPI
MiRemoveMappedView(
    _In_ PEPROCESS Process,
    _In_ PMMVAD Vad)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PCONTROL_AREA ControlArea;
    PMMEXTEND_INFO ExtendedInfo;
    PSUBSECTION LastSubsection;
    PSUBSECTION FirstSubsection;
    PVOID UsedAddress;
    PMMPTE Pde;
    PMMPTE Pte;
    PMMPTE LastPte;
    PMMPFN Pfn;
    PFN_NUMBER PdePage;
    KIRQL OldIrql;

    DPRINT("MiRemoveMappedView: Process %p, Vad %p\n", Process, Vad);

    /* Get the control area */
    ControlArea = Vad->ControlArea;

    /* If view of the physical section */
    if (Vad->u.VadFlags.VadType == VadDevicePhysicalMemory)
    {
        if (((PMMVAD_LONG)Vad)->u4.Banked)
        {
            DPRINT1("MiRemoveMappedView: FIXME\n");
            ASSERT(FALSE);
        }

        /* Remove Physical View */
        MiPhysicalViewRemover(Process, Vad);

        Pde = MiAddressToPde(Vad->StartingVpn * PAGE_SIZE);
        ASSERT(Pde->u.Hard.Valid == 1);

        Pte = MiAddressToPte(Vad->StartingVpn * PAGE_SIZE);
        LastPte = MiAddressToPte(Vad->EndingVpn * PAGE_SIZE);

        if (!Pde->u.Hard.LargePage)
        {
            PdePage = Pde->u.Hard.PageFrameNumber;
            UsedAddress = (PVOID)(Vad->StartingVpn * PAGE_SIZE);

            /* Lock the PFN database */
            OldIrql = MiLockPfnDb(APC_LEVEL);

            while (Pte <= LastPte)
            {
                /* Check if we're on a PDE boundary */
                if (MiIsPteOnPdeBoundary(Pte))
                {
                    Pde = MiAddressToPte(Pte);
                    PdePage = Pde->u.Hard.PageFrameNumber;
                    UsedAddress = MiPteToAddress(Pte);
                }

                /* Add an additional page table reference */
                MiDecrementPageTableReferences(UsedAddress);

                Pte->u.Long = 0;
                Pfn = &MmPfnDatabase[PdePage];

                if (Pfn->u2.ShareCount != 1)
                {
                    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
                    ASSERT(MmPfnOwner == KeGetCurrentThread());
                    ASSERT(PdePage > 0);
                    ASSERT(MiGetPfnEntry(PdePage) != NULL);
                    ASSERT(&MmPfnDatabase[PdePage] == Pfn);
                    ASSERT(Pfn->u2.ShareCount != 0);

                    if (Pfn->u3.e1.PageLocation != ActiveAndValid &&
                        Pfn->u3.e1.PageLocation != StandbyPageList)
                    {
                        DPRINT1("MiRemoveMappedView: FIXME\n");
                        ASSERT(FALSE);
                    }

                    /* Just decrease share count */
                    Pfn->u2.ShareCount--;
                    ASSERT(Pfn->u2.ShareCount < 0xF000000);
                }
                else
                {
                    /* Decrement the share count on the page */
                    MiDecrementShareCount(Pfn, PdePage);
                }

                /* See if we should delete it */
                if (!MiQueryPageTableReferences(UsedAddress))
                {
                    DPRINT1("MiRemoveMappedView: FIXME\n");
                    ASSERT(FALSE);
                }

                Pte++;
            }

            KeFlushProcessTb();

            /* Release the PFN lock */
            MiUnlockPfnDb(OldIrql, APC_LEVEL);

            /* Release the working set */
            MiUnlockProcessWorkingSetUnsafe(Process, CurrentThread);

            /* Lock the PFN database */
            OldIrql = MiLockPfnDb(APC_LEVEL);
        }
        else
        {
            /* Not supported yet */
            DPRINT1("MiRemoveMappedView: FIXME\n");
            ASSERT(FALSE);

            /* Release the working set */
            MiUnlockProcessWorkingSetUnsafe(Process, CurrentThread);

            /* Lock the PFN database */
            OldIrql = MiLockPfnDb(APC_LEVEL);
        }
    }
    else
    {
        if (Vad->u2.VadFlags2.ExtendableFile)
        {
            PMMVAD_LONG VadLong = (PMMVAD_LONG)Vad;

            /* Release the working set */
            MiUnlockProcessWorkingSetUnsafe(Process, CurrentThread);

            ExtendedInfo = NULL;

            /* Acquire the lock */
            KeAcquireGuardedMutexUnsafe(&MmSectionBasedMutex);

            ASSERT(VadLong->ControlArea->Segment->ExtendInfo == VadLong->u4.ExtendedInfo);
            VadLong->u4.ExtendedInfo->ReferenceCount--;

            if (!VadLong->u4.ExtendedInfo->ReferenceCount)
            {
                ExtendedInfo = VadLong->u4.ExtendedInfo;
                VadLong->ControlArea->Segment->ExtendInfo = NULL;
            }

            /* Now that we're done, release the lock */
            KeReleaseGuardedMutexUnsafe (&MmSectionBasedMutex);

            if (ExtendedInfo)
                ExFreePoolWithTag(ExtendedInfo, 'xCmM');

            /* Lock the working set */
            MiLockProcessWorkingSetUnsafe(Process, CurrentThread);
        }

        FirstSubsection = 0;
        LastSubsection = 0;

        if (Vad->u.VadFlags.VadType == VadImageMap)
        {
            Pde = MiAddressToPde(Vad->StartingVpn * PAGE_SIZE);

            if (Pde->u.Hard.Valid && Pde->u.Hard.LargePage)
            {
                DPRINT1("MiRemoveMappedView: FIXME\n");
                ASSERT(FALSE);

                /* Lock the PFN database */
                OldIrql = MiLockPfnDb(APC_LEVEL);
    
                /* Increase the reference counts */
                ControlArea->NumberOfMappedViews--;
                ControlArea->NumberOfUserReferences--;

                /* Check if it should be destroyed and return*/
                MiCheckControlArea(ControlArea, OldIrql);
                return;
            }
        }
        else
        {
            if (ControlArea->FilePointer)
            {
                if (Vad->u.VadFlags.Protection == MM_READWRITE ||
                    Vad->u.VadFlags.Protection == MM_EXECUTE_READWRITE)
                {
                    /* Add a reference */
                    InterlockedDecrement ((volatile PLONG)&ControlArea->WritableUserReferences);
                }

                FirstSubsection = MiLocateSubsection(Vad, Vad->StartingVpn);
                LastSubsection = MiLocateSubsection(Vad, Vad->EndingVpn);

                ASSERT(FirstSubsection != NULL);

                DPRINT("MiRemoveMappedView: FirstSubsection %p, LastSubsection %p\n", FirstSubsection, LastSubsection);
            }
        }

        if (Vad->u.VadFlags.VadType == VadLargePageSection)
        {
            DPRINT1("MiRemoveMappedView: FIXME\n");
            ASSERT(FALSE);
        }
        else
        {
            /* Delete the actual virtual memory pages */
            MiDeleteVirtualAddresses((Vad->StartingVpn * PAGE_SIZE),
                                     ((Vad->EndingVpn * PAGE_SIZE) | (PAGE_SIZE - 1)),
                                     Vad);
        }

        /* Release the working set */
        MiUnlockProcessWorkingSetUnsafe(Process, CurrentThread);

        /* Lock the PFN database */
        OldIrql = MiLockPfnDb(APC_LEVEL);

        if (FirstSubsection)
            MiDecrementSubsections(FirstSubsection, LastSubsection);
    }

    if (ControlArea)
    {
        /* Decrease the reference counts */
        ControlArea->NumberOfMappedViews--;
        ControlArea->NumberOfUserReferences--;

        /* Check if it should be destroyed and return */
        MiCheckControlArea(ControlArea, OldIrql);
        return;
    }

    /* Release the PFN lock and return */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    ASSERT(Vad->u.VadFlags.VadType == VadDevicePhysicalMemory);
    ASSERT(((PMMVAD_LONG)Vad)->u4.Banked == NULL);
    ASSERT(Vad->ControlArea == NULL);
    ASSERT(Vad->FirstPrototypePte == NULL);
}

NTSTATUS
NTAPI
MiUnmapViewOfSection(
    _In_ PEPROCESS Process,
    _In_ PVOID BaseAddress,
    _In_ ULONG Flags)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PEPROCESS CurrentProcess = PsGetCurrentProcess();
    PMMVAD Vad;
    PVOID DbgBase = NULL;
    SIZE_T RegionSize;
    ULONG_PTR StartingAddress;
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("MiUnmapViewOfSection: Process %p, BaseAddress %p, Flags %X\n", Process, BaseAddress, Flags);

    /* Check if we should attach to the process */
    if (CurrentProcess != Process)
    {
        /* The process is different, do an attach */
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    /* Check if we need to lock the address space */
    if (!(Flags & 0x01))
        MmLockAddressSpace(&Process->Vm);

    /* Check if the process is already daed */
    if (Process->VmDeleted)
    {
        /* Fail the call */
        DPRINT1("MiUnmapViewOfSection: STATUS_PROCESS_IS_TERMINATING\n");

        if (!(Flags & 0x01))
            MmUnlockAddressSpace(&Process->Vm);

        Status = STATUS_PROCESS_IS_TERMINATING;
        goto Exit;
    }

    /* Find the VAD for the address and make sure it's a section VAD */
    Vad = MiLocateAddress(BaseAddress);

    if (!Vad || Vad->u.VadFlags.PrivateMemory)
    {
        /* Couldn't find it, or invalid VAD, fail */
        DPRINT1("MiUnmapViewOfSection: No VAD or invalid VAD\n");

        if (!(Flags & 0x01))
            MmUnlockAddressSpace(&Process->Vm);

        Status = STATUS_NOT_MAPPED_VIEW;
        goto Exit;
    }

    /* We should be attached */
    ASSERT(Process == PsGetCurrentProcess());

    StartingAddress = (Vad->StartingVpn * PAGE_SIZE);

    /* We need the base address for the debugger message on image-backed VADs */
    if (Vad->u.VadFlags.VadType == VadImageMap)
    {
        DbgBase = (PVOID)StartingAddress;
    }

    /* Compute the size of the VAD region */
    RegionSize = ((Vad->EndingVpn - Vad->StartingVpn + 1) * PAGE_SIZE);

    /* For SEC_NO_CHANGE sections, we need some extra checks */
    if (Vad->u.VadFlags.NoChange)
    {
        /* Are we allowed to mess with this VAD? */
        Status = MiCheckSecuredVad(Vad,
                                   (PVOID)StartingAddress,
                                   RegionSize,
                                   MM_DELETE_CHECK);
        if (!NT_SUCCESS(Status))
        {
            /* We failed */
            DPRINT1("MiUnmapViewOfSection: Trying to unmap protected VAD! Status %X\n", Status);

            if (!(Flags & 0x01))
                MmUnlockAddressSpace(&Process->Vm);
 
            goto Exit1;
        }
    }

    if (Vad->u.VadFlags.VadType != VadRotatePhysical)
    {
        /* Remove VAD charges */
        MiRemoveVadCharges(Vad, Process);

        /* Lock the working set */
        MiLockProcessWorkingSetUnsafe(Process, CurrentThread);
    }
    else
    {
        if (Flags & 0x02)
        {
            /* Remove VAD charges */
            MiRemoveVadCharges(Vad, Process);

            /* Lock the working set */
            MiLockProcessWorkingSetUnsafe(Process, CurrentThread);

            /* Remove Physical View */
            MiPhysicalViewRemover(Process, Vad);
        }
        else
        {
            if (!(Flags & 0x01))
                MmUnlockAddressSpace(&Process->Vm);

            DPRINT1("MiUnmapViewOfSection: STATUS_NOT_MAPPED_VIEW\n");
            Status = STATUS_NOT_MAPPED_VIEW;
            goto Exit1;
        }
    }

    ASSERT(Process == PsGetCurrentProcess());
    ASSERT(Process->VadRoot.NumberGenericTableElements >= 1);

    /* Remove the VAD */
    MiRemoveNode((PMMADDRESS_NODE)Vad, &Process->VadRoot);

    if (Process->VadRoot.NodeHint == Vad)
    {
        Process->VadRoot.NodeHint = Process->VadRoot.BalancedRoot.RightChild;

        if(!Process->VadRoot.NumberGenericTableElements)
            Process->VadRoot.NodeHint = NULL;
    }

    /* Remove the PTEs for this view, which also releases the working set lock */
    MiRemoveMappedView(Process, Vad);

    /* Remove commitment */
    DPRINT("MiUnmapViewOfSection: FIXME MiReturnPageTablePageCommitment\n");

    /* Update performance counter and release the lock */
    Process->VirtualSize -= RegionSize;

    if (!(Flags & 0x01))
        MmUnlockAddressSpace(&Process->Vm);

    /* Destroy the VAD and return success */
    ExFreePool(Vad);

    Status = STATUS_SUCCESS;

    /* Failure and success case -- send debugger message, detach, and return */

Exit1:

    if (DbgBase)
        DbgkUnMapViewOfSection(DbgBase);

Exit:

    if (Attached)
        KeUnstackDetachProcess(&ApcState);

    return Status;
}

PCONTROL_AREA
NTAPI
MiFindImageSectionObject(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN IsLocked,
    _Out_ BOOLEAN* OutIsGlobal)
{
    PLARGE_CONTROL_AREA ControlArea;
    PLIST_ENTRY Header;
    PLIST_ENTRY Entry;
    ULONG SessionId;
    KIRQL OldIrql = PASSIVE_LEVEL;

    DPRINT("MiFindImageSectionObject: FileObject %p\n", FileObject);

    *OutIsGlobal = FALSE;

    if (!IsLocked)
    {
        OldIrql = MiLockPfnDb(APC_LEVEL);
    }
    else
    {
        ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
        ASSERT(MmPfnOwner == KeGetCurrentThread());
    }

    if (!FileObject->SectionObjectPointer->ImageSectionObject)
        goto Exit;

    ControlArea = FileObject->SectionObjectPointer->ImageSectionObject;

    if (!ControlArea->u.Flags.GlobalOnlyPerSession)
        goto Exit;

    SessionId = MmGetSessionId(PsGetCurrentProcess());

    if (ControlArea->SessionId == SessionId)
        goto Exit;

    Header = &ControlArea->UserGlobalList;

    for (Entry = ControlArea->UserGlobalList.Flink;
         Entry != Header;
         Entry = Entry->Flink)
    {
        ControlArea = CONTAINING_RECORD(Entry, LARGE_CONTROL_AREA, UserGlobalList);

        ASSERT(ControlArea->u.Flags.GlobalOnlyPerSession == 1);

        if (ControlArea->SessionId == SessionId)
            goto Exit;
    }

    ControlArea = NULL;
    *OutIsGlobal = TRUE;

Exit:

    if (!IsLocked)
         MiUnlockPfnDb(OldIrql, APC_LEVEL);

    return (PCONTROL_AREA)ControlArea;
}

VOID
NTAPI
MiInsertImageSectionObject(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_CONTROL_AREA ControlArea)
{
    PLARGE_CONTROL_AREA NextControlArea;
    PLARGE_CONTROL_AREA controlArea;
    PLIST_ENTRY Header;
    PLIST_ENTRY Entry;
    ULONG SessionId;

    DPRINT("MiInsertImageSectionObject: FileObject %p, ControlArea %p\n", FileObject, ControlArea);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(MmPfnOwner == KeGetCurrentThread());

    if (!FileObject->SectionObjectPointer->ImageSectionObject &&
        !ControlArea->u.Flags.GlobalOnlyPerSession)
    {
        goto Exit;
    }

    ASSERT(ControlArea->u.Flags.GlobalOnlyPerSession == 1);

    SessionId = MmGetSessionId(PsGetCurrentProcess());
    ControlArea->SessionId = SessionId;

    if (!FileObject->SectionObjectPointer->ImageSectionObject)
    {
        InitializeListHead(&ControlArea->UserGlobalList);
        goto Exit;
    }

    ASSERT(ControlArea->u.Flags.BeingDeleted ||
           ControlArea->u.Flags.BeingCreated ||
           ControlArea->SessionId != (ULONG)-1);

    controlArea = FileObject->SectionObjectPointer->ImageSectionObject;
    Header = &controlArea->UserGlobalList;

    for (Entry = Header->Flink;
         Entry != Header;
         Entry = Entry->Flink)
    {
        NextControlArea = CONTAINING_RECORD(Entry, LARGE_CONTROL_AREA, UserGlobalList);

        ASSERT(NextControlArea->SessionId != (ULONG)-1 &&
               NextControlArea->SessionId != ControlArea->SessionId);
    }

    InsertTailList(Header, &ControlArea->UserGlobalList);

Exit:

    FileObject->SectionObjectPointer->ImageSectionObject = ControlArea;
}

VOID
NTAPI
MiSubsectionConsistent(
    _In_ PSUBSECTION Subsection)
{
    ULONG NumberOfFullSectors = Subsection->NumberOfFullSectors;

    DPRINT("MiSubsectionConsistent: Subsection %p, NumberOfFullSectors %X\n", Subsection, NumberOfFullSectors);

    if (Subsection->u.SubsectionFlags.SectorEndOffset)
        NumberOfFullSectors++;

    /* Therefore, then number of PTEs should be equal to the number of sectors */
    if (NumberOfFullSectors == Subsection->PtesInSubsection)
        return;

    DPRINT1("MiSubsectionConsistent: Subsection inconsistent (%X vs %X)\n",
            NumberOfFullSectors, Subsection->PtesInSubsection);

    DbgBreakPoint();
}

NTSTATUS
NTAPI
MiCreateDataFileMap(
    _In_ PFILE_OBJECT File,
    _Out_ PSEGMENT* OutSegment,
    _In_ PSIZE_T MaximumSize,
    _In_ ULONG SectionPageProtection,
    _In_ ULONG AllocationAttributes,
    _In_ BOOLEAN IgnoreFileSizing)
{
    LARGE_INTEGER fileSize;
    ULONGLONG FileSize;
    ULONGLONG TotalNumberOfPtes;
    ULONGLONG CurrentSize;
    ULONGLONG CurrentPtes;
    PCONTROL_AREA ControlArea;
    PMAPPED_FILE_SEGMENT Segment;
    PMSUBSECTION NewSubsection;
    PMSUBSECTION Subsection;
    PMSUBSECTION LastSubsection;
    ULONG NumberOfNewSubsections;
    ULONG SubsectionSize;
    MMPTE ProtoTemplate;
    NTSTATUS Status;
    ULONGLONG maximum;
    PULONGLONG maximumSize;

    PAGED_CODE();
    DPRINT("MiCreateDataFileMap: %p '%wZ', %I64X, %X, %X, %X\n", File, &File->FileName, MaximumSize, SectionPageProtection, AllocationAttributes, IgnoreFileSizing);

    if (MaximumSize)
    {
         maximum = *MaximumSize;
         maximumSize = &maximum;
         //DPRINT("MiCreateDataFileMap: maximumSize %I64X\n", maximum);
    }

    if (IgnoreFileSizing)
    {
        /* CC is caller */
        FileSize = *maximumSize;
    }
    else
    {
        /* Get size via fs */
        Status = FsRtlGetFileSize(File, &fileSize);
        if (Status == STATUS_FILE_IS_A_DIRECTORY)
        {
            DPRINT1("MiCreateDataFileMap: STATUS_FILE_IS_A_DIRECTORY\n");
            return STATUS_INVALID_FILE_FOR_SECTION;
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MiCreateDataFileMap: Status %X\n", Status);
            return Status;
        }

        FileSize = (ULONGLONG)fileSize.QuadPart;

        if (!FileSize && !(*maximumSize))
        {
            DPRINT1("MiCreateDataFileMap: STATUS_MAPPED_FILE_SIZE_ZERO\n");
            return STATUS_MAPPED_FILE_SIZE_ZERO;
        }

        //DPRINT("MiCreateDataFileMap: FileSize %I64X\n", FileSize);

        if (*maximumSize > FileSize)
        {
            if (!(SectionPageProtection & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            {
                DPRINT1("MiCreateDataFileMap: STATUS_SECTION_TOO_BIG\n");
                return STATUS_SECTION_TOO_BIG;
            }

            fileSize.QuadPart = (LONGLONG)*maximumSize;

            DPRINT1("MiCreateDataFileMap: FsRtlSetFileSize\n");
            ASSERT(FALSE);
            Status = STATUS_NOT_IMPLEMENTED;//FsRtlSetFileSize(File, &fileSize);
            if (!NT_SUCCESS(Status))
            {
                DPRINT("MiCreateDataFileMap: Status %X\n", Status);
                return Status;
            }
        }
    }

    if (FileSize >= ((16 * _1PB) & ~(PAGE_SIZE - 1)))
    {
        DPRINT1("MiCreateDataFileMap: STATUS_SECTION_TOO_BIG\n");
        return STATUS_SECTION_TOO_BIG;
    }

    TotalNumberOfPtes = ((FileSize + (PAGE_SIZE - 1)) / PAGE_SIZE);

    Segment = ExAllocatePoolWithTag(PagedPool, sizeof(MAPPED_FILE_SEGMENT), 'mSmM');
    if (!Segment)
    {
        DPRINT1("MiCreateDataFileMap: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ASSERT(BYTE_OFFSET(MmAllocationFragment) == 0);
    ASSERT(MmAllocationFragment >= PAGE_SIZE);

    ControlArea = (PCONTROL_AREA)File->SectionObjectPointer->DataSectionObject;

    NumberOfNewSubsections = 0;
    SubsectionSize = MmAllocationFragment;

    NewSubsection = NULL;
    LastSubsection = NULL;

    /* Split file on parts sizeof MmAllocationFragment */
    for (CurrentSize = (TotalNumberOfPtes * sizeof(MMPTE));
         CurrentSize;
         CurrentSize -= SubsectionSize)
    {
        if (CurrentSize < MmAllocationFragment)
        {
            CurrentSize = PAGE_ROUND_UP(CurrentSize);
            SubsectionSize = CurrentSize;
        }

        /* Allocate subsections */
        if (NewSubsection)
        {
            NewSubsection = ExAllocatePoolWithTag(NonPagedPool, sizeof(MSUBSECTION), 'cSmM');
            if (!NewSubsection)
            {
                PMSUBSECTION NextSubsection;

                DPRINT1("MiCreateDataFileMap: STATUS_INSUFFICIENT_RESOURCES\n");

                ExFreePoolWithTag(Segment, 'mSmM');

                for (NewSubsection = (PMSUBSECTION)((PMSUBSECTION)&ControlArea[1])->NextSubsection;
                     NewSubsection;
                     NewSubsection = NextSubsection)
                {
                    NextSubsection = (PMSUBSECTION)NewSubsection->NextSubsection;
                    ExFreePoolWithTag(NewSubsection, 'cSmM');
                }

                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlZeroMemory(NewSubsection, sizeof(MSUBSECTION));
            LastSubsection->NextSubsection = (PSUBSECTION)NewSubsection;
        }
        else
        {
            /* First Subsection */
            NewSubsection = (PMSUBSECTION)&ControlArea[1];
        }

        LastSubsection = NewSubsection;
        NumberOfNewSubsections++;

        NewSubsection->PtesInSubsection = (SubsectionSize / sizeof(MMPTE));
    }

    RtlZeroMemory(Segment, sizeof(MAPPED_FILE_SEGMENT));
    *OutSegment = (PSEGMENT)Segment;

    Segment->LastSubsectionHint = LastSubsection;

    ControlArea->Segment = (PSEGMENT)Segment;
    ControlArea->NumberOfSectionReferences = 1;

    if (IgnoreFileSizing)
        /* CC is caller */
        ControlArea->u.Flags.WasPurged = 1;
    else
        ControlArea->NumberOfUserReferences = 1;

    ControlArea->u.Flags.BeingCreated = 1;
    ControlArea->u.Flags.File = 1;

    if (File->DeviceObject->Characteristics & FILE_REMOTE_DEVICE)
        ControlArea->u.Flags.Networked = 1;

    if (AllocationAttributes & SEC_NOCACHE)
        ControlArea->u.Flags.NoCache = 1;

    ControlArea->FilePointer = File;
    ASSERT(ControlArea->u.Flags.GlobalOnlyPerSession == 0);

    Subsection = (PMSUBSECTION)&ControlArea[1];

    MI_MAKE_SUBSECTION_PTE(&ProtoTemplate, Subsection);
    ProtoTemplate.u.Soft.Prototype = 1;
    ProtoTemplate.u.Soft.Protection = MM_EXECUTE_READWRITE;
    Segment->SegmentPteTemplate = ProtoTemplate;

    Segment->ControlArea = ControlArea;
    Segment->SizeOfSegment = FileSize;
    Segment->TotalNumberOfPtes = (ULONG)TotalNumberOfPtes;

    DPRINT("MiCreateDataFileMap: Segment->SizeOfSegment %I64X\n", Segment->SizeOfSegment);

    if (TotalNumberOfPtes >= (1ull << 32))
        Segment->SegmentFlags.TotalNumberOfPtes4132 = (TotalNumberOfPtes >> 32);

    if (Subsection->NextSubsection)
        Segment->NonExtendedPtes = (Subsection->PtesInSubsection & ~(((ULONG)MmAllocationFragment / PAGE_SIZE) - 1));
    else
        Segment->NonExtendedPtes = Segment->TotalNumberOfPtes;

    Subsection->PtesInSubsection = Segment->NonExtendedPtes;

    CurrentPtes = 0;

    for (; Subsection; Subsection = (PMSUBSECTION)Subsection->NextSubsection)
    {
        Subsection->StartingSector = (ULONG)CurrentPtes;
        Subsection->u.SubsectionFlags.StartingSector4132 = (CurrentPtes >> 32);

        Subsection->ControlArea = ControlArea;

        if (Subsection->NextSubsection)
        {
            Subsection->NumberOfFullSectors = Subsection->PtesInSubsection;
        }
        else
        {
            Subsection->NumberOfFullSectors = (FileSize / PAGE_SIZE) - (ULONG)CurrentPtes;
            Subsection->u.SubsectionFlags.SectorEndOffset = BYTE_OFFSET(FileSize);

            Subsection->UnusedPtes = Subsection->PtesInSubsection - (TotalNumberOfPtes - CurrentPtes);
            Subsection->PtesInSubsection -= Subsection->UnusedPtes;
        }

        MiSubsectionConsistent((PSUBSECTION)Subsection);

        CurrentPtes += (ULONGLONG)Subsection->PtesInSubsection;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI 
MmFlushSection(
    _In_ PSECTION_OBJECT_POINTERS SectionPointers,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _Out_ IO_STATUS_BLOCK* OutIoStatus,
    _In_ ULONG Flags)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
MiFlushDataSection(
    _In_ PFILE_OBJECT FileObject)
{
    PCONTROL_AREA ControlArea;
    IO_STATUS_BLOCK IoStatusBlock;
    BOOLEAN IsDataSectionUsed;
    KIRQL OldIrql;

    DPRINT("MiFlushDataSection: FileObject %p\n", FileObject);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    ControlArea = FileObject->SectionObjectPointer->DataSectionObject;
    if (!ControlArea)
    {
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        return FALSE;
    }

    if (ControlArea->NumberOfSectionReferences || ControlArea->NumberOfMappedViews)
        IsDataSectionUsed = TRUE;
    else
        IsDataSectionUsed = FALSE;

    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    if (ControlArea->NumberOfSystemCacheViews)
        CcFlushCache(FileObject->SectionObjectPointer, NULL, 0, &IoStatusBlock);
    else
        MmFlushSection(FileObject->SectionObjectPointer, NULL, 0, &IoStatusBlock, TRUE);

    return IsDataSectionUsed;
}

PFN_NUMBER
NTAPI
MiGetPageForHeader(
    _In_ BOOLEAN IsZeroPage)
{
    PFN_NUMBER PageNumber;
    PMMPFN PeHeaderPfn;
    ULONG Color;
    BOOLEAN IsFlush = FALSE;
    KIRQL OldIrql;

    DPRINT("MiGetPageForHeader: IsZeroPage %X\n", IsZeroPage);

    Color = MI_GET_NEXT_PROCESS_COLOR(PsGetCurrentProcess());

    OldIrql = MiLockPfnDb(APC_LEVEL);

    if (MmAvailablePages < 0x80)
    {
        DPRINT1("MiGetPageForHeader: FIXME MiEnsureAvailablePageOrWait()\n");
    }

    if (IsZeroPage)
        PageNumber = MiRemoveZeroPage(Color);
    else
        PageNumber = MiRemoveAnyPage(Color);

    PeHeaderPfn = &MmPfnDatabase[PageNumber];
    PeHeaderPfn->u3.ReferenceCount++;

    if (PeHeaderPfn->u3.e1.CacheAttribute != MiCached)
    {
        PeHeaderPfn->u3.e1.CacheAttribute = MiCached;
        IsFlush = TRUE;
    }

    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    ASSERT(PeHeaderPfn->u1.Flink == 0);
    ASSERT(PeHeaderPfn->u2.Blink == 0);

    PeHeaderPfn->OriginalPte.u.Long = 0;
    PeHeaderPfn->PteAddress = (PMMPTE)0x10001;

    if (IsFlush)
        KeFlushEntireTb(TRUE, TRUE);

    return PageNumber;
}

PVOID
NTAPI
MiCopyHeaderIfResident(
    _In_ PFILE_OBJECT FileObject,
    _In_ PFN_NUMBER PageNumber)
{
    PSECTION_OBJECT_POINTERS SectionPointers;
    PCONTROL_AREA ControlArea;
    PSUBSECTION Subsection;
    PEPROCESS Process;
    PVOID ImageHeader;
    PMMPTE ImagePte;
    PMMPTE BasePte;
    PMMPDE Pde;
    MMPTE PteContents;
    PFN_NUMBER PageFrameIndex;
    KIRQL OldIrql;
    KIRQL MapIrql;

    DPRINT("MiCopyHeaderIfResident: FileObject %p, PageNumber %X\n", FileObject, PageNumber);

    SectionPointers = FileObject->SectionObjectPointer;
  
    if (!SectionPointers)
        return NULL;

    if (!SectionPointers->DataSectionObject)
        return NULL;

    ImagePte = MiReserveSystemPtes(1, SystemPteSpace);
    if (!ImagePte)
        return NULL;

    ImageHeader = MiPteToAddress(ImagePte);
    ASSERT(ImagePte->u.Hard.Valid == 0);

    PteContents = ValidKernelPte;
    PteContents.u.Hard.PageFrameNumber = PageNumber;

    MI_WRITE_VALID_PTE(ImagePte, PteContents);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    SectionPointers = FileObject->SectionObjectPointer;
    if (!SectionPointers)
        goto ErrorExit;

    ControlArea = SectionPointers->DataSectionObject;
    if (!ControlArea)
        goto ErrorExit;

    if (ControlArea->u.Flags.BeingCreated || ControlArea->u.Flags.BeingDeleted)
        goto ErrorExit;

    if (ControlArea->u.Flags.Rom)
        Subsection = (PSUBSECTION)((PLARGE_CONTROL_AREA)ControlArea + 1);
    else
        Subsection = (PSUBSECTION)(ControlArea + 1);

    BasePte = Subsection->SubsectionBase;
    if (!BasePte)
        goto ErrorExit;

    Pde = MiAddressToPte(BasePte);
    if (!Pde->u.Hard.Valid)
        goto ErrorExit;

    PteContents.u.Long = BasePte->u.Long;

    if (!PteContents.u.Hard.Valid &&
        (PteContents.u.Soft.Prototype || !PteContents.u.Soft.Transition))
    {
        goto ErrorExit;
    }

    if (!PteContents.u.Hard.Valid)
    {
        PageFrameIndex = PteContents.u.Trans.PageFrameNumber;

        if (MI_PFN_ELEMENT(PageFrameIndex)->u3.e1.ReadInProgress)
            goto ErrorExit;
    }
    else
    {
        PageFrameIndex = PteContents.u.Hard.PageFrameNumber;
    }

    Process = PsGetCurrentProcess();
    ImagePte = MiMapPageInHyperSpace(Process, PageFrameIndex, &MapIrql);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    RtlCopyMemory(ImageHeader, ImagePte, PAGE_SIZE);

    MiUnmapPageInHyperSpace(Process, ImagePte, MapIrql);

    MiUnlockPfnDb(OldIrql, APC_LEVEL);
    return ImageHeader;

ErrorExit:

    MiUnlockPfnDb(OldIrql, APC_LEVEL);
    MiReleaseSystemPtes(ImagePte, 1, SystemPteSpace);

    return NULL;
}

NTSTATUS
NTAPI
MiReadPeHeader(
    _In_ PFILE_OBJECT FileObject,
    _In_ PKEVENT Event,
    _In_ PMI_PE_HEADER_MDL HeaderMdl,
    _Out_ ULONGLONG* OutFileSize,
    _Out_ BOOLEAN* OutIsDataSectionUsed,
    _Out_ PVOID* OutPeHeader,
    _Out_ ULONG* OutPeHeaderSize,
    _Out_ PIMAGE_DOS_HEADER* OutDosHeader,
    _Out_ PIMAGE_NT_HEADERS* OutNtHeader,
    _Out_ ULONG* OutNtHeaderSize)
{
    PMMPFN PeHeaderPfn;
    PPFN_NUMBER MdlPages;
    PVOID PeHeader;
    PMMPTE BasePte = NULL;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset = {{0, 0}};
    LARGE_INTEGER fileSize;
    ULONGLONG FileSize;
    PFN_NUMBER PageFrameNumber;
    MMPTE TempPte;
    ULONG PeHeaderSize;
    NTSTATUS Status;

    DPRINT("MiReadPeHeader: FileObject %p\n", FileObject);

    /* Retrieve the file size for a file */
    Status = FsRtlGetFileSize(FileObject, &fileSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MiReadPeHeader: Status %X, File '%wZ'\n", Status, &FileObject->FileName);

        if (Status != STATUS_FILE_IS_A_DIRECTORY)
        {
            ASSERT(FALSE);
            return Status;
        }

        DPRINT1("MiReadPeHeader: STATUS_INVALID_FILE_FOR_SECTION\n");
        ASSERT(FALSE);
        return STATUS_INVALID_FILE_FOR_SECTION;
    }

    if (fileSize.HighPart)
    {
        DPRINT1("MiReadPeHeader: File '%wZ'\n", &FileObject->FileName);
        ASSERT(FALSE);

        DPRINT1("MiReadPeHeader: STATUS_INVALID_FILE_FOR_SECTION\n");
        return STATUS_INVALID_FILE_FOR_SECTION;
    }

    FileSize = (ULONGLONG)fileSize.QuadPart;

    *OutFileSize = FileSize;

    /* Initialize MDL to read the first page of the PE header */
    MmInitializeMdl(&HeaderMdl->Mdl, NULL, PAGE_SIZE);
    HeaderMdl->Mdl.MdlFlags |= MDL_PAGES_LOCKED;

    /* Allocate one zero page for the first page of the PE header */
    PageFrameNumber = MiGetPageForHeader(TRUE);
    ASSERT(PageFrameNumber != 0);

    PeHeaderPfn = &MmPfnDatabase[PageFrameNumber];
    ASSERT(PeHeaderPfn->u1.Flink == 0);

    /* Setup Mdl */
    MdlPages = MmGetMdlPfnArray(&HeaderMdl->Mdl);
    MdlPages[0] = PageFrameNumber;

    DPRINT("MiReadPeHeader: FIXME CcZeroEndOfLastPage\n");

    *OutIsDataSectionUsed = MiFlushDataSection(FileObject);

    PeHeader = MiCopyHeaderIfResident(FileObject, PageFrameNumber);

    /* Is the header resident in memory? */
    if (PeHeader)
    {
        PeHeaderSize = PAGE_SIZE;
        IoStatusBlock.Information = PAGE_SIZE;
        BasePte = MiAddressToPte(PeHeader);
    }
    else
    {
        ASSERT(HeaderMdl->Mdl.MdlFlags & MDL_PAGES_LOCKED);

        /* Read the first page of the PE header */
        Status = IoPageRead(FileObject, &HeaderMdl->Mdl, &Offset, Event, &IoStatusBlock);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject (Event, WrPageIn, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }

        if (HeaderMdl->Mdl.MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
        {
            MmUnmapLockedPages(HeaderMdl->Mdl.MappedSystemVa, &HeaderMdl->Mdl);
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MiReadPeHeader: Status %X, File '%wZ'\n", Status, &FileObject->FileName);

            if (Status != STATUS_FILE_LOCK_CONFLICT &&
                Status != STATUS_FILE_IS_OFFLINE)
            {
                Status = STATUS_INVALID_FILE_FOR_SECTION;
            }

            goto Exit;
        }

        /* Allocate one system PTE for the first page of the PE header */
        BasePte = MiReserveSystemPtes(1, SystemPteSpace);
        if (!BasePte)
        {
            DPRINT1("MiReadPeHeader: Status %X, File '%wZ'\n", Status, &FileObject->FileName);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        ASSERT(BasePte->u.Hard.Valid == 0);

        /* Calculate Va for this system PTE */
        PeHeader = MiPteToAddress(BasePte);

        /* Fill tmp PTE with ValidKernelPte contents */
        TempPte = ValidKernelPte;

        /* Save number page of the PE header in tmp PTE */
        TempPte.u.Hard.PageFrameNumber = PageFrameNumber;

        /* Copy the tmp PTE to the PTE for the PE header and make it valid */
        MI_WRITE_VALID_PTE(BasePte, TempPte);

        /* Get size for the header */
        PeHeaderSize = IoStatusBlock.Information;

        /* Check size of the header */
        if (PeHeaderSize != PAGE_SIZE)
        {
            if (PeHeaderSize < sizeof(IMAGE_DOS_HEADER))
            {
                DPRINT1("MiReadPeHeader: PeHeaderSize %X, File '%wZ'\n", PeHeaderSize, &FileObject->FileName);
                Status = STATUS_INVALID_IMAGE_NOT_MZ;
                goto Exit;
            }

            /* Zero end of page */
            RtlZeroMemory((PVOID)((ULONG_PTR)PeHeader + PeHeaderSize), (PAGE_SIZE - PeHeaderSize));
        }
    }

Exit:

    *OutPeHeader = PeHeader;
    *OutPeHeaderSize = PeHeaderSize;

    return Status;
}

NTSTATUS
NTAPI
MiValidateDosHeader(
    _In_ PIMAGE_DOS_HEADER DosHeader,
    _In_ ULONGLONG FileSize,
    _In_ ULONG PeHeaderSize)
{
    DPRINT("MiValidateDosHeader: %p, FileSize %IX64, PeHeaderSize %X\n", DosHeader, FileSize, PeHeaderSize);

    if (DosHeader->e_magic != 0x5A4D)
    {
        DPRINT1("MiValidateDosHeader: STATUS_INVALID_IMAGE_NOT_MZ\n");
        DPRINT1("MiValidateDosHeader: DosHeader %p, FileSize %IX64, PeHeaderSize %X\n", DosHeader, FileSize, PeHeaderSize);
        DPRINT1("MiValidateDosHeader: DosHeader->e_magic %X\n", DosHeader->e_magic);
        return STATUS_INVALID_IMAGE_NOT_MZ;
    }

    if ((ULONG)DosHeader->e_lfanew > (ULONG)FileSize)
    {
        DPRINT1("MiValidateDosHeader: STATUS_INVALID_IMAGE_PROTECT\n");
        return STATUS_INVALID_IMAGE_PROTECT;
    }

    if ((ULONG)(DosHeader->e_lfanew + MI_PE_HEADER_MAX_SIZE) <= DosHeader->e_lfanew)
    {
        DPRINT1("MiValidateDosHeader: STATUS_INVALID_IMAGE_PROTECT\n");
        return STATUS_INVALID_IMAGE_PROTECT;
    }

    if ((ULONG)(DosHeader->e_lfanew + MI_PE_HEADER_MAX_SIZE) <= PAGE_SIZE)
    {
        if (DosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS) > PeHeaderSize)
        {
            DPRINT1("MiValidateDosHeader: STATUS_INVALID_IMAGE_PROTECT\n");
            return STATUS_INVALID_IMAGE_PROTECT;
        }
    }

    return STATUS_SUCCESS;
}

CHAR
NTAPI
MiGetImageProtection(
    _In_ ULONG Characteristics)
{
    ULONG Index = 0;

    PAGED_CODE();

    if (Characteristics & IMAGE_SCN_MEM_EXECUTE)
        Index = 1;

    if (Characteristics & IMAGE_SCN_MEM_READ)
        Index |= 2;

    if (Characteristics & IMAGE_SCN_MEM_WRITE)
        Index |= 4;

    if (Characteristics & IMAGE_SCN_MEM_SHARED)
        Index |= 8;

    return MmImageProtectionArray[Index];
}

VOID
NTAPI
MiInitializeTransitionPfn(
    _In_ PFN_NUMBER PageNumber,
    _In_ OUT PMMPTE SectionProto)
{
    PMMPTE ProtoPte;
    MMPTE TempPte;
    PMMPFN Pfn;
    PFN_NUMBER ProtoPtePageNumber;

    DPRINT("MiInitializeTransitionPfn: PageNumber %X, SectionProto %p\n", PageNumber, SectionProto);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(MmPfnOwner == KeGetCurrentThread());

    Pfn = MI_PFN_ELEMENT(PageNumber);
    Pfn->u1.Event = NULL;
    Pfn->PteAddress = SectionProto;
    Pfn->OriginalPte.u.Long = SectionProto->u.Long;

    ASSERT(!((Pfn->OriginalPte.u.Soft.Prototype == 0) &&
             (Pfn->OriginalPte.u.Soft.Transition == 1)));

    Pfn->u3.e1.PrototypePte = 1;
    Pfn->u3.e1.PageLocation = TransitionPage;
    Pfn->u2.ShareCount = 0;

    ProtoPte = MiAddressToPte(SectionProto);

    if (!ProtoPte->u.Hard.Valid)
    {
        if (!NT_SUCCESS(MiCheckPdeForPagedPool(SectionProto)))
        {
            DPRINT1("KeBugCheckEx()\n");
            ASSERT(FALSE);
            KeBugCheckEx(0x1A,
                         0x61940,
                         (ULONG_PTR)SectionProto,
                         ProtoPte->u.Long,
                         (ULONG_PTR)MiPteToAddress(SectionProto));
        }
    }

    ProtoPtePageNumber = ProtoPte->u.Hard.PageFrameNumber;

    Pfn->u4.PteFrame = ProtoPtePageNumber;

    MI_MAKE_TRANSITION_PTE(&TempPte, PageNumber, SectionProto->u.Soft.Protection);
    MI_WRITE_INVALID_PTE(SectionProto, TempPte);

    DPRINT("MiInitializeTransitionPfn: SectionProto %X, [%p]\n", SectionProto, SectionProto->u.Long);

    ASSERT(ProtoPtePageNumber != 0);
    MI_PFN_ELEMENT(ProtoPtePageNumber)->u2.ShareCount++;
}

NTSTATUS
NTAPI
MiCreateImageFileMap(
    _In_ PFILE_OBJECT FileObject,
    _Out_ PSEGMENT* OutSegment)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MiAddViewsForSection(
    _In_ PMSUBSECTION StartMappedSubsection,
    _In_ ULONGLONG LastPteOffset,
    _In_ KIRQL OldIrql)
{
    PMSUBSECTION MappedSubsection;
    PVOID SectionProtos;
    MMPTE ProtoTemplate;
    ULONG SubsectionPoolSize;
    ULONG PteCount;

    DPRINT("MiAddViewsForSection: %p, %I64X\n", StartMappedSubsection, LastPteOffset);

    ASSERT((StartMappedSubsection->ControlArea->u.Flags.Image == 0) &&
           (StartMappedSubsection->ControlArea->FilePointer != NULL) &&
           (StartMappedSubsection->ControlArea->u.Flags.PhysicalMemory == 0));

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(MmPfnOwner == KeGetCurrentThread());

    for (MappedSubsection = StartMappedSubsection;
         MappedSubsection;
         MappedSubsection = (PMSUBSECTION)MappedSubsection->NextSubsection)
    {
        ASSERT(MappedSubsection->ControlArea->DereferenceList.Flink == NULL);

        if (MappedSubsection->SubsectionBase)
        {
            MappedSubsection->NumberOfMappedViews++;

            if (MappedSubsection->DereferenceList.Flink)
            {
                RemoveEntryList(&MappedSubsection->DereferenceList);
                MappedSubsection->DereferenceList.Flink = NULL;

                PteCount = (MappedSubsection->PtesInSubsection + MappedSubsection->UnusedPtes);
                AlloccatePoolForSubsectionPtes(PteCount);
            }

            MappedSubsection->u2.SubsectionFlags2.SubsectionAccessed = 1;
        }
        else
        {
            ASSERT(MappedSubsection->u.SubsectionFlags.SubsectionStatic == 0);
            ASSERT(MappedSubsection->NumberOfMappedViews == 0);

            MiUnlockPfnDb(OldIrql, APC_LEVEL);

            PteCount = (MappedSubsection->PtesInSubsection + MappedSubsection->UnusedPtes);
            SubsectionPoolSize = (PteCount * sizeof(MMPTE));
            ASSERT(SubsectionPoolSize != 0);

            SectionProtos = ExAllocatePoolWithTag(PagedPool, SubsectionPoolSize, 'tSmM'); // ? POOL_TYPE == 0x80000001
            if (!SectionProtos)
            {
                OldIrql = MiLockPfnDb(APC_LEVEL);

                while (StartMappedSubsection != MappedSubsection);
                {
                    ASSERT((LONG_PTR)StartMappedSubsection->NumberOfMappedViews >= 1);
                    StartMappedSubsection->NumberOfMappedViews--;

                    ASSERT(StartMappedSubsection->u.SubsectionFlags.SubsectionStatic == 0);
                    ASSERT(StartMappedSubsection->DereferenceList.Flink == NULL);

                    if (!StartMappedSubsection->NumberOfMappedViews)
                    {
                        InsertHeadList(&MmUnusedSubsectionList, &StartMappedSubsection->DereferenceList);

                        PteCount = (MappedSubsection->PtesInSubsection + MappedSubsection->UnusedPtes);
                        FreePoolForSubsectionPtes(PteCount);
                    }

                    StartMappedSubsection = (PMSUBSECTION)StartMappedSubsection->NextSubsection;
                }

                MiUnlockPfnDb(OldIrql, APC_LEVEL);

                DPRINT1("MiAddViewsForSection: return STATUS_INSUFFICIENT_RESOURCES\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            MI_MAKE_SUBSECTION_PTE(&ProtoTemplate, MappedSubsection);

            ProtoTemplate.u.Soft.Prototype = 1;
            ProtoTemplate.u.Soft.Protection = MappedSubsection->ControlArea->Segment->SegmentPteTemplate.u.Soft.Protection;

            RtlFillMemoryUlong(SectionProtos, SubsectionPoolSize, ProtoTemplate.u.Long); // FIXME for 64 bit

            OldIrql = MiLockPfnDb(APC_LEVEL);

            MappedSubsection->NumberOfMappedViews++;
            MappedSubsection->u2.SubsectionFlags2.SubsectionAccessed = 1;

            if (MappedSubsection->SubsectionBase)
            {
                if (MappedSubsection->DereferenceList.Flink)
                {
                    ASSERT(MappedSubsection->NumberOfMappedViews == 1);

                    RemoveEntryList(&MappedSubsection->DereferenceList);
                    MappedSubsection->DereferenceList.Flink = NULL;

                    PteCount = (MappedSubsection->PtesInSubsection + MappedSubsection->UnusedPtes);
                    AlloccatePoolForSubsectionPtes(PteCount);
                }
                else
                {
                    ASSERT(MappedSubsection->NumberOfMappedViews > 1);
                }

                MiUnlockPfnDb(OldIrql, APC_LEVEL);
                ExFreePoolWithTag(SectionProtos, 'tSmM');
                OldIrql = MiLockPfnDb(APC_LEVEL);
            }
            else
            {
                ASSERT(MappedSubsection->NumberOfMappedViews == 1);
                MappedSubsection->SubsectionBase = (PMMPTE)SectionProtos;
            }
        }

        if (LastPteOffset)
        {
            ASSERT((LONG)MappedSubsection->PtesInSubsection > 0);
            ASSERT((UINT64)LastPteOffset > 0);

            if (LastPteOffset <= MappedSubsection->PtesInSubsection)
                break;

            LastPteOffset -= MappedSubsection->PtesInSubsection;
        }
    }

    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    DPRINT("MiAddViewsForSection: return STATUS_SUCCESS\n");
    return STATUS_SUCCESS;
}

VOID
NTAPI
MiRemoveViewsFromSection(
    _In_ PMSUBSECTION MappedSubsection,
    _In_ ULONGLONG PteCount)
{
    DPRINT("MiRemoveViewsFromSection: MappedSubsection %p, PteCount %X\n", MappedSubsection, PteCount);

    ASSERT((MappedSubsection->ControlArea->u.Flags.Image == 0) &&
           (MappedSubsection->ControlArea->FilePointer != NULL) &&
           (MappedSubsection->ControlArea->u.Flags.PhysicalMemory == 0));

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(MmPfnOwner == KeGetCurrentThread());

    for (;
         MappedSubsection;
         MappedSubsection = (PMSUBSECTION)MappedSubsection->NextSubsection)
    {
        ASSERT(MappedSubsection->ControlArea->DereferenceList.Flink == NULL);
        ASSERT(MappedSubsection->SubsectionBase != NULL);
        ASSERT(MappedSubsection->DereferenceList.Flink == NULL);

        ASSERT(((LONG_PTR)MappedSubsection->NumberOfMappedViews >= 1) ||
               (MappedSubsection->u.SubsectionFlags.SubsectionStatic == 1));

        MappedSubsection->NumberOfMappedViews--;

        if (!MappedSubsection->NumberOfMappedViews &&
            !MappedSubsection->u.SubsectionFlags.SubsectionStatic)
        {
            InsertTailList(&MmUnusedSubsectionList, &MappedSubsection->DereferenceList);
            FreePoolForSubsectionPtes(MappedSubsection->PtesInSubsection + MappedSubsection->UnusedPtes);

            //if (MiUnusedSubsectionPagedPoolPeak < MiUnusedSubsectionPagedPool)
            //    MiUnusedSubsectionPagedPoolPeak = MiUnusedSubsectionPagedPool;
        }

        if (!PteCount)
            continue;

        if (PteCount <= (ULONGLONG)MappedSubsection->PtesInSubsection)
            break;

        PteCount -= MappedSubsection->PtesInSubsection;
    }
}

BOOLEAN
NTAPI
MiCanFileBeTruncatedInternal(
    _In_ PSECTION_OBJECT_POINTERS SectionPointers,
    _In_ OUT PLARGE_INTEGER FileOffset,
    _In_ BOOLEAN IsNotCheckUserReferences,
    _Out_ KIRQL* OutOldIrql)
{
    PCONTROL_AREA ControlArea;
    PSUBSECTION Subsection;
    PMAPPED_FILE_SEGMENT Segment;
    LARGE_INTEGER Offset;
    KIRQL OldIrql;

    DPRINT("MiCanFileBeTruncatedInternal: %p, %X\n", SectionPointers, IsNotCheckUserReferences);

    if (!MmFlushImageSection(SectionPointers, MmFlushForWrite))
    {
        DPRINT("MiCanFileBeTruncatedInternal: return FALSE\n");
        return FALSE;
    }

    OldIrql = MiLockPfnDb(APC_LEVEL);

    ControlArea = SectionPointers->DataSectionObject;
    if (!ControlArea)
    {
        DPRINT("MiCanFileBeTruncatedInternal: ControlArea == NULL\n");
        *OutOldIrql = OldIrql;
        return TRUE;
    }

    if (ControlArea->u.Flags.BeingCreated ||
        ControlArea->u.Flags.BeingDeleted ||
        ControlArea->u.Flags.Rom)
    {
        goto Exit;
    }

    if (!ControlArea->NumberOfUserReferences ||
        (IsNotCheckUserReferences && !ControlArea->NumberOfMappedViews))
    {
        DPRINT("MiCanFileBeTruncatedInternal: return TRUE\n");
        *OutOldIrql = OldIrql;
        return TRUE;
    }

    if (!FileOffset)
        goto Exit;

    ASSERT(ControlArea->u.Flags.Image == 0);
    ASSERT(ControlArea->u.Flags.GlobalOnlyPerSession == 0);

    Subsection = (PSUBSECTION)&ControlArea[1];

    if (ControlArea->FilePointer)
    {
        Segment = (PMAPPED_FILE_SEGMENT)ControlArea->Segment;

        if (MiIsAddressValid(Segment) && Segment->LastSubsectionHint)
            Subsection = (PSUBSECTION)Segment->LastSubsectionHint;
    }

    while (Subsection->NextSubsection)
        Subsection = Subsection->NextSubsection;

    ASSERT(Subsection->ControlArea == ControlArea);

    if (Subsection->ControlArea->u.Flags.Image)
    {
        Offset.QuadPart = (Subsection->StartingSector + Subsection->NumberOfFullSectors);
        Offset.QuadPart *= MM_SECTOR_SIZE;
    }
    else
    {
        Offset.HighPart = Subsection->u.SubsectionFlags.StartingSector4132;
        Offset.LowPart = Subsection->StartingSector;

        Offset.QuadPart += Subsection->NumberOfFullSectors;
        Offset.QuadPart *= PAGE_SIZE;
    }

    Offset.QuadPart += Subsection->u.SubsectionFlags.SectorEndOffset;

    if (FileOffset->QuadPart >= Offset.QuadPart)
    {
        Offset.QuadPart += (PAGE_SIZE - 1);
        Offset.LowPart &= ~(PAGE_SIZE - 1);

        if (FileOffset->QuadPart < Offset.QuadPart)
            *FileOffset = Offset;

        *OutOldIrql = OldIrql;

        DPRINT("MiCanFileBeTruncatedInternal: return TRUE\n");
        return TRUE;
    }

Exit:

    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    DPRINT("MiCanFileBeTruncatedInternal: return FALSE\n");
    return FALSE;
}

BOOLEAN
NTAPI
MiReferenceSubsection(
    _In_ PMSUBSECTION MappedSubsection)
{
    DPRINT("MiReferenceSubsection: %p, %p\n", MappedSubsection, MappedSubsection->SubsectionBase);

    ASSERT((MappedSubsection->ControlArea->u.Flags.Image == 0) &&
           (MappedSubsection->ControlArea->FilePointer != NULL) &&
           (MappedSubsection->ControlArea->u.Flags.PhysicalMemory == 0));

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(MmPfnOwner == KeGetCurrentThread());

    if (!MappedSubsection->SubsectionBase)
        return FALSE;

    MappedSubsection->NumberOfMappedViews++;

    if (!MappedSubsection->DereferenceList.Flink)
        goto Exit;

    RemoveEntryList(&MappedSubsection->DereferenceList);
    AlloccatePoolForSubsectionPtes(MappedSubsection->PtesInSubsection + MappedSubsection->UnusedPtes);

    MappedSubsection->DereferenceList.Flink = NULL;

Exit:

    MappedSubsection->u2.SubsectionFlags2.SubsectionAccessed = 1;
    return TRUE;
}

BOOLEAN
NTAPI
MiCheckProtoPtePageState(
    _In_ PMMPTE SectionProto,
    _In_ KIRQL OldIrql,
    _Out_ BOOLEAN* OutIsLock)
{
    PMMPTE ProtoPte;
    MMPTE TempPte;
    PMMPFN Pfn;

    DPRINT("MiCheckProtoPtePageState: SectionProto %p, OldIrql %X\n", SectionProto, OldIrql);

    *OutIsLock = FALSE;

    ProtoPte = MiAddressToPte(SectionProto);

    if (!ProtoPte->u.Hard.Valid)
        MiCheckPdeForPagedPool(SectionProto);

    TempPte.u.Long = ProtoPte->u.Long;

    if (TempPte.u.Hard.Valid)
    {
        Pfn = MI_PFN_ELEMENT(TempPte.u.Hard.PageFrameNumber);

        if (Pfn->u2.ShareCount == 1)
            return FALSE;

        return TRUE;
    }

    if (!TempPte.u.Soft.Prototype && TempPte.u.Soft.Transition)
        return FALSE;

    Pfn = MI_PFN_ELEMENT(TempPte.u.Trans.PageFrameNumber);

    if (Pfn->u3.e1.PageLocation < ActiveAndValid)
        return FALSE;

    if (OldIrql != MM_NOIRQL)
    {
        DPRINT1("MmPurgeSection: FIXME\n");
        ASSERT(FALSE);
        //MiMakeSystemAddressValidPfn(SectionProto, OldIrql);
        *OutIsLock = TRUE;
    }

    return TRUE;
}

BOOLEAN
NTAPI
MmPurgeSection(
    _In_ PSECTION_OBJECT_POINTERS SectionPointer,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ SIZE_T Length,
    _In_ BOOLEAN IsFullPurge)
{
    PCONTROL_AREA ControlArea;
    PMAPPED_FILE_SEGMENT Segment;
    PSUBSECTION Subsection;
    PSUBSECTION FirstSubsection;
    PSUBSECTION LastSubsection;
    PSUBSECTION TempSubsection;
    PSUBSECTION LastTempSubsection;
    PMSUBSECTION MappedSubsection;
    PLARGE_INTEGER fileOffset;
    PMMPTE SectionProto;
    PMMPTE LastProto;
    PMMPTE lastProto;
    PMMPTE ProtoPte;
    MMPTE TempProto;
    PMMPFN ProtoPfn;
    PMMPFN Pfn;
    LARGE_INTEGER offset;
    ULONGLONG PteOffset;
    ULONG LastPteOffset;
    PFN_NUMBER PageNumber;
    KIRQL OldIrql;
    BOOLEAN IsLock;
    BOOLEAN Result;

    DPRINT("MmPurgeSection: %p, %I64X, %X, %X\n", SectionPointer, (FileOffset?FileOffset->QuadPart:0), Length, IsFullPurge);

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    if (FileOffset)
    {
        offset = *FileOffset;
        fileOffset = &offset;
    }
    else
    {
        fileOffset = NULL;
    }

    if (!MiCanFileBeTruncatedInternal(SectionPointer, fileOffset, TRUE, &OldIrql))
    {
        DPRINT("MmPurgeSection: return FALSE\n");
        return FALSE;
    }

    ControlArea = SectionPointer->DataSectionObject;

    if (!ControlArea || ControlArea->u.Flags.Rom)
    {
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        DPRINT("MmPurgeSection: return TRUE\n");
        return TRUE;
    }

    if (!IsFullPurge && ControlArea->NumberOfSystemCacheViews)
    {
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        DPRINT("MmPurgeSection: return FALSE\n");
        return FALSE;
    }

    ASSERT(ControlArea->u.Flags.BeingDeleted == 0);
    ASSERT(ControlArea->u.Flags.GlobalOnlyPerSession == 0);

    Subsection = (PSUBSECTION)&ControlArea[1];

    if (fileOffset)
    {
        for (PteOffset = (fileOffset->QuadPart / PAGE_SIZE);
             PteOffset >= Subsection->PtesInSubsection;
             PteOffset -= Subsection->PtesInSubsection)
        {
            Subsection = Subsection->NextSubsection;

            if (!Subsection)
            {
                MiUnlockPfnDb(OldIrql, APC_LEVEL);
                DPRINT("MmPurgeSection: return TRUE\n");
                return TRUE;
            }
        }

        ASSERT(PteOffset < (ULONGLONG)Subsection->PtesInSubsection);
    }
    else
    {
        PteOffset = 0;
    }

    if (fileOffset && Length)
    {
        LastPteOffset = PteOffset + (((Length + BYTE_OFFSET(fileOffset->LowPart)) - 1) / PAGE_SIZE);

        for (LastSubsection = Subsection;
             (ULONGLONG)LastSubsection->PtesInSubsection <= LastPteOffset;
             LastSubsection = LastSubsection->NextSubsection)
        {
            LastPteOffset -= LastSubsection->PtesInSubsection;

            if (!LastSubsection->NextSubsection)
            {
                LastPteOffset = (LastSubsection->PtesInSubsection - 1);
                break;
            }
        }

        ASSERT(LastPteOffset < (ULONGLONG)LastSubsection->PtesInSubsection);
    }
    else
    {
        LastSubsection = Subsection;
        Segment = (PMAPPED_FILE_SEGMENT)ControlArea->Segment;

        if (MiIsAddressValid(Segment) &&
            Segment->LastSubsectionHint)
        {
            LastSubsection = (PSUBSECTION)Segment->LastSubsectionHint;
        }

        while (LastSubsection->NextSubsection)
            LastSubsection = LastSubsection->NextSubsection;

        LastPteOffset = (LastSubsection->PtesInSubsection - 1);
    }

    if (!MiReferenceSubsection((PMSUBSECTION)Subsection))
    {
        while (TRUE)
        {
            if (Subsection == LastSubsection)
            {
                MiUnlockPfnDb(OldIrql, APC_LEVEL);
                DPRINT("MmPurgeSection: return TRUE\n");
                return TRUE;
            }

            Subsection = Subsection->NextSubsection;

            if (!Subsection)
            {
                MiUnlockPfnDb(OldIrql, APC_LEVEL);
                DPRINT("MmPurgeSection: return TRUE\n");
                return TRUE;
            }

            if (MiReferenceSubsection((PMSUBSECTION)Subsection))
            {
                SectionProto = Subsection->SubsectionBase;
                break;
            }
        }
    }
    else
    {
        SectionProto = &Subsection->SubsectionBase[PteOffset];
    }

    FirstSubsection = Subsection;
    ASSERT(Subsection->SubsectionBase != NULL);

    if (!MiReferenceSubsection((PMSUBSECTION)LastSubsection))
    {
        ASSERT(Subsection != LastSubsection);

        TempSubsection = Subsection->NextSubsection;
        LastTempSubsection = NULL;

        while (TempSubsection != LastSubsection)
        {
            ASSERT(TempSubsection != NULL);

            if ((PMSUBSECTION)TempSubsection->SubsectionBase)
                LastTempSubsection = TempSubsection;

            TempSubsection = TempSubsection->NextSubsection;
        }

        if (!LastTempSubsection)
        {
            ASSERT(Subsection != NULL);
            ASSERT(Subsection->SubsectionBase != NULL);

            TempSubsection = Subsection;
        }
        else
        {
            TempSubsection = LastTempSubsection;
        }

        if (!MiReferenceSubsection((PMSUBSECTION)TempSubsection))
        {
            DPRINT1("MmPurgeSection: FIXME\n");
            ASSERT(FALSE);
        }

        ASSERT(TempSubsection->SubsectionBase != NULL);

        LastSubsection = TempSubsection;
        LastPteOffset = LastSubsection->PtesInSubsection - 1;
    }

    lastProto = &LastSubsection->SubsectionBase[LastPteOffset + 1];

    ControlArea->NumberOfMappedViews++;

    ControlArea->u.Flags.BeingPurged = 1;
    ControlArea->u.Flags.WasPurged = 1;

    Result = TRUE;

    while (TRUE)
    {
        DPRINT("MmPurgeSection: SectionProto %p\n", SectionProto);

        if (OldIrql == MM_NOIRQL)
            OldIrql = MiLockPfnDb(APC_LEVEL);

        if (Subsection == LastSubsection)
            LastProto = lastProto;
        else
            LastProto = &Subsection->SubsectionBase[Subsection->PtesInSubsection];

        if (!Subsection->SubsectionBase)
        {
            ASSERT(OldIrql != MM_NOIRQL);
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            goto Next;
        }

        MappedSubsection = (PMSUBSECTION)Subsection;
        MappedSubsection->NumberOfMappedViews++;

        if (MappedSubsection->DereferenceList.Flink)
        {
            RemoveEntryList(&MappedSubsection->DereferenceList);
            AlloccatePoolForSubsectionPtes(MappedSubsection->PtesInSubsection + MappedSubsection->UnusedPtes);

            MappedSubsection->DereferenceList.Flink = NULL;
        }

        MappedSubsection->u2.SubsectionFlags2.SubsectionAccessed = 1;

        if (!MiCheckProtoPtePageState(SectionProto, OldIrql, &IsLock))
            SectionProto = (PMMPTE)(((ULONG_PTR)SectionProto | (PAGE_SIZE - 1)) + 1);

        while (SectionProto < LastProto)
        {
            if (MiIsPteOnPdeBoundary(SectionProto) &&
                !MiCheckProtoPtePageState(SectionProto, OldIrql, &IsLock))
            {
                SectionProto += PTE_PER_PAGE;
                continue;
            }

            TempProto.u.Long = SectionProto->u.Long;

            if (TempProto.u.Hard.Valid)
            {
                Result = FALSE;
                break;
            }

            if (!TempProto.u.Soft.Prototype && TempProto.u.Soft.Transition)
            {
                if (OldIrql == MM_NOIRQL)
                {
                    ProtoPte = MiAddressToPte(SectionProto);
                    OldIrql = MiLockPfnDb(APC_LEVEL);

                    if (!ProtoPte->u.Hard.Valid)
                    {
                        DPRINT1("MmPurgeSection: FIXME\n");
                        ASSERT(FALSE);
                        //MiMakeSystemAddressValidPfn(SectionProto, OldIrql);
                    }

                    continue;
                }

                ProtoPfn = MI_PFN_ELEMENT(TempProto.u.Hard.PageFrameNumber);

                if (!ProtoPfn->OriginalPte.u.Soft.Prototype ||
                    ProtoPfn->OriginalPte.u.Hard.Valid ||
                    ProtoPfn->PteAddress != SectionProto)
                {
                    DPRINT1("MmPurgeSection: FIXME KeBugCheckEx()\n");
                    ASSERT(FALSE);
                }

                if (ProtoPfn->u3.e1.WriteInProgress)
                {
                    DPRINT1("MmPurgeSection: FIXME\n");
                    ASSERT(FALSE);
                    continue;
                }

                if (ProtoPfn->u3.e1.ReadInProgress)
                {
                    Result = FALSE;
                    break;
                }

                ASSERT(!((ProtoPfn->OriginalPte.u.Soft.Prototype == 0) &&
                       (ProtoPfn->OriginalPte.u.Soft.Transition == 1)));

                MI_WRITE_INVALID_PTE(SectionProto, ProtoPfn->OriginalPte);
                ASSERT(ProtoPfn->OriginalPte.u.Hard.Valid == 0);

                ControlArea->NumberOfPfnReferences--;
                ASSERT((LONG)ControlArea->NumberOfPfnReferences >= 0);

                MiUnlinkPageFromList(ProtoPfn);
                MI_SET_PFN_DELETED(ProtoPfn);

                PageNumber = ProtoPfn->u4.PteFrame;
                Pfn = MI_PFN_ELEMENT(PageNumber);

                if (Pfn->u2.ShareCount != 1)
                {
                    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
                    ASSERT(MmPfnOwner == KeGetCurrentThread());
                    ASSERT(PageNumber > 0);

                    ASSERT(MI_PFN_ELEMENT(PageNumber) == Pfn);
                    ASSERT(Pfn->u2.ShareCount != 0);

                    if (Pfn->u3.e1.PageLocation != ActiveAndValid &&
                        Pfn->u3.e1.PageLocation != StandbyPageList)
                    {
                        DPRINT1("MmPurgeSection: FIXME\n");
                        ASSERT(FALSE);
                    }

                    Pfn->u2.ShareCount--;
                    ASSERT(Pfn->u2.ShareCount < 0xF000000);
                }
                else
                {
                    MiDecrementShareCount(Pfn, PageNumber);
                }

                if (!ProtoPfn->u3.e2.ReferenceCount)
                {
                    DPRINT("MmPurgeSection: FIXME MiReleasePageFileSpace \n");
                    MiInsertPageInFreeList(TempProto.u.Trans.PageFrameNumber);
                }
            }

            SectionProto++;

            if (MiIsPteOnPdeBoundary(SectionProto) && OldIrql != MM_NOIRQL)
            {
                MiUnlockPfnDb(OldIrql, APC_LEVEL);
                OldIrql = MM_NOIRQL;
            }
        }

        if (OldIrql == MM_NOIRQL)
            OldIrql = MiLockPfnDb(APC_LEVEL);

        ASSERT(MappedSubsection->DereferenceList.Flink == NULL);
        ASSERT(((LONG_PTR)MappedSubsection->NumberOfMappedViews >= 1) ||
                (MappedSubsection->u.SubsectionFlags.SubsectionStatic == 1));

        MappedSubsection->NumberOfMappedViews--;

        if (!MappedSubsection->NumberOfMappedViews &&
            !MappedSubsection->u.SubsectionFlags.SubsectionStatic)
        {
            InsertTailList(&MmUnusedSubsectionList, &MappedSubsection->DereferenceList);
            FreePoolForSubsectionPtes(MappedSubsection->PtesInSubsection + MappedSubsection->UnusedPtes);
        }

        ASSERT(OldIrql != MM_NOIRQL);
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
Next:
        OldIrql = MM_NOIRQL;

        if (LastSubsection == Subsection || !Result)
            break;

        Subsection = Subsection->NextSubsection;
        SectionProto = Subsection->SubsectionBase;
    }

    OldIrql = MiLockPfnDb(APC_LEVEL);

    MiDecrementSubsections(FirstSubsection, FirstSubsection);
    MiDecrementSubsections(LastSubsection, LastSubsection);

    ASSERT((LONG)ControlArea->NumberOfMappedViews >= 1);

    ControlArea->NumberOfMappedViews--;
    ControlArea->u.Flags.BeingPurged = 0;

    MiCheckControlArea(ControlArea, OldIrql);

    DPRINT("MmPurgeSection: return %X\n", Result);
    return Result;
}

BOOLEAN
NTAPI
MiCheckControlAreaStatus(
    _In_ ULONG Type,
    _In_ PSECTION_OBJECT_POINTERS SectionPointers,
    _In_ BOOLEAN IsDeleteOnClose,
    _Out_ PCONTROL_AREA* OutControlArea,
    _Out_ KIRQL* OutOldIrql)
{
    PCONTROL_AREA ControlArea;
    ULONG NumberOfReferences;
    KIRQL OldIrql;

    DPRINT("MiCheckControlAreaStatus: %X, %p, %X\n", Type, SectionPointers, IsDeleteOnClose);

    *OutControlArea = NULL;

    // FIXME SegmentEvent

    OldIrql = MiLockPfnDb(APC_LEVEL);

    if (Type == 1)
        ControlArea = SectionPointers->ImageSectionObject;
    else
        ControlArea = SectionPointers->DataSectionObject;

    DPRINT("MiCheckControlAreaStatus: ControlArea %p\n", ControlArea);

    if (ControlArea)
    {
        if (Type == 2)
            NumberOfReferences = ControlArea->NumberOfUserReferences;
        else
            NumberOfReferences = ControlArea->NumberOfSectionReferences;
    }
    else
    {
        if (Type == 3)
        {
            ControlArea = SectionPointers->ImageSectionObject;
            if (!ControlArea)
            {
                MiUnlockPfnDb(OldIrql, APC_LEVEL);
                // FIXME SegmentEvent
                return TRUE;
            }

            NumberOfReferences = ControlArea->NumberOfSectionReferences;
        }
        else
        {
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            // FIXME SegmentEvent
            return TRUE;
        }
    }

    if (NumberOfReferences ||
        ControlArea->NumberOfMappedViews ||
        ControlArea->u.Flags.BeingCreated)
    {
        if (IsDeleteOnClose)
            ControlArea->u.Flags.DeleteOnClose = 1;

        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        // FIXME SegmentEvent
        return FALSE;
    }

    // FIXME SegmentEvent

    if (ControlArea->u.Flags.BeingDeleted)
    {
        DPRINT1("MiCheckControlAreaStatus: FIXME\n");
        ASSERT(FALSE);
        return TRUE;
    }

    DPRINT1("MiCheckControlAreaStatus: FIXME\n");
    ASSERT(FALSE);

    *OutControlArea = ControlArea;
    *OutOldIrql = OldIrql;

    return FALSE;
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
    PVOID PreviousSectionPointer = NULL;
    PVOID NewControlArea = NULL;
    PCONTROL_AREA ControlArea;
    PSEGMENT NewSegment = NULL;
    PSEGMENT Segment = NULL;
    PSUBSECTION Subsection;
    PEVENT_COUNTER Event;
    SECTION Section;
    PSECTION NewSection;
    PFILE_OBJECT File;
    LARGE_INTEGER FileSize;
    ULONG ControlAreaSize;
    ULONG ProtectionMask;
    ULONG SubsectionSize;
    ULONG NonPagedCharge;
    ULONG PagedCharge;
    KIRQL OldIrql;
    BOOLEAN UserRefIncremented = FALSE;
    BOOLEAN FileLock = FALSE;
    BOOLEAN IgnoreFileSizing = FALSE; // TRUE if CC call (FileObject != NULL)
    BOOLEAN IsSectionSizeChanged = FALSE;
    BOOLEAN IsGlobal;
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
            /* This is the file-mapped section. */
            ASSERT(!FileObject);

            /* Reference the file handle to get the object */
            Status = ObReferenceObjectByHandle(FileHandle,
                                               MmMakeFileAccess[ProtectionMask],
                                               IoFileObjectType,
                                               PreviousMode,
                                               (PVOID*)&File,
                                               NULL);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("MmCreateSection: Status %X\n", Status);
                return Status;
            }

            /* Make sure Cc has been doing its job */
            if (!File->SectionObjectPointer)
            {
                /* This is not a valid system-based file, fail */
                DPRINT1("MmCreateSection: STATUS_INVALID_FILE_FOR_SECTION\n");
                ObDereferenceObject(File);
                return STATUS_INVALID_FILE_FOR_SECTION;
            }
        }
        else
        {
            /* This is the section used by the CC. */
            ASSERT(!FileHandle);

            IgnoreFileSizing = TRUE;
            File = FileObject;

            if (File->SectionObjectPointer->DataSectionObject)
            {
                OldIrql = MiLockPfnDb(APC_LEVEL);

                ControlArea = (PCONTROL_AREA)(File->SectionObjectPointer->DataSectionObject);

                if (ControlArea &&
                    !ControlArea->u.Flags.BeingDeleted &&
                    !ControlArea->u.Flags.BeingCreated)
                {
                    DPRINT1("MmCreateSection: FIXME\n");
                    ASSERT(FALSE);
                }

                MiUnlockPfnDb(OldIrql, APC_LEVEL);
            }

            ObReferenceObject(FileObject);
        }

        /* Compute the size of the control area */
        if (AllocationAttributes & SEC_IMAGE)
        {
            /* Image-file backed section */
            ControlAreaSize = sizeof(LARGE_CONTROL_AREA) + sizeof(SUBSECTION);
            CcWaitForUninitializeCacheMap(File);
        }
        else
        {
            /* Data-file backed section */
            ControlAreaSize = sizeof(CONTROL_AREA) + sizeof(MSUBSECTION);
        }

        /* Alocate the control area */
        NewControlArea = ExAllocatePoolWithTag(NonPagedPool, ControlAreaSize, 'aCmM');
        if (!NewControlArea)
        {
            DPRINT1("MmCreateSection: return STATUS_INSUFFICIENT_RESOURCES\n");
            ObDereferenceObject(File);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Zero it out */
        RtlZeroMemory(NewControlArea, ControlAreaSize);

        /* Did we get a handle, or an object? */
        if (FileHandle)
        {
            /* We got a file handle so we have to lock down the file */
#if 0
            Status = FsRtlAcquireToCreateMappedSection(File, SectionPageProtection);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("MmCreateSection: Status %X\n", Status);
                ExFreePoolWithTag(NewControlArea, 'aCmM');
                ObDereferenceObject(File);
                return Status;
            }
#else
            /* ReactOS doesn't support this API yet, so do nothing */
            DPRINT("MmCreateSection: FIXME FsRtlAcquireToCreateMappedSection\n");
            Status = STATUS_SUCCESS;
#endif
            /* Update the top-level IRP so that drivers know what's happening */
            IoSetTopLevelIrp((PIRP)FSRTL_FSP_TOP_LEVEL_IRP);
            FileLock = TRUE;
        }

        while (TRUE)
        {
            /* Lock the PFN database while we play with the section pointers */
            OldIrql = MiLockPfnDb(APC_LEVEL);

            if (AllocationAttributes & SEC_IMAGE)
                /* Find control area for image-file backed section */
                ControlArea = MiFindImageSectionObject(File, TRUE, &IsGlobal);
            else
                /* Get control area from file */
                ControlArea = (PCONTROL_AREA)File->SectionObjectPointer->DataSectionObject;

            if (!ControlArea)
            {
                /* Write down that this CA is being created, and set it */
                ControlArea = NewControlArea;
                ControlArea->u.Flags.BeingCreated = 1;

                if (AllocationAttributes & SEC_IMAGE)
                {
                    MiInsertImageSectionObject(File, (PLARGE_CONTROL_AREA)NewControlArea);
                }
                else
                {
                    PreviousSectionPointer = File->SectionObjectPointer;
                    File->SectionObjectPointer->DataSectionObject = ControlArea;
                }

                break;
            }
            else
            {
                if (ControlArea->u.Flags.BeingDeleted || ControlArea->u.Flags.BeingCreated)
                {
                    DPRINT1("MmCreateSection: FIXME\n");
                    ASSERT(FALSE);
                    continue;
                }

                if (ControlArea->u.Flags.ImageMappedInSystemSpace &&
                    (AllocationAttributes & SEC_IMAGE) &&
                    KeGetCurrentThread()->PreviousMode != KernelMode)
                {
                    MiUnlockPfnDb(OldIrql, APC_LEVEL);

                    /* Check if we locked and set the IRP */
                    if (FileLock)
                    {
                        /* Reset the top-level IRP and release the lock */
                        IoSetTopLevelIrp(NULL);
                        //FsRtlReleaseFile(File);
                    }

                    DPRINT1("MmCreateSection: STATUS_CONFLICTING_ADDRESSES\n");

                    ExFreePoolWithTag(NewControlArea, 'aCmM');
                    ObDereferenceObject(File);
                    return STATUS_CONFLICTING_ADDRESSES;
                }

                NewSegment = ControlArea->Segment;

                ControlArea->u.Flags.Accessed = 1;
                ControlArea->NumberOfSectionReferences++;

                if (ControlArea->DereferenceList.Flink)
                {
                    RemoveEntryList(&ControlArea->DereferenceList);

                    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
                    ASSERT(MmPfnOwner == KeGetCurrentThread());
                    MmUnusedSegmentCount--;

                    ControlArea->DereferenceList.Flink = NULL;
                    ControlArea->DereferenceList.Blink = NULL;
                }

                UserRefIncremented = TRUE;

                if (!IgnoreFileSizing)
                    ControlArea->NumberOfUserReferences++;

                break;
            }
        }

        /* We can release the PFN lock now */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);

        if ((AllocationAttributes & SEC_IMAGE) && File && (File->FileName.Length > 4))
        {
            DPRINT("MmCreateSection: File %p '%wZ' \n", File, &File->FileName);
        }

        if (!NewSegment)
        {
            if (AllocationAttributes & SEC_IMAGE)
            {
                /* Create image-file backed sections */
                Status = MiCreateImageFileMap(File, &Segment);
            }
            else
            {
                /* So create a data file map */
                Status = MiCreateDataFileMap(File,
                                             &Segment,
                                             (PSIZE_T)InputMaximumSize,
                                             SectionPageProtection,
                                             AllocationAttributes,
                                             IgnoreFileSizing);
                /*We expect this */
                ASSERT(PreviousSectionPointer == File->SectionObjectPointer);
            }

            if (!NT_SUCCESS(Status))
            {
                /* Lock the PFN database while we undo */
                OldIrql = MiLockPfnDb(APC_LEVEL);

                /* Reset the waiting-for-deletion event */
                Event = ControlArea->WaitingForDeletion;
                ControlArea->WaitingForDeletion = NULL;

                /* Set the file pointer NULL flag */
                ASSERT(ControlArea->u.Flags.FilePointerNull == 0);
                ControlArea->u.Flags.FilePointerNull = 1;

                /* Delete the section object */
                if (AllocationAttributes & SEC_IMAGE)
                {
                    MiRemoveImageSectionObject(File, (PLARGE_CONTROL_AREA)ControlArea);
                }
                else
                {
                    File->SectionObjectPointer->DataSectionObject = NULL;
                }

                /* No longer being created */
                ControlArea->u.Flags.BeingCreated = 0;

                /* We can release the PFN lock now */
                MiUnlockPfnDb(OldIrql, APC_LEVEL);

                /* Check if we locked and set the IRP */
                if (FileLock)
                {
                    /* Reset the top-level IRP and release the lock */
                    IoSetTopLevelIrp(NULL);
                    //FsRtlReleaseFile(File);
                }

                /* Free the control area and de-ref the file object */
                ExFreePoolWithTag(NewControlArea, 'aCmM');
                ObDereferenceObject(File);

                if (Event)
                    KeSetEvent(&Event->Event, 0, FALSE);

                /* All done */
                DPRINT1("MmCreateSection: Status %X\n", Status);
                return Status;
            }

            /* Check if a maximum size was specified */
            if (!InputMaximumSize->QuadPart)
            {
                /* Nope, use the segment size */
                Section.SizeOfSection.QuadPart = (LONGLONG)Segment->SizeOfSegment;
                //DPRINT("MmCreateSection: Section.SizeOfSection.QuadPart %I64X\n", Section.SizeOfSection.QuadPart);
            }
            else
            {
                /* Yep, use the entered size */
                Section.SizeOfSection.QuadPart = InputMaximumSize->QuadPart;
                DPRINT("MmCreateSection: Section.SizeOfSection.QuadPart %I64X\n", Section.SizeOfSection.QuadPart);
            }
        }
        else
        {
            /* This is a previously mapped file. */
            if (AllocationAttributes & SEC_IMAGE)
            {
                DPRINT1("MmCreateSection: FIXME MiFlushDataSection \n");
                //MiFlushDataSection(File);
            }

            /* Free the new control area */
            ExFreePoolWithTag(NewControlArea, 'aCmM');

            if (IgnoreFileSizing || ControlArea->u.Flags.Image)
            {
                /* If it CC or image-file section get size from Segment */
                FileSize.QuadPart = NewSegment->SizeOfSegment;
            }
            else
            {
                /* For data-file section get size from file */
                Status = FsRtlGetFileSize(File, &FileSize);

                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("MmCreateSection: Status %X\n", Status);

                    /* Check if we locked and set the IRP */
                    if (FileLock)
                    {
                        /* Reset the top-level IRP and release the lock */
                        IoSetTopLevelIrp(NULL);
                        //FsRtlReleaseFile(File);
                        FileLock = FALSE;
                    }

                    /* De-ref the file object */
                    ObDereferenceObject(File);
                    goto ErrorExit;
                }

                if (!FileSize.QuadPart && !InputMaximumSize->QuadPart)
                {
                    DPRINT1("MmCreateSection: STATUS_MAPPED_FILE_SIZE_ZERO\n");

                    /* Check if we locked and set the IRP */
                    if (FileLock)
                    {
                        /* Reset the top-level IRP and release the lock */
                        IoSetTopLevelIrp(NULL);
                        //FsRtlReleaseFile(File);
                        FileLock = FALSE;
                    }

                    ObDereferenceObject(File);
                    Status = STATUS_MAPPED_FILE_SIZE_ZERO;
                    goto ErrorExit;
                }
            }

            /* Check if we locked and set the IRP */
            if (FileLock)
            {
                /* Reset the top-level IRP and release the lock */
                IoSetTopLevelIrp(NULL);
                //FsRtlReleaseFile( File);
                FileLock = FALSE;
            }

            /* De-ref the file object */
            ObDereferenceObject(File);

            /* Set sizeof for the section */
            if (InputMaximumSize->QuadPart == 0)
            {
                Section.SizeOfSection.QuadPart = FileSize.QuadPart;
                DPRINT("MmCreateSection: Section.SizeOfSection.QuadPart %I64X\n", Section.SizeOfSection.QuadPart);
                IsSectionSizeChanged = TRUE;
            }
            else
            {
                if (FileSize.QuadPart < InputMaximumSize->QuadPart)
                {
                    if (!(SectionPageProtection & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
                    {
                        Status = STATUS_SECTION_TOO_BIG;
                        goto ErrorExit;
                    }
                }
                else
                {
                    IsSectionSizeChanged = TRUE;
                }

                Section.SizeOfSection.QuadPart = InputMaximumSize->QuadPart;
                DPRINT("MmCreateSection: Section.SizeOfSection.QuadPart %I64X\n", Section.SizeOfSection.QuadPart);
            }
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
        /* This must be the file path and we created a segment */
        NewSegment = Segment;
        ASSERT(File != NULL);

        /* Acquire the PFN lock while we set control area */
        OldIrql = MiLockPfnDb(APC_LEVEL);

        /* Reset the waiting-for-deletion event */
        Event = ControlArea->WaitingForDeletion;
        ControlArea->WaitingForDeletion = NULL;

        if (AllocationAttributes & SEC_IMAGE)
        {
            /* Image-file backed section*/
            ASSERT(FALSE);
        }
        else if (NewSegment->ControlArea->u.Flags.Rom)
        {
            /* ROM image sections */
            ASSERT(File->SectionObjectPointer->DataSectionObject == NewControlArea);
            File->SectionObjectPointer->DataSectionObject = NewSegment->ControlArea;
            ControlArea = NewSegment->ControlArea;
        }

        /* Take off the being created flag, and then release the lock */
        ControlArea->u.Flags.BeingCreated = 0;
        MiUnlockPfnDb(OldIrql, APC_LEVEL);

        if ((AllocationAttributes & SEC_IMAGE) || NewSegment->ControlArea->u.Flags.Rom)
            /* Free the new control area */
            ExFreePoolWithTag(NewControlArea, 'aCmM');

        if (Event)
            KeSetEvent(&Event->Event, 0, FALSE);
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

ErrorExit:

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
    _In_ PSECTION_OBJECT_POINTERS SectionPointer,
    _In_ MMFLUSH_TYPE FlushType)
{
    PCONTROL_AREA ControlArea;
    KIRQL OldIrql;
    BOOLEAN Result;

    DPRINT("MmFlushImageSection: SectionPointers %p, FlushType %X\n", SectionPointer, FlushType);

    if (FlushType == MmFlushForDelete)
    {
        OldIrql = MiLockPfnDb(APC_LEVEL);

        ControlArea = SectionPointer->DataSectionObject;

        if (ControlArea &&
            (ControlArea->NumberOfUserReferences || ControlArea->u.Flags.BeingCreated))
        {
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            return FALSE;
        }

        MiUnlockPfnDb(OldIrql, APC_LEVEL);
    }

    Result = MiCheckControlAreaStatus(1, SectionPointer, FALSE, &ControlArea, &OldIrql);

    if (!ControlArea)
        return Result;

    DPRINT("MmFlushImageSection: FIXME. ControlArea %p\n", ControlArea);
    ASSERT(FALSE);

    return TRUE;
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
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    NTSTATUS Status;

    DPRINT("NtUnmapViewOfSection: BaseAddress %p\n", BaseAddress);

    /* Don't allowing mapping kernel views */
    if (PreviousMode == UserMode && BaseAddress > MM_HIGHEST_USER_ADDRESS)
    {
        DPRINT1("Trying to unmap a kernel view\n");
        return STATUS_NOT_MAPPED_VIEW;
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
        DPRINT1("NtUnmapViewOfSection: Status %X\n", Status);
        return Status;
    }

    /* Unmap the view */
    Status = MiUnmapViewOfSection(Process, BaseAddress, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtUnmapViewOfSection: Status %X\n", Status);
    }

    /* Dereference the process and return status */
    ObDereferenceObject(Process);

    return Status;
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
