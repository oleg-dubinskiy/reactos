
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include "apic.h"

/* HAL profiling offsets in KeGetPcr()->HalReserved[] */
#define HAL_PROFILING_INTERVAL      0
#define HAL_PROFILING_MULTIPLIER    1

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

ULONG_PTR
NTAPI
HalSetProfileInterval(
    _In_ ULONG_PTR Interval)
{
    ULONGLONG TimerInterval;
    ULONGLONG FixedInterval;

    FixedInterval = (ULONGLONG)Interval;

    /* Check bounds */
    if (FixedInterval < HalMinProfileInterval)
    {
        FixedInterval = HalMinProfileInterval;
    }
    else if (FixedInterval > HalMaxProfileInterval)
    {
        FixedInterval = HalMaxProfileInterval;
    }

    /* Remember interval */
    HalCurProfileInterval = FixedInterval;

    /* Recalculate interval for APIC */
    TimerInterval = (FixedInterval * KeGetPcr()->HalReserved[HAL_PROFILING_MULTIPLIER] / HalMaxProfileInterval);

    /* Remember recalculated interval in PCR */
    KeGetPcr()->HalReserved[HAL_PROFILING_INTERVAL] = (ULONG)TimerInterval;

    /* And set it */
    ApicWrite(APIC_TICR, (ULONG)TimerInterval);

    return Interval;
}
/* EOF */