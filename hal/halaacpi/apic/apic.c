
/* INCLUDES ******************************************************************/

#include <hal.h>

//#define NDEBUG
#include <debug.h>

#include "apic.h"
#include "apicacpi.h"
#include "ioapic.h"

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DetectAcpiMP)
  #pragma alloc_text(INIT, HalInitApicInterruptHandlers)
  #pragma alloc_text(INIT, HalpInitializeLocalUnit)
  #pragma alloc_text(INIT, HalpInitializePICs)
  #pragma alloc_text(INIT, HalpInitIntiInfo)
  #pragma alloc_text(INIT, HalpInitializeIOUnits)
  #pragma alloc_text(INIT, HalpPmTimerScaleTimers)
  #pragma alloc_text(INIT, HalpPmTimerSpecialStall)
#endif

#define SUPPORTED_NODES      0x20

#define HALP_DEV_INT_EDGE    0
#define HALP_DEV_INT_LEVEL   1

#define DL_EDGE_SENSITIVE    0
#define DL_LEVEL_SENSITIVE   1

#define DP_LOW_ACTIVE        0
#define DP_HIGH_ACTIVE       1

/* GLOBALS *******************************************************************/

KAFFINITY HalpNodeProcessorAffinity[MAX_CPUS] = {0};
HALP_MP_INFO_TABLE HalpMpInfoTable;
APIC_ADDRESS_USAGE HalpApicUsage;
APIC_INTI_INFO HalpIntiInfo[MAX_INTI];
USHORT HalpMaxApicInti[MAX_IOAPICS];
USHORT HalpVectorToINTI[MAX_CPUS * MAX_INT_VECTORS] = {0xFFFF};
UCHAR HalpIntDestMap[MAX_CPUS] = {0};
UCHAR HalpMaxNode = 0;
UCHAR HalpMaxProcsPerCluster = 0;
BOOLEAN HalpForceApicPhysicalDestinationMode = FALSE;
BOOLEAN HalpHiberInProgress = FALSE;
BOOLEAN IsPmTimerCorrect = TRUE;
ULONG HalpHybridApicPhysicalTargets = 0;
KSPIN_LOCK HalpAccountingLock;

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

/* UCHAR HalpDevLevel[InterruptMode][TriggerMode] */
UCHAR HalpDevLevel[2][2] =
{
    /* Edge */           /* Level */
    {DL_EDGE_SENSITIVE,  DL_EDGE_SENSITIVE},  // Latched
    {DL_LEVEL_SENSITIVE, DL_LEVEL_SENSITIVE}  // LevelSensitive
}; 

/* UCHAR HalpDevPolarity[Polarity][TriggerMode] */
UCHAR HalpDevPolarity[4][2] =
{
    /* Edge */       /* Level */
    {DP_HIGH_ACTIVE, DP_LOW_ACTIVE},  // POLARITY_CONFORMS 
    {DP_HIGH_ACTIVE, DP_HIGH_ACTIVE}, // POLARITY_ACTIVE_HIGH
    {DP_HIGH_ACTIVE, DP_LOW_ACTIVE},  // POLARITY_RESERVED
    {DP_LOW_ACTIVE,  DP_LOW_ACTIVE}   // POLARITY_ACTIVE_LOW
}; 

extern FADT HalpFixedAcpiDescTable;
extern UCHAR HalpInitLevel;
extern ULONG HalpPicVectorRedirect[HAL_PIC_VECTORS];
extern ULONG HalpPicVectorFlags[HAL_PIC_VECTORS];
extern PADDRESS_USAGE HalpAddressUsageList;
extern UCHAR HalpIoApicId[MAX_IOAPICS];
extern ULONG HalpDefaultApicDestinationModeMask;

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

