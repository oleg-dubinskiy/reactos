
#pragma once

/* TYPES *********************************************************************/

/* Make the code cleaner with some definitions for size multiples */
#define _1KB (1024u)
#define _1MB (1024 * _1KB)
#define _1GB (1024 * _1MB)

/* Everyone loves 64K */
#define _64K (64 * _1KB)

/* System views are binned into 64K chunks*/
#define MI_SYSTEM_VIEW_BUCKET_SIZE  _64K

/* Number of pages in one unit of the system cache */
#define MM_PAGES_PER_VACB  (VACB_MAPPING_GRANULARITY / PAGE_SIZE)

/* Protection Bits part of the internal memory manager Protection Mask, from:
   http://reactos.org/wiki/Techwiki:Memory_management_in_the_Windows_XP_kernel
   https://www.reactos.org/wiki/Techwiki:Memory_Protection_constants
   and public assertions.
*/
#define MM_ZERO_ACCESS         0
#define MM_READONLY            1
#define MM_EXECUTE             2
#define MM_EXECUTE_READ        3
#define MM_READWRITE           4
#define MM_WRITECOPY           5
#define MM_EXECUTE_READWRITE   6
#define MM_EXECUTE_WRITECOPY   7
#define MM_PROTECT_ACCESS      7

/* These are flags on top of the actual protection mask */
#define MM_NOCACHE            0x08
#define MM_GUARDPAGE          0x10
#define MM_WRITECOMBINE       0x18
#define MM_PROTECT_SPECIAL    0x18

/* These are special cases */
#define MM_DECOMMIT           (MM_ZERO_ACCESS | MM_GUARDPAGE)
#define MM_NOACCESS           (MM_ZERO_ACCESS | MM_WRITECOMBINE)
#define MM_OUTSWAPPED_KSTACK  (MM_EXECUTE_WRITECOPY | MM_WRITECOMBINE)
#define MM_INVALID_PROTECTION  0xFFFFFFFF

/* Specific PTE Definitions that map to the Memory Manager's Protection Mask Bits.
   The Memory Manager's definition define the attributes that must be preserved
   and these PTE definitions describe the attributes in the hardware sense.
   This helps deal with hardware differences between the actual boolean expression of the argument.

   For example, in the logical attributes, we want to express read-only as a flag but on x86,
   it is writability that must be set. On the other hand, on x86, just like in the kernel,
   it is disabling the caches that requires a special flag, while on certain architectures such as ARM,
   it is enabling the cache which requires a flag.
*/

#if defined(_M_IX86)

/* Access Flags */
#define PTE_READONLY            0 // Doesn't exist on x86
#define PTE_EXECUTE             0 // Not worrying about NX yet
#define PTE_EXECUTE_READ        0 // Not worrying about NX yet
#define PTE_READWRITE           0x2
#define PTE_WRITECOPY           0x200
#define PTE_EXECUTE_READWRITE   0x2 // Not worrying about NX yet
#define PTE_EXECUTE_WRITECOPY   0x200
#define PTE_PROTOTYPE           0x400

/* State Flags */
#define PTE_VALID               0x1
#define PTE_ACCESSED            0x20
#define PTE_DIRTY               0x40

/* Cache flags */
#define PTE_ENABLE_CACHE        0
#define PTE_DISABLE_CACHE       0x10
#define PTE_WRITECOMBINED_CACHE 0x10
#define PTE_PROTECT_MASK        0x612

#else
  #error Define these please!
#endif

extern const ULONG_PTR MmProtectToPteMask[32];
extern const ULONG MmProtectToValue[32];
extern PMMPTE MiSessionBasePte;
extern PMMPTE MiSessionLastPte;
extern PVOID MmSessionBase;
extern PVOID MiSessionSpaceEnd;

/* Assertions for session images, addresses, and PTEs */
#define MI_IS_SESSION_IMAGE_ADDRESS(Address) \
    (((Address) >= MiSessionImageStart) && ((Address) < MiSessionImageEnd))

#define MI_IS_SESSION_ADDRESS(Address) \
    (((Address) >= MmSessionBase) && ((Address) < MiSessionSpaceEnd))

#define MI_IS_SESSION_PTE(Pte) \
    ((((PMMPTE)Pte) >= MiSessionBasePte) && (((PMMPTE)Pte) < MiSessionLastPte))

#define MI_IS_PAGE_TABLE_ADDRESS(Address) \
    (((PVOID)(Address) >= (PVOID)PTE_BASE) && ((PVOID)(Address) <= (PVOID)PTE_TOP))

