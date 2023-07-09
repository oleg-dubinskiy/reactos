/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver initialization code
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "acpi.h"

//#define NDEBUG
#include <debug.h>

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DriverEntry)
#endif

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, ACPILoadProcessRSDT)
  #pragma alloc_text(PAGE, ACPILoadProcessFADT)
  #pragma alloc_text(PAGE, ACPILoadFindRSDT)
#endif

/* GLOBALS *******************************************************************/

PCHAR AcpiProcessorCompatId = "ACPI\\Processor";
PCHAR ACPIFixedButtonId = "ACPI\\FixedButton";

PACPI_READ_REGISTER AcpiReadRegisterRoutine = DefPortReadAcpiRegister;
PACPI_WRITE_REGISTER AcpiWriteRegisterRoutine = DefPortWriteAcpiRegister;

PDRIVER_OBJECT AcpiDriverObject;
UNICODE_STRING AcpiRegistryPath;
FAST_IO_DISPATCH ACPIFastIoDispatch;
PDEVICE_EXTENSION RootDeviceExtension;
PRSDTINFORMATION RsdtInformation;
WORK_QUEUE_ITEM ACPIWorkItem;
ARBITER_INSTANCE AcpiArbiter;
PDEVICE_OBJECT AcpiArbiterDeviceObject;
PACPI_VECTOR_BLOCK IrqHashTable;
KDPC AcpiBuildDpc;
KDPC AcpiPowerDpc;
PVOID ACPIThread;

PUCHAR GpeEnable;
PUCHAR GpeCurEnable;
PUCHAR GpeIsLevel;
PUCHAR GpeHandlerType;
PUCHAR GpeWakeEnable;
PUCHAR GpeWakeHandler;
PUCHAR GpeSpecialHandler;
PUCHAR GpePending;
PUCHAR GpeRunMethod;
PUCHAR GpeComplete;
PUCHAR GpeSavedWakeMask;
PUCHAR GpeSavedWakeStatus;
PUCHAR GpeMap;

NPAGED_LOOKASIDE_LIST DeviceExtensionLookAsideList;
NPAGED_LOOKASIDE_LIST BuildRequestLookAsideList;
NPAGED_LOOKASIDE_LIST RequestLookAsideList;
KSPIN_LOCK AcpiDeviceTreeLock;
KSPIN_LOCK AcpiBuildQueueLock;
KSPIN_LOCK ACPIWorkerSpinLock;
KSPIN_LOCK AcpiPowerQueueLock;
KSPIN_LOCK AcpiGetLock;
KSPIN_LOCK AcpiPowerLock;
KEVENT ACPIWorkToDoEvent;
KEVENT ACPITerminateEvent;
LIST_ENTRY ACPIDeviceWorkQueue;
LIST_ENTRY ACPIWorkQueue;
LIST_ENTRY AcpiBuildDeviceList;
LIST_ENTRY AcpiBuildSynchronizationList;
LIST_ENTRY AcpiBuildRunMethodList;
LIST_ENTRY AcpiBuildQueueList;
LIST_ENTRY AcpiBuildOperationRegionList;
LIST_ENTRY AcpiBuildPowerResourceList;
LIST_ENTRY AcpiBuildThermalZoneList;
LIST_ENTRY AcpiPowerDelayedQueueList;
LIST_ENTRY AcpiGetListEntry;
LIST_ENTRY AcpiUnresolvedEjectList;
LIST_ENTRY AcpiPowerSynchronizeList;
LIST_ENTRY AcpiPowerQueueList;
LONG AcpiTableDelta = 0;
ULONG AcpiSciVector;
ULONG AcpiIrqDistributionDisposition;
UCHAR AcpiIrqDefaultBootConfig;
BOOLEAN AcpiLoadSimulatorTable = TRUE;
BOOLEAN AcpiBuildDpcRunning;
BOOLEAN AcpiBuildFixedButtonEnumerated;
BOOLEAN AcpiBuildWorkDone;
BOOLEAN AcpiPowerWorkDone;
BOOLEAN AcpiPowerDpcRunning;
BOOLEAN AcpiArbCardbusPresent;

extern IRP_DISPATCH_TABLE AcpiFdoIrpDispatch;
extern PACPI_INFORMATION AcpiInformation;
extern PAMLI_NAME_SPACE_OBJECT ProcessorList[0x20];
extern ANSI_STRING AcpiProcessorString;
extern ULONG AcpiOverrideAttributes;
extern KSPIN_LOCK GpeTableLock;
extern PPM_DISPATCH_TABLE PmHalDispatchTable;
extern ULONG InterruptModel;

/* ACPI TABLES FUNCTIONS ****************************************************/

USHORT
NTAPI
DefPortReadAcpiRegister(
    _In_ ULONG RegType,
    _In_ ULONG Size)
{
    USHORT RetValue;

    switch (RegType)
    {
        case 0:
            return READ_PORT_USHORT((PUSHORT)(AcpiInformation->PM1a_BLK + 2));

        case 1:
            return READ_PORT_USHORT((PUSHORT)(AcpiInformation->PM1b_BLK + 2));

        case 2:
            return READ_PORT_USHORT((PUSHORT)AcpiInformation->PM1a_BLK);

        case 3:
            return READ_PORT_USHORT((PUSHORT)AcpiInformation->PM1b_BLK);

        case 4:
            return READ_PORT_USHORT((PUSHORT)AcpiInformation->PM1a_CTRL_BLK);

        case 5:
            return READ_PORT_USHORT((PUSHORT)AcpiInformation->PM1b_CTRL_BLK);

        case 6:
            if (Size < AcpiInformation->Gpe0Size)
                RetValue = READ_PORT_UCHAR((PUCHAR)(AcpiInformation->GP0_BLK + Size));
            else
                RetValue = READ_PORT_UCHAR((PUCHAR)(AcpiInformation->GP1_BLK - AcpiInformation->Gpe0Size + Size));
            return (RetValue & 0xFF);

        case 7:
            if (Size >= AcpiInformation->Gpe0Size)
                RetValue = READ_PORT_UCHAR((PUCHAR)(AcpiInformation->GP1_ENABLE - AcpiInformation->Gpe0Size + Size));
            else
                RetValue = READ_PORT_UCHAR((PUCHAR)(AcpiInformation->GP0_ENABLE + Size));
            return (RetValue & 0xFF);

        case 8:
            RetValue = READ_PORT_UCHAR((PUCHAR)AcpiInformation->SMI_CMD);
            return (RetValue & 0xFF);

        default:
            return 0xFFFF;
    }
}

VOID
NTAPI
DefPortWriteAcpiRegister(
    _In_ ULONG RegType,
    _In_ ULONG Size,
    _In_ USHORT Value)
{
    switch (RegType)
    {
        case 0:
            WRITE_PORT_USHORT((PUSHORT)(AcpiInformation->PM1a_BLK + 2), Value);
            break;

        case 1:
            WRITE_PORT_USHORT((PUSHORT)(AcpiInformation->PM1b_BLK + 2), Value);
            break;

        case 2:
            WRITE_PORT_USHORT((PUSHORT)AcpiInformation->PM1a_BLK, Value);
            break;

        case 3:
            WRITE_PORT_USHORT((PUSHORT)AcpiInformation->PM1b_BLK, Value);
            break;

        case 4:
            WRITE_PORT_USHORT((PUSHORT)AcpiInformation->PM1a_CTRL_BLK, Value);
            break;

        case 5:
            WRITE_PORT_USHORT((PUSHORT)AcpiInformation->PM1b_CTRL_BLK, Value);
            break;

        case 6:
            if (Size < AcpiInformation->Gpe0Size)
                WRITE_PORT_UCHAR((PUCHAR)(AcpiInformation->GP0_BLK + Size), Value);
            else
                WRITE_PORT_UCHAR((PUCHAR)(AcpiInformation->GP1_BLK - AcpiInformation->Gpe0Size + Size), Value);
            break;

        case 7:
            if (Size >= AcpiInformation->Gpe0Size)
                WRITE_PORT_UCHAR((PUCHAR)(AcpiInformation->GP1_ENABLE - AcpiInformation->Gpe0Size + Size), Value);
            else
                WRITE_PORT_UCHAR((PUCHAR)(AcpiInformation->GP0_ENABLE + Size), Value);
            break;

        case 8:
            WRITE_PORT_UCHAR((PUCHAR)AcpiInformation->SMI_CMD, Value);
            break;

        default:
            break;
    }
}

USHORT
NTAPI
DefRegisterReadAcpiRegister(
    _In_ ULONG RegType,
    _In_ ULONG Size)
{
    USHORT RetValue;

    switch (RegType)
    {
        case 0:
            return READ_REGISTER_USHORT((PUSHORT)(AcpiInformation->PM1a_BLK + 2));

        case 1:
            return READ_REGISTER_USHORT((PUSHORT)(AcpiInformation->PM1b_BLK + 2));

        case 2:
            return READ_REGISTER_USHORT((PUSHORT)AcpiInformation->PM1a_BLK);

        case 3:
            return READ_REGISTER_USHORT((PUSHORT)AcpiInformation->PM1b_BLK);

        case 4:
            return READ_REGISTER_USHORT((PUSHORT)AcpiInformation->PM1a_CTRL_BLK);

        case 5:
            return READ_REGISTER_USHORT((PUSHORT)AcpiInformation->PM1b_CTRL_BLK);

        case 6:
            if (Size < AcpiInformation->Gpe0Size)
                RetValue = READ_REGISTER_UCHAR((PUCHAR)(AcpiInformation->GP0_BLK + Size));
            else
                RetValue = READ_REGISTER_UCHAR((PUCHAR)(AcpiInformation->GP1_BLK - AcpiInformation->Gpe0Size + Size));
            return (RetValue & 0xFF);

        case 7:
            if (Size >= AcpiInformation->Gpe0Size)
                RetValue = READ_REGISTER_UCHAR((PUCHAR)(AcpiInformation->GP1_ENABLE - AcpiInformation->Gpe0Size + Size));
            else
                RetValue = READ_REGISTER_UCHAR((PUCHAR)(AcpiInformation->GP0_ENABLE + Size));
            return (RetValue & 0xFF);

        case 8:
            RetValue = READ_PORT_UCHAR((PUCHAR)AcpiInformation->SMI_CMD);
            return (RetValue & 0xFF);

        default:
            return 0xFFFF;
    }
}

VOID
NTAPI
DefRegisterWriteAcpiRegister(
    _In_ ULONG RegType,
    _In_ ULONG Size,
    _In_ USHORT Value)
{
    switch (RegType)
    {
        case 0:
            WRITE_REGISTER_USHORT((PUSHORT)(AcpiInformation->PM1a_BLK + 2), Value);
            break;

        case 1:
            WRITE_REGISTER_USHORT((PUSHORT)(AcpiInformation->PM1b_BLK + 2), Value);
            break;

        case 2:
            WRITE_REGISTER_USHORT((PUSHORT)AcpiInformation->PM1a_BLK, Value);
            break;

        case 3:
            WRITE_REGISTER_USHORT((PUSHORT)AcpiInformation->PM1b_BLK, Value);
            break;

        case 4:
            WRITE_REGISTER_USHORT((PUSHORT)AcpiInformation->PM1a_CTRL_BLK, Value);
            break;

        case 5:
            WRITE_REGISTER_USHORT((PUSHORT)AcpiInformation->PM1b_CTRL_BLK, Value);
            break;

        case 6:
            if (Size < AcpiInformation->Gpe0Size)
                WRITE_REGISTER_UCHAR((PUCHAR)(AcpiInformation->GP0_BLK + Size), Value);
            else
                WRITE_REGISTER_UCHAR((PUCHAR)(AcpiInformation->GP1_BLK - AcpiInformation->Gpe0Size + Size), Value);
            break;

        case 7:
            if (Size >= AcpiInformation->Gpe0Size)
                WRITE_REGISTER_UCHAR((PUCHAR)(AcpiInformation->GP1_ENABLE - AcpiInformation->Gpe0Size + Size), Value);
            else
                WRITE_REGISTER_UCHAR((PUCHAR)(AcpiInformation->GP0_ENABLE + Size), Value);
            break;

        case 8:
            WRITE_PORT_UCHAR((PUCHAR)AcpiInformation->SMI_CMD, Value);
            break;

        default:
            break;
    }
}

USHORT
NTAPI
ACPIReadGpeStatusRegister(
    _In_ ULONG Size)
{
    DPRINT("ACPIReadGpeStatusRegister: Size %X\n", Size);
    return AcpiReadRegisterRoutine(6, Size);
}