INIT_FUNCTION
VOID
NTAPI
HalpInitializePICs(
    _In_ BOOLEAN EnableInterrupts)
{
    ULONG_PTR EFlags;

    DPRINT("HalpInitializePICs: EnableInterrupts %X\n", EnableInterrupts);

    /* Save EFlags and disable interrupts */
    EFlags = __readeflags();
    _disable();

    /* Initialize and mask the PIC */
    HalpInitializeLegacyPICs(FALSE); // EdgeTriggered

    DPRINT("HalpInitializePICs: FIXME HalpGlobal8259Mask\n");

    /* Restore interrupt state */
    if (EnableInterrupts)
        EFlags |= EFLAGS_INTERRUPT_MASK;

    __writeeflags(EFlags);
}

BOOLEAN
NTAPI 
HalpGetApicInterruptDesc(
    _In_ ULONG DeviceIrq,
    _Out_ USHORT* OutIntI)
{
    ULONG Count = HalpMpInfoTable.IoApicCount;
    ULONG IoApic;
    ULONG IrqBase;
    USHORT ApicInti = 0;

    DPRINT("HalpGetApicInterruptDesc: Irq %X, Count %X\n", DeviceIrq, Count);

    for (IoApic = 0; IoApic < Count; IoApic++)
    {
        IrqBase = HalpMpInfoTable.IoApicIrqBase[IoApic];

        DPRINT("HalpGetApicInterruptDesc: IrqBase %X, IoApic %X, Max Inti %X\n", IrqBase, IoApic, HalpMaxApicInti[IoApic]);

        if (DeviceIrq >= IrqBase &&
            DeviceIrq < (IrqBase + HalpMaxApicInti[IoApic]))
        {
            *OutIntI = (DeviceIrq + ApicInti - (USHORT)IrqBase);
            DPRINT("HalpGetApicInterruptDesc: *OutIntI %X\n", *OutIntI);
            return TRUE;
        }

        ApicInti += HalpMaxApicInti[IoApic];
    }

    DPRINT("HalpGetApicInterruptDesc: return FALSE\n");
    return FALSE;
}

INIT_FUNCTION
VOID
NTAPI 
HalpInitIntiInfo(VOID)
{
    PAPIC_INTI_INFO pIntiInfo;
    HAL_PIC_VECTOR_FLAGS PicFlags;
    ULONG Vector;
    ULONG Inti;
    ULONG ApicNo;
    USHORT InterruptDesc;
    USHORT SciVector;

    DPRINT("HalpInitIntiInfo: IoApicIrqBase %X, MAX_INTI %X\n", HalpMpInfoTable.IoApicIrqBase[0], MAX_INTI);

    for (Inti = 0; Inti < MAX_INTI; Inti++)
    {
        HalpIntiInfo[Inti].Type = INTI_INFO_TYPE_INT; // default type of Interrupt Source - INTR
        HalpIntiInfo[Inti].TriggerMode = INTI_INFO_TRIGGER_LEVEL;
        HalpIntiInfo[Inti].Polarity = INTI_INFO_POLARITY_ACTIVE_LOW;
    }

    Vector = HalpPicVectorRedirect[APIC_CLOCK_INDEX];

    if (!HalpGetApicInterruptDesc(Vector, &InterruptDesc))
    {
        DPRINT1("HalpInitIntiInfo: KeBugCheckEx\n");
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x3000, 1, Vector, 0);
    }

    DPRINT("HalpInitIntiInfo: Vector %X, InterruptDesc %X\n", Vector, InterruptDesc);

    PicFlags.AsULONG = HalpPicVectorFlags[APIC_CLOCK_INDEX];
    pIntiInfo = &HalpIntiInfo[InterruptDesc];

    DPRINT("HalpInitIntiInfo: PicFlags %X, IntiInfo %X\n", PicFlags.AsULONG, pIntiInfo->AsULONG);

    if (PicFlags.Polarity == PIC_FLAGS_POLARITY_CONFORMS)
    {
        pIntiInfo->Polarity = INTI_INFO_POLARITY_ACTIVE_HIGH;
    }
    else
    {
        pIntiInfo->Polarity = PicFlags.Polarity;
    }

    if (PicFlags.TriggerMode == PIC_FLAGS_TRIGGER_CONFORMS)
    {
        pIntiInfo->TriggerMode = INTI_INFO_TRIGGER_EDGE;
    }
    else
    {
        pIntiInfo->TriggerMode = INTI_INFO_TRIGGER_LEVEL;
    }

    SciVector = HalpFixedAcpiDescTable.sci_int_vector;
    Vector = HalpPicVectorRedirect[SciVector];

    if (!HalpGetApicInterruptDesc(Vector, &InterruptDesc))
    {
        DPRINT1("HalpInitIntiInfo: KeBugCheckEx\n");
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x3000, Vector, 0, 0);
    }

    DPRINT("SciVector %X, Vector %X, InterruptDesc %X\n", SciVector, Vector, InterruptDesc);

    PicFlags.AsULONG = HalpPicVectorFlags[SciVector];
    pIntiInfo = &HalpIntiInfo[InterruptDesc];

    DPRINT("HalpInitIntiInfo: PicFlags %X, IntiInfo %X\n", PicFlags.AsULONG, pIntiInfo->AsULONG);

    if (PicFlags.Polarity == PIC_FLAGS_POLARITY_CONFORMS)
    {
        pIntiInfo->Polarity = INTI_INFO_POLARITY_ACTIVE_LOW;
    }
    else
    {
        pIntiInfo->Polarity = PicFlags.Polarity;
    }

    if (PicFlags.TriggerMode != PIC_FLAGS_TRIGGER_CONFORMS &&
        PicFlags.TriggerMode != PIC_FLAGS_TRIGGER_LEVEL)
    {
        DPRINT1("HalpInitIntiInfo: KeBugCheckEx\n");
        KeBugCheckEx(ACPI_BIOS_ERROR, 0x10008, SciVector, 0, 0);
    }

    pIntiInfo->TriggerMode = INTI_INFO_TRIGGER_LEVEL;

    Inti = 0;
    for (ApicNo = 0; ApicNo < MAX_IOAPICS; ApicNo++)
    {
        Inti += HalpMaxApicInti[ApicNo];
    }

    ASSERT(Inti < MAX_INTI);
}

