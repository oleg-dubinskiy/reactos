/*
 * PROJECT:     Volume manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main header file
 * COPYRIGHT:   Copyright 2018, 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#ifndef _FTDISK_H_
#define _FTDISK_H_

#include <ntifs.h>
#include <initguid.h>
#include <mountmgr.h>
#include <ntdddisk.h>
#include <stdio.h>

/* NT compatible */
#define IsRecognizedPartition_(PartitionType) ( \
    (((PartitionType) & PARTITION_NTFT) && (((PartitionType) & ~0xC0) == PARTITION_FAT_12)) || \
    (((PartitionType) & PARTITION_NTFT) && (((PartitionType) & ~0xC0) == PARTITION_HUGE)) || \
    (((PartitionType) & PARTITION_NTFT) && (((PartitionType) & ~0xC0) == PARTITION_IFS)) || \
    (((PartitionType) & PARTITION_NTFT) && (((PartitionType) & ~0xC0) == PARTITION_FAT32)) || \
    (((PartitionType) & PARTITION_NTFT) && (((PartitionType) & ~0xC0) == PARTITION_FAT32_XINT13)) || \
    (((PartitionType) & PARTITION_NTFT) && (((PartitionType) & ~0xC0) == PARTITION_XINT13)) || \
    ((PartitionType) == PARTITION_FAT_12) || \
    ((PartitionType) == PARTITION_FAT_16) || \
    ((PartitionType) == PARTITION_HUGE) || \
    ((PartitionType) == PARTITION_IFS) || \
    ((PartitionType) == PARTITION_FAT32) || \
    ((PartitionType) == PARTITION_FAT32_XINT13) || \
    ((PartitionType) == PARTITION_XINT13))

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
    BOOLEAN IsBootReinitialized;
    UNICODE_STRING RegistryPath;
    ULONG EmptyDeviceCount;
} ROOT_EXTENSION, *PROOT_EXTENSION;

typedef struct _VOLUME_EXTENSION *PVOLUME_EXTENSION;
typedef VOID (NTAPI* PFT_ZERO_REF_CALLBACK)(_In_ PVOLUME_EXTENSION VolumeExtension);

typedef struct _VOLUME_EXTENSION
{
    PDEVICE_OBJECT SelfDeviceObject; // Volume PDO
    struct _ROOT_EXTENSION* RootExtension;
    ULONG DeviceExtensionType;
    KSPIN_LOCK SpinLock;
    PDEVICE_OBJECT PartitionPdo;
    PVOID FtVolume;
    PVOID RundownCache;
    PFT_ZERO_REF_CALLBACK ZeroRefCallback;
    PVOID ZeroRefContext;
    LIST_ENTRY IrpList;
    BOOLEAN IsStartCallback;
    BOOLEAN IsUnknown00;
    BOOLEAN IsUnknown01;
    BOOLEAN IsUnknown02;
    BOOLEAN IsVolumeOffline;
    BOOLEAN IsEmptyVolume;
    BOOLEAN IsGptPartition;
    BOOLEAN IsHidenPartition;
    BOOLEAN IsReadOnlyPartition;
    BOOLEAN IsSystemPartition;
    LONG Lock;
    LIST_ENTRY Link;
    ULONG VolumeNumber;
    LIST_ENTRY UniqueIdNotifyList;
    UNICODE_STRING DevnodeName;
    PDEVICE_OBJECT WholeDiskPdo;
    PDEVICE_OBJECT WholeDiskDevice;
    ULONGLONG StartingOffset;
    ULONGLONG PartitionLength;
    ULONG DiskSignature;
    UNICODE_STRING SymbolicLinkName;
    KSEMAPHORE ZeroRefSemaphore;
    KSEMAPHORE Semaphore;
    GUID GptPartitionId;
    ULONGLONG GptAttributes;
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

NTSTATUS
NTAPI
FtpQueryPartitionInformation(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PDEVICE_OBJECT PartitionPdo,
    _Out_ ULONG* OutDeviceNumber,
    _Out_ ULONGLONG* OutStartingOffset,
    _Out_ ULONG* OutPartitionNumber,
    _Out_ UCHAR* OutMbrPartitionType,
    _Out_ LONGLONG* OutPartitionLength,
    _Out_ GUID* OutGptPartitionType,
    _Out_ GUID* OutGptPartitionId,
    _Out_ UCHAR* OutIsGptPartition,
    _Out_ ULONGLONG* OutGptAttributes
);

NTSTATUS
NTAPI
FtpReadPartitionTableEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDRIVE_LAYOUT_INFORMATION_EX* OutDriveLayout
);

BOOLEAN
NTAPI
FtpCreateNewDevice(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PDEVICE_OBJECT PartitionPdo,
    _In_ PVOID FtVolume, // FT_VOLUME*
    _In_ PDEVICE_OBJECT WholeDiskPdo,
    _In_ ULONG Alignment,
    _In_ BOOLEAN IsEmptyDevice,
    _In_ BOOLEAN IsHidenPartition,
    _In_ BOOLEAN IsReadOnlyPartition,
    _In_ BOOLEAN IsSystemPartition,
    _In_ ULONGLONG GptAttributes
);

ULONG
NTAPI
FtpQueryDiskSignature(
    _In_ PDEVICE_OBJECT WholeDiskPdo
);

ULONG
NTAPI
FtpQueryDiskSignatureCache(
    _In_ PVOLUME_EXTENSION VolumeExtension
);

VOID
NTAPI
FtpCreateOldNameLinks(
    _In_ PVOLUME_EXTENSION VolumeExtension
);

NTSTATUS
NTAPI
FtpQueryId(
    _In_ PVOLUME_EXTENSION VolumeExtension,
    _In_ PIRP Irp
);

#endif /* _FTDISK_H_ */

/* EOF */