VOID
NTAPI
ACPIWriteGpeStatusRegister(
    _In_ ULONG Size,
    _In_ UCHAR Value)
{
    AcpiWriteRegisterRoutine(6, Size, Value);
}

VOID
NTAPI
ACPIWriteGpeEnableRegister(
    _In_ ULONG Size,
    _In_ UCHAR Value)
{
    DPRINT("ACPIWriteGpeEnableRegister: Writing GPE Enable register %X = %X\n", Size, Value);
    AcpiWriteRegisterRoutine(7, Size, Value);
}

PRSDT
NTAPI
ACPILoadFindRSDT(VOID)
{
    PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64 KeyInfo;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PACPI_BIOS_MULTI_NODE AcpiMultiNode;
    PRSDT Rsdt;
    PRSDT OutRsdt;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPILoadFindRSDT()\n");

    Status = OSReadAcpiConfigurationData(&KeyInfo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPILoadFindRSDT: Cannot open Configuration Data - %X\n", Status);
        DbgBreakPoint();
        return NULL;
    }

    PartialResourceList = (PCM_PARTIAL_RESOURCE_LIST)KeyInfo->Data;
    AcpiMultiNode = (PACPI_BIOS_MULTI_NODE)((PUCHAR)&PartialResourceList->PartialDescriptors[0] + sizeof(CM_PARTIAL_RESOURCE_LIST));

  #if !defined(_M_AMD64)
    ASSERT(AcpiMultiNode->RsdtAddress.HighPart == 0);
  #endif

    Rsdt = MmMapIoSpace(AcpiMultiNode->RsdtAddress, sizeof(DESCRIPTION_HEADER), MmNonCached);
    if (!Rsdt)
    {
        DPRINT1("ACPILoadFindRSDT: Cannot Map RSDT Pointer %X\n", AcpiMultiNode->RsdtAddress.LowPart);
        DbgBreakPoint();
        ExFreePool(KeyInfo);
        return NULL;
    }

    if (Rsdt->Header.Signature == 'TDSR' || Rsdt->Header.Signature == 'TDSX')
    {
      #if !defined(_M_AMD64)
        ASSERT(AcpiMultiNode->RsdtAddress.HighPart == 0);
      #endif

        OutRsdt = MmMapIoSpace(AcpiMultiNode->RsdtAddress, Rsdt->Header.Length, MmNonCached);
        MmUnmapIoSpace(Rsdt, sizeof(DESCRIPTION_HEADER));

        if (!OutRsdt)
        {
            DPRINT1("ACPILoadFindRSDT: Cannot Map RSDT Pointer %X\n", AcpiMultiNode->RsdtAddress.LowPart);
            DbgBreakPoint();
            ExFreePool(KeyInfo);
            return NULL;
        }
    }
    else
    {
        DPRINT1("ACPILoadFindRSDT: RSDT %X has invalid signature\n", Rsdt);
        DbgBreakPoint();
        MmUnmapIoSpace(Rsdt, sizeof(DESCRIPTION_HEADER));
    }

    ExFreePool(KeyInfo);

    return OutRsdt;
}

NTSTATUS
NTAPI
ACPILoadProcessFACS(
    _In_ ULONG_PTR FacsPointer)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PFACS Facs;

    DPRINT("ACPILoadProcessFACS: %IX\n", FacsPointer);

    if (!FacsPointer)
        return STATUS_SUCCESS;

    PhysicalAddress.QuadPart = (ULONGLONG)FacsPointer;

  #if !defined(_M_AMD64)
    ASSERT(PhysicalAddress.HighPart == 0);
  #endif

    Facs = MmMapIoSpace(PhysicalAddress, sizeof(FACS), MmNonCached);
    if (!Facs)
    {
        ASSERT(Facs != NULL);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Facs->Signature != 'SCAF')
    {
        DPRINT1("ACPILoadProcessFACS: %X does not have FACS signature\n", Facs);
        return STATUS_ACPI_INVALID_TABLE;
    }

    if (Facs->Length != sizeof(FACS))
    {
        DPRINT1("ACPILoadProcessFACS: %X does not have correct FACS length\n", Facs);
        return STATUS_ACPI_INVALID_TABLE;
    }

    DPRINT("ACPILoadProcessFACS: FACS located at %X\n", Facs);

    AcpiInformation->FirmwareACPIControlStructure = Facs;
    AcpiInformation->GlobalLock = &Facs->GlobalLock;

    DPRINT("ACPILoadProcessFACS: Initial GlobalLock state: %X\n", *AcpiInformation->GlobalLock);

    return STATUS_SUCCESS;
}

ULONG
NTAPI
GetFadtTablePointerEntry(
    _In_ PFADT Fadt,
    _In_ PULONG IoPort,
    _In_ PGEN_ADDR GenAddr,
    _In_ SIZE_T NumberOfBytes)
{
    ULONG RetValue;

  #if !defined(_M_AMD64)
    ASSERT(GenAddr->Address.HighPart == 0);
  #endif

    //DPRINT("GetFadtTablePointerEntry: %p, %p, %p, %X, %X\n", Fadt, IoPort, GenAddr, NumberOfBytes, Fadt->Header.Revision);

    if (Fadt->Header.Revision >= 3)
    {
        //DPRINT("GetFadtTablePointerEntry: AddressSpaceID %X\n", GenAddr->AddressSpaceID);
        if (GenAddr->AddressSpaceID)
        {
            if (GenAddr->AddressSpaceID != 1)
            {
                DPRINT1("GetFadtTablePointerEntry: AddressSpaceID %X\n", GenAddr->AddressSpaceID);
                ASSERT(FALSE);
            }
        }
        else
        {
            //DPRINT("GetFadtTablePointerEntry: Address.QuadPart %I64X\n", GenAddr->Address.QuadPart);
            if (GenAddr->Address.QuadPart)
            {
                if (MmMapIoSpace(GenAddr->Address, NumberOfBytes, MmNonCached))
                {
                    DPRINT("GetFadtTablePointerEntry: %p, %p, %p, %p\n", AcpiReadRegisterRoutine, DefPortReadAcpiRegister, AcpiWriteRegisterRoutine, DefPortWriteAcpiRegister);

                    if (AcpiReadRegisterRoutine == DefPortReadAcpiRegister &&
                        AcpiWriteRegisterRoutine == DefPortWriteAcpiRegister)
                    {
                        AcpiReadRegisterRoutine = DefRegisterReadAcpiRegister;
                        AcpiWriteRegisterRoutine = DefRegisterWriteAcpiRegister;
                    }
                }
                else
                {
                    DPRINT1("GetFadtTablePointerEntry: Address.QuadPart %I64X\n", GenAddr->Address.QuadPart);
                    ASSERT(FALSE);
                }
            }
        }
    }

    //DPRINT("GetFadtTablePointerEntry: IoPort %X\n", IoPort);
    RetValue = *IoPort;

    if (AcpiReadRegisterRoutine == DefRegisterReadAcpiRegister &&
        AcpiWriteRegisterRoutine == DefRegisterWriteAcpiRegister)
    {
        AcpiReadRegisterRoutine = DefPortReadAcpiRegister;
        AcpiWriteRegisterRoutine = DefPortWriteAcpiRegister;
    }

    DPRINT("GetFadtTablePointerEntry: RetValue %X\n", RetValue);

    return RetValue;
}

VOID
NTAPI
ACPIGpeClearRegisters(VOID)
{
    ULONG ix;
    UCHAR Status;

    DPRINT("ACPIGpeClearRegisters: GpeSize %X\n", AcpiInformation->GpeSize);

    for (ix = 0; ix < AcpiInformation->GpeSize; ix++)
    {
        Status = ACPIReadGpeStatusRegister(ix);
        ACPIWriteGpeStatusRegister(ix, (Status & (GpeEnable[ix] | GpeWakeEnable[ix])));
    }
}

VOID
NTAPI
ACPIGpeEnableDisableEvents(
    _In_ BOOLEAN IsEnableEvents)
{
    ULONG ix;

    DPRINT("ACPIGpeEnableDisableEvents: GpeSize %X\n", AcpiInformation->GpeSize);

    for (ix = 0; ix < AcpiInformation->GpeSize; ix++)
    {
        ACPIWriteGpeEnableRegister(ix, (IsEnableEvents == FALSE ? 0 : GpeCurEnable[ix]));
    }
}

