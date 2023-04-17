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

PACPI_READ_REGISTER AcpiReadRegisterRoutine = DefPortReadAcpiRegister;

PDRIVER_OBJECT AcpiDriverObject;
UNICODE_STRING AcpiRegistryPath;
FAST_IO_DISPATCH ACPIFastIoDispatch;
PDEVICE_EXTENSION RootDeviceExtension;
PRSDTINFORMATION RsdtInformation;
WORK_QUEUE_ITEM ACPIWorkItem;
KDPC AcpiBuildDpc;

NPAGED_LOOKASIDE_LIST DeviceExtensionLookAsideList;
NPAGED_LOOKASIDE_LIST BuildRequestLookAsideList;
KSPIN_LOCK AcpiDeviceTreeLock;
KSPIN_LOCK AcpiBuildQueueLock;
KSPIN_LOCK ACPIWorkerSpinLock;
KEVENT ACPIWorkToDoEvent;
KEVENT ACPITerminateEvent;
LIST_ENTRY ACPIDeviceWorkQueue;
LIST_ENTRY ACPIWorkQueue;
LIST_ENTRY AcpiBuildDeviceList;
LIST_ENTRY AcpiBuildSynchronizationList;
LIST_ENTRY AcpiBuildQueueList;
BOOLEAN AcpiLoadSimulatorTable = TRUE;
BOOLEAN AcpiBuildDpcRunning;

extern IRP_DISPATCH_TABLE AcpiFdoIrpDispatch;
extern PACPI_INFORMATION AcpiInformation;

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

NTSTATUS
NTAPI
ACPILoadProcessFADT(
    _In_ PFADT Fadt)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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

/* ACPI CALLBACKS ***********************************************************/

NTSTATUS
__cdecl
ACPICallBackLoad(
    _In_ int Param1,
    _In_ int Param2)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
__cdecl
OSNotifyCreate(
    _In_ int Param1,
    _In_ int Param2)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
ACPIWorkerThread(PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ACPIWorker(PVOID StartContext)
{
    UNIMPLEMENTED_DBGBREAK();
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

    ACPIInitReadRegistryKeys();

    KeInitializeDpc(&AcpiBuildDpc, ACPIBuildDeviceDpc, NULL);

    KeInitializeSpinLock(&AcpiDeviceTreeLock);
    KeInitializeSpinLock(&AcpiBuildQueueLock);

    InitializeListHead(&AcpiBuildDeviceList);
    InitializeListHead(&AcpiBuildSynchronizationList);

    ExInitializeNPagedLookasideList(&DeviceExtensionLookAsideList, NULL, NULL, 0, sizeof(DEVICE_EXTENSION), 'DpcA', 0x40);
    ExInitializeNPagedLookasideList(&BuildRequestLookAsideList, NULL, NULL, 0, sizeof(ACPI_BUILD_REQUEST), 'DpcA', 0x38);

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
