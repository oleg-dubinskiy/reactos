#pragma once

/* Counters */
extern ULONG CcLazyWritePages;
extern ULONG CcLazyWriteIos;
extern ULONG CcMapDataWait;
extern ULONG CcMapDataNoWait;
extern ULONG CcPinReadWait;
extern ULONG CcPinReadNoWait;
extern ULONG CcPinMappedDataCount;
extern ULONG CcDataPages;
extern ULONG CcDataFlushes;

/* cachesub.c */
INIT_FUNCTION
VOID
NTAPI
CcPfInitializePrefetcher(
    VOID
);

/* copysup.c */

/* fssup.c */
INIT_FUNCTION
BOOLEAN
NTAPI
CcInitializeCacheManager(
    VOID
);

/* lazyrite.c */

/* logsup.c */

/* mdlsup.c */
VOID
NTAPI
CcMdlReadComplete2(
    IN PFILE_OBJECT FileObject,
    IN PMDL MemoryDescriptorList
);

VOID
NTAPI
CcMdlWriteComplete2(
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER FileOffset,
    IN PMDL MdlChain
);

/* pinsup.c */

/* vacbsup.c */


/* EOF */
