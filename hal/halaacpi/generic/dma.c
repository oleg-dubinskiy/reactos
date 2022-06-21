
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
static PADAPTER_OBJECT HalpEisaAdapter[8];

static DMA_OPERATIONS HalpDmaOperations =
{
    sizeof(DMA_OPERATIONS),
    HalPutDmaAdapter,
    HalAllocateCommonBuffer,
    HalFreeCommonBuffer,
    IoAllocateAdapterChannel,
    IoFlushAdapterBuffers,
    IoFreeAdapterChannel,
    IoFreeMapRegisters,
    IoMapTransfer,
    HalpDmaGetDmaAlignment,
    HalReadDmaCounter,
    /* FIXME: Implement the S/G funtions. */
    HalGetScatterGatherList,
    HalPutScatterGatherList,
    HalCalculateScatterGatherListSize,
    HalBuildScatterGatherList,
    HalBuildMdlFromScatterGatherList
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

extern ULONG HalpBusType;
extern BOOLEAN HalpPhysicalMemoryMayAppearAbove4GB;
extern BOOLEAN LessThan16Mb;

/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
HalpGrowMapBuffers(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG SizeOfMapBuffers)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return FALSE;
}

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

PADAPTER_OBJECT
NTAPI
HalpAllocateAdapterEx(
    _In_ ULONG MapRegisters,
    _In_ PVOID AdapterBaseVa,
    _In_ BOOLEAN IsDma32Bit)
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

    if (HalpPhysicalMemoryMayAppearAbove4GB && IsDma32Bit)
        pMasterAdapter = &MasterAdapter32;
    else
        pMasterAdapter = &MasterAdapter24;

    if (AdapterBaseVa == LongToPtr(-1))
    {
        IsMasterAdapter = TRUE;
    }
    else
    {
        IsMasterAdapter = FALSE;

        if (MapRegisters && pMasterAdapter->MasterAdapter == NULL)
        {
            MasterAdapter = HalpAllocateAdapterEx(MapRegisters, LongToPtr(-1), IsDma32Bit);
            if (!MasterAdapter)
            {
                DPRINT1("Alloc DMA AdapterObject failed!\n");
                return NULL;
            }

            MasterAdapter->Dma32BitAddresses = IsDma32Bit;
            MasterAdapter->MasterDevice = IsDma32Bit;

            pMasterAdapter->MasterAdapter = MasterAdapter;
        }
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               (OBJ_KERNEL_HANDLE | OBJ_PERMANENT),
                               NULL,
                               NULL);
    if (IsMasterAdapter)
    {
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
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpAllocateAdapterEx: Status %X\n", Status);
        return NULL;
    }

    AdapterObject = Object;

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
        DPRINT1("HalpAllocateAdapterEx: Status - %X\n", Status);
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
        AdapterObject->MasterAdapter = pMasterAdapter->MasterAdapter;
    else
        AdapterObject->MasterAdapter = NULL;

    DPRINT1("HalpAllocateAdapterEx: MasterAdapter %p\n", AdapterObject->MasterAdapter);

    KeInitializeDeviceQueue(&AdapterObject->ChannelWaitQueue);

    if (!IsMasterAdapter)
    {
        DPRINT1("HalpAllocateAdapterEx: return %p\n", AdapterObject);
        return AdapterObject;
    }

    KeInitializeSpinLock(&AdapterObject->SpinLock);
    InitializeListHead(&AdapterObject->AdapterQueue);

    AdapterObject->MapRegisters = (PVOID)(MasterAdapter + 1);

    RtlInitializeBitMap(AdapterObject->MapRegisters,
                        (PULONG)(AdapterObject->MapRegisters + 1),
                        SizeOfBitMap);

    RtlSetAllBits(AdapterObject->MapRegisters);

    AdapterObject->NumberOfMapRegisters = 0;
    AdapterObject->CommittedMapRegisters = 0;

    MapRegisterBaseSize = (SizeOfBitMap * sizeof(ROS_MAP_REGISTER_ENTRY));
    MapRegisterBase = ExAllocatePoolWithTag(NonPagedPool, MapRegisterBaseSize, ' laH');

    AdapterObject->MapRegisterBase = MapRegisterBase;
    if (!MapRegisterBase)
    {
        DPRINT1("HalpAllocateAdapterEx: failed for AdapterObject %p\n", AdapterObject);
        ObDereferenceObject(AdapterObject);
        return NULL;
    }

    RtlZeroMemory(MapRegisterBase, MapRegisterBaseSize);

    if (!HalpGrowMapBuffers(AdapterObject, 0x10000))
    {
        DPRINT1("HalpAllocateAdapterEx: failed for AdapterObject %p\n", AdapterObject);
        ObDereferenceObject(AdapterObject);
        return NULL;
    }

    DPRINT1("HalpAllocateAdapterEx: Allocated %p\n", AdapterObject);
    return AdapterObject;
}

