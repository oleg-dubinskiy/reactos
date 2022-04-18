
#pragma once

/* Mm PTE/PDE to Hal PTE/PDE */
#define HalAddressToPde(x) (PHARDWARE_PTE)MiAddressToPde(x)
#define HalAddressToPte(x) (PHARDWARE_PTE)MiAddressToPte(x)

/* Usage flags */
#define IDT_REGISTERED          0x01
#define IDT_LATCHED             0x02
#define IDT_READ_ONLY           0x04
#define IDT_INTERNAL            0x11
#define IDT_DEVICE              0x21

typedef struct _IDTUsageFlags
{
    UCHAR Flags;
} IDTUsageFlags;

typedef struct
{
    KIRQL Irql;
    UCHAR BusReleativeVector;
} IDTUsage;

typedef struct _HalAddressUsage
{
    struct _HalAddressUsage *Next;
    CM_RESOURCE_TYPE Type;
    UCHAR Flags;
    struct
    {
        ULONG Start;
        ULONG Length;
    } Element[];
} ADDRESS_USAGE, *PADDRESS_USAGE;

/* cmos.c */
INIT_FUNCTION
VOID
NTAPI
HalpInitializeCmos(
    VOID
);

/* dma.c */
PDMA_ADAPTER
NTAPI
HalpGetDmaAdapter(
    IN PVOID Context,
    IN PDEVICE_DESCRIPTION DeviceDescriptor,
    OUT PULONG NumberOfMapRegisters
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

/* mics.c */
VOID
NTAPI
HalpFlushTLB(
    VOID
);

/* pic.c */
VOID
NTAPI
HalpInitializePICs(
    IN BOOLEAN EnableInterrupts
);

/* pcibus.c */
INIT_FUNCTION
NTSTATUS
NTAPI
HalpSetupPciDeviceForDebugging(
    IN PVOID LoaderBlock,
    IN OUT PDEBUG_DEVICE_DESCRIPTOR PciDevice
);

INIT_FUNCTION
NTSTATUS
NTAPI
HalpReleasePciDeviceForDebugging(
    IN OUT PDEBUG_DEVICE_DESCRIPTOR PciDevice
);

ULONG
NTAPI
HaliPciInterfaceReadConfig(
    IN IN PVOID Context,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
);

ULONG
NTAPI
HaliPciInterfaceWriteConfig(
    IN IN PVOID Context,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
);

/* sysinfo.c */
NTSTATUS
NTAPI
HaliQuerySystemInformation(
    IN HAL_QUERY_INFORMATION_CLASS InformationClass,
    IN ULONG BufferSize,
    IN OUT PVOID Buffer,
    OUT PULONG ReturnedLength
);

NTSTATUS
NTAPI
HaliSetSystemInformation(
    IN HAL_SET_INFORMATION_CLASS InformationClass,
    IN ULONG BufferSize,
    IN OUT PVOID Buffer
);

/* EOF */
