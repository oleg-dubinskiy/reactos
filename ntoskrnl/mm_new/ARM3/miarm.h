
#pragma once

/* TYPES *********************************************************************/

/* Make the code cleaner with some definitions for size multiples */
#define _1KB (1024u)
#define _1MB (1024 * _1KB)
#define _1GB (1024 * _1MB)

#if defined(_M_IX86) || defined(_M_ARM)
  /* PFN List Sentinel */
  #define LIST_HEAD 0xFFFFFFFF

  /* Because GCC cannot automatically downcast 0xFFFFFFFF to lesser-width bits,
     we need a manual definition suited to the number of bits _In_ the PteFrame.
     This is used as a LIST_HEAD for the colored list
  */
  #define COLORED_LIST_HEAD ((1 << 25) - 1) // 0x1FFFFFF
#elif defined(_M_AMD64)
  #define LIST_HEAD 0xFFFFFFFFFFFFFFFFLL
  #define COLORED_LIST_HEAD ((1ULL << 57) - 1) // 0x1FFFFFFFFFFFFFFLL
#else
  #error Define these please!
#endif

/* FIXFIX: These should go _In_ ex.h after the pool merge */

#ifdef _WIN64
  #define POOL_BLOCK_SIZE 16
#else
  #define POOL_BLOCK_SIZE  8
#endif

#define POOL_LISTS_PER_PAGE (PAGE_SIZE / POOL_BLOCK_SIZE)

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

extern PVOID MmPagedPoolStart;
extern PVOID MmNonPagedPoolEnd;
extern ULONG_PTR MmSubsectionBase;
extern ULONG MmSystemPageColor;

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

/* ARM3\i386\init.c */
INIT_FUNCTION
VOID
NTAPI
MiInitializeSessionSpaceLayout(
    VOID
);

/* mminit.c */
INIT_FUNCTION
PPHYSICAL_MEMORY_DESCRIPTOR
NTAPI
MmInitializeMemoryLimits(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PBOOLEAN IncludeType
);

/* EOF */
