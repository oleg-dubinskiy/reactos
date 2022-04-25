
/* INCLUDES *******************************************************************/

#include <hal.h>
#include "pic.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

extern ULONG HalpBusType;

/* IRQL MANAGEMENT ************************************************************/

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    /* Return the IRQL */
    return KeGetPcr()->Irql;
}

KIRQL
FASTCALL
KfRaiseIrql(IN KIRQL NewIrql)
{
    PKPCR Pcr = KeGetPcr();
    KIRQL CurrentIrql;

    /* Read current IRQL */
    CurrentIrql = Pcr->Irql;

#if DBG
    /* Validate correct raise */
    if (CurrentIrql > NewIrql)
    {
        /* Crash system */
        Pcr->Irql = PASSIVE_LEVEL;
        KeBugCheck(IRQL_NOT_GREATER_OR_EQUAL);
    }
#endif

    /* Set new IRQL */
    Pcr->Irql = NewIrql;

    /* Return old IRQL */
    return CurrentIrql;
}

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
HalpInitializeLegacyPICs(BOOLEAN InterruptMode)
{
    I8259_ICW1 Icw1;
    I8259_ICW2 Icw2;
    I8259_ICW3 Icw3;
    I8259_ICW4 Icw4;

    ASSERT(!(__readeflags() & EFLAGS_INTERRUPT_MASK));

    /* Initialize ICW1 for master, interval 8 */
    Icw1.NeedIcw4 = TRUE;
    Icw1.OperatingMode = Cascade;
    Icw1.Interval = Interval8;
    //Icw1.InterruptMode = EdgeTriggered;
    Icw1.InterruptMode = InterruptMode ? LevelTriggered : EdgeTriggered;
    Icw1.Init = TRUE;
    Icw1.InterruptVectorAddress = 0;
    WRITE_PORT_UCHAR(PIC1_CONTROL_PORT, Icw1.Bits);

    /* ICW2 - interrupt vector offset */
    Icw2.Bits = PRIMARY_VECTOR_BASE;
    WRITE_PORT_UCHAR(PIC1_DATA_PORT, Icw2.Bits);

    /* Connect slave to IRQ 2 */
    Icw3.Bits = 0;
    Icw3.SlaveIrq2 = TRUE;
    WRITE_PORT_UCHAR(PIC1_DATA_PORT, Icw3.Bits);

    /* Enable 8086 mode, non-automatic EOI, non-buffered mode, non special fully nested mode */
    Icw4.SystemMode = New8086Mode;
    Icw4.EoiMode = NormalEoi;
    Icw4.BufferedMode = NonBuffered;
    Icw4.SpecialFullyNestedMode = FALSE;
    Icw4.Reserved = 0;
    WRITE_PORT_UCHAR(PIC1_DATA_PORT, Icw4.Bits);

    /* Mask all interrupts */
    WRITE_PORT_UCHAR(PIC1_DATA_PORT, 0xFF);

    /* Initialize ICW1 for slave, interval 8 */
    Icw1.NeedIcw4 = TRUE;
    //Icw1.InterruptMode = EdgeTriggered;
    Icw1.InterruptMode = InterruptMode ? LevelTriggered : EdgeTriggered;
    Icw1.OperatingMode = Cascade;
    Icw1.Interval = Interval8;
    Icw1.Init = TRUE;
    Icw1.InterruptVectorAddress = 0; /* This is only used in MCS80/85 mode */
    WRITE_PORT_UCHAR(PIC2_CONTROL_PORT, Icw1.Bits);

    /* Set interrupt vector base */
    Icw2.Bits = PRIMARY_VECTOR_BASE + 8;
    WRITE_PORT_UCHAR(PIC2_DATA_PORT, Icw2.Bits);

    /* Slave ID */
    Icw3.Bits = 0;
    Icw3.SlaveId = 2;
    WRITE_PORT_UCHAR(PIC2_DATA_PORT, Icw3.Bits);

    /* Enable 8086 mode, non-automatic EOI, non-buffered mode, non special fully nested mode */
    Icw4.SystemMode = New8086Mode;
    Icw4.EoiMode = NormalEoi;
    Icw4.BufferedMode = NonBuffered;
    Icw4.SpecialFullyNestedMode = FALSE;
    Icw4.Reserved = 0;
    WRITE_PORT_UCHAR(PIC2_DATA_PORT, Icw4.Bits);

    /* Mask all interrupts */
    WRITE_PORT_UCHAR(PIC2_DATA_PORT, 0xFF);
}

VOID
NTAPI
HalpInitializePICs(IN BOOLEAN EnableInterrupts)
{
    ULONG EFlags;
    BOOLEAN InterruptMode = FALSE; // EdgeTriggered

    DPRINT1("HalpInitializePICs: EnableInterrupts - %X\n", EnableInterrupts);

    /* Save EFlags and disable interrupts */
    EFlags = __readeflags();
    _disable();

    if (HalpBusType & 2)
    {
        DPRINT1("HalpInitializePICs: HalpBusType - %X\n", HalpBusType);
         ASSERT(0);//DbgBreakPoint();
        //InterruptMode = TRUE; // LevelTriggered
    }

    DPRINT1("HalpInitializePICs: InterruptMode - %X\n", InterruptMode);

    /* Initialize and mask the PIC */
    HalpInitializeLegacyPICs(InterruptMode);

    /* Restore interrupt state */
    if (EnableInterrupts)
        EFlags |= EFLAGS_INTERRUPT_MASK;

    __writeeflags(EFlags);
}

/* PUBLIC FUNCTIONS **********************************************************/

BOOLEAN
NTAPI
HalBeginSystemInterrupt(IN KIRQL Irql,
                        IN ULONG Vector,
                        OUT PKIRQL OldIrql)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return FALSE;
}

VOID
FASTCALL
HalClearSoftwareInterrupt(IN KIRQL Irql)
{
    /* Mask out the requested bit */
    KeGetPcr()->IRR &= ~(1 << Irql);
}

VOID
NTAPI
HalDisableSystemInterrupt(IN ULONG Vector,
                          IN KIRQL Irql)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

BOOLEAN
NTAPI
HalEnableSystemInterrupt(IN ULONG Vector,
                         IN KIRQL Irql,
                         IN KINTERRUPT_MODE InterruptMode)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return FALSE;
}

/* NT use nonstandard parameters calling */
VOID
NTAPI
HalEndSystemInterrupt(_In_ KIRQL Irql,
                      _In_ PKTRAP_FRAME TrapFrame)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

VOID
FASTCALL
HalRequestSoftwareInterrupt(IN KIRQL Irql)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

VOID
FASTCALL
KfLowerIrql(IN KIRQL OldIrql)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID)
{
    PKPCR Pcr = KeGetPcr();
    KIRQL CurrentIrql;

    /* Save and update IRQL */
    CurrentIrql = Pcr->Irql;
    Pcr->Irql = DISPATCH_LEVEL;

#if DBG
    /* Validate correct raise */
    if (CurrentIrql > DISPATCH_LEVEL)
        KeBugCheck(IRQL_NOT_GREATER_OR_EQUAL);
#endif

    /* Return the previous value */
    return CurrentIrql;
}

/* EOF */
