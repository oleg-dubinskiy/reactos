
#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include <ndk/inbvfuncs.h>

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

VOID
NTAPI
HalAcquireDisplayOwnership(IN PHAL_RESET_DISPLAY_PARAMETERS ResetDisplayParameters)
{
    /* Stub since Windows XP implemented Inbv */
    return;
}

VOID
NTAPI
HalQueryDisplayParameters(OUT PULONG DispSizeX,
                          OUT PULONG DispSizeY,
                          OUT PULONG CursorPosX,
                          OUT PULONG CursorPosY)
{
    /* Stub since Windows XP implemented Inbv */
    return;
}

VOID
NTAPI
HalSetDisplayParameters(IN ULONG CursorPosX,
                        IN ULONG CursorPosY)
{
    /* Stub since Windows XP implemented Inbv */
    return;
}

/* EOF */
