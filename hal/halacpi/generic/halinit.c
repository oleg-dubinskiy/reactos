
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
HALP_DMA_MASTER_ADAPTER MasterAdapter24;
HALP_DMA_MASTER_ADAPTER MasterAdapter32;
LIST_ENTRY HalpDmaAdapterList;
KSPIN_LOCK HalpDmaAdapterListLock;
KSPIN_LOCK HalpSystemHardwareLock;
ULONG HalpBusType;

ADDRESS_USAGE HalpDefaultIoSpace =
{
    NULL, CmResourceTypePort, IDT_INTERNAL,
    {
        {0x00,  0x10}, /* DMA 1 */
        {0xC0,  0x10}, /* DMA 2 */
        {0x80,  0x10}, /* DMA EPAR */
        {0x20,  0x2},  /* PIC 1 */
        {0xA0,  0x2},  /* PIC 2 */
        {0x40,  0x4},  /* PIT 1 */
        {0x48,  0x4},  /* PIT 2 */
        {0x92,  0x1},  /* System Control Port A */
        {0x70,  0x2},  /* CMOS  */
        {0xF0,  0x10}, /* x87 Coprocessor */
        {0xCF8, 0x8},  /* PCI 0 */
        {0,0},
    }
};

ADDRESS_USAGE HalpEisaIoSpace =
{
    NULL, CmResourceTypePort, IDT_INTERNAL,
    {
        {0xD0,  0x10}, /*  */
        {0x400, 0x10}, /*  */
        {0x480, 0x10}, /*  */
        {0x4C2, 0xE},  /*  */
        {0x4D4, 0x2C}, /*  */
        {0x461, 0x2},  /*  */
        {0x464, 0x2},  /*  */
        {0x4D0, 0x2},  /*  */
        {0xC84, 0x1},  /*  */
        {0,0},
    }
};

PADDRESS_USAGE HalpAddressUsageList;

BOOLEAN HalpPciLockSettings;
BOOLEAN LessThan16Mb = TRUE;
BOOLEAN HalpPhysicalMemoryMayAppearAbove4GB = FALSE;

/* PRIVATE FUNCTIONS *********************************************************/

/* FIXME for other types of HALs there are other options. */
INIT_FUNCTION
VOID
NTAPI
HalpGetParameters(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PCHAR CommandLine;

    /* Make sure we have a loader block and command line */
    if (!LoaderBlock || !LoaderBlock->LoadOptions)
    {
        DPRINT1("HalpGetParameters: error ... \n");
        return;
    }

    /* Read the command line */
    CommandLine = LoaderBlock->LoadOptions;

    /* Check if PCI is locked */
    if (strstr(CommandLine, "PCILOCK"))
        HalpPciLockSettings = TRUE;

    /* Check for initial breakpoint */
    if (strstr(CommandLine, "BREAK"))
    {
        DPRINT1("HalpGetParameters: initial breakpoint [BREAK]\n");
        DbgBreakPoint();
    }
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

INIT_FUNCTION
BOOLEAN
NTAPI
HalInitSystem(IN ULONG BootPhase,
              IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    KIRQL OldIrql;

    DPRINT1("HalInitSystem: Phase - %X, Processor - %X\n", BootPhase, Prcb->Number);

    /* Check the boot phase */
    if (BootPhase == 0)
    {
        /* Phase 0... save bus type */
        HalpBusType = LoaderBlock->u.I386.MachineType & 0xFF;

        /* Get command-line parameters */
        HalpGetParameters(LoaderBlock);

        /* Check for PRCB version mismatch */
        if (Prcb->MajorVersion != PRCB_MAJOR_VERSION) {
            /* No match, bugcheck */
            DPRINT1("HalInitSystem: MajorVersion %X - (%X)\n", Prcb->MajorVersion, PRCB_MAJOR_VERSION);
            KeBugCheckEx(MISMATCHED_HAL, 1, Prcb->MajorVersion, PRCB_MAJOR_VERSION, 0);
        }

      #if DBG 
        if ((Prcb->BuildType & PRCB_BUILD_DEBUG) == 0) {
            DPRINT1("HalInitSystem: BuildType %X - (%X)\n", Prcb->BuildType, PRCB_BUILD_DEBUG);
            KeBugCheckEx(MISMATCHED_HAL, 2, Prcb->BuildType, PRCB_BUILD_DEBUG, 0);
        }
        if ((Prcb->BuildType & PRCB_BUILD_UNIPROCESSOR) == PRCB_BUILD_UNIPROCESSOR) {
            DPRINT1("HalInitSystem: BuildType %X - (%X)\n", Prcb->BuildType, PRCB_BUILD_UNIPROCESSOR);
            KeBugCheckEx(MISMATCHED_HAL, 2, Prcb->BuildType, 0, 0);
        }
      #else 
        if ((Prcb->BuildType & PRCB_BUILD_DEBUG) == PRCB_BUILD_DEBUG)
        {
            DPRINT1("HalInitSystem: BuildType %X - (%X)\n", Prcb->BuildType, PRCB_BUILD_DEBUG);
            KeBugCheckEx(MISMATCHED_HAL, 2, Prcb->BuildType, PRCB_BUILD_DEBUG, 0);
        }
      #endif

        /* Do some HAL-specific initialization */ // HalpInitPhase0_0
        if (HalpBusType == MACHINE_TYPE_MCA) {
            DPRINT1("HalInitSystem: HalpBusType %X - (%X)\n", HalpBusType, MACHINE_TYPE_MCA);
            KeBugCheckEx(MISMATCHED_HAL, 3, MACHINE_TYPE_MCA, 0, 0);
        }

        /* Initialize ACPI */
        HalpSetupAcpiPhase0(LoaderBlock);

        /* Initialize the PICs */
        HalpInitializePICs(TRUE);

        OldIrql = KeGetCurrentIrql();
        KfRaiseIrql(OldIrql);

        /* Initialize CMOS lock */
        KeInitializeSpinLock(&HalpSystemHardwareLock);

        /* Initialize CMOS */
        HalpInitializeCmos();

        /* Fill out HalDispatchTable */
        HalQuerySystemInformation = HaliQuerySystemInformation;
        HalSetSystemInformation = HaliSetSystemInformation;
        HalInitPnpDriver = HaliInitPnpDriver;
        HalGetDmaAdapter = HalpGetDmaAdapter; // HaliGetDmaAdapter
        HalGetInterruptTranslator = HalacpiGetInterruptTranslator;
        HalInitPowerManagement = HalacpiInitPowerManagement;

        /* Fill out HalPrivateDispatchTable */
        HalResetDisplay = HalpBiosDisplayReset;
        HalAllocateMapRegisters = HalpAllocateMapRegisters;
        HalLocateHiberRanges = HaliLocateHiberRanges;
        HalHaltSystem = HaliHaltSystem;

        DPRINT1("HalInitSystem: FIXME! HalDispatchTable ...\n");
        ASSERT(0);// HalpDbgBreakPointEx();


    }
    else if (BootPhase == 1)
    {
        DPRINT1("HalInitSystem: FIXME! BootPhase %X, Processor %X\n", BootPhase, Prcb->Number);
        UNIMPLEMENTED;
        ASSERT(0);// HalpDbgBreakPointEx();
    }

    /* All done, return */
    return TRUE;
}

/* EOF */
