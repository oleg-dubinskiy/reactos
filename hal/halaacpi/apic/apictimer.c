
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include "apic.h"

extern LARGE_INTEGER HalpCpuClockFrequency;

/* HAL profiling variables */
BOOLEAN HalIsProfiling = FALSE;
ULONGLONG HalCurProfileInterval = 10000000;
ULONGLONG HalMinProfileInterval = 1000;
ULONGLONG HalMaxProfileInterval = 10000000;

/* TIMER FUNCTIONS ************************************************************/

VOID
NTAPI
HalStopProfileInterrupt(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    LVT_REGISTER LvtEntry;

    /* Only handle ProfileTime */
    if (ProfileSource == ProfileTime)
    {
        /* We are not profiling */
        HalIsProfiling = FALSE;

        /* Mask interrupt */
        LvtEntry.Long = 0;
        LvtEntry.TimerMode = 1;
        LvtEntry.Vector = APIC_PROFILE_VECTOR;
        LvtEntry.Mask = 1;
        ApicWrite(APIC_TMRLVTR, LvtEntry.Long);
    }
}

/* EOF */