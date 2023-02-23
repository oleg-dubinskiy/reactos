/*
 * PROJECT:     Volume manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main file
 * COPYRIGHT:   Copyright 2018, 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "ftdisk.h"

//#define NDEBUG
#include <debug.h>

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DriverEntry)
  #pragma alloc_text(INIT, FtDiskAddDevice)
#endif

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, FtDiskUnload)
  #pragma alloc_text(PAGE, FtDiskDeviceControl)
  #pragma alloc_text(PAGE, FtWmi)
  #pragma alloc_text(PAGE, FtDiskPnp)
  #pragma alloc_text(PAGE, IoRegisterBootDriverReinitialization)
  #pragma alloc_text(PAGE, IoRegisterDriverReinitialization)
#endif

/* GLOBALS *******************************************************************/

GUID VOLMGR_VOLUME_MANAGER_GUID = {0x53F5630E, 0xB6BF, 0x11D0, {0X94, 0XF2, 0X00, 0XA0, 0XC9, 0X1E, 0XFB, 0X8B}};

/* FUNCTIONS ****************************************************************/

NTSTATUS
NTAPI
FtpSignalCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = Context;

    KeSetEvent(Event, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
NTAPI
FtpAcquire(
    _In_ PROOT_EXTENSION RootExtension)
{
    KeWaitForSingleObject(&RootExtension->RootSemaphore, Executive, KernelMode, FALSE, NULL);
}

VOID
NTAPI
FtpRelease(
    _In_ PROOT_EXTENSION RootExtension)
{
    KeReleaseSemaphore(&RootExtension->RootSemaphore, 0, 1, FALSE);
}

VOID
NTAPI
FtDiskUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* DRIVER DISPATCH ROUTINES *************************************************/

NTSTATUS
NTAPI
FtDiskCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskShutdownFlush(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtWmi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* PNP */

static
NTSTATUS
FASTCALL
FtpPnpPdo(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

static
NTSTATUS
FASTCALL
FtpPnpFdo(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PROOT_EXTENSION Extension;
    NTSTATUS Status;

    DPRINT("FtDiskPnp: %p, %p\n", DeviceObject, Irp);

    Extension = DeviceObject->DeviceExtension;

    if (Extension->DeviceExtensionType == 0) // ROOT
    {
        Status = FtpPnpFdo(DeviceObject, Irp);
        DPRINT("FtDiskPnp: %p, %p, %X\n", DeviceObject, Irp, Status);
        return Status;
    }

    if (Extension->DeviceExtensionType == 1) // VOLUME
    {
        Status = FtpPnpPdo(DeviceObject, Irp);
        DPRINT("FtDiskPnp: %p, %p, %X\n", DeviceObject, Irp, Status);
        return Status;
    }

    DPRINT("FtDiskPnp: %p, %p, DeviceExtensionType %X\n", DeviceObject, Irp, Extension->DeviceExtensionType);

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;

    IoCompleteRequest(Irp, 0);

    return STATUS_INVALID_DEVICE_REQUEST;
}

/* REINITIALIZE DRIVER ROUTINES *********************************************/

VOID
NTAPI
FtpDriverReinitialization(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
FtpBootDriverReinitialization(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* INIT DRIVER ROUTINES *****************************************************/

NTSTATUS
NTAPI
FtDiskAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT VolControlRootPdo,
    _In_ PUNICODE_STRING RegistryPath)
{
    PROOT_EXTENSION RootExtension;
    PDEVICE_OBJECT RootFdo;
    UNICODE_STRING NameString;
    UNICODE_STRING SymbolicLinkName;
    USHORT Size;
    NTSTATUS Status;

    DPRINT("FtDiskAddDevice: %p, %p, '%wZ'\n", DriverObject, VolControlRootPdo, RegistryPath);

    RtlInitUnicodeString(&NameString, L"\\Device\\FtControl");

    Status = IoCreateDevice(DriverObject,
                            sizeof(*RootExtension),
                            &NameString,
                            FILE_DEVICE_NETWORK,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &RootFdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FtDiskAddDevice: Status %X\n", Status);
        return Status;
    }

    RtlInitUnicodeString(&SymbolicLinkName, L"\\DosDevices\\FtControl");
    IoCreateSymbolicLink(&SymbolicLinkName, &NameString);

    RootExtension = RootFdo->DeviceExtension;
    RtlZeroMemory(RootExtension, sizeof(*RootExtension));

    DPRINT("FtDiskAddDevice: RootFdo %p, %p\n", RootFdo, RootExtension);

    RootExtension->DriverObject = DriverObject;
    RootExtension->SelfDeviceObject = RootFdo;
    RootExtension->RootExtension = RootExtension;

    RootExtension->DeviceExtensionType = 0; // Root ext.

    KeInitializeSpinLock(&RootExtension->SpinLock);

    RootExtension->AttachedToDevice = IoAttachDeviceToDeviceStack(RootFdo, VolControlRootPdo);
    if (!RootExtension->AttachedToDevice)
    {
        DPRINT1("FtDiskAddDevice: AttachedToDevice is NULL!\n");
        IoDeleteSymbolicLink(&SymbolicLinkName);
        IoDeleteDevice(RootFdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    DPRINT("FtDiskAddDevice: RootPdo %p, RootFdo attached to %p\n", VolControlRootPdo, RootExtension->AttachedToDevice);

    RootExtension->VolControlRootPdo = VolControlRootPdo;

    InitializeListHead(&RootExtension->VolumeList);
    InitializeListHead(&RootExtension->EmptyVolumesList);

    RootExtension->VolumeCounter = 1;

    RootExtension->LogicalDiskInfoSet = ExAllocatePoolWithTag(NonPagedPool, sizeof(*RootExtension->LogicalDiskInfoSet), 'tFcS');
    if (!RootExtension->LogicalDiskInfoSet)
    {
        DPRINT1("FtDiskAddDevice: STATUS_INSUFFICIENT_RESOURCES\n");
        IoDeleteSymbolicLink(&SymbolicLinkName);
        IoDetachDevice(RootExtension->AttachedToDevice);
        IoDeleteDevice(RootFdo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(RootExtension->LogicalDiskInfoSet, sizeof(*RootExtension->LogicalDiskInfoSet));

    InitializeListHead(&RootExtension->WorkerQueue);
    InitializeListHead(&RootExtension->ChangeNotifyIrpList);
    KeInitializeSemaphore(&RootExtension->RootSemaphore, 1, 1);

    Size = (RegistryPath->Length + sizeof(WCHAR));
    RootExtension->RegistryPath.MaximumLength = Size;

    RootExtension->RegistryPath.Buffer = ExAllocatePoolWithTag(PagedPool, Size, 'tFcS');
    if (!RootExtension->RegistryPath.Buffer)
    {
        DPRINT1("FtDiskAddDevice: STATUS_INSUFFICIENT_RESOURCES\n");

        if (RootExtension->LogicalDiskInfoSet)
            ExFreePoolWithTag(RootExtension->LogicalDiskInfoSet, 'tFcS');

        IoDeleteSymbolicLink(&SymbolicLinkName);
        IoDetachDevice(RootExtension->AttachedToDevice);
        IoDeleteDevice(RootFdo);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyUnicodeString(&RootExtension->RegistryPath, RegistryPath);

    Status = IoRegisterShutdownNotification(RootFdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FtDiskAddDevice: Status %X\n", Status);

        ExFreePoolWithTag(RootExtension->RegistryPath.Buffer, 'tFcS');

        if (RootExtension->LogicalDiskInfoSet)
            ExFreePoolWithTag(RootExtension->LogicalDiskInfoSet, 'tFcS');

        IoDeleteSymbolicLink(&SymbolicLinkName);
        IoDetachDevice(RootExtension->AttachedToDevice);
        IoDeleteDevice(RootFdo);

        return Status;
    }

    RootFdo->Flags &= ~DO_DEVICE_INITIALIZING;

    DPRINT1("FtDiskAddDevice: return STATUS_SUCCESS\n");
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    PDEVICE_OBJECT VolControlRootPdo = NULL;
    PDEVICE_OBJECT AttachedToDevice;
    PROOT_EXTENSION RootExtension;
    NTSTATUS Status;

    DPRINT("DriverEntry: %p, '%wZ'\n", DriverObject, RegistryPath);

    DriverObject->DriverUnload = FtDiskUnload;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = FtDiskCreate;
    DriverObject->MajorFunction[IRP_MJ_READ] = FtDiskReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = FtDiskReadWrite;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = FtDiskShutdownFlush;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = FtDiskDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = FtDiskInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = FtDiskShutdownFlush;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = FtDiskCleanup;
    DriverObject->MajorFunction[IRP_MJ_POWER] = FtDiskPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = FtWmi;
    DriverObject->MajorFunction[IRP_MJ_PNP] = FtDiskPnp;

    ObReferenceObject(DriverObject);

    Status = IoReportDetectedDevice(DriverObject, InterfaceTypeUndefined, -1, -1, NULL, NULL, TRUE, &VolControlRootPdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        return Status;
    }

    Status = FtDiskAddDevice(DriverObject, VolControlRootPdo, RegistryPath);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        return Status;
    }

    AttachedToDevice = IoGetAttachedDeviceReference(VolControlRootPdo);
    ObDereferenceObject(AttachedToDevice);

    RootExtension = AttachedToDevice->DeviceExtension;

    Status = IoRegisterDeviceInterface(RootExtension->VolControlRootPdo,
                                       &VOLMGR_VOLUME_MANAGER_GUID,
                                       NULL,
                                       &RootExtension->SymbolicLinkName);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        RootExtension->SymbolicLinkName.Buffer = NULL;
    }
    else
    {
        IoSetDeviceInterfaceState(&RootExtension->SymbolicLinkName, TRUE);
    }

    IoRegisterBootDriverReinitialization(DriverObject, FtpBootDriverReinitialization, RootExtension);
    IoRegisterDriverReinitialization(DriverObject, FtpDriverReinitialization, RootExtension);

    //FtpQueryRegistryRevertEntries(...);

    RtlDeleteRegistryValue(0, RegistryPath->Buffer, L"GptAttributeRevertEntries");

    IoInvalidateDeviceState(RootExtension->VolControlRootPdo);

    DPRINT("DriverEntry: exit with STATUS_SUCCESS\n");

    return STATUS_SUCCESS;
}

/* EOF */