#define MI_IS_SYSTEM_PAGE_TABLE_ADDRESS(Address) \
    (((Address) >= (PVOID)MiAddressToPte(MmSystemRangeStart)) && ((Address) <= (PVOID)PTE_TOP))

#define MI_IS_PAGE_TABLE_OR_HYPER_ADDRESS(Address) \
    (((PVOID)(Address) >= (PVOID)PTE_BASE) && ((PVOID)(Address) <= (PVOID)MmHyperSpaceEnd))

#define InterlockedExchangePte(Pte, Value) \
    InterlockedExchange((PLONG)(Pte), Value)

/* Creates a software PTE with the given protection */
#define MI_MAKE_SOFTWARE_PTE(p, x)  ((p)->u.Long = (x << MM_PTE_SOFTWARE_PROTECTION_BITS))

/* Marks a PTE as deleted */
#define MI_SET_PFN_DELETED(x)       ((x)->PteAddress = (PMMPTE)((ULONG_PTR)(x)->PteAddress | 1))
#define MI_IS_PFN_DELETED(x)        ((ULONG_PTR)((x)->PteAddress) & 1)

#ifdef __REACTOS__
  #define MI_IS_ROS_PFN(x)          ((x)->u4.AweAllocation == TRUE)
#endif

#if defined(_M_IX86) || defined(_M_ARM)
  /* PFN List Sentinel */
  #define LIST_HEAD 0xFFFFFFFF

  /* Because GCC cannot automatically downcast 0xFFFFFFFF to lesser-width bits,
     we need a manual definition suited to the number of bits in the PteFrame.
     This is used as a LIST_HEAD for the colored list
  */
  #define COLORED_LIST_HEAD ((1 << 25) - 1)    // 0x1FFFFFF
#elif defined(_M_AMD64)
  #define LIST_HEAD 0xFFFFFFFFFFFFFFFFLL
  #define COLORED_LIST_HEAD ((1ULL << 57) - 1) // 0x1FFFFFFFFFFFFFFLL
#else
  #error Define these please!
#endif

/* Returns the color of a page */
#define MI_GET_PAGE_COLOR(x)          ((x) & MmSecondaryColorMask)
#if !defined(CONFIG_SMP)
  #define MI_GET_NEXT_COLOR()         (MI_GET_PAGE_COLOR(++MmSystemPageColor))
#else
  #define MI_GET_NEXT_COLOR()         (MI_GET_PAGE_COLOR(++KeGetCurrentPrcb()->PageColor))
#endif
#define MI_GET_NEXT_PROCESS_COLOR(x)  (MI_GET_PAGE_COLOR(++(x)->NextPageColor))

/* These two mappings are actually used by Windows itself, based on the ASSERTS */
#define StartOfAllocation ReadInProgress
#define EndOfAllocation WriteInProgress

#define MiGetPteContents(Pte) \
    (ULONGLONG)((Pte != NULL) ? (Pte->u.Long) : (0))

/* FIXFIX: These should go in ex.h after the pool merge */

#ifdef _WIN64
  #define POOL_BLOCK_SIZE 16
#else
  #define POOL_BLOCK_SIZE  8
#endif

#define POOL_LISTS_PER_PAGE (PAGE_SIZE / POOL_BLOCK_SIZE)
#define BASE_POOL_TYPE_MASK 1
#define POOL_MAX_ALLOC      (PAGE_SIZE - (sizeof(POOL_HEADER) + POOL_BLOCK_SIZE))

/* Pool debugging/analysis/tracing flags */
#define POOL_FLAG_CHECK_TIMERS         0x01
#define POOL_FLAG_CHECK_WORKERS        0x02
#define POOL_FLAG_CHECK_RESOURCES      0x04
#define POOL_FLAG_VERIFIER             0x08
#define POOL_FLAG_CHECK_DEADLOCK       0x10
#define POOL_FLAG_SPECIAL_POOL         0x20
#define POOL_FLAG_DBGPRINT_ON_FAILURE  0x40
#define POOL_FLAG_CRASH_ON_FAILURE     0x80

/* BAD_POOL_HEADER codes during pool bugcheck */
#define POOL_CORRUPTED_LIST                3
#define POOL_SIZE_OR_INDEX_MISMATCH        5
#define POOL_ENTRIES_NOT_ALIGNED_PREVIOUS  6
#define POOL_HEADER_NOT_ALIGNED            7
#define POOL_HEADER_IS_ZERO                8
#define POOL_ENTRIES_NOT_ALIGNED_NEXT      9
#define POOL_ENTRY_NOT_FOUND               10

