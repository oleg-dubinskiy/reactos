
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

BOOLEAN HalpProfilingStopped = TRUE;
UCHAR HalpProfileRate = 8;

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
HalStartProfileInterrupt(IN KPROFILE_SOURCE ProfileSource)
{
    UCHAR StatusA, StatusB;

    UNREFERENCED_PARAMETER(ProfileSource);

    HalpProfilingStopped = FALSE;

    /* Acquire the CMOS lock */
    HalpAcquireCmosSpinLock();

    /* Set the interval in Status Register A */
    StatusA = HalpReadCmos(RTC_REGISTER_A);
    StatusA = (StatusA & 0xF0) | HalpProfileRate;
    HalpWriteCmos(RTC_REGISTER_A, StatusA);

    /* Enable periodic interrupts in Status Register B */
    StatusB = HalpReadCmos(RTC_REGISTER_B);
    StatusB = StatusB | RTC_REG_B_PI;
    HalpWriteCmos(RTC_REGISTER_B, StatusB);

    /* Release the CMOS lock */
    HalpReleaseCmosSpinLock();
}

VOID
NTAPI
HalStopProfileInterrupt(IN KPROFILE_SOURCE ProfileSource)
{
    UCHAR StatusB;

    UNREFERENCED_PARAMETER(ProfileSource);

    /* Acquire the CMOS lock */
    HalpAcquireCmosSpinLock();

    /* Read Status Register B */
    StatusB = HalpReadCmos(RTC_REGISTER_B);

    /* Disable periodic interrupts */
    StatusB = StatusB & ~RTC_REG_B_PI;

    /* Write new value into Status Register B */
    HalpWriteCmos(RTC_REGISTER_B, StatusB);

    HalpProfilingStopped = TRUE;

    /* Release the CMOS lock */
    HalpReleaseCmosSpinLock();
}

/* EOF */
