
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

/* HalpGrowMapBuffers
      Allocate initial, or additional, map buffers for DMA master adapter.

   MasterAdapter
     DMA master adapter to allocate buffers for.
   SizeOfMapBuffers
     Size of the map buffers to allocate (not including the size already allocated).
*/
BOOLEAN
NTAPI
HalpGrowMapBuffers(IN PADAPTER_OBJECT AdapterObject,
                   IN ULONG SizeOfMapBuffers)
{
    PROS_MAP_REGISTER_ENTRY CurrentEntry;
    PROS_MAP_REGISTER_ENTRY PreviousEntry;
    PHYSICAL_ADDRESS PhysicalAddress;
    PHYSICAL_ADDRESS HighestAcceptableAddress;
    PHYSICAL_ADDRESS LowestAcceptableAddress;
    PHYSICAL_ADDRESS BoundryAddressMultiple;
    PVOID VirtualAddress;
    ULONG MapRegisterCount;
    KIRQL OldIrql;

    /* Check if enough map register slots are available. */
    MapRegisterCount = BYTES_TO_PAGES(SizeOfMapBuffers);
    if (MapRegisterCount + AdapterObject->NumberOfMapRegisters > MAX_MAP_REGISTERS)
    {
        DPRINT("No more map register slots available! (Current: %d | Requested: %d | Limit: %d)\n",
               AdapterObject->NumberOfMapRegisters, MapRegisterCount, MAX_MAP_REGISTERS);

        return FALSE;
    }

    /* Allocate memory for the new map registers.
       For 32-bit adapters we use two passes in order not to waste scare resource (low memory).
    */
    HighestAcceptableAddress = HalpGetAdapterMaximumPhysicalAddress(AdapterObject);

    LowestAcceptableAddress.HighPart = 0;
    LowestAcceptableAddress.LowPart = ((HighestAcceptableAddress.LowPart == 0xFFFFFFFF) ? 0x1000000 : 0);

    BoundryAddressMultiple.QuadPart = 0;

    VirtualAddress = 
    MmAllocateContiguousMemorySpecifyCache((MapRegisterCount << PAGE_SHIFT),
                                           LowestAcceptableAddress,
                                           HighestAcceptableAddress,
                                           BoundryAddressMultiple,
                                           MmNonCached);

    if (!VirtualAddress && LowestAcceptableAddress.LowPart)
    {
        LowestAcceptableAddress.LowPart = 0;

        VirtualAddress =
        MmAllocateContiguousMemorySpecifyCache((MapRegisterCount << PAGE_SHIFT),
                                               LowestAcceptableAddress,
                                               HighestAcceptableAddress,
                                               BoundryAddressMultiple,
                                               MmNonCached);
    }

    if (!VirtualAddress)
        return FALSE;

    PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);

    /* All the following must be done with the master adapter lock held to prevent corruption. */
    KeAcquireSpinLock(&AdapterObject->SpinLock, &OldIrql);

    /* Setup map register entries for the buffer allocated.
       Each entry has a virtual and physical address and corresponds to PAGE_SIZE large buffer.
    */
    CurrentEntry = (AdapterObject->MapRegisterBase + AdapterObject->NumberOfMapRegisters);

    for (; MapRegisterCount; MapRegisterCount--)
    {
        /* Leave one entry free for every non-contiguous memory region in the map register bitmap.
           This ensures that we can search using RtlFindClearBits for contiguous map register regions.

           Also for non-EISA DMA leave one free entry for every 64Kb break,
           because the DMA controller can handle only coniguous 64Kb regions.
        */
        if (CurrentEntry != AdapterObject->MapRegisterBase)
        {
            PreviousEntry = (CurrentEntry - 1);

            if ((PreviousEntry->PhysicalAddress.LowPart + PAGE_SIZE) == PhysicalAddress.LowPart)
            {
                if (!HalpEisaDma)
                {
                    if ((PreviousEntry->PhysicalAddress.LowPart ^ PhysicalAddress.LowPart) & 0xFFFF0000)
                    {
                        CurrentEntry++;
                        AdapterObject->NumberOfMapRegisters++;
                    }
                }
            }
            else
            {
                CurrentEntry++;
                AdapterObject->NumberOfMapRegisters++;
            }
        }

        RtlClearBit(AdapterObject->MapRegisters, (ULONG)(CurrentEntry - AdapterObject->MapRegisterBase));

        CurrentEntry->VirtualAddress = VirtualAddress;
        CurrentEntry->PhysicalAddress = PhysicalAddress;

        PhysicalAddress.LowPart += PAGE_SIZE;
        VirtualAddress = (PVOID)((ULONG_PTR)VirtualAddress + PAGE_SIZE);

        CurrentEntry++;
        AdapterObject->NumberOfMapRegisters++;
    }

    KeReleaseSpinLock(&AdapterObject->SpinLock, OldIrql);
    return TRUE;
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
    PADAPTER_OBJECT MasterAdapter;
    PGROW_WORK_ITEM WorkItem;
    ULONG Index = MAXULONG;
    ULONG Result;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    /* Set up the wait context block in case we can't run right away. */
    WaitContextBlock->DeviceRoutine = ExecutionRoutine;
    WaitContextBlock->NumberOfMapRegisters = NumberOfMapRegisters;

    /* Returns true if queued, else returns false and sets the queue to busy */
    if (KeInsertDeviceQueue(&AdapterObject->ChannelWaitQueue, &WaitContextBlock->WaitQueueEntry))
        return STATUS_SUCCESS;

    MasterAdapter = AdapterObject->MasterAdapter;

    AdapterObject->NumberOfMapRegisters = NumberOfMapRegisters;
    AdapterObject->CurrentWcb = WaitContextBlock;

    if ((NumberOfMapRegisters) && (AdapterObject->NeedsMapRegisters))
    {
        if (NumberOfMapRegisters > AdapterObject->MapRegistersPerChannel)
        {
            AdapterObject->NumberOfMapRegisters = 0;
            IoFreeAdapterChannel(AdapterObject);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Get the map registers.
           This is partly complicated by the fact that new map registers can only be allocated at PASSIVE_LEVEL and we're currently at DISPATCH_LEVEL.
           The following code has two code paths:

          - If there is no adapter queued for map register allocation, try to see if enough contiguous map registers are present.
            In case they're we can just get them and proceed further.

          - If some adapter is already present in the queue we must respect the order of adapters asking for map registers and so the fast case described above can't take place.
            This case is also entered if not enough coniguous map registers are present.

            A work queue item is allocated and queued, the adapter is also queued into the master adapter queue.
            The worker routine does the job of allocating the map registers at PASSIVE_LEVEL and calling the ExecutionRoutine.
        */

        KeAcquireSpinLock(&MasterAdapter->SpinLock, &OldIrql);

        if (IsListEmpty(&MasterAdapter->AdapterQueue))
        {
            Index = RtlFindClearBitsAndSet(MasterAdapter->MapRegisters, NumberOfMapRegisters, 0);

            if (Index != MAXULONG)
            {
                AdapterObject->MapRegisterBase = MasterAdapter->MapRegisterBase + Index;
                if (!AdapterObject->ScatterGather)
                {
                    AdapterObject->MapRegisterBase = (PROS_MAP_REGISTER_ENTRY)((ULONG_PTR)AdapterObject->MapRegisterBase | MAP_BASE_SW_SG);
                }
            }
        }

        if (Index == MAXULONG)
        {
            InsertTailList(&MasterAdapter->AdapterQueue, &AdapterObject->AdapterQueue);

            WorkItem = ExAllocatePoolWithTag(NonPagedPool, sizeof(GROW_WORK_ITEM), TAG_DMA);
            if (WorkItem)
            {
                ExInitializeWorkItem(&WorkItem->WorkQueueItem, HalpGrowMapBufferWorker, WorkItem);
                WorkItem->AdapterObject = AdapterObject;
                WorkItem->NumberOfMapRegisters = NumberOfMapRegisters;

                ExQueueWorkItem(&WorkItem->WorkQueueItem, DelayedWorkQueue);
            }

            KeReleaseSpinLock(&MasterAdapter->SpinLock, OldIrql);

            return STATUS_SUCCESS;
        }

        KeReleaseSpinLock(&MasterAdapter->SpinLock, OldIrql);
    }
    else
    {
        AdapterObject->MapRegisterBase = NULL;
        AdapterObject->NumberOfMapRegisters = 0;
    }

    AdapterObject->CurrentWcb = WaitContextBlock;

    Result = ExecutionRoutine(WaitContextBlock->DeviceObject,
                              WaitContextBlock->CurrentIrp,
                              AdapterObject->MapRegisterBase,
                              WaitContextBlock->DeviceContext);

    /* Possible return values:
     
       - KeepObject
         Don't free any resources, the ADAPTER_OBJECT is still in use and the caller will call IoFreeAdapterChannel later.

       - DeallocateObject
         Deallocate the map registers and release the ADAPTER_OBJECT, so someone else can use it.

       - DeallocateObjectKeepRegisters
         Release the ADAPTER_OBJECT, but hang on to the map registers. The client will later call IoFreeMapRegisters.

       NOTE:
       IoFreeAdapterChannel runs the queue, so it must be called unless the adapter object is not to be freed.
    */
    if (Result == DeallocateObject)
    {
        IoFreeAdapterChannel(AdapterObject);
    }
    else if (Result == DeallocateObjectKeepRegisters)
    {
        AdapterObject->NumberOfMapRegisters = 0;
        IoFreeAdapterChannel(AdapterObject);
    }

    return STATUS_SUCCESS;
}

