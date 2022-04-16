
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalInitializeProcessor)
  #pragma alloc_text(INIT, HalInitSystem)
#endif

/* GLOBALS *******************************************************************/

KAFFINITY HalpActiveProcessors;
KAFFINITY HalpDefaultInterruptAffinity;

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
HalpRegisterKdSupportFunctions(VOID)
{
    /* Register PCI Device Functions */
    KdSetupPciDeviceForDebugging = HalpSetupPciDeviceForDebugging;
    KdReleasePciDeviceforDebugging = HalpReleasePciDeviceForDebugging;

    /* Register ACPI stub */
    KdGetAcpiTablePhase0 = HalAcpiGetTable;
    KdCheckPowerButton = HalpCheckPowerButton;

    /* Register memory functions */
    KdMapPhysicalMemory64 = HalpMapPhysicalMemory64;
    KdUnmapVirtualAddress = HalpUnmapVirtualAddress;
}

/* FUNCTIONS *****************************************************************/

INIT_FUNCTION
VOID
NTAPI
HalInitializeProcessor(
    IN ULONG ProcessorNumber,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* Set default IDR */
    KeGetPcr()->IDR = 0xFFFFFFFB;

    /* Set default stall count */
    KeGetPcr()->StallScaleFactor = INITIAL_STALL_COUNT;

    /* Update the interrupt affinity and processor mask */
    InterlockedBitTestAndSet((PLONG)&HalpActiveProcessors, ProcessorNumber);
    InterlockedBitTestAndSet((PLONG)&HalpDefaultInterruptAffinity, ProcessorNumber);

    /* Register routines for KDCOM */
    HalpRegisterKdSupportFunctions();
}

INIT_FUNCTION
BOOLEAN
NTAPI
HalInitSystem(IN ULONG BootPhase,
              IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNIMPLEMENTED;
    return TRUE;
}

/* EOF */
