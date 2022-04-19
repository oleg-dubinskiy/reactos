
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

UCHAR HalpCmosCenturyOffset = 0;
extern FADT HalpFixedAcpiDescTable;

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

VOID
NTAPI
HalpInitializeCmos(VOID)
{
    /* Set default century offset byte */
    if (HalpFixedAcpiDescTable.century_alarm_index)
    {
        HalpCmosCenturyOffset = HalpFixedAcpiDescTable.century_alarm_index;
    }
    else
    {
        HalpCmosCenturyOffset = 50;
    }
}

/* PUBLIC FUNCTIONS **********************************************************/


/* EOF */
