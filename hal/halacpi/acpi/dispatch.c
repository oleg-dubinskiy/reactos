
/* INCLUDES ******************************************************************/

#include <hal.h>
#include "dispatch.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

ACPI_PM_DISPATCH_TABLE HalAcpiDispatchTable =
{
    0x48414C20, // 'HAL '
    2,
    HaliAcpiTimerInit,
    NULL,
    HaliAcpiMachineStateInit,
    HaliAcpiQueryFlags,
    HalpAcpiPicStateIntact,
    HalpRestoreInterruptControllerState,
    HaliPciInterfaceReadConfig,
    HaliPciInterfaceWriteConfig,
    HaliSetVectorState,
    HalpGetApicVersion,
    HaliSetMaxLegacyPciBusNumber,
    HaliIsVectorValid 
};

HALP_TIMER_INFO TimerInfo;
BOOLEAN HalpBrokenAcpiTimer = FALSE;

extern FADT HalpFixedAcpiDescTable;
extern ULONG HalpWAETDeviceFlags;

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
HalaAcpiTimerInit(_In_ PULONG TimerPort,
                  _In_ BOOLEAN IsTimerValExt)
{
    DPRINT("HalaAcpiTimerInit: Port %X, IsValExt %X\n", TimerPort, IsTimerValExt);

    RtlZeroMemory(&TimerInfo, sizeof(TimerInfo));

    TimerInfo.TimerPort = TimerPort;

    if (IsTimerValExt)
        TimerInfo.ValueExt = 0x80000000; // 32-bit
    else
        TimerInfo.ValueExt = 0x00800000; // 24-bit

    if (!HalpBrokenAcpiTimer)
    {
        DPRINT("HalpBrokenAcpiTimer - FALSE\n");
        return;
    }

    DPRINT1("HalaAcpiTimerInit: FIXME HalpQueryBrokenPiix4()\n");
    ASSERT(0); // HalpDbgBreakPointEx();

}

VOID
NTAPI
HaliAcpiTimerInit(_In_ PULONG TimerPort,
                  _In_ BOOLEAN TimerValExt)
{
    PAGED_CODE();

    DPRINT("HaliAcpiTimerInit: Port %X, ValExt %X\n", TimerPort, TimerValExt);
    DPRINT("HalpFixedAcpiDescTable.flags - %08X\n", HalpFixedAcpiDescTable.flags);

    /* Is this in the init phase? */
    if (!TimerPort)
    {
        /* Get the data from the FADT */

        /* System port address of the Power Management Timer Control Register Block */
        TimerPort = (PULONG)HalpFixedAcpiDescTable.pm_tmr_blk_io_port;

        /* A zero indicates TMR_VAL is implemented as a 24-bit value.
           A one indicates TMR_VAL is implemented as a 32-bit value.
        */
        TimerValExt = ((HalpFixedAcpiDescTable.flags & ACPI_TMR_VAL_EXT) != 0);
        DPRINT1("TimerPort %X, TimerValExt %X\n", TimerPort, TimerValExt);
    }

    /* FIXME: Now proceed to the timer initialization */
    HalaAcpiTimerInit(TimerPort, TimerValExt);
}

VOID
NTAPI
HalAcpiTimerCarry(VOID)
{
    LARGE_INTEGER Value;
    ULONG Time;

    Time = READ_PORT_ULONG(TimerInfo.TimerPort);
    DPRINT("HalAcpiTimerCarry: Time %X\n", Time);

    Value.QuadPart = (TimerInfo.AcpiTimeValue.QuadPart + TimerInfo.ValueExt);
    Value.QuadPart += ((Value.LowPart ^ Time) & TimerInfo.ValueExt);

    TimerInfo.TimerCarry = Value.HighPart;
    TimerInfo.AcpiTimeValue.QuadPart = Value.QuadPart;
}

VOID
NTAPI
HalAcpiBrokenPiix4TimerCarry()
{
    /* Nothing */
    ;
}

