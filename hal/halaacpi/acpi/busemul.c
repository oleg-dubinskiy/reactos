
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include "apic.h"

/* GLOBALS ********************************************************************/

extern ULONG HalpPicVectorRedirect[HAL_PIC_VECTORS];
extern BUS_HANDLER HalpFakePciBusHandler;
extern BOOLEAN HalpPCIConfigInitialized;
extern ULONG HalpMinPciBus;
extern ULONG HalpMaxPciBus;

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

ULONG
NTAPI
HalGetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    BUS_HANDLER BusHandler;

    /* Look as the bus type */
    if (BusDataType == Cmos)
        return HalpGetCmosData(0, SlotNumber, Buffer, Length);

    if (BusDataType == EisaConfiguration)
    {
        /* FIXME: TODO */
        ASSERT(FALSE);
        return 0;
    }

    if (BusDataType != PCIConfiguration)
        /* Invalid bus */
        return 0;

    if (!HalpPCIConfigInitialized)
        /* Invalid bus */
        return 0;

    if ((BusNumber < HalpMinPciBus) || (BusNumber > HalpMaxPciBus))
        /* Invalid bus */
        return 0;


    /* Setup fake PCI Bus handler */
    RtlCopyMemory(&BusHandler, &HalpFakePciBusHandler, sizeof(BUS_HANDLER));
    BusHandler.BusNumber = BusNumber;

    /* Call PCI function */
    return HalpGetPCIData(&BusHandler, &BusHandler, SlotNumber, Buffer, Offset, Length);
}

ULONG
NTAPI
HalGetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ PVOID Buffer,
    _In_ ULONG Length)
{
    /* Call the extended function */
    return HalGetBusDataByOffset(BusDataType,
                                 BusNumber,
                                 SlotNumber,
                                 Buffer,
                                 0,
                                 Length);
}

ULONG
NTAPI
HalSetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    BUS_HANDLER BusHandler;

    /* Look as the bus type */
    if (BusDataType == Cmos)
        return HalpSetCmosData(0, SlotNumber, Buffer, Length);

    if (BusDataType != PCIConfiguration)
        return 0;

    if (!HalpPCIConfigInitialized)
        return 0;

    /* Setup fake PCI Bus handler */
    RtlCopyMemory(&BusHandler, &HalpFakePciBusHandler, sizeof(BUS_HANDLER));
    BusHandler.BusNumber = BusNumber;

    /* Call PCI function */
    return HalpSetPCIData(&BusHandler, &BusHandler, SlotNumber, Buffer, Offset, Length);
}

ULONG
NTAPI
HalSetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ PVOID Buffer,
    _In_ ULONG Length)
{
    /* Call the extended function */
    return HalSetBusDataByOffset(BusDataType,
                                 BusNumber,
                                 SlotNumber,
                                 Buffer,
                                 0,
                                 Length);
}

NTSTATUS
NTAPI
HalAdjustResourceList(
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST* pRequirementsList)
{
    /* Deprecated, return success */
    return STATUS_SUCCESS;
}

/* EOF */
