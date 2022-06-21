
/* INCLUDES ******************************************************************/

#include <hal.h>
#include "dispatch.h"
#include "../../../drivers/usb/usbohci/hardware.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern ULONG HalpWAETDeviceFlags;

/* PRIVATE FUNCTIONS *********************************************************/

NTSTATUS
NTAPI
HalpGetChipHacks(
    _In_ USHORT VendorID,
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
        ASSERT(FALSE); // HalpDbgBreakPointEx();
        ChipHacks >>= 12;
    }

    ChipHacks &= 0xFFF;

    if (HalpWAETDeviceFlags & 2)
    {
        DPRINT1("HalpGetChipHacks: HalpWAETDeviceFlags %X\n", HalpWAETDeviceFlags);
        ASSERT(FALSE); // HalpDbgBreakPointEx();
        ChipHacks &= 0xFFFFFFFE;
    }

    *OutChipHacks = ChipHacks;

    ZwClose(KeyHandle);
    KeFlushWriteBuffer();

    return Status;
}

VOID
NTAPI
HalpStopOhciInterrupt(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber)
{
    POHCI_OPERATIONAL_REGISTERS OperationalRegs;
    PHYSICAL_ADDRESS PhysicalAddresses;
    PCI_COMMON_CONFIG PciHeader;
    ULONG ix;

    DPRINT("HalpStopOhciInterrupt: Bus %X, Slot %X\n", BusNumber, SlotNumber);

    HalGetBusData(PCIConfiguration, BusNumber, SlotNumber, &PciHeader, sizeof(PciHeader));

    if (!(PciHeader.Command & PCI_ENABLE_MEMORY_SPACE))
        goto Exit;

    PhysicalAddresses.QuadPart = (PciHeader.u.type0.BaseAddresses[0] & PCI_ADDRESS_MEMORY_ADDRESS_MASK);
    if (!PhysicalAddresses.QuadPart)
        goto Exit;

    OperationalRegs = HalpMapPhysicalMemory64(PhysicalAddresses, 1);
    if (!OperationalRegs)
    {
        DPRINT1("HalpStopOhciInterrupt: failed HalpMapPhysicalMemory64()\n");
        goto Exit;
    }

    if (!OperationalRegs->HcControl.InterruptRouting)
        goto Exit1;

    if (OperationalRegs->HcControl.InterruptRouting &&
        !OperationalRegs->HcInterruptEnable.AsULONG)
    {
        OperationalRegs->HcControl.AsULONG = 0;
        goto Exit1;
    }

    OperationalRegs->HcInterruptDisable.RootHubStatusChange = 1;
    OperationalRegs->HcInterruptEnable.OwnershipChange = 1;
    OperationalRegs->HcInterruptEnable.MasterInterruptEnable = 1;

    OperationalRegs->HcCommandStatus.OwnershipChangeRequest = 1;

    for (ix = 0; ix < 500; ix++)
    {
        KeStallExecutionProcessor(1000);

        if (!OperationalRegs->HcControl.InterruptRouting)
            break;
    }

Exit1:
    OperationalRegs->HcInterruptDisable.MasterInterruptEnable = 1;
    HalpUnmapVirtualAddress(OperationalRegs, 1);

Exit:
    KeFlushWriteBuffer();
}

NTSTATUS
NTAPI
HaliInitPowerManagement(
    _In_ PPM_DISPATCH_TABLE PmDriverDispatchTable,
    _Out_ PPM_DISPATCH_TABLE* PmHalDispatchTable)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

/* PM DISPATCH FUNCTIONS *****************************************************/


/* EOF */
