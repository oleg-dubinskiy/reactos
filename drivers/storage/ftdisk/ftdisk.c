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
  #pragma alloc_text(PAGE, FtpQueryRootId)
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

NTSTATUS
NTAPI
FtpQueryRootId(
    _In_ PROOT_EXTENSION RootExtension,
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
    PDEVICE_OBJECT AttachedToDevice;
    PROOT_EXTENSION RootExtension;
    PVOLUME_EXTENSION VolumeExtension;
    PIO_STACK_LOCATION IoStack;
    PDEVICE_RELATIONS DeviceRelation;
    PDEVICE_CAPABILITIES Capabilities;
    PLIST_ENTRY Entry;
    KEVENT Event;
    ULONG size;
    ULONG ix;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    DPRINT("FtpPnpFdo: DeviceObject %p, Irp %p\n", DeviceObject, Irp);

    RootExtension = DeviceObject->DeviceExtension;
    AttachedToDevice = RootExtension->AttachedToDevice;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

  #if DBG
    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            DPRINT("IRP_MN_START_DEVICE\n");
            break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
            DPRINT("IRP_MN_QUERY_REMOVE_DEVICE\n");
            break;

        case IRP_MN_REMOVE_DEVICE:
            DPRINT("USBPORT_FdoPnP: IRP_MN_REMOVE_DEVICE\n");
            break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
            DPRINT("IRP_MN_CANCEL_REMOVE_DEVICE\n");
            break;

        case IRP_MN_STOP_DEVICE:
            DPRINT("IRP_MN_STOP_DEVICE\n");
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
            DPRINT("IRP_MN_QUERY_STOP_DEVICE\n");
            break;

        case IRP_MN_CANCEL_STOP_DEVICE:
            DPRINT("IRP_MN_CANCEL_STOP_DEVICE\n");
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            DPRINT("IRP_MN_QUERY_DEVICE_RELATIONS\n");
            break;

        case IRP_MN_QUERY_INTERFACE:
            DPRINT("IRP_MN_QUERY_INTERFACE\n");
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            DPRINT("IRP_MN_QUERY_CAPABILITIES\n");
            break;

        case IRP_MN_QUERY_RESOURCES:
            DPRINT("IRP_MN_QUERY_RESOURCES\n");
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_QUERY_RESOURCE_REQUIREMENTS\n");
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            DPRINT("IRP_MN_QUERY_DEVICE_TEXT\n");
            break;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
            break;

        case IRP_MN_READ_CONFIG:
            DPRINT("IRP_MN_READ_CONFIG\n");
            break;

        case IRP_MN_WRITE_CONFIG:
            DPRINT("IRP_MN_WRITE_CONFIG\n");
            break;

        case IRP_MN_EJECT:
            DPRINT("IRP_MN_EJECT\n");
            break;

        case IRP_MN_SET_LOCK:
            DPRINT("IRP_MN_SET_LOCK\n");
            break;

        case IRP_MN_QUERY_ID:
            DPRINT("IRP_MN_QUERY_ID\n");
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            DPRINT("IRP_MN_QUERY_PNP_DEVICE_STATE\n");
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
            DPRINT("IRP_MN_QUERY_BUS_INFORMATION\n");
            break;

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            DPRINT("IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            DPRINT1("IRP_MN_SURPRISE_REMOVAL\n");
            break;

        case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
            DPRINT("IRP_MN_QUERY_LEGACY_BUS_INFORMATION\n");
            break;

        default:
            DPRINT1("FtpPnpFdo: Unknown PNP IRP_MN_ (%X)\n", IoStack->MinorFunction);
            break;
    }
  #endif

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_RESOURCES:
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(AttachedToDevice, Irp);
        }
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
        {
            DPRINT("FtpPnpFdo: STATUS_INVALID_DEVICE_REQUEST\n");
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }
        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        {
            DPRINT1("FtpPnpFdo: FIXME\n");
            ASSERT(FALSE);
            break;
        }
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            if (IoStack->Parameters.QueryDeviceRelations.Type != BusRelations)
            {
                IoSkipCurrentIrpStackLocation(Irp);
                return IoCallDriver(AttachedToDevice, Irp);
            }

            FtpAcquire(RootExtension);

            ix = 0;

            for (Entry = RootExtension->VolumeList.Flink;
                 Entry != &RootExtension->VolumeList;
                 Entry = Entry->Flink)
            {
                ix++;
            }

            size = (sizeof(DEVICE_RELATIONS) - sizeof(PDEVICE_OBJECT));
            size += (ix * sizeof(PDEVICE_OBJECT));

            DeviceRelation = ExAllocatePoolWithTag(PagedPool, size, 'tFcS');
            if (!DeviceRelation)
            {
                FtpRelease(RootExtension);

                Irp->IoStatus.Information = 0;
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;

                IoCompleteRequest(Irp, 0);

                return STATUS_INSUFFICIENT_RESOURCES;
            }

            DeviceRelation->Count = ix;

            if (Entry != &RootExtension->VolumeList)
            {
                ix = 0;

                for (Entry = RootExtension->VolumeList.Flink;
                     Entry != &RootExtension->VolumeList;
                     Entry = Entry->Flink)
                {
                    VolumeExtension = CONTAINING_RECORD(Entry, VOLUME_EXTENSION, Link);

                    DeviceRelation->Objects[ix] = VolumeExtension->SelfDeviceObject;
                    ObReferenceObject(DeviceRelation->Objects[ix]);

                    ix++;
                }
            }

            while (!IsListEmpty(&RootExtension->EmptyVolumesList))
            {
                ASSERT(FALSE);
                //Entry = RemoveHeadList(&RootExtension->EmptyVolumesList);
                //VolumeExtension = CONTAINING_RECORD(Entry, VOLUME_EXTENSION, Link);
                //VolumeExtension->VolExt31 = 1;
            }

            FtpRelease(RootExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = (ULONG_PTR)DeviceRelation;

            IoSkipCurrentIrpStackLocation(Irp);

            return IoCallDriver(AttachedToDevice, Irp);
        }
        case IRP_MN_QUERY_CAPABILITIES:
        {
              KeInitializeEvent(&Event, NotificationEvent, FALSE);

              IoCopyCurrentIrpStackLocationToNext(Irp);
              IoSetCompletionRoutine(Irp, FtpSignalCompletion, &Event, TRUE, TRUE, TRUE);

              IoCallDriver(AttachedToDevice, Irp);

              KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

              Capabilities = IoStack->Parameters.DeviceCapabilities.Capabilities;

              Capabilities->RawDeviceOK = 1;
              Capabilities->SilentInstall = 1;

              return Irp->IoStatus.Status;
        }
        case IRP_MN_QUERY_ID:
        {
            Status = FtpQueryRootId(RootExtension, Irp);
            if (NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoSkipCurrentIrpStackLocation(Irp);

                Status = IoCallDriver(AttachedToDevice, Irp);
                DPRINT1("FtpPnpFdo: Status (%X)\n", Status);
                return Status;
            }

            if (Status == STATUS_NOT_SUPPORTED)
            {
                IoSkipCurrentIrpStackLocation(Irp);
                Status = IoCallDriver(AttachedToDevice, Irp);
                DPRINT1("FtpPnpFdo: Status (%X)\n", Status);
                return Status;
            }

            break;
        }
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
        {
              KeInitializeEvent(&Event, NotificationEvent, FALSE);

              IoCopyCurrentIrpStackLocationToNext(Irp);
              IoSetCompletionRoutine(Irp, FtpSignalCompletion, &Event, TRUE, TRUE, TRUE);

              IoCallDriver(AttachedToDevice, Irp);
              KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

              Status = Irp->IoStatus.Status;

              /* Do not show device in device manager. */
              if (!NT_SUCCESS(Status))
              {
                  Status = STATUS_SUCCESS;
                  Irp->IoStatus.Information = (PNP_DEVICE_DONT_DISPLAY_IN_UI | PNP_DEVICE_NOT_DISABLEABLE);
              }
              else
              {
                  Irp->IoStatus.Information |= (PNP_DEVICE_DONT_DISPLAY_IN_UI | PNP_DEVICE_NOT_DISABLEABLE);
              }

              return Status;
        }
        default:
        {
            DPRINT1("FtpPnpFdo: Unknown PNP IRP_MN_ (%X)\n", IoStack->MinorFunction);
            ASSERT(FALSE);
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(AttachedToDevice, Irp);
        }
    }

    if (Status != STATUS_NOT_SUPPORTED)
        Irp->IoStatus.Status = Status;
    else
        Status = Irp->IoStatus.Status;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    DPRINT("FtpPnpFdo: Status (%X)\n", Status);
    return Status;
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
