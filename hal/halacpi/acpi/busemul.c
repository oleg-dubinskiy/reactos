
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
    DPRINT1("HalpFindBusAddressTranslation: ... \n");
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return FALSE;
}

/* PUBLIC FUNCTIONS **********************************************************/


/* EOF */
