
#pragma once

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

/* memory.c */
INIT_FUNCTION
PVOID
NTAPI
HalpMapPhysicalMemory64(
    IN PHYSICAL_ADDRESS PhysicalAddress,
    IN PFN_COUNT PageCount
);

INIT_FUNCTION
VOID
NTAPI
HalpUnmapVirtualAddress(
    IN PVOID VirtualAddress,
    IN PFN_COUNT NumberPages
);

/*  pcibus.c */
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


/* EOF */