VOID
NTAPI
HalPutDmaAdapter(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
}

PVOID
NTAPI
HalAllocateCommonBuffer(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ ULONG Length,
    _In_ PPHYSICAL_ADDRESS LogicalAddress,
    _In_ BOOLEAN CacheEnabled)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return NULL;
}

VOID
NTAPI
HalFreeCommonBuffer(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ ULONG Length,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ BOOLEAN CacheEnabled)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
}

BOOLEAN
NTAPI
IoFlushAdapterBuffers(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return FALSE;
}

VOID
NTAPI
IoFreeAdapterChannel(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
}

VOID
NTAPI
IoFreeMapRegisters(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PVOID MapRegisterBase,
    _In_ ULONG NumberOfMapRegisters)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
}

PHYSICAL_ADDRESS
NTAPI
IoMapTransfer(
_In_ PDMA_ADAPTER DmaAdapter,
_In_ PMDL Mdl,
_In_ PVOID MapRegisterBase,
_In_ PVOID CurrentVa,
_Inout_ PULONG Length,
_In_ BOOLEAN WriteToDevice)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    PHYSICAL_ADDRESS HighestAddress;
    UNIMPLEMENTED;HighestAddress.QuadPart = 0;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return HighestAddress;
}

ULONG
NTAPI
HalpDmaGetDmaAlignment(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return 0;
}

ULONG
NTAPI
HalReadDmaCounter(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return 0;
}

NTSTATUS
NTAPI
HalGetScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_ PVOID Context,
    _In_ BOOLEAN WriteToDevice)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
HalPutScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PSCATTER_GATHER_LIST ScatterGather,
    _In_ BOOLEAN WriteToDevice)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
}