NTSTATUS
NTAPI
ACPILoadProcessDSDT(
    _In_ PHYSICAL_ADDRESS PhysicalAddress)
{
    PDSDT Dsdt;
    ULONG numElements;
    ULONG Length;

    DPRINT("ACPILoadProcessDSDT: PhysicalAddress %I64X\n", PhysicalAddress.QuadPart);

  #if !defined(_M_AMD64)
    ASSERT(PhysicalAddress.HighPart == 0);
  #endif

    Dsdt = MmMapIoSpace(PhysicalAddress, sizeof(DESCRIPTION_HEADER), MmNonCached);
    if (!Dsdt)
    {
        DPRINT1("ACPILoadProcessDSDT: not mapped Dsdt\n");
        ASSERT(Dsdt != NULL);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if ( Dsdt->Header.Signature != 'TDSD')
    {
        DPRINT1("ACPILoadProcessDSDT: %X does not have DSDT signature\n", Dsdt);
        return STATUS_ACPI_INVALID_TABLE;
    }

    Length = Dsdt->Header.Length;
    MmUnmapIoSpace(Dsdt, sizeof(DESCRIPTION_HEADER));

  #if !defined(_M_AMD64)
    ASSERT(PhysicalAddress.HighPart == 0);
  #endif

    Dsdt = MmMapIoSpace(PhysicalAddress, Length, MmNonCached);
    if (!Dsdt)
    {
        DPRINT1("ACPILoadProcessDSDT: not mapped Dsdt\n");
        ASSERT(Dsdt != NULL);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!RsdtInformation->NumElements)
    {
        return STATUS_ACPI_NOT_INITIALIZED;
    }

    numElements = (RsdtInformation->NumElements - 1);

    if (ACPIRegReadAMLRegistryEntry((PDESCRIPTION_HEADER *)&Dsdt, TRUE))
    {
        DPRINT1("ACPILoadProcessDSDT: DSDT Overloaded from registry (%X)\n", Dsdt);
        RsdtInformation->Tables[numElements].Flags |= 8;
    }

    AcpiInformation->DiffSystemDescTable = Dsdt;

    RsdtInformation->Tables[numElements].Flags |= 5;
    RsdtInformation->Tables[numElements].Address = Dsdt;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPILoadProcessFADT(
    _In_ PFADT Fadt)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID GpeTables;
    ULONG Size;
    BOOLEAN IsEnableEvents = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPILoadProcessFADT: Fadt %p\n", Fadt);

    Status = ACPILoadProcessFACS(Fadt->facs);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPILoadProcessFADT: Status %X\n", Status);
        return Status;
    }

    //DPRINT("ACPILoadProcessFADT: %I64X\n", Fadt->x_pm1a_evt_blk.Address.QuadPart);
    AcpiInformation->PM1a_BLK = GetFadtTablePointerEntry(Fadt, &Fadt->pm1a_evt_blk_io_port, &Fadt->x_pm1a_evt_blk, 4);

    //DPRINT("ACPILoadProcessFADT: %I64X\n", Fadt->x_pm1b_evt_blk.Address.QuadPart);
    AcpiInformation->PM1b_BLK = GetFadtTablePointerEntry(Fadt, &Fadt->pm1b_evt_blk_io_port, &Fadt->x_pm1b_evt_blk, 4);

    //DPRINT("ACPILoadProcessFADT: %I64X\n", Fadt->x_pm1a_ctrl_blk.Address.QuadPart);
    AcpiInformation->PM1a_CTRL_BLK = GetFadtTablePointerEntry(Fadt, &Fadt->pm1a_ctrl_blk_io_port, &Fadt->x_pm1a_ctrl_blk, 4);

    //DPRINT("ACPILoadProcessFADT: %I64X\n", Fadt->x_pm1b_ctrl_blk.Address.QuadPart);
    AcpiInformation->PM1b_CTRL_BLK = GetFadtTablePointerEntry(Fadt, &Fadt->pm1b_ctrl_blk_io_port, &Fadt->x_pm1b_ctrl_blk, 4);

    //DPRINT("ACPILoadProcessFADT: %I64X\n", Fadt->x_pm2_ctrl_blk.Address.QuadPart);
    AcpiInformation->PM2_CTRL_BLK = GetFadtTablePointerEntry(Fadt, &Fadt->pm2_ctrl_blk_io_port, &Fadt->x_pm2_ctrl_blk, 4);

    //DPRINT("ACPILoadProcessFADT: %I64X\n", Fadt->x_pm_tmr_blk.Address.QuadPart);
    AcpiInformation->PM_TMR = GetFadtTablePointerEntry(Fadt, &Fadt->pm_tmr_blk_io_port, &Fadt->x_pm_tmr_blk, 4);

    AcpiInformation->SMI_CMD = Fadt->smi_cmd_io_port;

    DPRINT("ACPILoadProcessFADT: PM1a_BLK located at port %p\nACPILoadProcessFADT: PM1b_BLK located at port %p\n", AcpiInformation->PM1a_BLK, AcpiInformation->PM1b_BLK);
    DPRINT("ACPILoadProcessFADT: PM1a_CTRL_BLK located at port %p\nACPILoadProcessFADT: PM1b_CTRL_BLK located at port %p\n", AcpiInformation->PM1a_CTRL_BLK, AcpiInformation->PM1b_CTRL_BLK);
    DPRINT("ACPILoadProcessFADT: PM2_CTRL_BLK located at port %p\nACPILoadProcessFADT: PM_TMR located at port %p\n", AcpiInformation->PM2_CTRL_BLK, AcpiInformation->PM_TMR);

    AcpiInformation->GP1_Base_Index = -1;

    //DPRINT("ACPILoadProcessFADT: %I64X\n", Fadt->x_gp0_blk.Address.QuadPart);
    AcpiInformation->GP0_BLK = GetFadtTablePointerEntry(Fadt, &Fadt->gp0_blk_io_port, &Fadt->x_gp0_blk, Fadt->gp0_blk_len);
    if (AcpiInformation->GP0_BLK)
    {
        AcpiInformation->GP0_LEN = Fadt->gp0_blk_len;
        //ACPIAssert(Fadt->gp0_blk_len != 0, 0x100B, 0, 0, 5);
        ASSERT(Fadt->gp0_blk_len != 0);
    }

    //DPRINT("ACPILoadProcessFADT: %I64X\n", Fadt->x_gp1_blk.Address.QuadPart);
    AcpiInformation->GP1_BLK = GetFadtTablePointerEntry(Fadt, &Fadt->gp1_blk_io_port, &Fadt->x_gp1_blk, Fadt->gp1_blk_len);
    if (AcpiInformation->GP1_BLK)
    {
        AcpiInformation->GP1_LEN = Fadt->gp1_blk_len;
        AcpiInformation->GP1_Base_Index = Fadt->gp1_base;
        //ACPIAssert(Fadt->gp1_blk_len != 0, 0x100C, 0, 0, 5);
        ASSERT(Fadt->gp1_blk_len != 0);
    }

    AcpiInformation->Gpe0Size = ((UCHAR)AcpiInformation->GP0_LEN >> 1);
    AcpiInformation->Gpe1Size = ((UCHAR)AcpiInformation->GP1_LEN >> 1);

    AcpiInformation->GpeSize = (AcpiInformation->Gpe0Size + AcpiInformation->Gpe1Size);

    AcpiInformation->GP0_ENABLE = (AcpiInformation->GP0_BLK + AcpiInformation->Gpe0Size);
    AcpiInformation->GP1_ENABLE = (AcpiInformation->GP1_BLK + AcpiInformation->Gpe1Size);

    if (AcpiInformation->GpeSize)
    {
        Size = ((12 + 8) * AcpiInformation->GpeSize);

        GpeTables = ExAllocatePoolWithTag(NonPagedPool, Size, 'gpcA');
        if (!GpeTables)
        {
            DPRINT1("ACPILoadProcessFADT: Could not allocate GPE tables, size = %X\n", Size);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(GpeTables, Size);

        GpeEnable = GpeTables;
        GpeCurEnable = Add2Ptr(GpeTables, AcpiInformation->GpeSize);
        GpeIsLevel = Add2Ptr(GpeCurEnable, AcpiInformation->GpeSize);
        GpeHandlerType = Add2Ptr(GpeIsLevel, AcpiInformation->GpeSize);
        GpeWakeEnable = Add2Ptr(GpeHandlerType, AcpiInformation->GpeSize);
        GpeWakeHandler = Add2Ptr(GpeWakeEnable, AcpiInformation->GpeSize);
        GpeSpecialHandler = Add2Ptr(GpeWakeHandler, AcpiInformation->GpeSize);
        GpePending = Add2Ptr(GpeSpecialHandler, AcpiInformation->GpeSize);
        GpeRunMethod = Add2Ptr(GpePending, AcpiInformation->GpeSize);
        GpeComplete = Add2Ptr(GpeRunMethod, AcpiInformation->GpeSize);
        GpeSavedWakeMask = Add2Ptr(GpeComplete, AcpiInformation->GpeSize);
        GpeSavedWakeStatus = Add2Ptr(GpeSavedWakeMask, AcpiInformation->GpeSize);
        GpeMap = Add2Ptr(GpeSavedWakeStatus, AcpiInformation->GpeSize); // size of GpeMap = (8 * AcpiInformation->GpeSize)

        IsEnableEvents = FALSE;
    }

    DPRINT("ACPILoadProcessFADT: GP0_BLK located at port %p length %X\nACPILoadProcessFADT: GP1_BLK located at port %p length %X\nACPILoadProcessFADT: GP1_Base_Index = %X\n",
           AcpiInformation->GP0_BLK, AcpiInformation->GP0_LEN, AcpiInformation->GP1_BLK, AcpiInformation->GP1_LEN, AcpiInformation->GP1_Base_Index);

    ACPIGpeClearRegisters();
    ACPIGpeEnableDisableEvents(IsEnableEvents);

    AcpiInformation->ACPI_Flags = 0;
    AcpiInformation->ACPI_Capabilities = 0;
    AcpiInformation->Dockable = ((Fadt->flags >> 9) & 1);
    AcpiInformation->pm1_en_bits = 0x21;

    if (Fadt->flags & 0x10)
    {
        DPRINT("ACPILoadProcessFADT: Power Button not fixed event or not present\n");
    }
    else
    {
        AcpiInformation->pm1_en_bits |= 0x100;
        DPRINT("ACPILoadProcessFADT: Power Button in Fixed Feature Space\n");
    }

    if (Fadt->flags & 0x20)
    {
        DPRINT("ACPILoadProcessFADT: Sleep Button not fixed event or not present\n");
    }
    else
    {
        AcpiInformation->pm1_en_bits |= 0x200;
        DPRINT("ACPILoadProcessFADT: Sleep Button in Fixed Feature Space\n");
    }

    PhysicalAddress.QuadPart = (ULONGLONG)Fadt->dsdt;

    Status = ACPILoadProcessDSDT(PhysicalAddress);
    return Status;
}

NTSTATUS
NTAPI
ACPILoadProcessRSDT(VOID)
{
    PDESCRIPTION_HEADER MappedAddress;
    PDESCRIPTION_HEADER SimulatorTable;
    PHYSICAL_ADDRESS PhysicalAddress;
    PRSDT Rsdt;
    PXSDT Xsdt;
    ULONG NumElements;
    ULONG FullSize;
    ULONG Offset;
    ULONG Size;
    ULONG ix;
    BOOLEAN IsForceSuccesStatus = FALSE;
    BOOLEAN IsXsdtTable = FALSE;
    BOOLEAN Result = FALSE;

    PAGED_CODE();
    DPRINT("ACPILoadProcessRSDT()\n");

    FullSize = AcpiInformation->RootSystemDescTable->Header.Length;
    Offset = FIELD_OFFSET(RSDT, Tables);

    if (AcpiInformation->RootSystemDescTable->Header.Signature == 'TDSX')
    {
        Xsdt = (PXSDT)AcpiInformation->RootSystemDescTable;

        if (FullSize < Offset)
            Offset = AcpiInformation->RootSystemDescTable->Header.Length;

        NumElements = ((FullSize - Offset) / sizeof(PHYSICAL_ADDRESS));

        IsXsdtTable = TRUE;
    }
    else
    {
        Rsdt = (PRSDT)AcpiInformation->RootSystemDescTable;

        if (FullSize < Offset)
            Offset = AcpiInformation->RootSystemDescTable->Header.Length;

        NumElements = ((FullSize - Offset) / sizeof(ULONG));
    }

    DPRINT("ACPILoadProcessRSDT: RSDT contains %u tables\n", NumElements);

    if (!NumElements)
    {
        DPRINT1("ACPILoadProcessRSDT: STATUS_ACPI_INVALID_TABLE\n");
        return STATUS_ACPI_INVALID_TABLE;
    }

    Size = (sizeof(RSDTINFORMATION) + ((NumElements + 1) * sizeof(RSDTELEMENT)));

    RsdtInformation = ExAllocatePoolWithTag(NonPagedPool, Size, 'tpcA');
    if (!RsdtInformation)
    {
        DPRINT1("ACPILoadProcessRSDT: STATUS_ACPI_INVALID_TABLE\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(RsdtInformation, Size);

    RsdtInformation->NumElements = (NumElements + 2);

    for (ix = 0; ix < NumElements; ix++)
    {
        DPRINT("ACPILoadProcessRSDT: ix %X \n", ix);

        if (IsXsdtTable)
            PhysicalAddress.QuadPart = Xsdt->Tables[ix].QuadPart;
        else
            PhysicalAddress.QuadPart = Rsdt->Tables[ix];

      #if !defined(_M_AMD64)
        ASSERT(PhysicalAddress.HighPart == 0);
      #endif

        MappedAddress = MmMapIoSpace(PhysicalAddress, sizeof(DESCRIPTION_HEADER), MmNonCached);
        if (!MappedAddress)
        {
            DPRINT1("ACPILoadProcessRSDT: STATUS_ACPI_INVALID_TABLE\n");
            ASSERT(MappedAddress != NULL);
            return STATUS_ACPI_INVALID_TABLE;
        }

        if (MappedAddress->Signature == 'TSBS')
        {
            DPRINT("ACPILoadProcessRSDT: SBST Found at %X\n", MappedAddress);
            MmUnmapIoSpace(MappedAddress, sizeof(DESCRIPTION_HEADER));
            continue;
        }

        if (MappedAddress->Signature != 'PCAF' &&
            MappedAddress->Signature != 'TDSS' &&
            MappedAddress->Signature != 'TDSP' &&
            MappedAddress->Signature != 'CIPA')
        {
            DPRINT("ACPILoadProcessRSDT: Unrecognized table signature %X\n", MappedAddress->Signature);
            MmUnmapIoSpace(MappedAddress, sizeof(DESCRIPTION_HEADER));
            continue;
        }

        Size = MappedAddress->Length;
        MmUnmapIoSpace(MappedAddress, sizeof(DESCRIPTION_HEADER));

      #if !defined(_M_AMD64)
        ASSERT(PhysicalAddress.HighPart == 0);
      #endif

        MappedAddress = MmMapIoSpace(PhysicalAddress, Size, MmNonCached);
        if (!MappedAddress)
        {
            DPRINT1("ACPILoadProcesRSDT: Could not load table at %X\n", AcpiInformation->RootSystemDescTable->Tables[ix]);
            return STATUS_ACPI_INVALID_TABLE;
        }

        Result = ACPIRegReadAMLRegistryEntry(&MappedAddress, TRUE);
        if (Result)
        {
            DPRINT1("ACPILoadProcessRSDT: Table Overloaded from registry (%X)\n", MappedAddress);
            RsdtInformation->Tables[ix].Flags |= 8;
        }

        RsdtInformation->Tables[ix].Flags |= 1;
        RsdtInformation->Tables[ix].Address = MappedAddress;

        if (MappedAddress->Signature == 'PCAF')
        {
            AcpiInformation->FixedACPIDescTable = (PFADT)MappedAddress;
            IsForceSuccesStatus = TRUE;
            ACPILoadProcessFADT(AcpiInformation->FixedACPIDescTable);
        }
        else if (MappedAddress->Signature == 'CIPA')
        {
            AcpiInformation->MultipleApicTable = (PMAPIC)MappedAddress;
        }
        else
        {
            RsdtInformation->Tables[ix].Flags |= 4;
        }
    }

    Size = sizeof(*SimulatorTable);
    DPRINT("ACPILoadProcessRSDT: Size %X \n", Size);

    SimulatorTable = ExAllocatePoolWithTag(NonPagedPool, Size, 'tpcA');
    if (SimulatorTable)
    {
        RtlZeroMemory(SimulatorTable, Size);

        SimulatorTable->Signature = 'TDSS';
        SimulatorTable->Length = Size;
        SimulatorTable->Revision = 1;
        SimulatorTable->Checksum = 0;
        SimulatorTable->OEMRevision = 1;
        SimulatorTable->CreatorRev = 1;

        RtlCopyMemory(SimulatorTable->OEMID, "MSFT", 4 );
        RtlCopyMemory(SimulatorTable->OEMTableID, "simulatr", 8);
        RtlCopyMemory(SimulatorTable->CreatorID, "MSFT", 4);

        if (AcpiLoadSimulatorTable)
        {
            Result = ACPIRegReadAMLRegistryEntry(&SimulatorTable, FALSE);
        }

        if (Result)
        {
            DPRINT("ACPILoadProcessRSDT: Simulator Table Overloaded from registry (%X)\n", MappedAddress);

            RsdtInformation->Tables[NumElements].Flags |= (1 + 4 + 8);
            RsdtInformation->Tables[NumElements].Address = SimulatorTable;
        }
        else
        {
            ExFreePoolWithTag(SimulatorTable, 'tpcA');
        }
    }

    DPRINT("ACPILoadProcessRSDT: Size %X \n", Size);
    ACPIRegDumpAcpiTables();

    if (IsForceSuccesStatus)
        return STATUS_SUCCESS;

    DPRINT1("ACPILoadProcessRSDT: Did not find an FADT\n");
    return STATUS_ACPI_INVALID_TABLE;
}

/* ACPI INIT FUNCTIONS ******************************************************/

VOID
NTAPI
CLEAR_PM1_STATUS_REGISTER(VOID)
{
    USHORT Value;

    if (AcpiInformation->PM1a_BLK)
    {
        Value = AcpiReadRegisterRoutine(2, 0);
        AcpiWriteRegisterRoutine(2, 0, Value);
    }

    if (AcpiInformation->PM1b_BLK)
    {
        Value = AcpiReadRegisterRoutine(3, 0);
        AcpiWriteRegisterRoutine(3, 0, Value);
    }
}

USHORT
NTAPI
READ_PM1_CONTROL(VOID)
{
    USHORT RetValue = 0;

    if (AcpiInformation->PM1a_CTRL_BLK)
        RetValue = AcpiReadRegisterRoutine(4, 0);

    if (AcpiInformation->PM1b_CTRL_BLK)
        RetValue |= AcpiReadRegisterRoutine(5, 0);

    return RetValue;
}

USHORT
NTAPI
READ_PM1_STATUS(VOID)
{
    USHORT RetValue = 0;

    if (AcpiInformation->PM1a_BLK)
        RetValue = AcpiReadRegisterRoutine(2, 0);

    if (AcpiInformation->PM1b_BLK)
        RetValue |= AcpiReadRegisterRoutine(3, 0);

    return RetValue;
}

USHORT
NTAPI
READ_PM1_ENABLE(VOID)
{
    USHORT RetValue = 0;

    if (AcpiInformation->PM1a_BLK)
        RetValue = AcpiReadRegisterRoutine(0, 0);

    if (AcpiInformation->PM1b_BLK)
        RetValue |= AcpiReadRegisterRoutine(1, 0);

    return RetValue;
}

VOID
NTAPI
WRITE_PM1_ENABLE(
    _In_ USHORT Value)
{
    if (AcpiInformation->PM1a_BLK)
      AcpiWriteRegisterRoutine(0, 0, Value);

    if (AcpiInformation->PM1b_BLK)
      AcpiWriteRegisterRoutine(1, 0, Value);
}

VOID
NTAPI
WRITE_PM1_CONTROL(
    _In_ USHORT Value,
    _In_ BOOLEAN Param2,
    _In_ UCHAR Flags)
{
    USHORT value;
  
    if (Param2)
    {
        ASSERT((Flags & 4) || (Value & 1));

        if (Flags & 1 && AcpiInformation->PM1a_BLK)
            AcpiWriteRegisterRoutine(4, 0, Value);

        if (Flags & 2 && AcpiInformation->PM1b_BLK)
            AcpiWriteRegisterRoutine(5, 0, Value);

        return;
    }

    if ((Flags & 1) && AcpiInformation->PM1a_BLK)
    {
        value = AcpiReadRegisterRoutine(4, 0);
        AcpiWriteRegisterRoutine(4, 0, (Value | value));
    }

    if ((Flags & 2) && AcpiInformation->PM1b_BLK)
    {
        value = AcpiReadRegisterRoutine(5, 0);
        AcpiWriteRegisterRoutine(5, 0, (Value | value));
    }
}

VOID
NTAPI
ACPIEnableEnterACPIMode(
    _In_ BOOLEAN IsNotRevertAffinity)
{
    BOOLEAN IsNeedRevert = FALSE;
    UCHAR acpi_on_value;
    ULONG ix;

    ASSERTMSG("ACPIEnableEnterACPIMode: System already in ACPI mode!\n", !(READ_PM1_CONTROL() & 1));
    ASSERTMSG("ACPIEnableEnterACPIMode: System SMI_CMD port is zero\n", (AcpiInformation->SMI_CMD != 0));

    DPRINT("ACPIEnableEnterACPIMode: Enabling ACPI\n");

    if (!IsNotRevertAffinity)
    {
        if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
        {
            ASSERTMSG("ACPIEnableEnterACPIMode: IRQL >= DISPATCH_LEVEL\n", FALSE);
        }
        else
        {
            KeSetSystemAffinityThread(1);
            IsNeedRevert = TRUE;
        }
    }

    acpi_on_value = AcpiInformation->FixedACPIDescTable->acpi_on_value;

    AcpiWriteRegisterRoutine(8, 0, acpi_on_value);

    for (ix = 0; !(READ_PM1_CONTROL() & 1); ix++)
    {
        if (ix > 0xFFFFFF)
        {
            KeBugCheckEx(0xA5, 0x11, 6, 0, 0);
        }
    }

    if (IsNeedRevert)
        KeRevertToUserAffinityThread();
}

VOID
NTAPI
ACPIEnableInitializeACPI(
    _In_ BOOLEAN IsNotRevertAffinity)
{
    USHORT pm1_control;
    USHORT contents;

    if (!(READ_PM1_CONTROL() & 1))
    {
        AcpiInformation->ACPIOnly = FALSE;
        ACPIEnableEnterACPIMode(IsNotRevertAffinity);
    }

    CLEAR_PM1_STATUS_REGISTER();

    contents = (READ_PM1_STATUS() & 0xFBEF);
    if (contents)
    {
        CLEAR_PM1_STATUS_REGISTER();

        contents = (READ_PM1_STATUS() & 0xFBEF);
        ASSERTMSG("ACPIEnableInitializeACPI: Cannot clear PM1 Status Register\n", (contents == 0));
    }

    WRITE_PM1_ENABLE(AcpiInformation->pm1_en_bits);

    ASSERTMSG("ACPIEnableInitializeACPI: Cannot write all PM1 Enable Bits\n", (READ_PM1_ENABLE() == AcpiInformation->pm1_en_bits));

    if (IsNotRevertAffinity)
    {
        ACPIGpeClearRegisters();
        ACPIGpeEnableDisableEvents(1);
    }

    pm1_control = READ_PM1_CONTROL();
    WRITE_PM1_CONTROL((pm1_control & ~0x2002), 1, 3);
}

NTSTATUS
NTAPI
ACPIBuildDeviceExtension(
    _In_ PAMLI_NAME_SPACE_OBJECT AcpiObject,
    _In_ PDEVICE_EXTENSION ParentDeviceExtension,
    _Out_ PDEVICE_EXTENSION* OutDeviceExtension)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    DPRINT("ACPIBuildDeviceExtension: Parent %p\n", ParentDeviceExtension);

    if (ParentDeviceExtension)
    {
        ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    }

    if (AcpiObject && AcpiObject->Context)
    {
        DeviceExtension = AcpiObject->Context;
        ASSERT(DeviceExtension->ParentExtension == ParentDeviceExtension);
        Status = ((DeviceExtension->ParentExtension != ParentDeviceExtension) ? STATUS_NO_SUCH_DEVICE : STATUS_SUCCESS);
        return Status;
    }

    DeviceExtension = ExAllocateFromNPagedLookasideList(&DeviceExtensionLookAsideList);
    if (!DeviceExtension)
    {
        DPRINT1("ACPIBuildDeviceExtension: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    DeviceExtension->ReferenceCount++;
    DeviceExtension->OutstandingIrpCount++;
    DeviceExtension->AcpiObject = AcpiObject;
    DeviceExtension->Signature = '_SGP';
    DeviceExtension->Flags = 0xA;

    *OutDeviceExtension = DeviceExtension;

    DeviceExtension->PowerInfo.DevicePowerMatrix[0] = 0;
    DeviceExtension->PowerInfo.DevicePowerMatrix[1] = 1;
    DeviceExtension->PowerInfo.DevicePowerMatrix[2] = 1;
    DeviceExtension->PowerInfo.DevicePowerMatrix[3] = 1;
    DeviceExtension->PowerInfo.DevicePowerMatrix[4] = 1;
    DeviceExtension->PowerInfo.DevicePowerMatrix[5] = 4;
    DeviceExtension->PowerInfo.DevicePowerMatrix[6] = 4;

    InitializeListHead(&DeviceExtension->ChildDeviceList);
    InitializeListHead(&DeviceExtension->EjectDeviceHead);
    InitializeListHead(&DeviceExtension->EjectDeviceList);
    InitializeListHead(&DeviceExtension->PowerInfo.WakeSupportList);
    InitializeListHead(&DeviceExtension->PowerInfo.PowerRequestListEntry);

    DeviceExtension->ParentExtension = ParentDeviceExtension;

    if (ParentDeviceExtension)
    {
        InterlockedIncrement(&ParentDeviceExtension->ReferenceCount);
        InsertTailList(&ParentDeviceExtension->ChildDeviceList, &DeviceExtension->SiblingDeviceList);
    }

    if (AcpiObject)
        AcpiObject->Context = DeviceExtension;

    return STATUS_SUCCESS;
}

/* ACPI CALLBACKS ***********************************************************/

VOID
NTAPI
ACPIGpeClearEventMasks(VOID)
{
    //UNIMPLEMENTED_DBGBREAK();
    UNIMPLEMENTED;
}

VOID
NTAPI
ACPIGpeBuildEventMasks(VOID)
{
    //UNIMPLEMENTED_DBGBREAK();
    UNIMPLEMENTED;
}

VOID
NTAPI
ACPITableLoadCallBack(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PVOID Param2,
    _In_ NTSTATUS Param3)
{
    DPRINT("ACPITableLoadCallBack: %p\n", DeviceExtension);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    KeAcquireSpinLockAtDpcLevel(&AcpiDeviceTreeLock);
    KeAcquireSpinLockAtDpcLevel(&GpeTableLock);

    DPRINT("ACPITableLoadCallBack: FIXME ACPIGpeBuildWakeMasks()\n");

    KeReleaseSpinLockFromDpcLevel(&GpeTableLock);
    KeReleaseSpinLockFromDpcLevel(&AcpiDeviceTreeLock);

    KeAcquireSpinLockAtDpcLevel(&AcpiPowerQueueLock);

    if (!IsListEmpty(&AcpiPowerDelayedQueueList))
    {
        DPRINT1("ACPITableLoadCallBack: FIXME\n");
        ASSERT(FALSE);
    }

    KeReleaseSpinLockFromDpcLevel(&AcpiPowerQueueLock);
}

USHORT
NTAPI
ACPIEnableQueryFixedEnables(VOID)
{
    return AcpiInformation->pm1_en_bits;
}

NTSTATUS
NTAPI
ACPIBuildFixedButtonExtension(
    _In_ PDEVICE_EXTENSION RootDeviceExtension,
    _Out_ PDEVICE_EXTENSION* OutDeviceExtension)
{
    PDEVICE_EXTENSION DeviceExtension;
    ULONG ButtonCaps;
    USHORT EnBits;
    NTSTATUS Status;

    DPRINT("ACPIBuildFixedButtonExtension: %p\n", RootDeviceExtension);

    if (AcpiBuildFixedButtonEnumerated)
    {
        *OutDeviceExtension = NULL;
        return STATUS_SUCCESS;
    }

    AcpiBuildFixedButtonEnumerated = TRUE;

    EnBits = ACPIEnableQueryFixedEnables();

    ButtonCaps = 0;

    if (EnBits & 0x100)
        ButtonCaps = 1;

    if (EnBits & 0x200)
        ButtonCaps |= 2;

    if (!ButtonCaps)
    {
        *OutDeviceExtension = NULL;
        return STATUS_SUCCESS;
    }

    Status = ACPIBuildDeviceExtension(NULL, RootDeviceExtension, OutDeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        *OutDeviceExtension = NULL;
        return Status;
    }

    DeviceExtension = *OutDeviceExtension;

    ACPIInternalUpdateFlags(*OutDeviceExtension, 0x0018000000360000, FALSE);

    KeInitializeSpinLock(&DeviceExtension->Button.SpinLock);

    DeviceExtension->Button.Capabilities = (ButtonCaps | 0x80000000);

    DeviceExtension->Address = ExAllocatePoolWithTag(NonPagedPool, (strlen(ACPIFixedButtonId) + 1), 'SpcA');
    if (!DeviceExtension->Address)
    {
        ACPIInternalUpdateFlags(DeviceExtension, 0x0002000000000000, FALSE);
        *OutDeviceExtension = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    strcpy(DeviceExtension->Address, ACPIFixedButtonId);

    ACPIInternalUpdateFlags(DeviceExtension, 0x0000A00000000000, FALSE);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIBuildRunMethodRequest(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PVOID CallBack,
    _In_ PVOID CallBackContext,
    _In_ PVOID Context,
    _In_ ULONG Flags,
    _In_ BOOLEAN IsInsertDpc)
{
    PACPI_BUILD_REQUEST RunMethodRequest;
    PACPI_BUILD_REQUEST SynchronizationRequest;

    DPRINT("ACPIBuildRunMethodRequest: %p, %X, %X\n", DeviceExtension, Flags, IsInsertDpc);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    RunMethodRequest = ExAllocateFromNPagedLookasideList(&BuildRequestLookAsideList);
    if (!RunMethodRequest)
    {
        if (!CallBack)
            return STATUS_INSUFFICIENT_RESOURCES;

        DPRINT1("ACPIBuildRunMethodRequest: FIXME\n");
        ASSERT(FALSE);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (CallBack)
    {
        SynchronizationRequest = ExAllocateFromNPagedLookasideList(&BuildRequestLookAsideList);
        if (!SynchronizationRequest)
        {
            ExFreeToNPagedLookasideList(&BuildRequestLookAsideList, RunMethodRequest);

            DPRINT1("ACPIBuildRunMethodRequest: FIXME\n");
            ASSERT(FALSE);

            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        DPRINT("ACPIBuildRunMethodRequest: No CallBack\n");
    }

    if (!DeviceExtension->ReferenceCount)
    {
        ExFreeToNPagedLookasideList(&BuildRequestLookAsideList, RunMethodRequest);

        if (CallBack)
        {
            DPRINT1("ACPIBuildRunMethodRequest: FIXME\n");
            ASSERT(FALSE);
            ExFreeToNPagedLookasideList(&BuildRequestLookAsideList, SynchronizationRequest);
        }

        DPRINT1("ACPIBuildRunMethodRequest: STATUS_DEVICE_REMOVED\n");
        return STATUS_DEVICE_REMOVED;
    }

    InterlockedIncrement(&DeviceExtension->ReferenceCount);

    if (CallBack)
        InterlockedIncrement(&DeviceExtension->ReferenceCount);

    RtlZeroMemory(RunMethodRequest, sizeof(ACPI_BUILD_REQUEST));

    RunMethodRequest->Signature = '_SGP';
    RunMethodRequest->Status = STATUS_SUCCESS;
    RunMethodRequest->Flags = 0x100C;
    RunMethodRequest->DeviceExtension = DeviceExtension;
    RunMethodRequest->ListHeadForInsert = &AcpiBuildRunMethodList;
    RunMethodRequest->WorkDone = 3;
    RunMethodRequest->RunMethod.Context = Context;
    RunMethodRequest->RunMethod.Flags = Flags;

    if (CallBack)
    {
        RtlZeroMemory(SynchronizationRequest, sizeof(ACPI_BUILD_REQUEST));

        SynchronizationRequest->Signature = '_SGP';
        SynchronizationRequest->Status = STATUS_SUCCESS;
        SynchronizationRequest->Flags = 0x100A;
        SynchronizationRequest->DeviceExtension = DeviceExtension;
        SynchronizationRequest->CallBack = CallBack;
        SynchronizationRequest->CallBackContext = CallBackContext;
        SynchronizationRequest->ListHeadForInsert = &AcpiBuildSynchronizationList;
        SynchronizationRequest->WorkDone = 3;
        SynchronizationRequest->BuildReserved1 = 0;

        SynchronizationRequest->Synchronize.ListHead = &AcpiBuildRunMethodList;
        SynchronizationRequest->Synchronize.Context = Context;
        SynchronizationRequest->Synchronize.Reserved1 = 1;
    }

    KeAcquireSpinLockAtDpcLevel(&AcpiBuildQueueLock);

    InsertTailList(&AcpiBuildQueueList, &RunMethodRequest->Link);

    if (CallBack)
        InsertTailList(&AcpiBuildQueueList, &SynchronizationRequest->Link);

    if (IsInsertDpc && !AcpiBuildDpcRunning)
        KeInsertQueueDpc(&AcpiBuildDpc, NULL, NULL);

    KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);

    return STATUS_PENDING;
}

VOID
NTAPI
ACPITableLoad(VOID)
{
    PAMLI_NAME_SPACE_OBJECT ChildObject;
    PDEVICE_EXTENSION DeviceExtension = NULL;
    PAMLI_NAME_SPACE_OBJECT NsObject;
    BOOLEAN IsGetChild = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("ACPITableLoad()\n");

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &OldIrql);

    /* System bus tree */
    Status = AMLIGetNameSpaceObject("\\_SB", NULL, &NsObject, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPITableLoad: No SB Object!\n");
        ASSERT(FALSE);
    }

    if (!RootDeviceExtension->AcpiObject)
    {
        IsGetChild = TRUE;

        InterlockedIncrement(&RootDeviceExtension->ReferenceCount);

        RootDeviceExtension->AcpiObject = NsObject;
        NsObject->Context = RootDeviceExtension;

        Status = ACPIBuildFixedButtonExtension(RootDeviceExtension, &DeviceExtension);

        if (NT_SUCCESS(Status) && DeviceExtension)
            InterlockedIncrement(&DeviceExtension->ReferenceCount);
    }

    Status = ACPIBuildRunMethodRequest(RootDeviceExtension, NULL, NULL, (PVOID)'INI_', 7, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPITableLoad: FIXME\n");
        ASSERT(FALSE);
    }

    KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);

    if (IsGetChild)
    {
        ChildObject = ACPIAmliGetNamedChild(NsObject->Parent, 'INI_');
        if (ChildObject)
            AMLIAsyncEvalObject(ChildObject, NULL, 0, NULL, NULL, NULL);
    }

    DPRINT("ACPITableLoad: ACPITableLoadCallBack %X\n", ACPITableLoadCallBack);

    Status = ACPIBuildSynchronizationRequest(RootDeviceExtension, ACPITableLoadCallBack, NULL, &AcpiBuildDeviceList, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPITableLoad: FIXME\n");
        ASSERT(FALSE);
    }
    DPRINT("ACPITableLoad: Status %X\n", Status);

    KeAcquireSpinLock(&AcpiBuildQueueLock, &OldIrql);
    if (!AcpiBuildDpcRunning)
        KeInsertQueueDpc(&AcpiBuildDpc, NULL, NULL);
    KeReleaseSpinLock(&AcpiBuildQueueLock, OldIrql);

    DPRINT("ACPITableLoad: exit\n");
}

NTSTATUS
__cdecl
ACPICallBackLoad(
    _In_ int Param1,
    _In_ int Param2)
{
    DPRINT("ACPICallBackLoad: Param1 %X, Param2 %X\n", Param1, Param2);

    if (Param2 == 1)
    {
        if (InterlockedIncrement(&AcpiTableDelta) == 1)
            ACPIGpeClearEventMasks();
    }
    else if (!InterlockedDecrement(&AcpiTableDelta))
    {
        ACPIGpeBuildEventMasks();
        ACPITableLoad();
    }

    DPRINT("ACPICallBackLoad: ret STATUS_SUCCESS\n");
    return STATUS_SUCCESS;
}

NTSTATUS
__cdecl
ACPICallBackUnload(
    _In_ int Param1,
    _In_ int Param2)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
ACPITableNotifyFreeObject(
    _In_ int Param1,
    _In_ int Param2,
    _In_ int Param3)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
NotifyHandler(
    _In_ int Param1,
    _In_ int Param2,
    _In_ int Param3)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
GlobalLockEventHandler(
    _In_ int Param1,
    _In_ int Param2,
    _In_ int Param3,
    _In_ int Param4,
    _In_ int Param5)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBuildDeviceRequest(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PVOID CallBack,
    _In_ PVOID CallBackContext,
    _In_ BOOLEAN IsInsertDpc)
{
    PACPI_BUILD_REQUEST BuildRequest;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    BuildRequest = ExAllocateFromNPagedLookasideList(&BuildRequestLookAsideList);
    if (!BuildRequest)
    {
        DPRINT1("ACPIBuildDeviceRequest: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!DeviceExtension->ReferenceCount)
    {
        DPRINT1("ACPIBuildDeviceRequest: STATUS_DEVICE_REMOVED\n");
        ExFreeToNPagedLookasideList(&BuildRequestLookAsideList, BuildRequest);
        return STATUS_DEVICE_REMOVED;
    }

    InterlockedIncrement(&DeviceExtension->ReferenceCount);

    RtlZeroMemory(BuildRequest, sizeof(*BuildRequest));

    BuildRequest->DeviceExtension = DeviceExtension;
    BuildRequest->CallBack = CallBack;
    BuildRequest->Signature = '_SGP';
    BuildRequest->ListHeadForInsert = &AcpiBuildDeviceList;
    BuildRequest->WorkDone = 3;
    BuildRequest->Status = STATUS_SUCCESS;
    BuildRequest->CallBackContext = CallBackContext;
    BuildRequest->Flags = 0x1001;

    KeAcquireSpinLockAtDpcLevel(&AcpiBuildQueueLock);

    InsertTailList(&AcpiBuildQueueList, &BuildRequest->Link);

    if (IsInsertDpc && !AcpiBuildDpcRunning)
        KeInsertQueueDpc(&AcpiBuildDpc, NULL, NULL);

    KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);

    return STATUS_PENDING;
}

NTSTATUS
NTAPI
OSNotifyCreateDevice(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ ULONGLONG FlagValue)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;
    PDEVICE_EXTENSION Destination = NULL;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(NsObject != NULL);
    ASSERT(NsObject->Parent != NULL);

    DeviceExtension = NsObject->Parent->Context;
    if (!DeviceExtension)
    {
        DeviceExtension = RootDeviceExtension;
        ASSERT(RootDeviceExtension != NULL);
    }

    Status = ACPIBuildDeviceExtension(NsObject, DeviceExtension, &Destination);
    if (!Destination)
    {
        DPRINT1("OSNotifyCreateDevice: STATUS_UNSUCCESSFUL\n");
        Status = STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSNotifyCreateDevice: NSObj %p, Status %X\n", NsObject, Status);
        return Status;
    }

    InterlockedIncrement(&Destination->ReferenceCount);

    ACPIInternalUpdateFlags(Destination, FlagValue, FALSE);

    Status = ACPIBuildDeviceRequest(Destination, NULL, NULL, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSNotifyCreateDevice: Destination %p, Status %X\n", Destination, Status);
    }

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessorRequest(
    _In_ PDEVICE_EXTENSION ProcessorExt,
    _In_ ULONG Param2,
    _In_ ULONG Param3,
    _In_ ULONG Param4)
{
    return STATUS_PENDING;
}

NTSTATUS
NTAPI
ACPIBuildProcessorExtension(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ PDEVICE_EXTENSION ParentDeviceExtension,
    _Out_ PDEVICE_EXTENSION* OutDeviceExtension,
    _In_ ULONG ProcessorIndex)
{
    PDEVICE_EXTENSION DeviceExtension;
    PCHAR CompatIdStr;
    CHAR Char;
    NTSTATUS Status;

    if (!AcpiProcessorString.Buffer)
    {
        DPRINT1("ACPIBuildProcessorExtension: STATUS_OBJECT_NAME_NOT_FOUND\n");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Status = ACPIBuildDeviceExtension(NsObject, ParentDeviceExtension, OutDeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIBuildProcessorExtension: Status %X\n", Status);
        return Status;
    }

    DeviceExtension = *OutDeviceExtension;
    if (DeviceExtension == NULL)
    {
        DPRINT1("ACPIBuildProcessorExtension: Status %X\n", Status);
        return Status;
    }

    ACPIInternalUpdateFlags(DeviceExtension, 0x0010001000300000, FALSE);

    DeviceExtension->Processor.ProcessorIndex = ProcessorIndex;

    DeviceExtension->DeviceID = ExAllocatePoolWithTag(NonPagedPool, AcpiProcessorString.Length, 'SpcA');
    if (!DeviceExtension->DeviceID)
    {
        DPRINT1("ACPIBuildProcessorExtension: failed to allocate %X bytes\n", AcpiProcessorString.Length);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ErrorExit;
    }

    RtlCopyMemory(DeviceExtension->DeviceID, AcpiProcessorString.Buffer, AcpiProcessorString.Length);

    CompatIdStr = AcpiProcessorCompatId;

    DeviceExtension->Processor.CompatibleID = ExAllocatePoolWithTag(NonPagedPool, (strlen(AcpiProcessorCompatId) + 1), 'SpcA');
    if (!DeviceExtension->Processor.CompatibleID)
    {
        do
            Char = *CompatIdStr++;
        while (Char);

        DPRINT1("ACPIBuildProcessorExtension: failed to allocate %X bytes\n", (CompatIdStr - (AcpiProcessorCompatId + 1) + 1));
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ErrorExit;
    }

    strcpy(DeviceExtension->Processor.CompatibleID, AcpiProcessorCompatId);

    DeviceExtension->InstanceID = ExAllocatePoolWithTag(NonPagedPool, 3, 'SpcA');
    if (DeviceExtension->InstanceID)
    {
        sprintf(DeviceExtension->InstanceID, "%2d", (int)ProcessorIndex);
        ACPIInternalUpdateFlags(DeviceExtension, 0x8001E00000000000, FALSE);
        DPRINT("ACPIBuildProcessorExtension: Status %X\n", Status);
        return Status;
    }

    DPRINT1("ACPIBuildProcessorExtension: failed to allocate %X bytes\n", 3);
    Status = STATUS_INSUFFICIENT_RESOURCES;

ErrorExit:

    if (DeviceExtension->InstanceID)
    {
        ACPIInternalUpdateFlags(DeviceExtension, 0x0001400000000000, TRUE);
        ExFreePoolWithTag(DeviceExtension->InstanceID, 'SpcA');
        DeviceExtension->InstanceID = NULL;
    }

    if (DeviceExtension->DeviceID)
    {
        ACPIInternalUpdateFlags(DeviceExtension, 0x0000A00000000000, TRUE);
        ExFreePoolWithTag(DeviceExtension->DeviceID, 'SpcA');
        DeviceExtension->DeviceID = NULL;
    }

    if (DeviceExtension->Processor.CompatibleID)
    {
        ACPIInternalUpdateFlags(DeviceExtension, 0x8000000000000000, TRUE);
        ExFreePoolWithTag(DeviceExtension->Processor.CompatibleID, 'SpcA');
        DeviceExtension->Processor.CompatibleID = NULL;
    }

    ACPIInternalUpdateFlags(DeviceExtension, 0x0002000000000000, TRUE);

    return Status;
}

NTSTATUS
NTAPI
OSNotifyCreateProcessor(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ ULONGLONG FlagValue)
{
    PDEVICE_EXTENSION ParentDeviceExt;
    PDEVICE_EXTENSION ProcessorExt = NULL;
    PAMLI_NAME_SPACE_OBJECT NsParentObject;
    ULONG ix;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(NsObject != NULL);

    for (ix = 0; ix < 0x20; ix++)
    {
        if (!ProcessorList[ix])
            break;
    }

    if (ix >= 0x20)
        return STATUS_UNSUCCESSFUL;

    if (ProcessorList[ix])
        return STATUS_UNSUCCESSFUL;

    DPRINT("OSNotifyCreateProcessor: Processor %X, NsObject %p\n", (ix + 1), NsObject);

    ProcessorList[ix] = NsObject;

    NsParentObject = NsObject->Parent;
    ASSERT(NsParentObject != NULL);

    ParentDeviceExt = NsParentObject->Context;
    if (!ParentDeviceExt)
    {
        ParentDeviceExt = RootDeviceExtension;
        ASSERT(ParentDeviceExt != NULL);
    }

    Status = ACPIBuildProcessorExtension(NsObject, ParentDeviceExt, &ProcessorExt, ix);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("OSNotifyCreateProcessor: NSObj %p Failed %08lx\n", NsObject, Status);
        return Status;
    }

    InterlockedIncrement(&ProcessorExt->ReferenceCount);

    ACPIInternalUpdateFlags(ProcessorExt, FlagValue, FALSE);

    Status = ACPIBuildProcessorRequest(ProcessorExt, 0, 0, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSNotifyCreateProcessor: Status %X\n", Status);
    }

    return Status;
}

static CHAR NameObject[8];

PCHAR
NTAPI
ACPIAmliNameObject(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject)
{
    RtlZeroMemory(NameObject, sizeof(NameObject));

    ASSERT(sizeof(NameObject) >= sizeof(NsObject->NameSeg));
    RtlCopyMemory(NameObject, &NsObject->NameSeg, sizeof(NsObject->NameSeg));

    NameObject[4] = 0;

    return NameObject;
}

NTSTATUS
__cdecl
OSNotifyCreate(
    _In_ ULONG Type,
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject)
{
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("OSNotifyCreate: Type %X, NsObject %p\n");

    ASSERT(NsObject != NULL);

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &OldIrql);

    switch (Type)
    {
        case 6:
            Status = OSNotifyCreateDevice(NsObject, 0);
            break;

        case 0xA:
            DPRINT("OSNotifyCreate: FIXME\n");
            ASSERT(FALSE);
            break;

        case 0xB:
            DPRINT("OSNotifyCreate: FIXME\n");
            ASSERT(FALSE);
            break;

        case 0xC:
            Status = OSNotifyCreateProcessor(NsObject, 0);
            break;

        case 0xD:
            DPRINT("OSNotifyCreate: FIXME\n");
            ASSERT(FALSE);
            break;

        default:
            DPRINT("OSNotifyCreate: received unhandled type %X\n", Type);
            break;
    }

    KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);

    DPRINT("OSNotifyCreate: (%p) '%s', Status %X\n", NsObject, ACPIAmliNameObject(NsObject), Status);

    return STATUS_SUCCESS;
}

NTSTATUS
__cdecl
OSNotifyFatalError(
    _In_ int Param1,
    _In_ int Param2,
    _In_ int Param3,
    _In_ int Param4)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* ACPI ARBITER ROUTINES *****************************************************/

PACPI_VECTOR_BLOCK
NTAPI
HashVector(
    _In_ ULONG Vector)
{
    PACPI_VECTOR_BLOCK VectorBlock;
    ULONG CurrentVector;
    ULONG ix;

    PAGED_CODE();
    DPRINT("HashVector: Vector %X\n", Vector);

    VectorBlock = &IrqHashTable[2 * (Vector % 0x1F)];

    while (TRUE)
    {
        for (ix = 0; ix < 2; ix++)
        {
            CurrentVector = VectorBlock->Entry.Vector;

            if (VectorBlock->Chain.Token == 'WWWW')
                break;

            if (CurrentVector == Vector)
                return VectorBlock;

            if (CurrentVector == 'XXXX')
                return NULL;

            if (ix == 1)
                return NULL;

            VectorBlock++;
        }

        ASSERT(VectorBlock->Chain.Token == 'WWWW');//TOKEN_VALUE

        VectorBlock = VectorBlock->Chain.Next;
    }

    DPRINT1("HashVector: FIXME\n");
    ASSERT(FALSE);

    return NULL;
}

NTSTATUS
NTAPI
LookupIsaVectorOverride(
    _In_ ULONG IntVector,
    _Out_ ULONG* OutGlobalVector,
    _Out_ UCHAR* OutFlags)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AddVectorToTable(
    _In_ ULONG Vector,
    _In_ UCHAR Count,
    _In_ UCHAR TempCount,
    _In_ UCHAR Flags)
{
    PACPI_VECTOR_BLOCK VectorBlock;
    PACPI_VECTOR_BLOCK NewEntries;
    ULONG ix;

    PAGED_CODE();
    DPRINT("AddVectorToTable: %X, %X, %X, %X\n", Vector, Count, TempCount, Flags);

    ASSERT((Flags & 0xF8) == 0);// ~(VECTOR_MODE | VECTOR_POLARITY | VECTOR_TYPE)

    for (VectorBlock = &IrqHashTable[2 * (Vector % 0x1F)];
         ;
         VectorBlock = VectorBlock->Chain.Next)
    {
        for (ix = 0; ix < 2; ix++, VectorBlock++)
        {
            if (VectorBlock->Entry.Vector == 'WWWW')
                break;

            if (VectorBlock->Entry.Vector == 'XXXX')
            {
                VectorBlock->Entry.Vector = Vector;
                VectorBlock->Entry.Count = Count;
                VectorBlock->Entry.TempCount = TempCount;
                VectorBlock->Entry.Flags = Flags;
                VectorBlock->Entry.TempFlags = Flags;

                return STATUS_SUCCESS;
            }

            if (ix == 1)
            {
                NewEntries = ExAllocatePoolWithTag(PagedPool, (2 * sizeof(ACPI_VECTOR_BLOCK)), 'ApcA');
                if (!NewEntries)
                {
                    DPRINT1("AddVectorToTable: STATUS_INSUFFICIENT_RESOURCES (%X)\n", (2 * sizeof(ACPI_VECTOR_BLOCK)));
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                RtlFillMemory(NewEntries, (2 * sizeof(ACPI_VECTOR_BLOCK)), 'XX');
                RtlMoveMemory(NewEntries, VectorBlock, sizeof(*VectorBlock));

                VectorBlock->Chain.Next = NewEntries;
                VectorBlock->Entry.Vector = 'WWWW';

                break;
            }
        }
    }

    DPRINT1("AddVectorToTable: FIXME\n");
    ASSERT(FALSE);

    return STATUS_UNSUCCESSFUL;
}

VOID
NTAPI
ReferenceVector(
    _In_ ULONG Vector,
    _In_ UCHAR Flags)
{
    PACPI_VECTOR_BLOCK VectorBlock;

    PAGED_CODE();

    ASSERT((Flags & 0xF8) == 0);// ~(VECTOR_MODE | VECTOR_POLARITY | VECTOR_TYPE)

    VectorBlock = HashVector(Vector);

    DPRINT("ReferenceVector: Flags %X, Vector %X (%X-%X)\n", Flags, Vector, 
           (VectorBlock ? VectorBlock->Entry.Count : 0), (VectorBlock ? VectorBlock->Entry.TempCount : 0));

    if (!VectorBlock)
    {
        AddVectorToTable(Vector, 0, 1, Flags);
        return;
    }

    if ((VectorBlock->Entry.TempCount + VectorBlock->Entry.Count) == 0)
        VectorBlock->Entry.TempFlags = Flags;

    VectorBlock->Entry.TempCount++;

    ASSERT(Flags == VectorBlock->Entry.TempFlags);
    ASSERT(VectorBlock->Entry.Count <= 0xFF);
}

VOID
NTAPI
MakeTempVectorCountsPermanent(
    VOID)
{
    PACPI_PM_DISPATCH_TABLE HalAcpiDispatchTable = (PVOID)PmHalDispatchTable;
    PACPI_VECTOR_BLOCK HashEntry;
    ULONG CurrentVector;
    ULONG jx;
    ULONG ix;

    PAGED_CODE();
    DPRINT("MakeTempVectorCountsPermanent()\n");

    for (ix = 0; ix < 0x3E; ix += 2)
    {
        HashEntry = &IrqHashTable[ix];

StartHash:

        for (jx = 0; jx < 2; jx++)
        {
            CurrentVector = HashEntry->Entry.Vector;

            if (HashEntry->Chain.Token == 'WWWW')
            {
                HashEntry = HashEntry->Chain.Next;
                goto StartHash;
            }

            if (CurrentVector == 'XXXX')
                break;

            if ((HashEntry->Entry.Count + HashEntry->Entry.TempCount) != 0)
            {
                if (!HashEntry->Entry.Count || HashEntry->Entry.TempFlags != HashEntry->Entry.Flags)
                {
                    HalAcpiDispatchTable->HalSetVectorState(CurrentVector, HashEntry->Entry.TempFlags);
                }
            }

            HashEntry->Entry.Flags = HashEntry->Entry.TempFlags;
            HashEntry->Entry.Count += HashEntry->Entry.TempCount;

            HashEntry++;
        }
    }
}

NTSTATUS
__cdecl
DisableLinkNodesAsyncWorker(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ NTSTATUS InStatus,
    _In_ ULONG Param3,
    _In_ PVOID InContext)
{
    PDISABLE_LINK_NODES_CONTEXT Context = InContext;
    PAMLI_NAME_SPACE_OBJECT Child;
    PAMLI_NAME_SPACE_OBJECT Current;
    PCHAR IdString;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(Context);

    InterlockedIncrement(&Context->RefCount);

    while (Context->Type == 0)
    {
        Context->Type = 1;

        Status = ACPIGet(Context->NsObject, 'DIH_', 0x58080206, NULL, 0, DisableLinkNodesAsyncWorker, Context, &Context->DataBuff, NULL);
        if (Status == STATUS_PENDING)
            return STATUS_PENDING;

        if (NT_SUCCESS(Status))
            break;

        Context->Type = 3;
    }

    if (Context->Type == 1)
    {
        Context->Type = 3;

        IdString = Context->DataBuff;
        if (!IdString || !strstr(IdString, "PNP0C0F"))
            goto Type3;

        Child = ACPIAmliGetNamedChild(Context->NsObject, 'SID_');
        if (!Child)
        {
            DPRINT1("DisableLinkNodesAsyncWorker: KeBugCheckEx(..)\n");
            KeBugCheckEx(0xA5, 0x10006, (ULONG_PTR)Context->NsObject, 0, 0);
        }

        Context->Type = 2;

        Status = AMLIAsyncEvalObject(Child, NULL, 0, NULL, (PVOID)DisableLinkNodesAsyncWorker, Context);
        if (Status == STATUS_PENDING)
            return STATUS_PENDING;

        if (NT_SUCCESS(Status))
            goto Finish;

        goto Type3;
    }
    else if (Context->Type == 3)
    {
Type3:
        Context->ChildNsObject = Context->NsObject->FirstChild;
        if (!Context->ChildNsObject)
        {
            Status = STATUS_SUCCESS;
            goto Finish;
        }

        Context->Type = 4;
        goto Type4;
    }
    else if (Context->Type == 4)
    {
Type4:
        while (Context->ChildNsObject)
        {
            Current = Context->ChildNsObject;

            if (Current->Parent && (ULONG_PTR)Current->Parent->FirstChild != (ULONG_PTR)Current->List.Next)
                Context->ChildNsObject = (PAMLI_NAME_SPACE_OBJECT)Current->List.Next;
            else
                Context->ChildNsObject = NULL;

            if (Current->ObjData.DataType == 6)
                Status = DisableLinkNodesAsync(Current, DisableLinkNodesAsyncWorker, (PVOID)Context);

            if (Status == STATUS_PENDING)
                return STATUS_PENDING;
        }
    }

Finish:

    if (Context->RefCount)
    {
        PAMLI_FN_ASYNC_CALLBACK CallBack = Context->Callback;
        CallBack(Context->NsObject, Status, 0, Context->Context);
    }

    if (Context->DataBuff)
        ExFreePool(Context->DataBuff);

    ExFreePool(Context);

    return Status;
}

NTSTATUS
NTAPI
DisableLinkNodesAsync(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ PVOID Callback,
    _In_ PVOID InContext)
{
    PDISABLE_LINK_NODES_CONTEXT Context;
    NTSTATUS Status;

    DPRINT("DisableLinkNodesAsync: NsObject %X\n", NsObject);

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), 'ApcA');
    if (!Context)
    {
        DPRINT1("DisableLinkNodesAsync: STATUS_INSUFFICIENT_RESOURCES (%X)\n", sizeof(*Context));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context, sizeof(*Context));

    Context->NsObject = NsObject;
    Context->Type = 0;
    Context->Callback = Callback;
    Context->Context = InContext;
    Context->RefCount = -1;

    Status = DisableLinkNodesAsyncWorker(NsObject, STATUS_SUCCESS, 0, Context);

    return Status;
}

VOID
__cdecl
AmlisuppCompletePassive(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ NTSTATUS InStatus,
    _In_ ULONG Param3,
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
AcpiArbUnpackRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PULONGLONG OutMinimumAddress,
    _Out_ PULONGLONG OutMaximumAddress,
    _Out_ PULONG OutLength,
    _Out_ PULONG OutAlignment)
{
    PAGED_CODE();
    DPRINT("AcpiArbUnpackRequirement: [%p] %I64X, %I64X, %X\n", IoDescriptor, IoDescriptor->u.Generic.MinimumAddress.QuadPart,
           IoDescriptor->u.Generic.MaximumAddress.QuadPart, IoDescriptor->u.Generic.Length);

    ASSERT(IoDescriptor);
    ASSERT(IoDescriptor->Type == CmResourceTypeInterrupt);

    *OutMinimumAddress = IoDescriptor->u.Port.Length;
    *OutMaximumAddress = IoDescriptor->u.Dma.MaximumChannel;
    *OutLength = 1;
    *OutAlignment = 1;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AcpiArbPackResource(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ ULONGLONG Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiArbUnpackResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PULONGLONG Start,
    _Out_ PULONG OutLength)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

LONG
NTAPI
AcpiArbScoreRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

BOOLEAN
NTAPI
AcpiArbFindSuitableRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
AcpiArbTestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiArbBootAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiArbRetestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiArbRollbackAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiArbCommitAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
AcpiArbAddAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
AcpiArbBacktrackAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
AcpiArbPreprocessEntry(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/*  Not correct yet, FIXME! */
NTSTATUS
NTAPI
AcpiArbOverrideConflict(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PVOID Param2)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiArbQueryConflict(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIO_RESOURCE_DESCRIPTOR ConflictingResource,
    _Out_ ULONG* OutConflictCount,
    _Out_ PARBITER_CONFLICT_INFO* OutConflicts)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
AcpiArbGetNextAllocationRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
AcpiInitIrqArbiter(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PACPI_PM_DISPATCH_TABLE HalAcpiDispatchTable = (PVOID)PmHalDispatchTable;
    PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64 RegistryValue = NULL;
    PARBITER_EXTENSION ArbiterExtension;
    PDEVICE_EXTENSION DeviceExtension;
    ACPI_WAIT_CONTEXT WaitContext;
    PCI_COMMON_CONFIG PciConfig;
    PCI_SLOT_NUMBER SlotNumber;
    UNICODE_STRING NameString;
    HANDLE Handle = NULL;
    ULONG Device;
    ULONG Function;
    ULONG Vector;
    UCHAR Flags;
    UCHAR SubordinateBus;
    UCHAR BusNumber;
    BOOLEAN IsNotFound;
    BOOLEAN IsBootConfig;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("AcpiInitIrqArbiter: DeviceObject %p\n", DeviceObject);

    ArbiterExtension = ExAllocatePoolWithTag(NonPagedPool, sizeof(*ArbiterExtension), 'ApcA');
    if (!ArbiterExtension)
    {
        DPRINT1("AcpiInitIrqArbiter: STATUS_INSUFFICIENT_RESOURCES (%X)\n", sizeof(*ArbiterExtension));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(ArbiterExtension, sizeof(*ArbiterExtension));

    InitializeListHead(&ArbiterExtension->LinkNodeHead);
    AcpiArbiter.Extension = ArbiterExtension;

    AcpiArbiterDeviceObject = DeviceObject;

    AcpiArbiter.UnpackRequirement = AcpiArbUnpackRequirement;
    AcpiArbiter.PackResource = AcpiArbPackResource;
    AcpiArbiter.UnpackResource = AcpiArbUnpackResource;
    AcpiArbiter.ScoreRequirement = AcpiArbScoreRequirement;
    AcpiArbiter.FindSuitableRange = AcpiArbFindSuitableRange;
    AcpiArbiter.TestAllocation = AcpiArbTestAllocation;
    AcpiArbiter.BootAllocation = AcpiArbBootAllocation;
    AcpiArbiter.RetestAllocation = AcpiArbRetestAllocation;
    AcpiArbiter.RollbackAllocation = AcpiArbRollbackAllocation;
    AcpiArbiter.CommitAllocation = AcpiArbCommitAllocation;
    AcpiArbiter.AddAllocation = AcpiArbAddAllocation;
    AcpiArbiter.BacktrackAllocation = AcpiArbBacktrackAllocation;
    AcpiArbiter.PreprocessEntry = AcpiArbPreprocessEntry;
    AcpiArbiter.OverrideConflict = AcpiArbOverrideConflict;
    AcpiArbiter.QueryConflict = AcpiArbQueryConflict;
    AcpiArbiter.GetNextAllocationRange = AcpiArbGetNextAllocationRange;

    IrqHashTable = ExAllocatePoolWithTag(PagedPool, (0x3E * sizeof(ACPI_VECTOR_BLOCK)), 'ApcA');
    if (!IrqHashTable)
    {
        DPRINT1("AcpiInitIrqArbiter: STATUS_INSUFFICIENT_RESOURCES (%X)\n", (0x3E * sizeof(ACPI_VECTOR_BLOCK)));
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ErrorExit;
    }
    RtlFillMemory(IrqHashTable, (0x3E * sizeof(ACPI_VECTOR_BLOCK)), 'X'); // FIXME

    Status = ArbInitializeArbiterInstance(&AcpiArbiter, DeviceObject, CmResourceTypeInterrupt, L"ACPI_IRQ", L"Root", NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("AcpiInitIrqArbiter: Status %X\n", Status);
        Status = STATUS_UNSUCCESSFUL;
        goto ErrorExit;
    }

    Vector = AcpiInformation->FixedACPIDescTable->sci_int_vector;
    Flags = 3;

    LookupIsaVectorOverride(Vector, &Vector, &Flags);

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlAddRange(AcpiArbiter.Allocation, Vector, Vector, 0, 2, NULL, DeviceExtension->PhysicalDeviceObject);

    ReferenceVector(Vector, Flags);
    AcpiSciVector = Vector;

    MakeTempVectorCountsPermanent();

    KeInitializeEvent(&WaitContext.Event, SynchronizationEvent, 0);
    WaitContext.Status = STATUS_UNSUCCESSFUL;

    Status = DisableLinkNodesAsync(DeviceExtension->AcpiObject, AmlisuppCompletePassive, &WaitContext);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&WaitContext, Executive, KernelMode, FALSE, NULL);

    IsBootConfig = FALSE;
    IsNotFound = FALSE;

    BusNumber = 0;
    SubordinateBus = 0;

    do
    {
        SlotNumber.u.AsULONG = 0;
        Device = 0;

        do
        {
            Function = 0;

            while (TRUE)
            {
                SlotNumber.u.bits.DeviceNumber = Device;
                SlotNumber.u.bits.FunctionNumber = Function;

                HalAcpiDispatchTable->HalPciInterfaceReadConfig(NULL, BusNumber, SlotNumber, &PciConfig, 0, 0x40);

                if (PciConfig.VendorID == -1)
                    break;

                if (PciConfig.HeaderType & 0x7F)
                {
                    if (SubordinateBus <= PciConfig.u.type1.SubordinateBus)
                        SubordinateBus = PciConfig.u.type1.SubordinateBus;

                    if ((PciConfig.HeaderType & 0x7F) == 2)
                        AcpiArbCardbusPresent = TRUE;
                }
                else if (PciConfig.u.type0.InterruptPin &&
                         PciConfig.u.type0.InterruptLine &&
                         PciConfig.u.type0.InterruptLine < 0xFF)
                {
                    if (!IsBootConfig)
                    {
                        AcpiIrqDefaultBootConfig = PciConfig.u.type0.InterruptLine;
                        IsBootConfig = TRUE;
                    }
                    else if (PciConfig.u.type0.InterruptLine != AcpiIrqDefaultBootConfig)
                    {
                        IsNotFound = TRUE;
                        break;
                    }
                }

                if (PciConfig.HeaderType & 0x80 || Function)
                {
                    Function++;
                    if (Function < 8)
                        continue;
                }

                break;
            }

            Device++;
        }
        while (Device < 0x20);

        BusNumber++;
    }
    while (SubordinateBus != BusNumber);

    if (!IsBootConfig || IsNotFound || !AcpiArbCardbusPresent)
        AcpiIrqDefaultBootConfig = 0;

    RtlInitUnicodeString(&NameString, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\ACPI\\Parameters");

    Status = OSOpenUnicodeHandle(&NameString, NULL, &Handle);
    if (!NT_SUCCESS(Status))
        return STATUS_SUCCESS;

    Status = OSGetRegistryValue(Handle, L"IRQDistribution", (PVOID *)&RegistryValue);
    if (NT_SUCCESS(Status))
    {
        if (RegistryValue->DataLength && RegistryValue->Type == 4)
            AcpiIrqDistributionDisposition = *(PULONG)&RegistryValue->Data[0];

        ExFreePoolWithTag(RegistryValue, 'SpcA');
    }

    Status = OSGetRegistryValue(Handle, L"ForcePCIBootConfig", (PVOID *)&RegistryValue);
    if (NT_SUCCESS(Status))
    {
        if (RegistryValue->DataLength && RegistryValue->Type == 4)
            AcpiIrqDefaultBootConfig = RegistryValue->Data[0];

        ExFreePoolWithTag(RegistryValue, 'SpcA');
    }

    OSCloseHandle(Handle);
    return STATUS_SUCCESS;

ErrorExit:

    ExFreePoolWithTag(ArbiterExtension, 'ApcA');

    if (IrqHashTable)
        ExFreePoolWithTag(IrqHashTable, 'ApcA');

    if (Handle)
        OSCloseHandle(Handle);

    if (RegistryValue)
        ExFreePoolWithTag(RegistryValue, 'SpcA');

    return Status;
}

/* INIT DRIVER ROUTINES *****************************************************/

NTSTATUS
NTAPI
ACPIDispatchAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    PDEVICE_EXTENSION DeviceExtension = NULL;
    PDEVICE_OBJECT AttachedToDevice = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    PACPI_POWER_INFO PowerInfo;
    PCHAR InstanceID;
    PCHAR Address;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPIDispatchAddDevice: %p, %p\n", DriverObject, TargetDevice);

    Address = ExAllocatePoolWithTag(NonPagedPool, 0xE, 'SpcA');
    if (!Address)
    {
        DPRINT1("ACPIDispatchAddDevice: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Address, 0xE);
    RtlCopyMemory(Address, "ACPI\\PNP0C08", sizeof("ACPI\\PNP0C08"));

    InstanceID = ExAllocatePoolWithTag(NonPagedPool, 0xB, 'SpcA');
    if (!InstanceID)
    {
        DPRINT1("ACPIDispatchAddDevice: STATUS_INSUFFICIENT_RESOURCES\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    RtlZeroMemory(InstanceID, 0xB);
    RtlCopyMemory(InstanceID, "0x5F534750", sizeof("0x5F534750"));

    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_ACPI, 0, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIDispatchAddDevice: Status %X\n", Status);
        goto Exit;
    }

    AttachedToDevice = IoAttachDeviceToDeviceStack(DeviceObject, TargetDevice);
    if (!AttachedToDevice)
    {
        DPRINT1("ACPIDispatchAddDevice: STATUS_NO_SUCH_DEVICE\n");
        Status = STATUS_NO_SUCH_DEVICE;
        goto Exit;
    }

    DeviceExtension = ExAllocateFromNPagedLookasideList(&DeviceExtensionLookAsideList);
    if (!DeviceExtension)
    {
        DPRINT1("ACPIDispatchAddDevice: STATUS_INSUFFICIENT_RESOURCES\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    InterlockedIncrement(&DeviceExtension->ReferenceCount);
    InterlockedIncrement(&DeviceExtension->OutstandingIrpCount);

    DeviceObject->DeviceExtension = DeviceExtension;
    DeviceExtension->DeviceObject = DeviceObject;
    DeviceExtension->PhysicalDeviceObject = TargetDevice;
    DeviceExtension->TargetDeviceObject = AttachedToDevice;
    DeviceExtension->Address = Address;
    DeviceExtension->InstanceID = InstanceID;

    PowerInfo = &DeviceExtension->PowerInfo;

    DeviceExtension->Signature = '_SGP';
    DeviceExtension->DispatchTable = &AcpiFdoIrpDispatch;

    PowerInfo->DevicePowerMatrix[1] = 1;
    PowerInfo->DevicePowerMatrix[2] = 1;
    PowerInfo->DevicePowerMatrix[3] = 1;
    PowerInfo->DevicePowerMatrix[4] = 1;
    PowerInfo->DevicePowerMatrix[0] = 0;
    PowerInfo->DevicePowerMatrix[5] = 4;
    PowerInfo->DevicePowerMatrix[6] = 4;

    PowerInfo->SystemWakeLevel = 0;
    PowerInfo->DeviceWakeLevel = 0;

    ACPIInternalUpdateFlags(DeviceExtension, 0x0001E00000200010, FALSE);

    InitializeListHead(&DeviceExtension->ChildDeviceList);
    InitializeListHead(&DeviceExtension->SiblingDeviceList);
    InitializeListHead(&DeviceExtension->EjectDeviceHead);
    InitializeListHead(&DeviceExtension->EjectDeviceList);
    InitializeListHead(&DeviceExtension->PowerInfo.PowerRequestListEntry);

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &OldIrql);
    RootDeviceExtension = DeviceExtension;
    KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);

    // FIXME Interfaces and WMI ...

    DeviceObject->Flags &= ~0x80;

    if (NT_SUCCESS(Status))
    {
        DPRINT("ACPIDispatchAddDevice: Status %X\n", Status);
        return Status;
    }

    DPRINT1("ACPIDispatchAddDevice: Status %X\n", Status);

Exit:

    ExFreePoolWithTag(Address, 'SpcA');

    if (InstanceID)
        ExFreePoolWithTag(InstanceID, 'SpcA');

    if (AttachedToDevice)
        IoDetachDevice(AttachedToDevice);

    if (DeviceObject)
        IoDeleteDevice(DeviceObject);

    if (DeviceExtension)
        ExFreeToNPagedLookasideList(&DeviceExtensionLookAsideList, DeviceExtension);

    DPRINT("ACPIDispatchAddDevice: Status %X\n", Status);
    return Status;
}

VOID
NTAPI
ACPIUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
OSQueueWorkItem(
    _In_ PWORK_QUEUE_ITEM WorkQueueItem)
{
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeAcquireSpinLock(&ACPIWorkerSpinLock, &OldIrql);

    if (IsListEmpty(&ACPIWorkQueue))
        KeSetEvent(&ACPIWorkToDoEvent, 0, FALSE);

    InsertTailList(&ACPIWorkQueue, &WorkQueueItem->List);
 
    KeReleaseSpinLock(&ACPIWorkerSpinLock, OldIrql);
}

VOID
NTAPI
ACPIWorkerThread(PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ACPIWorker(PVOID StartContext)
{
    PWORK_QUEUE_ITEM WorkQueueItem;
    KWAIT_BLOCK WaitBlockArray;
    PLIST_ENTRY Entry;
    PVOID Object[2];
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("ACPIWorker()\n");

    ACPIThread = KeGetCurrentThread();

    Object[0] = &ACPIWorkToDoEvent;
    Object[1] = &ACPITerminateEvent;

    while (TRUE)
    {
        Status = KeWaitForMultipleObjects(2, Object, WaitAny, Executive, KernelMode, FALSE, NULL, &WaitBlockArray);
        if (Status == STATUS_WAIT_1)
            PsTerminateSystemThread(STATUS_SUCCESS);

        KeAcquireSpinLock(&ACPIWorkerSpinLock, &OldIrql);

        ASSERT(!IsListEmpty(&ACPIWorkQueue));

        Entry = RemoveHeadList(&ACPIWorkQueue);
        WorkQueueItem = CONTAINING_RECORD(Entry, WORK_QUEUE_ITEM, List);

        if (IsListEmpty(&ACPIWorkQueue))
            KeClearEvent(&ACPIWorkToDoEvent);

        KeReleaseSpinLock(&ACPIWorkerSpinLock, OldIrql);

        //_SEH2_TRY

        WorkQueueItem->WorkerRoutine(WorkQueueItem->Parameter);

        if (KeGetCurrentIrql())
        {
            OldIrql = KeGetCurrentIrql();

            DPRINT1("ACPIWorker: worker exit at IRQL %X, worker routine %X, parameter %X, item %X\n",
                    OldIrql, WorkQueueItem->WorkerRoutine, WorkQueueItem->Parameter, WorkQueueItem);

            DbgBreakPoint();
        }

        //_SEH2_END
    }
}

VOID
NTAPI
ACPIInitializeWorker()
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PVOID Object;
    HANDLE ThreadHandle;

    DPRINT("ACPIInitializeWorker()\n");

    KeInitializeSpinLock(&ACPIWorkerSpinLock);

    ExInitializeWorkItem(&ACPIWorkItem, ACPIWorkerThread, NULL);

    KeInitializeEvent(&ACPIWorkToDoEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&ACPITerminateEvent, NotificationEvent, FALSE);

    InitializeListHead(&ACPIDeviceWorkQueue);
    InitializeListHead(&ACPIWorkQueue);
    InitializeListHead(&AcpiBuildQueueList);

    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);

    if (PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, &ObjectAttributes, 0, NULL, ACPIWorker, NULL))
    {
        DPRINT1("DriverEntry: PsCreateSystemThread() failed\n");
        ASSERT(FALSE);
    }

    if (ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, &Object, NULL))
    {
        DPRINT1("DriverEntry: ObReferenceObjectByHandle() failed\n");
        ASSERT(FALSE);
    }
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Size;
    ULONG ix;

    DPRINT("DriverEntry: %X, '%wZ'\n", DriverObject, RegistryPath);

    AcpiDriverObject = DriverObject;

    Size = (RegistryPath->Length + sizeof(WCHAR));

    AcpiRegistryPath.Length = 0;
    AcpiRegistryPath.MaximumLength = Size;
    AcpiRegistryPath.Buffer = ExAllocatePoolWithTag(PagedPool, Size, 'MpcA');

    if (AcpiRegistryPath.Buffer)
        RtlCopyUnicodeString(&AcpiRegistryPath, RegistryPath);
    else
        AcpiRegistryPath.MaximumLength = 0;

    if ((AcpiOverrideAttributes & 4) && KeQueryActiveProcessors() == 1)
        AcpiOverrideAttributes &= ~4;

    ACPIInitReadRegistryKeys();

    KeInitializeDpc(&AcpiBuildDpc, ACPIBuildDeviceDpc, NULL);
    KeInitializeDpc(&AcpiPowerDpc, ACPIDevicePowerDpc, NULL);

    KeInitializeSpinLock(&AcpiDeviceTreeLock);
    KeInitializeSpinLock(&AcpiBuildQueueLock);
    KeInitializeSpinLock(&AcpiPowerQueueLock);
    KeInitializeSpinLock(&AcpiGetLock);
    KeInitializeSpinLock(&AcpiPowerLock);

    InitializeListHead(&AcpiBuildDeviceList);
    InitializeListHead(&AcpiBuildSynchronizationList);
    InitializeListHead(&AcpiBuildRunMethodList);
    InitializeListHead(&AcpiBuildOperationRegionList);
    InitializeListHead(&AcpiBuildPowerResourceList);
    InitializeListHead(&AcpiBuildThermalZoneList);
    InitializeListHead(&AcpiPowerDelayedQueueList);
    InitializeListHead(&AcpiGetListEntry);
    InitializeListHead(&AcpiUnresolvedEjectList);
    InitializeListHead(&AcpiPowerSynchronizeList);
    InitializeListHead(&AcpiPowerQueueList);

    AcpiBuildFixedButtonEnumerated = FALSE;
    AcpiBuildWorkDone = FALSE;
    AcpiPowerWorkDone = FALSE;
    AcpiPowerDpcRunning = FALSE;

    ExInitializeNPagedLookasideList(&DeviceExtensionLookAsideList, NULL, NULL, 0, sizeof(DEVICE_EXTENSION), 'DpcA', 0x40);
    ExInitializeNPagedLookasideList(&BuildRequestLookAsideList, NULL, NULL, 0, sizeof(ACPI_BUILD_REQUEST), 'DpcA', 0x38);
    ExInitializeNPagedLookasideList(&RequestLookAsideList, NULL, NULL, 0, sizeof(ACPI_POWER_REQUEST), 'PpcA', 0xCC);

    ACPIInitializeWorker();

    DriverObject->DriverUnload = ACPIUnload;
    DriverObject->DriverExtension->AddDevice = ACPIDispatchAddDevice;

    for (ix = 0; ix <= IRP_MJ_MAXIMUM_FUNCTION; ix++)
        DriverObject->MajorFunction[ix] = ACPIDispatchIrp;

    RtlZeroMemory(&ACPIFastIoDispatch, sizeof(ACPIFastIoDispatch));

    ACPIFastIoDispatch.SizeOfFastIoDispatch = sizeof(ACPIFastIoDispatch);
    ACPIFastIoDispatch.FastIoDetachDevice = ACPIFilterFastIoDetachCallback;

    DriverObject->FastIoDispatch = &ACPIFastIoDispatch;

    ACPIInitHalDispatchTable();

    DPRINT("DriverEntry: return STATUS_SUCCESS\n");
    return STATUS_SUCCESS;
}

/* EOF */
