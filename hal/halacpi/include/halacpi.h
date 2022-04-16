
#pragma once


INIT_FUNCTION
PVOID
NTAPI
HalAcpiGetTable(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN ULONG Signature
);

INIT_FUNCTION
VOID
NTAPI
HalpCheckPowerButton(
    VOID
);

/* EOF */
