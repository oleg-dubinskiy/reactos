
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

UCHAR HalpCmosCenturyOffset = 0;
extern FADT HalpFixedAcpiDescTable;

/* PRIVATE FUNCTIONS *********************************************************/

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
