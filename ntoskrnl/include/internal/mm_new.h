
#pragma once

#include <internal/arch/mm.h>

/* TYPES *********************************************************************/

typedef ULONG_PTR SWAPENTRY;

extern PFN_COUNT MmNumberOfPhysicalPages;

/* Although Microsoft says this isn't hardcoded anymore, they won't be able to change it.
   Stuff depends on it
*/
#define MM_VIRTMEM_GRANULARITY              (64 * 1024)

#define MC_CACHE                            (0)
#define MC_USER                             (1)
#define MC_SYSTEM                           (2)
#define MC_MAXIMUM                          (3)

#define PAGED_POOL_MASK                     1
#define MUST_SUCCEED_POOL_MASK              2
#define CACHE_ALIGNED_POOL_MASK             4
#define QUOTA_POOL_MASK                     8
#define SESSION_POOL_MASK                   32
#define VERIFIER_POOL_MASK                  64

typedef struct _MMPFNENTRY
{
    USHORT Modified:1;
    USHORT ReadInProgress:1;                // StartOfAllocation
    USHORT WriteInProgress:1;               // EndOfAllocation
    USHORT PrototypePte:1;
    USHORT PageColor:4;
    USHORT PageLocation:3;
    USHORT RemovalRequested:1;
    USHORT CacheAttribute:2;
    USHORT Rom:1;
    USHORT ParityError:1;
} MMPFNENTRY;

/* Mm internal */

#if defined(_X86PAE_)
  #pragma pack(1)
#endif

typedef struct _MMPFN
{
    union
    {
        PFN_NUMBER Flink;
        ULONG WsIndex;
        PKEVENT Event;
        NTSTATUS ReadStatus;
        SINGLE_LIST_ENTRY NextStackPfn;
    } u1;
    PMMPTE PteAddress;
    union
    {
        PFN_NUMBER Blink;
        ULONG_PTR ShareCount;
    } u2;
    union
    {
        struct
        {
            USHORT ReferenceCount;
            MMPFNENTRY e1;
        };
        struct
        {
            USHORT ReferenceCount;
            USHORT ShortFlags;
        } e2;
    } u3;
    union
    {
        MMPTE OriginalPte;
        struct
        {
            LONG AweReferenceCount;
          #if defined(_X86PAE_)
            ULONG Pad0;
          #endif
        };
    };
    union
    {
        ULONG_PTR EntireFrame;
        struct
        {
            ULONG_PTR PteFrame:25;
            ULONG_PTR InPageError:1;
            ULONG_PTR VerifierAllocation:1;
            ULONG_PTR AweAllocation:1;
            ULONG_PTR Priority:3;
            ULONG_PTR MustBeCached:1;
        };
    } u4;
} MMPFN, *PMMPFN;

#if defined(_X86PAE_)
  #pragma pack()
#endif

typedef struct _MMPFNLIST
{
    PFN_NUMBER Total;
    MMLISTS ListName;
    PFN_NUMBER Flink;
    PFN_NUMBER Blink;
} MMPFNLIST, *PMMPFNLIST;

typedef struct _MM_MEMORY_CONSUMER
{
    ULONG PagesUsed;
    ULONG PagesTarget;
    NTSTATUS (*Trim)(ULONG Target, ULONG Priority, PULONG NrFreed);
} MM_MEMORY_CONSUMER, *PMM_MEMORY_CONSUMER;

/* Paged pool information */
typedef struct _MM_PAGED_POOL_INFO
{
    PRTL_BITMAP PagedPoolAllocationMap;
    PRTL_BITMAP EndOfPagedPoolBitmap;
    PMMPTE FirstPteForPagedPool;
    PMMPTE LastPteForPagedPool;
    PMMPDE NextPdeForPagedPoolExpansion;
    ULONG PagedPoolHint;
    SIZE_T PagedPoolCommit;
    SIZE_T AllocatedPagedPool;
} MM_PAGED_POOL_INFO, *PMM_PAGED_POOL_INFO;

/* FUNCTIONS *****************************************************************/

/* ARM3\contmem.c */
/* ARM3\drvmgmt.c */
/* ARM3\dynamic.c */

/* ARM3\expool.c */
VOID
NTAPI
ExpCheckPoolAllocation(
    PVOID P,
    POOL_TYPE PoolType,
    ULONG Tag
);

VOID
NTAPI
ExReturnPoolQuota(
    _In_ PVOID P
);