NTSTATUS
NTAPI
HalpGetChipHacks(_In_ USHORT VendorID,
                 _In_ USHORT DeviceID,
                 _In_ UCHAR RevisionID,
                 _Out_ ULONG* OutChipHacks)
{
    KEY_VALUE_PARTIAL_INFORMATION KeyValueInfo;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DestinationString;
    HANDLE KeyHandle = NULL;
    ULONG ResultLength;
    ULONG ChipHacks;
    WCHAR SourceString[10];
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("HalpGetChipHacks: VendorID %X, DeviceID %X\n", VendorID, DeviceID);

    RtlInitUnicodeString(&DestinationString, L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\Control\\HAL");

    InitializeObjectAttributes(&ObjectAttributes, &DestinationString, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpGetChipHacks: Status %X\n", Status);
        KeFlushWriteBuffer();
        return Status;
    }

    swprintf(SourceString, L"%04X%04X", VendorID, DeviceID);
    RtlInitUnicodeString(&DestinationString, SourceString);

    Status = ZwQueryValueKey(KeyHandle,
                             &DestinationString,
                             KeyValuePartialInformation,
                             &KeyValueInfo,
                             sizeof(KeyValueInfo),
                             &ResultLength);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(KeyHandle);
        KeFlushWriteBuffer();
        return Status;
    }

    ChipHacks = *(PULONG)KeyValueInfo.Data;
    DPRINT1("HalpGetChipHacks: ChipHacks %X\n", ChipHacks);

    if (RevisionID && (RevisionID >= (ChipHacks >> 24)))
    {
        DPRINT1("HalpGetChipHacks: RevisionID %X\n", RevisionID);
        ASSERT(0); // HalpDbgBreakPointEx();
        ChipHacks >>= 12;
    }

    ChipHacks &= 0xFFF;

    if (HalpWAETDeviceFlags & 2)
    {
        DPRINT1("HalpGetChipHacks: HalpWAETDeviceFlags %X\n", HalpWAETDeviceFlags);
        ASSERT(0); // HalpDbgBreakPointEx();
        ChipHacks &= 0xFFFFFFFE;
    }

    *OutChipHacks = ChipHacks;

    ZwClose(KeyHandle);
    KeFlushWriteBuffer();

    return Status;
}

