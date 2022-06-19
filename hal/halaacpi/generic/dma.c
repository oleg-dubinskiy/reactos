
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpInitDma)
#endif

/* GLOBALS *******************************************************************/

HALP_DMA_MASTER_ADAPTER MasterAdapter24;
HALP_DMA_MASTER_ADAPTER MasterAdapter32;
LIST_ENTRY HalpDmaAdapterList;
KSPIN_LOCK HalpDmaAdapterListLock;

static BOOLEAN HalpEisaDma = FALSE;
static KEVENT HalpDmaLock; // NT use HalpNewAdapter?

extern ULONG HalpBusType;

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

NTSTATUS
NTAPI
HalpAllocateMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Unknown,
    _In_ ULONG Unknown2,
    _In_ PMAP_REGISTER_ENTRY Registers)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
HaliLocateHiberRanges(
    _In_ PVOID MemoryMap)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
}

INIT_FUNCTION
VOID
NTAPI
HalpInitDma(VOID)
{
    DPRINT("HalpInitDma()\n");

    if (HalpBusType == MACHINE_TYPE_EISA)
    {
        /* Check if Extended DMA is available. We're just going to do a random read and write. */
        WRITE_PORT_UCHAR(UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaController2Pages.Channel2)), 0x2A);

        if (READ_PORT_UCHAR(UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaController2Pages.Channel2))) == 0x2A)
        {
            DPRINT1("Machine supports EISA DMA. Bus type: %lu\n", HalpBusType);
            HalpEisaDma = TRUE;
        }
    }

    /* Intialize all the global variables and allocate master adapter with first map buffers. */
    KeInitializeEvent(&HalpDmaLock, NotificationEvent, TRUE);
}

/* PUBLIC FUNCTIONS **********************************************************/

/* HalAllocateAdapterChannel
       Setup map registers for an adapter object.

   AdapterObject
      Pointer to an ADAPTER_OBJECT to set up.

   WaitContextBlock
      Context block to be used with ExecutionRoutine.

   NumberOfMapRegisters
      Number of map registers requested.

   ExecutionRoutine
      Callback to call when map registers are allocated.

   return:
      If not enough map registers can be allocated then STATUS_INSUFFICIENT_RESOURCES is returned.
      If the function succeeds or the callback is queued for later delivering then STATUS_SUCCESS is returned.
 
   see IoFreeAdapterChannel
*/
NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PWAIT_CONTEXT_BLOCK WaitContextBlock,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

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

PVOID
NTAPI
HalAllocateCrashDumpRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _Inout_ PULONG NumberOfMapRegisters)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return NULL;
}

/* EOF */
