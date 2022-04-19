
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
//ULONG HalpNextMSRate = 14;
ULONG HalpLargestClockMS = 15;

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

/* PUBLIC FUNCTIONS ***********************************************************/


/* EOF */
