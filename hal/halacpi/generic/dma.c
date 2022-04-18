
/* INCLUDES *****************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/**
 * @name HalpGetDmaAdapter
 *
 * Internal routine to allocate PnP DMA adapter object. It's exported through
 * HalDispatchTable and used by IoGetDmaAdapter.
 *
 * @see HalGetAdapter
 */
PDMA_ADAPTER
NTAPI
HalpGetDmaAdapter(IN PVOID Context,
                  IN PDEVICE_DESCRIPTION DeviceDescriptor,
                  OUT PULONG NumberOfMapRegisters)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return NULL;
}

/* EOF */