/* ARM3\hypermap.c */
/* ARM3\iosup.c */
/* ARM3\kdbg.c */
/* ARM3\largepag.c */
/* ARM3\mdlsup.c */
/* ARM3\miarm.h */

/* ARM3\mmdbg.c */

/* Maximum chunk size per copy */
#define MMDBG_COPY_MAX_SIZE         0x8

/* MmDbgCopyMemory Flags */
#define MMDBG_COPY_WRITE            0x00000001
#define MMDBG_COPY_PHYSICAL         0x00000002
#define MMDBG_COPY_UNSAFE           0x00000004
#define MMDBG_COPY_CACHED           0x00000008
#define MMDBG_COPY_UNCACHED         0x00000010
#define MMDBG_COPY_WRITE_COMBINED   0x00000020

/* Mm copy support for Kd */
NTSTATUS
NTAPI
MmDbgCopyMemory(
    _In_ ULONG64 Address,
    _In_ PVOID Buffer,
    _In_ ULONG Size,
    _In_ ULONG Flags
);

/* ARM3\mminit.c */

/* ARM3\mmsup.c */
NTSTATUS
NTAPI
MmAdjustWorkingSetSize(
    _In_ SIZE_T WorkingSetMinimumInBytes,
    _In_ SIZE_T WorkingSetMaximumInBytes,
    _In_ ULONG SystemCache,
    _In_ BOOLEAN IncreaseOkay
);

/* ARM3\ncache.c */

/* ARM3\pagfault.c */
NTSTATUS
NTAPI
MmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation
);

NTSTATUS
NTAPI
MmSetExecuteOptions(
    _In_ ULONG ExecuteOptions
);

NTSTATUS
NTAPI
MmGetExecuteOptions(
    _In_ PULONG ExecuteOptions
);

/* ARM3\pfnlist.c */
/* ARM3\pool.c */

/* ARM3\procsup.c */
INIT_FUNCTION
NTSTATUS
NTAPI
MmInitializeHandBuiltProcess(
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase
);

INIT_FUNCTION
NTSTATUS
NTAPI
MmInitializeHandBuiltProcess2(
    _In_ PEPROCESS Process
);

BOOLEAN
NTAPI
MmCreateProcessAddressSpace(
    _In_ ULONG MinWs,
    _In_ PEPROCESS Dest,
    _In_ PULONG_PTR DirectoryTableBase
);

NTSTATUS
NTAPI
MmInitializeProcessAddressSpace(
    _In_ PEPROCESS Process,
    _In_ PEPROCESS Clone OPTIONAL,
    _In_ PVOID Section OPTIONAL,
    _Inout_ PULONG Flags,
    _In_ POBJECT_NAME_INFORMATION* AuditName OPTIONAL
);

NTSTATUS
NTAPI
MmCreatePeb(
    _In_ PEPROCESS Process,
    _In_ PINITIAL_PEB InitialPeb,
    _Out_ PPEB* BasePeb
);

NTSTATUS
NTAPI
MmCreateTeb(
    _In_ PEPROCESS Process,
    _In_ PCLIENT_ID ClientId,
    _In_ PINITIAL_TEB InitialTeb,
    _Out_ PTEB* BaseTeb
);

VOID
NTAPI
MmDeleteTeb(
    struct _EPROCESS* Process,
    PTEB Teb
);

VOID
NTAPI
MmCleanProcessAddressSpace(
    _In_ PEPROCESS Process
);

NTSTATUS
NTAPI
MmDeleteProcessAddressSpace(
    _In_ PEPROCESS Process
);

NTSTATUS
NTAPI
MmSetMemoryPriorityProcess(
    _In_ PEPROCESS Process,
    _In_ UCHAR MemoryPriority
);

PVOID
NTAPI
MmCreateKernelStack(
    BOOLEAN GuiStack,
    UCHAR Node
);

VOID
NTAPI
MmDeleteKernelStack(
    PVOID Stack,
    BOOLEAN GuiStack
);

NTSTATUS
NTAPI
MmGrowKernelStack(
    _In_ PVOID StackPointer
);

/* ARM3\section.c */
VOID
NTAPI
MmGetImageInformation(
    _Out_ PSECTION_IMAGE_INFORMATION ImageInformation
);

PFILE_OBJECT
NTAPI
MmGetFileObjectForSection(
    _In_ PVOID Section
);

