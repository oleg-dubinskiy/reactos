
#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include <ndk/inbvfuncs.h>

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
HalDisplayString(IN PCH String)
{
    /* Call the Inbv driver */
    InbvDisplayString(String);
}

/* EOF */