VOID
NTAPI
HalpPiix4Detect(_In_ BOOLEAN IsInitialize)
{
    BOOLEAN IsBroken440BX = FALSE;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DestinationString;
    PCI_COMMON_CONFIG PciHeader;
    PCI_SLOT_NUMBER SlotNumber;
    HANDLE KeyHandle = NULL;
    HANDLE Handle = NULL;
    ULONG Disposition;
    ULONG BusNumber;
    ULONG Device;
    ULONG Function;
    ULONG BytesRead;
    ULONG ChipHacks;
    NTSTATUS Status;

    DPRINT("HalpPiix4Detect: IsInitialize %X\n", IsInitialize);

    if (IsInitialize)
    {
        PAGED_CODE();

        RtlInitUnicodeString(&DestinationString, L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET");
        InitializeObjectAttributes(&ObjectAttributes, &DestinationString, OBJ_CASE_INSENSITIVE, NULL, NULL);

        Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            KeFlushWriteBuffer();
            return;
        }

        RtlInitUnicodeString(&DestinationString, L"Control\\HAL");
        InitializeObjectAttributes(&ObjectAttributes, &DestinationString, OBJ_CASE_INSENSITIVE, KeyHandle, NULL);

        Status = ZwCreateKey(&Handle, KEY_READ, &ObjectAttributes, 0, NULL, 0, &Disposition);
        if (!NT_SUCCESS(Status))
            goto Exit;
    }

    for (BusNumber = 0; BusNumber < 0xFF; BusNumber++)
    {
        SlotNumber.u.AsULONG = 0;

        for (Device = 0; Device < 0x20; Device++)
        {
            for (Function = 0; Function < 8; Function++)
            {
                DPRINT("HalpPiix4Detect: Bus %X, Dev %X, Func %X, Slot %X\n", BusNumber, Device, Function, SlotNumber.u.AsULONG);

                SlotNumber.u.bits.DeviceNumber = Device;
                SlotNumber.u.bits.FunctionNumber = Function;

                BytesRead = HalGetBusData(PCIConfiguration, BusNumber, SlotNumber.u.AsULONG, &PciHeader, 0x40);
                if (!BytesRead)
                    goto Finish;

                if (PciHeader.VendorID == 0xFFFF)
                    continue;

                DPRINT("HalpPiix4Detect: VendorID %X, DeviceID %X\n", PciHeader.VendorID, PciHeader.DeviceID);

                if (IsInitialize)
                {
                    if (PciHeader.VendorID == 0x8086 &&
                        (PciHeader.DeviceID == 0x7190 || PciHeader.DeviceID == 0x7192) &&
                        PciHeader.RevisionID <= 2)
                    {
                        DPRINT1("HalpPiix4Detect: PciHeader.RevisionID %X\n", PciHeader.RevisionID);
                        ASSERT(0); // HalpDbgBreakPointEx();
                    }

                    Status = HalpGetChipHacks(PciHeader.VendorID, PciHeader.DeviceID, PciHeader.RevisionID, &ChipHacks);
                    if (NT_SUCCESS(Status))
                    {
                        DPRINT1("HalpPiix4Detect: ChipHacks %X\n", ChipHacks);

                        if (ChipHacks & 1)
                        {
                            ASSERT(0); // HalpDbgBreakPointEx();
                            HalpBrokenAcpiTimer = 1;
                        }
                        if (ChipHacks & 2)
                        {
                            ASSERT(0); // HalpDbgBreakPointEx();
                        }
                        if (ChipHacks & 8)
                        {
                            ASSERT(0); // HalpDbgBreakPointEx();
                        }
                    }
                }
                else
                {
                    DPRINT("HalpPiix4Detect: VendorID %X, DeviceID %X\n", PciHeader.VendorID, PciHeader.DeviceID);
                }

                if (PciHeader.VendorID == 0x8086 && PciHeader.DeviceID == 0x7110)
                {
                    DPRINT1("HalpPiix4Detect: VendorID %X, DeviceID %X\n", PciHeader.VendorID, PciHeader.DeviceID);
                    ASSERT(0); // HalpDbgBreakPointEx();
                    goto Finish;
                }

                if (PciHeader.BaseClass == 0xC && PciHeader.SubClass == 3)
                {
                    if (PciHeader.ProgIf == 0x10)
                    {
                        HalpStopOhciInterrupt(BusNumber, SlotNumber.u.AsULONG);
                    }
                    else
                    {
                        if (PciHeader.VendorID == 0x8086)
                        {
                            HalpStopUhciInterrupt(BusNumber, SlotNumber.u.AsULONG, TRUE);
                        }
                        else
                        {
                            if (PciHeader.VendorID == 0x1106)
                                HalpStopUhciInterrupt(BusNumber, SlotNumber.u.AsULONG, FALSE);
                        }
                    }
                }

                if (Function == 0)
                {
                    if ((PciHeader.HeaderType & PCI_MULTIFUNCTION) == 0)
                        break;
                }
            }
        }
    }

Finish:

    if (!IsInitialize)
    {
        KeFlushWriteBuffer();
        return;
    }

    if (Handle)
    {
        ZwClose(Handle);
        Handle = 0;
    }

    if (!IsBroken440BX)
        goto Exit;

    DPRINT1("HalpPiix4Detect: IsBroken440BX TRUE\n");
    ASSERT(0); // HalpDbgBreakPointEx();

Exit:

    if (Handle)
        ZwClose(Handle);

    if (KeyHandle)
        ZwClose(KeyHandle);

    KeFlushWriteBuffer();
}

NTSTATUS
NTAPI
HalacpiInitPowerManagement(
    _In_ PPM_DISPATCH_TABLE PmDriverDispatchTable,
    _Out_ PPM_DISPATCH_TABLE * PmHalDispatchTable)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

/* PM DISPATCH FUNCTIONS *****************************************************/

VOID
NTAPI
HaliAcpiMachineStateInit(_In_ ULONG Par1,
                         _In_ PVOID Par2,
                         _Out_ PVOID OutPar3)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

ULONG
NTAPI
HaliAcpiQueryFlags(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

UCHAR
NTAPI
HalpAcpiPicStateIntact(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

VOID
NTAPI
HalpRestoreInterruptControllerState(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

VOID
NTAPI
HaliSetVectorState(_In_ ULONG Par1,
                   _In_ ULONG Par2)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

ULONG
NTAPI
HalpGetApicVersion(_In_ ULONG Par1)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

VOID
NTAPI
HaliSetMaxLegacyPciBusNumber(_In_ ULONG MaxLegacyPciBusNumber)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

BOOLEAN
NTAPI
HaliIsVectorValid(_In_ ULONG Vector)
{
    DPRINT("HaliIsVectorValid: Vector %X\n", Vector);

    if (Vector >= 0x10)
    {
        DPRINT1("HaliIsVectorValid: Vector %X\n", Vector);
        //ASSERT(Vector < 0x10);
    }

    return (Vector < 0x10);
}

/* EOF */