NTSTATUS
NTAPI
MmGetFileNameForAddress(
    _In_ PVOID Address,
    _Out_ PUNICODE_STRING ModuleName
);

NTSTATUS
NTAPI
MmGetFileNameForSection(
    _In_ PVOID Section,
    _Out_ POBJECT_NAME_INFORMATION* ModuleName
);

/* ARM3\session.c */
ULONG
NTAPI
MmGetSessionLocaleId(
    VOID
);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
MmAttachSession(
    _Inout_ PVOID SessionEntry,
    _Out_ PKAPC_STATE ApcState
);

#ifdef MM_NEW
  _IRQL_requires_max_(APC_LEVEL)
  NTSTATUS
  NTAPI
  MmDetachSession(
      _Inout_ PVOID SessionEntry,
      _Out_ PKAPC_STATE ApcState
  );
#else
  _IRQL_requires_max_(APC_LEVEL)
  VOID
  NTAPI
  MmDetachSession(
      _Inout_ PVOID SessionEntry,
      _Out_ PKAPC_STATE ApcState
  );
#endif

VOID
NTAPI
MmQuitNextSession(
    _Inout_ PVOID SessionEntry
);

PVOID
NTAPI
MmGetSessionById(
    _In_ ULONG SessionId
);

_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
MmSetSessionLocaleId(
    _In_ LCID LocaleId
);

/* Determines if a given address is a session address */
BOOLEAN
NTAPI
MmIsSessionAddress(
    _In_ PVOID Address
);

ULONG
NTAPI
MmGetSessionId(
    _In_ PEPROCESS Process
);

ULONG
NTAPI
MmGetSessionIdEx(
    _In_ PEPROCESS Process
);

/* ARM3\special.c */
BOOLEAN
NTAPI
MmIsSpecialPoolAddress(
    _In_ PVOID P
);

BOOLEAN
NTAPI
MmIsSpecialPoolAddressFree(
    _In_ PVOID P
);

/* ARM3\sysldr.c */
BOOLEAN
NTAPI
MmChangeKernelResourceSectionProtection(
    _In_ ULONG_PTR ProtectionMask
);

VOID
NTAPI
MmMakeKernelResourceSectionWritable(
    VOID
);

NTSTATUS
NTAPI
MmLoadSystemImage(
    _In_ PUNICODE_STRING FileName,
    _In_ PUNICODE_STRING NamePrefix OPTIONAL,
    _In_ PUNICODE_STRING LoadedName OPTIONAL,
    _In_ ULONG Flags,
    _Out_ PVOID* ModuleObject,
    _Out_ PVOID* ImageBaseAddress
);

NTSTATUS
NTAPI
MmUnloadSystemImage(
    _In_ PVOID ImageHandle
);

NTSTATUS
NTAPI
MmCheckSystemImage(
    _In_ HANDLE ImageHandle,
    _In_ BOOLEAN PurgeSection
);

NTSTATUS
NTAPI
MmCallDllInitialize(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PLIST_ENTRY ListHead
);

/* ARM3\syspte.c */
/* ARM3\vadnode.c */

/* ARM3\virtual.c */
NTSTATUS
NTAPI
MmCopyVirtualMemory(
    _In_ PEPROCESS SourceProcess,
    _In_ PVOID SourceAddress,
    _In_ PEPROCESS TargetProcess,
    _Out_ PVOID TargetAddress,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PSIZE_T ReturnSize
);

/* ARM3\zeropage.c */
VOID
NTAPI
MmZeroPageThread(
    VOID
);

/* ARM3\mmdbg.c */

/* i386\page.c */
/* i386\pagepae.c */
ULONG
NTAPI
MmGetPageProtect(
    struct _EPROCESS* Process,
    PVOID Address
);

VOID
NTAPI
MmSetPageProtect(
    struct _EPROCESS* Process,
    PVOID Address,
    ULONG flProtect
);

/* balance.c */
/* freelist.c */

/* mminit.c */
INIT_FUNCTION
BOOLEAN
NTAPI
MmInitSystem(
    _In_ ULONG Phase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
);

VOID
NTAPI
MmDumpArmPfnDatabase(
   _In_ BOOLEAN StatusOnly
);


/* pagefile.c */
BOOLEAN
NTAPI
MmIsFileObjectAPagingFile(
    PFILE_OBJECT FileObject
);

/* shutdown.c */
VOID
MmShutdownSystem(
    _In_ ULONG Phase
);

/* EOF */
