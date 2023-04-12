/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main header file
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#ifndef _ACPI_H_
#define _ACPI_H_

#include <ntddk.h>
#include <stdio.h>

/* STRUCTURES ***************************************************************/


/* ACPI DRIVER **************************************************************/

typedef struct _ACPI_HAL_DISPATCH_TABLE
{
    ULONG Signature;
    ULONG Version;
    PVOID Function1;
    PVOID Function2;
    PVOID Function3;
} ACPI_HAL_DISPATCH_TABLE, *PACPI_HAL_DISPATCH_TABLE;

typedef struct _ACPI_DEVICE_POWER_NODE
{
    struct _ACPI_DEVICE_POWER_NODE* Next;
    struct _DEVICE_EXTENSION* DeviceExtension;
    LIST_ENTRY DevicePowerListEntry;
} ACPI_DEVICE_POWER_NODE, *PACPI_DEVICE_POWER_NODE;

typedef struct _ACPI_POWER_INFO
{
    DEVICE_POWER_STATE DevicePowerMatrix[7];
    SYSTEM_POWER_STATE SystemWakeLevel;
    DEVICE_POWER_STATE DeviceWakeLevel;
    LIST_ENTRY PowerRequestListEntry;
} ACPI_POWER_INFO, *PACPI_POWER_INFO;

typedef struct _IRP_DISPATCH_TABLE
{
    PDRIVER_DISPATCH CreateClose;
    PDRIVER_DISPATCH DeviceControl;
    PDRIVER_DISPATCH PnpStartDevice;
    PDRIVER_DISPATCH* Pnp;
    PDRIVER_DISPATCH* Power;
    PDRIVER_DISPATCH SystemControl;
    PDRIVER_DISPATCH Other;
    VOID (NTAPI* Worker)(struct _DEVICE_EXTENSION*, ULONG);
} IRP_DISPATCH_TABLE, *PIRP_DISPATCH_TABLE;

typedef struct _DEVICE_EXTENSION
{
    ULONG Signature;
    PIRP_DISPATCH_TABLE DispatchTable;
    ACPI_POWER_INFO PowerInfo;
    union
    {
        PCHAR DeviceID;
        PCHAR Address;
    };
    PCHAR InstanceID;
    LONG OutstandingIrpCount;
    LONG ReferenceCount;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT TargetDeviceObject;
    PDEVICE_OBJECT PhysicalDeviceObject;
    LIST_ENTRY ChildDeviceList;
    LIST_ENTRY SiblingDeviceList;
    LIST_ENTRY EjectDeviceHead;
    LIST_ENTRY EjectDeviceList;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

/* FUNCTIONS ****************************************************************/

/* acpiinit.c */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

/* dispatch.c */
NTSTATUS
NTAPI
ACPIDispatchIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
);

VOID
NTAPI
ACPIFilterFastIoDetachCallback(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice
);

VOID
NTAPI
ACPIInitHalDispatchTable(
     VOID
);

NTSTATUS NTAPI ACPIIrpDispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIDispatchForwardIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIDispatchIrpSuccess(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIDispatchWmiLog(PDEVICE_OBJECT DeviceObject, PIRP Irp);

NTSTATUS NTAPI ACPIRootIrpStartDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIRootIrpQueryRemoveOrStopDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIRootIrpRemoveDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIRootIrpCancelRemoveOrStopDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIRootIrpStopDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIRootIrpQueryDeviceRelations(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIRootIrpQueryInterface(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIRootIrpQueryCapabilities(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIFilterIrpDeviceUsageNotification(PDEVICE_OBJECT DeviceObject, PIRP Irp);

NTSTATUS NTAPI ACPIWakeWaitIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIDispatchForwardPowerIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIRootIrpSetPower(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI ACPIRootIrpQueryPower(PDEVICE_OBJECT DeviceObject, PIRP Irp);

ULONGLONG
NTAPI
ACPIInternalUpdateFlags(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG InputFlags,
    _In_ BOOLEAN IsResetFlags
);


/* registry.c */
VOID
NTAPI
ACPIInitReadRegistryKeys(
    VOID
);

NTSTATUS
NTAPI
OSCloseHandle(
    _In_ HANDLE Handle
);

NTSTATUS
NTAPI
OSOpenUnicodeHandle(
    _In_ PUNICODE_STRING Name,
    _In_ HANDLE ParentKeyHandle,
    _In_ PHANDLE KeyHandle
);

NTSTATUS
NTAPI
OSOpenHandle(
    _In_ PSZ NameString,
    _In_ HANDLE ParentKeyHandle,
    _In_ PHANDLE KeyHandle
);

NTSTATUS
NTAPI
OSReadRegValue(
    _In_ PSZ SourceString,
    _In_ HANDLE Handle,
    _In_ PVOID ValueEntry,
    _Out_ ULONG *OutMaximumLength
);

NTSTATUS
NTAPI
OSReadAcpiConfigurationData(
    _Out_ PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64* OutKeyInfo
);

NTSTATUS
NTAPI
OSGetRegistryValue(
    _In_ HANDLE KeyHandle,
    _In_ PWSTR NameString,
    _In_ PVOID* OutValue
);

#endif /* _ACPI_H_ */

/* EOF */
