/*
 * PROJECT:     Volume manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main header file
 * COPYRIGHT:   Copyright 2018, 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#ifndef _FTDISK_H_
#define _FTDISK_H_

#include <ntifs.h>

typedef struct _FT_LOGICAL_DISK_INFORMATION_SET
{
    ULONG DiskInfoCount;
    PVOID* LogicalDiskInfoArray;
    ULONG Reserved1;
    ULONG Reserved2;
} FT_LOGICAL_DISK_INFORMATION_SET, *PFT_LOGICAL_DISK_INFORMATION_SET;

typedef struct _ROOT_EXTENSION
{
    PDEVICE_OBJECT SelfDeviceObject; // RootFdo
    struct _ROOT_EXTENSION* RootExtension;
    ULONG DeviceExtensionType; // 0 Root, 1 Volume
    KSPIN_LOCK SpinLock;
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT AttachedToDevice; // RootFdo attached to ...
    PDEVICE_OBJECT VolControlRootPdo; // Pdo created PNP Mgr
    LIST_ENTRY VolumeList;
    LIST_ENTRY EmptyVolumesList;
    ULONG VolumeCounter;
    PFT_LOGICAL_DISK_INFORMATION_SET LogicalDiskInfoSet;
    LIST_ENTRY WorkerQueue;
    LIST_ENTRY ChangeNotifyIrpList;
    KSEMAPHORE RootSemaphore;
    UNICODE_STRING SymbolicLinkName;
    UNICODE_STRING RegistryPath;
} ROOT_EXTENSION, *PROOT_EXTENSION;

typedef struct _VOLUME_EXTENSION
{
    PDEVICE_OBJECT SelfDeviceObject; // Volume PDO
    struct _ROOT_EXTENSION* RootExtension;
    ULONG DeviceExtensionType;
    KSPIN_LOCK SpinLock;
    LIST_ENTRY Link;
    ULONG VolumeNumber;
} VOLUME_EXTENSION, *PVOLUME_EXTENSION;

typedef struct _FT_PARTITION_ARRIVED
{
    PDEVICE_OBJECT PartitionPdo;
    PDEVICE_OBJECT WholeDiskPdo;
} FT_PARTITION_ARRIVED, *PFT_PARTITION_ARRIVED;

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
NTAPI
FtDiskAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT VolControlRootPdo,
    _In_ PUNICODE_STRING RegistryPath
);

VOID
NTAPI
FtDiskUnload(
    _In_ PDRIVER_OBJECT DriverObject
);

NTSTATUS
NTAPI
FtDiskDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
);

NTSTATUS
NTAPI
FtWmi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
);

NTSTATUS
NTAPI
FtDiskPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
);

VOID
NTAPI
FtpBootDriverReinitialization(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count
);

VOID
NTAPI
FtpDriverReinitialization(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count
);

NTSTATUS
NTAPI
FtpQueryRootId(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PIRP Irp
);

NTSTATUS
NTAPI
FtpPartitionArrived(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PIRP Irp
);

NTSTATUS
NTAPI
FtpPartitionArrivedHelper(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PDEVICE_OBJECT PartitionPdo,
    _In_ PDEVICE_OBJECT WholeDiskPdo
);

#endif /* _FTDISK_H_ */

/* EOF */