/* BAD_POOL_CALLER codes during pool bugcheck */
#define POOL_ENTRY_CORRUPTED         1
#define POOL_ENTRY_ALREADY_FREE      6
#define POOL_ENTRY_NOT_ALLOCATED     7
#define POOL_ALLOC_IRQL_INVALID      8
#define POOL_FREE_IRQL_INVALID       9
#define POOL_BILLED_PROCESS_INVALID  13
#define POOL_HEADER_SIZE_INVALID     32

typedef struct _POOL_DESCRIPTOR
{
    POOL_TYPE PoolType;
    ULONG PoolIndex;
    ULONG RunningAllocs;
    ULONG RunningDeAllocs;
    ULONG TotalPages;
    ULONG TotalBigPages;
    ULONG Threshold;
    PVOID LockAddress;
    PVOID PendingFrees;
    LONG PendingFreeDepth;
    SIZE_T TotalBytes;
    SIZE_T Spare0;
    LIST_ENTRY ListHeads[POOL_LISTS_PER_PAGE];
} POOL_DESCRIPTOR, *PPOOL_DESCRIPTOR;

typedef struct _POOL_HEADER
{
    union
    {
        struct
        {
          #ifdef _WIN64
            USHORT PreviousSize:8;
            USHORT PoolIndex:8;
            USHORT BlockSize:8;
            USHORT PoolType:8;
          #else
            USHORT PreviousSize:9;
            USHORT PoolIndex:7;
            USHORT BlockSize:9;
            USHORT PoolType:7;
          #endif
        };
        ULONG Ulong1;
    };
  #ifdef _WIN64
    ULONG PoolTag;
  #endif
    union
    {
      #ifdef _WIN64
        PEPROCESS ProcessBilled;
      #else
        ULONG PoolTag;
      #endif
        struct
        {
            USHORT AllocatorBackTraceIndex;
            USHORT PoolTagHash;
        };
    };
} POOL_HEADER, *PPOOL_HEADER;

C_ASSERT(sizeof(POOL_HEADER) == POOL_BLOCK_SIZE);
C_ASSERT(POOL_BLOCK_SIZE == sizeof(LIST_ENTRY));

typedef struct _POOL_TRACKER_TABLE
{
    ULONG Key;
    LONG NonPagedAllocs;
    LONG NonPagedFrees;
    SIZE_T NonPagedBytes;
    LONG PagedAllocs;
    LONG PagedFrees;
    SIZE_T PagedBytes;
} POOL_TRACKER_TABLE, *PPOOL_TRACKER_TABLE;

/* END FIXFIX */

typedef enum _MMSYSTEM_PTE_POOL_TYPE
{
    SystemPteSpace,
    NonPagedPoolExpansion,
    MaximumPtePoolTypes
} MMSYSTEM_PTE_POOL_TYPE;

typedef enum _MI_PFN_CACHE_ATTRIBUTE
{
    MiNonCached,
    MiCached,
    MiWriteCombined,
    MiNotMapped
} MI_PFN_CACHE_ATTRIBUTE, *PMI_PFN_CACHE_ATTRIBUTE;

typedef struct _PHYSICAL_MEMORY_RUN
{
    PFN_NUMBER BasePage;
    PFN_NUMBER PageCount;
} PHYSICAL_MEMORY_RUN, *PPHYSICAL_MEMORY_RUN;

typedef struct _PHYSICAL_MEMORY_DESCRIPTOR
{
    ULONG NumberOfRuns;
    PFN_NUMBER NumberOfPages;
    PHYSICAL_MEMORY_RUN Run[1];
} PHYSICAL_MEMORY_DESCRIPTOR, *PPHYSICAL_MEMORY_DESCRIPTOR;

typedef struct _MMCOLOR_TABLES
{
    PFN_NUMBER Flink;
    PVOID Blink;
    PFN_NUMBER Count;
} MMCOLOR_TABLES, *PMMCOLOR_TABLES;

typedef struct _MMVIEW
{
    ULONG_PTR Entry;
    PCONTROL_AREA ControlArea;
} MMVIEW, *PMMVIEW;

typedef struct _MMSESSION
{
    KGUARDED_MUTEX SystemSpaceViewLock;
    PKGUARDED_MUTEX SystemSpaceViewLockPointer;
    PCHAR SystemSpaceViewStart;
    PMMVIEW SystemSpaceViewTable;
    ULONG SystemSpaceHashSize;
    ULONG SystemSpaceHashEntries;
    ULONG SystemSpaceHashKey;
    ULONG BitmapFailures;
    PRTL_BITMAP SystemSpaceBitMap;
} MMSESSION, *PMMSESSION;

