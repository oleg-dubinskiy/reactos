/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main header file
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#ifndef _ACPI_H_
#define _ACPI_H_

#include <ntddk.h>
#include <drivers/acpi/acpi.h> //sdk/include/reactos/drivers/acpi/acpi.h
#include <stdio.h>
#include "amli.h"

/* STRUCTURES ***************************************************************/

/* ACPI TABLES **************************************************************/

typedef struct _MAPIC
{
    DESCRIPTION_HEADER Header;
    ULONG LocalAPICAddress;
    ULONG Flags;
    ULONG APICTables[1];
} MAPIC, *PMAPIC;

typedef struct _ACPIInformation
{
    PRSDT RootSystemDescTable;
    PFADT FixedACPIDescTable;
    PFACS FirmwareACPIControlStructure;
    PDSDT DiffSystemDescTable;
    PMAPIC MultipleApicTable;
    PULONG GlobalLock;
    LIST_ENTRY GlobalLockQueue;
    KSPIN_LOCK GlobalLockQueueLock;
    PVOID GlobalLockOwnerContext;
    ULONG GlobalLockOwnerDepth;
    BOOLEAN ACPIOnly;
    UCHAR Pad0[3];
    ULONG PM1a_BLK;
    ULONG PM1b_BLK;
    ULONG PM1a_CTRL_BLK;
    ULONG PM1b_CTRL_BLK;
    ULONG PM2_CTRL_BLK;
    ULONG PM_TMR;
    ULONG GP0_BLK;
    ULONG GP0_ENABLE;
    UCHAR GP0_LEN;
    UCHAR Pad1;
    USHORT Gpe0Size;
    ULONG GP1_BLK;
    ULONG GP1_ENABLE;
    UCHAR GP1_LEN;
    UCHAR Pad2;
    USHORT Gpe1Size;
    USHORT GP1_Base_Index;
    USHORT GpeSize;
    ULONG SMI_CMD;
    USHORT pm1_en_bits;
    USHORT pm1_wake_mask;
    USHORT pm1_wake_status;
    USHORT c2_latency;
    USHORT c3_latency;
    UCHAR Pad3[2];
    ULONG ACPI_Flags;
    ULONG ACPI_Capabilities;
    BOOLEAN Dockable;
    UCHAR Pad4[3];
} ACPI_INFORMATION, *PACPI_INFORMATION;

typedef struct _RSDTELEMENT
{
    ULONG Flags;
    PVOID Handle;
    PVOID Address;
} RSDTELEMENT, *PRSDTELEMENT;

typedef struct _RSDTINFORMATION
{
    ULONG NumElements;
    RSDTELEMENT Tables[1];
} RSDTINFORMATION, *PRSDTINFORMATION;

/* ACPI DRIVER **************************************************************/

typedef enum
{
    Stopped = 0x0,
    Inactive = 0x1,
    Started = 0x2,
    Removed = 0x3,
    SurpriseRemoved = 0x4,
    Invalid = 0x5,
} ACPI_DEVICE_STATE;

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
    ULONG WakeSupportCount;
    LIST_ENTRY WakeSupportList;
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

typedef struct _EXTENSION_WORKER
{
    ULONG PendingEvents;
    LIST_ENTRY Link;
} EXTENSION_WORKER, *PEXTENSION_WORKER;

typedef struct _BUTTON_EXTENSION
{
    EXTENSION_WORKER WorkQueue;
    KSPIN_LOCK SpinLock;
    CHAR LidState;
    union
    {
        ULONG Events;
        struct
        {
            ULONG Power_Button : 1;
            ULONG Sleep_Button : 1;
            ULONG Lid_Switch : 1;
            ULONG Reserved : 28;
            ULONG Wake_Capable : 1;
        } UEvents;
    };
    union
    {
        ULONG Capabilities;
        struct
        {
          ULONG Power_Button : 1;
          ULONG Sleep_Button : 1;
          ULONG Lid_Switch : 1;
          ULONG Reserved : 28;
          ULONG Wake_Capable : 1;
        } UCapabilities;
    };
} BUTTON_EXTENSION, *PBUTTON_EXTENSION;

