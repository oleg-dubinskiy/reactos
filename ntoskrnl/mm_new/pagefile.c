
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG MmNumberOfPagingFiles; /* Number of paging files */
PFN_COUNT MiFreeSwapPages;   /* Number of pages that are available for swapping */
PFN_COUNT MiUsedSwapPages;   /* Number of pages that have been allocated for swapping */
BOOLEAN MmZeroPageFile;

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
MmIsFileObjectAPagingFile(PFILE_OBJECT FileObject)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
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
