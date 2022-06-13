
/* INCLUDES ******************************************************************/

#include <hal.h>

//#define NDEBUG
#include <debug.h>

#include "apic.h"
#include "apicacpi.h"

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DetectAcpiMP)
#endif

/* GLOBALS *******************************************************************/

HALP_MP_INFO_TABLE HalpMpInfoTable;
APIC_INTI_INFO HalpIntiInfo[MAX_INTI];
USHORT HalpMaxApicInti[MAX_IOAPICS];

/* PRIVATE FUNCTIONS *********************************************************/


/* FUNCTIONS *****************************************************************/

INIT_FUNCTION
BOOLEAN
NTAPI 
DetectAcpiMP(
    _Out_ PBOOLEAN OutIsMpSystem,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_TABLE_MADT HalpApicTable;

    RtlZeroMemory(&HalpMpInfoTable, sizeof(HalpMpInfoTable));

    *OutIsMpSystem = FALSE;

    HalpApicTable = HalAcpiGetTable(LoaderBlock, 'CIPA');
    if (!HalpApicTable)
    {
        HalDisplayString("HAL: No ACPI APIC Table Found\n");
        return FALSE;
    }

    HalpInitMpInfo(HalpApicTable, 0, LoaderBlock);
    if (!HalpMpInfoTable.IoApicCount)
    {
        HalDisplayString("HAL: No IO APIC Found");
        return FALSE;
    }


    HalDisplayString("HAL: DetectAPIC: APIC system found - Returning TRUE\n");
    return TRUE;
}

/* EOF */