INIT_FUNCTION
VOID
NTAPI 
HalpInitializeIOUnits(VOID)
{
    PIO_APIC_REGISTERS IoApicRegs;
    ULONG ApicNo;
    ULONG IoApicId;
    ULONG MaxRedirectRegs;
    ULONG Idx;

    DPRINT("HalpInitializeIOUnits: IoApicCount %X\n", HalpMpInfoTable.IoApicCount);

    for (ApicNo = 0; ApicNo < HalpMpInfoTable.IoApicCount; ApicNo++)
    {
        IoApicRegs = (PIO_APIC_REGISTERS)HalpMpInfoTable.IoApicVA[ApicNo];
        IoApicId = HalpIoApicId[ApicNo];

        IoApicRegs->IoRegisterSelect = IOAPIC_ID;
        IoApicRegs->IoWindow = ((IoApicRegs->IoWindow & 0x00FFFFFF) | SET_IOAPIC_ID(IoApicId));

        IoApicRegs->IoRegisterSelect = IOAPIC_VER;
        MaxRedirectRegs = GET_IOAPIC_MRE(IoApicRegs->IoWindow);

        for (Idx = 0; Idx <= (MaxRedirectRegs * 2); Idx += 2)
        {
            IoApicRegs->IoRegisterSelect = IOAPIC_REDTBL + Idx;

            if ((IoApicRegs->IoWindow & IOAPIC_TBL_DELMOD) != IOAPIC_DM_SMI)
            {
                IoApicRegs->IoRegisterSelect = IOAPIC_REDTBL + Idx;
                IoApicRegs->IoWindow |= (IOAPIC_TBL_IM | IOAPIC_TBL_VECTOR);
            }
        }
    }

    if (HalpHiberInProgress)
        return;

    HalpApicUsage.Next = NULL;
    HalpApicUsage.Type = CmResourceTypeMemory;
    HalpApicUsage.Flags = IDT_DEVICE;

    HalpApicUsage.Element[0].Start = HalpMpInfoTable.LocalApicPA;
    HalpApicUsage.Element[0].Length = IOAPIC_SIZE;

    ASSERT(HalpMpInfoTable.IoApicCount <= MAX_IOAPICS);

    for (ApicNo = 0; ApicNo < HalpMpInfoTable.IoApicCount; ApicNo++)
    {
        HalpApicUsage.Element[ApicNo + 1].Start = HalpMpInfoTable.IoApicPA[ApicNo];
        HalpApicUsage.Element[ApicNo + 1].Length = IOAPIC_SIZE;
    }

    HalpApicUsage.Element[ApicNo + 1].Start = 0;
    HalpApicUsage.Element[ApicNo + 1].Length = 0;

    HalpApicUsage.Next = HalpAddressUsageList;
    HalpAddressUsageList = (PADDRESS_USAGE)&HalpApicUsage;
}