typedef struct _MM_SESSION_SPACE_FLAGS
{
    ULONG Initialized:1;
    ULONG DeletePending:1;
    ULONG Filler:30;
} MM_SESSION_SPACE_FLAGS;

typedef struct _MM_SESSION_SPACE
{
    struct _MM_SESSION_SPACE *GlobalVirtualAddress;
    LONG ReferenceCount;
    union
    {
        ULONG LongFlags;
        MM_SESSION_SPACE_FLAGS Flags;
    } u;
    ULONG SessionId;
    LIST_ENTRY ProcessList;
    LARGE_INTEGER LastProcessSwappedOutTime;
    PFN_NUMBER SessionPageDirectoryIndex;
    SIZE_T NonPageablePages;
    SIZE_T CommittedPages;
    PVOID PagedPoolStart;
    PVOID PagedPoolEnd;
    PMMPDE PagedPoolBasePde;
    ULONG Color;
    LONG ResidentProcessCount;
    ULONG SessionPoolAllocationFailures[4];
    LIST_ENTRY ImageList;
    LCID LocaleId;
    ULONG AttachCount;
    KEVENT AttachEvent;
    PEPROCESS LastProcess;
    LONG ProcessReferenceToSession;
    LIST_ENTRY WsListEntry;
    GENERAL_LOOKASIDE Lookaside[SESSION_POOL_LOOKASIDES];
    MMSESSION Session;
    KGUARDED_MUTEX PagedPoolMutex;
    MM_PAGED_POOL_INFO PagedPoolInfo;
    MMSUPPORT Vm;
    PMMWSLE Wsle;
    PDRIVER_UNLOAD Win32KDriverUnload;
    POOL_DESCRIPTOR PagedPool;
  #if defined (_M_AMD64)
    MMPDE PageDirectory;
  #else
    PMMPDE PageTables;
  #endif
  #if defined (_M_AMD64)
    PMMPTE SpecialPoolFirstPte;
    PMMPTE SpecialPoolLastPte;
    PMMPTE NextPdeForSpecialPoolExpansion;
    PMMPTE LastPdeForSpecialPoolExpansion;
    PFN_NUMBER SpecialPagesInUse;
  #endif
    LONG ImageLoadingCount;
} MM_SESSION_SPACE, *PMM_SESSION_SPACE;

#define MI_ASSERT_PFN_LOCK_HELD()  ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL)

#if MI_TRACE_PFNS
  #error Define these please!
#else
  #define MI_SET_USAGE(x)
  #define MI_SET_PROCESS2(x)
#endif

/* Signature of a freed block */
#define MM_FREE_POOL_SIGNATURE 'ARM3'

/* Entry describing free pool memory */
typedef struct _MMFREE_POOL_ENTRY
{
    LIST_ENTRY List;
    PFN_COUNT Size;
    ULONG Signature;
    struct _MMFREE_POOL_ENTRY *Owner;
} MMFREE_POOL_ENTRY, *PMMFREE_POOL_ENTRY;

typedef struct _POOL_TRACKER_BIG_PAGES
{
    PVOID Va;
    ULONG Key;
    ULONG NumberOfPages;
    PVOID QuotaObject;
} POOL_TRACKER_BIG_PAGES, *PPOOL_TRACKER_BIG_PAGES;

extern PVOID MmPagedPoolStart;
extern PVOID MmNonPagedPoolEnd;
extern ULONG_PTR MmSubsectionBase;
extern ULONG MmSystemPageColor;
extern PMMPDE MiHighestUserPde;
extern PMMPTE MiHighestUserPte;
extern MMPTE ValidKernelPte;

/* FUNCTIONS *****************************************************************/

#ifndef _M_AMD64

