
/* INCLUDES ******************************************************************/

#include <hal.h>

//#define NDEBUG
#include <debug.h>

#include "apic.h"
#include "apicacpi.h"

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DetectAcpiMP)
  #pragma alloc_text(INIT, HalInitApicInterruptHandlers)
#endif

/* GLOBALS *******************************************************************/

KAFFINITY HalpNodeProcessorAffinity[MAX_CPUS] = {0};
HALP_MP_INFO_TABLE HalpMpInfoTable;
APIC_INTI_INFO HalpIntiInfo[MAX_INTI];
USHORT HalpMaxApicInti[MAX_IOAPICS];
UCHAR HalpIntDestMap[MAX_CPUS] = {0};
UCHAR HalpMaxNode = 0;
UCHAR HalpMaxProcsPerCluster = 0;
BOOLEAN HalpForceApicPhysicalDestinationMode = FALSE;
ULONG HalpHybridApicPhysicalTargets = 0;

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

extern UCHAR HalpInitLevel;

/* PRIVATE FUNCTIONS *********************************************************/

UCHAR
NTAPI
HalpMapNtToHwProcessorId(
    _In_ UCHAR Number)
{
    ASSERT(HalpForceApicPhysicalDestinationMode == FALSE);

    if (!HalpMaxProcsPerCluster)
    {
        ASSERT(Number < 8);
        return (1 << Number);
    }

    // FIXME
    DbgBreakPoint();
    return 0;
}

UCHAR
NTAPI
HalpNodeNumber(
    _In_ PKPCR Pcr)
{
    UCHAR NodeNumber = 0;
    UCHAR DestMap;

    if (HalpForceApicPhysicalDestinationMode)
    {
        NodeNumber = Pcr->Prcb->Number;
        return (NodeNumber + 1);
    }

    if (!HalpMaxProcsPerCluster)
        return (NodeNumber + 1);

    DestMap = HalpIntDestMap[Pcr->Prcb->Number];

    if (DestMap)
    {
        NodeNumber = (DestMap >> 4);
        return (NodeNumber + 1);
    }

    return 0;
}

VOID
NTAPI
HalpInitializeApicAddressing(VOID)
{
    PKPCR Pcr = KeGetPcr();
    PKPRCB Prcb = (PKPRCB)Pcr->Prcb;
    UCHAR PrcNumber = Prcb->Number;
    UCHAR NodeNumber;
    UCHAR DestMap;

    if (HalpForceApicPhysicalDestinationMode)
    {
        ApicWrite(APIC_DFR, 0x0FFFFFFF);
        ApicWrite(APIC_LDR, 0);
    }
    else
    {
        if (HalpMaxProcsPerCluster)
            ApicWrite(APIC_DFR, 0x0FFFFFFF);
        else
            ApicWrite(APIC_DFR, 0xFFFFFFFF);

        DestMap = HalpMapNtToHwProcessorId(PrcNumber);
        HalpIntDestMap[PrcNumber] = DestMap;
        ApicWrite(APIC_LDR, ((ULONG)DestMap << 24));
    }

    NodeNumber = HalpNodeNumber(Pcr);

    if (HalpMaxNode < NodeNumber)
        HalpMaxNode = NodeNumber;

    ASSERT(HalpMaxNode);

    if (NodeNumber)
        HalpNodeProcessorAffinity[NodeNumber - 1] |= (1 << PrcNumber);
    else
        HalpHybridApicPhysicalTargets |= (1 << PrcNumber);
}

VOID
NTAPI
HalpBuildIpiDestinationMap(
    _In_ ULONG ProcessorNumber)
{
    if (HalpInitLevel == 0xFF)
        return;

    if (HalpForceApicPhysicalDestinationMode)
    {
        DPRINT1("HalpBuildIpiDestinationMap: [%X] FIXME\n", HalpInitLevel);
    }
    else if (HalpMaxProcsPerCluster)
    {
        DPRINT1("HalpBuildIpiDestinationMap: [%X] FIXME\n", HalpInitLevel);
    }
    else
    {
        DPRINT1("HalpBuildIpiDestinationMap: [%X] FIXME\n", HalpInitLevel);
    }
}

/* SOFTWARE INTERRUPT TRAPS ***************************************************/

DECLSPEC_NORETURN
VOID
FASTCALL
HalpLocalApicErrorServiceHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    /* Enter trap */
    KiEnterInterruptTrap(TrapFrame);

    // FIXME HalpApicErrorLog and HalpLocalApicErrorCount

    ApicWrite(APIC_EOI, 0);

    if (KeGetCurrentPrcb()->CpuType >= 6)
        ApicWrite(APIC_ESR, 0);

  #ifdef __REACTOS__
    KiEoiHelper(TrapFrame);
  #else
    #error FIXME Kei386EoiHelper()
  #endif
}

/* SOFTWARE INTERRUPTS ********************************************************/


/* SYSTEM INTERRUPTS **********************************************************/


/* FUNCTIONS *****************************************************************/

INIT_FUNCTION
VOID
NTAPI
HalInitApicInterruptHandlers(VOID)
{
    KDESCRIPTOR IdtDescriptor;
    PKIDTENTRY Idt;

    __sidt(&IdtDescriptor.Limit);
    Idt = (PKIDTENTRY)IdtDescriptor.Base;

    Idt[0x37].Offset = PtrToUlong(PicSpuriousService37);
    Idt[0x37].Selector = KGDT_R0_CODE;
    Idt[0x37].Access = 0x8E00;
    Idt[0x37].ExtendedOffset = (PtrToUlong(PicSpuriousService37) >> 16);

    Idt[0x1F].Offset = PtrToUlong(ApicSpuriousService);
    Idt[0x1F].Selector = KGDT_R0_CODE;
    Idt[0x1F].Access = 0x8E00;
    Idt[0x1F].ExtendedOffset = (PtrToUlong(ApicSpuriousService) >> 16);
}

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
