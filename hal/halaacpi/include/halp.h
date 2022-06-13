
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

/* EOF */