INIT_FUNCTION
BOOLEAN
FASTCALL
HalpPmTimerSpecialStall(
    _In_ ULONG StallValue)
{
    LARGE_INTEGER OldValue;
    LARGE_INTEGER NewValue;
    LARGE_INTEGER EndValue;
    ULONG ix = 0;

    HaliPmTimerQueryPerfCount(&OldValue, NULL);
    EndValue.QuadPart = (OldValue.QuadPart + StallValue);

    while (OldValue.QuadPart < EndValue.QuadPart)
    {
        HaliPmTimerQueryPerfCount(&NewValue, NULL);

        ASSERT(NewValue.QuadPart >= OldValue.QuadPart);

        if (NewValue.QuadPart == OldValue.QuadPart)
            ix++;

        if (ix > 1000)
        {
            DPRINT1("HalpPmTimerSpecialStall: return FALSE\n");
            return FALSE;
        }

        OldValue = NewValue;
    }

    return TRUE;
}

#define HALP_SPECIAL_STALL_VALUE  0x6D3D3

INIT_FUNCTION
BOOLEAN
NTAPI
HalpPmTimerScaleTimers(VOID)
{
    PHALP_PCR_HAL_RESERVED HalReserved;
    LVT_REGISTER LvtEntry;
    ULONG EFlags = __readeflags();
    ULONG ApicCount;
    ULONGLONG TscHz;
    LARGE_INTEGER TscCount;
    ULONG ApicHz;
    INT CpuInfo[4];

    if (!IsPmTimerCorrect)
    {
        DPRINT1("HalpPmTimerScaleTimers: IsPmTimerCorrect - FALSE \n");
        return FALSE;
    }

    _disable();

    HalReserved = (PHALP_PCR_HAL_RESERVED)KeGetPcr()->HalReserved;

    HalReserved->Reserved3 = 0;
    HalReserved->Reserved4 = 0;

    /* Set to periodic */
    LvtEntry.Long = 0;
    LvtEntry.Vector = APIC_PROFILE_VECTOR;
    LvtEntry.Mask = 1;
    LvtEntry.TimerMode = 1;
    ApicWrite(APIC_TMRLVTR, LvtEntry.Long);

    /* Set clock multiplier to 1 */
    ApicWrite(APIC_TDCR, TIMER_DV_DivideBy1);

    if (ApicRead(APIC_TDCR) != TIMER_DV_DivideBy1)
    {
        DPRINT1("HalpPmTimerScaleTimers: wrong Timer Divide %X\n", ApicRead(APIC_TDCR));
    }

    /* Serializing instruction execution */
    __cpuid(CpuInfo, 0);

    /* Reset the count interval */
    ApicWrite(APIC_TICR, 0xFFFFFFFF);

    /* Reset TSC value */
    WRMSR(MSR_RDTSC, 0ull);

    IsPmTimerCorrect = HalpPmTimerSpecialStall(HALP_SPECIAL_STALL_VALUE);
    if (!IsPmTimerCorrect)
    {
        DPRINT1("HalpPmTimerScaleTimers: IsPmTimerCorrect is FALSE \n");
        goto Exit;
    }

    /* Get the initial time-stamp counter value */
    TscCount.QuadPart = __rdtsc();

    /* Get the APIC current timer counter value */
    ApicCount =  ApicRead(APIC_TCCR);

    /* Calculating */
    TscHz = (TscCount.QuadPart * APIC_CLOCK_INDEX);
    TscHz = ((TscHz + 10000/2) / 10000) * 10000; // Round

    HalReserved->TscHz = TscHz;
    KeGetPcr()->StallScaleFactor = (TscHz + (1000000/2)) / 1000000;

    ApicHz = ((0xFFFFFFFF - ApicCount) * APIC_CLOCK_INDEX);
    ApicHz = ((ApicHz + 10000/2) / 10000) * 10000; // Round

    HalReserved->ApicHz = ApicHz;
    HalReserved->ProfileCount = ApicHz;

    /* Set the count interval */
    ApicWrite(APIC_TICR, ApicHz);

Exit:

    if (EFlags & EFLAGS_INTERRUPT_MASK)
        _enable();

    DPRINT("HalpPmTimerScaleTimers: IsPmTimerCorrect %X\n", IsPmTimerCorrect);
    return IsPmTimerCorrect;
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

//DECLSPEC_NORETURN
VOID
FASTCALL
HalpProfileInterruptHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    DPRINT1("HalpProfileInterruptHandler: TrapFrame %X\n", TrapFrame);
    ASSERT(FALSE); // DbgBreakPointEx();
}

