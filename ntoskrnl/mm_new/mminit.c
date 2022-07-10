
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
BOOLEAN
NTAPI
MmInitSystem(_In_ ULONG Phase,
             _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

/* EOF */
