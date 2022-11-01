#pragma once

/* Make the code cleaner with some definitions for size multiples */
#define _1KB (1024u)
#define _1MB (1024 * _1KB)
#define _1GB (1024ull * _1MB)
#define _1TB (1024ull * _1GB)

#define CC_DEFAULT_NUMBER_OF_VACBS    4
#define CC_VACBS_DEFAULT_MAPPING_SIZE (CC_DEFAULT_NUMBER_OF_VACBS * VACB_MAPPING_GRANULARITY)
#define BCB_MAPPING_GRANULARITY       (2 * VACB_MAPPING_GRANULARITY)
#define CACHE_OVERALL_SIZE            (32 * _1MB) // VACB_SIZE_OF_FIRST_LEVEL (win)

#define VACB_NUMBER_OF_LEVELS         7
#define VACB_LAST_INDEX_FOR_LEVEL     (0x80 - 1)
#define VACB_LEVEL_BLOCK_SIZE         ((VACB_LAST_INDEX_FOR_LEVEL + 1) * sizeof(PVOID))
#define VACB_LEVEL_SHIFT              7
#define VACB_SPECIAL_DEREFERENCE      (PVACB)(-2)

#define MBCB_BITMAP_RANGE             0x1000000 // 16 Mb (32 * 512 Kb)

#define READAHEAD_DISABLED    0x1
#define WRITEBEHIND_DISABLED  0x2

typedef union _CC_BCB
{
    struct
    {
        USHORT NodeTypeCode;
        UCHAR Reserved1[2]; // FIXME
        ULONG Length;
        LARGE_INTEGER FileOffset;
        LIST_ENTRY Link;
        LARGE_INTEGER BeyondLastByte;
        UINT64 Reserved3;
        UINT64 Reserved4;
        PVACB Vacb;
        ULONG PinCount;
        ERESOURCE BcbResource;
        PSHARED_CACHE_MAP SharedCacheMap;
        PVOID BaseAddress;
    };
    struct _MBCB Mbcb;
} CC_BCB, *PCC_BCB;

typedef struct _SHARED_CACHE_MAP_LIST_CURSOR
{
    LIST_ENTRY SharedCacheMapLinks;
    ULONG Flags;
} SHARED_CACHE_MAP_LIST_CURSOR, *PSHARED_CACHE_MAP_LIST_CURSOR;

typedef struct _LAZY_WRITER
{
    LIST_ENTRY WorkQueue;
    KDPC ScanDpc;
    KTIMER ScanTimer;
    BOOLEAN ScanActive;
    BOOLEAN OtherWork;
    BOOLEAN PendingTeardown;
    UCHAR Pad[0x5];
} LAZY_WRITER, *PLAZY_WRITER;

typedef struct _WORK_QUEUE_ENTRY
{
    LIST_ENTRY WorkQueueLinks;
    union
    {
        struct
        {
            PFILE_OBJECT FileObject;
        } Read;
        struct
        {
            PSHARED_CACHE_MAP SharedCacheMap;
        } Write;
        struct
        {
            PKEVENT Event;
        } Event;
        struct
        {
            ULONG Reason;
        } Notification;
    } Parameters;
    UCHAR Function;
} WORK_QUEUE_ENTRY, *PWORK_QUEUE_ENTRY;

typedef enum _WORK_QUEUE_FUNCTIONS
{
    ReadAhead = 1,
    WriteBehind = 2,
    LazyWriteScan = 3,
    SetDone = 4,
} WORK_QUEUE_FUNCTIONS, *PWORK_QUEUE_FUNCTIONS;

typedef struct _CC_VACB_REFERENCE
{
    LONG Reference;
    LONG SpecialReference;
} CC_VACB_REFERENCE, *PCC_VACB_REFERENCE;

VOID
NTAPI
CcInitializeVacbs(
    VOID
);

NTSTATUS
NTAPI
CcCreateVacbArray(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LARGE_INTEGER AllocationSize
);

PVOID
NTAPI
CcGetVirtualAddress(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LARGE_INTEGER FileOffset,
    _Out_ PVACB* OutVacb,
    _Out_ ULONG* OutReceivedLength
);

