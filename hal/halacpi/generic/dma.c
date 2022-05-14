
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

static BOOLEAN HalpEisaDma = FALSE;
static KEVENT HalpDmaLock; // NT use HalpNewAdapter?
static PADAPTER_OBJECT HalpEisaAdapter[8];

static DMA_OPERATIONS HalpDmaOperations = {
    sizeof(DMA_OPERATIONS),
    (PPUT_DMA_ADAPTER)HalPutDmaAdapter,
    (PALLOCATE_COMMON_BUFFER)HalAllocateCommonBuffer,
    (PFREE_COMMON_BUFFER)HalFreeCommonBuffer,
    (PALLOCATE_ADAPTER_CHANNEL)IoAllocateAdapterChannel, /* Initialized in HalpInitDma() */
    (PFLUSH_ADAPTER_BUFFERS)IoFlushAdapterBuffers, /* Initialized in HalpInitDma() */
    (PFREE_ADAPTER_CHANNEL)IoFreeAdapterChannel, /* Initialized in HalpInitDma() */
    (PFREE_MAP_REGISTERS)IoFreeMapRegisters, /* Initialized in HalpInitDma() */
    (PMAP_TRANSFER)IoMapTransfer, /* Initialized in HalpInitDma() */
    (PGET_DMA_ALIGNMENT)HalpDmaGetDmaAlignment,
    (PREAD_DMA_COUNTER)HalReadDmaCounter,
    /* FIXME: Implement the S/G funtions. */
    (PGET_SCATTER_GATHER_LIST)HalGetScatterGatherList,
    (PPUT_SCATTER_GATHER_LIST)HalPutScatterGatherList,
    NULL /*(PCALCULATE_SCATTER_GATHER_LIST_SIZE)HalCalculateScatterGatherListSize*/,
    NULL /*(PBUILD_SCATTER_GATHER_LIST)HalBuildScatterGatherList*/,
    NULL /*(PBUILD_MDL_FROM_SCATTER_GATHER_LIST)HalBuildMdlFromScatterGatherList*/
};

static const ULONG_PTR HalpEisaPortPage[8] = {
   FIELD_OFFSET(DMA_PAGE, Channel0),
   FIELD_OFFSET(DMA_PAGE, Channel1),
   FIELD_OFFSET(DMA_PAGE, Channel2),
   FIELD_OFFSET(DMA_PAGE, Channel3),
   0,
   FIELD_OFFSET(DMA_PAGE, Channel5),
   FIELD_OFFSET(DMA_PAGE, Channel6),
   FIELD_OFFSET(DMA_PAGE, Channel7)
};

HALP_DMA_MASTER_ADAPTER MasterAdapter24;
HALP_DMA_MASTER_ADAPTER MasterAdapter32;

extern ULONG HalpBusType;
extern KSPIN_LOCK HalpDmaAdapterListLock;
extern BOOLEAN LessThan16Mb;
extern BOOLEAN HalpPhysicalMemoryMayAppearAbove4GB;
extern LIST_ENTRY HalpDmaAdapterList;

/* FUNCTIONS *****************************************************************/

/* HalpGetDmaAdapter
     Internal routine to allocate PnP DMA adapter object.
     It's exported through HalDispatchTable and used by IoGetDmaAdapter.
 
   see HalGetAdapter
*/
PDMA_ADAPTER
NTAPI
HalpGetDmaAdapter(IN PVOID Context,
                  IN PDEVICE_DESCRIPTION DeviceDescriptor,
                  OUT PULONG NumberOfMapRegisters)
{
    return &HalGetAdapter(DeviceDescriptor, NumberOfMapRegisters)->DmaHeader;
}

NTSTATUS
NTAPI
HalpAllocateMapRegisters(_In_ PADAPTER_OBJECT AdapterObject,
                         _In_ ULONG Unknown,
                         _In_ ULONG Unknown2,
                         PMAP_REGISTER_ENTRY Registers)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
HaliLocateHiberRanges(_In_ PVOID MemoryMap)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
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
    //InitializeListHead(&HalpDmaAdapterList);
    KeInitializeEvent(&HalpDmaLock, NotificationEvent, TRUE);
}

/* HalpGetAdapterMaximumPhysicalAddress
      Get the maximum physical address acceptable by the device represented by the passed DMA adapter.
*/
PHYSICAL_ADDRESS
NTAPI
HalpGetAdapterMaximumPhysicalAddress(IN PADAPTER_OBJECT AdapterObject)
{
    PHYSICAL_ADDRESS HighestAddress;

    if (!AdapterObject->MasterDevice)
    {
        HighestAddress.QuadPart = 0xFFFFFF;
    }
    else if (AdapterObject->Dma64BitAddresses)
    {
        HighestAddress.QuadPart = 0xFFFFFFFFFFFFFFFFULL;
    }
    else if (AdapterObject->Dma32BitAddresses)
    {
        HighestAddress.QuadPart = 0xFFFFFFFF;
    }

    return HighestAddress;
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
HalAllocateAdapterChannel(IN PADAPTER_OBJECT AdapterObject,
                          IN PWAIT_CONTEXT_BLOCK WaitContextBlock,
                          IN ULONG NumberOfMapRegisters,
                          IN PDRIVER_CONTROL ExecutionRoutine)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
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
HalGetAdapter(IN PDEVICE_DESCRIPTION DeviceDescription,
              OUT PULONG NumberOfMapRegisters)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return NULL;
}