/* Builds a Prototype PTE from the poiner to section proto structure */
FORCEINLINE
VOID
MI_MAKE_PROTOTYPE_PTE(
    _In_ PMMPTE ProtoPte,
    _In_ PMMPTE SectionProto)
{
    ULONG_PTR Offset;

    /* Mark this as a prototype */
    ProtoPte->u.Long = 0;
    ProtoPte->u.Proto.Prototype = 1;

  #if !defined(_X86PAE_)
    /* Section proto structures are only valid in paged pool by design,
       this little trick lets us only use 30 bits for the adress of the PTE,
       as long as the area stays 1024 MB at most.
    */
    Offset = ((ULONG_PTR)SectionProto - (ULONG_PTR)MmPagedPoolStart);

    /* 9 bits go in the "low" (we assume the bottom 2 are zero and trim it) */
    ASSERT((Offset % 4) == 0);
    ProtoPte->u.Proto.ProtoAddressLow = ((Offset & 0x1FF) >> 2);

    /* and the other 21 bits go in the "high" field. */
    ProtoPte->u.Proto.ProtoAddressHigh = ((Offset & 0x3FFFFE00) >> 9);
  #else
    ProtoPte->u.Proto.ProtoAddress = (ULONG_PTR)SectionProto;
  #endif
}

/* Decodes a Prototype PTE into the poiner to section proto structure */
FORCEINLINE
PMMPTE
MiGetProtoPtr(
    _In_ PMMPTE SectionProto)
{
    ULONG_PTR Offset;

    /* Do MI_MAKE_PROTOTYPE_PTE() in the opposite direction. */

  #if !defined(_X86PAE_)
    Offset = (SectionProto->u.Proto.ProtoAddressHigh << 9);
    Offset += (SectionProto->u.Proto.ProtoAddressLow << 2);

    return (PMMPTE)((ULONG_PTR)MmPagedPoolStart + Offset);
  #else
    return (PMMPTE)SectionProto->u.Proto.ProtoAddress;
  #endif
}

/* Builds a Subsection PTE for the address of the Subsection */
FORCEINLINE
VOID
MI_MAKE_SUBSECTION_PTE(
    _In_ PMMPTE NewPte,
    _In_ PVOID Subsection)
{
    ULONG_PTR Offset;

    /* Mark this as a prototype */
    NewPte->u.Long = 0;
    NewPte->u.Subsect.Prototype = 1;

  #if !defined(_X86PAE_)
    /* Subsections are only in nonpaged pool (NP).
       We use the 27-bit difference either from the top or bottom of NP,
       giving a maximum of 128 MB to each delta, meaning NP cannot exceed 256 MB.
    */
    if ((ULONG_PTR)Subsection < ((ULONG_PTR)MmSubsectionBase + (128 * _1MB)))
    {
        /* MmNonPagedPoolStart ... + MmSizeOfNonPagedPoolInBytes */
        Offset = ((ULONG_PTR)Subsection - (ULONG_PTR)MmSubsectionBase);
        NewPte->u.Subsect.WhichPool = 1;
    }
    else
    {
        /* MmNonPagedPoolExpansionStart ... MmNonPagedPoolEnd */
        Offset = ((ULONG_PTR)MmNonPagedPoolEnd - (ULONG_PTR)Subsection);
        NewPte->u.Subsect.WhichPool = 0;
    }

    /* 7 bits go in the "low" (we assume the bottom 3 are zero and trim it) */
    ASSERT((Offset % 8) == 0);
    NewPte->u.Subsect.SubsectionAddressLow = ((Offset & 0x7F) >> 3);

    /* and the other 20 bits go in the "high" field */
    NewPte->u.Subsect.SubsectionAddressHigh = ((Offset & 0xFFFFF80) >> 7);
  #else
    NewPte->u.Subsect.SubsectionAddress = (ULONG_PTR)Subsection;
  #endif
}

/* Get a pointer to a Subsection from the Subsection PTE */
FORCEINLINE
PVOID
MiSubsectionPteToSubsection(
    _In_ PMMPTE SubsectionPte)
{
    ULONG_PTR Offset;

    /* Do MI_MAKE_SUBSECTION_PTE() in the opposite direction. */

  #if !defined(_X86PAE_)
    Offset = (SubsectionPte->u.Subsect.SubsectionAddressHigh << 7);
    Offset += (SubsectionPte->u.Subsect.SubsectionAddressLow << 3);

    if (SubsectionPte->u.Subsect.WhichPool == 1)
    {
        /* MmNonPagedPoolStart ... + MmSizeOfNonPagedPoolInBytes */
        return (PVOID)((ULONG_PTR)MmSubsectionBase + Offset);
    }
    else
    {
        /* MmNonPagedPoolExpansionStart ... MmNonPagedPoolEnd */
        return (PVOID)((ULONG_PTR)MmNonPagedPoolEnd - Offset);
    }
  #else
    return (PVOID)OutPte->u.Subsect.SubsectionAddress;
  #endif
}

