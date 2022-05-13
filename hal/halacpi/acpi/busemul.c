
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG HalpPicVectorRedirect[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

extern ULONG HalpMinPciBus;
extern ULONG HalpMaxPciBus;

extern BUS_HANDLER HalpFakePciBusHandler;
extern BOOLEAN HalpPCIConfigInitialized;

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI 
HalacpiIrqTranslateResourcesIsa(
     _Inout_opt_ PVOID Context,
     _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Source,
     _In_ RESOURCE_TRANSLATION_DIRECTION Direction,
     _In_opt_ ULONG AlternativesCount,
     _In_opt_ IO_RESOURCE_DESCRIPTOR Alternatives[],
     _In_ PDEVICE_OBJECT PhysicalDeviceObject,
     _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Target)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

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
    PAGED_CODE();

    ASSERT(Version == 0);
    ASSERT(Size >= sizeof(TRANSLATOR_INTERFACE));

    if (BridgeInterfaceType != -1 &&
        BridgeInterfaceType != 1 &&
        BridgeInterfaceType != 2)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    RtlZeroMemory(Translator, sizeof(TRANSLATOR_INTERFACE));

    Translator->Size = sizeof(TRANSLATOR_INTERFACE);
    Translator->Version = 0;

    Translator->InterfaceReference = HalTranslatorDereference;
    Translator->InterfaceDereference = HalTranslatorDereference;

    Translator->TranslateResources = HalacpiIrqTranslateResourcesIsa;
    Translator->TranslateResourceRequirements = HalacpiIrqTranslateResourceRequirementsIsa;

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
HalpTranslateBusAddress(IN INTERFACE_TYPE InterfaceType,
                        IN ULONG BusNumber,
                        IN PHYSICAL_ADDRESS BusAddress,
                        IN OUT PULONG AddressSpace,
                        OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    DPRINT1("HalpTranslateBusAddress: ... \n");
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return FALSE;
}

NTSTATUS
NTAPI
HalpAssignSlotResources(IN PUNICODE_STRING RegistryPath,
                        IN PUNICODE_STRING DriverClassName,
                        IN PDRIVER_OBJECT DriverObject,
                        IN PDEVICE_OBJECT DeviceObject,
                        IN INTERFACE_TYPE BusType,
                        IN ULONG BusNumber,
                        IN ULONG SlotNumber,
                        IN OUT PCM_RESOURCE_LIST * AllocatedResources)
{
    DPRINT1("HalpAssignSlotResources: ... \n");
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
HalpFindBusAddressTranslation(IN PHYSICAL_ADDRESS BusAddress,
                              IN OUT PULONG AddressSpace,
                              OUT PPHYSICAL_ADDRESS TranslatedAddress,
                              IN OUT PULONG_PTR Context,
                              IN BOOLEAN NextBus)
{
    /* Make sure we have a context */
    if (!Context)
        return FALSE;

    /* If we have data in the context, then this shouldn't be a new lookup */
    if ((*Context != 0) && (NextBus != FALSE))
        return FALSE;

    /* Return bus data */
    TranslatedAddress->QuadPart = BusAddress.QuadPart;

    /* Set context value and return success */
    *Context = 1;
    return TRUE;
}


/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
HalAdjustResourceList(IN OUT PIO_RESOURCE_REQUIREMENTS_LIST * pRequirementsList)
{
    /* Deprecated, return success */
    return STATUS_SUCCESS;
}

UCHAR
FASTCALL
HalSystemVectorDispatchEntry(IN ULONG Vector,
                             OUT PKINTERRUPT_ROUTINE **FlatDispatch,
                             OUT PKINTERRUPT_ROUTINE *NoConnection)
{
    /* Not implemented on x86 */
    return 0;
}

ULONG
NTAPI
HalGetInterruptVector(IN INTERFACE_TYPE InterfaceType,
                      IN ULONG BusNumber,
                      IN ULONG BusInterruptLevel,
                      IN ULONG BusInterruptVector,
                      OUT PKIRQL OutIrql,
                      OUT PKAFFINITY OutAffinity)
{
    ULONG Vector;
    ULONG Level;
    ULONG SystemVector;
    BUS_HANDLER BusHandler;

    if (InterfaceType == Isa)
    {
        if (BusInterruptVector >= 0x10)
        {
            DPRINT1("HalGetInterruptVector: BusInterruptVector %X\n", BusInterruptVector);
            ASSERT(BusInterruptVector < 0x10); // HalpDbgBreakPointEx();
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

    DPRINT("HalGetInterruptVector: Level %X, Vector %X, SystemVector %X\n", Level, Vector, SystemVector);

    return SystemVector;
}

BOOLEAN
NTAPI
HalTranslateBusAddress(IN INTERFACE_TYPE InterfaceType,
                       IN ULONG BusNumber,
                       IN PHYSICAL_ADDRESS BusAddress,
                       IN OUT PULONG AddressSpace,
                       OUT PPHYSICAL_ADDRESS TranslatedAddress)
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
HalGetBusDataByOffset(IN BUS_DATA_TYPE BusDataType,
                      IN ULONG BusNumber,
                      IN ULONG SlotNumber,
                      IN PVOID Buffer,
                      IN ULONG Offset,
                      IN ULONG Length)
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
HalGetBusData(IN BUS_DATA_TYPE BusDataType,
              IN ULONG BusNumber,
              IN ULONG SlotNumber,
              IN PVOID Buffer,
              IN ULONG Length)
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
HalSetBusDataByOffset(IN BUS_DATA_TYPE BusDataType,
                      IN ULONG BusNumber,
                      IN ULONG SlotNumber,
                      IN PVOID Buffer,
                      IN ULONG Offset,
                      IN ULONG Length)
{
    BUS_HANDLER BusHandler;

    /* Look as the bus type */
    if (BusDataType == Cmos)
        /* Call CMOS Function */
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
HalSetBusData(IN BUS_DATA_TYPE BusDataType,
              IN ULONG BusNumber,
              IN ULONG SlotNumber,
              IN PVOID Buffer,
              IN ULONG Length)
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
HalAssignSlotResources(IN PUNICODE_STRING RegistryPath,
                       IN PUNICODE_STRING DriverClassName,
                       IN PDRIVER_OBJECT DriverObject,
                       IN PDEVICE_OBJECT DeviceObject,
                       IN INTERFACE_TYPE BusType,
                       IN ULONG BusNumber,
                       IN ULONG SlotNumber,
                       IN OUT PCM_RESOURCE_LIST * AllocatedResources)
{
    /* Check the bus type */
    if (BusType != PCIBus)
    {
        /* Call our internal handler */
        return HalpAssignSlotResources(RegistryPath,
                                       DriverClassName,
                                       DriverObject,
                                       DeviceObject,
                                       BusType,
                                       BusNumber,
                                       SlotNumber,
                                       AllocatedResources);
    }
    else
    {
        /* Call the PCI registered function */
        return HalPciAssignSlotResources(RegistryPath,
                                         DriverClassName,
                                         DriverObject,
                                         DeviceObject,
                                         PCIBus,
                                         BusNumber,
                                         SlotNumber,
                                         AllocatedResources);
    }
}

/* EOF */
