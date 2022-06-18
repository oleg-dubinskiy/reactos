
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include "apic.h"

/* GLOBALS ********************************************************************/

extern ULONG HalpPicVectorRedirect[HAL_PIC_VECTORS];
extern BUS_HANDLER HalpFakePciBusHandler;

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
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
HalpFindBusAddressTranslation(
    _In_ PHYSICAL_ADDRESS BusAddress,
    _In_ OUT PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress,
    _In_ OUT PULONG_PTR Context,
    _In_ BOOLEAN NextBus)
{
    /* Make sure we have a context */
    if (!Context)
        return FALSE;

    /* If we have data in the context, then this shouldn't be a new lookup */
    if (*Context && NextBus)
        return FALSE;

    /* Return bus data */
    TranslatedAddress->QuadPart = BusAddress.QuadPart;

    /* Set context value and return success */
    *Context = 1;
    return TRUE;
}

/* PUBLIC FUNCTIONS **********************************************************/

ULONG
NTAPI
HalGetInterruptVector(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG BusInterruptLevel,
    _In_ ULONG BusInterruptVector,
    _Out_ PKIRQL OutIrql,
    _Out_ PKAFFINITY OutAffinity)
{
    BUS_HANDLER BusHandler;
    ULONG SystemVector;
    ULONG Vector;
    ULONG Level;

    if (InterfaceType == Isa)
    {
        if (BusInterruptVector >= HAL_PIC_VECTORS)
        {
            DPRINT1("HalGetInterruptVector: BusInterruptVector %X\n", BusInterruptVector);
            ASSERT(BusInterruptVector < HAL_PIC_VECTORS); // DbgBreakPoint();
        }

        Vector = HalpPicVectorRedirect[BusInterruptVector];
        Level = HalpPicVectorRedirect[BusInterruptLevel];
    }
    else
    {
        Vector = BusInterruptVector;
        Level = BusInterruptLevel;
    }

    RtlCopyMemory(&BusHandler, &HalpFakePciBusHandler, sizeof(BusHandler));

    BusHandler.BusNumber = BusNumber;
    BusHandler.InterfaceType = InterfaceType;
    BusHandler.ParentHandler = &BusHandler;

    SystemVector = HalpGetSystemInterruptVector(&BusHandler,
                                                &BusHandler,
                                                Level,
                                                Vector,
                                                OutIrql,
                                                OutAffinity);
    return SystemVector;
}

BOOLEAN
NTAPI
HalTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _In_ OUT PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    /* Look as the bus type */
    if (InterfaceType == PCIBus)
    {
        /* Call the PCI registered function */
        return HalPciTranslateBusAddress(PCIBus,
                                         BusNumber,
                                         BusAddress,
                                         AddressSpace,
                                         TranslatedAddress);
    }
    else
    {
        /* Translation is easy */
        TranslatedAddress->QuadPart = BusAddress.QuadPart;
        return TRUE;
    }
}

/* EOF */