FORCEINLINE
BOOLEAN
MI_IS_MAPPED_PTE(
    _In_ PMMPTE Pte)
{
    if (Pte->u.Soft.Valid ||
        Pte->u.Soft.Prototype ||
        Pte->u.Soft.Transition ||
      #if defined(_X86PAE_)
        Pte->u.Soft.Unused ||
      #endif
        Pte->u.Soft.PageFileHigh)
    {
        return TRUE;
    }
    else
    {
        /* Demand zero PTE */
        return FALSE;
    }
}

#endif

FORCEINLINE
KIRQL 
MiLockPfnDb(
    _In_ KIRQL MaximumLevel)
{
    KIRQL OldIrql;

    if (MaximumLevel == APC_LEVEL)
        ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    else if (MaximumLevel == DISPATCH_LEVEL)
        ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    OldIrql = KeAcquireQueuedSpinLock(LockQueuePfnLock);

    ASSERT(MmPfnOwner == NULL);
    MmPfnOwner = KeGetCurrentThread();

    return OldIrql;
}

FORCEINLINE
VOID 
MiUnlockPfnDb(
    _In_ KIRQL OldIrql,
    _In_ KIRQL MaximumLevel)
{
    if (MaximumLevel == APC_LEVEL)
        ASSERT(OldIrql <= APC_LEVEL);
    else if (MaximumLevel == DISPATCH_LEVEL)
        ASSERT(OldIrql <= DISPATCH_LEVEL);

    ASSERT(MmPfnOwner == KeGetCurrentThread());
    MmPfnOwner = NULL;

    KeReleaseQueuedSpinLock(LockQueuePfnLock, OldIrql);

    if (MaximumLevel == APC_LEVEL)
        ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    else if (MaximumLevel == DISPATCH_LEVEL)
        ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
}

FORCEINLINE
KIRQL
MiAcquirePfnLock(VOID)
{
    return KeAcquireQueuedSpinLock(LockQueuePfnLock);
}

FORCEINLINE
VOID
MiReleasePfnLock(
    _In_ KIRQL OldIrql)
{
    KeReleaseQueuedSpinLock(LockQueuePfnLock, OldIrql);
}

FORCEINLINE
VOID
MiAcquirePfnLockAtDpcLevel(VOID)
{
    PKSPIN_LOCK_QUEUE LockQueue;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    LockQueue = &KeGetCurrentPrcb()->LockQueue[LockQueuePfnLock];
    KeAcquireQueuedSpinLockAtDpcLevel(LockQueue);
}

FORCEINLINE
VOID
MiReleasePfnLockFromDpcLevel(VOID)
{
    PKSPIN_LOCK_QUEUE LockQueue;

    LockQueue = &KeGetCurrentPrcb()->LockQueue[LockQueuePfnLock];
    KeReleaseQueuedSpinLockFromDpcLevel(LockQueue);
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
}

/* Returns the PFN Database entry for the given page number.
   Warning: This is not necessarily a valid PFN database entry!
*/
FORCEINLINE
PMMPFN
MI_PFN_ELEMENT(
    _In_ PFN_NUMBER Pfn)
{
    /* Get the entry */
    return &MmPfnDatabase[Pfn];
};

FORCEINLINE
PFN_NUMBER
MiGetPfnEntryIndex(
    _In_ PMMPFN Pfn1)
{
    /* This will return the Page Frame Number (PFN) from the MMPFN */
    return (Pfn1 - MmPfnDatabase);
}

#ifdef _M_AMD64
  #error FIXME
#else
FORCEINLINE
BOOLEAN
MiIsUserPde(PVOID Address)
{
    return ((Address >= (PVOID)MiAddressToPde(NULL)) &&
            (Address <= (PVOID)MiHighestUserPde));
}

FORCEINLINE
BOOLEAN
MiIsUserPte(PVOID Address)
{
    return (Address <= (PVOID)MiHighestUserPte);
}
#endif

/* Figures out the hardware bits for a PTE */
FORCEINLINE
ULONG_PTR
MiDetermineUserGlobalPteMask(
    _In_ PVOID Pte)
{
    MMPTE TempPte;

    /* Start fresh */
    TempPte.u.Long = 0;

    /* Make it valid and accessed */
    TempPte.u.Hard.Valid = TRUE;
    MI_MAKE_ACCESSED_PAGE(&TempPte);

    /* Is this for user-mode? */
    if (
      #if (_MI_PAGING_LEVELS >= 3)
        #error FIXME
      #endif
        MiIsUserPde(Pte) ||
        MiIsUserPte(Pte))
    {
        /* Set the owner bit */
        MI_MAKE_OWNER_PAGE(&TempPte);
    }

    /* FIXME: We should also set the global bit */

    /* Return the protection */
    return TempPte.u.Long;
}

