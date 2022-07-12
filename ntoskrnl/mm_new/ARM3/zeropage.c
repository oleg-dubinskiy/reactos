
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
//#include "miarm.h"

/* GLOBALS ********************************************************************/

KEVENT MmZeroingPageEvent;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
MmZeroPageThread(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
