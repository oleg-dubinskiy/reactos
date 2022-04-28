
/* INCLUDES ******************************************************************/

#include <hal.h>
#include "timer.h"

//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpInitializeClock)
#endif

/* GLOBALS *******************************************************************/

ULONG HalpCurrentTimeIncrement;
ULONG HalpCurrentRollOver;
ULONG HalpNextMSRate = 14;
ULONG HalpLargestClockMS = 15;
LARGE_INTEGER HalpPerfCounter;
ULONG HalpPerfCounterCutoff;
BOOLEAN HalpClockSetMSRate;

static struct _HALP_ROLLOVER
{
    ULONG RollOver;
    ULONG Increment;
} HalpRolloverTable[15] =
{
    {1197, 10032},
    {2394, 20064},
    {3591, 30096},
    {4767, 39952},
    {5964, 49984},
    {7161, 60016},
    {8358, 70048},
    {9555, 80080},
    {10731, 89936},
    {11949, 100144},
    {13125, 110000},
    {14322, 120032},
    {15519, 130064},
    {16695, 139920},
    {17892, 149952}
};

extern BOOLEAN HalpProfilingStopped;

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
HalpSetTimerRollOver(USHORT RollOver)
{
    ULONG_PTR Flags;
    TIMER_CONTROL_PORT_REGISTER TimerControl;

    DPRINT1("HalpSetTimerRollOver()\n");

    /* Disable interrupts */
    Flags = __readeflags();
    _disable();

    /* Program the PIT for binary mode */
    TimerControl.BcdMode = FALSE;

    /*
     * Program the PIT to generate a normal rate wave (Mode 3) on channel 0.
     * Channel 0 is used for the IRQ0 clock interval timer, and channel
     * 1 is used for DRAM refresh.
     *
     * Mode 2 gives much better accuracy than Mode 3.
     */
    TimerControl.OperatingMode = PitOperatingMode2;
    TimerControl.Channel = PitChannel0;

    /* Set the access mode that we'll use to program the reload value */
    TimerControl.AccessMode = PitAccessModeLowHigh;

    /* Now write the programming bits */
    WRITE_PORT_UCHAR(TIMER_CONTROL_PORT, TimerControl.Bits);

    /* Next we write the reload value for channel 0 */
    WRITE_PORT_UCHAR(TIMER_CHANNEL0_DATA_PORT, RollOver & 0xFF);
    WRITE_PORT_UCHAR(TIMER_CHANNEL0_DATA_PORT, RollOver >> 8);

    /* Restore interrupts if they were previously enabled */
    __writeeflags(Flags);
}

INIT_FUNCTION
VOID
NTAPI
HalpInitializeClock(VOID)
{
    ULONG Increment;
    USHORT RollOver;

    DPRINT1("HalpInitializeClock()\n");

    /* Get increment and rollover for the largest time clock ms possible */
    Increment = HalpRolloverTable[HalpLargestClockMS - 1].Increment;
    RollOver = (USHORT)HalpRolloverTable[HalpLargestClockMS - 1].RollOver;

    /* Set the maximum and minimum increment with the kernel */
    KeSetTimeIncrement(Increment, HalpRolloverTable[0].Increment);

    /* Set the rollover value for the timer */
    HalpSetTimerRollOver(RollOver);

    /* Save rollover and increment */
    HalpCurrentRollOver = RollOver;
    HalpCurrentTimeIncrement = Increment;
}