/* Creates a valid PTE with the given protection */
FORCEINLINE
VOID
MI_MAKE_HARDWARE_PTE(
    _In_ PMMPTE NewPte,
    _In_ PMMPTE MappingPte,
    _In_ ULONG_PTR ProtectionMask,
    _In_ PFN_NUMBER PageFrameNumber)
{
    /* Set the protection and page */
    NewPte->u.Long = MiDetermineUserGlobalPteMask(MappingPte);
    NewPte->u.Long |= MmProtectToPteMask[ProtectionMask];
    NewPte->u.Hard.PageFrameNumber = PageFrameNumber;
}

/* Creates a valid kernel PTE with the given protection */
FORCEINLINE
VOID
MI_MAKE_HARDWARE_PTE_KERNEL(
    _In_ PMMPTE NewPte,
    _In_ PMMPTE MappingPte,
    _In_ ULONG_PTR ProtectionMask,
    _In_ PFN_NUMBER PageFrameNumber)
{
    /* Only valid for kernel, non-session PTEs */
    ASSERT(MappingPte > MiHighestUserPte);
    ASSERT(!MI_IS_SESSION_PTE(MappingPte));
    ASSERT((MappingPte < (PMMPTE)PDE_BASE) || (MappingPte > (PMMPTE)PDE_TOP));

    /* Start fresh */
    *NewPte = ValidKernelPte;

    /* Set the protection and page */
    NewPte->u.Hard.PageFrameNumber = PageFrameNumber;
    NewPte->u.Long |= MmProtectToPteMask[ProtectionMask];
}

/* Writes a valid PTE */
FORCEINLINE
VOID
MI_WRITE_VALID_PTE(
    _In_ PMMPTE Pte,
    _In_ MMPTE TempPte)
{
    /* Write the valid PTE */
    ASSERT(Pte->u.Hard.Valid == 0);
    ASSERT(TempPte.u.Hard.Valid == 1);
    *Pte = TempPte;
}

/* Writes an invalid PTE */
FORCEINLINE
VOID
MI_WRITE_INVALID_PTE(
    _In_ PMMPTE Pte,
    _In_ MMPTE InvalidPte)
{
    ASSERT(InvalidPte.u.Hard.Valid == 0);
    ASSERT(InvalidPte.u.Long != 0);

    /* Write the invalid PTE */
    *Pte = InvalidPte;
}

/* Erase the PTE completely */
FORCEINLINE
VOID
MI_ERASE_PTE(
    _In_ PMMPTE Pte)
{
    /* Zero out the PTE */
    ASSERT(Pte->u.Long != 0);
    Pte->u.Long = 0;
}

/* Writes a valid PDE */
FORCEINLINE
VOID
MI_WRITE_VALID_PDE(
    _In_ PMMPDE Pde,
    _In_ MMPDE TempPde)
{
    /* Write the valid PDE */
    ASSERT(Pde->u.Hard.Valid == 0);
    ASSERT(TempPde.u.Hard.Valid == 1);

    *Pde = TempPde;
}

/* ARM3\i386\init.c */
INIT_FUNCTION
VOID
NTAPI
MiInitializeSessionSpaceLayout(
    VOID
);

INIT_FUNCTION
NTSTATUS
NTAPI
MiInitMachineDependent(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
);

/* ARM3\expool.c */
INIT_FUNCTION
VOID
NTAPI
InitializePool(
    _In_ POOL_TYPE PoolType,
    _In_ ULONG Threshold
);

/* ARM3\hypermap.c */
PVOID
NTAPI
MiMapPageInHyperSpace(
    _In_ PEPROCESS Process,
    _In_ PFN_NUMBER Page,
    _In_ PKIRQL OldIrql
);

VOID
NTAPI
MiUnmapPageInHyperSpace(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ KIRQL OldIrql
);

/* ARM3\largepag.c */
INIT_FUNCTION
VOID
NTAPI
MiSyncCachedRanges(
    VOID
);

INIT_FUNCTION
VOID
NTAPI
MiInitializeLargePageSupport(
    VOID
);

INIT_FUNCTION
VOID
NTAPI
MiInitializeDriverLargePageList(
    VOID
);

/* ARM3\mminit.c */
INIT_FUNCTION
PPHYSICAL_MEMORY_DESCRIPTOR
NTAPI
MmInitializeMemoryLimits(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PBOOLEAN IncludeType
);

