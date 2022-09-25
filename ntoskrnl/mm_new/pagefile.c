
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "ARM3/miarm.h"

/* GLOBALS ********************************************************************/

PMMPAGING_FILE MmPagingFile[MAX_PAGING_FILES]; /* List of paging files, both used and free */
KGUARDED_MUTEX MmPageFileCreationLock;         /* Lock for examining the list of paging files */

ULONG MmNumberOfPagingFiles; /* Number of paging files */
PFN_COUNT MiFreeSwapPages;   /* Number of pages that are available for swapping */
PFN_COUNT MiUsedSwapPages;   /* Number of pages that have been allocated for swapping */
BOOLEAN MmZeroPageFile;

/* Number of pages that have been reserved for swapping but not yet allocated */
static PFN_COUNT MiReservedSwapPages;

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
MmIsFileObjectAPagingFile(PFILE_OBJECT FileObject)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

INIT_FUNCTION
VOID
NTAPI
MmInitPagingFile(VOID)
{
    ULONG ix;

    KeInitializeGuardedMutex(&MmPageFileCreationLock);

    MiFreeSwapPages = 0;
    MiUsedSwapPages = 0;
    MiReservedSwapPages = 0;

    for (ix = 0; ix < MAX_PAGING_FILES; ix++)
        MmPagingFile[ix] = NULL;

    MmNumberOfPagingFiles = 0;
}


/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtCreatePagingFile(_In_ PUNICODE_STRING FileName,
                   _In_ PLARGE_INTEGER MinimumSize,
                   _In_ PLARGE_INTEGER MaximumSize,
                   _In_ ULONG Reserved)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