typedef struct _DEVICE_EXTENSION
{
    union
    {
        ULONGLONG Flags;
        struct
        {
            ULONGLONG Type_Never_Present : 1;
            ULONGLONG Type_Not_Present : 1;
            ULONGLONG Type_Removed : 1;
            ULONGLONG Type_Not_Found : 1;
            ULONGLONG Type_Fdo : 1;
            ULONGLONG Type_Pdo : 1;
            ULONGLONG Type_Filter : 1;
            ULONGLONG Type_Surprise_Removed : 1;
            ULONGLONG Type_Not_Enumerated : 1;
            ULONGLONG Reserved1 : 7;
            ULONGLONG Cap_Wake : 1;
            ULONGLONG Cap_Raw : 1;
            ULONGLONG Cap_Button : 1;
            ULONGLONG Cap_Always_PS0 : 1;
            ULONGLONG Cap_No_Filter : 1;
            ULONGLONG Cap_No_Stop : 1;
            ULONGLONG Cap_No_Override : 1;
            ULONGLONG Cap_ISA : 1;
            ULONGLONG Cap_EIO : 1;
            ULONGLONG Cap_PCI : 1;
            ULONGLONG Cap_Serial : 1;
            ULONGLONG Cap_Thermal_Zone : 1;
            ULONGLONG Cap_LinkNode : 1;
            ULONGLONG Cap_No_Show_in_UI : 1;
            ULONGLONG Cap_Never_show_in_UI : 1;
            ULONGLONG Cap_Start_in_D3 : 1;
            ULONGLONG Cap_PCI_Device : 1;
            ULONGLONG Cap_PIC_Device : 1;
            ULONGLONG Cap_Unattached_Dock : 1;
            ULONGLONG Cap_No_Disable_Wake : 1;
            ULONGLONG Cap_Processor : 1;
            ULONGLONG Cap_Container : 1;
            ULONGLONG Cap_PCI_Bar_Target : 1;
            ULONGLONG Cap_No_Remove_or_Eject : 1;
            ULONGLONG Reserved2 : 1;
            ULONGLONG Prop_Rebuild_Children : 1;
            ULONGLONG Prop_Invalid_Relations : 1;
            ULONGLONG Prop_Unloading : 1;
            ULONGLONG Prop_Address : 1;
            ULONGLONG Prop_HID : 1;
            ULONGLONG Prop_UID : 1;
            ULONGLONG Prop_Fixed_HID : 1;
            ULONGLONG Prop_Fixed_UID : 1;
            ULONGLONG Prop_Failed_Init : 1;
            ULONGLONG Prop_Srs_Present : 1;
            ULONGLONG Prop_No_Object : 1;
            ULONGLONG Prop_Exclusive : 1;
            ULONGLONG Prop_Ran_INI : 1;
            ULONGLONG Prop_Device_Enabled : 1;
            ULONGLONG Prop_Device_Failed : 1;
            ULONGLONG Prop_Acpi_Power : 1;
            ULONGLONG Prop_Dock : 1;
            ULONGLONG Prop_Built_Power_Table : 1;
            ULONGLONG Prop_Has_PME : 1;
            ULONGLONG Prop_No_Lid_Action : 1;
            ULONGLONG Prop_Fixed_Address : 1;
            ULONGLONG Prop_Callback : 1;
            ULONGLONG Prop_Fixed_CiD : 1;
        } UFlags;
    };
    ULONG Signature;
    PIRP_DISPATCH_TABLE DispatchTable;
    union
    {
        EXTENSION_WORKER WorkQueue;
        BUTTON_EXTENSION Button;
    };
    ACPI_DEVICE_STATE DeviceState;
    ACPI_DEVICE_STATE PreviousState;
    ACPI_POWER_INFO PowerInfo;
    union
    {
        PCHAR DeviceID;
        PCHAR Address;
    };
    PCHAR InstanceID;
    PCM_RESOURCE_LIST ResourceList;
    LONG OutstandingIrpCount;
    LONG ReferenceCount;
    PKEVENT RemoveEvent;
    PAMLI_NAME_SPACE_OBJECT AcpiObject;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT TargetDeviceObject;
    PDEVICE_OBJECT PhysicalDeviceObject;
    struct _DEVICE_EXTENSION* ParentExtension;
    LIST_ENTRY ChildDeviceList;
    LIST_ENTRY SiblingDeviceList;
    LIST_ENTRY EjectDeviceHead;
    LIST_ENTRY EjectDeviceList;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

typedef struct _ACPI_BUILD_REQUEST
{
    LIST_ENTRY Link;
    ULONG Signature;
    ULONG Flags;
    LONG WorkDone;
    ULONG BuildReserved1;
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;
    PVOID CallBack;
    PVOID CallBackContext;
    PLIST_ENTRY ListHead1;
    PVOID Context;
    ULONG BuildReserved4;
    PLIST_ENTRY ListHeadForInsert;
} ACPI_BUILD_REQUEST, *PACPI_BUILD_REQUEST;

/* FUNCTIONS ****************************************************************/

#ifndef Add2Ptr
  #define Add2Ptr(P,I) ((PVOID)((PUCHAR)(P) + (I)))
#endif

typedef USHORT
(NTAPI* PACPI_READ_REGISTER)(
    _In_ ULONG RegType,
    _In_ ULONG Size
);

typedef VOID
(NTAPI* PACPI_WRITE_REGISTER)(
    _In_ ULONG RegType,
    _In_ ULONG Size,
    _In_ USHORT Value
);

typedef NTSTATUS
(NTAPI * PACPI_BUILD_DISPATCH)(
    _In_ PACPI_BUILD_REQUEST BuildRequest
);

/* acpiinit.c */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

PRSDT
NTAPI
ACPILoadFindRSDT(
    VOID
);

NTSTATUS
NTAPI
ACPILoadProcessRSDT(
    VOID
);

NTSTATUS
NTAPI
ACPILoadProcessFADT(
    _In_ PFADT Fadt
);

NTSTATUS
__cdecl
ACPICallBackLoad(
    _In_ int Param1,
    _In_ int Param2
);

NTSTATUS
__cdecl
ACPICallBackUnload(
    _In_ int Param1,
    _In_ int Param2
);

NTSTATUS
__cdecl
ACPITableNotifyFreeObject(
    _In_ int Param1,
    _In_ int Param2,
    _In_ int Param3
);

NTSTATUS
__cdecl
NotifyHandler(
    _In_ int Param1,
    _In_ int Param2,
    _In_ int Param3
);

NTSTATUS
__cdecl
GlobalLockEventHandler(
    _In_ int Param1,
    _In_ int Param2,
    _In_ int Param3,
    _In_ int Param4,
    _In_ int Param5
);

NTSTATUS
__cdecl
OSNotifyCreate(
    _In_ ULONG Type,
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject
);

NTSTATUS
__cdecl
OSNotifyFatalError(
    _In_ int Param1,
    _In_ int Param2,
    _In_ int Param3,
    _In_ int Param4
);

USHORT
NTAPI
DefPortReadAcpiRegister(
    _In_ ULONG RegType,
    _In_ ULONG Size
);

VOID
NTAPI
DefPortWriteAcpiRegister(
    _In_ ULONG RegType,
    _In_ ULONG Size,
    _In_ USHORT Value
);

VOID
NTAPI
ACPIEnableInitializeACPI(
    _In_ BOOLEAN IsNotRevertAffinity
);

VOID
NTAPI
OSQueueWorkItem(
    _In_ PWORK_QUEUE_ITEM WorkQueueItem
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

NTSTATUS NTAPI ACPIBuildProcessGenericComplete(_In_ PACPI_BUILD_REQUEST BuildRequest);
NTSTATUS NTAPI ACPIBuildProcessRunMethodPhaseCheckSta(_In_ PACPI_BUILD_REQUEST BuildRequest);
NTSTATUS NTAPI ACPIBuildProcessRunMethodPhaseCheckBridge(_In_ PACPI_BUILD_REQUEST BuildRequest);
NTSTATUS NTAPI ACPIBuildProcessRunMethodPhaseRunMethod(_In_ PACPI_BUILD_REQUEST BuildRequest);
NTSTATUS NTAPI ACPIBuildProcessRunMethodPhaseRecurse(_In_ PACPI_BUILD_REQUEST BuildRequest);

ULONGLONG
NTAPI
ACPIInternalUpdateFlags(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG InputFlags,
    _In_ BOOLEAN IsResetFlags
);

VOID
NTAPI
ACPIBuildDeviceDpc(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2
);

BOOLEAN
NTAPI
ACPIInitialize(
    _In_ PDEVICE_OBJECT DeviceObject
);

NTSTATUS
NTAPI
ACPIBuildSynchronizationRequest(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PVOID CallBack,
    _In_ PKEVENT Event,
    _In_ PLIST_ENTRY BuildDeviceList,
    _In_ BOOLEAN IsAddDpc
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

BOOLEAN
NTAPI
ACPIRegReadAMLRegistryEntry(
    _In_ PDESCRIPTION_HEADER* OutTableHeader,
    _In_ BOOLEAN IsNeedUnmap
);

VOID
NTAPI
ACPIRegDumpAcpiTables(
    VOID
);

#endif /* _ACPI_H_ */

/* EOF */
