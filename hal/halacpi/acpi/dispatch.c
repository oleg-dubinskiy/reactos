
/* INCLUDES ******************************************************************/

#include <hal.h>
#include "dispatch.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

ACPI_PM_DISPATCH_TABLE HalAcpiDispatchTable =
{
    0x48414C20, // 'HAL '
    2,
    HaliAcpiTimerInit,
    NULL,
    HaliAcpiMachineStateInit,
    HaliAcpiQueryFlags,
    HalpAcpiPicStateIntact,
    HalpRestoreInterruptControllerState,
    HaliPciInterfaceReadConfig,
    HaliPciInterfaceWriteConfig,
    HaliSetVectorState,
    HalpGetApicVersion,
    HaliSetMaxLegacyPciBusNumber,
    HaliIsVectorValid 
};

HALP_TIMER_INFO TimerInfo;
BOOLEAN HalpBrokenAcpiTimer = FALSE;

extern FADT HalpFixedAcpiDescTable;

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
HalaAcpiTimerInit(_In_ PULONG TimerPort,
                  _In_ BOOLEAN IsTimerValExt)
{
    DPRINT("HalaAcpiTimerInit: Port %X, IsValExt %X\n", TimerPort, IsTimerValExt);

    RtlZeroMemory(&TimerInfo, sizeof(TimerInfo));

    TimerInfo.TimerPort = TimerPort;

    if (IsTimerValExt)
        TimerInfo.ValueExt = 0x80000000; // 32-bit
    else
        TimerInfo.ValueExt = 0x00800000; // 24-bit

    if (!HalpBrokenAcpiTimer)
    {
        DPRINT("HalpBrokenAcpiTimer - FALSE\n");
        return;
    }

    DPRINT1("HalaAcpiTimerInit: FIXME HalpQueryBrokenPiix4()\n");
    ASSERT(0); // HalpDbgBreakPointEx();

}

VOID
NTAPI
HaliAcpiTimerInit(_In_ PULONG TimerPort,
                  _In_ BOOLEAN TimerValExt)
{
    PAGED_CODE();

    DPRINT("HaliAcpiTimerInit: Port %X, ValExt %X\n", TimerPort, TimerValExt);
    DPRINT("HalpFixedAcpiDescTable.flags - %08X\n", HalpFixedAcpiDescTable.flags);

    /* Is this in the init phase? */
    if (!TimerPort)
    {
        /* Get the data from the FADT */

        /* System port address of the Power Management Timer Control Register Block */
        TimerPort = (PULONG)HalpFixedAcpiDescTable.pm_tmr_blk_io_port;

        /* A zero indicates TMR_VAL is implemented as a 24-bit value.
           A one indicates TMR_VAL is implemented as a 32-bit value.
        */
        TimerValExt = ((HalpFixedAcpiDescTable.flags & ACPI_TMR_VAL_EXT) != 0);
        DPRINT1("TimerPort %X, TimerValExt %X\n", TimerPort, TimerValExt);
    }

    /* FIXME: Now proceed to the timer initialization */
    HalaAcpiTimerInit(TimerPort, TimerValExt);
}


/* PM DISPATCH FUNCTIONS *****************************************************/

VOID
NTAPI
HaliAcpiMachineStateInit(_In_ ULONG Par1,
                         _In_ PVOID Par2,
                         _Out_ PVOID OutPar3)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

ULONG
NTAPI
HaliAcpiQueryFlags(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

UCHAR
NTAPI
HalpAcpiPicStateIntact(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

VOID
NTAPI
HalpRestoreInterruptControllerState(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

VOID
NTAPI
HaliSetVectorState(_In_ ULONG Par1,
                   _In_ ULONG Par2)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

ULONG
NTAPI
HalpGetApicVersion(_In_ ULONG Par1)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

VOID
NTAPI
HaliSetMaxLegacyPciBusNumber(_In_ ULONG MaxLegacyPciBusNumber)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

BOOLEAN
NTAPI
HaliIsVectorValid(_In_ ULONG Vector)
{
    DPRINT("HaliIsVectorValid: Vector %X\n", Vector);

    if (Vector >= 0x10)
    {
        DPRINT1("HaliIsVectorValid: Vector %X\n", Vector);
        //ASSERT(Vector < 0x10);
    }

    return (Vector < 0x10);
}

/* EOF */