//DECLSPEC_NORETURN
VOID
FASTCALL
HalpApicRebootServiceHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    DPRINT1("HalpApicRebootServiceHandler: TrapFrame %X\n", TrapFrame);
    ASSERT(FALSE); // DbgBreakPointEx();
}

//DECLSPEC_NORETURN
VOID
FASTCALL
HalpBroadcastCallServiceHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    DPRINT1("HalpBroadcastCallServiceHandler: TrapFrame %X\n", TrapFrame);
    ASSERT(FALSE); // DbgBreakPointEx();
}

//DECLSPEC_NORETURN
VOID
FASTCALL
HalpPerfInterruptHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    DPRINT1("HalpPerfInterruptHandler: TrapFrame %X\n", TrapFrame);
    ASSERT(FALSE); // DbgBreakPointEx();
}

//DECLSPEC_NORETURN
VOID
FASTCALL
HalpIpiHandlerHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    DPRINT1("HalpIpiHandlerHandler: TrapFrame %X\n", TrapFrame);
    ASSERT(FALSE); // DbgBreakPointEx();
}

/* SOFTWARE INTERRUPTS ********************************************************/


/* SYSTEM INTERRUPTS **********************************************************/

UCHAR
NTAPI
HalpAddInterruptDest(_In_ ULONG InDestination,
                     _In_ UCHAR ProcessorNumber)
{
    UCHAR Destination;

    DPRINT("HalpAddInterruptDest: InDestination %X, Processor %X\n", InDestination, ProcessorNumber);

    if (HalpForceApicPhysicalDestinationMode)
    {
        DPRINT1("HalpAddInterruptDest: FIXME! DbgBreakPoint()\n");Destination = 0;
        ASSERT(FALSE); // DbgBreakPoint();
        return Destination;
    }

    Destination = HalpIntDestMap[ProcessorNumber];
    if (!Destination)
    {
        DPRINT("HalpAddInterruptDest: return %X\n", InDestination);
        return InDestination;
    }

    if (!HalpMaxProcsPerCluster)
    {
        Destination |= InDestination;
        DPRINT("HalpAddInterruptDest: return Destination %X\n", Destination);
        return Destination;
    }

    DPRINT1("HalpAddInterruptDest: FIXME! DbgBreakPoint()\n");
    ASSERT(FALSE); // DbgBreakPoint();

    DPRINT("HalpAddInterruptDest: return Destination %X\n", Destination);
    return Destination;
}

