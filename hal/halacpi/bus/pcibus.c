
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpSetupPciDeviceForDebugging)
  #pragma alloc_text(INIT, HalpReleasePciDeviceForDebugging)
#endif

/* GLOBALS *******************************************************************/


/* HAL PCI CALLBACKS *********************************************************/

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

/* EOF */

