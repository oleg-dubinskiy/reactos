
#pragma once

/* Internal HAL structure */
typedef struct _ACPI_CACHED_TABLE
{
    LIST_ENTRY Links;
    DESCRIPTION_HEADER Header;
    // table follows
    // ...
} ACPI_CACHED_TABLE, *PACPI_CACHED_TABLE;


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

INIT_FUNCTION
NTSTATUS
NTAPI
HalpSetupAcpiPhase0(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
);

VOID
NTAPI
HaliAcpiTimerInit(
    _In_ PULONG TimerPort,
    _In_ BOOLEAN TimerValExt
);

NTSTATUS
NTAPI
HalacpiGetInterruptTranslator(
    _In_ INTERFACE_TYPE ParentInterfaceType,
    _In_ ULONG ParentBusNumber,
    _In_ INTERFACE_TYPE BridgeInterfaceType,
    _In_ USHORT Size,
    _In_ USHORT Version,
    _Out_ PTRANSLATOR_INTERFACE Translator,
    _Out_ PULONG BridgeBusNumber
);

NTSTATUS
NTAPI
HalacpiInitPowerManagement(
    _In_ PPM_DISPATCH_TABLE PmDriverDispatchTable,
    _Out_ PPM_DISPATCH_TABLE * PmHalDispatchTable
);

/* EOF */