VOID
NTAPI
HalpSetRedirEntry(_In_ USHORT IntI,
                  _In_ PIOAPIC_REDIRECTION_REGISTER IoApicReg,
                  _In_ ULONG Destination)
{
    PIO_APIC_REGISTERS IoApicRegs;
    UCHAR IoUnit;

    for (IoUnit = 0; IoUnit < MAX_IOAPICS; IoUnit++)
    {
        if (IntI + 1 <= HalpMaxApicInti[IoUnit])
            break;

        ASSERT(IntI >= HalpMaxApicInti[IoUnit]);
        IntI -= HalpMaxApicInti[IoUnit];
    }

    ASSERT(IoUnit < MAX_IOAPICS);

    IoApicRegs = (PIO_APIC_REGISTERS)HalpMpInfoTable.IoApicVA[IoUnit];

    IoApicWrite(IoApicRegs, ((IOAPIC_REDTBL + 1) + IntI * 2), Destination); // RedirReg + 1
    IoApicWrite(IoApicRegs, (IOAPIC_REDTBL + IntI * 2), IoApicReg->Long0);  // RedirReg
}

VOID
NTAPI
HalpEnableRedirEntry(
    _In_ USHORT IntI,
    _In_ PIOAPIC_REDIRECTION_REGISTER IoApicReg,
    _In_ UCHAR ProcessorNumber)
{
    UCHAR Destination;

    HalpIntiInfo[IntI].Entry = IoApicReg->Long0;

    Destination = HalpAddInterruptDest(HalpIntiInfo[IntI].Destinations, ProcessorNumber);
    HalpIntiInfo[IntI].Destinations = Destination;

    HalpSetRedirEntry(IntI, IoApicReg, ((ULONG)Destination << 24));

    HalpIntiInfo[IntI].Enabled = 1;
}

BOOLEAN
NTAPI
HalEnableSystemInterrupt(
    _In_ ULONG SystemVector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE InterruptMode)
{
    IOAPIC_REDIRECTION_REGISTER IoApicReg;
    APIC_INTI_INFO IntiInfo;
    ULONG TriggerMode;
    ULONG Lock;
    USHORT IntI;
    UCHAR DevLevel;
    UCHAR CpuNumber;

    DPRINT1("HalEnableSystemInterrupt: Vector %X, Irql %X, Mode %X\n", SystemVector, Irql, InterruptMode);

    ASSERT(SystemVector < ((1 + SUPPORTED_NODES) * MAX_INT_VECTORS - 1));
    ASSERT(Irql <= HIGH_LEVEL);

    IntI = HalpVectorToINTI[SystemVector];
    if (IntI == 0xFFFF)
    {
        DPRINT1("HalEnableSystemInterrupt: return FALSE\n");
        return FALSE;
    }

    if (IntI >= MAX_INTI)
    {
        DPRINT1("EnableSystemInt: IntI %X, MAX_INTI %X\n", IntI, MAX_INTI);
        ASSERT(IntI < MAX_INTI);
    }

    IntiInfo = HalpIntiInfo[IntI];
    TriggerMode = IntiInfo.TriggerMode;

    if (InterruptMode == LevelSensitive)
        DevLevel = HalpDevLevel[HALP_DEV_INT_LEVEL][TriggerMode];
    else
        DevLevel = HalpDevLevel[HALP_DEV_INT_EDGE][TriggerMode];

    Lock = HalpAcquireHighLevelLock(&HalpAccountingLock);
    CpuNumber = KeGetPcr()->Prcb->Number;

    if (IntiInfo.Type == INTI_INFO_TYPE_ExtINT)
    {
        DPRINT1("HalEnableSystemInterrupt: FIXME ExtINT. DbgBreakPoint()\n");
        ASSERT(FALSE); // DbgBreakPoint();

        HalpReleaseHighLevelLock(&HalpAccountingLock, Lock);
        return TRUE;
    }

    if (IntiInfo.Type != INTI_INFO_TYPE_INT)
    {
        DPRINT1("HalEnableSystemInterrupt: Unsupported IntiInfo.Type %X. DbgBreakPoint()\n", IntiInfo.Type);
        ASSERT(FALSE); // DbgBreakPoint();

        HalpReleaseHighLevelLock(&HalpAccountingLock, Lock);
        return TRUE;
    }

    /* IntiInfo.Type == INTI_INFO_TYPE_INT (INTR Interrupt Source) */

    IoApicReg.LongLong = 0;

    if (SystemVector == APIC_CLOCK_VECTOR)
    {
        ASSERT(CpuNumber == 0);
        IoApicReg.Vector = APIC_CLOCK_VECTOR;
        IoApicReg.Long0 |= HalpDefaultApicDestinationModeMask;
    }
    else
    {
        if (SystemVector == 0xFF)
            return FALSE;

        IoApicReg.Vector = HalVectorToIDTEntry(SystemVector);

        if (!HalpForceApicPhysicalDestinationMode)
        {
            IoApicReg.DeliveryMode = 1;    // Lowest Priority
            IoApicReg.DestinationMode = 1; // Logical Mode
        }
    }

    if (DevLevel & DL_LEVEL_SENSITIVE)
        IoApicReg.TriggerMode = 1; // Level sensitive

    if (HalpDevPolarity[IntiInfo.Polarity][(DevLevel & DL_LEVEL_SENSITIVE)] == DP_LOW_ACTIVE)
        IoApicReg.Polarity = 1; // Low active

    HalpEnableRedirEntry(IntI, &IoApicReg, CpuNumber);

    DPRINT("HalEnableSystemInterrupt: HalpIntiInfo[IntI] %X\n", HalpIntiInfo[IntI].AsULONG);
    HalpReleaseHighLevelLock(&HalpAccountingLock, Lock);

    return TRUE;
}