INIT_FUNCTION
PFN_NUMBER
NTAPI
MxGetNextPage(
    _In_ PFN_NUMBER PageCount
);

/* ARM3\pagfault.c */
NTSTATUS
FASTCALL
MiCheckPdeForPagedPool(
    _In_ PVOID Address
);

/* ARM3\pfnlist.c */
PFN_NUMBER
NTAPI
MiRemoveAnyPage(
    _In_ ULONG Color
);

VOID
NTAPI
MiInsertPageInFreeList(
    _In_ PFN_NUMBER PageFrameIndex
);

VOID
NTAPI
MiZeroPhysicalPage(
    _In_ PFN_NUMBER PageFrameIndex
);

VOID
NTAPI
MiInitializePfnForOtherProcess(
    _In_ PFN_NUMBER PageFrameIndex,
    _In_ PVOID PteAddress,
    _In_ PFN_NUMBER PteFrame
);

VOID
NTAPI
MiInitializePfn(
    _In_ PFN_NUMBER PageFrameIndex,
    _In_ PMMPTE Pte,
    _In_ BOOLEAN Modified
);

VOID
NTAPI
MiInitializePfnAndMakePteValid(
    _In_ PFN_NUMBER PageFrameIndex,
    _In_ PMMPTE Pte,
    _In_ MMPTE TempPte
);

VOID
NTAPI
MiDecrementShareCount(
    _In_ PMMPFN Pfn,
    _In_ PFN_NUMBER PageFrameIndex
);

PFN_NUMBER
NTAPI
MiRemoveZeroPage(
    _In_ ULONG Color
);

/* ARM3\pool.c */
INIT_FUNCTION
VOID
NTAPI
MiInitializeNonPagedPool(
    VOID
);

INIT_FUNCTION
VOID
NTAPI
MiInitializeNonPagedPoolThresholds(
    VOID
);

PVOID
NTAPI
MiAllocatePoolPages(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T SizeInBytes
);

ULONG
NTAPI
MiFreePoolPages(
    _In_ PVOID StartingVa
);

POOL_TYPE
NTAPI
MmDeterminePoolType(
    _In_ PVOID PoolAddress
);

/* ARM3\section.c */
BOOLEAN
NTAPI
MiInitializeSystemSpaceMap(
    _In_ PMMSESSION InputSession OPTIONAL
);

/* ARM3\special.c */
PVOID
NTAPI
MmAllocateSpecialPool(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag,
    _In_ POOL_TYPE PoolType,
    _In_ ULONG SpecialType
);

VOID
NTAPI
MmFreeSpecialPool(
    _In_ PVOID P
);

VOID
NTAPI
MiInitializeSpecialPool(
    VOID
);

/* ARM3\syscache.c */
VOID
NTAPI
MiInitializeSystemCache(
    _In_ ULONG MinimumWorkingSetSize,
    _In_ ULONG MaximumWorkingSetSize
);

/* ARM3\sysldr.c */
INIT_FUNCTION
VOID
NTAPI
MiReloadBootLoadedDrivers(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
);

/* ARM3\syspte.c */
INIT_FUNCTION
VOID
NTAPI
MiInitializeSystemPtes(
    _In_ PMMPTE StartingPte,
    _In_ ULONG NumberOfPtes,
    _In_ MMSYSTEM_PTE_POOL_TYPE PoolType
);

PMMPTE
NTAPI
MiReserveSystemPtes(
    _In_ ULONG NumberOfPtes,
    _In_ MMSYSTEM_PTE_POOL_TYPE SystemPtePoolType
);

PMMPTE
NTAPI
MiReserveAlignedSystemPtes(
    _In_ ULONG NumberOfPtes,
    _In_ MMSYSTEM_PTE_POOL_TYPE SystemPtePoolType,
    _In_ ULONG Alignment
);

/* ARM3\virtual.c */
PFN_COUNT
NTAPI
MiDeleteSystemPageableVm(
    _In_ PMMPTE PointerPte,
    _In_ PFN_NUMBER PageCount,
    _In_ ULONG Flags,
    _Out_ PPFN_NUMBER ValidPages
);

/* balance.c */
INIT_FUNCTION
VOID
NTAPI
MmInitializeBalancer(
    ULONG NrAvailablePages,
    ULONG NrSystemPages
);

BOOLEAN
MmRosNotifyAvailablePage(
    PFN_NUMBER Page
);

VOID
NTAPI
MmRebalanceMemoryConsumers(
    VOID
);

/* EOF */
