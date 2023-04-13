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

/* GLOBALS *******************************************************************/

PDRIVER_OBJECT AcpiDriverObject;
UNICODE_STRING AcpiRegistryPath;
FAST_IO_DISPATCH ACPIFastIoDispatch;
PDEVICE_EXTENSION RootDeviceExtension;
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
BOOLEAN AcpiBuildDpcRunning;

extern IRP_DISPATCH_TABLE AcpiFdoIrpDispatch;

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