FORCEINLINE
VOID
KeSetCurrentIrql(
    _In_ KIRQL NewIrql)
{
    /* Set new current IRQL */
    KeGetPcr()->Irql = NewIrql;
}


/* IRQL MANAGEMENT ************************************************************/

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    /* Return the IRQL */
    return KeGetPcr()->Irql;
}

KIRQL
FASTCALL
KfRaiseIrql(
    _In_ KIRQL NewIrql)
{
    PKPCR Pcr = KeGetPcr();
    KIRQL OldIrql;

    OldIrql = Pcr->Irql;
    Pcr->Irql = NewIrql;

    return OldIrql;
}

VOID NTAPI Kii386SpinOnSpinLock(_In_ PKSPIN_LOCK SpinLock, _In_ ULONG Flags);

ULONG
FASTCALL
HalpAcquireHighLevelLock(
    _In_ volatile PKSPIN_LOCK SpinLock)
{
    ULONG EFlags;

    EFlags = __readeflags();

    while (TRUE)
    {
        _disable();

        if (!InterlockedBitTestAndSet((volatile PLONG)SpinLock, 0))
            break;

      #if defined(_M_IX86) && DBG
        /* On x86 debug builds, we use a much slower but useful routine */
        Kii386SpinOnSpinLock(SpinLock, 5);
      #else
        /* It's locked... spin until it's unlocked */
        while (*(volatile PKSPIN_LOCK)SpinLock & 1)
            /* Yield and keep looping */
            YieldProcessor();
      #endif
    }

  #if DBG
    /* On debug builds, we OR in the KTHREAD */
    *SpinLock = ((KSPIN_LOCK)KeGetCurrentThread() | 1);
  #endif

    return EFlags;
}

VOID
FASTCALL
HalpReleaseHighLevelLock(
    _In_ volatile PKSPIN_LOCK SpinLock,
    _In_ ULONG EFlags)
{
  #if DBG
    if (*SpinLock != ((KSPIN_LOCK)KeGetCurrentThread() | 1))
        KeBugCheckEx(SPIN_LOCK_NOT_OWNED, (ULONG_PTR)SpinLock, 0, 0, 0);
  #endif

    InterlockedAnd((volatile PLONG)SpinLock, 0);

    __writeeflags(EFlags);
}

/* FUNCTIONS *****************************************************************/

