
#pragma once

PDMA_ADAPTER NTAPI
HalpGetDmaAdapter(
    IN PVOID Context,
    IN PDEVICE_DESCRIPTION DeviceDescription,
    OUT PULONG NumberOfMapRegisters
);

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
