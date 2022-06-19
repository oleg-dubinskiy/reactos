
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* Conversion functions */
#define BCD_INT(bcd)  (((bcd & 0xF0) >> 4) * 10 + (bcd & 0x0F))
#define INT_BCD(int)  (UCHAR)(((int / 10) << 4) + (int % 10))

/* GLOBALS *******************************************************************/


/* PRIVATE FUNCTIONS *********************************************************/

UCHAR
NTAPI
HalpReadCmos(IN UCHAR Reg)
{
    /* Select the register */
    WRITE_PORT_UCHAR(CMOS_CONTROL_PORT, Reg);

    /* Query the value */
    return READ_PORT_UCHAR(CMOS_DATA_PORT);
}

VOID
NTAPI
HalpWriteCmos(IN UCHAR Reg,
              IN UCHAR Value)
{
    /* Select the register */
    WRITE_PORT_UCHAR(CMOS_CONTROL_PORT, Reg);

    /* Write the value */
    WRITE_PORT_UCHAR(CMOS_DATA_PORT, Value);
}

/* PUBLIC FUNCTIONS **********************************************************/

ARC_STATUS
NTAPI
HalGetEnvironmentVariable(
    _In_ PCH Name,
    _In_ USHORT ValueLength,
    _Out_writes_z_(ValueLength) PCH Value)
{
    UCHAR Val;

    /* Only variable supported on x86 */
    if (_stricmp(Name, "LastKnownGood"))
        return ENOENT;

    HalpAcquireCmosSpinLock();
    Val = HalpReadCmos(RTC_REGISTER_B); // Query the current value
    HalpReleaseCmosSpinLock();

    /* Check the flag */
    if (Val & 0x01)
        strncpy(Value, "FALSE", ValueLength); // out "FALSE"
    else
        strncpy(Value, "TRUE", ValueLength); // out "TRUE"

    return ESUCCESS;
}

ARC_STATUS
NTAPI
HalSetEnvironmentVariable(
    _In_ PCH Name,
    _In_ PCH Value)
{
    UCHAR Val;

    /* Only variable supported on x86 */
    if (_stricmp(Name, "LastKnownGood"))
        return ENOMEM;

    /* Check if this is true or false */
    if (!_stricmp(Value, "TRUE"))
    {
        /* It's true, acquire CMOS lock */
        HalpAcquireCmosSpinLock();

        /* Read the current value and add the flag */
        Val = HalpReadCmos(RTC_REGISTER_B) | 1;
    }
    else if (!_stricmp(Value, "FALSE"))
    {
        /* It's false, acquire CMOS lock */
        HalpAcquireCmosSpinLock();

        /* Read the current value and mask out  the flag */
        Val = HalpReadCmos(RTC_REGISTER_B) & ~1;
    }
    else
    {
        DPRINT1("HalSetEnvironmentVariable: Fail\n");
        return ENOMEM;
    }

    /* Write new value */
    HalpWriteCmos(RTC_REGISTER_B, Val);

    /* Release the lock and return success */
    HalpReleaseCmosSpinLock();
    return ESUCCESS;
}

BOOLEAN
NTAPI
HalQueryRealTimeClock(
    _Out_ PTIME_FIELDS Time)
{
    HalpAcquireCmosSpinLock();

    /* Loop while update is in progress */
    while ((HalpReadCmos(RTC_REGISTER_A)) & RTC_REG_A_UIP)
        ;

    /* Set the time data */
    Time->Second = BCD_INT(HalpReadCmos(0));
    Time->Minute = BCD_INT(HalpReadCmos(2));
    Time->Hour = BCD_INT(HalpReadCmos(4));
    Time->Weekday = BCD_INT(HalpReadCmos(6));
    Time->Day = BCD_INT(HalpReadCmos(7));
    Time->Month = BCD_INT(HalpReadCmos(8));
    Time->Year = BCD_INT(HalpReadCmos(9));
    Time->Milliseconds = 0;

    /* FIXME: Check century byte */

    /* Compensate for the century field */
    Time->Year += ((Time->Year > 80) ? 1900: 2000);

    HalpReleaseCmosSpinLock();
    return TRUE;
}

/* EOF */