PADAPTER_OBJECT
NTAPI
HalpAllocateAdapterEx(IN ULONG MapRegisters,
                      IN PVOID AdapterBaseVa,
                      IN BOOLEAN IsDma32Bit)
{
    PHALP_DMA_MASTER_ADAPTER pMasterAdapter;
    PROS_MAP_REGISTER_ENTRY MapRegisterBase;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PADAPTER_OBJECT MasterAdapter;
    PADAPTER_OBJECT AdapterObject;
    HANDLE Handle;
    PVOID Object;
    ULONG MapRegisterBaseSize;
    ULONG MapBufferMaxPages;
    ULONG SizeOfBitMap;
    ULONG AdapterSize;
    BOOLEAN IsMasterAdapter;
    NTSTATUS Status;
  
    PAGED_CODE();
    DPRINT1("HalpAllocateAdapterEx: %X, %X, %X\n", MapRegisters, AdapterBaseVa, IsDma32Bit);

    if (AdapterBaseVa == LongToPtr(-1))
    {
        IsMasterAdapter = TRUE;
    }
    else
    {
        IsMasterAdapter = FALSE;

        if (MapRegisters)
        {
            if (!HalpPhysicalMemoryMayAppearAbove4GB || !IsDma32Bit)
                pMasterAdapter = &MasterAdapter24;
            else
                pMasterAdapter = &MasterAdapter32;

            if (!pMasterAdapter->MasterAdapter)
            {
                MasterAdapter = HalpAllocateAdapterEx(MapRegisters, LongToPtr(-1), IsDma32Bit);
                if (!MasterAdapter)
                    return NULL;

                MasterAdapter->Dma32BitAddresses = IsDma32Bit;
                MasterAdapter->MasterDevice = IsDma32Bit;

                if (!HalpPhysicalMemoryMayAppearAbove4GB || !IsDma32Bit)
                    pMasterAdapter = &MasterAdapter24;
                else
                    pMasterAdapter = &MasterAdapter32;

                pMasterAdapter->MasterAdapter = MasterAdapter;
            }
        }
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               (OBJ_KERNEL_HANDLE | OBJ_PERMANENT),
                               NULL,
                               NULL);
    if (IsMasterAdapter)
    {
        if (!HalpPhysicalMemoryMayAppearAbove4GB || !IsDma32Bit)
            pMasterAdapter = &MasterAdapter24;
        else
            pMasterAdapter = &MasterAdapter32;

        MapBufferMaxPages = pMasterAdapter->MapBufferMaxPages;
        SizeOfBitMap = MapBufferMaxPages;

        AdapterSize = sizeof(ADAPTER_OBJECT) + ((((MapBufferMaxPages + 7) >> 3) + 11) & ~3);
    }
    else
    {
        AdapterSize = sizeof(ADAPTER_OBJECT);
    }

    Status = ObCreateObject(KernelMode,
                            IoAdapterObjectType,
                            &ObjectAttributes,
                            KernelMode,
                            NULL,
                            AdapterSize,
                            0,
                            0,
                            &Object);

    AdapterObject = (PADAPTER_OBJECT)Object;

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpAllocateAdapterEx: Status %X\n", Status);
        return NULL;
    }

    Status = ObReferenceObjectByPointer(AdapterObject,
                                        (FILE_READ_DATA | FILE_WRITE_DATA),
                                        IoAdapterObjectType,
                                        KernelMode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpAllocateAdapterEx: Status %X\n", Status);
        return NULL;
    }

    RtlZeroMemory(AdapterObject, sizeof(ADAPTER_OBJECT));

    Status = ObInsertObject(AdapterObject,
                            NULL,
                            (FILE_READ_DATA | FILE_WRITE_DATA),
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpAllocateAdapterEx: Status %X\n", Status);
        return NULL;
    }

    ZwClose(Handle);

    AdapterObject->DmaHeader.Version = 1;
    AdapterObject->DmaHeader.Size = AdapterSize;
    AdapterObject->DmaHeader.DmaOperations = &HalpDmaOperations;

    AdapterObject->MapRegistersPerChannel = 1;
    AdapterObject->AdapterBaseVa = AdapterBaseVa;
    AdapterObject->ChannelNumber = 0xFF;
    AdapterObject->Dma32BitAddresses = IsDma32Bit;

    if (MapRegisters)
    {
        if (!HalpPhysicalMemoryMayAppearAbove4GB || !IsDma32Bit)
            pMasterAdapter = &MasterAdapter24;
        else
            pMasterAdapter = &MasterAdapter32;

        AdapterObject->MasterAdapter = pMasterAdapter->MasterAdapter;
    }
    else
    {
        AdapterObject->MasterAdapter = NULL;
    }

    DPRINT1("HalpAllocateAdapterEx: MasterAdapter %p\n", AdapterObject->MasterAdapter);

    KeInitializeDeviceQueue(&AdapterObject->ChannelWaitQueue);

    if (!IsMasterAdapter)
    {
        DPRINT1("HalpAllocateAdapterEx: return %p\n", AdapterObject);
        return AdapterObject;
    }

    KeInitializeSpinLock(&AdapterObject->SpinLock);
    InitializeListHead(&AdapterObject->AdapterQueue);

    AdapterObject->MapRegisters = (PRTL_BITMAP)&AdapterObject[1].DmaHeader.DmaOperations;

    RtlInitializeBitMap((PRTL_BITMAP)&AdapterObject[1].DmaHeader.DmaOperations,
                        &AdapterObject[1].MapRegistersPerChannel,
                        SizeOfBitMap);

    RtlSetAllBits(AdapterObject->MapRegisters);

    AdapterObject->NumberOfMapRegisters = 0;
    AdapterObject->CommittedMapRegisters = 0;

    MapRegisterBaseSize = (12 * SizeOfBitMap);
    MapRegisterBase = ExAllocatePoolWithTag(0, MapRegisterBaseSize, ' laH');

    AdapterObject->MapRegisterBase = MapRegisterBase;
    if (!MapRegisterBase)
    {
        ObDereferenceObject(AdapterObject);
        return NULL;
    }

    RtlZeroMemory(MapRegisterBase, MapRegisterBaseSize);

    if (!HalpGrowMapBuffers(AdapterObject, 0x10000))
    {
        ObDereferenceObject(AdapterObject);
        return NULL;
    }

    DPRINT1("HalpAllocateAdapterEx: return %p\n", AdapterObject);
    return AdapterObject;
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
