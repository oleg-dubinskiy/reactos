
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include "apic.h"
#include "pic.h"

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalInitializeProcessor)
  #pragma alloc_text(INIT, HalInitSystem)
#endif

/* GLOBALS *******************************************************************/

PKPCR HalpProcessorPCR[32];
KAFFINITY HalpActiveProcessors;
KAFFINITY HalpDefaultInterruptAffinity;

/* PRIVATE FUNCTIONS *********************************************************/


/* FUNCTIONS *****************************************************************/

INIT_FUNCTION
VOID
NTAPI
HalInitializeProcessor(
    IN ULONG ProcessorNumber,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKPCR Pcr = KeGetPcr();
    //LONG Comparand;
    //LONG Exchange = -1;
    BOOLEAN IsMpSystem;
    BOOLEAN Result;

    Pcr->IDR = 0xFFFFFFFF;                         /* Set default IDR */
    Pcr->StallScaleFactor = INITIAL_STALL_COUNT;   /* Set default stall count */

    *(PUCHAR)(Pcr->HalReserved) = ProcessorNumber; // FIXME
    HalpProcessorPCR[ProcessorNumber] = Pcr;

    /* Update the processors mask */
    InterlockedBitTestAndSet((PLONG)&HalpActiveProcessors, ProcessorNumber);

#if 0 /* Support '/INTAFFINITY' key in boot.ini */

    for (Comparand = HalpDefaultInterruptAffinity;
         Comparand != Exchange;
         )
    {
        if (!HalpStaticIntAffinity)
            Exchange = Comparand;
        else
            Exchange = 0;

        BitTestAndSet(&Exchange, ProcessorNumber);

        if (Exchange < Comparand)
            /* If true 'INTAFFINITY' key, then exit if InterruptAffinity != 0 */
            break;

        /* Update the interrupt affinity */
        Comparand = InterlockedCompareExchange((PLONG)&HalpDefaultInterruptAffinity, Exchange, Comparand);
    }
#else
    /* By default, the HAL allows interrupt requests to be received by all processors */
    InterlockedBitTestAndSet((PLONG)&HalpDefaultInterruptAffinity, ProcessorNumber);
#endif

    if (ProcessorNumber == 0)
    {
        Result = DetectAcpiMP(&IsMpSystem, KeLoaderBlock);
        if (!Result)
        {
            HalDisplayString("\n\nHAL: This HAL.DLL requires an MPS version 1.1 system\nReplace HAL.DLL with the correct hal for this system\nThe system is halting");
            __halt();
        }

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

        //HalpGlobal8259Mask = 0xFFFF; // FIXME

        WRITE_PORT_UCHAR(PIC1_DATA_PORT, 0xFF);
        WRITE_PORT_UCHAR(PIC2_DATA_PORT, 0xFF);
    }

    HalInitApicInterruptHandlers();

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
