#pragma once

/* Make the code cleaner with some definitions for size multiples */
#define _1KB (1024u)
#define _1MB (1024 * _1KB)
#define _1GB (1024ull * _1MB)
#define _1TB (1024ull * _1GB)

#define CC_DEFAULT_NUMBER_OF_VACBS 4

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


/* EOF */