NTSTATUS
NTAPI
HalCalculateScatterGatherListSize(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl OPTIONAL,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _Out_ PULONG ScatterGatherListSize,
    _Out_ PULONG pNumberOfMapRegisters)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalBuildScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _In_ PVOID ScatterGatherBuffer,
    _In_ ULONG ScatterGatherBufferLength)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalBuildMdlFromScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PSCATTER_GATHER_LIST ScatterGather,
    _In_ PMDL OriginalMdl,
    _Out_ PMDL* TargetMdl)
{
    //PDMA_ADAPTER DmaAdapter = (PADAPTER_OBJECT)DmaAdapter;
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

/* HalpDmaInitializeEisaAdapter
       Setup DMA modes and extended modes for (E)ISA DMA adapter object.
*/
BOOLEAN
NTAPI
HalpDmaInitializeEisaAdapter(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PDEVICE_DESCRIPTION DeviceDescriptor)
{
    DMA_EXTENDED_MODE ExtendedMode = {{0}};
    DMA_MODE DmaMode = {{0}};
    PVOID AdapterBaseVa;
    PVOID Port;
    UCHAR Controller;
    UCHAR DmaMask;

    Controller = ((DeviceDescriptor->DmaChannel & 4) ? 2 : 1);

    if (Controller == 1)
        AdapterBaseVa = UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaController1));
    else
        AdapterBaseVa = UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaController2));

    AdapterObject->AdapterNumber = Controller;
    AdapterObject->ChannelNumber = (UCHAR)(DeviceDescriptor->DmaChannel & 3);
    AdapterObject->PagePort = (PUCHAR)HalpEisaPortPage[DeviceDescriptor->DmaChannel];
    AdapterObject->Width16Bits = FALSE;
    AdapterObject->AdapterBaseVa = AdapterBaseVa;

    if (HalpEisaDma)
    {
        ExtendedMode.ChannelNumber = AdapterObject->ChannelNumber;

        switch (DeviceDescriptor->DmaSpeed)
        {
            case Compatible: ExtendedMode.TimingMode = COMPATIBLE_TIMING; break;
            case TypeA:      ExtendedMode.TimingMode = TYPE_A_TIMING;     break;
            case TypeB:      ExtendedMode.TimingMode = TYPE_B_TIMING;     break;
            case TypeC:      ExtendedMode.TimingMode = BURST_TIMING;      break;
            default:
                return FALSE;
        }

        switch (DeviceDescriptor->DmaWidth)
        {
            case Width8Bits:  ExtendedMode.TransferSize = B_8BITS;  break;
            case Width16Bits: ExtendedMode.TransferSize = B_16BITS; break;
            case Width32Bits: ExtendedMode.TransferSize = B_32BITS; break;
            default:
                return FALSE;
        }

        if (Controller == 1)
        {
            Port = UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaExtendedMode1));
            WRITE_PORT_UCHAR(Port, ExtendedMode.Byte);
        }
        else
        {
            Port = UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaExtendedMode2));
            WRITE_PORT_UCHAR(Port, ExtendedMode.Byte);
        }
    }
    else
    {
        /* Validate setup for non-busmaster DMA adapter.
           Secondary controller supports only 16-bit transfers and main controller supports only 8-bit transfers.
           Anything else is invalid.
        */
        if (!DeviceDescriptor->Master)
        {
            if ((Controller == 2) && (DeviceDescriptor->DmaWidth == Width16Bits))
            {
                AdapterObject->Width16Bits = TRUE;
            }
            else if ((Controller != 1) || (DeviceDescriptor->DmaWidth != Width8Bits))
            {
                return FALSE;
            }
        }
    }

    DmaMode.Channel = AdapterObject->ChannelNumber;
    DmaMode.AutoInitialize = DeviceDescriptor->AutoInitialize;

    /* Set the DMA request mode.
       For (E)ISA bus master devices just unmask (enable) the DMA channel and set it to cascade mode.
       Otherwise just select the right one bases on the passed device description.
    */
    if (!DeviceDescriptor->Master)
    {
        if (DeviceDescriptor->DemandMode)
            DmaMode.RequestMode = DEMAND_REQUEST_MODE;
        else
            DmaMode.RequestMode = SINGLE_REQUEST_MODE;

        goto Exit;
    }

    DmaMode.RequestMode = CASCADE_REQUEST_MODE;
    DmaMask = (AdapterObject->ChannelNumber | DMA_CLEARMASK);

    if (Controller == 1)
    {
        /* Set the Request Data */
        _PRAGMA_WARNING_SUPPRESS(__WARNING_DEREF_NULL_PTR)
        WRITE_PORT_UCHAR(&((PDMA1_CONTROL)AdapterBaseVa)->Mode, DmaMode.Byte);

        /* Unmask DMA Channel */
        WRITE_PORT_UCHAR(&((PDMA1_CONTROL)AdapterBaseVa)->SingleMask, DmaMask);
    }
    else
    {
        /* Set the Request Data */
        WRITE_PORT_UCHAR(&((PDMA2_CONTROL)AdapterBaseVa)->Mode, DmaMode.Byte);

        /* Unmask DMA Channel */
        WRITE_PORT_UCHAR(&((PDMA2_CONTROL)AdapterBaseVa)->SingleMask, DmaMask);
    }

Exit:
    AdapterObject->AdapterMode = DmaMode;
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
HalAllocateAdapterChannel(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PWAIT_CONTEXT_BLOCK WaitContextBlock,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine)
{
    //PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
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
