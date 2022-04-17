
#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
HalDisplayString(IN PCH String)
{
#ifndef _MINIHAL_
    /* Call the Inbv driver */
    InbvDisplayString(String);
#endif
}

/* EOF */
