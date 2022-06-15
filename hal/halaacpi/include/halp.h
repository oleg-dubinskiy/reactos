
#pragma once

/* Mm PTE/PDE to Hal PTE/PDE */
#define HalAddressToPde(x) (PHARDWARE_PTE)MiAddressToPde(x)
#define HalAddressToPte(x) (PHARDWARE_PTE)MiAddressToPte(x)

#define HAL_PIC_VECTORS  16

/* dma.c */
PDMA_ADAPTER
NTAPI
HaliGetDmaAdapter(
    _In_ PVOID Context,
    _In_ PDEVICE_DESCRIPTION DeviceDescriptor,
    _Out_ PULONG NumberOfMapRegisters
);

/* halinit.c */
INIT_FUNCTION
VOID
NTAPI
HalInitializeProcessor(
    IN ULONG ProcessorNumber,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
);

INIT_FUNCTION
BOOLEAN
NTAPI
HalInitSystem(
    IN ULONG BootPhase,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
);

INIT_FUNCTION
VOID
NTAPI
HalpGetParameters(
    _In_ PCHAR CommandLine
);

/* halpnpdd.c */
NTSTATUS
NTAPI
HaliInitPnpDriver(
    VOID
);

/* memory.c */
INIT_FUNCTION
ULONG_PTR
NTAPI
HalpAllocPhysicalMemory(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN ULONG MaxAddress,
    IN PFN_NUMBER PageCount,
    IN BOOLEAN Aligned
);

PVOID
NTAPI
HalpMapPhysicalMemory64(
    IN PHYSICAL_ADDRESS PhysicalAddress,
    IN PFN_COUNT PageCount
);

VOID
NTAPI
HalpUnmapVirtualAddress(
    IN PVOID VirtualAddress,
    IN PFN_COUNT NumberPages
);

PVOID
NTAPI
HalpMapPhysicalMemoryWriteThrough64(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ PFN_COUNT PageCount
);

PVOID
NTAPI
HalpRemapVirtualAddress64(
    _In_ PVOID VirtualAddress,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ BOOLEAN IsWriteThrough
);

/* mics.c */
VOID
NTAPI
HalpFlushTLB(
    VOID
);

/* pic.c */
VOID
NTAPI
HalpInitializeLegacyPICs(
    _In_ BOOLEAN InterruptMode
);

/* pcibus.c */
INIT_FUNCTION
NTSTATUS
NTAPI
HalpSetupPciDeviceForDebugging(
    _In_ PVOID LoaderBlock,
    _Inout_ OUT PDEBUG_DEVICE_DESCRIPTOR PciDevice
);

INIT_FUNCTION
NTSTATUS
NTAPI
HalpReleasePciDeviceForDebugging(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR PciDevice
);

/* sysinfo.c */
NTSTATUS
NTAPI
HaliQuerySystemInformation(
    _In_ HAL_QUERY_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _Inout_ PVOID Buffer,
    _Out_ PULONG ReturnedLength
);

NTSTATUS
NTAPI
HaliSetSystemInformation(
    _In_ HAL_SET_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _Inout_ PVOID Buffer
);

/* EOF */
