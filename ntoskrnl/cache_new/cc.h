#pragma once

/* Make the code cleaner with some definitions for size multiples */
#define _1KB (1024u)
#define _1MB (1024 * _1KB)
#define _1GB (1024ull * _1MB)
#define _1TB (1024ull * _1GB)

#define CC_DEFAULT_NUMBER_OF_VACBS 4
#define CACHE_OVERALL_SIZE        (32 * _1MB)

typedef union _CC_BCB
{
    struct
    {
        USHORT NodeTypeCode;
        UCHAR Reserved1[2];
        ULONG Length;
        LARGE_INTEGER FileOffset;
        LIST_ENTRY Link;
        LARGE_INTEGER BeyondLastByte;
        PVACB Vacb;
        ULONG PinCount;
        ERESOURCE BcbResource;
        PSHARED_CACHE_MAP SharedCacheMap;
        PVOID BaseAddress;
    };
    struct _MBCB Mbcb;
} CC_BCB, *PCC_BCB;

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

/* EOF */