VOID
FASTCALL
HalpClockInterruptHandler(IN PKTRAP_FRAME TrapFrame)
{
    ULONG LastIncrement;
    KIRQL Irql;

    //DPRINT("HalpClockInterruptHandler: ... \n");

    /* Enter trap */
    KiEnterInterruptTrap(TrapFrame);

    /* Start the interrupt */
    if (!HalBeginSystemInterrupt(CLOCK2_LEVEL, (PRIMARY_VECTOR_BASE + 0), &Irql))
    {
        /* Spurious, just end the interrupt */
        KiEoiHelper(TrapFrame); // DECLSPEC_NORETURN
        return;
    }

    /* Update the performance counter */
    HalpPerfCounter.QuadPart += HalpCurrentRollOver;
    HalpPerfCounterCutoff = KiEnableTimerWatchdog;

    /* Save increment */
    LastIncrement = HalpCurrentTimeIncrement;

    /* Check if someone changed the time rate */
    if (HalpClockSetMSRate)
    {
        /* Update the global values */
        HalpCurrentTimeIncrement = HalpRolloverTable[HalpNextMSRate - 1].Increment;
        HalpCurrentRollOver = HalpRolloverTable[HalpNextMSRate - 1].RollOver;

        /* Set new timer rollover */
        HalpSetTimerRollOver((USHORT)HalpCurrentRollOver);

        /* We're done */
        HalpClockSetMSRate = FALSE;
    }

    /* Update the system time -- the kernel will exit this trap  */
    RosKeUpdateSystemTime(TrapFrame, LastIncrement, 0xFF, Irql);

    ASSERT(0);//HalpDbgBreakPointEx();
    KiEoiHelper(TrapFrame);
}

VOID
FASTCALL
HalpProfileInterruptHandler(IN PKTRAP_FRAME TrapFrame)
{
    KIRQL Irql;

    DPRINT("HalpProfileInterruptHandler: ... \n");

    /* Enter trap */
    KiEnterInterruptTrap(TrapFrame);

    /* Start the interrupt */
    if (!HalBeginSystemInterrupt(PROFILE_LEVEL, (PRIMARY_VECTOR_BASE + 8), &Irql))
    {
        /* Spurious, just end the interrupt */
        KiEoiHelper(TrapFrame); // DECLSPEC_NORETURN
        ASSERT(0);//HalpDbgBreakPointEx();
        return;
    }

    /* Spin until the interrupt pending bit is clear */
    HalpAcquireCmosSpinLock();
    while (HalpReadCmos(RTC_REGISTER_C) & RTC_REG_C_IRQ)
        ;
    HalpReleaseCmosSpinLock();

    /* If profiling is enabled, call the kernel function */
    if (!HalpProfilingStopped)
        KeProfileInterrupt(TrapFrame);

    /* Finish the interrupt */
    _disable();
  #ifdef __REACTOS__
    RosHalEndSystemInterrupt(Irql, 0xFF, TrapFrame);
  #else
    DPRINT1("KiEndInterrupt: FIXME! Irql %X, TrapFrame %X\n", Irql, TrapFrame);
    ASSERT(FALSE);// DbgBreakPoint();
    /* NT actually uses the stack to place the pointer to the TrapFrame (really the third parameter),
       but ... HalEndSystemInterrupt() is defined with only two parameters ...
    */
    // ?add before?:
    // _asm push (IntContext->TrapFrame)
    HalEndSystemInterrupt(Irql, 0xFF /*, TrapFrame*/); // FIXME (compatible with NT)
  #endif

  /* Exit the interrupt */
  #ifdef __REACTOS__
    KiEoiHelper(TrapFrame);
  #else
    DPRINT1("KiEndInterrupt: FIXME before calling Kei386EoiHelper()\n");
    ASSERT(FALSE);// DbgBreakPoint();
    /* NT uses non-standard call parameters */
    // ?add before?:
    // _asm mov ebp, (IntContext->TrapFrame)
    // _asm push ebp
    Kei386EoiHelper(/*TrapFrame*/); // FIXME (compatible with NT)
  #endif

    ASSERT(0);//HalpDbgBreakPointEx();
}

/* PUBLIC FUNCTIONS ***********************************************************/

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER CurrentPerfCounter = {{0,0}};
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return CurrentPerfCounter;
}

VOID
NTAPI
HalCalibratePerformanceCounter(IN volatile PLONG Count,
                               IN ULONGLONG NewCount)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

/* EOF */