INIT_FUNCTION
VOID
NTAPI
HalpInitializeLocalUnit(VOID)
{
    APIC_SPURIOUS_INERRUPT_REGISTER SpIntRegister;
    APIC_COMMAND_REGISTER CommandRegister;
    LVT_REGISTER LvtEntry;
    ULONG EFlags = __readeflags();
    PKPRCB Prcb;
    UCHAR Id;

    _disable();

    Prcb = KeGetPcr()->Prcb;

    if (Prcb->Number == 0)
    {
        /* MultiProcessor Specification, Table 4-1.
           MP Floating Pointer Structure Fields (MP FEATURE INFORMATION BYTE 2) Bit 7:IMCRP
           If TRUE - PIC Mode, if FALSE - Virtual Wire Mode
        */
        if (HalpMpInfoTable.ImcrPresent)
        {
            /* Enable PIC mode to Processor via APIC */
            WRITE_PORT_UCHAR(IMCR_ADDRESS_PORT, IMCR_SELECT);
            WRITE_PORT_UCHAR(IMCR_DATA_PORT, IMCR_PIC_VIA_APIC);
        }

        if (HalpMaxProcsPerCluster > APIC_MAX_CPU_PER_CLUSTER ||
            (HalpMaxProcsPerCluster == 0 && HalpMpInfoTable.ProcessorCount > 8))
        {
            HalpMaxProcsPerCluster = APIC_MAX_CPU_PER_CLUSTER;
        }

        if (HalpMpInfoTable.LocalApicversion == 0)
        {
            ASSERT(HalpMpInfoTable.ProcessorCount <= 8);
            HalpMaxProcsPerCluster = 0;
        }
    }

    ApicWrite(APIC_TPR, 0xFF);

    HalpInitializeApicAddressing();
    Id = (UCHAR)((ApicRead(APIC_ID)) >> 24);
    HalpMarkProcessorStarted(Id, Prcb->Number);

    KeRegisterInterruptHandler(APIC_SPURIOUS_VECTOR, ApicSpuriousService);

    SpIntRegister.Long = 0;
    SpIntRegister.Vector = APIC_SPURIOUS_VECTOR;
    SpIntRegister.SoftwareEnable = 1;
    ApicWrite(APIC_SIVR, SpIntRegister.Long);

    if (HalpMpInfoTable.LocalApicversion)
    {
        KeRegisterInterruptHandler(APIC_ERROR_VECTOR, HalpLocalApicErrorService);
        ApicWrite(APIC_ERRLVTR, APIC_ERROR_VECTOR);
    }

    LvtEntry.Long = 0;
    LvtEntry.Vector = APIC_PROFILE_VECTOR;
    LvtEntry.Mask = 1;
    LvtEntry.TimerMode = 1;
    ApicWrite(APIC_TMRLVTR, LvtEntry.Long);

    LvtEntry.Long = 0;
    LvtEntry.Vector = APIC_PERF_VECTOR;
    LvtEntry.Mask = 1;
    LvtEntry.TimerMode = 0;
    ApicWrite(APIC_PCLVTR, LvtEntry.Long);

    LvtEntry.Long = 0;
    LvtEntry.Vector = APIC_SPURIOUS_VECTOR;
    LvtEntry.Mask = 1;
    LvtEntry.TimerMode = 0;
    ApicWrite(APIC_LINT0, LvtEntry.Long);

    LvtEntry.Long = 0;
    LvtEntry.Vector = APIC_NMI_VECTOR;
    LvtEntry.Mask = 1;
    LvtEntry.TimerMode = 0;
    LvtEntry.MessageType = APIC_MT_NMI;
    LvtEntry.TriggerMode = APIC_TGM_Level;
    ApicWrite(APIC_LINT1, LvtEntry.Long);

    CommandRegister.Long0 = 0;
    CommandRegister.Vector = ZERO_VECTOR;
    CommandRegister.MessageType = APIC_MT_INIT;
    CommandRegister.TriggerMode = APIC_TGM_Level;
    CommandRegister.DestinationShortHand = APIC_DSH_AllIncludingSelf;
    ApicWrite(APIC_ICR0, CommandRegister.Long0);

    HalpBuildIpiDestinationMap(Prcb->Number);

    ApicWrite(APIC_TPR, 0x00);

    if (EFlags & EFLAGS_INTERRUPT_MASK)
        _enable();
}

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
