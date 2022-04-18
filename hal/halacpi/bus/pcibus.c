
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpSetupPciDeviceForDebugging)
  #pragma alloc_text(INIT, HalpReleasePciDeviceForDebugging)
#endif

/* GLOBALS *******************************************************************/


/* HAL PCI FOR DEBUGGING *****************************************************/

INIT_FUNCTION
NTSTATUS
NTAPI
HalpSetupPciDeviceForDebugging(IN PVOID LoaderBlock,
                               IN OUT PDEBUG_DEVICE_DESCRIPTOR PciDevice)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

INIT_FUNCTION
NTSTATUS
NTAPI
HalpReleasePciDeviceForDebugging(IN OUT PDEBUG_DEVICE_DESCRIPTOR PciDevice)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* PCI CONFIGURATION SPACE ***************************************************/


/* HAL PCI CALLBACKS *********************************************************/

ULONG
NTAPI
HaliPciInterfaceReadConfig(_In_ PVOID Context,
                           _In_ ULONG BusNumber,
                           _In_ ULONG SlotNumber,
                           _In_ PVOID Buffer,
                           _In_ ULONG Offset,
                           _In_ ULONG Length)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

ULONG
NTAPI
HaliPciInterfaceWriteConfig(_In_ PVOID Context,
                            _In_ ULONG BusNumber,
                            _In_ ULONG SlotNumber,
                            _In_ PVOID Buffer,
                            _In_ ULONG Offset,
                            _In_ ULONG Length)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

/* EOF */

