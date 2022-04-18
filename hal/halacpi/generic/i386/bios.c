/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* V86 OPCODE HANDLERS ********************************************************/


/* V86 TRAP HANDLERS **********************************************************/


/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
HalpBiosDisplayReset(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return FALSE;
}

/* EOF */
