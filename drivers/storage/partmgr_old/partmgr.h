/*
 * PROJECT:     Partition manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main header file
 * COPYRIGHT:   Copyright 2018, 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#ifndef _PARTMGR_H_
#define _PARTMGR_H_

#include <ntifs.h>

typedef struct _PM_DRIVER_EXTENSION
{
    PDRIVER_OBJECT SelfDriverObject;
    LIST_ENTRY NotifyList;
    LIST_ENTRY ExtensionList;
    PVOID NotificationEntry;
    KMUTEX Mutex;
    LONG IsReinitialized;
    RTL_AVL_TABLE TableSignature;
    RTL_AVL_TABLE TableGuid;
    UNICODE_STRING RegistryPath;
} PM_DRIVER_EXTENSION, *PPM_DRIVER_EXTENSION;

typedef struct _PM_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PartitionFido; // self device object (filter device object for partition)
    PPM_DRIVER_EXTENSION DriverExtension;
    PDEVICE_OBJECT AttachedToDevice; // the topmost device object on the stack to which the current device is attached
    PDEVICE_OBJECT WholeDiskPdo; // PDO created for the disk device stack
    LIST_ENTRY PartitionList;
    LIST_ENTRY Link;
    KEVENT Event;
    LIST_ENTRY ListOfSignatures;
    LIST_ENTRY ListOfGuids;
    UNICODE_STRING NameString;
    WCHAR NameBuffer[64];
} PM_DEVICE_EXTENSION, *PPM_DEVICE_EXTENSION;

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
NTAPI
PmAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DiskPdo
);

NTSTATUS
NTAPI
PmDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
);

NTSTATUS
NTAPI
PmPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
);

NTSTATUS
NTAPI
PmWmi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
);

NTSTATUS
NTAPI
PmPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
);

RTL_GENERIC_COMPARE_RESULTS
NTAPI
PmTableSignatureCompareRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct
);

RTL_GENERIC_COMPARE_RESULTS
NTAPI
PmTableGuidCompareRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct
);

PVOID
NTAPI
PmTableAllocateRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ CLONG ByteSize
);

VOID
NTAPI
PmTableFreeRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID Buffer
);

VOID
NTAPI
PmDriverReinit(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count
);


#endif /* _PARTMGR_H_ */

/* EOF */
