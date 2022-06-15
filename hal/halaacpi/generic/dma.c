
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/


/* FUNCTIONS *****************************************************************/

/* HaliGetDmaAdapter
     Internal routine to allocate PnP DMA adapter object.
     It's exported through HalDispatchTable and used by IoGetDmaAdapter.
 
   see HalGetAdapter
*/
PDMA_ADAPTER
NTAPI
HaliGetDmaAdapter(
    _In_ PVOID Context,
    _In_ PDEVICE_DESCRIPTION DeviceDescriptor,
    _Out_ PULONG NumberOfMapRegisters)
{
    return &HalGetAdapter(DeviceDescriptor, NumberOfMapRegisters)->DmaHeader;
}

/* PUBLIC FUNCTIONS **********************************************************/

/* HalGetAdapter
      Allocate an adapter object for DMA device.

   DeviceDescriptor
      Structure describing the attributes of the device.

   NumberOfMapRegisters
      On return filled with the maximum number of map registers
      the device driver can allocate for DMA transfer operations.

   return:
      The DMA adapter on success, NULL otherwise.
*/
PADAPTER_OBJECT
NTAPI
HalGetAdapter(
    _In_ PDEVICE_DESCRIPTION DeviceDescriptor,
    _Out_ PULONG NumberOfMapRegisters)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return NULL;
}

/* EOF */