/* HalAllocateCommonBuffer
      Allocates memory that is visible to both the processor(s) and the DMA device.
  
   AdapterObject
      Adapter object representing the bus master or system dma controller.

   Length
      Number of bytes to allocate.

   LogicalAddress
      Logical address the driver can use to access the buffer.

   CacheEnabled
      Specifies if the memory can be cached.
  
   return: The base virtual address of the memory allocated or NULL on failure.
  
   On real NT x86 systems the CacheEnabled parameter is ignored, we honour it.
   If it proves to cause problems change it.
  
   see HalFreeCommonBuffer
*/
PVOID
NTAPI
HalAllocateCommonBuffer(IN PADAPTER_OBJECT AdapterObject,
                        IN ULONG Length,
                        IN PPHYSICAL_ADDRESS LogicalAddress,
                        IN BOOLEAN CacheEnabled)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return NULL;
}

PVOID
NTAPI
HalAllocateCrashDumpRegisters(IN PADAPTER_OBJECT AdapterObject,
                              IN OUT PULONG NumberOfMapRegisters)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return NULL;
}

BOOLEAN
NTAPI
HalFlushCommonBuffer(IN PADAPTER_OBJECT AdapterObject,
                     IN ULONG Length,
                     IN PHYSICAL_ADDRESS LogicalAddress,
                     IN PVOID VirtualAddress)
{
    /* Function always returns true */
    return TRUE;
}

/* HalFreeCommonBuffer
      Free common buffer allocated with HalAllocateCommonBuffer.

   see HalAllocateCommonBuffer
*/
VOID
NTAPI
HalFreeCommonBuffer(IN PADAPTER_OBJECT AdapterObject,
                    IN ULONG Length,
                    IN PHYSICAL_ADDRESS LogicalAddress,
                    IN PVOID VirtualAddress,
                    IN BOOLEAN CacheEnabled)
{
    MmFreeContiguousMemorySpecifyCache(VirtualAddress,
                                       Length,
                                       CacheEnabled ? MmCached : MmNonCached);
}

/* HalReadDmaCounter
     Read DMA operation progress counter.
*/
ULONG
NTAPI
HalReadDmaCounter(IN PADAPTER_OBJECT AdapterObject)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return 0;
}

/* IoFlushAdapterBuffers
      Flush any data remaining in the DMA controller's memory into the host memory.

   AdapterObject
      The adapter object to flush.

   Mdl
      Original MDL to flush data into.

   MapRegisterBase
      Map register base that was just used by IoMapTransfer, etc.

   CurrentVa
      Offset into Mdl to be flushed into, same as was passed to IoMapTransfer.

   Length
      Length of the buffer to be flushed into.

   WriteToDevice
     TRUE if it's a write, FALSE if it's a read.

   return: TRUE in all cases.

   This copies data from the map register-backed buffer to the user's target buffer.
   Data are not in the user buffer until this function is called.
   For slave DMA transfers the controller channel is masked effectively stopping the current transfer.
*/
BOOLEAN
NTAPI
IoFlushAdapterBuffers(IN PADAPTER_OBJECT AdapterObject,
                      IN PMDL Mdl,
                      IN PVOID MapRegisterBase,
                      IN PVOID CurrentVa,
                      IN ULONG Length,
                      IN BOOLEAN WriteToDevice)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return FALSE;
}

/* IoFreeAdapterChannel
      Free DMA resources allocated by IoAllocateAdapterChannel.

   AdapterObject
      Adapter object with resources to free.

   This function releases map registers registers assigned to the DMA adapter.
   After releasing the adapter, it checks the adapter's queue
   and runs each queued device object in series until the queue is empty.
   This is the only way the device queue is emptied.
  
   see IoAllocateAdapterChannel
*/
VOID
NTAPI
IoFreeAdapterChannel(IN PADAPTER_OBJECT AdapterObject)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

/* IoFreeMapRegisters
      Free map registers reserved by the system for a DMA.
 
   AdapterObject
     DMA adapter to free map registers on.

   MapRegisterBase
     Handle to map registers to free.

   NumberOfRegisters
     Number of map registers to be freed.
*/
VOID
NTAPI
IoFreeMapRegisters(IN PADAPTER_OBJECT AdapterObject,
                   IN PVOID MapRegisterBase,
                   IN ULONG NumberOfMapRegisters)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

/* IoMapTransfer
      Map a DMA for transfer and do the DMA if it's a slave.

   AdapterObject
      Adapter object to do the DMA on. Bus-master may pass NULL.

   Mdl
      Locked-down user buffer to DMA in to or out of.

   MapRegisterBase
      Handle to map registers to use for this dma.

   CurrentVa
      Index into Mdl to transfer into/out of.

   Length
      Length of transfer. Number of bytes actually transferred on output.

   WriteToDevice
      TRUE if it's an output DMA, FALSE otherwise.

   return: A logical address that can be used to program a DMA controller,
           it's not meaningful for slave DMA device.

   This function does a copyover to contiguous memory <16MB represented by the map registers if needed.
   If the buffer described by MDL can be used as is no copyover is done.
   If it's a slave transfer, this function actually performs it.
*/
PHYSICAL_ADDRESS
NTAPI
IoMapTransfer(IN PADAPTER_OBJECT AdapterObject,
              IN PMDL Mdl,
              IN PVOID MapRegisterBase,
              IN PVOID CurrentVa,
              IN OUT PULONG Length,
              IN BOOLEAN WriteToDevice)
{
    PHYSICAL_ADDRESS PhysicalAddress = {{0,0}};
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return PhysicalAddress;
}

/* EOF */
