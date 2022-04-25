
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

/* CMOS Registers and Ports */
#define CMOS_CONTROL_PORT       (PUCHAR)0x0070
#define CMOS_DATA_PORT          (PUCHAR)0x0071
#define RTC_REGISTER_A          0x0A
#define RTC_REG_A_UIP           0x80
#define RTC_REGISTER_B          0x0B
#define RTC_REG_B_PI            0x40
//#define RTC_REGISTER_C          0x0C
//#define RTC_REG_C_IRQ           0x80
//#define RTC_REGISTER_D          0x0D
//#define RTC_REGISTER_CENTURY    0x32

/* bios.c */
BOOLEAN
NTAPI
HalpBiosDisplayReset(
    VOID
);

/* cmos.c */
INIT_FUNCTION
VOID
NTAPI
HalpInitializeCmos(
    VOID
);

UCHAR
NTAPI
HalpReadCmos(
    IN UCHAR Reg
);

VOID
NTAPI
HalpWriteCmos(
    IN UCHAR Reg,
    IN UCHAR Value
);

/* dma.c */
PDMA_ADAPTER
NTAPI
HalpGetDmaAdapter(
    IN PVOID Context,
    IN PDEVICE_DESCRIPTION DeviceDescriptor,
    OUT PULONG NumberOfMapRegisters
);

NTSTATUS
NTAPI
HalpAllocateMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Unknown,
    _In_ ULONG Unknown2,
    PMAP_REGISTER_ENTRY Registers
);

VOID
NTAPI
HaliLocateHiberRanges(
    _In_ PVOID MemoryMap
);

INIT_FUNCTION
VOID
NTAPI
HalpInitDma(
    VOID
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

/* pic.c */
VOID
NTAPI
HalpInitializePICs(
    IN BOOLEAN EnableInterrupts
);

/* processor.c */
ULONG
NTAPI
HalpGetFeatureBits(
    VOID
);

/* spinlock.c */
VOID
NTAPI
HalpAcquireCmosSpinLock(
    VOID
);

VOID
NTAPI
HalpReleaseCmosSpinLock(
    VOID
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

/* timer.c */
INIT_FUNCTION
VOID
NTAPI
HalpInitializeClock(
    VOID
);

/* usage.c */
INIT_FUNCTION
VOID
NTAPI
HalpRegisterVector(
    IN UCHAR Flags,
    IN ULONG BusVector,
    IN ULONG SystemVector,
    IN KIRQL Irql
);

/* EOF */
