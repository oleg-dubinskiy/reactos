
#pragma once

/* DMA Mask Register Structure
  
   MSB                             LSB
      x   x   x   x     x   x   x   x
      \---/   -   -     -----   -----
        |     |   |       |       |     00 - Channel 0 select
        |     |   |       |       \---- 01 - Channel 1 select
        |     |   |       |             10 - Channel 2 select
        |     |   |       |             11 - Channel 3 select
        |     |   |       |
        |     |   |       |             00 - Verify transfer
        |     |   |       \------------ 01 - Write transfer
        |     |   |                     10 - Read transfer
        |     |   |
        |     |   \--------------------  0 - Autoinitialized
        |     |                          1 - Non-autoinitialized
        |     |
        |     \------------------------  0 - Address increment select
        |
        |                               00 - Demand mode
        \------------------------------ 01 - Single mode
                                        10 - Block mode
                                        11 - Cascade mode
*/
typedef union _DMA_MODE
{
   struct
   {
      UCHAR Channel: 2;
      UCHAR TransferType: 2;
      UCHAR AutoInitialize: 1;
      UCHAR AddressDecrement: 1;
      UCHAR RequestMode: 2;
   };
   UCHAR Byte;
} DMA_MODE, *PDMA_MODE;

typedef struct _ROS_MAP_REGISTER_ENTRY
{
   PVOID VirtualAddress;
   PHYSICAL_ADDRESS PhysicalAddress;
   ULONG Counter;
} ROS_MAP_REGISTER_ENTRY, *PROS_MAP_REGISTER_ENTRY;

typedef struct _ADAPTER_OBJECT
{
   /* New style DMA object definition.
      The fact that it is at the beginning of the ADAPTER_OBJECT structure allows
      us to easily implement the fallback implementation of IoGetDmaAdapter.
   */
   DMA_ADAPTER DmaHeader;

   /* For normal adapter objects pointer to master adapter that takes care
      of channel allocation. For master adapter set to NULL.
   */
   struct _ADAPTER_OBJECT* MasterAdapter;

   ULONG MapRegistersPerChannel;
   PVOID AdapterBaseVa;
   PROS_MAP_REGISTER_ENTRY MapRegisterBase;
   ULONG NumberOfMapRegisters;
   ULONG CommittedMapRegisters;
   PWAIT_CONTEXT_BLOCK CurrentWcb;
   KDEVICE_QUEUE ChannelWaitQueue;
   PKDEVICE_QUEUE RegisterWaitQueue;
   LIST_ENTRY AdapterQueue;
   KSPIN_LOCK SpinLock;
   PRTL_BITMAP MapRegisters;
   PUCHAR PagePort;
   UCHAR ChannelNumber;
   UCHAR AdapterNumber;
   USHORT DmaPortAddress;
   DMA_MODE AdapterMode;
   BOOLEAN NeedsMapRegisters;
   BOOLEAN MasterDevice;
   BOOLEAN Width16Bits;
   BOOLEAN ScatterGather;
   BOOLEAN IgnoreCount;
   BOOLEAN Dma32BitAddresses;
   BOOLEAN Dma64BitAddresses;
   LIST_ENTRY AdapterList;
} ADAPTER_OBJECT;

typedef struct _HALP_DMA_MASTER_ADAPTER
{
    PADAPTER_OBJECT MasterAdapter;
    ULONG MapBufferMaxPages;
    ULONG MapBufferSize;
    ULONG Unknown1;
    PHYSICAL_ADDRESS MapBufferPhysicalAddress;
    ULONG Unknown2;
    ULONG Unknown3;
} HALP_DMA_MASTER_ADAPTER, *PHALP_DMA_MASTER_ADAPTER;

/* EOF */
