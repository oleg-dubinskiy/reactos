
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

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

/* EOF */
