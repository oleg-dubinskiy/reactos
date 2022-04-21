
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/

ULONG HalpFeatureBits;

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
HalpRemoveFences()
{
    DPRINT1("HalpRemoveFences FIXME!\n");
    ASSERT(FALSE);//HalpDbgBreakPointEx();
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    DPRINT1("HalAllProcessorsStarted: HalpFeatureBits %X\n", HalpFeatureBits);

    if (HalpFeatureBits & 2) {
        HalpRemoveFences();
    }

    return TRUE;
}

BOOLEAN
NTAPI
HalStartNextProcessor(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                      IN PKPROCESSOR_STATE ProcessorState)
{
    /* Ready to start */
    return FALSE;
}

VOID
NTAPI
HalProcessorIdle(VOID)
{
    /* Enable interrupts and halt the processor */
    _enable();
    __halt();
}

VOID
NTAPI
HalRequestIpi(KAFFINITY TargetProcessors)
{
    /* Not implemented on UP */
    __debugbreak();
}

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    return;
}

/* EOF */
