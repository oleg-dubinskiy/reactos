
#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include <ndk/inbvfuncs.h>

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
HalDisplayString(
    _In_ PCH String)
{
    /* Call the Inbv driver */
    InbvDisplayString(String);
}

VOID
NTAPI
HalAcquireDisplayOwnership(
    _In_ PHAL_RESET_DISPLAY_PARAMETERS ResetDisplayParameters)
{
    /* Stub since Windows XP implemented Inbv */
    return;
}

VOID
NTAPI
HalQueryDisplayParameters(
    _Out_ PULONG DispSizeX,
    _Out_ PULONG DispSizeY,
    _Out_ PULONG CursorPosX,
    _Out_ PULONG CursorPosY)
{
    /* Stub since Windows XP implemented Inbv */
    return;
}

VOID
NTAPI
HalSetDisplayParameters(
    _In_ ULONG CursorPosX,
    _In_ ULONG CursorPosY)
{
    /* Stub since Windows XP implemented Inbv */
    return;
}

/* EOF */
