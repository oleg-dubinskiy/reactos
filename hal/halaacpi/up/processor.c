
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* PRIVATE FUNCTIONS *********************************************************/


/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    return TRUE;
}

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return FALSE;
}

VOID
NTAPI
HalProcessorIdle(VOID)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
}

VOID
NTAPI
HalRequestIpi(
    _In_ KAFFINITY TargetProcessors)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
}

/* EOF */
