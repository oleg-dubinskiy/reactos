
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/


/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
HalReturnToFirmware(IN FIRMWARE_REENTRY Action)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

/* EOF */
