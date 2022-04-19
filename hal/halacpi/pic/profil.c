
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

BOOLEAN HalpProfilingStopped = TRUE;
//UCHAR HalpProfileRate = 8;

/* FUNCTIONS *****************************************************************/

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
