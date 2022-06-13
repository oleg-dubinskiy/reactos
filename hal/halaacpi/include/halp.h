
#pragma once

/* Mm PTE/PDE to Hal PTE/PDE */
#define HalAddressToPde(x) (PHARDWARE_PTE)MiAddressToPde(x)
#define HalAddressToPte(x) (PHARDWARE_PTE)MiAddressToPte(x)

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

/* EOF */