PVOID
NTAPI
CcGetVirtualAddressIfMapped(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LONGLONG FileOffset,
    _Out_ PVACB* OutVacb,
    _Out_ ULONG* OutLength
);

VOID
NTAPI
CcFreeVirtualAddress(
    _In_ PVACB Vacb
);

VOID
NTAPI
CcGetActiveVacb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _Out_ PVACB* OutVacb,
    _Out_ ULONG* OutActivePage,
    _Out_ BOOLEAN* OutIsVacbLocked
);

VOID
NTAPI
CcSetActiveVacb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _Inout_ PVACB* OutVacb,
    _In_ ULONG ActivePage,
    _In_ BOOLEAN IsVacbLocked
);

VOID
FASTCALL
CcPostWorkQueue(
    _In_ PWORK_QUEUE_ENTRY WorkItem,
    _In_ PLIST_ENTRY WorkQueue
);

VOID
NTAPI
CcScheduleLazyWriteScan(
    _In_ BOOLEAN NoDelay
);

VOID
NTAPI
CcFreeActiveVacb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PVACB Vacb,
    _In_ ULONG ActivePage,
    _In_ BOOLEAN IsVacbLocked
);

VOID
NTAPI
CcScanDpc(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2
);

VOID
NTAPI
CcWorkerThread(
    _In_ PVOID Parameter
);

BOOLEAN
NTAPI
CcUnmapVacbArray(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN FrontOfList
);

VOID
NTAPI
CcScheduleLazyWriteScanEx(
    _In_ BOOLEAN NoDelay,
    _In_ BOOLEAN PendingTeardown
);

NTSTATUS
NTAPI
CcExtendVacbArray(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LARGE_INTEGER AllocationSize
);

VOID
NTAPI
CcDeleteSharedCacheMap(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ KIRQL OldIrql,
    _In_ BOOLEAN IsReleaseFile
);

PLIST_ENTRY
NTAPI
CcGetBcbListHead(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LONGLONG FileOffset,
    _In_ BOOLEAN Flag3
);

BOOLEAN
NTAPI
CcFindBcb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER EndFileOffset,
    _Out_ PCC_BCB* OutBcb
);

PCC_BCB
NTAPI
CcAllocateInitializeBcb(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PCC_BCB PreviousBcb,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER Length
);

BOOLEAN
NTAPI
CcMapAndRead(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _In_ BOOLEAN Flag5,
    _In_ PVOID BaseAddress
);

VOID
NTAPI
CcDeallocateBcb(
    _In_ PCC_BCB Bcb
);

VOID
NTAPI
CcUnpinFileDataEx(
    _In_ PCC_BCB Bcb,
    _In_ BOOLEAN IsNoWrite,
    _In_ ULONG Type
);

VOID
NTAPI
CcPostDeferredWrites(
    VOID
);

VOID
NTAPI
CcSetDirtyInMask(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length
);

BOOLEAN
NTAPI
CcPrefillVacbLevelZone(
    _In_ ULONG ToLevel,
    _Out_ PKLOCK_QUEUE_HANDLE OutLockHandle,
    _In_ BOOLEAN IsModifiedNoWrite,
    _In_ BOOLEAN LockMode,
    _In_ PSHARED_CACHE_MAP SharedMap
);

PVOID*
NTAPI
CcAllocateVacbLevel(
    _In_ BOOLEAN WithBcbs
);

VOID
NTAPI
CcDeallocateVacbLevel(
    _In_ PVOID* Entry,
    _In_ BOOLEAN WithBcbs
);

VOID
NTAPI
CcDrainVacbLevelZone(
    VOID
);

ULONG
NTAPI
IsVacbLevelReferenced(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ PVACB* Vacbs,
    _In_ ULONG Level
);

VOID
NTAPI
CcAdjustVacbLevelLockCount(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LONGLONG FileOffset,
    _In_ LONG AdjustLevel
);

BOOLEAN
NTAPI
CcPinFileData(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN IsNoWrite,
    _In_ BOOLEAN Flag5,
    _In_ ULONG PinFlags,
    _Out_ PCC_BCB* OutBcb,
    _Out_ PVOID* OutBuffer,
    _Out_ LARGE_INTEGER* OutBeyondLastByte
);

/* EOF */
