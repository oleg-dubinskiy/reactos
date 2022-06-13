
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

UCHAR
HalpIRQLtoTPR[32] =
{
    0x00, /*  0 PASSIVE_LEVEL */
    0x3d, /*  1 APC_LEVEL */
    0x41, /*  2 DISPATCH_LEVEL */
    0x41, /*  3 \  */
    0x51, /*  4  \ */
    0x61, /*  5  | */
    0x71, /*  6  | */
    0x81, /*  7  | */
    0x91, /*  8  | */
    0xa1, /*  9  | */
    0xb1, /* 10  | */
    0xb1, /* 11  | */
    0xb1, /* 12  | */
    0xb1, /* 13  | */
    0xb1, /* 14  | */
    0xb1, /* 15 DEVICE IRQL */
    0xb1, /* 16  | */
    0xb1, /* 17  | */
    0xb1, /* 18  | */
    0xb1, /* 19  | */
    0xb1, /* 20  | */
    0xb1, /* 21  | */
    0xb1, /* 22  | */
    0xb1, /* 23  | */
    0xb1, /* 24  | */
    0xb1, /* 25  / */
    0xb1, /* 26 /  */
    0xc1, /* 27 PROFILE_LEVEL */
    0xd1, /* 28 CLOCK2_LEVEL */
    0xe1, /* 29 IPI_LEVEL */
    0xef, /* 30 POWER_LEVEL */
    0xff, /* 31 HIGH_LEVEL */
};

KIRQL
HalVectorToIRQL[16] =
{
       0, /* 00 PASSIVE_LEVEL */
    0xff, /* 10 */
    0xff, /* 20 */
       1, /* 3D APC_LEVEL */
       2, /* 41 DISPATCH_LEVEL */
       4, /* 50 \ */
       5, /* 60  \ */
       6, /* 70  | */
       7, /* 80 DEVICE IRQL */
       8, /* 90  | */
       9, /* A0  / */
      10, /* B0 /  */
      27, /* C1 PROFILE_LEVEL */
      28, /* D1 CLOCK2_LEVEL */
      29, /* E1 IPI_LEVEL / EF POWER_LEVEL */
      31, /* FF HIGH_LEVEL */
};

/* For 0x50..0xBF vectors IRQLs values saves dynamically in HalpAllocateSystemInterruptVector() */
KIRQL HalpVectorToIRQL[16] =
{
    0x00, /* 00 PASSIVE_LEVEL */
    0xFF, /* 10 */
    0xFF, /* 20 */
    0x01, /* 3D APC_LEVEL */
    0x02, /* 41 DISPATCH_LEVEL */
    0xFF, /* 50 \ */
    0xFF, /* 60  \ */
    0xFF, /* 70  | */
    0xFF, /* 80 DEVICE IRQL */
    0xFF, /* 90  | */
    0xFF, /* A0  / */
    0xFF, /* B0 /  */
    0x1B, /* C1 PROFILE_LEVEL */
    0x1C, /* D1 CLOCK2_LEVEL */
    0x1D, /* E1 IPI_LEVEL / EF POWER_LEVEL */
    0x1F, /* FF HIGH_LEVEL */
};

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
    PHYSICAL_ADDRESS PhAddress;
    ULONG_PTR LocalApicBaseVa;
    ULONG ix;

    LoaderBlock->Extension->HalpIRQLToTPR = HalpIRQLtoTPR;
    LoaderBlock->Extension->HalpVectorToIRQL = HalpVectorToIRQL;

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

    if (HalpMpInfoTable.ProcessorCount > 1)
        *OutIsMpSystem = TRUE;

    HalpMpInfoTable.LocalApicPA = HalpApicTable->Address;
    PhAddress.QuadPart = HalpMpInfoTable.LocalApicPA;

    LocalApicBaseVa = (ULONG_PTR)HalpMapPhysicalMemoryWriteThrough64(PhAddress, 1);

    HalpRemapVirtualAddress64((PVOID)LOCAL_APIC_BASE, PhAddress, 1);

    if (*(volatile PUCHAR)(LocalApicBaseVa + APIC_VER) > 0x1F)
    {
        HalDisplayString("HAL: Bad APIC Version");
        return FALSE;
    }

    for (ix = 0; ix < HalpMpInfoTable.IoApicCount; ix++)
    {
        if (!HalpVerifyIOUnit((PIO_APIC_REGISTERS)HalpMpInfoTable.IoApicVA[ix]))
        {
            HalDisplayString("HAL: APIC not verified");
            return FALSE;
        }
    }

    HalDisplayString("HAL: DetectAPIC: APIC system found - Returning TRUE\n");
    return TRUE;
}

/* EOF */
