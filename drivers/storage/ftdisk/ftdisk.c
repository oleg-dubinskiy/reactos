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
#endif

/* GLOBALS *******************************************************************/

GUID VOLMGR_VOLUME_MANAGER_GUID = {0x53F5630E, 0xB6BF, 0x11D0, {0X94, 0XF2, 0X00, 0XA0, 0XC9, 0X1E, 0XFB, 0X8B}};

/* FUNCTIONS ****************************************************************/

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
