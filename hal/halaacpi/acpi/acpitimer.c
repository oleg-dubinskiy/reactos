
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PHALP_QUERY_TIMER QueryTimer = HalpQueryPerformanceCounter;
ULONG PMTimerFreq = 3579545;

HALP_TIMER_INFO TimerInfo;

/* PM TIMER FUNCTIONS *********************************************************/

VOID
NTAPI
HaliPmTimerQueryPerfCount(
    _Out_ LARGE_INTEGER* OutPerfCount,
    _Out_ LARGE_INTEGER* OutPerfFrequency)
{
    OutPerfCount->QuadPart = (TimerInfo.PerformanceCounter.QuadPart + QueryTimer());

    if (OutPerfFrequency)
        OutPerfFrequency->QuadPart = PMTimerFreq;
}

ULONGLONG
__cdecl
HalpQueryPerformanceCounter(VOID)
{
    LARGE_INTEGER TimeValue;
    LARGE_INTEGER PerfCounter;
    ULONG CurrentTime;
    ULONG ValueExt;

    do
    {
        YieldProcessor();
    }
    while (TimerInfo.AcpiTimeValue.HighPart != TimerInfo.TimerCarry);

    CurrentTime = READ_PORT_ULONG(TimerInfo.TimerPort);

    TimeValue = TimerInfo.AcpiTimeValue;
    ValueExt = TimerInfo.ValueExt;

    PerfCounter.HighPart = TimeValue.HighPart;
    PerfCounter.LowPart = ((CurrentTime & (~ValueExt)) | (TimeValue.LowPart & (~(ValueExt - 1))));

    PerfCounter.QuadPart += ((CurrentTime ^ TimeValue.LowPart) & ValueExt);

    return PerfCounter.QuadPart;
}

/* EOF */
