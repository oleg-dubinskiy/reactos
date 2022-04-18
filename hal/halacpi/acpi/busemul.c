
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
HalacpiGetInterruptTranslator(
    _In_ INTERFACE_TYPE ParentInterfaceType,
    _In_ ULONG ParentBusNumber,
    _In_ INTERFACE_TYPE BridgeInterfaceType,
    _In_ USHORT Size,
    _In_ USHORT Version,
    _Out_ PTRANSLATOR_INTERFACE Translator,
    _Out_ PULONG BridgeBusNumber)
{
    DPRINT1("HalacpiGetInterruptTranslator: %X, %X\n", ParentInterfaceType, ParentBusNumber);
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

/* PUBLIC FUNCTIONS **********************************************************/


/* EOF */
