/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     IRP dispatching
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "acpi.h"

//#define NDEBUG
#include <debug.h>

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, ACPIInitHalDispatchTable)
#endif

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, ACPIRootIrpStartDevice)
  #pragma alloc_text(PAGE, ACPIRootIrpQueryRemoveOrStopDevice)
  #pragma alloc_text(PAGE, ACPIRootIrpCancelRemoveOrStopDevice)
  #pragma alloc_text(PAGE, ACPIRootIrpStopDevice)
  #pragma alloc_text(PAGE, ACPIRootIrpQueryDeviceRelations)
  #pragma alloc_text(PAGE, ACPIRootIrpQueryInterface)
  #pragma alloc_text(PAGE, ACPIRootIrpQueryCapabilities)
  #pragma alloc_text(PAGE, ACPIFilterIrpDeviceUsageNotification)
  #pragma alloc_text(PAGE, ACPIInitialize)
#endif

/* GLOBALS *******************************************************************/

PAMLI_NAME_SPACE_OBJECT ProcessorList[0x20];
ACPI_INTERFACE_STANDARD ACPIInterfaceTable;
ACPI_HAL_DISPATCH_TABLE AcpiHalDispatchTable;
PDEVICE_OBJECT FixedButtonDeviceObject;
PPM_DISPATCH_TABLE PmHalDispatchTable;
PACPI_INFORMATION AcpiInformation;
KSPIN_LOCK NotifyHandlerLock;
KSPIN_LOCK GpeTableLock;
ULONG AcpiSupportedSystemStates;
ULONG InterruptModel;
BOOLEAN AcpiSystemInitialized;

ACPI_INTERNAL_DEVICE_FLAG AcpiInternalDeviceFlagTable[] =
{
    {"CPQB01D", 0x0000000080000000},
    {"IBM3760", 0x0000000080000000},
    {"ACPI0006", 0x0000002000120000},
    {"PNP0000", 0x0010000200300000},
    {"PNP0001", 0x0010000200300000},
    {"PNP0002", 0x0010000000300000},
    {"PNP0003", 0x0010000200300000},
    {"PNP0004", 0x0010000200300000},
    {"PNP0100", 0x0010000000300000},
    {"PNP0101", 0x0010000000300000},
    {"PNP0102", 0x0010000000300000},
    {"PNP0200", 0x0010000000300000},
    {"PNP0201", 0x0010000000300000},
    {"PNP0202", 0x0010000000300000},
    {"PNP0500", 0x0000000004000000},
    {"PNP0501", 0x0000000004000000},
    {"PNP0800", 0x0010000000300000},
    {"PNP0A00", 0x0000000000800000},
    {"PNP0A03", 0x0000000002000000},
    {"PNP0A05", 0x0000000001120000},
    {"PNP0A06", 0x0000000001120000},
    {"PNP0B00", 0x0010000800320000},
    {"PNP0C00", 0x0010000040300000},
    {"PNP0C01", 0x0010000040300000},
    {"PNP0C02", 0x0010000040300000},
    {"PNP0C04", 0x0010000000300000},
    {"PNP0C05", 0x0010000000300000},
    {"PNP0C09", 0x0000000000200000},
    {"PNP0C0B", 0x0010000000320000},
    {"PNP0C0C", 0x0010000800360000},
    {"PNP0C0D", 0x0010000800360000},
    {"PNP0C0E", 0x0010000800360000},
    {"PNP0C0F", 0x0000000010100001},
    {"PNP0C80", 0x0000008000000000},
    {"PNP8294", 0x0000000004000000},
    {"TOS6200", 0x0000000000020000},
    {NULL, 0}
};

ULONG AcpiBuildDevicePowerNameLookup[] =
{
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    '_EJD',
    0,
    '_PRW',
    0,
    '_PR0',
    0,
    '_PR1',
    0,
    '_PR2',
    0,
    '_CRS',
    0,
    '_PSC',
    0
};

ULONG AcpiSxDMethodTable[] =
{
    'DWS_',
    'D0S_',
    'D1S_',
    'D2S_',
    'D3S_',
    'D4S_',
    'D5S_'
};

PDRIVER_DISPATCH ACPIDispatchFdoPnpTable[] =
{
    NULL,
    ACPIRootIrpQueryRemoveOrStopDevice,
    ACPIRootIrpRemoveDevice,
    ACPIRootIrpCancelRemoveOrStopDevice,
    ACPIRootIrpStopDevice,
    ACPIRootIrpQueryRemoveOrStopDevice,
    ACPIRootIrpCancelRemoveOrStopDevice,
    ACPIRootIrpQueryDeviceRelations,
    ACPIRootIrpQueryInterface,
    ACPIRootIrpQueryCapabilities,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    ACPIFilterIrpDeviceUsageNotification,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp
};

PDRIVER_DISPATCH ACPIDispatchPdoPnpTable[] =
{
    NULL,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpRemoveDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIBusIrpStopDevice,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIBusIrpQueryDeviceRelations,
    ACPIBusIrpQueryInterface,
    ACPIBusIrpQueryCapabilities,
    ACPIBusIrpQueryResources,
    ACPIBusIrpQueryResourceRequirements,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpEject,
    ACPIBusIrpSetLock,
    ACPIBusIrpQueryId,
    ACPIBusIrpQueryPnpDeviceState,
    ACPIBusIrpUnhandled,
    ACPIBusIrpDeviceUsageNotification,
    ACPIBusIrpSurpriseRemoval,
    ACPIBusIrpUnhandled
};

PDRIVER_DISPATCH ACPIDispatchFdoPowerTable[] =
{
    ACPIWakeWaitIrp,
    ACPIDispatchForwardPowerIrp,
    ACPIRootIrpSetPower,
    ACPIRootIrpQueryPower,
    ACPIDispatchForwardPowerIrp
};

PDRIVER_DISPATCH ACPIDispatchBusPowerTable[] =
{
    ACPIWakeWaitIrp,
    ACPIDispatchPowerIrpUnhandled,
    ACPIBusIrpSetPower,
    ACPIBusIrpQueryPower,
    ACPIDispatchPowerIrpUnhandled
};

IRP_DISPATCH_TABLE AcpiFdoIrpDispatch =
{
    ACPIDispatchIrpSuccess,
    ACPIIrpDispatchDeviceControl,
    ACPIRootIrpStartDevice,
    ACPIDispatchFdoPnpTable,
    ACPIDispatchFdoPowerTable,
    ACPIDispatchWmiLog,
    ACPIDispatchForwardIrp,
    NULL
};

IRP_DISPATCH_TABLE AcpiPdoIrpDispatch =
{
    ACPIDispatchIrpInvalid,
    ACPIIrpDispatchDeviceControl,
    ACPIBusIrpStartDevice,
    ACPIDispatchPdoPnpTable,
    ACPIDispatchBusPowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    NULL
};

PACPI_BUILD_DISPATCH AcpiBuildRunMethodDispatch[] =
{
    ACPIBuildProcessGenericComplete,
    NULL,
    NULL,
    ACPIBuildProcessRunMethodPhaseCheckSta,
    ACPIBuildProcessRunMethodPhaseCheckBridge,
    ACPIBuildProcessRunMethodPhaseRunMethod,
    ACPIBuildProcessRunMethodPhaseRecurse
};

PACPI_BUILD_DISPATCH AcpiBuildDeviceDispatch[] =
{
    ACPIBuildProcessGenericComplete,
    NULL,
    ACPIBuildProcessDeviceFailure,
    ACPIBuildProcessDevicePhaseAdrOrHid,
    ACPIBuildProcessDevicePhaseAdr,
    ACPIBuildProcessDevicePhaseHid,
    ACPIBuildProcessDevicePhaseUid,
    ACPIBuildProcessDevicePhaseCid,
    ACPIBuildProcessDevicePhaseSta,
    ACPIBuildProcessDeviceGenericEvalStrict,
    ACPIBuildProcessDevicePhaseEjd,
    ACPIBuildProcessDeviceGenericEvalStrict,
    ACPIBuildProcessDevicePhasePrw,
    ACPIBuildProcessDeviceGenericEvalStrict,
    ACPIBuildProcessDevicePhasePr0,
    ACPIBuildProcessDeviceGenericEvalStrict,
    ACPIBuildProcessDevicePhasePr1,
    ACPIBuildProcessDeviceGenericEvalStrict,
    ACPIBuildProcessDevicePhasePr2,
    ACPIBuildProcessDeviceGenericEvalStrict,
    ACPIBuildProcessDevicePhaseCrs,
    ACPIBuildProcessDeviceGenericEval,
    ACPIBuildProcessDevicePhasePsc
};

PACPI_BUILD_DISPATCH AcpiBuildPowerResourceDispatch[] =
{
    ACPIBuildProcessGenericComplete,
    NULL,
    ACPIBuildProcessPowerResourceFailure,
    ACPIBuildProcessPowerResourcePhase0,
    ACPIBuildProcessPowerResourcePhase1
};

PDRIVER_DISPATCH ACPIDispatchBusFilterPnpTable[] =
{
    NULL,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpRemoveDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIBusIrpStopDevice,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIBusIrpQueryDeviceRelations,
    ACPIBusIrpQueryInterface,
    ACPIBusIrpQueryCapabilities,
    ACPIBusIrpQueryResources,
    ACPIBusIrpQueryResourceRequirements,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpEject,
    ACPIBusIrpSetLock,
    ACPIBusIrpQueryId,
    ACPIBusIrpQueryPnpDeviceState,
    ACPIBusIrpUnhandled,
    ACPIBusIrpDeviceUsageNotification,
    ACPIBusIrpSurpriseRemoval,
    ACPIBusIrpUnhandled
};

IRP_DISPATCH_TABLE AcpiGenericBusIrpDispatch =
{
    ACPIDispatchIrpInvalid,
    ACPIDispatchIrpInvalid,
    ACPIBusIrpStartDevice,
    ACPIDispatchBusFilterPnpTable,
    ACPIDispatchBusPowerTable,
    ACPIDispatchForwardIrp,
    ACPIDispatchIrpInvalid,
    NULL
};

PDRIVER_DISPATCH ACPIDispatchInternalDevicePnpTable[] =
{
    NULL,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpRemoveDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIBusIrpStopDevice,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIInternalDeviceQueryDeviceRelations,
    ACPIBusIrpQueryInterface,
    ACPIInternalDeviceQueryCapabilities,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpQueryId,
    ACPIBusIrpQueryPnpDeviceState,
    ACPIBusIrpUnhandled,
    ACPIBusIrpDeviceUsageNotification,
    ACPIBusIrpSurpriseRemoval,
    ACPIBusIrpUnhandled
};

PDRIVER_DISPATCH ACPIDispatchInternalDevicePowerTable[] =
{
    ACPIDispatchPowerIrpInvalid,
    ACPIDispatchPowerIrpUnhandled,
    ACPIDispatchPowerIrpSuccess,
    ACPIDispatchPowerIrpSuccess,
    ACPIDispatchPowerIrpUnhandled,
};

IRP_DISPATCH_TABLE AcpiFixedButtonIrpDispatch =
{
    ACPIDispatchIrpSuccess,
    ACPIButtonDeviceControl,
    ACPIButtonStartDevice,
    ACPIDispatchInternalDevicePnpTable,
    ACPIDispatchInternalDevicePowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    NULL
};

PDRIVER_DISPATCH ACPIDispatchRawDevicePnpTable[] =
{
    NULL,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpRemoveDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIBusIrpStopDevice,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIInternalDeviceQueryDeviceRelations,
    ACPIBusIrpQueryInterface,
    ACPIInternalDeviceQueryCapabilities,
    ACPIBusIrpQueryResources,
    ACPIBusIrpQueryResourceRequirements,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpSetLock,
    ACPIBusIrpQueryId,
    ACPIBusIrpQueryPnpDeviceState,
    ACPIBusIrpUnhandled,
    ACPIBusIrpDeviceUsageNotification,
    ACPIBusIrpSurpriseRemoval,
    ACPIBusIrpUnhandled
};

IRP_DISPATCH_TABLE AcpiRawDeviceIrpDispatch =
{
    ACPIDispatchIrpInvalid,
    ACPIDispatchIrpInvalid,
    ACPIBusIrpStartDevice,
    ACPIDispatchRawDevicePnpTable,
    ACPIDispatchBusPowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    NULL
};

PDRIVER_DISPATCH ACPIDispatchEIOBusPnpTable[] =
{
    NULL,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpRemoveDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIBusIrpStopDevice,
    ACPIBusIrpQueryRemoveOrStopDevice,
    ACPIBusIrpCancelRemoveOrStopDevice,
    ACPIBusIrpQueryDeviceRelations,
    ACPIBusIrpQueryInterface,
    ACPIBusIrpQueryCapabilities,
    ACPIBusIrpQueryResources,
    ACPIBusIrpQueryResourceRequirements,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpEject,
    ACPIBusIrpSetLock,
    ACPIBusIrpQueryId,
    ACPIBusIrpQueryPnpDeviceState,
    ACPIBusIrpQueryBusInformation,
    ACPIBusIrpDeviceUsageNotification,
    ACPIBusIrpSurpriseRemoval,
    ACPIBusIrpUnhandled
};

IRP_DISPATCH_TABLE AcpiEIOBusIrpDispatch =
{
    ACPIDispatchIrpInvalid,
    ACPIDispatchIrpInvalid,
    ACPIBusIrpStartDevice,
    ACPIDispatchEIOBusPnpTable,
    ACPIDispatchBusPowerTable,
    ACPIDispatchForwardIrp,
    ACPIDispatchIrpInvalid,
    NULL
};

IRP_DISPATCH_TABLE AcpiRealTimeClockIrpDispatch =
{
    ACPIDispatchIrpSuccess,
    ACPIDispatchIrpInvalid,
    ACPIInternalDeviceClockIrpStartDevice,
    ACPIDispatchRawDevicePnpTable,
    ACPIDispatchBusPowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    NULL
};

IRP_DISPATCH_TABLE AcpiFanIrpDispatch =
{
    ACPIDispatchIrpSuccess,
    ACPIDispatchIrpInvalid,
    ACPIThermalFanStartDevice,
    ACPIDispatchRawDevicePnpTable,
    ACPIDispatchInternalDevicePowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    NULL
};

PDRIVER_DISPATCH ACPIDispatchButtonPowerTable[] =
{
    ACPIWakeWaitIrp,
    ACPIDispatchPowerIrpUnhandled,
    ACPICMButtonSetPower,
    ACPIDispatchPowerIrpSuccess,
    ACPIDispatchPowerIrpUnhandled,
};

IRP_DISPATCH_TABLE AcpiPowerButtonIrpDispatch =
{
    ACPIDispatchIrpSuccess,
    ACPIButtonDeviceControl,
    ACPICMPowerButtonStart,
    ACPIDispatchInternalDevicePnpTable,
    ACPIDispatchButtonPowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    NULL
};

PDRIVER_DISPATCH ACPIDispatchLidPowerTable[] =
{
    ACPIWakeWaitIrp,
    ACPIDispatchPowerIrpUnhandled,
    ACPICMLidSetPower,
    ACPIDispatchPowerIrpSuccess,
    ACPIDispatchPowerIrpUnhandled,
};

IRP_DISPATCH_TABLE AcpiLidIrpDispatch =
{
    ACPIDispatchIrpSuccess,
    ACPIButtonDeviceControl,
    ACPICMLidStart,
    ACPIDispatchInternalDevicePnpTable,
    ACPIDispatchLidPowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    ACPICMLidWorker
};

IRP_DISPATCH_TABLE AcpiSleepButtonIrpDispatch =
{
    ACPIDispatchIrpSuccess,
    ACPIButtonDeviceControl,
    ACPICMSleepButtonStart,
    ACPIDispatchInternalDevicePnpTable,
    ACPIDispatchButtonPowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    NULL
};

IRP_DISPATCH_TABLE AcpiBusFilterIrpDispatchSucceedCreate =
{
    ACPIDispatchIrpSuccess,
    ACPIIrpDispatchDeviceControl,
    ACPIBusIrpStartDevice,
    ACPIDispatchBusFilterPnpTable,
    ACPIDispatchBusPowerTable,
    ACPIDispatchForwardIrp,
    ACPIDispatchForwardIrp,
    NULL
};

PDRIVER_DISPATCH ACPIDispatchDockPnpTable[] =
{
    NULL,
    ACPIDispatchIrpSuccess,
    ACPIDockIrpRemoveDevice,
    ACPIDispatchIrpSuccess,
    ACPIDispatchIrpSuccess,
    ACPIDispatchIrpSuccess,
    ACPIDispatchIrpSuccess,
    ACPIDockIrpQueryDeviceRelations,
    ACPIDockIrpQueryInterface,
    ACPIDockIrpQueryCapabilities,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIBusIrpUnhandled,
    ACPIDockIrpEject,
    ACPIDockIrpSetLock,
    ACPIDockIrpQueryID,
    ACPIDockIrpQueryPnpDeviceState,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    ACPIDispatchIrpSuccess,
    ACPIBusIrpUnhandled
};

PDRIVER_DISPATCH ACPIDispatchDockPowerTable[] =
{
    ACPIDispatchPowerIrpInvalid,
    ACPIDispatchPowerIrpUnhandled,
    ACPIDockIrpSetPower,
    ACPIDockIrpQueryPower,
    ACPIDispatchPowerIrpUnhandled,
};

IRP_DISPATCH_TABLE AcpiDockPdoIrpDispatch =
{
    ACPIDispatchIrpInvalid,
    ACPIIrpDispatchDeviceControl,
    ACPIDockIrpStartDevice,
    ACPIDispatchDockPnpTable,
    ACPIDispatchDockPowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    NULL
};

IRP_DISPATCH_TABLE AcpiThermalZoneIrpDispatch =
{
    ACPIDispatchIrpSuccess,
    ACPIThermalDeviceControl,
    ACPIThermalStartDevice,
    ACPIDispatchPdoPnpTable,
    ACPIDispatchBusPowerTable,
    ACPIThermalWmi,
    ACPIDispatchIrpInvalid,
    ACPIThermalWorker
};

IRP_DISPATCH_TABLE AcpiProcessorIrpDispatch =
{
    ACPIDispatchIrpInvalid,
    ACPIProcessorDeviceControl,
    ACPIProcessorStartDevice,
    ACPIDispatchRawDevicePnpTable,
    ACPIDispatchBusPowerTable,
    ACPIBusIrpUnhandled,
    ACPIDispatchIrpInvalid,
    NULL
};

ACPI_INTERNAL_DEVICE AcpiInternalDeviceTable[] =
{
    {"ACPI0006", &AcpiGenericBusIrpDispatch},
    {"FixedButton", &AcpiFixedButtonIrpDispatch},
    {"PNP0000", &AcpiRawDeviceIrpDispatch},
    {"PNP0001", &AcpiRawDeviceIrpDispatch},
    {"PNP0002", &AcpiRawDeviceIrpDispatch},
    {"PNP0003", &AcpiRawDeviceIrpDispatch},
    {"PNP0004", &AcpiRawDeviceIrpDispatch},
    {"PNP0100", &AcpiRawDeviceIrpDispatch},
    {"PNP0101", &AcpiRawDeviceIrpDispatch},
    {"PNP0102", &AcpiRawDeviceIrpDispatch},
    {"PNP0200", &AcpiRawDeviceIrpDispatch},
    {"PNP0201", &AcpiRawDeviceIrpDispatch},
    {"PNP0202", &AcpiRawDeviceIrpDispatch},
    {"PNP0800", &AcpiRawDeviceIrpDispatch},
    {"PNP0A05", &AcpiGenericBusIrpDispatch},
    {"PNP0A06", &AcpiEIOBusIrpDispatch},
    {"PNP0B00", &AcpiRealTimeClockIrpDispatch},
    {"PNP0C00", &AcpiRawDeviceIrpDispatch},
    {"PNP0C01", &AcpiRawDeviceIrpDispatch},
    {"PNP0C02", &AcpiRawDeviceIrpDispatch},
    {"PNP0C04", &AcpiRawDeviceIrpDispatch},
    {"PNP0C05", &AcpiRawDeviceIrpDispatch},
    {"PNP0C0B", &AcpiFanIrpDispatch},
    {"PNP0C0C", &AcpiPowerButtonIrpDispatch},
    {"PNP0C0D", &AcpiLidIrpDispatch},
    {"PNP0C0E", &AcpiSleepButtonIrpDispatch},
    {"SNY5001", &AcpiBusFilterIrpDispatchSucceedCreate},
    {"IBM0062", &AcpiBusFilterIrpDispatchSucceedCreate},
    {"DockDevice", &AcpiDockPdoIrpDispatch},
    {"ThermalZone", &AcpiThermalZoneIrpDispatch},
    {"Processor", &AcpiProcessorIrpDispatch},
    {NULL, NULL}
};

SYSTEM_POWER_STATE SystemPowerStateTranslation[6] =
{
    1, 2, 3, 4, 5, 6
};

DEVICE_POWER_STATE DevicePowerStateTranslation[4] =
{
    1, 2, 3, 4
};

PCHAR StateName[] =
{
    "\\_S1",
    "\\_S2",
    "\\_S3",
    "\\_S4",
    "\\_S5"
};

extern NPAGED_LOOKASIDE_LIST BuildRequestLookAsideList;
extern NPAGED_LOOKASIDE_LIST RequestLookAsideList;
extern KSPIN_LOCK AcpiDeviceTreeLock;
extern KSPIN_LOCK AcpiBuildQueueLock;
extern KSPIN_LOCK AcpiPowerQueueLock;
extern KSPIN_LOCK AcpiGetLock;
extern LIST_ENTRY AcpiBuildDeviceList;
extern LIST_ENTRY AcpiBuildSynchronizationList;
extern LIST_ENTRY AcpiBuildQueueList;
extern LIST_ENTRY AcpiBuildRunMethodList;
extern LIST_ENTRY AcpiBuildOperationRegionList;
extern LIST_ENTRY AcpiBuildPowerResourceList;
extern LIST_ENTRY AcpiBuildThermalZoneList;
extern LIST_ENTRY AcpiPowerDelayedQueueList;
extern LIST_ENTRY AcpiGetListEntry;
extern LIST_ENTRY AcpiUnresolvedEjectList;
extern LIST_ENTRY AcpiPowerSynchronizeList;
extern LIST_ENTRY AcpiPowerQueueList;
extern KDPC AcpiBuildDpc;
extern KDPC AcpiPowerDpc;
extern BOOLEAN AcpiBuildDpcRunning;
extern BOOLEAN AcpiBuildWorkDone;
extern BOOLEAN AcpiPowerWorkDone;
extern BOOLEAN AcpiPowerDpcRunning;
extern PRSDTINFORMATION RsdtInformation;
extern PDEVICE_EXTENSION RootDeviceExtension;
extern ULONG AcpiOverrideAttributes;
extern KSPIN_LOCK AcpiPowerLock;
extern PUCHAR GpeEnable;
extern PUCHAR GpeWakeHandler;
extern PUCHAR GpeSpecialHandler;
extern ARBITER_INSTANCE AcpiArbiter;

/* FUNCTIOS *****************************************************************/

VOID
NTAPI
ACPIInternalMoveList(
    _In_ PLIST_ENTRY List1,
    _In_ PLIST_ENTRY List2)
{
    PLIST_ENTRY Flink1;
    PLIST_ENTRY Blink1;
    PLIST_ENTRY Blink2;

    Flink1 = List1->Flink;

    if (!IsListEmpty(List1))
    {
        Blink1 = List1->Blink;
        Blink2 = List2->Blink;

        Blink1->Flink = List2;
        List2->Blink = Blink1;
        Flink1->Blink = Blink2;
        Blink2->Flink = Flink1;

        InitializeListHead(List1);
    }
}

PDEVICE_EXTENSION
NTAPI
ACPIInternalGetDeviceExtension(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_EXTENSION DeviceExtension;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &OldIrql);

    DeviceExtension = DeviceObject->DeviceExtension;

    if (DeviceExtension && DeviceExtension->Signature != '_SGP')
    {
        DPRINT1("ACPIInternalGetDeviceExtension: FIXME\n");
        ASSERT(FALSE);
    }

    KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);

    return DeviceExtension;
}

NTSTATUS
NTAPI
ACPIBuildProcessGenericComplete(
    _In_ PACPI_BUILD_REQUEST Entry)
{
    PACPI_BUILD_REQUEST BuildRequest = Entry;
    PDEVICE_EXTENSION DeviceExtension;

    //DPRINT("ACPIBuildProcessGenericComplete: %p\n", BuildRequest);

    if (Entry->CallBack)
    {
        ((VOID (NTAPI *)(PDEVICE_EXTENSION, PVOID, NTSTATUS))Entry->CallBack)(Entry->DeviceExtension, Entry->CallBackContext, Entry->Status);
    }

    if (Entry->Flags & 8)
    {
        DeviceExtension = Entry->DeviceExtension;

        KeAcquireSpinLockAtDpcLevel(&AcpiDeviceTreeLock);
        InterlockedDecrement(&DeviceExtension->ReferenceCount);
        KeReleaseSpinLockFromDpcLevel(&AcpiDeviceTreeLock);
    }

    KeAcquireSpinLockAtDpcLevel(&AcpiBuildQueueLock);
    AcpiBuildWorkDone = TRUE;
    RemoveEntryList(&BuildRequest->Link);
    KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);

    ExFreeToNPagedLookasideList(&BuildRequestLookAsideList, BuildRequest);

    return STATUS_SUCCESS;
}

VOID
NTAPI
ACPIBuildCompleteCommon(
    _In_ LONG* Destination,
    _In_ LONG ExChange)
{
    KIRQL OldIrql;

    DPRINT("ACPIBuildCompleteCommon: %X, %X\n", *Destination, ExChange);

    InterlockedCompareExchange(Destination, ExChange, 1);

    KeAcquireSpinLock(&AcpiBuildQueueLock, &OldIrql);

    AcpiBuildWorkDone = TRUE;

    if (!AcpiBuildDpcRunning)
    {
        DPRINT("ACPIBuildCompleteCommon: %X\n", *Destination);
        KeInsertQueueDpc(&AcpiBuildDpc, NULL, NULL);
    }

    KeReleaseSpinLock(&AcpiBuildQueueLock, OldIrql);
}

VOID
__cdecl
ACPIBuildCompleteMustSucceed(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ NTSTATUS Status,
    _In_ ULONG Unknown3,
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    LONG OldBuildReserved1;
    ULONG NameSeg;

    DPRINT("ACPIBuildCompleteMustSucceed: %p, %X\n", BuildRequest, Status);

    OldBuildReserved1 = BuildRequest->BuildReserved1;

    if (NT_SUCCESS(Status))
    {
        BuildRequest->BuildReserved1 = 2;
        ACPIBuildCompleteCommon(&BuildRequest->WorkDone, OldBuildReserved1);
        return;
    }

    BuildRequest->Status = Status;

    if (NsObject)
        NameSeg = NsObject->NameSeg;
    else
        NameSeg = 0;

    DPRINT1("ACPIBuildCompleteMustSucceed: KeBugCheckEx()\n");
    ASSERT(FALSE);

    KeBugCheckEx(0xA5, 3, (ULONG_PTR)NsObject, Status, NameSeg);
}

VOID
NTAPI 
ACPIInternalUpdateDeviceStatus(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ ULONG DeviceStatus)
{
    DEVICE_EXTENSION* Extension;
    ULONGLONG RetFlagValue;
    KIRQL OldIrql;

    //DPRINT("ACPIInternalUpdateDeviceStatus: %p, %X\n", DeviceExtension, DeviceStatus);

    ACPIInternalUpdateFlags(DeviceExtension, 0x0080000000000000, (DeviceStatus & 8));
    ACPIInternalUpdateFlags(DeviceExtension, 0x0000000020000000, (DeviceStatus & 4));
    ACPIInternalUpdateFlags(DeviceExtension, 0x0040000000000000, !(DeviceStatus & 2));

    RetFlagValue = ACPIInternalUpdateFlags(DeviceExtension, 2, (DeviceStatus & 1));

    if (RetFlagValue & 2)
        return;

    if (DeviceStatus & 1)
        return;

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &OldIrql);

    Extension = DeviceExtension->ParentExtension;
    if (Extension)
    {
        do
        {
            if (!(Extension->Flags & 8))
                break;

            Extension = Extension->ParentExtension;
        }
        while (Extension);

        if (Extension)
            IoInvalidateDeviceRelations(Extension->PhysicalDeviceObject, BusRelations);
    }

    KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);
}

NTSTATUS
NTAPI 
ACPIGetConvertToDevicePresence(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ ULONG GetFlags,
    _Out_ PVOID* OutDataBuff,
    _Out_ ULONG* OutDataLen)
{
    PAMLI_NAME_SPACE_OBJECT Child;
    ULONGLONG UFlags;
    ULONG DeviceStatus = 0xF;

    DPRINT("ACPIGetConvertToDevicePresence: %p\n", DeviceExtension);

    if (GetFlags & 0x08000000)
    {
        if (InStatus != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            if (NT_SUCCESS(InStatus))
            {
                if (AmliData->DataType != 1)
                {
                    DPRINT1("ACPIGetConvertToDevicePresence: KeBugCheckEx()\n");
                    ASSERT(FALSE);
                    KeBugCheckEx(0xA5, 8, (ULONG_PTR)DeviceExtension, 0, AmliData->DataType);
                }

                DeviceStatus = (ULONG)AmliData->DataValue;
            }
            else
            {
                DeviceStatus = 0;
            }
        }

        goto Finish;
    }

    if (DeviceExtension->Flags & 0x0200000000000000)            // Prop_Dock
        UFlags = (DeviceExtension->Flags & 0x0000000400000000); // Cap_Unattached_Dock
    else
        UFlags = (DeviceExtension->Flags & 0x0008000000000000); // Prop_No_Object

    if (UFlags == 0)
    {
        if (InStatus == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            if (DeviceExtension->Flags & 0x0000001000000000)    // Cap_Processor
            {
                DPRINT1("ACPIGetConvertToDevicePresence: KeBugCheckEx()\n");
                ASSERT(FALSE);
            }
        }
        else
        {
            if (NT_SUCCESS(InStatus))
            {
                if (AmliData->DataType != 1)
                {
                    Child = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'ATS_');
                    DPRINT1("ACPIGetConvertToDevicePresence: KeBugCheckEx()\n");
                    ASSERT(FALSE);
                    KeBugCheckEx(0xA5, 8, (ULONG_PTR)Child, AmliData->DataType, AmliData->DataType);
                }

                DeviceStatus = (ULONG)AmliData->DataValue;
            }
            else
            {
                DeviceStatus = 0;
            }
        }
    }

    if ((DeviceExtension->Flags & 1) && !(GetFlags & 0x1000)) // Type_Never_Present
        DeviceStatus &= ~1;

    if (DeviceExtension->Flags & 0x40000000) // Cap_Never_show_in_UI
        DeviceStatus &= ~4;

    ACPIInternalUpdateDeviceStatus(DeviceExtension, DeviceStatus);

Finish:

    *OutDataBuff = (PVOID)DeviceStatus;

    if (OutDataLen)
        *OutDataLen = 4;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIGetConvertToAddress(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ ULONG GetFlags,
    _Out_ PVOID* OutDataBuff,
    _Out_ ULONG* OutDataLen)
{
    PVOID DataBuff;

    DPRINT("ACPIGetConvertToAddress: %p\n", DeviceExtension);

    ASSERT(OutDataBuff != NULL);

    if (!(GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x2000000000000000))
    {
        DataBuff = DeviceExtension->DeviceID;
        goto Finish;
    }

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIGetConvertToAddress: InStatus %X\n", InStatus);
        return InStatus;
    }

    if (AmliData->DataType != 1)
    {
        DPRINT1("ACPIGetConvertToAddress: STATUS_ACPI_INVALID_DATA. DataType %X\n", AmliData->DataType);
        return STATUS_ACPI_INVALID_DATA;
    }

    DataBuff = AmliData->DataValue;

Finish:

    *OutDataBuff = DataBuff;

    if (OutDataLen)
        *OutDataLen = 4;

    return STATUS_SUCCESS;
}

VOID
__cdecl
ACPIGetWorkerForInteger(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ PVOID Context)
{
    PACPI_GET_CONTEXT AcpiGetContext = Context;
    PAMLI_FN_ASYNC_CALLBACK CallBack;
    ULONG Flags;
    KIRQL OldIrql;
    NTSTATUS Status = InStatus;

    DPRINT("ACPIGetWorkerForInteger: %p\n", AcpiGetContext);

    ASSERT(AcpiGetContext->OutDataBuff);

    if (!AcpiGetContext->OutDataBuff)
    {
        DPRINT("ACPIGetWorkerForInteger: FIXME\n");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Finish;
    }

    Flags = AcpiGetContext->Flags;

    if (Flags & 0x400)
    {
        Status = ACPIGetConvertToAddress(AcpiGetContext->DeviceExtension,
                                         InStatus,
                                         AmliData,
                                         Flags,
                                         AcpiGetContext->OutDataBuff,
                                         AcpiGetContext->OutDataLen);
        goto Finish;
    }

    if (Flags & 0x800)
    {
        Status = ACPIGetConvertToDevicePresence(AcpiGetContext->DeviceExtension,
                                                InStatus,
                                                AmliData,
                                                Flags,
                                                AcpiGetContext->OutDataBuff,
                                                AcpiGetContext->OutDataLen);
        goto Finish;
    }

    if (NT_SUCCESS(InStatus))
    {
        if ((Flags & 0x4000) && AmliData->DataType != 1)
        {
            DPRINT("ACPIGetWorkerForInteger: FIXME\n");
            ASSERT(FALSE);
            Status = STATUS_ACPI_INVALID_DATA;
        }
        else
        {
            *AcpiGetContext->OutDataBuff = AmliData->DataValue;

            if (AcpiGetContext->OutDataLen)
                *AcpiGetContext->OutDataLen = 4;

            Status = STATUS_SUCCESS;
        }
    }

Finish:

    AcpiGetContext->Status = Status;

    if (NT_SUCCESS(InStatus))
        AMLIFreeDataBuffs(AmliData, 1);

    if (AcpiGetContext->Flags & 0x20000000)
        return;

    CallBack = AcpiGetContext->CallBack;
    if (CallBack)
        CallBack(NsObject, Status, NULL, AcpiGetContext->CallBackContext);

    KeAcquireSpinLock(&AcpiGetLock, &OldIrql);
    RemoveEntryList(&AcpiGetContext->List);
    KeReleaseSpinLock(&AcpiGetLock, OldIrql);

    ExFreePool(AcpiGetContext);
}

VOID
NTAPI
ACPIAmliDoubleToName(
    _In_ PCHAR DataBuff,
    _In_ ULONG Index,
    _In_ BOOLEAN IsNameID)
{
    PCHAR Name = DataBuff;

    DPRINT("ACPIAmliDoubleToName: %X, %X, %X\n", DataBuff, Index, IsNameID);

    if (IsNameID)
        *DataBuff++ = '*';

    *DataBuff++ = (((Index >> 2) & 0x1F) + 0x40);
    *DataBuff++ = (((Index >> 0xD) & 7) + (8 * ((Index & 3) + 8)));
    *DataBuff = (((Index & 0x1F00) >> 8) + 0x40);

    sprintf((DataBuff + 1), "%02X%02X", (int)((Index & 0x00FF0000) >> 16), (int)(Index >> 24));

    DPRINT("ACPIAmliDoubleToName: '%s'\n", Name);
}

VOID
NTAPI
ACPIAmliDoubleToNameWide(
    _In_ PWCHAR DataBuff,
    _In_ ULONG Index,
    _In_ BOOLEAN IsNameID)
{
    PWCHAR Name = DataBuff;

    DPRINT("ACPIAmliDoubleToNameWide: %X, %X, %X\n", DataBuff, Index, IsNameID);

    if (IsNameID)
        *DataBuff++ = '*';

    *DataBuff++ = (((Index >> 2) & 0x1F) + 0x40);
    *DataBuff++ = (((Index >> 0xD) & 7) + (8 * ((Index & 3) + 8)));
    *DataBuff = (((Index & 0x1F00) >> 8) + 0x40);

    swprintf((DataBuff + 1), L"%02X%02X", (int)((Index & 0x00FF0000) >> 16), (int)(Index >> 24));

    DPRINT("ACPIAmliDoubleToNameWide: '%S'\n", Name);
}

NTSTATUS
NTAPI
ACPIGetConvertToPnpID(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ ULONG GetFlags,
    _Out_ PVOID* OutDataBuff,
    _Out_ ULONG* OutDataLen)
{
    PCHAR DataBuff;
    PCHAR IdString;
    POOL_TYPE PoolType;
    ULONG DataLen;

    DPRINT("ACPIGetConvertToPnpID: GetFlags %X\n", GetFlags);

    if (GetFlags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    if (!(GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x0000800000000000))
    {
        DataLen = (strlen(DeviceExtension->DeviceID) - 3);

        DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
        if (!DataBuff)
        {
            DPRINT1("ACPIGetConvertToPnpID: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(DataBuff, DataLen);

        sprintf(DataBuff, "*%s", (DeviceExtension->Address + 5));
        goto Exit;
    }

    if (!(GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x0000004000000000))
    {
        DataLen = 0xE;

        DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
        if (!DataBuff)
        {
            DPRINT1("ACPIGetConvertToPnpID: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(DataBuff, DataLen);

        sprintf(DataBuff, "*%s", "PciBarTarget");
        goto Exit;
    }

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIGetConvertToPnpID: InStatus %X\n", InStatus);
        return InStatus;
    }

    if (AmliData->DataType == 1)
    {
        DataLen = 9;

        DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
        if (!DataBuff)
        {
            DPRINT1("ACPIGetConvertToPnpID: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(DataBuff, DataLen);

        ACPIAmliDoubleToName(DataBuff, (ULONG)AmliData->DataValue, TRUE);
        goto Exit;
    }
    else if (AmliData->DataType == 2)
    {
        IdString = AmliData->DataBuff;

        if (*IdString == '*')
            IdString++;

        DataLen = (strlen(IdString) + 2);

        DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
        if (!DataBuff)
        {
            DPRINT1("ACPIGetConvertToPnpID: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(DataBuff, DataLen);

        sprintf(DataBuff, "*%s", IdString);
        goto Exit;
    }
    else
    {
        DPRINT1("ACPIGetConvertToPnpID: AmliData->DataType %X\n", AmliData->DataType);
        return STATUS_ACPI_INVALID_DATA;
    }

Exit:

    *OutDataBuff = DataBuff;

    if (OutDataLen)
        *OutDataLen = DataLen;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIGetConvertToInstanceID(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ ULONG GetFlags,
    _Out_ PVOID* OutDataBuff,
    _Out_ ULONG* OutDataLen)
{
    PVOID DataBuff;
    POOL_TYPE PoolType;
    ULONG DataLen;

    DPRINT("ACPIGetConvertToInstanceID: GetFlags %X\n", GetFlags);

    if (GetFlags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    if (!(GetFlags & 0x8000000) && (DeviceExtension->Flags & 0x0001000000000000))
    {
        DataLen = (strlen(DeviceExtension->InstanceID) + 1);

        DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
        if (!DataBuff)
        {
            DPRINT1("ACPIGetConvertToInstanceID: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        //RtlZeroMemory(DataBuff, DataLen);
        RtlCopyMemory(DataBuff, AmliData->DataBuff, DataLen);

        goto Finish;
    }

    if (!(GetFlags & 0x8000000) && (DeviceExtension->Flags & 0x0000004000000000))
    {
        DataLen = 9;

        DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
        if (!DataBuff)
        {
            DPRINT1("ACPIGetConvertToInstanceID: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(DataBuff, DataLen);
        sprintf(DataBuff, "%lx", (ULONG)DeviceExtension->Address);

        goto Finish;
    }

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIGetConvertToInstanceID: InStatus %X\n", InStatus);
        return InStatus;
    }

    if (AmliData->DataType == 1)
    {
        DataLen = 9;

        DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
        if (!DataBuff)
        {
            DPRINT1("ACPIGetConvertToInstanceID: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(DataBuff, DataLen);
        sprintf(DataBuff, "%lx", (ULONG)AmliData->DataValue);

        goto Finish;
    }

    if (AmliData->DataType != 2)
    {
        DPRINT1("ACPIGetConvertToInstanceID: STATUS_ACPI_INVALID_DATA\n");
        return STATUS_ACPI_INVALID_DATA;
    }

    DataLen = (strlen(AmliData->DataBuff) + 1);

    DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
    if (!DataBuff)
    {
        DPRINT1("ACPIGetConvertToInstanceID: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //RtlZeroMemory(DataBuff, DataLen);
    RtlCopyMemory(DataBuff, AmliData->DataBuff, DataLen);

Finish:

    *OutDataBuff = DataBuff;

    if (OutDataLen)
        *OutDataLen = DataLen;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIGetProcessorID(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_  PAMLI_OBJECT_DATA AmliData,
    _In_  ULONG GetFlags,
    _In_  PVOID* OutDataBuff,
    _In_  ULONG* OutDataLen)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIGetConvertToDeviceID(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_  PAMLI_OBJECT_DATA AmliData,
    _In_  ULONG GetFlags,
    _In_  PVOID* OutDataBuff,
    _In_  ULONG* OutDataLen)
{
    PCHAR deviceID;
    PCHAR IdString;
    POOL_TYPE PoolType;
    ULONG DataLen;

    DPRINT("ACPIGetConvertToDeviceID: GetFlags %X\n", GetFlags);

    if (!(GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x0000001000000000))
        return ACPIGetProcessorID(DeviceExtension, InStatus, AmliData, GetFlags, OutDataBuff, OutDataLen);

    if (GetFlags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    if (!(GetFlags & 0x08000000))
    {
        if (DeviceExtension->Flags & 0x0000800000000000)
        {
            DataLen = strlen(DeviceExtension->DeviceID);
            DataLen++;

            deviceID = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
            if (!deviceID)
            {
                DPRINT1("ACPIGetConvertToDeviceID: STATUS_INSUFFICIENT_RESOURCES\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            //RtlZeroMemory(deviceID, DataLen);

            RtlCopyMemory(deviceID, DeviceExtension->DeviceID, DataLen);

            goto Finish;
        }

        if (DeviceExtension->Flags & 0x0000004000000000)
        {
            DataLen = 0x12;

            deviceID = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
            if (!deviceID)
            {
                DPRINT1("ACPIGetConvertToDeviceID: STATUS_INSUFFICIENT_RESOURCES\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            RtlZeroMemory(deviceID, DataLen);

            strncpy(deviceID, "ACPI\\PciBarTarget", (DataLen - 1));
            goto Finish;
        }
    }

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIGetConvertToDeviceID: InStatus %X\n", InStatus);
        return InStatus;
    }

    if (AmliData->DataType == 1)
    {
        DataLen = 0xD;

        deviceID = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
        if (!deviceID)
        {
            DPRINT1("ACPIGetConvertToDeviceID: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(deviceID, DataLen);

        sprintf(deviceID, "ACPI\\");
        ACPIAmliDoubleToName((deviceID + 5), (ULONG)AmliData->DataValue, FALSE);

        goto Finish;
    }

    if (AmliData->DataType != 2)
    {
        DPRINT1("ACPIGetConvertToDeviceID: STATUS_ACPI_INVALID_DATA\n");
        return STATUS_ACPI_INVALID_DATA;
    }

    IdString = AmliData->DataBuff;
    if (*IdString == '*')
        IdString++;

    DataLen = (strlen(IdString) + 6);

    deviceID = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
    if (!deviceID)
    {
        DPRINT1("ACPIGetConvertToDeviceID: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(deviceID, DataLen);

    sprintf(deviceID, "ACPI\\%s", IdString);

Finish:

    *OutDataBuff = deviceID;

    if (OutDataLen)
        *OutDataLen = DataLen;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIGetConvertToString(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_  PAMLI_OBJECT_DATA AmliData,
    _In_  ULONG GetFlags,
    _In_  PVOID* OutDataBuff,
    _In_  ULONG* OutDataLen)
{
    PVOID DataBuff;
    POOL_TYPE PoolType;
    ULONG DataLen;
    NTSTATUS Status = InStatus;

    if (GetFlags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIGetConvertToString: Status %X\n", Status);
        return Status;
    }

    if (AmliData->DataType != 2)
    {
        DPRINT1("ACPIGetConvertToString: STATUS_INSUFFICIENT_RESOURCES\n");
        ASSERT(FALSE);
        return STATUS_ACPI_INVALID_DATA;
    }

    DataLen = (strlen(AmliData->DataBuff) + 1);

    DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
    if (!DataBuff)
    {
        DPRINT1("ACPIGetConvertToString: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    //RtlZeroMemory(DataBuff, DataLen);

    RtlCopyMemory(DataBuff, AmliData->DataBuff, DataLen);

    *OutDataBuff = DataBuff;

    if (OutDataLen)
        *OutDataLen = DataLen;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIGetConvertToCompatibleID(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_  PAMLI_OBJECT_DATA AmliData,
    _In_  ULONG GetFlags,
    _In_  PVOID* OutDataBuff,
    _In_  ULONG* OutDataLen)
{
    PAMLI_PACKAGE_OBJECT PackageObject;
    PCHAR* buffer1;
    PULONG buffer2;
    PCHAR buffer;
    PVOID DataBuff;
    POOL_TYPE PoolType;
    ULONG DataLen;
    ULONG Count;
    ULONG IdSize;
    ULONG ix = 0;
    ULONG NumberOfBytes = 0;
    NTSTATUS Status = InStatus;

    if (GetFlags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    if ((GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x8000000000000000))
    {
        IdSize = (strlen(DeviceExtension->Processor.CompatibleID) + 2);

        DataBuff = ExAllocatePoolWithTag(PoolType, IdSize, 'SpcA');
        if (!DataBuff)
        {
            DPRINT1("ACPIGetConvertToCompatibleID: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        //RtlZeroMemory(DataBuff, IdSize);

        RtlCopyMemory(DataBuff, DeviceExtension->Processor.CompatibleID, IdSize);

        *OutDataBuff = DataBuff;

        if (OutDataLen)
            *OutDataLen = 0;

        return STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIGetConvertToCompatibleID: Status %X\n", Status);
        return InStatus;
    }

    if (AmliData->DataType == 1 || AmliData->DataType == 2)
    {
        Count = 1;
    }
    else if (AmliData->DataType == 4)
    {
        PackageObject = AmliData->DataBuff;
        Count = PackageObject->Elements;
    }
    else
    {
        DPRINT1("ACPIGetConvertToCompatibleID: STATUS_ACPI_INVALID_DATA\n");
        ASSERT(FALSE);
        return STATUS_ACPI_INVALID_DATA;
    }

    buffer1 = ExAllocatePoolWithTag(NonPagedPool, (Count * 4), 'MpcA');
    if (!buffer1)
    {
        DPRINT1("ACPIGetConvertToCompatibleID: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(buffer1, (Count * 4));

    buffer2 = ExAllocatePoolWithTag(NonPagedPool, (Count * 4), 'MpcA');
    if (!buffer2)
    {
        DPRINT1("ACPIGetConvertToCompatibleID: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(buffer1, 'MpcA');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(buffer2, (Count * 4));

    if (AmliData->DataType == 1)
    {
        Status = ACPIGetConvertToPnpID(DeviceExtension, InStatus, AmliData, GetFlags, (PVOID *)buffer1, buffer2);
        NumberOfBytes = *buffer2;
    }
    else if (AmliData->DataType == 2)
    {
        Status = ACPIGetConvertToString(DeviceExtension, InStatus, AmliData, GetFlags, (PVOID *)buffer1, buffer2);
        NumberOfBytes = *buffer2;
    }
    else if (AmliData->DataType == 4)
    {
        ix = 0;

        if (Count)
        {
            DPRINT1("ACPIGetConvertToCompatibleID: FIXME\n");
            ASSERT(FALSE);
        }
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIGetConvertToCompatibleID: Status %X\n", Status);
        Count = ix;
        goto Exit;
    }

    if (NumberOfBytes <= 1)
    {
        DPRINT1("ACPIGetConvertToCompatibleID: STATUS_ACPI_INVALID_DATA\n");
        ASSERT(FALSE);
        Status = STATUS_ACPI_INVALID_DATA;
        goto Exit;
    }

    DataLen = (NumberOfBytes + 1);

    DataBuff = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
    if (!DataBuff)
    {
        DPRINT1("ACPIGetConvertToCompatibleID: STATUS_INSUFFICIENT_RESOURCES\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    RtlZeroMemory(DataBuff, DataLen);

    buffer = DataBuff;
    for (ix = 0; ix < Count; ix++)
    {
        if (buffer1[ix])
            RtlCopyMemory(buffer, buffer1[ix], buffer2[ix]);

        buffer += buffer2[ix];
    }

    *OutDataBuff = DataBuff;

    if (OutDataLen)
        *OutDataLen = DataLen;

Exit:

    for (ix = 0; ix < Count; ix++)
    {
        if (buffer1[ix])
            ExFreePool(buffer1[ix]);
    }

    ExFreePoolWithTag(buffer2, 'MpcA');
    ExFreePoolWithTag(buffer1, 'MpcA');

    return Status;
}

NTSTATUS
NTAPI
ACPIGetConvertToDeviceIDWide(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ ULONG GetFlags,
    _Out_ PVOID* OutDataBuff,
    _Out_ ULONG* OutDataLen)
{
    PCHAR String;
    PWSTR DataBuff;
    POOL_TYPE PoolType;
    ULONG DataLen;

    DPRINT("ACPIGetConvertToDeviceIDWide: %p\n", DeviceExtension);

    if (!(GetFlags & 0x8000000) && (DeviceExtension->Flags & 0x0000001000000000))
    {
        DPRINT1("ACPIGetConvertToDeviceIDWide: FIXME\n");
        ASSERT(FALSE);
        return STATUS_NOT_IMPLEMENTED;
    }

    if (GetFlags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    if (!(GetFlags & 0x08000000))
    {
        if (DeviceExtension->Flags & 0x0000800000000000)
        {
            DataLen = (strlen(DeviceExtension->DeviceID) + 1);

            DataBuff = ExAllocatePoolWithTag(PoolType, (DataLen * 2), 'SpcA');
            if (!DataBuff)
            {
                DPRINT1("ACPIGetConvertToDeviceIDWide: STATUS_INSUFFICIENT_RESOURCES\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            RtlZeroMemory(DataBuff, (DataLen * 2));

            swprintf(DataBuff, L"%S", DeviceExtension->DeviceID);
            goto Finish;
        }

        if (DeviceExtension->Flags & 0x0000004000000000)
        {
            DataLen = 0x12;

            DataBuff = ExAllocatePoolWithTag(PoolType, 0x24, 'SpcA');
            if (!DataBuff)
            {
                DPRINT1("ACPIGetConvertToDeviceIDWide: STATUS_INSUFFICIENT_RESOURCES\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            RtlZeroMemory(DataBuff, 0x24);

            swprintf(DataBuff, L"%S", "ACPI\\PciBarTarget");
            goto Finish;
        }
    }

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIGetConvertToDeviceIDWide: InStatus %X\n", InStatus);
        return InStatus;
    }

    if (AmliData->DataType == 1)
    {
        DataLen = 0xD;

        DataBuff = ExAllocatePoolWithTag(PoolType, 0x1A, 'SpcA');
        if (!DataBuff)
        {
            DPRINT1("ACPIGetConvertToDeviceIDWide: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(DataBuff, 0x1A);

        swprintf(DataBuff, L"ACPI\\");

        ACPIAmliDoubleToNameWide((DataBuff + 5), (ULONG)AmliData->DataValue, FALSE);
        goto Finish;
    }

    if (AmliData->DataType != 2)
    {
        DPRINT1("ACPIGetConvertToDeviceIDWide: FIXME\n");
        ASSERT(FALSE);
        return STATUS_ACPI_INVALID_DATA;
    }

    String = AmliData->DataBuff;
    if (*String == '*')
        String++;

    DataLen = (strlen(String) + 6);

    DataBuff = ExAllocatePoolWithTag(PoolType, (DataLen * 2), 'SpcA');
    if (!DataBuff)
    {
        DPRINT1("ACPIGetConvertToDeviceIDWide: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(DataBuff, (DataLen * 2));

    swprintf(DataBuff, L"ACPI\\%S", String);

Finish:

    *OutDataBuff = DataBuff;

    if (OutDataLen)
        *OutDataLen = (DataLen * 2);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIGetConvertToInstanceIDWide(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ ULONG GetFlags,
    _Out_ PVOID* OutDataBuff,
    _Out_ ULONG* OutDataLen)
{
    PWCHAR String;
    POOL_TYPE PoolType;
    ULONG DataLen;

    DPRINT("ACPIGetConvertToInstanceIDWide: %p\n", DeviceExtension);

    if (GetFlags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    if (!(GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x0001000000000000))
    {
        DataLen = (strlen(DeviceExtension->InstanceID) + 1);

        String = ExAllocatePoolWithTag(PoolType, (DataLen * 2), 'SpcA');
        if (!String)
        {
            DPRINT1("ACPIGetConvertToInstanceIDWide: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(String, (DataLen * 2));

        swprintf(String, L"%S", DeviceExtension->InstanceID);
        goto Exit;
    }

    if (!(GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x0000004000000000))
    {
        DataLen = 9;

        String = ExAllocatePoolWithTag(PoolType, (DataLen * 2), 'SpcA');
        if (!String)
        {
            DPRINT1("ACPIGetConvertToInstanceIDWide: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(String, (DataLen * 2));

        swprintf(String, L"%lx", AmliData->DataValue);
        goto Exit;
    }

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT("ACPIGetConvertToInstanceIDWide: InStatus %X\n", InStatus);
        return InStatus;
    }

    if (AmliData->DataType == 1)
    {
        DataLen = 9;

        String = ExAllocatePoolWithTag(PoolType, (DataLen * 2), 'SpcA');
        if (!String)
        {
            DPRINT1("ACPIGetConvertToInstanceIDWide: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(String, (DataLen * 2));

        swprintf(String, L"%lx", AmliData->DataValue);
        goto Exit;
    }

    if (AmliData->DataType != 2)
    {
        DPRINT1("ACPIGetConvertToInstanceIDWide: FIXME\n");
        ASSERT(FALSE);
        return STATUS_ACPI_INVALID_DATA;
    }

    DataLen = (strlen(AmliData->DataBuff) + 1);

    String = ExAllocatePoolWithTag(PoolType, (DataLen * 2), 'SpcA');
    if (!String)
    {
        DPRINT1("ACPIGetConvertToInstanceIDWide: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(String, (DataLen * 2));

    swprintf(String, L"%S", AmliData->DataBuff);
    goto Exit;

Exit:

    *OutDataBuff = String;

    if (OutDataLen)
        *OutDataLen = (DataLen * 2);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIGetConvertToHardwareIDWide(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ ULONG GetFlags,
    _Out_ PVOID* OutDataBuff,
    _Out_ ULONG* OutDataLen)
{
    PCHAR IdString;
    PWCHAR HardwareID;
    POOL_TYPE PoolType;
    ULONG DataBuffLen;
    ULONG DataLen;
    BOOLEAN IsAllocated = FALSE;

    DPRINT("ACPIGetConvertToHardwareIDWide: %p\n", DeviceExtension);

    if (!(GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x0000001000000000))
    {
        DPRINT1("ACPIGetConvertToHardwareIDWide: FIXME\n");
        ASSERT(FALSE);
        goto Exit;
    }

    if (GetFlags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    if (!(GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x0000800000000000))
    {
        DataBuffLen = (strlen(DeviceExtension->DeviceID) - 4);

        IdString = ExAllocatePoolWithTag(PoolType, DataBuffLen, 'SpcA');
        if (!IdString)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(IdString, DataBuffLen);

        IsAllocated = TRUE;

        strncpy(IdString, (DeviceExtension->DeviceID + 5), (DataBuffLen - 1));
        goto Finish;
    }

    if (!(GetFlags & 0x08000000) && DeviceExtension->Flags & 0x0000004000000000)
    {
        DataBuffLen = 0xD;

        IdString = ExAllocatePoolWithTag(PoolType, DataBuffLen, 'SpcA');
        if (!IdString)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(IdString, DataBuffLen);

        IsAllocated = TRUE;

        strncpy(IdString, "PciBarTarget", (DataBuffLen - 1));
        goto Finish;
    }

    if (!NT_SUCCESS(InStatus))
    {
        return InStatus;
    }

    if (AmliData->DataType == 1)
    {
        DataBuffLen = 0x8;

        IdString = ExAllocatePoolWithTag(PoolType, DataBuffLen, 'SpcA');
        if (!IdString)
        {
            if (IsAllocated)
                ExFreePool(IdString);

            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(IdString, DataBuffLen);
        IsAllocated = TRUE;

        ACPIAmliDoubleToName(IdString, (ULONG)AmliData->DataValue, FALSE);
        goto Finish;
    }

    if (AmliData->DataType != 2)
    {
        ASSERT(FALSE);
        return STATUS_ACPI_INVALID_DATA;
    }

    IdString = AmliData->DataBuff;

    if (*IdString == '*')
        IdString++;

    DataBuffLen = (strlen(IdString) + 1);

Finish:

    DataLen = (7 + (DataBuffLen * 2));

    HardwareID = ExAllocatePoolWithTag(PoolType, (DataLen * 2), 'SpcA');
    if (!HardwareID)
    {
        if (IsAllocated)
            ExFreePool(IdString);

        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(HardwareID, (DataLen * 2));

    swprintf(HardwareID, L"ACPI\\%S", IdString);
    swprintf(&HardwareID[DataBuffLen + 5], L"*%S", IdString);

Exit:

    *(OutDataBuff) = HardwareID;

    if (OutDataLen)
        *(OutDataLen) = (DataLen * 2);

    if (IsAllocated)
        ExFreePool(IdString);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIGetConvertToCompatibleIDWide(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ ULONG GetFlags,
    _Out_ PVOID* OutDataBuff,
    _Out_ ULONG* OutDataLen)
{
    PVOID CompatibleId;
    POOL_TYPE PoolType;
    ULONG DataLen;
    NTSTATUS Status = InStatus;

    DPRINT("ACPIGetConvertToCompatibleIDWide: %p\n", DeviceExtension);

    if (GetFlags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    if (!(GetFlags & 0x08000000) && (DeviceExtension->Flags & 0x8000000000000000))
    {
        DataLen = ((strlen(DeviceExtension->Processor.CompatibleID) + 2) * 2);

        CompatibleId = ExAllocatePoolWithTag(PoolType, DataLen, 'SpcA');
        if (!CompatibleId)
        {
            DPRINT1("ACPIGetConvertToCompatibleIDWide: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(CompatibleId, DataLen);

        swprintf(CompatibleId, L"%S", DeviceExtension->Button.SpinLock);

        *OutDataBuff = CompatibleId;

        if (OutDataLen)
            *OutDataLen = 0;

        return STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIGetConvertToCompatibleIDWide: InStatus %X\n", InStatus);
        return Status;
    }

    DPRINT1("ACPIGetConvertToCompatibleIDWide: FIXME\n");
    ASSERT(FALSE);

    return Status;
}

VOID
__cdecl
ACPIGetWorkerForString(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ PVOID Context)
{
    PACPI_GET_CONTEXT AcpiGetContext = Context;
    PDEVICE_EXTENSION DeviceExtension;
    PAMLI_FN_ASYNC_CALLBACK CallBack;
    PVOID* OutDataBuff;
    PULONG OutDataLen;
    ULONG GetFlags;
    KIRQL Irql;
    BOOLEAN IsSuccess = FALSE;
    NTSTATUS Status;

    DPRINT("ACPIGetWorkerForString: %p\n", AcpiGetContext, AcpiGetContext->Flags);

    if (NT_SUCCESS(InStatus))
        IsSuccess = TRUE;

    ASSERT(AcpiGetContext->OutDataBuff != NULL);

    OutDataBuff = AcpiGetContext->OutDataBuff;
    if (!OutDataBuff)
    {
        DPRINT1("ACPIGetWorkerForString: STATUS_INSUFFICIENT_RESOURCES\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Finish;
    }

    if (AmliData->DataType == 2 && (!AmliData->DataBuff || !AmliData->DataLen))
    {
        DPRINT1("ACPIGetWorkerForString: STATUS_ACPI_INVALID_DATA\n");
        Status = STATUS_ACPI_INVALID_DATA;
        goto Finish;
    }

    OutDataLen = AcpiGetContext->OutDataLen;
    GetFlags = AcpiGetContext->Flags;
    DeviceExtension = AcpiGetContext->DeviceExtension;

    if (GetFlags & 0x10)
    {
        if (GetFlags & 0x20)
        {
            Status = ACPIGetConvertToDeviceIDWide(DeviceExtension, InStatus, AmliData, GetFlags, OutDataBuff, OutDataLen);
        }
        else if (GetFlags & 0x40)
        {
            Status = ACPIGetConvertToHardwareIDWide(DeviceExtension, InStatus, AmliData, GetFlags, OutDataBuff, OutDataLen);
        }
        else if (GetFlags & 0x80)
        {
            Status = ACPIGetConvertToInstanceIDWide(DeviceExtension, InStatus, AmliData, GetFlags, OutDataBuff, OutDataLen);
        }
        else if (GetFlags & 0x0200)
        {
            DPRINT1("ACPIGetWorkerForString: FIXME\n");
            ASSERT(FALSE);
        }
        else if (GetFlags & 0x0100)
        {
            Status = ACPIGetConvertToCompatibleIDWide(DeviceExtension, InStatus, AmliData, GetFlags, OutDataBuff, OutDataLen);
        }
        else if (GetFlags & 0x2000)
        {
            DPRINT1("ACPIGetWorkerForString: FIXME\n");
            ASSERT(FALSE);
        }
        else
        {
            DPRINT1("ACPIGetWorkerForString: FIXME\n");
            ASSERT(FALSE);
        }
    }
    else if (GetFlags & 0x20)
    {
        Status = ACPIGetConvertToDeviceID(DeviceExtension, InStatus, AmliData, GetFlags, OutDataBuff, OutDataLen);
    }
    else if (GetFlags & 0x40)
    {
        DPRINT1("ACPIGetWorkerForString: FIXME\n");
        ASSERT(FALSE);
    }
    else if (GetFlags & 0x80)
    {
        Status = ACPIGetConvertToInstanceID(DeviceExtension, InStatus, AmliData, GetFlags, OutDataBuff, OutDataLen);
    }
    else if (GetFlags & 0x0200)
    {
        Status = ACPIGetConvertToPnpID(DeviceExtension, InStatus, AmliData, GetFlags, OutDataBuff, OutDataLen);
    }
    else if (GetFlags & 0x0100)
    {
        Status = ACPIGetConvertToCompatibleID(DeviceExtension, InStatus, AmliData, GetFlags, OutDataBuff, OutDataLen);
    }
    else
    {
        DPRINT1("ACPIGetWorkerForString: FIXME\n");
        ASSERT(FALSE);
    }

Finish:

    AcpiGetContext->Status = Status;

    if (IsSuccess)
        AMLIFreeDataBuffs(AmliData, 1);

    if (GetFlags & 0x20000000)
        return;

    if (AcpiGetContext->CallBack)
    {
        CallBack = AcpiGetContext->CallBack;
        CallBack(NsObject, Status, NULL, AcpiGetContext->CallBackContext);
    }

    KeAcquireSpinLock(&AcpiGetLock, &Irql);
    RemoveEntryList(&AcpiGetContext->List);
    KeReleaseSpinLock(&AcpiGetLock, Irql);

    ExFreePool(AcpiGetContext);
}

VOID
__cdecl
ACPIGetWorkerForBuffer(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ PVOID Context)
{
    PACPI_GET_CONTEXT AcpiGetContext = Context;
    PVOID DataBuff;
    POOL_TYPE PoolType;
    KIRQL Irql;
    BOOLEAN IsSuccess = TRUE;

    DPRINT("ACPIGetWorkerForBuffer: %p, %X\n", AcpiGetContext, AcpiGetContext->Flags);

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIGetWorkerForBuffer: InStatus %X\n", InStatus);
        IsSuccess = FALSE;
        goto Exit;
    }

    if (AmliData->DataType != 3)
    {
        DPRINT1("ACPIGetWorkerForBuffer: AmliData %p, DataType %X\n", AmliData, AmliData->DataType);
        ASSERT(FALSE);

        if (AcpiGetContext->Flags & 0x80000000)
        {
            DPRINT1("ACPIGetWorkerForBuffer: FIXME\n");
            ASSERT(FALSE);
            //ACPIInternalError(..);
        }

        InStatus = STATUS_ACPI_INVALID_DATA;
        goto Exit;
    }

    if (!AmliData->DataLen)
    {
        DPRINT1("ACPIGetWorkerForBuffer: AmliData %p, DataType %X\n", AmliData, AmliData->DataType);
        ASSERT(FALSE);
        InStatus = STATUS_ACPI_INVALID_DATA;
        goto Exit;
    }

    if (AcpiGetContext->Flags & 0x10000000)
        PoolType = NonPagedPool;
    else
        PoolType = PagedPool;

    DataBuff = ExAllocatePoolWithTag(PoolType, AmliData->DataLen, 'BpcA');
    if (!DataBuff)
    {
        DPRINT1("ACPIGetWorkerForBuffer: InStatus %X\n", InStatus);
        InStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlCopyMemory(DataBuff, AmliData->DataBuff, AmliData->DataLen);

    if (AcpiGetContext->OutDataBuff)
    {
        *(AcpiGetContext->OutDataBuff) = DataBuff;

        if (AcpiGetContext->OutDataLen)
            *(AcpiGetContext->OutDataLen) = AmliData->DataLen;
    }

Exit:

    AcpiGetContext->Status = InStatus;

    if (IsSuccess)
        AMLIFreeDataBuffs(AmliData, 1);

    if (AcpiGetContext->Flags & 0x20000000)
        return;

    if (AcpiGetContext->CallBack)
    {
        DPRINT1("ACPIGetWorkerForBuffer: FIXME\n");
        ASSERT(FALSE);
    }

    KeAcquireSpinLock(&AcpiGetLock, &Irql);
    RemoveEntryList(&AcpiGetContext->List);
    KeReleaseSpinLock(&AcpiGetLock, Irql);

    ExFreePool(AcpiGetContext);
}

VOID
__cdecl
ACPIGetWorkerForData(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ PVOID Context)
{
    PACPI_GET_CONTEXT AcpiGetContext = Context;
    BOOLEAN IsFreeBuffs;
    KIRQL Irql;

    DPRINT("ACPIGetWorkerForData: %p, %X\n", AcpiGetContext, AcpiGetContext->Flags);

    if (NT_SUCCESS(InStatus))
        IsFreeBuffs = TRUE;
    else
        IsFreeBuffs = FALSE;

    ASSERT(AcpiGetContext->OutDataBuff);

    if (!AcpiGetContext->OutDataBuff)
    {
        DPRINT1("ACPIGetWorkerForData: STATUS_INSUFFICIENT_RESOURCES\n");
        InStatus = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (NT_SUCCESS(InStatus))
    {
        RtlCopyMemory(AcpiGetContext->OutDataBuff, AmliData, sizeof(AMLI_OBJECT_DATA));
        RtlZeroMemory(AmliData, sizeof(*AmliData));

        IsFreeBuffs = FALSE;
    }

    AcpiGetContext->Status = InStatus;

    if (IsFreeBuffs)
        AMLIFreeDataBuffs(AmliData, 1);

    if (AcpiGetContext->Flags & 0x20000000)
        return;

    if (AcpiGetContext->CallBack)
    {
        DPRINT1("ACPIGetWorkerForData: FIXME\n");
        ASSERT(FALSE);
    }

    KeAcquireSpinLock(&AcpiGetLock, &Irql);
    RemoveEntryList(&AcpiGetContext->List);
    KeReleaseSpinLock(&AcpiGetLock, Irql);

    ExFreePool(AcpiGetContext);
}

NTSTATUS
NTAPI
ACPIGet(
    _In_ PVOID Context,
    _In_ ULONG NameSeg,
    _In_ ULONG Flags,
    _In_ PVOID SimpleArgumentBuff,
    _In_ ULONG SimpleArgumentSize,
    _In_ PVOID CallBack,
    _In_ PVOID CallBackContext,
    _Out_ PVOID* OutDataBuff,
    _Out_ ULONG* OutDataLen)
{
    PDEVICE_EXTENSION DeviceExtension;
    PAMLI_NAME_SPACE_OBJECT NsObject;
    PACPI_GET_CONTEXT AcpiGetContext;
    PAMLI_OBJECT_DATA DataArgs = NULL;
    PAMLI_FN_ASYNC_CALLBACK Worker;
    AMLI_OBJECT_DATA Argument = {0};
    ULONG ArgsCount = 0;
    BOOLEAN IsAsyncEval;
    BOOLEAN IsFlag8000000;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("ACPIGet: %p, %X, %X, %X, %X, %X, %X\n", Context, NameSeg, Flags, SimpleArgumentBuff, SimpleArgumentSize, CallBack, CallBackContext);

    IsAsyncEval = ((Flags & 0x40000000) != 0);
    IsFlag8000000 = ((Flags & 0x8000000) != 0);

    if (!IsFlag8000000)
    {
        DeviceExtension = Context;
        NsObject = DeviceExtension->AcpiObject;
    }
    else
    {
        DeviceExtension = NULL;
        NsObject = Context;
    }

    if ((Flags & 0x1F0000) == 0x10000)
    {
        Worker = ACPIGetWorkerForBuffer;
    }
    else if ((Flags & 0x1F0000) == 0x20000)
    {
        Worker = ACPIGetWorkerForData;
    }
    else if ((Flags & 0x1F0000) == 0x40000)
    {
        Worker = ACPIGetWorkerForInteger;

        if ((Flags & 0x800) && !IsFlag8000000 && (DeviceExtension->Flags & 0x0200000000000000))
        {
            DPRINT1("ACPIGet: FIXME\n");
            ASSERT(FALSE);
        }
    }
    else if ((Flags & 0x1F0000) == 0x80000)
    {
        Worker = ACPIGetWorkerForString;
    }
    else if ((Flags & 0x1F0000) == 0x100000)
    {
        DPRINT1("ACPIGet: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        DPRINT1("ACPIGet: STATUS_INVALID_PARAMETER_3\n");
        return STATUS_INVALID_PARAMETER_3;
    }

    if (Flags & 0x7000000)
    {
        ASSERT(SimpleArgumentSize != 0);

        if (Flags & 0x1000000)
        {
            Argument.DataType = 1;
            Argument.DataValue = SimpleArgumentBuff;
        }
        else
        {
            if (Flags & 0x2000000)
            {
                Argument.DataType = 2;
            }
            else
            {
                if (!(Flags & 0x4000000))
                {
                    DPRINT1("ACPIGet: FIXME\n");
                    ASSERT(FALSE);
                }

                Argument.DataType = 3;
            }

            Argument.DataLen = SimpleArgumentSize;
            Argument.DataBuff = SimpleArgumentBuff;
        }

        ArgsCount = 1;
        DataArgs = &Argument;
    }

    AcpiGetContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*AcpiGetContext), 'MpcA');
    if (!AcpiGetContext)
    {
        DPRINT1("ACPIGet: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(AcpiGetContext, sizeof(*AcpiGetContext));

    AcpiGetContext->DeviceExtension = DeviceExtension;
    AcpiGetContext->NsObject = NsObject;
    AcpiGetContext->NameSeg = NameSeg;
    AcpiGetContext->Flags = Flags;
    AcpiGetContext->CallBack = CallBack;
    AcpiGetContext->CallBackContext = CallBackContext;
    AcpiGetContext->OutDataBuff = OutDataBuff;
    AcpiGetContext->OutDataLen = OutDataLen;

    KeAcquireSpinLock(&AcpiGetLock, &OldIrql);
    InsertTailList(&AcpiGetListEntry, &AcpiGetContext->List);
    KeReleaseSpinLock(&AcpiGetLock, OldIrql);

    if (!IsFlag8000000 &&
        (DeviceExtension->Flags & 0x0008000000000000) &&
        !(DeviceExtension->Flags & 0x0200000000000000))
    {
        DPRINT("ACPIGet: STATUS_OBJECT_NAME_NOT_FOUND\n");
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto Finish;
    }

    NsObject = ACPIAmliGetNamedChild(NsObject, NameSeg);
    if (!NsObject)
    {
        DPRINT("ACPIGet: STATUS_OBJECT_NAME_NOT_FOUND\n");
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto Finish;
    }

    if (IsAsyncEval)
    {
        Status = AMLIAsyncEvalObject(NsObject,
                                     &AcpiGetContext->DataResult,
                                     ArgsCount,
                                     DataArgs,
                                     Worker,
                                     AcpiGetContext);
        if (Status == STATUS_PENDING)
            return STATUS_PENDING;
    }
    else
    {
        Status = AMLIEvalNameSpaceObject(NsObject, &AcpiGetContext->DataResult, ArgsCount, DataArgs);
    }

Finish:

    AcpiGetContext->Flags |= 0x20000000;

    Worker(NsObject, Status, &AcpiGetContext->DataResult, AcpiGetContext);

    Status = AcpiGetContext->Status;

    KeAcquireSpinLock(&AcpiGetLock, &OldIrql);
    RemoveEntryList(&AcpiGetContext->List);
    KeReleaseSpinLock(&AcpiGetLock, OldIrql);

    ExFreePoolWithTag(AcpiGetContext, 'MpcA');

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessRunMethodPhaseCheckSta(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPIBuildProcessRunMethodPhaseCheckSta: %p\n", BuildRequest);

    DeviceExtension = BuildRequest->DeviceExtension;
    BuildRequest->BuildReserved1 = 4;

    if (DeviceExtension->Flags & 0x0008000000000000)
    {
        BuildRequest->BuildReserved1 = 0;
        ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);
        return Status;
    }

    if (!(BuildRequest->RunMethod.Flags & 1))
    {
        ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);
        return Status;
    }

    Status = ACPIGet(DeviceExtension,
                     'ATS_',
                     0x40040802,
                     NULL,
                     0,
                     ACPIBuildCompleteMustSucceed,
                     BuildRequest,
                     (PVOID *)&BuildRequest->ListHeadForInsert,
                     NULL);


    if (Status != STATUS_PENDING)
        ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);

    DPRINT("ACPIBuildProcessRunMethodPhaseCheckSta: ret Status %X\n", Status);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessRunMethodPhaseCheckBridge(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension = BuildRequest->DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPIBuildProcessRunMethodPhaseCheckBridge: %p\n", BuildRequest);

    if ((BuildRequest->RunMethod.Flags & 1) && (DeviceExtension->Flags & 2))
    {
        BuildRequest->BuildReserved1 = 0;
        ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);
        return Status;
    }

    BuildRequest->BuildReserved1 = 5;

    if (!(BuildRequest->RunMethod.Flags & 0x40))
    {
        ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);
        return Status;
    }

    BuildRequest->ListHeadForInsert = NULL;

    DPRINT("ACPIBuildProcessRunMethodPhaseCheckBridge: FIXME\n");
    ASSERT(FALSE);

    if (Status != STATUS_PENDING)
        ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);

    DPRINT("ACPIBuildProcessRunMethodPhaseCheckBridge: ret Status %X\n", Status);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessRunMethodPhaseRunMethod(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension = BuildRequest->DeviceExtension;
    PAMLI_NAME_SPACE_OBJECT NsObject = NULL;
    AMLI_OBJECT_DATA amliData[2];
    PAMLI_OBJECT_DATA AmliData = NULL;
    ULONG ArgsCount = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPIBuildProcessRunMethodPhaseRunMethod: %p\n", BuildRequest);

    if (!((BuildRequest->RunMethod.Flags & 0x40) == 0) && BuildRequest->ListHeadForInsert)
    {
        DPRINT("ACPIBuildProcessRunMethodPhaseRunMethod: Is PCI-PCI bridge\n");
        BuildRequest->BuildReserved1 = 0;
        goto Exit;
    }

    BuildRequest->BuildReserved1 = 6;

    NsObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, (ULONG)BuildRequest->RunMethod.Context);//?
    if (!NsObject)
    {
        goto Exit;
    }

    if (BuildRequest->RunMethod.Flags & 2)
    {
        if ((ACPIInternalUpdateFlags(DeviceExtension, 0x0020000000000000, FALSE) >> 0x20) & 0x200000)
            goto Exit;
    }

    else if (BuildRequest->RunMethod.Flags & 8)
    {
        if (!DeviceExtension->PowerInfo.WakeSupportCount)
            goto Exit;

        RtlZeroMemory(amliData, sizeof(AMLI_OBJECT_DATA));

        amliData[0].DataType = 1;
        amliData[0].DataValue = ULongToPtr(1);

        AmliData = amliData;
        ArgsCount = 1;
    }
    else if (BuildRequest->RunMethod.Flags & 0x30)
    {
        BuildRequest->RunMethod.Flags |= 0x40;

        RtlZeroMemory(amliData, sizeof(amliData));

        amliData[0].DataType = 1;
        amliData[0].DataValue = (PVOID)2;

        amliData[1].DataType = 1;
        amliData[1].DataValue = (((BuildRequest->RunMethod.Flags & 0x10) == 0x10) ? (PVOID)1 : (PVOID)0);

        AmliData = amliData;
        ArgsCount = 2;
    }

    BuildRequest->ChildObject = NsObject;

    Status = AMLIAsyncEvalObject(NsObject, NULL, ArgsCount, AmliData, (PVOID)ACPIBuildCompleteMustSucceed, BuildRequest);

Exit:

    if (Status == STATUS_PENDING)
        Status = STATUS_SUCCESS;
    else
        ACPIBuildCompleteMustSucceed(NsObject, Status, 0, BuildRequest);

    DPRINT("ACPIBuildProcessRunMethodPhaseRunMethod: retStatus %X\n", Status);

    return Status;
}

BOOLEAN
NTAPI
ACPIExtListIsFinished(
    _In_ PACPI_EXT_LIST_ENUM_DATA ExtList)
{
    BOOLEAN Result;

    if (Add2Ptr(ExtList->DeviceExtension, ExtList->Offset) == ExtList->List)
        Result = TRUE;
    else
        Result = FALSE;

    return Result;
}

PDEVICE_EXTENSION
__cdecl
ACPIExtListStartEnum(
    _In_ PACPI_EXT_LIST_ENUM_DATA ExtList)
{
    if (ExtList->ExtListEnum2)
        KeAcquireSpinLock(ExtList->SpinLock, &ExtList->Irql);

    ExtList->DeviceExtension = (PDEVICE_EXTENSION)((ULONG_PTR)ExtList->List->Flink - ExtList->Offset);

    if (ACPIExtListIsFinished(ExtList))
        return NULL ;

    return ExtList->DeviceExtension;
}

BOOLEAN
__cdecl
ACPIExtListTestElement(
    _In_ PACPI_EXT_LIST_ENUM_DATA ExtList,
    _In_ BOOLEAN IsParam2)
{
    BOOLEAN Result;

    if (ACPIExtListIsFinished(ExtList) || !IsParam2)
    {
        if (ExtList->ExtListEnum2)
            KeReleaseSpinLock(ExtList->SpinLock, ExtList->Irql);

        Result = FALSE;
    }
    else
    {
        if (ExtList->ExtListEnum2 == 1)
        {
            InterlockedIncrement(&ExtList->DeviceExtension->ReferenceCount);
            KeReleaseSpinLock(ExtList->SpinLock, ExtList->Irql);
        }

        Result = TRUE;
    }

    return Result;
}

VOID
NTAPI
ACPIInitDeleteDeviceExtension(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    UNIMPLEMENTED_DBGBREAK();
}

PDEVICE_EXTENSION
__cdecl
ACPIExtListEnumNext(
    _In_ PACPI_EXT_LIST_ENUM_DATA ExtList)
{
    PLIST_ENTRY List;
    LONG RefCount;
    KIRQL OldIrql;
    BOOLEAN Result;

    //DPRINT("ACPIExtListEnumNext: %p\n", ExtList);

    if (ExtList->ExtListEnum2 != 1)
    {
        List = Add2Ptr(ExtList->DeviceExtension, ExtList->Offset);
        ExtList->DeviceExtension = (PDEVICE_EXTENSION)((ULONG_PTR)List->Flink - ExtList->Offset);

        Result = ACPIExtListIsFinished(ExtList);

        return (Result ? NULL : ExtList->DeviceExtension);
    }

    KeAcquireSpinLock(ExtList->SpinLock, &OldIrql);
    ExtList->Irql = OldIrql;

    RefCount = InterlockedDecrement(&ExtList->DeviceExtension->ReferenceCount);

    ASSERT(!ACPIExtListIsFinished(ExtList));

    List = Add2Ptr(ExtList->DeviceExtension, ExtList->Offset);
    ExtList->DeviceExtension = (PDEVICE_EXTENSION)((ULONG_PTR)List->Flink - ExtList->Offset);

    if (!RefCount)
        ACPIInitDeleteDeviceExtension(ExtList->DeviceExtension);

    Result = ACPIExtListIsFinished(ExtList);

    return (Result ? NULL : ExtList->DeviceExtension);
}

NTSTATUS
NTAPI
ACPIBuildProcessRunMethodPhaseRecurse(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    ACPI_EXT_LIST_ENUM_DATA ExtList;
    BOOLEAN Result;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPIBuildProcessRunMethodPhaseRecurse: %p\n", BuildRequest);

    BuildRequest->BuildReserved1 = 0;

    if (!(BuildRequest->RunMethod.Flags & 4))
        goto Finish;

    ExtList.List = &BuildRequest->DeviceExtension->ChildDeviceList;
    ExtList.SpinLock = &AcpiDeviceTreeLock;
    ExtList.Offset = FIELD_OFFSET(DEVICE_EXTENSION, SiblingDeviceList);
    ExtList.ExtListEnum2 = 2;

    DeviceExtension = ACPIExtListStartEnum(&ExtList);

    for (Result = ACPIExtListTestElement(&ExtList, TRUE);
         Result;
         Result = ACPIExtListTestElement(&ExtList, NT_SUCCESS(Status)))
    {
        Status = ACPIBuildRunMethodRequest(DeviceExtension,
                                           NULL,
                                           NULL,
                                           BuildRequest->RunMethod.Context,
                                           BuildRequest->RunMethod.Flags,
                                           FALSE);

        DeviceExtension = ACPIExtListEnumNext(&ExtList);
    }

Finish:

    ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);

    DPRINT("ACPIBuildProcessRunMethodPhaseRecurse: ret Status %X\n", Status);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDeviceFailure(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhaseAdrOrHid(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension = BuildRequest->DeviceExtension;
    PAMLI_NAME_SPACE_OBJECT ChildObject;
    PAMLI_NAME_SPACE_OBJECT HidChild;
    PAMLI_NAME_SPACE_OBJECT AdrChild;
    PAMLI_NAME_SPACE_OBJECT UidChild;
    PVOID CallBack;
    PCHAR* IdString;
    ULONG NameSeg;
    ULONG Flags;
    NTSTATUS Status;

    DPRINT("ACPIBuildProcessDevicePhaseAdrOrHid: %p\n", BuildRequest);

    HidChild = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'DIH_');
    if (!HidChild)
    {
        ChildObject = AdrChild = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'RDA_');

        if (!AdrChild)
        {
            DPRINT("ACPIBuildProcessDevicePhaseAdrOrHid: KeBugCheckEx(..)\n");
            ASSERT(FALSE);
            KeBugCheckEx(0xA5, 0xD, (ULONG_PTR)DeviceExtension, 'RDA_', 0);
        }

        NameSeg = 'RDA_';
        Flags = 0x40040402;
        IdString = &DeviceExtension->DeviceID;
        CallBack = ACPIBuildCompleteMustSucceed;

        BuildRequest->BuildReserved1 = 4;
        BuildRequest->ChildObject = AdrChild;

        goto Finish;
    }

    ChildObject = UidChild = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'DIU_');
    if (UidChild)
    {
        NameSeg = 'DIU_';
        Flags = 0x50080086;
        IdString = &DeviceExtension->InstanceID;
        CallBack = ACPIBuildCompleteMustSucceed;

        BuildRequest->BuildReserved1 = 6;
        BuildRequest->ChildObject = UidChild;

        goto Finish;
    }

    NameSeg = 'DIH_';
    Flags = 0x50080026;
    IdString = &DeviceExtension->DeviceID;
    CallBack = ACPIBuildCompleteMustSucceed;

    BuildRequest->BuildReserved1 = 5;
    BuildRequest->ChildObject = HidChild;

Finish:

    Status = ACPIGet(DeviceExtension, NameSeg, Flags, NULL, 0, CallBack, BuildRequest, (PVOID *)IdString, NULL);

    if (Status == STATUS_PENDING)
        Status = STATUS_SUCCESS;
    else
        ACPIBuildCompleteMustSucceed(ChildObject, Status, 0, BuildRequest);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhaseAdr(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    DeviceExtension = BuildRequest->DeviceExtension;

    ACPIInternalUpdateFlags(BuildRequest->DeviceExtension, 0x0000100000000000, FALSE);
    BuildRequest->BuildReserved1 = 8;

    Status = ACPIGet(DeviceExtension,
                     'ATS_',
                     0x40040802,
                     NULL,
                     0,
                     ACPIBuildCompleteMustSucceed,
                     BuildRequest,
                     (PVOID *)&BuildRequest->ListHeadForInsert,
                     NULL);

    DPRINT("ACPIBuildProcessDevicePhaseAdr: Status %X\n", Status);

    if (Status == STATUS_PENDING)
        Status = STATUS_SUCCESS;
    else
        ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhaseHid(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PAMLI_NAME_SPACE_OBJECT NsObject = NULL;
    PDEVICE_EXTENSION DeviceExtension;
    ULONG Flags;
    ULONG NameSeg;
    ULONG ix;
    BOOLEAN IsMatch = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    DeviceExtension = BuildRequest->DeviceExtension;

    for (ix = 0; AcpiInternalDeviceFlagTable[ix].StringId; ix++)
    {
        if (strstr(DeviceExtension->DeviceID, AcpiInternalDeviceFlagTable[ix].StringId))
        {
            ACPIInternalUpdateFlags(DeviceExtension, AcpiInternalDeviceFlagTable[ix].Flags, FALSE);
            IsMatch = TRUE;
            break;
        }
    }

    ACPIInternalUpdateFlags(DeviceExtension, 0x0000200000000000, FALSE);

    NsObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'DIC_');
    if (!NsObject || IsMatch)
    {
        NameSeg = 'ATS_';
        BuildRequest->BuildReserved1 = 8;
        Flags = 0x40040802;
    }
    else
    {
        NameSeg = 'DIC_';
        BuildRequest->BuildReserved1 = 7;
        Flags = 0x50080107;
    }

    Status = ACPIGet(DeviceExtension, NameSeg, Flags, NULL, 0, ACPIBuildCompleteMustSucceed, BuildRequest, &BuildRequest->DataBuff, NULL);
    if (Status == STATUS_PENDING)
        return STATUS_SUCCESS;

    ACPIBuildCompleteMustSucceed(NsObject, Status, 0, BuildRequest);

    DPRINT("ACPIBuildProcessDevicePhaseHid: Status %X\n", Status);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhaseUid(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    PAMLI_NAME_SPACE_OBJECT NsChild;
    NTSTATUS Status;

    DeviceExtension = BuildRequest->DeviceExtension;

    ACPIInternalUpdateFlags(BuildRequest->DeviceExtension, 0x0000400000000000, FALSE);

    NsChild = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'DIH_');
    if (!NsChild)
    {
        DPRINT1("ACPIBuildProcessDevicePhaseUid: KeBugCheckEx()\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 0xD, (ULONG_PTR)DeviceExtension, 'DIH_', 0);
    }

    BuildRequest->BuildReserved1 = 5;

    Status = ACPIGet(DeviceExtension,
                     'DIH_',
                     0x50080026,
                     NULL,
                     0,
                     ACPIBuildCompleteMustSucceed,
                     BuildRequest,
                     (PVOID *)&DeviceExtension->DeviceID,
                     NULL);

    DPRINT("ACPIBuildProcessDevicePhaseUid: Status %X\n", Status);

    if (Status == STATUS_PENDING)
        Status = STATUS_SUCCESS;
    else
        ACPIBuildCompleteMustSucceed(NsChild, Status, 0, BuildRequest);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhaseCid(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    PCHAR Cid;
    ULONG ix;
    NTSTATUS Status = STATUS_SUCCESS;

    DeviceExtension = BuildRequest->DeviceExtension;

    for (Cid = BuildRequest->DataBuff; Cid && *Cid; )
    {
        Cid += strlen(Cid);

        if (!Cid[1])
            break;

        *Cid = ' ';
    }

    Cid = BuildRequest->DataBuff;

    for (ix = 0; AcpiInternalDeviceFlagTable[ix].StringId; ix++)
    {
        if (strstr(Cid, AcpiInternalDeviceFlagTable[ix].StringId))
        {
            ACPIInternalUpdateFlags(DeviceExtension, AcpiInternalDeviceFlagTable[ix].Flags, FALSE);
            break;
        }
    }

    if (Cid)
        ExFreePool(Cid);

    BuildRequest->BuildReserved1 = 8;

    Status = ACPIGet(DeviceExtension,
                     'ATS_',
                     0x40040802,
                     NULL,
                     0,
                     ACPIBuildCompleteMustSucceed,
                     BuildRequest,
                     &BuildRequest->DataBuff,
                     NULL);

    DPRINT("ACPIBuildProcessDevicePhaseCid: Status %X\n", Status);

    if (Status != STATUS_PENDING)
    {
        ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);
        return Status;
    }

    return STATUS_SUCCESS;
}

VOID
__cdecl
ACPIExtListExitEnumEarly(
    _In_ PACPI_EXT_LIST_ENUM_DATA ExtList)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ACPIDetectDuplicateHID(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    PDEVICE_EXTENSION Extension;
    ACPI_EXT_LIST_ENUM_DATA ExtList;

    DPRINT("ACPIDetectDuplicateHID: DeviceExtension %X\n", DeviceExtension);

    if (!DeviceExtension->ParentExtension)
        return;

    if ((DeviceExtension->Flags & 0x0000000000000001) ||
        (DeviceExtension->Flags & 0x0002000000000002) ||
        !(DeviceExtension->Flags & 0x0000A00000000000))
    {
        return;
    }

    ExtList.List = &DeviceExtension->ParentExtension->ChildDeviceList;
    ExtList.Offset = FIELD_OFFSET(DEVICE_EXTENSION, SiblingDeviceList);
    ExtList.SpinLock = &AcpiDeviceTreeLock;
    ExtList.ExtListEnum2 = 2;

    Extension = ACPIExtListStartEnum(&ExtList);

    while (ACPIExtListTestElement(&ExtList, TRUE))
    {
        if (!Extension)
        {
            ACPIExtListExitEnumEarly(&ExtList);
            return;
        }

        if (Extension == DeviceExtension)
            goto Next;

        if ((Extension->Flags & 0x0000000000000001) ||
            (Extension->Flags & 0x0002000000000002) ||
            (Extension->Flags & 0x0000080000000000) ||
            !(Extension->Flags & 0x0000A00000000000))
        {
            goto Next;
        }

        if (!strstr(Extension->DeviceID, DeviceExtension->DeviceID))
            goto Next;

        if (!(Extension->Flags & 0x0001400000000000) || !(DeviceExtension->Flags & 0x0001400000000000))
        {
            DPRINT1("ACPIDetectDuplicateHID: matches with %X\n", Extension);
            ASSERT(FALSE);
            KeBugCheckEx(0xA5, 0xD, (ULONG_PTR)DeviceExtension, 'DIU_', 0);
        }

        if (!strcmp(Extension->InstanceID, DeviceExtension->InstanceID))
        {
            DPRINT1("ACPIDetectDuplicateHID: has _UID match with %X\n\t\tContact the Machine Vendor to get this problem fixed\n", Extension);
            ASSERT(FALSE);
            KeBugCheckEx(0xA5, 0xD, (ULONG_PTR)DeviceExtension, 'DIU_', 1);
        }

Next:
        Extension = ACPIExtListEnumNext(&ExtList);
    }
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhaseSta(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;

    DPRINT("ACPIBuildProcessDevicePhaseSta: BuildRequest %X\n", BuildRequest);

    DeviceExtension = BuildRequest->DeviceExtension;
    BuildRequest->BuildReserved1 = 9;

    ACPIDetectDuplicateHID(DeviceExtension);
    ACPIBuildCompleteMustSucceed(NULL, STATUS_SUCCESS, 0, BuildRequest);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIBuildProcessDeviceGenericEvalStrict(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    PAMLI_NAME_SPACE_OBJECT ChildObject;
    ULONG NameSeg;
    ULONG Idx;
    NTSTATUS Status;

    DPRINT("ACPIBuildProcessDeviceGenericEvalStrict: BuildRequest %X\n", BuildRequest);

    DeviceExtension = BuildRequest->DeviceExtension;

    RtlZeroMemory(&BuildRequest->Device.Data, sizeof(BuildRequest->Device.Data));

    Idx = BuildRequest->BuildReserved0;
    NameSeg = AcpiBuildDevicePowerNameLookup[Idx];
    BuildRequest->BuildReserved1 = (Idx + 1);

    ChildObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, NameSeg);
    BuildRequest->ChildObject = ChildObject;

    if (ChildObject)
    {
        Status = AMLIAsyncEvalObject(ChildObject,
                                     &BuildRequest->Device.Data,
                                     0,
                                     NULL,
                                     (PVOID)ACPIBuildCompleteMustSucceed,
                                     BuildRequest);
    }
    else
    {
        Status = STATUS_SUCCESS;
    }

    DPRINT("ACPIBuildProcessDeviceGenericEvalStrict: Phase%X, Status %X\n", (BuildRequest->BuildReserved0 - 3), Status);

    if (Status != STATUS_PENDING)
    {
        ACPIBuildCompleteMustSucceed(BuildRequest->ChildObject,
                                     Status,
                                     (ULONG)&BuildRequest->Device.Data,
                                     BuildRequest);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIDockGetDockObject(
    _In_ PAMLI_NAME_SPACE_OBJECT ScopeObject,
    _Out_ PAMLI_NAME_SPACE_OBJECT* OutNsObject)
{
    return AMLIGetNameSpaceObject("_DCK", ScopeObject, OutNsObject, 1);
}

BOOLEAN
NTAPI
ACPIDockIsDockDevice(
    _In_ PAMLI_NAME_SPACE_OBJECT AcpiObject)
{
    PAMLI_NAME_SPACE_OBJECT dummy;

    if (NT_SUCCESS(ACPIDockGetDockObject(AcpiObject, &dummy)))
    {
        return TRUE;
    }

    return FALSE;
}

PDEVICE_EXTENSION
NTAPI
ACPIDockFindCorrespondingDock(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

NTSTATUS
NTAPI
ACPIBuildDockExtension(
    _In_ PAMLI_NAME_SPACE_OBJECT AcpiObject,
    _In_ PDEVICE_EXTENSION rootDeviceExtension)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
__cdecl
ACPIBuildCompleteGeneric(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ NTSTATUS InStatus,
    _In_ ULONG Param3,
    _In_ PVOID Context)
{
    PACPI_BUILD_REQUEST BuildRequest = Context;
    LONG ExChange;

    DPRINT("ACPIBuildCompleteGeneric: BuildRequest %X\n", BuildRequest);

    ExChange = BuildRequest->BuildReserved1;

    if (!NT_SUCCESS(InStatus))
    {
        DPRINT1("ACPIBuildCompleteGeneric: InStatus %X\n", InStatus);
        BuildRequest->Status = InStatus;
    }

    BuildRequest->BuildReserved1 = 2;

    ACPIBuildCompleteCommon(&BuildRequest->WorkDone, ExChange);
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhaseEjd(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    DPRINT("ACPIBuildProcessDevicePhaseEjd: BuildRequest %X\n", BuildRequest);

    DeviceExtension = BuildRequest->DeviceExtension;

    if ((DeviceExtension->Flags & 0000000000000002) || !(DeviceExtension->Flags & 0x0000000004000000))
        BuildRequest->BuildReserved1 = 0x0B;
    else
        BuildRequest->BuildReserved1 = 0x13;

    if (BuildRequest->ChildObject)
    {
        AMLIFreeDataBuffs(&BuildRequest->Device.Data, TRUE);

        ExInterlockedInsertTailList(&AcpiUnresolvedEjectList, &DeviceExtension->EjectDeviceList, &AcpiDeviceTreeLock);

        if (DeviceExtension->DebugFlags & 1)
        {
            DPRINT1("ACPIBuildProcessDevicePhaseEjd: Ejector already found\n");
        }
        else
        {
            DeviceExtension->DebugFlags |= 1;
        }
    }

    if (!ACPIDockIsDockDevice(DeviceExtension->AcpiObject))
    {
        Status = STATUS_SUCCESS;
        goto Finish;
    }

    if (!AcpiInformation->Dockable)
    {
        DPRINT1("ACPIBuildProcessDevicePhaseEjd: BIOS BUG - DOCK bit not set\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 0xC, (ULONG_PTR)DeviceExtension, (ULONG_PTR)BuildRequest->ChildObject, 0);
    }

    if (ACPIDockFindCorrespondingDock(DeviceExtension))
    {
        DPRINT1("ACPIBuildProcessDevicePhaseEjd: KeBugCheckEx()\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 0xC, (ULONG_PTR)DeviceExtension, (ULONG_PTR)BuildRequest->ChildObject, 1);
    }

    KeAcquireSpinLockAtDpcLevel(&AcpiDeviceTreeLock);
    Status = ACPIBuildDockExtension(DeviceExtension->AcpiObject, RootDeviceExtension);
    KeReleaseSpinLockFromDpcLevel(&AcpiDeviceTreeLock);

Finish:

    DPRINT("ACPIBuildProcessDevicePhaseEjd: Status %X\n", Status);

    ACPIBuildCompleteGeneric(NULL, Status, 0, BuildRequest);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildDevicePowerNodes(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ PAMLI_OBJECT_DATA Data,
    _In_ ULONG Phase)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhasePrw(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    PAMLI_PACKAGE_OBJECT DataBuff;
    AMLI_OBJECT_DATA data;
    SYSTEM_POWER_STATE SystemPowerState;
    SYSTEM_POWER_STATE SystemWakeLevel;
    ULONG Idx;
    ULONG Mask;
    BOOLEAN IsOverride = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPIBuildProcessDevicePhasePrw: BuildRequest %X\n", BuildRequest);

    DeviceExtension = BuildRequest->DeviceExtension;
    BuildRequest->BuildReserved1 = 0xD;

    DeviceExtension->PowerInfo.PowerObject[0] = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'WSP_');

    if (!BuildRequest->ChildObject)
        goto Finish;

    if ((AcpiOverrideAttributes & 8) && !(DeviceExtension->Flags & 0x0000000800000000))
        IsOverride = TRUE;

    if (BuildRequest->Device.Data.DataType != 4) // Package
    {
        DPRINT1("ACPIBuildProcessDevicePhasePrw: KeBugCheckEx()\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 9, (ULONG_PTR)DeviceExtension, (ULONG_PTR)BuildRequest->ChildObject, BuildRequest->Device.Data.DataType);
    }

    Status = ACPIBuildDevicePowerNodes(DeviceExtension, BuildRequest->ChildObject, &BuildRequest->Device.Data, 0);

    KeAcquireSpinLockAtDpcLevel(&AcpiPowerLock);

    DataBuff = BuildRequest->Device.Data.DataBuff;

    if (DataBuff->Data[0].DataType != 1) // Integer
    {
        DPRINT1("ACPIBuildProcessDevicePhasePrw: KeBugCheckEx()\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 4, (ULONG_PTR)DeviceExtension, (ULONG_PTR)BuildRequest->ChildObject, DataBuff->Data[0].DataType);
    }

    if (DataBuff->Data[1].DataType != 1) // Integer
    {
        DPRINT1("ACPIBuildProcessDevicePhasePrw: KeBugCheckEx()\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 4, (ULONG_PTR)DeviceExtension, (ULONG_PTR)BuildRequest->ChildObject, DataBuff->Data[1].DataType);
    }

    if (!IsOverride)
    {
        DeviceExtension->PowerInfo.WakeBit = (ULONG)DataBuff->Data[0].DataValue;

        SystemPowerState = (ULONG)DataBuff->Data[1].DataValue;

        if (SystemPowerState < PowerSystemShutdown)
            SystemWakeLevel = SystemPowerStateTranslation[SystemPowerState];
        else
            SystemWakeLevel = 0;

        DeviceExtension->PowerInfo.SystemWakeLevel = SystemWakeLevel;

        ACPIInternalUpdateFlags(DeviceExtension, 0x0000000000010000, FALSE);
    }

    KeReleaseSpinLockFromDpcLevel(&AcpiPowerLock);

    Idx = (((ULONG)DataBuff->Data[0].DataValue & 0xFF) / 8);
    Mask = (1 << (((ULONG)DataBuff->Data[0].DataValue & 0xFF) % 8));

    KeAcquireSpinLockAtDpcLevel(&GpeTableLock);

    if (GpeEnable[Idx] & Mask)
    {
        if (!(DeviceExtension->Flags & 0x0000000800000000))
        {
            if (!(GpeSpecialHandler[Idx] & Mask))
                GpeWakeHandler[Idx] |= Mask;
        }
        else
        {
            GpeSpecialHandler[Idx] |= Mask;

            if (GpeWakeHandler[Idx] & Mask)
                GpeWakeHandler[Idx] &= ~Mask;
        }
    }

    KeReleaseSpinLockFromDpcLevel(&GpeTableLock);

    AMLIFreeDataBuffs(&BuildRequest->Device.Data, 1);

    if (DeviceExtension->PowerInfo.PowerObject[0])
    {
        RtlZeroMemory(&data, sizeof(data));

        data.DataType = 1;
        data.DataValue = 0;

        AMLIAsyncEvalObject(DeviceExtension->PowerInfo.PowerObject[0], NULL, 1, &data, NULL, NULL);
    }

Finish:

    DPRINT("ACPIBuildProcessDevicePhasePrw: Status %X\n", Status);

    ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhasePr0(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    DeviceExtension = BuildRequest->DeviceExtension;
    BuildRequest->BuildReserved1 = 0x0F;

    DeviceExtension->PowerInfo.PowerObject[1] = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, '0SP_');

    if (BuildRequest->ChildObject)
    {
        if (BuildRequest->Device.Data.DataType != 4)
        {
            DPRINT1("ACPIBuildProcessDevicePhasePr0: KeBugCheckEx()\n");
            ASSERT(FALSE);
            KeBugCheckEx(0xA5, 9, (ULONG_PTR)DeviceExtension, (ULONG_PTR)BuildRequest->ChildObject, BuildRequest->Device.Data.DataType);
        }

        Status = ACPIBuildDevicePowerNodes(DeviceExtension, BuildRequest->ChildObject, &BuildRequest->Device.Data, 1);

        AMLIFreeDataBuffs(&BuildRequest->Device.Data, 1);
    }

    DPRINT("ACPIBuildProcessDevicePhasePr0: Status %X\n", Status);

    ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhasePr1(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    DeviceExtension = BuildRequest->DeviceExtension;
    BuildRequest->BuildReserved1 = 0x11;

    DeviceExtension->PowerInfo.PowerObject[2] = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, '1SP_');

    if (!DeviceExtension->PowerInfo.PowerObject[2])
        DeviceExtension->PowerInfo.PowerObject[2] = DeviceExtension->PowerInfo.PowerObject[1];

    if (BuildRequest->ChildObject)
    {
        if (BuildRequest->Device.Data.DataType != 4)
        {
            DPRINT1("ACPIBuildProcessDevicePhasePr1: KeBugCheckEx()\n");
            ASSERT(FALSE);
            KeBugCheckEx(0xA5, 9, (ULONG_PTR)DeviceExtension, (ULONG_PTR)BuildRequest->ChildObject, BuildRequest->Device.Data.DataType);
        }

        Status = ACPIBuildDevicePowerNodes(DeviceExtension, BuildRequest->ChildObject, &BuildRequest->Device.Data, 2);

        AMLIFreeDataBuffs(&BuildRequest->Device.Data, 1);
    }

    DPRINT("ACPIBuildProcessDevicePhasePr1: Status %X\n", Status);

    ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhasePr2(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    DeviceExtension = BuildRequest->DeviceExtension;

    DeviceExtension->PowerInfo.PowerObject[3] = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, '2SP_');

    if (!DeviceExtension->PowerInfo.PowerObject[3])
        DeviceExtension->PowerInfo.PowerObject[3] = DeviceExtension->PowerInfo.PowerObject[2];

    if (BuildRequest->ChildObject)
    {
        if (BuildRequest->Device.Data.DataType != 4)
        {
            DPRINT1("ACPIBuildProcessDevicePhasePr2: KeBugCheckEx()\n");
            ASSERT(FALSE);
            KeBugCheckEx(0xA5, 9, (ULONG_PTR)DeviceExtension, (ULONG_PTR)BuildRequest->ChildObject, BuildRequest->Device.Data.DataType);
        }

        Status = ACPIBuildDevicePowerNodes(DeviceExtension, BuildRequest->ChildObject, &BuildRequest->Device.Data, 3);
        AMLIFreeDataBuffs(&BuildRequest->Device.Data, 1);
    }

    if (DeviceExtension->Flags & 2)
    {
        BuildRequest->ChildObject = NULL;
        BuildRequest->BuildReserved1 = 0x16;
    }
    else
    {
        BuildRequest->BuildReserved1 = 0x15;
    }

    DPRINT("ACPIBuildProcessDevicePhasePr2: Status %X\n", Status);

    ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhaseCrs(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    //PDEVICE_EXTENSION DeviceExtension;

    DPRINT("ACPIBuildProcessDevicePhaseCrs: BuildRequest %X\n", BuildRequest);

    //DeviceExtension = BuildRequest->DeviceExtension;
    BuildRequest->BuildReserved1 = 0xB;

    if (BuildRequest->ChildObject)
    {
        if (BuildRequest->Device.Data.DataType != 3)
        {
            DPRINT1("ACPIBuildProcessDevicePhaseCrs: KeBugCheckEx()\n");
            ASSERT(FALSE);
            //KeBugCheckEx(0xA5, 7, DeviceExtension, BuildRequest->ChildObject, BuildRequest->Device.Data.DataType);
        }

        DPRINT1("ACPIBuildProcessDevicePhaseCrs: FIXME\n");
        ASSERT(FALSE);

        //ACPIMatchKernelPorts(DeviceExtension, &BuildRequest->Synchronize);
        //AMLIFreeDataBuffs(&BuildRequest->Synchronize, 1);
    }

    //ACPIDebugDevicePrint(8u, DeviceExtension, "ACPIBuildProcessDevicePhaseCrs: Status = %08lx\n", 0);

    ACPIBuildCompleteMustSucceed(NULL, STATUS_SUCCESS, 0, BuildRequest);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIBuildProcessDeviceGenericEval(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    ULONG NameSeg;
    ULONG Idx;
    NTSTATUS Status;

    RtlZeroMemory(&BuildRequest->Device.Data, sizeof(BuildRequest->Device.Data));

    DeviceExtension = BuildRequest->DeviceExtension;
    Idx = BuildRequest->BuildReserved0;
    NameSeg = AcpiBuildDevicePowerNameLookup[Idx];
    BuildRequest->BuildReserved1 = (Idx + 1);

    BuildRequest->ChildObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, NameSeg);
    if (!BuildRequest->ChildObject)
    {
        Status = STATUS_SUCCESS;

        ACPIBuildCompleteGeneric(BuildRequest->ChildObject, Status, (ULONG)&BuildRequest->Device.Data, BuildRequest);
        goto Exit;
    }

    Status = AMLIAsyncEvalObject(BuildRequest->ChildObject, &BuildRequest->Device.Data, 0, NULL, (PVOID)ACPIBuildCompleteGeneric, BuildRequest);
    if (Status != STATUS_PENDING)
    {
        ACPIBuildCompleteGeneric(BuildRequest->ChildObject, Status, (ULONG)&BuildRequest->Device.Data, BuildRequest);
        goto Exit;
    }

Exit:

    DPRINT("ACPIBuildProcessDeviceGenericEval: Phase%X, Status %X\n", (BuildRequest->BuildReserved0 - 3), Status);

    return STATUS_SUCCESS;
}

VOID
NTAPI
ACPIDevicePowerDpc(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    NTSTATUS Status;

    DPRINT("ACPIDevicePowerDpc: AcpiBuildDpcRunning %X\n", AcpiBuildDpcRunning);

    KeAcquireSpinLockAtDpcLevel(&AcpiBuildQueueLock);

    if (AcpiBuildDpcRunning)
    {
        KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);
        DPRINT("ACPIDevicePowerDpc: AcpiBuildDpcRunning %X\n", AcpiBuildDpcRunning);
        return;
    }

    AcpiBuildDpcRunning = TRUE;

    do
    {
        AcpiBuildWorkDone = FALSE;

        if (!IsListEmpty(&AcpiBuildQueueList))
            ACPIBuildProcessQueueList();

        KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);

        if (!IsListEmpty(&AcpiBuildRunMethodList))
        {
            Status = ACPIBuildProcessGenericList(&AcpiBuildRunMethodList,AcpiBuildRunMethodDispatch);

            KeAcquireSpinLockAtDpcLevel(&AcpiBuildQueueLock);

            if (Status == STATUS_PENDING)
            {
                DPRINT("ACPIDevicePowerDpc: continue Status == STATUS_PENDING\n");
                continue;
            }

            if (!IsListEmpty(&AcpiBuildQueueList))
            {
                AcpiBuildWorkDone = TRUE;
                continue;
            }

            KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);
        }

        if (!IsListEmpty(&AcpiBuildOperationRegionList))
        {
            DPRINT1("ACPIDevicePowerDpc: FIXME\n");
            ASSERT(FALSE);
        }

        if (!IsListEmpty(&AcpiBuildPowerResourceList))
        {
            DPRINT1("ACPIDevicePowerDpc: FIXME\n");
            ASSERT(FALSE);
        }

        if (!IsListEmpty(&AcpiBuildDeviceList))
        {
            DPRINT1("ACPIDevicePowerDpc: FIXME\n");
            ASSERT(FALSE);
        }

        if (!IsListEmpty(&AcpiBuildThermalZoneList))
        {
            DPRINT1("ACPIDevicePowerDpc: FIXME\n");
            ASSERT(FALSE);
        }

        if (IsListEmpty(&AcpiBuildDeviceList) &&
            IsListEmpty(&AcpiBuildOperationRegionList) &&
            IsListEmpty(&AcpiBuildPowerResourceList) &&
            IsListEmpty(&AcpiBuildRunMethodList) &&
            IsListEmpty(&AcpiBuildThermalZoneList))
        {
            KeAcquireSpinLockAtDpcLevel(&AcpiPowerQueueLock);

            if (!IsListEmpty(&AcpiPowerDelayedQueueList))
            {
                DPRINT1("ACPIDevicePowerDpc: FIXME\n");
                ASSERT(FALSE);
            }

            KeReleaseSpinLockFromDpcLevel(&AcpiPowerQueueLock);
        }

        if (!IsListEmpty(&AcpiBuildSynchronizationList))
            Status = ACPIBuildProcessSynchronizationList(&AcpiBuildSynchronizationList);

        KeAcquireSpinLockAtDpcLevel(&AcpiBuildQueueLock);
    }
    while (AcpiBuildWorkDone);

    AcpiBuildDpcRunning = FALSE;

    KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);

    DPRINT("ACPIDevicePowerDpc: exit (%p)\n", Dpc);
}

VOID
NTAPI
ACPIDeviceInternalQueueRequest(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PACPI_POWER_REQUEST Request,
    _In_ ULONG Flags)
{
    if (Flags & 0x100)
    {
        InsertHeadList(&AcpiPowerSynchronizeList, &Request->ListEntry);
    }
    else if (IsListEmpty(&DeviceExtension->PowerInfo.PowerRequestListEntry))
    {
        InsertTailList(&DeviceExtension->PowerInfo.PowerRequestListEntry, &Request->SerialListEntry);

        if (Flags & 1)
        {
            InsertTailList(&AcpiPowerDelayedQueueList, &Request->ListEntry);
        }
        else
        {
            InsertTailList(&AcpiPowerQueueList, &Request->ListEntry);
        }
    }
    else
    {
        InsertTailList(&DeviceExtension->PowerInfo.PowerRequestListEntry, &Request->SerialListEntry);
    }

    AcpiPowerWorkDone = TRUE;

    if ((Flags & 1) || AcpiPowerDpcRunning)
        return;

    KeInsertQueueDpc(&AcpiPowerDpc, NULL, NULL);
}

NTSTATUS
NTAPI
ACPIDeviceInitializePowerRequest(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ POWER_STATE State,
    _In_ PVOID CallBack,
    _In_ PVOID Context,
    _In_ POWER_ACTION ShutdownType,
    _In_ ACPI_POWER_REQUEST_TYPE RequestType,
    _In_ ULONG Flags)
{
    PACPI_POWER_REQUEST Request;
    KIRQL OldIrql;

    Request = ExAllocateFromNPagedLookasideList(&RequestLookAsideList);
    if (!Request)
    {
        if (CallBack)
        {
            DPRINT1("ACPIDeviceInitializePowerRequest: FIXME\n");
            ASSERT(FALSE);
        }

        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    RtlZeroMemory(Request, sizeof(*Request));

    Request->Status = STATUS_SUCCESS;
    Request->CallBack = CallBack;
    Request->Context = Context;
    Request->Signature = '_SGP';
    Request->DeviceExtension = DeviceExtension;
    Request->WorkDone = 3;
    Request->RequestType = RequestType;

    InitializeListHead(&Request->ListEntry);
    InitializeListHead(&Request->SerialListEntry);

    KeAcquireSpinLock(&AcpiPowerQueueLock, &OldIrql);

    if (RequestType == AcpiPowerRequestDevice)
    {
        if (InterlockedCompareExchange(&DeviceExtension->HibernatePathCount, 0, 0))
        {
            if (ShutdownType == PowerActionHibernate)
            {
                if (State.SystemState == 4)
                    Flags |= 0x10;
            }
            else if (State.SystemState == 1)
            {
                Flags |= 0x20;
            }
        }

        Request->u.DevicePowerRequest.DevicePowerState = State.DeviceState;
        Request->u.DevicePowerRequest.Flags = Flags;

        if (State.DeviceState > DeviceExtension->PowerInfo.PowerState)
        {
            if (DeviceExtension->DeviceObject)
                PoSetPowerState(DeviceExtension->DeviceObject, DevicePowerState, State);
        }

        goto Finish;
    }

    if (RequestType == AcpiPowerRequestSystem)
    {
        Request->u.DevicePowerRequest.Flags = State.SystemState;
        Request->u.SystemPowerRequest.SystemPowerAction = ShutdownType;
        goto Finish;
    }

    if (RequestType == AcpiPowerRequestWaitWake)
    {
        Request->u.DevicePowerRequest.DevicePowerState = State.DeviceState;
        Request->u.DevicePowerRequest.Flags = Flags;

        KeReleaseSpinLock(&AcpiPowerQueueLock, OldIrql);

        DPRINT1("ACPIDeviceInitializePowerRequest: FIXME\n");
        ASSERT(FALSE);
    }

    if (RequestType == AcpiPowerRequestWarmEject)
    {
        Request->u.DevicePowerRequest.DevicePowerState = State.DeviceState;
    }
    else if (RequestType == AcpiPowerRequestSynchronize)
    {
        Request->u.DevicePowerRequest.Flags = Flags;
    }

Finish:

    if (!(Flags & 2))
    {
        ACPIDeviceInternalQueueRequest(DeviceExtension, Request, Flags);
    }

    KeReleaseSpinLock(&AcpiPowerQueueLock, OldIrql);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
ACPIDeviceInternalDelayedDeviceRequest(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ DEVICE_POWER_STATE DeviceState,
    _In_ PVOID CallBack,
    _In_ PIRP Irp)
{
    POWER_STATE PowerState;
    NTSTATUS Status;

    DPRINT("ACPIDeviceInternalDelayedDeviceRequest: Irp %p, Transition to D%X\n", Irp, (DeviceState - 1));

    PowerState.DeviceState = DeviceState;

    Status = ACPIDeviceInitializePowerRequest(DeviceExtension, PowerState, CallBack, Irp, 0, 0, 9);
    if (Status == STATUS_MORE_PROCESSING_REQUIRED)
        Status = STATUS_PENDING;

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessDevicePhasePsc(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
{
    PDEVICE_EXTENSION DeviceExtension;
    PACPI_DEVICE_POWER_NODE* PowerNodes;
    PACPI_DEVICE_POWER_NODE powerNode;
    DEVICE_POWER_STATE* OutDevicePowerMatrix;
    SYSTEM_POWER_STATE systemState = 2;
    DEVICE_POWER_STATE deviceState;
    DEVICE_POWER_STATE State;
    NTSTATUS Status;

    DeviceExtension = BuildRequest->DeviceExtension;
    BuildRequest->BuildReserved1 = 0;

    DeviceExtension->PowerInfo.PowerObject[4] = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, '3SP_');

    KeAcquireSpinLockAtDpcLevel(&AcpiPowerLock);

    OutDevicePowerMatrix = &DeviceExtension->PowerInfo.DevicePowerMatrix[2];
    do
    {
        deviceState = 1;
        PowerNodes = &DeviceExtension->PowerInfo.PowerNode[1];

        while (TRUE)
        {
            powerNode = *PowerNodes;
            if (powerNode)
            {
                do
                {
                    if (powerNode->SystemState < systemState)
                        break;

                    powerNode = powerNode->Next;
                }
                while (powerNode);

                if (!powerNode)
                    break;
            }

            deviceState++;
            PowerNodes++;

            if (deviceState > 3)
                goto Next;
        }

        DPRINT("ACPIBuildProcessDevicePhasePsc: D%X <-> S%X\n", (deviceState - 1), (systemState - 1));

        *OutDevicePowerMatrix = deviceState;
Next:
        systemState++;
        OutDevicePowerMatrix++;
    }
    while (systemState <= 5);

    DeviceExtension->PowerInfo.DeviceWakeLevel = DeviceExtension->PowerInfo.DevicePowerMatrix[DeviceExtension->PowerInfo.SystemWakeLevel];

    KeReleaseSpinLockFromDpcLevel(&AcpiPowerLock);

    State = 1;

    if (DeviceExtension->Flags & 0x0000000080000000)
    {
        State = 4;
        goto Finish;
    }

    if (!BuildRequest->ChildObject)
    {
        goto Finish;
    }

    if (!NT_SUCCESS(BuildRequest->Status))
    {
        goto Finish;
    }

    if (DeviceExtension->Flags & 0x0000000000080000)
    {
        AMLIFreeDataBuffs(&BuildRequest->Device.Data, 1);
        DeviceExtension->PowerInfo.PowerState = 1;
        goto Finish;
    }

    if (BuildRequest->Device.Data.DataType != 1)
    {
        DPRINT1("ACPIBuildProcessDevicePhasePsc: KeBugCheckEx()\n");
        KeBugCheckEx(0xA5, 8, (ULONG_PTR)DeviceExtension, (ULONG_PTR)BuildRequest->ChildObject, BuildRequest->Device.Data.DataType);
    }

    if ((ULONG)BuildRequest->Device.Data.DataValue < 4)
        State = DevicePowerStateTranslation[(ULONG)BuildRequest->Device.Data.DataValue];
    else
        State = 0;

    AMLIFreeDataBuffs(&BuildRequest->Device.Data, 1);

Finish:

    Status = ACPIDeviceInternalDelayedDeviceRequest(DeviceExtension, State, NULL, NULL);

    DPRINT("ACPIBuildProcessDevicePhasePsc: Status %X\n", Status);

    ACPIBuildCompleteGeneric(NULL, Status, 0, BuildRequest);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessPowerResourceFailure(
    _In_ PACPI_BUILD_REQUEST Entry)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBuildProcessPowerResourcePhase0(
    _In_ PACPI_BUILD_REQUEST Entry)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBuildProcessPowerResourcePhase1(
    _In_ PACPI_BUILD_REQUEST Entry)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
ACPIInternalGetDispatchTable(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDEVICE_EXTENSION* OutDeviceExtension,
    _Out_ PIRP_DISPATCH_TABLE* OutIrpDispatch)
{
    PDEVICE_EXTENSION DeviceExtension;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &OldIrql);

    DeviceExtension = DeviceObject->DeviceExtension;

    *OutDeviceExtension = DeviceExtension;

    if (DeviceExtension)
        *OutIrpDispatch = DeviceExtension->DispatchTable;
    else
        *OutIrpDispatch = NULL;

    KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);
}

LONG
NTAPI
ACPIInternalDecrementIrpReferenceCount(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    LONG OldReferenceCount;

    OldReferenceCount = InterlockedDecrement(&DeviceExtension->OutstandingIrpCount);

    if (!OldReferenceCount)
        OldReferenceCount = KeSetEvent(DeviceExtension->RemoveEvent, 0, FALSE);

    return OldReferenceCount;
}

NTSTATUS
NTAPI
ACPIDispatchIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_EXTENSION DeviceExtension;
    PDRIVER_DISPATCH DispatchEntry;
    PIRP_DISPATCH_TABLE IrpDispatch;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    LONG OldReferenceCount;
    UCHAR MinorFunction;
    UCHAR MajorFunction;
    NTSTATUS Status;

    DPRINT("ACPIDispatchIrp: Device %X, Irp %X\n", DeviceObject, Irp);

    IoStack = Irp->Tail.Overlay.CurrentStackLocation;

    ACPIInternalGetDispatchTable(DeviceObject, &DeviceExtension, &IrpDispatch);

    if (!DeviceExtension || (DeviceExtension->Flags & 4) || DeviceExtension->Signature != '_SGP')
    {
        DPRINT1("ACPIDispatchIrp: Deleted Device %p got Irp %p\n", DeviceObject, Irp);

        if (IoStack->MajorFunction == IRP_MJ_POWER)
        {
            DPRINT1("ACPIDispatchIrp: FIXME\n");
            ASSERT(FALSE);
            Status = STATUS_NOT_IMPLEMENTED;
        }
        else
        {
            DPRINT1("ACPIDispatchIrp: FIXME\n");
            ASSERT(FALSE);
            Status = STATUS_NOT_IMPLEMENTED;
        }

        return Status;
    }

    ASSERT(DeviceExtension->RemoveEvent == NULL);

    MajorFunction = IoStack->MajorFunction;
    MinorFunction = IoStack->MinorFunction;

    if (IoStack->MajorFunction == IRP_MJ_POWER)
    {
        if (MinorFunction >= 4)
            DispatchEntry = IrpDispatch->Power[4];
        else
            DispatchEntry = IrpDispatch->Power[MinorFunction];

        InterlockedIncrement(&DeviceExtension->OutstandingIrpCount);

        Status = DispatchEntry(DeviceObject, Irp);

        ACPIInternalDecrementIrpReferenceCount(DeviceExtension);

        return Status;
    }

    if (MajorFunction != IRP_MJ_PNP)
    {
        if (MajorFunction == IRP_MJ_DEVICE_CONTROL)
        {
            DispatchEntry = IrpDispatch->DeviceControl;
        }
        else if (MajorFunction == IRP_MJ_CREATE || MinorFunction == IRP_MJ_CLOSE)
        {
            DispatchEntry = IrpDispatch->CreateClose;
        }
        else if (MajorFunction == IRP_MJ_SYSTEM_CONTROL)
        {
            DispatchEntry = IrpDispatch->SystemControl;
        }
        else
        {
            DispatchEntry = IrpDispatch->Other;
        }

        InterlockedIncrement(&DeviceExtension->OutstandingIrpCount);

        Status = DispatchEntry(DeviceObject, Irp);

        ACPIInternalDecrementIrpReferenceCount(DeviceExtension);

        return Status;
    }

    /* IRP_MJ_PNP */

    if (MinorFunction == IRP_MN_START_DEVICE)
    {
        DispatchEntry = (PDRIVER_DISPATCH)IrpDispatch->PnpStartDevice;
        InterlockedIncrement(&DeviceExtension->OutstandingIrpCount);
        Status = DispatchEntry(DeviceObject, Irp);
        ACPIInternalDecrementIrpReferenceCount(DeviceExtension);
        return Status;
    }

    if (MinorFunction >= IRP_MN_QUERY_LEGACY_BUS_INFORMATION)
        DispatchEntry = IrpDispatch->Pnp[IRP_MN_QUERY_LEGACY_BUS_INFORMATION];
    else
        DispatchEntry = IrpDispatch->Pnp[MinorFunction];

    if (MinorFunction == IRP_MN_REMOVE_DEVICE || MinorFunction == IRP_MN_SURPRISE_REMOVAL)
    {
        KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
        DeviceExtension->RemoveEvent = &Event;

        DPRINT1("ACPIDispatchIrp: FIXME ACPIWakeEmptyRequestQueue()\n");
        ASSERT(FALSE);

        OldReferenceCount = InterlockedDecrement(&DeviceExtension->OutstandingIrpCount);
        ASSERT(OldReferenceCount >= 0);

        if (OldReferenceCount != 0)
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

        InterlockedIncrement(&DeviceExtension->OutstandingIrpCount);
        DeviceExtension->RemoveEvent = NULL;

        Status = DispatchEntry(DeviceObject, Irp);

        return Status;
    }

    InterlockedIncrement(&DeviceExtension->OutstandingIrpCount);

    Status = DispatchEntry(DeviceObject, Irp);

    ACPIInternalDecrementIrpReferenceCount(DeviceExtension);

    return Status;
}

VOID
NTAPI
ACPIFilterFastIoDetachCallback(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ACPIBuildProcessQueueList(VOID)
{
    PACPI_BUILD_REQUEST BuildRequest;
    PLIST_ENTRY Entry;

    DPRINT("ACPIBuildProcessQueueList: start\n");

    Entry = AcpiBuildQueueList.Flink;
    while (Entry != &AcpiBuildQueueList)
    {
        BuildRequest = CONTAINING_RECORD(Entry, ACPI_BUILD_REQUEST, Link);

        RemoveEntryList(Entry);
        InsertTailList(BuildRequest->ListHeadForInsert, Entry);

        BuildRequest->Flags &= ~0x1000;
        BuildRequest->ListHeadForInsert = NULL;

        Entry = AcpiBuildQueueList.Flink;
    }

    DPRINT("ACPIBuildProcessQueueList: exit\n");
}

NTSTATUS
NTAPI
ACPIBuildProcessSynchronizationList(
    _In_ PLIST_ENTRY SynchronizationList)
{
    PACPI_BUILD_REQUEST BuildRequest;
    PLIST_ENTRY Entry;
    BOOLEAN Result = TRUE;

    DPRINT("ACPIBuildProcessSynchronizationList: %p\n", SynchronizationList);

    Entry = SynchronizationList->Flink;
    while (Entry != SynchronizationList)
    {
        BuildRequest = CONTAINING_RECORD(Entry, ACPI_BUILD_REQUEST, Link);

        Entry = Entry->Flink;

        if (!IsListEmpty(BuildRequest->Synchronize.ListHead))
        {
            Result = FALSE;
            continue;
        }

        DPRINT("ACPIBuildProcessSynchronizationList(%4s) = STATUS_SUCCESS\n", &BuildRequest->Synchronize.Context);

        ACPIBuildProcessGenericComplete(BuildRequest);
    }

    return (Result ? STATUS_SUCCESS : STATUS_PENDING);
}

NTSTATUS
NTAPI
ACPIBuildProcessGenericList(
    _In_ PLIST_ENTRY GenericList,
    _In_ PACPI_BUILD_DISPATCH* BuildDispatch)
{
    PACPI_BUILD_DISPATCH CallBack = NULL;
    PACPI_BUILD_REQUEST BuildRequest;
    PLIST_ENTRY Entry;
    PLIST_ENTRY NextValue;
    ULONG Idx;
    BOOLEAN allWorkComplete = TRUE;
    //NTSTATUS status = STATUS_SUCCESS;

    DPRINT("ACPIBuildProcessGenericList: %p, %p\n", BuildDispatch, *BuildDispatch);

    Entry = GenericList->Flink;
    while (Entry != GenericList)
    {
        BuildRequest = CONTAINING_RECORD(Entry, ACPI_BUILD_REQUEST, Link);

        //DPRINT("ACPIBuildProcessGenericList: %X '%s', %X\n", BuildRequest, NameSegString(BuildRequest->Signature), BuildRequest->WorkDone);

        NextValue = Entry->Flink;
        Idx = InterlockedCompareExchange(&(BuildRequest->WorkDone), 1, 1);

        //DPRINT("ACPIBuildProcessGenericList: [%X] %p, %p\n", Idx, GenericList, Entry);

        CallBack = BuildDispatch[Idx];
        if (!CallBack)
        {
            allWorkComplete = FALSE;
            Entry = NextValue;
            continue;
        }

        if (Idx != 2)
            BuildRequest->BuildReserved0 = Idx;

        Idx = InterlockedCompareExchange(&(BuildRequest->WorkDone), 1, Idx);
        /*status =*/ (CallBack)(BuildRequest);
        //DPRINT("ACPIBuildProcessGenericList: [%X] status %X\n", Idx, status);

        if (Idx == 0 || Idx == 2)
            Entry = NextValue;
    }

    DPRINT("ACPIBuildProcessGenericList: status %X\n", allWorkComplete ? STATUS_SUCCESS : STATUS_PENDING);

    return (allWorkComplete ? STATUS_SUCCESS : STATUS_PENDING);
}

VOID
NTAPI
ACPIBuildDeviceDpc(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    NTSTATUS Status;

    KeAcquireSpinLockAtDpcLevel(&AcpiBuildQueueLock);

    if (AcpiBuildDpcRunning)
    {
        KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);
        DPRINT("ACPIBuildDeviceDpc: AcpiBuildDpcRunning %X\n", AcpiBuildDpcRunning);
        return;
    }

    AcpiBuildDpcRunning = TRUE;

    do
    {
        AcpiBuildWorkDone = FALSE;

        if (!IsListEmpty(&AcpiBuildQueueList))
            ACPIBuildProcessQueueList();

        KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);

        if (!IsListEmpty(&AcpiBuildRunMethodList))
        {
            Status = ACPIBuildProcessGenericList(&AcpiBuildRunMethodList, AcpiBuildRunMethodDispatch);

            KeAcquireSpinLockAtDpcLevel(&AcpiBuildQueueLock);

            if (Status == STATUS_PENDING)
            {
                DPRINT("ACPIBuildDeviceDpc: continue Status == STATUS_PENDING\n");
                continue;
            }

            if (!IsListEmpty(&AcpiBuildQueueList))
            {
                AcpiBuildWorkDone = TRUE;
                continue;
            }

            KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);
        }

        if (!IsListEmpty(&AcpiBuildOperationRegionList))
        {
            DPRINT1("ACPIBuildDeviceDpc: FIXME\n");
            ASSERT(FALSE);
        }

        if (!IsListEmpty(&AcpiBuildPowerResourceList))
        {
            DPRINT1("ACPIBuildDeviceDpc: FIXME\n");
            ASSERT(FALSE);
        }

        if (!IsListEmpty(&AcpiBuildDeviceList))
            Status = ACPIBuildProcessGenericList(&AcpiBuildDeviceList, AcpiBuildDeviceDispatch);

        if (!IsListEmpty(&AcpiBuildThermalZoneList))
        {
            DPRINT1("ACPIBuildDeviceDpc: FIXME\n");
            ASSERT(FALSE);
        }

        if (IsListEmpty(&AcpiBuildDeviceList) &&
            IsListEmpty(&AcpiBuildOperationRegionList) &&
            IsListEmpty(&AcpiBuildPowerResourceList) &&
            IsListEmpty(&AcpiBuildRunMethodList) &&
            IsListEmpty(&AcpiBuildThermalZoneList))
        {
            KeAcquireSpinLockAtDpcLevel(&AcpiPowerQueueLock);

            if (!IsListEmpty(&AcpiPowerDelayedQueueList))
            {
                ACPIInternalMoveList(&AcpiPowerDelayedQueueList, &AcpiPowerQueueList);

                if (!AcpiPowerDpcRunning)
                    KeInsertQueueDpc(&AcpiPowerDpc, NULL, NULL);
            }

            KeReleaseSpinLockFromDpcLevel(&AcpiPowerQueueLock);
        }

        if (!IsListEmpty(&AcpiBuildSynchronizationList))
        {
            Status = ACPIBuildProcessSynchronizationList(&AcpiBuildSynchronizationList);
            DPRINT("ACPIBuildDeviceDpc: Status %X, AcpiBuildWorkDone %X\n", Status, AcpiBuildWorkDone);
        }

        KeAcquireSpinLockAtDpcLevel(&AcpiBuildQueueLock);
    }
    while (AcpiBuildWorkDone);

    AcpiBuildDpcRunning = FALSE;

    KeReleaseSpinLockFromDpcLevel(&AcpiBuildQueueLock);

    DPRINT("ACPIBuildDeviceDpc: exit (%p)\n", Dpc);
}

/* HAL FUNCTIOS *************************************************************/

VOID
NTAPI
ACPIGpeHalEnableDisableEvents(
    _In_ BOOLEAN IsEnable)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ACPIWakeEnableWakeEvents(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ACPIInitHalDispatchTable(VOID)
{
    AcpiHalDispatchTable.Signature = 'ACPI';
    AcpiHalDispatchTable.Version = 1;
    AcpiHalDispatchTable.Function1 = ACPIGpeHalEnableDisableEvents;
    AcpiHalDispatchTable.Function2 = ACPIEnableInitializeACPI;
    AcpiHalDispatchTable.Function3 = ACPIWakeEnableWakeEvents;

    HalInitPowerManagement((PPM_DISPATCH_TABLE)&AcpiHalDispatchTable, &PmHalDispatchTable);
}

/* ACPI interface FUNCTIONS *************************************************/

VOID
NTAPI
AcpiNullReference(
    _In_ PVOID Context)
{
    ;
}

NTSTATUS
NTAPI
ACPIVectorConnect(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG GpeNumber,
    _In_ KINTERRUPT_MODE Mode,
    _In_ BOOLEAN Shareable,
    _In_ PGPE_SERVICE_ROUTINE ServiceRoutine,
    _In_ PVOID ServiceContext,
    _In_ PVOID ObjectContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIVectorDisconnect(
    _In_ PVOID ObjectContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIVectorEnable(
    _In_ PDEVICE_OBJECT Context,
    _In_ PVOID ObjectContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIVectorDisable(
    _In_ PDEVICE_OBJECT Context,
    _In_ PVOID ObjectContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIVectorClear(
    _In_ PDEVICE_OBJECT Context,
    _In_ PVOID ObjectContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIRegisterForDeviceNotifications(
    _In_ PDEVICE_OBJECT Context,
    _In_ PDEVICE_NOTIFY_CALLBACK NotificationHandler,
    _In_ PVOID NotificationContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
ACPIUnregisterForDeviceNotifications(
    _In_ PDEVICE_OBJECT Context,
    _In_ PDEVICE_NOTIFY_CALLBACK NotificationHandler)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* FDO PNP FUNCTIOS *********************************************************/

NTSTATUS
NTAPI
ACPIRootIrpCompleteRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PRKEVENT Event = Context;
    KeSetEvent(Event, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
NTAPI
ACPIDevicePowerNotifyEvent(
    _In_ PVOID Param1,
    _In_ PVOID Context,
    _In_ ULONG Param3)
{
    PRKEVENT Event = Context;
    DPRINT("ACPIDevicePowerNotifyEvent: %p, %p, %X\n", Param1, Context, Param3);
    KeSetEvent(Event, 0, FALSE);
}

NTSTATUS
NTAPI
ACPIBuildSynchronizationRequest(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PVOID CallBack,
    _In_ PKEVENT Event,
    _In_ PLIST_ENTRY BuildDeviceList,
    _In_ BOOLEAN IsAddDpc)
{
    PACPI_BUILD_REQUEST BuildRequest;
    KIRQL BuildQueueIrql;
    KIRQL DeviceTreeIrql;

    DPRINT("ACPIBuildSynchronizationRequest: %p, %X\n", DeviceExtension, IsAddDpc);

    BuildRequest = ExAllocateFromNPagedLookasideList(&BuildRequestLookAsideList);
    if (!BuildRequest)
    {
        DPRINT1("ACPIBuildSynchronizationRequest: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &DeviceTreeIrql);

    if (!DeviceExtension->ReferenceCount)
    {
        DPRINT1("ACPIBuildSynchronizationRequest: STATUS_DEVICE_REMOVED\n");
        ExFreeToNPagedLookasideList(&BuildRequestLookAsideList, BuildRequest);
        return STATUS_DEVICE_REMOVED;
    }

    InterlockedIncrement(&DeviceExtension->ReferenceCount);

    RtlZeroMemory(BuildRequest, sizeof(ACPI_BUILD_REQUEST));

    BuildRequest->Signature = '_SGP';
    BuildRequest->Flags = 0x100A;
    BuildRequest->WorkDone = 3;
    BuildRequest->BuildReserved1 = 0;
    BuildRequest->DeviceExtension = DeviceExtension;
    BuildRequest->Status = 0;
    BuildRequest->CallBack = CallBack;
    BuildRequest->CallBackContext = Event;
    BuildRequest->Synchronize.ListHead = BuildDeviceList;
    BuildRequest->ListHeadForInsert = &AcpiBuildSynchronizationList;

    KeReleaseSpinLock(&AcpiDeviceTreeLock, DeviceTreeIrql);
    KeAcquireSpinLock(&AcpiBuildQueueLock, &BuildQueueIrql);

    InsertHeadList(&AcpiBuildQueueList, &BuildRequest->Link);

    if (IsAddDpc && !AcpiBuildDpcRunning)
        KeInsertQueueDpc(&AcpiBuildDpc, NULL, NULL);

    KeReleaseSpinLock(&AcpiBuildQueueLock, BuildQueueIrql);

    return STATUS_PENDING;
}

NTSTATUS
NTAPI
ACPIRootIrpQueryRemoveOrStopDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIRootIrpRemoveDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIRootIrpCancelRemoveOrStopDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIRootIrpStopDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
ACPIBuildMissingChildren(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
ACPIBuildFlushQueue(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, SynchronizationEvent, 0);

    Status = ACPIBuildSynchronizationRequest(DeviceExtension, ACPIDevicePowerNotifyEvent, &Event, &AcpiBuildDeviceList, TRUE);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = STATUS_SUCCESS;
    }

    return Status;
}

NTSTATUS
NTAPI
ACPIDetectCouldExtensionBeInRelation(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_RELATIONS DeviceRelation,
    _In_ BOOLEAN Param3,
    _In_ BOOLEAN Param4,
    _Out_ PDEVICE_OBJECT* OutPdoObject)
{
    UNICODE_STRING HardwareId;
    ULONG HardwareAddress;
    ULONG ix;
    BOOLEAN IsSuccess = FALSE;
    BOOLEAN IsAdr = FALSE;
    BOOLEAN IsHid = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPIDetectCouldExtensionBeInRelation: %p, %X, %X\n", DeviceExtension, Param3, Param4);

    ASSERT(OutPdoObject != NULL);

    if (!OutPdoObject)
    {
        DPRINT1("ACPIDetectCouldExtensionBeInRelation: STATUS_INVALID_PARAMETER_1\n");
        return STATUS_INVALID_PARAMETER_1;
    }

    *OutPdoObject = NULL;

    RtlZeroMemory(&HardwareId, sizeof(UNICODE_STRING));

    if (Param3 && !(DeviceExtension->Flags & 0x0000100000000000))
    {
        DPRINT1("ACPIDetectCouldExtensionBeInRelation: STATUS_OBJECT_NAME_NOT_FOUND\n");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (Param4 &&
        (!DeviceExtension->DeviceID || !(DeviceExtension->Flags & 0x0000200000000000)))
    {
        DPRINT1("ACPIDetectCouldExtensionBeInRelation: STATUS_OBJECT_NAME_NOT_FOUND\n");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (!DeviceRelation || !DeviceRelation->Count)
        return STATUS_SUCCESS;

    if (DeviceExtension->Flags & 0x2000100000000000)
    {
        IsAdr = TRUE;
        Status = ACPIGet(DeviceExtension, 'RDA_', 0x20040402, NULL, 0, NULL, NULL, (PVOID *)&HardwareAddress, NULL);
    }

    if (DeviceExtension->Flags & 0x0000A00000000000)
    {
        Status = ACPIGet(DeviceExtension, 'DIH_', 0x20080216, NULL, 0, NULL, NULL, (PVOID *)&HardwareId.Buffer, (ULONG *)&HardwareId.Length);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIDetectCouldExtensionBeInRelation: Status %X\n", Status);
            return Status;
        }

        HardwareId.MaximumLength = HardwareId.Length;
        IsHid = TRUE;
    }

    for (ix = 0; ix < DeviceRelation->Count; ix++)
    {
        IsSuccess = FALSE;

        if (IsHid)
        {
            DPRINT1("ACPIDetectCouldExtensionBeInRelation: FIXME\n");
            ASSERT(FALSE);
        }

        if (!IsSuccess && !IsAdr)
            continue;

        if (IsAdr)
        {
            IsSuccess = FALSE;

            DPRINT1("ACPIDetectCouldExtensionBeInRelation: FIXME\n");
            ASSERT(FALSE);
        }

        *OutPdoObject = DeviceRelation->Objects[ix];
        break;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
ACPIDetectPdoMatch(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_RELATIONS DeviceRelation)
{
    PDEVICE_OBJECT DeviceObject = NULL;
    BOOLEAN Result;
    NTSTATUS Status;

    PAGED_CODE();

    if (!(DeviceExtension->Flags & 0x0000000000000008) ||
        (DeviceExtension->Flags & 0x0200000000000000) ||
        DeviceExtension->DeviceObject)
    {
        return TRUE;
    }

    Status = ACPIDetectCouldExtensionBeInRelation(DeviceExtension, DeviceRelation, FALSE, TRUE, &DeviceObject);

    Result = (DeviceObject || !NT_SUCCESS(Status));

    return Result;
}

NTSTATUS
NTAPI
ACPIBuildPdo(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_OBJECT InPdo,
    _In_ BOOLEAN IsFilterDO)
{
    PDEVICE_OBJECT FilterDO = NULL;
    PDEVICE_OBJECT Pdo = NULL;
    ULONG ix;
    KIRQL Irql;
    NTSTATUS Status;

    DPRINT("ACPIBuildPdo: %p, %p, %X\n", DeviceExtension, InPdo, IsFilterDO);

    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_ACPI, FILE_AUTOGENERATED_DEVICE_NAME, FALSE, &Pdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIBuildPdo: Status %X\n", Status);
        return Status;
    }

    if (IsFilterDO)
    {
        if (!(DeviceExtension->Flags & 0x0000000000100000))
        {
            FilterDO = IoGetAttachedDeviceReference(InPdo);
            if (!FilterDO)
            {
                DPRINT1("ACPIBuildPdo: STATUS_NO_SUCH_DEVICE\n");
                IoDeleteDevice(Pdo);
                return STATUS_NO_SUCH_DEVICE;
            }
        }
        else
        {
            IsFilterDO = FALSE;
        }
    }

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &Irql);

    Pdo->DeviceExtension = DeviceExtension;
    DeviceExtension->DeviceObject = Pdo;
    DeviceExtension->PhysicalDeviceObject = Pdo;

    InterlockedIncrement(&DeviceExtension->ReferenceCount);

    ACPIInternalUpdateFlags(DeviceExtension, 0x00000000000001FF, TRUE);
    ACPIInternalUpdateFlags(DeviceExtension, 0x0000000000000020, FALSE);

    DeviceExtension->PreviousState = DeviceExtension->DeviceState;
    DeviceExtension->DeviceState = 0;
    DeviceExtension->DispatchTable = &AcpiPdoIrpDispatch;

    if (IsFilterDO)
    {
        DPRINT1("ACPIBuildPdo: FIXME\n");
        ASSERT(FALSE);
    }

    if (DeviceExtension->Flags & 0x0000001000000000)
    {
        DPRINT1("ACPIBuildPdo: FIXME\n");
        ASSERT(FALSE);
    }
    else if (DeviceExtension->Flags & 0x0000200000000000)
    {
        ASSERT(DeviceExtension->DeviceID);

        for (ix = 0; AcpiInternalDeviceTable[ix].StringId; ix++)
        {
            if (strstr(DeviceExtension->DeviceID, AcpiInternalDeviceTable[ix].StringId))
            {
                DeviceExtension->DispatchTable = AcpiInternalDeviceTable[ix].DispatchTable;
                break;
            }
        }
    }

    if ((DeviceExtension->Flags & 0x0000000000040000) && (DeviceExtension->Flags & 0x0008000000000000))
        FixedButtonDeviceObject = Pdo;

    KeReleaseSpinLock(&AcpiDeviceTreeLock, Irql);

    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    if (DeviceExtension->Flags & 0x0010000000000000)
        Pdo->Flags |= 8;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIDetectPdoDevices(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDEVICE_RELATIONS* OutDeviceRelation)
{
    PDEVICE_EXTENSION DeviceExtension;
    PDEVICE_EXTENSION Extension;
    PDEVICE_RELATIONS InDeviceRelation = NULL;
    PDEVICE_RELATIONS DeviceRelation;
    PDEVICE_OBJECT* Objects;
    PLIST_ENTRY Head;
    PLIST_ENTRY Entry;
    LONG RefCount;
    ULONG dummyData;
    ULONG count = 0;
    ULONG Size;
    ULONG ix;
    KIRQL Irql;
    BOOLEAN IsFound;
    NTSTATUS Status;

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    if (OutDeviceRelation && *OutDeviceRelation)
    {
        InDeviceRelation = *OutDeviceRelation;
        count = InDeviceRelation->Count;
    }

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &Irql);

    if (DeviceExtension->Flags & 0x0000020000000000)
    {
        DPRINT1("ACPIDetectPdoDevices: FIXME\n");
        ASSERT(FALSE);
    }

    KeReleaseSpinLock(&AcpiDeviceTreeLock, Irql);

    Status = ACPIBuildFlushQueue(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIDetectPdoDevices: Status %X\n", Status);
        return Status;
    }

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &Irql);

    if (IsListEmpty(&DeviceExtension->ChildDeviceList))
    {
        DPRINT1("ACPIDetectPdoDevices: FIXME\n");
        ASSERT(FALSE);
    }

    Head = &DeviceExtension->ChildDeviceList;
    Extension = CONTAINING_RECORD(DeviceExtension->ChildDeviceList.Flink, DEVICE_EXTENSION, SiblingDeviceList);

    while (TRUE)
    {
        InterlockedIncrement(&Extension->ReferenceCount);

        KeReleaseSpinLock(&AcpiDeviceTreeLock, Irql);

        if (!Extension)
            break;

        ACPIInternalUpdateFlags(Extension, 0x100, FALSE);

        Status = ACPIGet(Extension, 'ATS_', 0x20040802, NULL, 0, NULL, NULL, (PVOID *)&dummyData, NULL);
        if (NT_SUCCESS(Status))
        {
            if (!(Extension->Flags & 0x0002000000000002)) 
            {
                if (ACPIDetectPdoMatch(Extension, InDeviceRelation))
                {
                    if ((Extension->Flags & 0x0000000000000020) && Extension->DeviceObject)
                    {
                        if (InDeviceRelation && InDeviceRelation->Count)
                        {
                            ix = 0;
                            Objects = InDeviceRelation->Objects;
                            while (*Objects != Extension->DeviceObject)
                            {
                                Objects++;
                                ix++;
                                if (ix >= InDeviceRelation->Count)
                                {
                                    count++;
                                    ACPIInternalUpdateFlags(Extension, 0x100, 1);
                                    break;
                                }
                            }
                        }
                        else
                        {
                            count++;
                            ACPIInternalUpdateFlags(Extension, 0x100, 1);
                        }
                    }
                }
                else
                {
                    IsFound = FALSE;

                    if (!(DeviceExtension->Flags & 0x10))
                        IsFound = TRUE;

                    Status = ACPIBuildPdo(DeviceObject->DriverObject, Extension, DeviceExtension->PhysicalDeviceObject, IsFound);
                    if (NT_SUCCESS(Status))
                        count++;
                }
            }
        }

        KeAcquireSpinLock(&AcpiDeviceTreeLock, &Irql);
        RefCount = InterlockedDecrement(&Extension->ReferenceCount);

        Entry = Extension->SiblingDeviceList.Flink;
        if (Entry == Head)
        {
            if (!RefCount)
                ACPIInitDeleteDeviceExtension(Extension);

            KeReleaseSpinLock(&AcpiDeviceTreeLock, Irql);

            break;
        }

        Extension = CONTAINING_RECORD(Entry, DEVICE_EXTENSION, SiblingDeviceList);

        if (!RefCount)
        {
            DPRINT1("ACPIDetectPdoDevices: FIXME\n");
            ASSERT(FALSE);
        }
    }

    if (InDeviceRelation)
    {
        if (count == InDeviceRelation->Count)
            return STATUS_SUCCESS;
    }
    else if (!count)
    {
        return STATUS_SUCCESS;
    }

    Size = ((count + 1) * 4);

    DeviceRelation = ExAllocatePoolWithTag(NonPagedPool, Size, 'DpcA');
    if (!DeviceRelation)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(DeviceRelation, Size);

    if (InDeviceRelation)
    {
        DPRINT1("ACPIDetectPdoDevices: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        ix = 0;
    }

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &Irql);

    if (IsListEmpty(Head))
    {
        KeReleaseSpinLock(&AcpiDeviceTreeLock, Irql);
        ExFreePoolWithTag(DeviceRelation, 'DpcA');
        return STATUS_SUCCESS;
    }

    Extension = CONTAINING_RECORD(Head->Flink, DEVICE_EXTENSION, SiblingDeviceList);

    while (Extension)
    {
        if (Extension->Flags & 0x0000000000000020)
        {
            if (Extension->DeviceObject)
            {
                if (!(Extension->Flags & 0x0002000000000002))
                {
                    DeviceRelation->Objects[ix] = Extension->DeviceObject;
                    ACPIInternalUpdateFlags(Extension, 0x0000000000000100, TRUE);
                    ix++;
                }
            }
        }

        if (count == ix)
            break;

        if (Extension->SiblingDeviceList.Flink == &DeviceExtension->ChildDeviceList)
            break;

        Extension = CONTAINING_RECORD(Extension->SiblingDeviceList.Flink, DEVICE_EXTENSION, SiblingDeviceList);
    }

    count = ix;
    DeviceRelation->Count = count;

    KeReleaseSpinLock(&AcpiDeviceTreeLock, Irql);

    if (InDeviceRelation)
        ix = InDeviceRelation->Count;
    else
        ix = 0;

    for (; ix < count; ix++)
    {
        Status = ObReferenceObjectByPointer(DeviceRelation->Objects[ix], 0, NULL, KernelMode);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIDetectPdoDevices: Status %X\n", Status);
            DPRINT1("ACPIDetectPdoDevices: FIXME\n");
            ASSERT(FALSE);
        }
    }

    if (InDeviceRelation)
        ExFreePoolWithTag(*OutDeviceRelation, 'DpcA');

    *OutDeviceRelation = DeviceRelation;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIDetectDockDevices(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _Out_ PDEVICE_RELATIONS* OutDeviceRelation)
{
    PDEVICE_EXTENSION ProviderExtension;
    PDEVICE_RELATIONS OldDeviceRelation;
    PDEVICE_RELATIONS NewDeviceRelation = NULL;
    PDEVICE_OBJECT PrevDeviceObject;
    ACPI_EXT_LIST_ENUM_DATA ExtList;
    PVOID dummy;
    ULONG DeviceCount;
    ULONG count = 0;
    ULONG ix = 0;
    ULONG Size;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPIDetectDockDevices: DeviceExtension %p\n", DeviceExtension);

    if (OutDeviceRelation && *OutDeviceRelation)
    {
        OldDeviceRelation = *OutDeviceRelation;
        DeviceCount = OldDeviceRelation->Count;
    }
    else
    {
        OldDeviceRelation = NULL;
        DeviceCount = 0;
    }

    ExtList.List = &DeviceExtension->ChildDeviceList;
    ExtList.SpinLock = &AcpiDeviceTreeLock;
    ExtList.Offset = FIELD_OFFSET(DEVICE_EXTENSION, SiblingDeviceList);
    ExtList.ExtListEnum2 = 1;

    ProviderExtension = ACPIExtListStartEnum(&ExtList);

    while (ACPIExtListTestElement(&ExtList, NT_SUCCESS(Status)))
    {
        if (!ProviderExtension)
        {
            ACPIExtListExitEnumEarly(&ExtList);
            break;
        }

        if (ProviderExtension->Flags & 0x0200000000000000)
        {
            Status = ACPIGet(ProviderExtension, 'ATS_', 0x20040802, NULL, 0, NULL, NULL, &dummy, NULL);

            if (!(ProviderExtension->Flags & 0x0002000000000002))
            {
                if (!ProviderExtension->DeviceObject)
                {
                    Status = ACPIBuildPdo(DeviceExtension->DeviceObject->DriverObject,
                                          ProviderExtension,
                                          DeviceExtension->DeviceObject,
                                          FALSE);

                    if (!NT_SUCCESS(Status))
                    {
                        DPRINT1("ACPIDetectDockDevices: Status %X\n", Status);
                        ASSERT(ProviderExtension->DeviceObject == NULL);
                    }
                }

                if (ProviderExtension->DeviceObject)
                {
                    DPRINT1("ACPIDetectDockDevices: FIXME\n");
                    ASSERT(FALSE);
                }
            }
        }

        ProviderExtension = ACPIExtListEnumNext(&ExtList);
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIDetectDockDevices: Status %X\n", Status);
        return Status;
    }

    if (OldDeviceRelation && OldDeviceRelation->Count == DeviceCount)
        return STATUS_SUCCESS;

    if (!OldDeviceRelation && !DeviceCount)
        return STATUS_SUCCESS;

    Size = ((DeviceCount + 1) * 4);

    NewDeviceRelation = ExAllocatePoolWithTag(NonPagedPool, Size, 'DpcA');
    if (!NewDeviceRelation)
    {
        DPRINT1("ACPIDetectDockDevices: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(NewDeviceRelation, Size);

    if (OldDeviceRelation)
    {
        RtlCopyMemory(NewDeviceRelation->Objects, OldDeviceRelation->Objects, (OldDeviceRelation->Count * 4));
        count = OldDeviceRelation->Count;
    }

    ExtList.List = &DeviceExtension->ChildDeviceList;
    ExtList.SpinLock = &AcpiDeviceTreeLock;
    ExtList.Offset = FIELD_OFFSET(DEVICE_EXTENSION, SiblingDeviceList);
    ExtList.ExtListEnum2 = 2;

    ProviderExtension = ACPIExtListStartEnum(&ExtList);

    while (ACPIExtListTestElement(&ExtList, (DeviceCount != count)))
    {
        if (!(ProviderExtension->Flags & 0x0002000000000002) &&
            (ProviderExtension->Flags & 0x0200000000000000) &&
            ProviderExtension->DeviceObject)
        {
            NewDeviceRelation->Objects[count] = ProviderExtension->PhysicalDeviceObject;
            count++;
        }

        ProviderExtension = ACPIExtListEnumNext(&ExtList);
    }

    DeviceCount = count;
    NewDeviceRelation->Count = DeviceCount;

    if (OldDeviceRelation)
        ix = OldDeviceRelation->Count;
    else
        ix = 0;

    for (; ix < DeviceCount; ix++)
    {
        Status = ObReferenceObjectByPointer(NewDeviceRelation->Objects[ix], 0, NULL, KernelMode);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIDetectDockDevices: Status %X\n", Status);

            NewDeviceRelation->Count--;

            PrevDeviceObject = NewDeviceRelation->Objects[NewDeviceRelation->Count];
            NewDeviceRelation->Objects[NewDeviceRelation->Count] = NewDeviceRelation->Objects[ix];
            NewDeviceRelation->Objects[ix] = PrevDeviceObject;
        }
    }

    if (OldDeviceRelation)
        ExFreePool(*OutDeviceRelation);

    *OutDeviceRelation = NewDeviceRelation;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIRootIrpQueryBusRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _Out_ PDEVICE_RELATIONS* OutDeviceRelation)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPIRootIrpQueryBusRelations: %p, %p\n", DeviceObject, Irp);

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    if (!DeviceExtension->AcpiObject)
    {
        DPRINT1("ACPIRootIrpQueryBusRelations: STATUS_INVALID_PARAMETER\n");
        ASSERT(DeviceExtension->AcpiObject != NULL);
        return STATUS_INVALID_PARAMETER;
    }

    Status = ACPIDetectPdoDevices(DeviceObject, OutDeviceRelation);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIRootIrpQueryBusRelations: Status %X\n", Status);
        return Status;
    }

    Status = ACPIDetectDockDevices(DeviceExtension, OutDeviceRelation);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIRootIrpQueryBusRelations: Status %X\n", Status);
    }

    return Status;
}

NTSTATUS
NTAPI
ACPIDetectFilterMatch(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_RELATIONS DeviceRelation,
    _Out_ PDEVICE_OBJECT* OutPdo)
{
    ULONG ix;

    PAGED_CODE();
    DPRINT("ACPIDetectFilterMatch: %p, %p\n", DeviceExtension, DeviceRelation);

    ASSERT(OutPdo != NULL);

    if (!OutPdo)
    {
        DPRINT1("ACPIDetectFilterMatch: STATUS_INVALID_PARAMETER_1\n");
        return STATUS_INVALID_PARAMETER_1;
    }

    *OutPdo = NULL;

    if ((DeviceExtension->Flags & 0x0000000000000008) &&
        !(DeviceExtension->Flags & 0x0200000000000000) &&
        !DeviceExtension->DeviceObject)
    {
        DPRINT1("ACPIDetectFilterMatch: FIXME\n");
        ASSERT(FALSE);
    }

    if (!DeviceRelation)
        return STATUS_SUCCESS;

    if (!DeviceRelation->Count)
        return STATUS_SUCCESS;

    for (ix = 0; ix < DeviceRelation->Count; ix++)
    {
        if (DeviceExtension->PhysicalDeviceObject == DeviceRelation->Objects[ix])
            ACPIInternalUpdateFlags(DeviceExtension, 0x0000000000000100, TRUE);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIDetectFilterDevices(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PDEVICE_RELATIONS DeviceRelation)
{
    PDEVICE_EXTENSION DeviceExtension;
    PDEVICE_EXTENSION ChildDeviceExtension;
    PDEVICE_OBJECT PhysicalDeviceObject = NULL;
    PLIST_ENTRY Entry;
    PVOID dummy;
    LONG RefCount;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("ACPIDetectFilterDevices: %p, %p\n", DeviceObject, DeviceRelation);

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &OldIrql);

    if (DeviceExtension->Flags & 0x0000020000000000)
    {
        ACPIInternalUpdateFlags(DeviceExtension, 0x0000020000000000, TRUE);
        ACPIBuildMissingChildren(DeviceExtension);
    }

    KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);

    Status = ACPIBuildFlushQueue(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIDetectFilterDevices: Status %X\n", Status);
        return Status;
    }

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &OldIrql);

    if (IsListEmpty(&DeviceExtension->ChildDeviceList))
    {
        KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);
        return STATUS_SUCCESS;
    }

    ChildDeviceExtension = CONTAINING_RECORD(DeviceExtension->ChildDeviceList.Flink, DEVICE_EXTENSION, SiblingDeviceList);
    InterlockedIncrement(&ChildDeviceExtension->ReferenceCount);

    KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);

    while (ChildDeviceExtension)
    {
        Status = ACPIGet(ChildDeviceExtension, 'ATS_', 0x20040802, NULL, 0, NULL, NULL, &dummy, NULL);

        if (NT_SUCCESS(Status) && !(ChildDeviceExtension->Flags & 0x0002000000000002))
        {
            Status = ACPIDetectFilterMatch(ChildDeviceExtension, DeviceRelation, &PhysicalDeviceObject);

            if (NT_SUCCESS(Status))
            {
                if (PhysicalDeviceObject)
                {
                    DPRINT1("ACPIDetectFilterDevices: FIXME\n");
                    ASSERT(FALSE);
                }
            }
            else
            {
                DPRINT1("ACPIDetectFilterDevices: Status %X\n", Status);
            }
        }

        KeAcquireSpinLock(&AcpiDeviceTreeLock, &OldIrql);

        RefCount = InterlockedDecrement(&ChildDeviceExtension->ReferenceCount);

        if (ChildDeviceExtension->SiblingDeviceList.Flink == &DeviceExtension->ChildDeviceList)
        {
            if (!RefCount)
                ACPIInitDeleteDeviceExtension(ChildDeviceExtension);

            KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);

            break;
        }

        ChildDeviceExtension = CONTAINING_RECORD(ChildDeviceExtension->SiblingDeviceList.Flink, DEVICE_EXTENSION, SiblingDeviceList);

        if (!RefCount)
        {
            Entry = RemoveTailList(&ChildDeviceExtension->SiblingDeviceList);
            ACPIInitDeleteDeviceExtension(CONTAINING_RECORD(Entry, DEVICE_EXTENSION, SiblingDeviceList));
        }

        InterlockedIncrement(&ChildDeviceExtension->ReferenceCount);
        KeReleaseSpinLock(&AcpiDeviceTreeLock, OldIrql);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIRootIrpQueryDeviceRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_EXTENSION DeviceExtension;
    PDEVICE_RELATIONS DeviceRelation;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    BOOLEAN IsBusRelations = FALSE;
    NTSTATUS status;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPIRootIrpQueryDeviceRelations: %p, %p\n", DeviceObject, Irp);

    IoStack = Irp->Tail.Overlay.CurrentStackLocation;
    DeviceRelation = (PDEVICE_RELATIONS)Irp->IoStatus.Information;

    if (IoStack->Parameters.QueryDeviceRelations.Type == BusRelations)
    {
        IsBusRelations = TRUE;
        Status = ACPIRootIrpQueryBusRelations(DeviceObject, Irp, &DeviceRelation);
    }
    else
    {
        DPRINT1("ACPIRootIrpQueryDeviceRelations: Unhandled Type %X\n", IoStack->Parameters.QueryDeviceRelations.Type);
        Status = STATUS_NOT_SUPPORTED;
    }

    if (NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = (ULONG_PTR)DeviceRelation;
    }
    else if (Status != STATUS_NOT_SUPPORTED && !DeviceRelation)
    {
        DPRINT1("ACPIRootIrpQueryDeviceRelations: Status %X\n", Status);

        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, 0);
        return Status;
    }

    KeInitializeEvent(&Event, SynchronizationEvent, 0);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, ACPIRootIrpCompleteRoutine, &Event, TRUE, TRUE, TRUE);

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    Status = IoCallDriver(DeviceExtension->TargetDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    DeviceRelation = (PDEVICE_RELATIONS)Irp->IoStatus.Information;

    if ((NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED) && IsBusRelations)
    {
        status = ACPIDetectFilterDevices(DeviceObject, DeviceRelation);
        DPRINT("ACPIRootIrpQueryDeviceRelations: Status %X, status %X\n", Status, status);
    }

    IoCompleteRequest(Irp, 0);

    return Status;
}

NTSTATUS
NTAPI
ACPIRootIrpQueryInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    GUID* InterfaceType;
    ARBITER_INTERFACE Interface;
    UNICODE_STRING GuidString;
    CM_RESOURCE_TYPE ResourceType;
    ULONG Size;
    NTSTATUS Status;

    PAGED_CODE();

    IoStack = Irp->Tail.Overlay.CurrentStackLocation;
    InterfaceType = (PVOID)IoStack->Parameters.QueryInterface.InterfaceType;
    ResourceType = (CM_RESOURCE_TYPE)IoStack->Parameters.QueryInterface.InterfaceSpecificData;

    Status = RtlStringFromGUID(IoStack->Parameters.QueryInterface.InterfaceType, &GuidString);
    if (NT_SUCCESS(Status))
    {
        DPRINT("ACPIRootIrpQueryInterface: %X, '%wZ'\n", ResourceType, &GuidString);
        RtlFreeUnicodeString(&GuidString);
    }

    if (InterfaceType == &GUID_ARBITER_INTERFACE_STANDARD ||
        (RtlCompareMemory(InterfaceType, &GUID_ARBITER_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID)))
    {
        if (ResourceType == CmResourceTypeInterrupt)
        {
            if (IoStack->Parameters.QueryInterface.Size <= sizeof(ARBITER_INTERFACE))
                Size = IoStack->Parameters.QueryInterface.Size;
            else
                Size = sizeof(ARBITER_INTERFACE);

            Interface.Size = sizeof(ARBITER_INTERFACE);
            Interface.Version = 1;
            Interface.Flags = 0;
            Interface.ArbiterHandler = ArbArbiterHandler;
            Interface.Context = &AcpiArbiter;
            Interface.InterfaceReference = AcpiNullReference;
            Interface.InterfaceDereference = AcpiNullReference;

            RtlCopyMemory(IoStack->Parameters.QueryInterface.Interface, &Interface, Size);

            Irp->IoStatus.Status = STATUS_SUCCESS;
        }
    }

    Status = Irp->IoStatus.Status;

    DPRINT("ACPIRootIrpQueryInterface: Status %X\n", Status);

    return ACPIDispatchForwardIrp(DeviceObject, Irp);
}

NTSTATUS
NTAPI
ACPISystemPowerGetSxD(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ SYSTEM_POWER_STATE SystemState,
    _Out_ DEVICE_POWER_STATE* OutDeviceState)
{
    DEVICE_POWER_STATE DeviceState = PowerDeviceUnspecified;
    ULONG DataBuff;
    NTSTATUS Status;

    PAGED_CODE();

    *OutDeviceState = PowerDeviceUnspecified;

    if ((DeviceExtension->Flags & 0x0008000000000000) ||
        (DeviceExtension->Flags & 0x0002000000000000))
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Status = ACPIGet(DeviceExtension, AcpiSxDMethodTable[SystemState], 0x20040002, NULL, 0, NULL, NULL, (PVOID *)&DataBuff, NULL);
    if (NT_SUCCESS(Status))
    {
        if (DataBuff < 4)
            DeviceState = DevicePowerStateTranslation[DataBuff];

        *OutDeviceState = DeviceState;

        return Status;
    }

    if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DPRINT1("ACPISystemPowerGetSxD: Cannot run _S%cD - %X\n", (SystemState ? ((SystemState - 1) + '0') : 'w'), Status);
        return Status;
    }

    if (SystemState == PowerSystemSleeping1 &&
        (DeviceExtension->Flags & 0x0000A00000000000) &&
        (DeviceExtension->Flags & 0x0000000002000000))
    {
        *OutDeviceState = PowerDeviceD1;
        Status = STATUS_SUCCESS;
    }

    return Status;
}

NTSTATUS
NTAPI
ACPISystemPowerProcessSxD(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_POWER_STATE PowerMatrix,
    _Out_ BOOLEAN* OutMatchFound)
{
    SYSTEM_POWER_STATE SystemState;
    DEVICE_POWER_STATE DeviceState;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(OutMatchFound);
    *OutMatchFound = FALSE;

    for (SystemState = PowerSystemWorking; SystemState < PowerSystemMaximum; SystemState++)
    {
        if (!(AcpiSupportedSystemStates & (1 << SystemState)))
        {
            PowerMatrix[SystemState] = PowerDeviceUnspecified;
            continue;
        }

        Status = ACPISystemPowerGetSxD(DeviceExtension, SystemState, &DeviceState);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
            continue;

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPISystemPowerProcessSxD: Cannot Evaluate _SxD (%X)\n", Status);
            continue;
        }

        *OutMatchFound = TRUE;

        if (DeviceState > PowerMatrix[SystemState])
            PowerMatrix[SystemState] = DeviceState;
    }

    return STATUS_SUCCESS;
}

SYSTEM_POWER_STATE
NTAPI
ACPISystemPowerDetermineSupportedSystemState(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ DEVICE_POWER_STATE DeviceState)
{
    SYSTEM_POWER_STATE RetState = PowerSystemMaximum;
    PACPI_DEVICE_POWER_NODE Node;
  
    if (DeviceState == PowerDeviceD3)
    {
        RetState = PowerDeviceUnspecified;
        return RetState;
    }

    Node = DeviceExtension->PowerInfo.PowerNode[DeviceState];
    if (!Node)
    {
        RetState = PowerDeviceUnspecified;
        return RetState;
    }

    do
    {
        if (Node->SystemState < RetState)
            RetState = Node->SystemState;

        Node = Node->Next;
    }
    while (Node);

    if (RetState == PowerSystemMaximum)
        RetState = PowerDeviceUnspecified;

    return RetState;
}

NTSTATUS
NTAPI
ACPISystemPowerDetermineSupportedDeviceStates(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ SYSTEM_POWER_STATE SystemState,
    _Out_ DEVICE_POWER_STATE* OutSupportedDeviceStates)
{
    ACPI_EXT_LIST_ENUM_DATA ExtList;
    SYSTEM_POWER_STATE systemState;
    DEVICE_POWER_STATE DeviceState;
    PDEVICE_EXTENSION Extension;
    KIRQL Irql;
    BOOLEAN Result;
    NTSTATUS Status;

    ASSERT(SystemState >= PowerSystemWorking && SystemState <= PowerSystemShutdown);
    ASSERT(OutSupportedDeviceStates != NULL);

    ExtList.List = &DeviceExtension->ChildDeviceList;
    ExtList.SpinLock = &AcpiDeviceTreeLock;
    ExtList.Offset = FIELD_OFFSET(DEVICE_EXTENSION, SiblingDeviceList);
    ExtList.ExtListEnum2 = 1;

    Extension = ACPIExtListStartEnum(&ExtList);

    for (Result = ACPIExtListTestElement(&ExtList, TRUE);
         Result;
         Result = ACPIExtListTestElement(&ExtList, NT_SUCCESS(Status)))
    {
        Status = ACPISystemPowerDetermineSupportedDeviceStates(Extension, SystemState, OutSupportedDeviceStates);
        if (!NT_SUCCESS(Status))
        {
            Extension = ACPIExtListEnumNext(&ExtList);
            continue;
        }

        Status = ACPISystemPowerGetSxD(Extension, SystemState, &DeviceState);
        if (NT_SUCCESS(Status))
        {
            *OutSupportedDeviceStates |= (1 << DeviceState);
            DPRINT("ACPISystemPowerDetermineSupportedDeviceStates: S%x->D%x\n", (SystemState - 1), (DeviceState - 1));
            Extension = ACPIExtListEnumNext(&ExtList);
            continue;
        }

        if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            DPRINT("ACPISystemPowerDetermineSupportedDeviceStates: Status %X\n", Status);
            Extension = ACPIExtListEnumNext(&ExtList);
            continue;
        }

        Status = STATUS_SUCCESS;

        KeAcquireSpinLock(&AcpiPowerLock, &Irql);
        DeviceState = PowerDeviceD0;

        do
        {
            systemState = ACPISystemPowerDetermineSupportedSystemState(Extension, DeviceState);
            if (systemState >= SystemState)
            {
                *OutSupportedDeviceStates |= (1 << DeviceState);
                DPRINT("ACPISystemPowerDetermineSupportedDeviceStates: PR%X maps to S%X, so S%X->D%X\n", (DeviceState - 1), (systemState - 1), (SystemState - 1), (DeviceState - 1));
            }

            DeviceState++;
        }
        while (DeviceState <= PowerDeviceD2);

        KeReleaseSpinLock(&AcpiPowerLock, Irql);
        Extension = ACPIExtListEnumNext(&ExtList);
     }
 
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPISystemPowerProcessRootMapping(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_POWER_STATE PowerMatrix)
{
    SYSTEM_POWER_STATE SystemState;
    DEVICE_POWER_STATE DeviceState;
    DEVICE_POWER_STATE SupportedDeviceStates;
    NTSTATUS Status;

    PAGED_CODE();

    SystemState = PowerSystemSleeping1;
    do
    {
        if ((1 << SystemState) & AcpiSupportedSystemStates)
        {
            SupportedDeviceStates = 0x10;

            Status = ACPISystemPowerDetermineSupportedDeviceStates(DeviceExtension, SystemState, &SupportedDeviceStates);
            if (NT_SUCCESS(Status))
            {
                for (DeviceState = PowerMatrix[SystemState]; DeviceState <= PowerDeviceD3; DeviceState++)
                {
                    if ((1 << DeviceState) & SupportedDeviceStates)
                    {
                        PowerMatrix[SystemState] = DeviceState;
                        break;
                    }
                }
            }
            else
            {
                DPRINT1("ACPISystemPowerProcessRootMapping: Cannot determine D state for S%x - %X\n", (SystemState - 1), Status);
                PowerMatrix[SystemState] = PowerDeviceD3;
            }
        }
        SystemState++;
    }
    while (SystemState <= PowerSystemShutdown);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPISystemPowerInitializeRootMapping(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_CAPABILITIES Capabilities)
{
    PDEVICE_POWER_STATE OutDeviceState;
    DEVICE_POWER_STATE deviceStates[7];
    ULONG ix;
    KIRQL Irql;
    BOOLEAN dummyMatchFound;
    NTSTATUS Status;

    if (DeviceExtension->Flags & 0x0400000000000000)
        goto Finish;

    if (DeviceExtension->DeviceState != Started)
        goto Finish;

    RtlCopyMemory(deviceStates, DeviceExtension->PowerInfo.DevicePowerMatrix, sizeof(deviceStates));

    deviceStates[1] = PowerDeviceD0;
    OutDeviceState = &Capabilities->DeviceState[2];

    ix = 2;
    do
    {
        if (*OutDeviceState)
            deviceStates[ix] = *OutDeviceState;
        ix++;
        OutDeviceState++;
    }
    while (ix <= 6);

    Status = ACPISystemPowerProcessSxD(DeviceExtension, deviceStates, &dummyMatchFound);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPISystemPowerInitializeRootMapping: Status %X\n", Status);
        return Status;
    }

    if (deviceStates[6] == PowerDeviceUnspecified)
        deviceStates[6] = PowerDeviceD3;

    Status = ACPISystemPowerProcessRootMapping(DeviceExtension, deviceStates);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPISystemPowerInitializeRootMapping: Status %X\n", Status);
        goto Finish;
    }

    ACPIInternalUpdateFlags(DeviceExtension, 0x0400000000000000, FALSE);

    KeAcquireSpinLock(&AcpiPowerLock, &Irql);
    RtlCopyMemory(DeviceExtension->PowerInfo.DevicePowerMatrix, deviceStates, sizeof(DeviceExtension->PowerInfo.DevicePowerMatrix));
    KeReleaseSpinLock(&AcpiPowerLock, Irql);

Finish:

    RtlCopyMemory(Capabilities->DeviceState, DeviceExtension->PowerInfo.DevicePowerMatrix, sizeof(Capabilities->DeviceState));

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIRootIrpQueryCapabilities(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_EXTENSION DeviceExtension;
    PDEVICE_CAPABILITIES Capabilities;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;

    PAGED_CODE();

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    DPRINT("ACPIRootIrpQueryCapabilities: %p (%p), %p\n", DeviceObject, DeviceExtension, Irp);

    KeInitializeEvent(&Event, SynchronizationEvent, 0);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, ACPIRootIrpCompleteRoutine, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(DeviceExtension->TargetDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    if (NT_SUCCESS(Status))
    {
        IoStack = Irp->Tail.Overlay.CurrentStackLocation;
        Capabilities = IoStack->Parameters.DeviceCapabilities.Capabilities;

        Capabilities->LockSupported = 0;
        Capabilities->EjectSupported = 0;
        Capabilities->Removable = 0;
        Capabilities->UniqueID = 1;
        Capabilities->RawDeviceOK = 0;
        Capabilities->SurpriseRemovalOK = 0;

        Capabilities->UINumber = 0xFFFFFFFF;
        Capabilities->Address = 0xFFFFFFFF;

        Capabilities->SystemWake = 0;
        Capabilities->DeviceWake = 0;

        Status = ACPISystemPowerInitializeRootMapping(DeviceExtension, Capabilities);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIRootIrpQueryCapabilities: %p (%p), %p - %X\n", DeviceObject, DeviceExtension, Irp, Status);
        }
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, 0);

    DPRINT("ACPIRootIrpQueryCapabilities: %p (%p), %p - %X\n", DeviceObject, DeviceExtension, Irp, Status);

    return Status;
}

NTSTATUS
NTAPI
ACPIFilterIrpDeviceUsageNotification(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* FDO Power FUNCTIOS *******************************************************/

NTSTATUS
NTAPI
ACPIWakeWaitIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDispatchForwardPowerIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIRootIrpSetPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIRootIrpQueryPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* PDO PNP FUNCTIOS *********************************************************/

NTSTATUS
NTAPI
ACPIBusIrpQueryRemoveOrStopDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpRemoveDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpCancelRemoveOrStopDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpStopDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpQueryDeviceRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
IsPciBusAsyncWorker(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ NTSTATUS InStatus,
    _In_ PVOID Param3,
    _In_ PVOID InContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
IsPciBusAsync(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ PVOID CallBack,
    _In_ PVOID CallBackContext,
    _In_ BOOLEAN* OutIsBusAsync)
{
    PDEVICE_EXTENSION DeviceExtension;
    PIS_PCI_BUS_CONTEXT Context;
    NTSTATUS Status;

    DPRINT("IsPciBusAsync: NsObject %p\n", NsObject);

    DeviceExtension = NsObject->Context;
    if (DeviceExtension)
    {
        ASSERT(DeviceExtension->Signature == '_SGP');//ACPI_SIGNATURE

        if (DeviceExtension->Flags & 0x0000000002000000)
        {
            *OutIsBusAsync = TRUE;
            return STATUS_SUCCESS;
        }
    }

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(IS_PCI_BUS_CONTEXT), 'FpcA');
    if (!Context)
    {
        DPRINT1("IsPciBusAsync: STATUS_INSUFFICIENT_RESOURCES\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Context, sizeof(IS_PCI_BUS_CONTEXT));

    Context->NsObject = NsObject;
    Context->RefCount = -1;
    Context->CallBack = CallBack;
    Context->CallBackContext = CallBackContext;
    Context->OutIsBusAsync = OutIsBusAsync;

    *OutIsBusAsync = FALSE;

    Status = IsPciBusAsyncWorker(NsObject, STATUS_SUCCESS, NULL, Context);

    return Status;
}

BOOLEAN
NTAPI
IsNsobjPciBus(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject)
{
    PDEVICE_EXTENSION DeviceExtension;
    ACPI_WAIT_CONTEXT WaitContext;
    BOOLEAN IsBusAsync = FALSE;
    NTSTATUS Status;

    DPRINT("IsPciBusExtension: NsObject %p\n", NsObject);
    PAGED_CODE();

    DeviceExtension = NsObject->Context;
    if (DeviceExtension)
    {
        ASSERT(DeviceExtension->Signature == '_SGP'); // ACPI_SIGNATURE

        if (DeviceExtension->Flags & 0x0000000002000000)
            return TRUE;
    }

    KeInitializeEvent(&WaitContext.Event, SynchronizationEvent, FALSE);

    WaitContext.Status = STATUS_NOT_FOUND;

    Status = IsPciBusAsync(NsObject, AmlisuppCompletePassive, &WaitContext, &IsBusAsync);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&WaitContext.Event, Executive, KernelMode, FALSE, NULL);
    }

    return IsBusAsync;
}

BOOLEAN
NTAPI
IsPciBusExtension(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    DPRINT("IsPciBusExtension: DeviceExtension %p\n", DeviceExtension);
    PAGED_CODE();
    return IsNsobjPciBus(DeviceExtension->AcpiObject);
}

BOOLEAN
NTAPI
IsPciBus(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_EXTENSION DeviceExtension;

    DPRINT("IsPciBus: DeviceObject %p\n", DeviceObject);
    PAGED_CODE();

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    return IsPciBusExtension(DeviceExtension);
}

NTSTATUS
NTAPI
TranslateEjectInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IoStack;
    PIO_RESOURCE_DESCRIPTOR IoDescriptor;
    PTRANSLATOR_INTERFACE TranslateInterface;
    PIO_RESOURCE_REQUIREMENTS_LIST IoResource = NULL;
    PVOID Data = NULL;
    PHYSICAL_ADDRESS MinimumAddress;
    ULONG ix;
    ULONG DataLen;
    NTSTATUS Status;

    DPRINT("TranslateEjectInterface: DeviceObject %p\n", DeviceObject);
    PAGED_CODE();

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);
    ASSERT(DeviceExtension);
    ASSERT(DeviceExtension->AcpiObject);

    IoStack = Irp->Tail.Overlay.CurrentStackLocation;
    ASSERT(IoStack->Parameters.QueryInterface.Size >= sizeof(TRANSLATOR_INTERFACE));

    TranslateInterface = (PVOID)IoStack->Parameters.QueryInterface.Interface;
    ASSERT(TranslateInterface != NULL);

    Status = ACPIGet(DeviceExtension, 'SRC_', 0x20010008, NULL, 0, NULL, NULL, &Data, &DataLen);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("TranslateEjectInterface: Status %X\n", Status);
        Status = Irp->IoStatus.Status;
        goto Exit;
    }

    Status = PnpBiosResourcesToNtResources(Data, 1, &IoResource);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("TranslateEjectInterface: Status %X\n", Status);
        goto Exit;
    }

    if (!IoResource || !IoResource->List[0].Count)
    {
        DPRINT1("TranslateEjectInterface: Status %X\n", Status);
        Status = Irp->IoStatus.Status;
        goto Exit;
    }

    for (ix = 0; ix < IoResource->List[0].Count; ix++)
    {
        IoDescriptor = &IoResource->List[0].Descriptors[ix];

        if (IoResource->List[0].Descriptors[ix].Type == 0x81 &&
            (IoResource->List[0].Descriptors[ix].Flags & 0x6000))
        {
            ASSERT(ix != 0);

            MinimumAddress.LowPart  = IoDescriptor->u.DevicePrivate.Data[1];
            MinimumAddress.HighPart = IoDescriptor->u.DevicePrivate.Data[2];

            if (IoDescriptor->u.DevicePrivate.Data[0] != IoResource->List[0].Descriptors[ix - 1].Type ||
                (MinimumAddress.QuadPart != IoDescriptor[-1].u.Generic.MinimumAddress.QuadPart))
            {
                DPRINT1("TranslateEjectInterface: FIXME\n");
                ASSERT(FALSE);
                break;
            }
        }
    }

    Status = Irp->IoStatus.Status;

Exit:

    if (Data)
        ExFreePool(Data);

    if (IoResource)
        ExFreePool(IoResource);

    return Status;
}

VOID
NTAPI
PciInterfacePinToLine(
    _In_ PVOID Context,
    _In_ PPCI_COMMON_CONFIG PciData)
{
    ;
}

VOID
NTAPI
PciInterfaceLineToPin(
    _In_ PVOID Context,
    _In_ PPCI_COMMON_CONFIG PciNewData,
    _In_ PPCI_COMMON_CONFIG PciOldData)
{
    ;
}

NTSTATUS
NTAPI
PciBusEjectInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_RESOURCE_REQUIREMENTS_LIST IoResource = NULL;
    PPCI_BUS_INTERFACE_STANDARD Interface;
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;
    BOOLEAN IsFound = FALSE;
    AMLI_OBJECT_DATA Data;
    ULONG MinBusNumber;
    ULONG ix;

    DPRINT("PciBusEjectInterface: DeviceObject %p\n", DeviceObject);
    PAGED_CODE();

    ASSERT(PmHalDispatchTable->Function[6]);//HalPciInterfaceReadConfig
    ASSERT(PmHalDispatchTable->Function[7]);//HalPciInterfaceWriteConfig

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);
    ASSERT(DeviceExtension);
    ASSERT(DeviceExtension->AcpiObject);

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    ASSERT(IoStack->Parameters.QueryInterface.Size >= sizeof(PCI_BUS_INTERFACE_STANDARD));

    Interface = (PPCI_BUS_INTERFACE_STANDARD)IoStack->Parameters.QueryInterface.Interface;
    ASSERT(Interface);

    Status = ACPIGet(DeviceExtension, 'SRC_', 0x20020000, NULL, 0, NULL, NULL, (PVOID *)&Data, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PciBusEjectInterface: Status %X\n", Status);
        goto Finish;
    }

    ASSERT(Data.DataType == 3);//OBJTYPE_BUFFDATA

    Status = PnpBiosResourcesToNtResources(Data.DataBuff, 1, &IoResource);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PciBusEjectInterface: Status %X\n", Status);
        AMLIFreeDataBuffs(&Data, 1);
        goto Finish;
    }

    if (!IoResource)
    {
        AMLIFreeDataBuffs(&Data, 1);
        goto Finish;
    }

    ASSERT(IoResource->AlternativeLists == 1);

    for (ix = 0; ix < IoResource->List[0].Count; ix++)
    {
        if (IoResource->List[0].Descriptors[ix].Type == CmResourceTypeBusNumber)
            break;
    }

    if (ix != IoResource->List[0].Count)
    {
        MinBusNumber = IoResource->List[0].Descriptors[ix].u.BusNumber.MinBusNumber;
        IsFound = TRUE;
    }

    AMLIFreeDataBuffs(&Data, 1);

Finish:

    if (!IsFound)
        MinBusNumber = 0;

    Interface->Size = sizeof(PCI_BUS_INTERFACE_STANDARD);
    Interface->Version = 1;
    Interface->Context = (PVOID)MinBusNumber;
    Interface->InterfaceReference = AcpiNullReference;
    Interface->InterfaceDereference = AcpiNullReference;
    Interface->ReadConfig = PmHalDispatchTable->Function[6];//HalPciInterfaceReadConfig;
    Interface->WriteConfig = PmHalDispatchTable->Function[7];//HalPciInterfaceWriteConfig;
    Interface->PinToLine = PciInterfacePinToLine;
    Interface->LineToPin = PciInterfaceLineToPin;

    if (IoResource)
        ExFreePool(IoResource);

    Irp->IoStatus.Status = STATUS_SUCCESS;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIBusIrpQueryInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_EXTENSION DeviceExtension;
    PACPI_INTERFACE_STANDARD Interface;
    PIO_STACK_LOCATION IoStack;
    GUID* InterfaceType;
    UNICODE_STRING GuidString;
    CM_RESOURCE_TYPE ResourceType;
    ULONG Size;
    NTSTATUS Status;

    DPRINT("ACPIBusIrpQueryInterface: DeviceObject %p\n", DeviceObject);
    PAGED_CODE();

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);
    IoStack = Irp->Tail.Overlay.CurrentStackLocation;

    InterfaceType = (PVOID)IoStack->Parameters.QueryInterface.InterfaceType;
    ResourceType = (CM_RESOURCE_TYPE)IoStack->Parameters.QueryInterface.InterfaceSpecificData;

    Status = RtlStringFromGUID(InterfaceType, &GuidString);
    if (NT_SUCCESS(Status))
    {
        DPRINT("ACPIBusIrpQueryInterface: %X, %X, '%wZ'\n", IoStack->MinorFunction, ResourceType, &GuidString);
        RtlFreeUnicodeString(&GuidString);
    }

    Status = STATUS_NOT_SUPPORTED;

    if (InterfaceType == &GUID_ACPI_INTERFACE_STANDARD ||
        RtlCompareMemory(InterfaceType, &GUID_ACPI_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
    {
        DPRINT("ACPIBusIrpQueryInterface: GUID_ACPI_INTERFACE_STANDARD\n");

        if (IoStack->Parameters.QueryInterface.Size <= sizeof(ACPI_INTERFACE_STANDARD))
            Size = IoStack->Parameters.QueryInterface.Size;
        else
            Size = sizeof(ACPI_INTERFACE_STANDARD);

        Interface = (PVOID)IoStack->Parameters.QueryInterface.Interface;
        RtlCopyMemory(Interface, &ACPIInterfaceTable, Size);

        if (Size > 8) // FIXME
            Interface->Context = DeviceObject;

        Irp->IoStatus.Status = Status = STATUS_SUCCESS;

        goto Exit;
    }

    if (InterfaceType == &GUID_TRANSLATOR_INTERFACE_STANDARD ||
        RtlCompareMemory(InterfaceType, &GUID_TRANSLATOR_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
    {
        DPRINT("ACPIBusIrpQueryInterface: GUID_TRANSLATOR_INTERFACE_STANDARD. ResourceType %X\n", ResourceType);

        if (ResourceType == CmResourceTypeInterrupt)
        {
            if (IsPciBus(DeviceObject))
            {
                DPRINT1("ACPIBusIrpQueryInterface: FIXME\n");
                ASSERT(FALSE);
                //SmashInterfaceQuery(Irp);
            }

            Status = Irp->IoStatus.Status;
        }
        else
        {
            if ((ResourceType == CmResourceTypePort || ResourceType == CmResourceTypeMemory) && IsPciBus(DeviceObject))
            {
                Status = TranslateEjectInterface(DeviceObject, Irp);
            }

            if (Status == STATUS_NOT_SUPPORTED)
                Status = Irp->IoStatus.Status;
            else
                Irp->IoStatus.Status = Status;
        }

        goto Exit;
    }

    if (InterfaceType == &GUID_PCI_BUS_INTERFACE_STANDARD ||
        RtlCompareMemory(InterfaceType, &GUID_PCI_BUS_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
    {
        if (!IsPciBus(DeviceObject))
        {
            Status = Irp->IoStatus.Status;
            goto Exit;
        }

        Status = PciBusEjectInterface(DeviceObject, Irp);

        if (Status == STATUS_NOT_SUPPORTED)
            Status = Irp->IoStatus.Status;
        else
            Irp->IoStatus.Status = Status;

        goto Exit;
    }

    if (InterfaceType == &GUID_BUS_INTERFACE_STANDARD ||
        RtlCompareMemory(InterfaceType, &GUID_BUS_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
    {
        DPRINT("ACPIBusIrpQueryInterface: GUID_BUS_INTERFACE_STANDARD\n");

        Irp->IoStatus.Status = STATUS_NOINTERFACE;

        if (DeviceExtension->ParentExtension)
        {
            if (DeviceExtension->ParentExtension->DeviceObject)
            {
                DPRINT1("ACPIBusIrpQueryInterface: FIXME\n");
                ASSERT(FALSE);
                Irp->IoStatus.Status = 0;//ACPIInternalSendSynchronousIrp(DeviceExtension->ParentExtension->DeviceObject, IoStack, 0);
            }
        }

        Status = Irp->IoStatus.Status;

        goto Exit;
    }

    if (InterfaceType == &GUID_ARBITER_INTERFACE_STANDARD ||
        RtlCompareMemory(InterfaceType, &GUID_ARBITER_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
    {
        DPRINT("ACPIBusIrpQueryInterface: GUID_ARBITER_INTERFACE_STANDARD\n");

        if ((DeviceExtension->Flags & 0x0000002000000000) && DeviceExtension->Module.ArbitersNeeded)
        {
            DPRINT1("ACPIBusIrpQueryInterface: FIXME\n");
            ASSERT(FALSE);
            Irp->IoStatus.Status = Status = 0;//AcpiArblibEjectInterface(DeviceObject, Irp);

            if (Status == STATUS_NOT_SUPPORTED)
                Status = Irp->IoStatus.Status;

            goto Exit;
        }

        Status = Irp->IoStatus.Status;
    }

Exit:

    IoCompleteRequest(Irp, 0);
    return Status;
}

NTSTATUS
NTAPI
ACPIInternalSendSynchronousIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIO_STACK_LOCATION InIoStack,
    _In_ ULONG_PTR* OutInformation)
{
    PDEVICE_OBJECT AttachedDevice;
    PIO_STACK_LOCATION IoStack;
    IO_STATUS_BLOCK ioStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    PAGED_CODE();

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    AttachedDevice = IoGetAttachedDeviceReference(DeviceObject);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, AttachedDevice, NULL, 0, NULL, &Event, &ioStatus);
    if (!Irp)
    {
        DPRINT1("ACPIInternalSendSynchronousIrp: STATUS_INSUFFICIENT_RESOURCES\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;

    IoStack = IoGetNextIrpStackLocation(Irp);
    if (!IoStack)
    {
        DPRINT1("ACPIInternalSendSynchronousIrp: STATUS_INVALID_PARAMETER\n");
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    *IoStack = *InIoStack;

    IoSetCompletionRoutine(Irp, NULL, NULL, FALSE, FALSE, FALSE);

    Status = IoCallDriver(AttachedDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = ioStatus.Status;
    }

    if (NT_SUCCESS(Status) && OutInformation)
        *OutInformation = ioStatus.Information;

Exit:

    DPRINT("ACPIInternalSendSynchronousIrp: DeviceObject %p, Status %X\n", DeviceObject, Status);

    ObDereferenceObject(AttachedDevice);

    return Status;
}

NTSTATUS
NTAPI
ACPIInternalGetDeviceCapabilities(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PDEVICE_CAPABILITIES Capabilities)
{
    IO_STACK_LOCATION ioStack;
    ULONG_PTR dummyInformation;

    PAGED_CODE();

    ASSERT(DeviceObject != NULL);
    ASSERT(Capabilities != NULL);

    RtlZeroMemory(&ioStack, sizeof(IO_STACK_LOCATION));
    RtlZeroMemory(Capabilities, sizeof(DEVICE_CAPABILITIES));

    Capabilities->Address = 0xFFFFFFFF;
    Capabilities->UINumber = 0xFFFFFFFF;

    ioStack.MajorFunction = IRP_MJ_PNP;
    ioStack.MinorFunction = IRP_MN_QUERY_CAPABILITIES;

    ioStack.Parameters.DeviceCapabilities.Capabilities = Capabilities;

    Capabilities->Size = sizeof(*Capabilities);
    Capabilities->Version = 1;

    return ACPIInternalSendSynchronousIrp(DeviceObject, &ioStack, (ULONG_PTR *)&dummyInformation);
}

NTSTATUS
NTAPI
ACPIDevicePowerDetermineSupportedDeviceStates(
     _In_ PDEVICE_EXTENSION DeviceExtension,
     _In_ ULONG* OutSupportedPrStates,
     _In_ ULONG* OutSupportedPsStates)
{
    ULONG PsNameSegment[4] = {'0SP_', '1SP_', '2SP_', '3SP_'};
    ULONG PrNameSegment[3] = {'0RP_', '1RP_', '2RP_'};
    ULONG PsStates = 0;
    ULONG PrStates = 0;
    ULONG Shift;
    ULONG ix;
    ULONG States;

    PAGED_CODE();

    ASSERT(DeviceExtension != NULL);
    ASSERT(OutSupportedPrStates != NULL);
    ASSERT(OutSupportedPsStates != NULL);

    *OutSupportedPrStates = 0;
    *OutSupportedPsStates = 0;

    if (DeviceExtension->Flags & 0x0008000000000000)
    {
        *OutSupportedPrStates = PrStates;
        *OutSupportedPsStates = 0x12;
        return STATUS_SUCCESS;
    }

    Shift = 1;
    for (ix = 0; Shift <= 4; ix++, Shift++)
    {
        if (ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, PsNameSegment[ix]))
            PsStates |= (1 << Shift);
    }

    Shift = 1;
    for (ix = 0; Shift <= 3; ix++, Shift++)
    {
        if (ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, PrNameSegment[ix]))
            PrStates |= ((1 << Shift) | 0x10);
    }

    States = (PrStates | PsStates);

    if (!States)
        return STATUS_SUCCESS;

    if (!(States & 2))
    {
        DPRINT("ACPIDevicePowerDetermineSupportedDeviceStates: does not support D0 power state!\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 0xD, (ULONG_PTR)DeviceExtension, (PrStates ? '0RP_' : '0SP_'), 0);
    }
    else if (!(States & 0x10))
    {
        DPRINT("ACPIDevicePowerDetermineSupportedDeviceStates: does not support D3 power state!\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 0xD, (ULONG_PTR)DeviceExtension, '3SP_', 0);
    }
    else
    {
        if (PrStates && PsStates && PrStates != PsStates)
        {
            DPRINT("ACPIDevicePowerDetermineSupportedDeviceStates: has mismatch between power plane and power source information!\n");
            PrStates &= PsStates;
            PsStates &= PrStates;
        }

        *OutSupportedPrStates = PrStates;
        *OutSupportedPsStates = PsStates;
    }

    return STATUS_SUCCESS;
}

DEVICE_POWER_STATE
NTAPI
ACPISystemPowerDetermineSupportedDeviceWakeState(
     _In_ PDEVICE_EXTENSION DeviceExtension)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

NTSTATUS
NTAPI
ACPISystemPowerUpdateWakeCapabilitiesForPDOs(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_CAPABILITIES Capabilities,
    _In_ PDEVICE_CAPABILITIES OutCapabilities,
    _In_ DEVICE_POWER_STATE* States,
    _In_ ULONG* OutDeviceWakeBit,
    _In_ SYSTEM_POWER_STATE* OutSystemWakeLevel,
    _In_ DEVICE_POWER_STATE* OutDeviceWakeLevel,
    _In_ DEVICE_POWER_STATE* OutWakeLevel)
{
    DEVICE_POWER_STATE DeviceWakeLevel = 0;
    DEVICE_POWER_STATE DeviceWakeState;
    DEVICE_POWER_STATE WakeLevel = 0;
    SYSTEM_POWER_STATE SystemWakeLevel;
    BOOLEAN IsFound = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("ACPISystemPowerUpdateWakeCapabilitiesForPDOs: DeviceExtension %p\n", DeviceExtension);

    if (!(DeviceExtension->Flags & 0x0000000000010000))
    {
        SystemWakeLevel = 0;

        *OutDeviceWakeBit = 0;

        goto Finish;
    }

    KeAcquireSpinLock(&AcpiPowerLock, &OldIrql);
    SystemWakeLevel = DeviceExtension->PowerInfo.SystemWakeLevel;
    DeviceWakeState = ACPISystemPowerDetermineSupportedDeviceWakeState(DeviceExtension);
    KeReleaseSpinLock(&AcpiPowerLock, OldIrql);

    if (!((1 << SystemWakeLevel) & AcpiSupportedSystemStates))
    {
        if (!(AcpiOverrideAttributes & 4))
        {
            DPRINT1("ACPISystemPowerUpdateWakeCapabilitiesForPDOs: KeBugCheckEx()! FIXME\n");
            ASSERT(FALSE);
            //KeBugCheckEx(..);
        }

        DeviceWakeLevel = 0;
        SystemWakeLevel = 0;

        *OutDeviceWakeBit = 0;

        goto Finish;
    }

    if (DeviceWakeState)
    {
        DeviceWakeLevel = DeviceWakeState;
        WakeLevel = DeviceWakeState;

        *OutDeviceWakeBit = (1 << DeviceWakeState);

        IsFound = TRUE;
    }
    else
    {
        DPRINT1("ACPISystemPowerUpdateWakeCapabilitiesForPDOs: FIXME\n");
        ASSERT(FALSE);
    }

    Status = ACPISystemPowerGetSxD(DeviceExtension, SystemWakeLevel, &DeviceWakeState);
    if (NT_SUCCESS(Status))
    {
        DeviceWakeLevel = DeviceWakeState;
        WakeLevel = DeviceWakeState;

        IsFound = TRUE;
    }

    if (!IsFound)
    {
        DeviceWakeLevel = States[SystemWakeLevel];
        if (DeviceWakeLevel)
        {
            *OutDeviceWakeBit = (1 << DeviceWakeLevel);
            goto Finish;
        }

        DeviceWakeLevel = 4;
    }

    if (DeviceWakeLevel == PowerDeviceUnspecified)
    {
        *OutDeviceWakeBit = 0;
        goto Finish;
    }

    *OutDeviceWakeBit = (1 << DeviceWakeLevel);

  Finish:

    if (OutSystemWakeLevel)
        *OutSystemWakeLevel = SystemWakeLevel;

    if (OutDeviceWakeLevel)
        *OutDeviceWakeLevel = DeviceWakeLevel;

    if (OutWakeLevel)
        *OutWakeLevel = WakeLevel;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPISystemPowerUpdateWakeCapabilities(
     _In_ PDEVICE_EXTENSION DeviceExtension,
     _In_ PDEVICE_CAPABILITIES Capabilities,
     _In_ DEVICE_CAPABILITIES* OutCapabilities,
     _In_ DEVICE_POWER_STATE* States,
     _In_ ULONG* OutDeviceWakeBit,
     _In_ SYSTEM_POWER_STATE* OutSystemWakeLevel,
     _In_ DEVICE_POWER_STATE* OutDeviceWakeLevel,
     _In_ DEVICE_POWER_STATE* OutWakeLevel)
{
    PAGED_CODE();

    if ((DeviceExtension->Flags & 0x0000000000000040) &&
        !(DeviceExtension->Flags & 0x0000000000000020))
    {
        DPRINT1("ACPISystemPowerUpdateWakeCapabilities: FIXME\n");
        ASSERT(FALSE);
    }

    if (OutWakeLevel)
        *OutWakeLevel = PowerDeviceUnspecified;

    return ACPISystemPowerUpdateWakeCapabilitiesForPDOs(DeviceExtension, Capabilities, OutCapabilities, States, OutDeviceWakeBit, OutSystemWakeLevel, OutDeviceWakeLevel, OutWakeLevel);
}

NTSTATUS
NTAPI
ACPISystemPowerUpdateDeviceCapabilities(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_CAPABILITIES Capabilities,
    _In_ DEVICE_CAPABILITIES* OutCapabilities)
{
    DEVICE_POWER_STATE deviceState[PowerSystemMaximum];
    DEVICE_POWER_STATE DeviceStateBit;
    DEVICE_POWER_STATE CurrentState;
    DEVICE_POWER_STATE DeviceWakeLevel = 0;
    DEVICE_POWER_STATE WakeLevel = 0;
    SYSTEM_POWER_STATE SystemState;
    SYSTEM_POWER_STATE SystemWakeLevel = 0;
    ULONG SupportedPsStates = 0;
    ULONG SupportedPrStates = 0;
    ULONG SupportedStates = 0;
    ULONG DeviceWakeBit = 0;
    ULONG Bits;
    KIRQL Irql;
    BOOLEAN IsFound;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPISystemPowerUpdateDeviceCapabilities: DeviceExtension %p\n", DeviceExtension);

    RtlCopyMemory(deviceState, Capabilities->DeviceState, sizeof(deviceState));

    if (deviceState[PowerSystemWorking] != PowerDeviceD0)
        deviceState[PowerSystemWorking] = PowerDeviceD0;

    Status = ACPIDevicePowerDetermineSupportedDeviceStates(DeviceExtension, &SupportedPrStates, &SupportedPsStates);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPISystemPowerUpdateDeviceCapabilities: %X\n", Status);
        return Status;
    }

    SupportedStates = (SupportedPrStates | SupportedPsStates);
    if (!SupportedStates)
    {
        if ((DeviceExtension->Flags & 0x0000000000000040) &&
            !(DeviceExtension->Flags & 0x0000000000000020) &&
            !(OutCapabilities->DeviceD1) &&
            !(OutCapabilities->DeviceD2))
        {
            goto Finish;
        }

        SupportedStates = 0x12;

        if (OutCapabilities->DeviceD1)
            SupportedStates = 0x16;

        if (OutCapabilities->DeviceD2)
            SupportedStates |= 0x08;
    }

    Status = ACPISystemPowerUpdateWakeCapabilities(DeviceExtension, Capabilities, OutCapabilities, deviceState, &DeviceWakeBit, &SystemWakeLevel, &DeviceWakeLevel, &WakeLevel);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPISystemPowerUpdateDeviceCapabilities: %X\n", Status);
        return Status;
    }

    for (SystemState = PowerSystemSleeping1; SystemState <= PowerSystemShutdown; SystemState++)
    {
        if (!(AcpiSupportedSystemStates & (1 << SystemState)))
            continue;

        Status = ACPISystemPowerGetSxD(DeviceExtension, SystemState, &DeviceStateBit);
        if (NT_SUCCESS(Status))
        {
            if (DeviceStateBit > deviceState[SystemState])
                deviceState[SystemState] = DeviceStateBit;

            continue;
        }

        if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            DPRINT1("ACPISystemPowerUpdateDeviceCapabilities: %X\n", Status);
        }

        CurrentState = deviceState[SystemState];
        IsFound = FALSE;

        Bits = SupportedStates & ~((1 << CurrentState) - 1);

        while (Bits)
        {
            DeviceStateBit = (DEVICE_POWER_STATE)RtlFindLeastSignificantBit((ULONGLONG)Bits);
            Bits &= ~((1 << DeviceStateBit));

            if (SystemState <= SystemWakeLevel)
            {
                if ((DeviceWakeBit & Bits))
                    continue;

                if (DeviceStateBit == WakeLevel)
                {
                    deviceState[SystemState] = DeviceStateBit;
                    IsFound = TRUE;
                }
            }

            if (DeviceStateBit == PowerDeviceD3)
            {
                deviceState[SystemState] = DeviceStateBit;
                IsFound = TRUE;
                break;
            }

            if (!SupportedPrStates)
            {
                deviceState[SystemState] = DeviceStateBit;
                IsFound = TRUE;
                break;
            }

            KeAcquireSpinLock(&AcpiPowerLock, &Irql);

            DPRINT1("ACPISystemPowerUpdateDeviceCapabilities: FIXME\n");
            ASSERT(FALSE);
        }

        if (!IsFound)
        {
            DPRINT1("ACPISystemPowerUpdateDeviceCapabilities: No match found for S%x\n", (SystemState - 1));
            ASSERT(FALSE);
            KeBugCheckEx(0xA5, 0x10, (ULONG_PTR)DeviceExtension, 1, SystemState);
        }
    }

Finish:

    Status = ACPISystemPowerUpdateWakeCapabilities(DeviceExtension, Capabilities, OutCapabilities, deviceState, &DeviceWakeBit, &SystemWakeLevel, &DeviceWakeLevel, &WakeLevel);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPISystemPowerUpdateDeviceCapabilities: %X\n", Status);
        return Status;
    }

    KeAcquireSpinLock(&AcpiPowerLock, &Irql);

    RtlCopyMemory(DeviceExtension->PowerInfo.DevicePowerMatrix, deviceState, sizeof(DeviceExtension->PowerInfo.DevicePowerMatrix));

    DeviceExtension->PowerInfo.DeviceWakeLevel = DeviceWakeLevel;
    DeviceExtension->PowerInfo.SystemWakeLevel = SystemWakeLevel;

    DeviceExtension->PowerInfo.SupportDeviceD1 = ((SupportedStates & 0x04) != 0);
    DeviceExtension->PowerInfo.SupportDeviceD2 = ((SupportedStates & 0x08) != 0);

    DeviceExtension->PowerInfo.SupportWakeFromD0 = ((DeviceWakeBit & 0x02) != 0);
    DeviceExtension->PowerInfo.SupportWakeFromD1 = ((DeviceWakeBit & 0x04) != 0);
    DeviceExtension->PowerInfo.SupportWakeFromD2 = ((DeviceWakeBit & 0x08) != 0);
    DeviceExtension->PowerInfo.SupportWakeFromD3 = ((DeviceWakeBit & 0x10) != 0);

    KeReleaseSpinLock(&AcpiPowerLock, Irql);

    if (!(DeviceExtension->Flags & 0x0008000000000000))
        ACPIInternalUpdateFlags(DeviceExtension, 0x0100000000000000, FALSE);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPISystemPowerQueryDeviceCapabilities(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_CAPABILITIES Capabilities)
{
    DEVICE_CAPABILITIES capabilities;
    NTSTATUS Status;

    PAGED_CODE();

    if (!(DeviceExtension->Flags & 0x0400000000000000))
    {
        if ((DeviceExtension->Flags & 0x0000000000000040) &&
            !(DeviceExtension->Flags & 0x0000000000000020))
        {
            Status = ACPISystemPowerUpdateDeviceCapabilities(DeviceExtension, Capabilities, Capabilities);
        }
        else
        {
            Status = ACPIInternalGetDeviceCapabilities(DeviceExtension->ParentExtension->DeviceObject, &capabilities);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("ACPISystemPowerQueryDeviceCapabilities: Could not get parent caps (%X)\n", Status);
                return Status;
            }

            Status = ACPISystemPowerUpdateDeviceCapabilities(DeviceExtension, &capabilities, Capabilities);
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPISystemPowerQueryDeviceCapabilities: Could not update caps (%X)\n", Status);

            if ((DeviceExtension->Flags & 0x0000000000000020))
            {
                DPRINT1("ACPISystemPowerQueryDeviceCapabilities: FIXME\n");
                ASSERT(FALSE);
            }

            return Status;
        }

        ACPIInternalUpdateFlags(DeviceExtension, 0x0400000000000000, FALSE);
    }

    RtlCopyMemory(Capabilities->DeviceState, DeviceExtension->PowerInfo.DevicePowerMatrix, (7 * sizeof(DEVICE_POWER_STATE)));

    Capabilities->DeviceD1 = DeviceExtension->PowerInfo.SupportDeviceD1;
    Capabilities->DeviceD2 = DeviceExtension->PowerInfo.SupportDeviceD2;

    Capabilities->WakeFromD0 = DeviceExtension->PowerInfo.SupportWakeFromD0;
    Capabilities->WakeFromD1 = DeviceExtension->PowerInfo.SupportWakeFromD1;
    Capabilities->WakeFromD2 = DeviceExtension->PowerInfo.SupportWakeFromD2;
    Capabilities->WakeFromD3 = DeviceExtension->PowerInfo.SupportWakeFromD3;

    Capabilities->SystemWake = DeviceExtension->PowerInfo.SystemWakeLevel;
    Capabilities->DeviceWake = DeviceExtension->PowerInfo.DeviceWakeLevel;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIBusAndFilterIrpQueryCapabilities(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ ULONG Param3,
    _In_ BOOLEAN Param4)
{
    PDEVICE_CAPABILITIES Capabilities;
    PDEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IoStack;
    PAMLI_NAME_SPACE_OBJECT NsObject;
    PAMLI_NAME_SPACE_OBJECT ChildNsObject;
    ULONG DataBuff1;
    ULONG DataBuff2;
    ULONG UINumber;
    UCHAR MinorFunction;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPIBusAndFilterIrpQueryCapabilities: %p, %p, %X, %X\n", DeviceObject, Irp, Param3, Param4);

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    MinorFunction = IoStack->MinorFunction;
    Capabilities = IoStack->Parameters.DeviceCapabilities.Capabilities;

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);
    NsObject = DeviceExtension->AcpiObject;

    if (!(DeviceExtension->Flags & 0x0000008000000000))
    {
        ChildNsObject = ACPIAmliGetNamedChild(NsObject, 'VMR_');
        if (ChildNsObject)
        {
            if (ChildNsObject->ObjData.DataType == 8)
            {
                Status = ACPIGet(DeviceExtension, 'VMR_', 0x20044002, NULL, 0, NULL, NULL, (PVOID *)&DataBuff1, NULL);

                if (!NT_SUCCESS(Status) || DataBuff1)
                {
                    Capabilities->Removable = 1;
                }
                else
                {
                    Capabilities->Removable = 0;
                }
            }
            else
            {
                Capabilities->Removable = 1;
            }
        }

        if (!ACPIDockIsDockDevice(NsObject))
        {
            if (ACPIAmliGetNamedChild(NsObject, '0JE_'))
            {
                Capabilities->EjectSupported = 1;
                Capabilities->Removable = 1;
            }

            if (ACPIAmliGetNamedChild(NsObject, '1JE_') ||
                ACPIAmliGetNamedChild(NsObject, '2JE_') ||
                ACPIAmliGetNamedChild(NsObject, '3JE_') ||
                ACPIAmliGetNamedChild(NsObject, '4JE_'))
            {
                Capabilities->WarmEjectSupported = 1;
                Capabilities->Removable = 1;
            }
        }
    }

    if (ACPIAmliGetNamedChild(NsObject, 'CRI_'))
        DeviceObject->Flags |= DO_POWER_INRUSH;

    Status = ACPIGet(DeviceExtension, 'ATS_', 0x20040802, NULL, 0, NULL, NULL, (PVOID *)&DataBuff2, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIBusAndFilterIrpQueryCapabilities: Status %X\n", Status);
        goto Exit;
    }

    if (!(DeviceExtension->Flags & 0x0040000000000000))
    {
        if (ACPIAmliGetNamedChild(NsObject, 'SRC_') && !ACPIAmliGetNamedChild(NsObject, 'SRS_'))
            Capabilities->HardwareDisabled = 1;
        else if (Param4)
            Capabilities->HardwareDisabled = 0;

    }
    else if (!Param4)
    {
        if (AcpiOverrideAttributes & 2)
            Capabilities->HardwareDisabled = 1;
        else
            Capabilities->HardwareDisabled = 0;
    }

    if (!(DataBuff2 & 4))
        Capabilities->NoDisplayInUI = 1;

    if (ACPIAmliGetNamedChild(NsObject, 'NUS_'))
    {
        Status = ACPIGet(DeviceExtension, 'NUS_', 0x20040002, NULL, 0, NULL, NULL, (PVOID *)&UINumber, NULL);
        if (NT_SUCCESS(Status))
            Capabilities->UINumber = UINumber;
    }

    if (ACPIAmliGetNamedChild(NsObject, 'RDA_'))
    {
        Status = ACPIGet(DeviceExtension, 'RDA_', 0x20040402, NULL, 0, NULL, NULL, (PVOID *)&Capabilities->Address, NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIBusAndFilterIrpQueryCapabilities: Could query device address %X\n", Status);
            goto Exit;
        }
    }

    Status = ACPISystemPowerQueryDeviceCapabilities(DeviceExtension, Capabilities);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIBusAndFilterIrpQueryCapabilities: Could query device Capabilities %X\n", Status);
        goto Exit;
    }

    if (!Param4)
    {
        Capabilities->SilentInstall = 1;

        if (DeviceExtension->Flags & 0x0000000000020000)
            Capabilities->RawDeviceOK =  1;
        else
            Capabilities->RawDeviceOK = 0;

        if (DeviceExtension->InstanceID)
            Capabilities->UniqueID = 1;
        else
            Capabilities->UniqueID = 0;

        Status = STATUS_SUCCESS;
    }

Exit:

    DPRINT("ACPIBusAndFilterIrpQueryCapabilities: %X, %X, %X\n", Irp, MinorFunction, Status);

    return Status;
}

NTSTATUS
NTAPI
ACPIIrpInvokeDispatchRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ ULONG Param3,
    _In_ PVOID Callback,
    _In_ BOOLEAN Param5,
    _In_ BOOLEAN Param6)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPIIrpInvokeDispatchRoutine: %p, %p, %X, %X, %X\n", DeviceObject, Irp, Param3, Param5, Param6);

    Status = STATUS_NOT_SUPPORTED;

    if (NT_SUCCESS(Irp->IoStatus.Status))
    {
        if (Param5)
            Status = ((NTSTATUS (NTAPI *)(PDEVICE_OBJECT, PIRP, ULONG, BOOLEAN))Callback)(DeviceObject, Irp, Param3, FALSE);
    }
    else if (Irp->IoStatus.Status == STATUS_NOT_SUPPORTED)
    {
        if (Param6)
            Status = ((NTSTATUS (NTAPI *)(PDEVICE_OBJECT, PIRP, ULONG, BOOLEAN))Callback)(DeviceObject, Irp, Param3, FALSE);
    }

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    if (DeviceExtension->Flags & 0x0000000000000020)
    {
        if (Status != STATUS_PENDING)
        {
            if (Status != STATUS_NOT_SUPPORTED)
                Irp->IoStatus.Status = Status;
            else
                Status = Irp->IoStatus.Status;

            IoCompleteRequest(Irp, IO_NO_INCREMENT);
        }
    }
    else if (Status != STATUS_PENDING)
    {
        if (Status != STATUS_NOT_SUPPORTED)
            Irp->IoStatus.Status = Status;

        if (NT_SUCCESS(Status) || (Status == STATUS_NOT_SUPPORTED))
            Status = IoCallDriver(DeviceExtension->TargetDeviceObject, Irp);
        else
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return Status;
}

NTSTATUS
NTAPI
ACPIBusIrpQueryCapabilities(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PAGED_CODE();
    return ACPIIrpInvokeDispatchRoutine(DeviceObject, Irp, 0, ACPIBusAndFilterIrpQueryCapabilities, TRUE, TRUE);
}

NTSTATUS
NTAPI
ACPIInitDosDeviceName(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    PAMLI_NAME_SPACE_OBJECT Child;
    AMLI_OBJECT_DATA DataResult;
    UNICODE_STRING NameString;
    ANSI_STRING AnsiString;
    HANDLE DevInstRegKey;
    ULONG Data = 1;
    NTSTATUS Status;

    DPRINT("ACPIInitDosDeviceName: %p\n", DeviceExtension);

    RtlInitUnicodeString(&NameString, L"FirmwareIdentified");

    Status = IoOpenDeviceRegistryKey(DeviceExtension->PhysicalDeviceObject, 1, 0x00020000, &DevInstRegKey);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIInitDosDeviceName: Status %X\n", Status);
        return STATUS_SUCCESS;
    }

    Status = ZwSetValueKey(DevInstRegKey, &NameString, 0, REG_DWORD, &Data, sizeof(Data));
    RtlInitUnicodeString(&NameString, L"DosDeviceName");

    Child = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'NDD_');
    if (!Child)
    {
        ZwClose(DevInstRegKey);
        return STATUS_SUCCESS;
    }

    Status = AMLIEvalNameSpaceObject(Child, &DataResult, 0, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIInitDosDeviceName: Status %X\n", Status);
        ZwClose(DevInstRegKey);
        return STATUS_SUCCESS;
    }

    if (DataResult.DataType != 2)
    {
        DPRINT1("ACPIInitDosDeviceName: eval returns wrong type %X\n", DataResult.DataType);
        AMLIFreeDataBuffs(&DataResult, 1);
        ZwClose(DevInstRegKey);
        return STATUS_SUCCESS;
    }

    RtlInitAnsiString(&AnsiString, DataResult.DataBuff);

    Status = RtlAnsiStringToUnicodeString(&NameString, &AnsiString, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIInitDosDeviceName: Status %X\n", Status);
        AMLIFreeDataBuffs(&DataResult, 1);
        ZwClose(DevInstRegKey);
        return Status;
    }

    Status = ZwSetValueKey(DevInstRegKey, &NameString, 0, REG_SZ, NameString.Buffer, NameString.Length);

    AMLIFreeDataBuffs(&DataResult, 1);
    ZwClose(DevInstRegKey);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIInitDosDeviceName: Status %X\n", Status);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PnpIoResourceListToCmResourceList(
    _In_ PIO_RESOURCE_REQUIREMENTS_LIST IoResource,
    _Out_ PCM_RESOURCE_LIST* OutCmResource)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor;
    PCM_RESOURCE_LIST CmResource;
    PIO_RESOURCE_DESCRIPTOR IoDescriptor;
    PIO_RESOURCE_LIST IoList;
    ULONG Size;
    ULONG ix;

    DPRINT("PnpIoResourceListToCmResourceList: %p\n", IoResource);
    PAGED_CODE();

    *OutCmResource = NULL;

    if (!IoResource)
    {
        DPRINT1("PnpIoResourceListToCmResourceList: STATUS_INVALID_DEVICE_REQUEST\n");
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (!IoResource->List)
    {
        DPRINT1("PnpIoResourceListToCmResourceList: STATUS_INVALID_DEVICE_REQUEST\n");
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (!IoResource->List[0].Count)
    {
        DPRINT1("PnpIoResourceListToCmResourceList: STATUS_INVALID_DEVICE_REQUEST\n");
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Size = (sizeof(CM_RESOURCE_LIST) + (IoResource->List[0].Count - 1) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));

    CmResource = ExAllocatePoolWithTag(PagedPool, Size, 'RpcA');
    if (!CmResource)
    {
        DPRINT1("PnpIoResourceListToCmResourceList: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(CmResource, Size);

    IoList = IoResource->List;

    CmResource->Count = 1;
    CmResource->List[0].InterfaceType = IoResource->InterfaceType;
    CmResource->List[0].BusNumber = IoResource->BusNumber;

    CmResource->List[0].PartialResourceList.Version = 1;
    CmResource->List[0].PartialResourceList.Revision = 1;
    CmResource->List[0].PartialResourceList.Count = IoList->Count;

    for (ix = 0; ix < IoList->Count; ix++)
    {
        IoDescriptor = &IoList->Descriptors[ix];
        CmDescriptor = &CmResource->List[0].PartialResourceList.PartialDescriptors[ix];

        CmDescriptor->Type = IoDescriptor->Type;
        CmDescriptor->ShareDisposition = IoDescriptor->ShareDisposition;
        CmDescriptor->Flags = IoDescriptor->Flags;

        switch (CmDescriptor->Type)
        {
            case CmResourceTypePort:
                CmDescriptor->u.Port.Start = IoDescriptor->u.Port.MinimumAddress;
                CmDescriptor->u.Port.Length = IoDescriptor->u.Port.Length;
                break;

            case CmResourceTypeInterrupt:
                CmDescriptor->u.Interrupt.Level = IoDescriptor->u.Interrupt.MinimumVector;
                CmDescriptor->u.Interrupt.Vector = IoDescriptor->u.Interrupt.MinimumVector;
                CmDescriptor->u.Interrupt.Affinity = 0xFFFFFFFF;
                break;

            case CmResourceTypeMemory:
                CmDescriptor->u.Memory.Start = IoDescriptor->u.Memory.MinimumAddress;
                CmDescriptor->u.Memory.Length = IoDescriptor->u.Memory.Length;
                break;

            case CmResourceTypeDma:
                CmDescriptor->u.Dma.Channel = IoDescriptor->u.Dma.MinimumChannel;
                CmDescriptor->u.Dma.Port = 0;
                break;

            case CmResourceTypeBusNumber:
                CmDescriptor->u.BusNumber.Start = IoDescriptor->u.BusNumber.MinBusNumber;
                CmDescriptor->u.BusNumber.Length = IoDescriptor->u.BusNumber.Length;
                break;

            default:
                CmDescriptor->u.DevicePrivate.Data[0] = IoDescriptor->u.DevicePrivate.Data[0];
                CmDescriptor->u.DevicePrivate.Data[1] = IoDescriptor->u.DevicePrivate.Data[1];
                CmDescriptor->u.DevicePrivate.Data[2] = IoDescriptor->u.DevicePrivate.Data[2];
                break;
        }
    }

    *OutCmResource = CmResource;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIBusIrpQueryResources(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_RESOURCE_REQUIREMENTS_LIST IoResource = NULL;
    PCM_RESOURCE_LIST CmResource = NULL;
    PDEVICE_EXTENSION DeviceExtension;
    PVOID DataBuff;
    ULONG DataLen;
    NTSTATUS Status;

    DPRINT("ACPIBusIrpQueryResources: %p, %p\n", DeviceObject, Irp);
    PAGED_CODE();

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);
    ACPIInitDosDeviceName(DeviceExtension);

    Status = ACPIGet(DeviceExtension, 'ATS_', 0x20040802, NULL, 0, NULL, NULL, &DataBuff, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIBusIrpQueryResources: Status %X\n", Status);
        goto Finish;
    }

    if (!(DeviceExtension->Flags & 0x0040000000000000))
    {
        DPRINT1("ACPIBusIrpQueryResources: STATUS_INVALID_DEVICE_STATE (Device not Enabled)\n");
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Finish;
    }

    if (DeviceExtension->Flags & 0x0000002000000000)
    {
        DPRINT1("ACPIBusIrpQueryResources: STATUS_OBJECT_NAME_NOT_FOUND\n");
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    else
    {
        DataBuff = NULL;
        Status = ACPIGet(DeviceExtension, 'SRC_', 0x20010008, NULL, 0, NULL, NULL, &DataBuff, &DataLen);
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIBusIrpQueryResources: Status %X\n", Status);

        if (! (DeviceExtension->Flags & 0x0000000002000000))
            Status = Irp->IoStatus.Status;

        goto Finish;
    }

    Status = PnpDeviceBiosResourcesToNtResources(DeviceExtension, DataBuff, ((DeviceExtension->Flags & 0x0000000002000000) != 0), &IoResource);

    ExFreePool(DataBuff);

    if (!IoResource)
    {
        if (DeviceExtension->Flags & 0x0000000002000000)
            Status = STATUS_UNSUCCESSFUL;

        goto Finish;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIBusIrpQueryResources: Status %X\n", Status);
        goto Finish;
    }

    if (DeviceExtension->Flags & 0x0000000002000000)
    {
        Status = ACPIRangeSubtract(&IoResource, RootDeviceExtension->ResourceList);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIBusIrpQueryResources: Status %X\n", Status);
            ExFreePool(IoResource);
            goto Finish;
        }

        //ACPIRangeValidatePciResources(DeviceExtension, IoResource);
    }
    else if (DeviceExtension->Flags & 0x0000000200000000)
    {
        ASSERT(FALSE);
        Status = 0;//ACPIRangeFilterPICInterrupt(IoResource);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIBusIrpQueryResources: Status %X\n", Status);
            ExFreePool(IoResource);
            goto Finish;
        }
    }

    if (NT_SUCCESS(Status))
    {
        Status = PnpIoResourceListToCmResourceList(IoResource, &CmResource);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIBusIrpQueryResources: Status %X\n", Status);
            ExFreePool(IoResource);
            goto Finish;
        }
    }

    ExFreePool(IoResource);

Finish:

    if (!NT_SUCCESS(Status) && Status != STATUS_INSUFFICIENT_RESOURCES)
    {
        DPRINT1("ACPIBusIrpQueryResources: Status %X\n", Status);

        if (DeviceExtension->Flags & 0x0000000002000000)
        {
            DPRINT1("ACPIBusIrpQueryResources: KeBugCheckEx()!!!n");
            ASSERT(FALSE);
            KeBugCheckEx(0xA5, 2, (ULONG_PTR)DeviceExtension, 0, (ULONG_PTR)Irp);
        }
    }

    Irp->IoStatus.Status = Status;
    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information = (ULONG_PTR)CmResource;
    else
        Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, 0);

    DPRINT("ACPIBusIrpQueryResources: ret Status %X\n", Status);

    return Status;
}

NTSTATUS
NTAPI
ACPIInternalGrowBuffer(
    _Inout_ PVOID* OutIoResource,
    _In_ ULONG CopySize,
    _In_ ULONG AllocateSize)
{
    PIO_RESOURCE_LIST IoResource;

    DPRINT("ACPIInternalGrowBuffer: %X, %X\n", CopySize, AllocateSize);

    PAGED_CODE();
    ASSERT(OutIoResource != NULL);

    IoResource = ExAllocatePoolWithTag(PagedPool, AllocateSize, 'RpcA');
    if (!IoResource)
    {
        if (*OutIoResource)
        {
            ExFreePool(*OutIoResource);
            *OutIoResource = NULL;
        }

        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(IoResource, AllocateSize);

    if (*OutIoResource)
    {
        RtlCopyMemory(IoResource, *OutIoResource, CopySize);
        ExFreePool(*OutIoResource);
    }

    *OutIoResource = IoResource;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PnpiGrowResourceList(
    _Out_ PIO_RESOURCE_LIST** OutResourceListArray,
    _Out_ ULONG* OutResourceListArraySize)
{
    ULONG ResourceListArraySize;
    ULONG Count;
    SIZE_T Size;
    NTSTATUS Status;

    DPRINT("PnpiGrowResourceList: %X\n", *OutResourceListArray);

    PAGED_CODE();
    ASSERT(OutResourceListArray != NULL);

    if (!(*OutResourceListArray) || !(*OutResourceListArraySize))
    {
        DPRINT("PnpiGrowResourceList: %X -> %X, (%X)\n", 0, 8, sizeof(IO_RESOURCE_DESCRIPTOR));

        *OutResourceListArray = ExAllocatePoolWithTag(PagedPool, sizeof(IO_RESOURCE_DESCRIPTOR), 'RpcA');
        if (!(*OutResourceListArray))
        {
            DPRINT1("PnpiGrowResourceList: STATUS_INSUFFICIENT_RESOURCES\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(*OutResourceListArray, sizeof(IO_RESOURCE_DESCRIPTOR));

        *OutResourceListArraySize = 8;

        return STATUS_SUCCESS;
    }

    Count = *OutResourceListArraySize;

    Size = (sizeof(IO_RESOURCE_DESCRIPTOR) + (Count * sizeof(PIO_RESOURCE_LIST)));
    ResourceListArraySize = (Count + 8);

    DPRINT("PnpiGrowResourceList: %X -> %X, (%X)\n", Count, (Count + 8), Size);

    Status = ACPIInternalGrowBuffer((PVOID *)OutResourceListArray, (Count * sizeof(PIO_RESOURCE_LIST)), Size);

    if (NT_SUCCESS(Status))
        *OutResourceListArraySize = ResourceListArraySize;
    else
        *OutResourceListArraySize = 0;

    return Status;
}

NTSTATUS
NTAPI
PnpiGrowResourceDescriptor(
    _Inout_ PIO_RESOURCE_LIST* OutIoResource)
{
    PIO_RESOURCE_LIST IoResource;
    ULONG Count;
    ULONG Size;

    DPRINT("PnpiGrowResourceDescriptor: *OutIoResource %X\n", *OutIoResource);

    PAGED_CODE();
    ASSERT(OutIoResource != NULL);

    // FIXME sizeof ..

    IoResource = *OutIoResource;
    if (*OutIoResource)
    {
        Count = IoResource->Count;
        Size = (sizeof(IO_RESOURCE_LIST) + sizeof(IO_RESOURCE_DESCRIPTOR) * (Count - 1));

        DPRINT("PnpiGrowResourceDescriptor: %X -> %X, Size %X\n", Count, (Count + 8), (0x128 + sizeof(IO_RESOURCE_DESCRIPTOR) * (Count - 1)));

        return ACPIInternalGrowBuffer((PVOID *)OutIoResource, Size, (Size + 0x100));
    }

    DPRINT("PnpiGrowResourceDescriptor: %X -> %X, Size %X\n", 0, 8, 0x108);

    *OutIoResource = IoResource = ExAllocatePoolWithTag(PagedPool, 0x108, 'RpcA');
    if (!IoResource)
    {
        DPRINT1("PnpiGrowResourceDescriptor: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(IoResource, 0x108);

    IoResource->Version = 1;
    IoResource->Revision = 1;
    IoResource->Count = 0;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PnpiUpdateResourceList(
    _Inout_ PIO_RESOURCE_LIST* OutIoResource,
    _Out_ PIO_RESOURCE_DESCRIPTOR* OutIoDescriptors)
{
    PIO_RESOURCE_LIST IoResource;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("PnpiUpdateResourceList: %p\n", OutIoResource);

    PAGED_CODE();
    ASSERT(OutIoResource != NULL);

    IoResource = *OutIoResource;

    if (!IoResource || !(IoResource->Count & 7))
    {
        Status = PnpiGrowResourceDescriptor(OutIoResource);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("PnpiUpdateResourceList: Status %X\n", Status);
            return Status;
        }
    }

    *OutIoDescriptors = &((*OutIoResource)->Descriptors[(*OutIoResource)->Count]);
    (*OutIoResource)->Count++;

    return Status;
}

VOID
NTAPI
PnpiBiosAddressHandlePortFlags(
    _In_ PVOID Data,
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    PACPI_WORD_ADDRESS_SPACE_DESCRIPTOR AcpiDesc = Data;

    PAGED_CODE();

    if (!(AcpiDesc->GeneralFlags & 2))
        IoDescriptor->Flags |= 0x20;
}

VOID
NTAPI
PnpiBiosAddressHandleBusFlags(
    _In_ PVOID Data,
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    PAGED_CODE();
    ASSERT(IoDescriptor->u.BusNumber.Length > 0);
}

NTSTATUS
NTAPI
PnpiBiosAddressHandleGlobalFlags(
    _In_ PVOID Data,
    _In_ PIO_RESOURCE_LIST* ResourceListArray,
    _In_ ULONG Index,
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    PACPI_WORD_ADDRESS_SPACE_DESCRIPTOR AcpiDesc = Data;
    NTSTATUS Status;

    DPRINT("PnpiBiosAddressHandleGlobalFlags: %p, %X\n", AcpiDesc, AcpiDesc->GeneralFlags);
    PAGED_CODE();

    if ((AcpiOverrideAttributes & 0x800) || (AcpiDesc->GeneralFlags & 1))
        IoDescriptor->ShareDisposition = 1;
    else
        IoDescriptor->ShareDisposition = 3;

    if ((AcpiDesc->GeneralFlags & 4) && (AcpiDesc->GeneralFlags & 8))
    {
        if (IoDescriptor->Type == CmResourceTypeBusNumber)
            IoDescriptor->u.BusNumber.Length = (IoDescriptor->u.BusNumber.MaxBusNumber - IoDescriptor->u.BusNumber.MinBusNumber + 1);
        else
            IoDescriptor->u.Memory.Length = (IoDescriptor->u.Memory.MaximumAddress.LowPart - IoDescriptor->u.Memory.MinimumAddress.LowPart + 1);

        goto Finish;
    }

    if (AcpiDesc->GeneralFlags & 8)
    {
        if (IoDescriptor->Type == CmResourceTypeBusNumber)
            IoDescriptor->u.BusNumber.MinBusNumber = (IoDescriptor->u.BusNumber.MaxBusNumber - IoDescriptor->u.BusNumber.Length + 1);
        else
            IoDescriptor->u.Memory.MinimumAddress.LowPart = (IoDescriptor->u.Memory.MaximumAddress.LowPart - IoDescriptor->u.Memory.Length + 1);

        goto Finish;
    }

    if (AcpiDesc->GeneralFlags & 4)
    {
        if (IoDescriptor->Type == CmResourceTypeBusNumber)
            IoDescriptor->u.BusNumber.MaxBusNumber = (IoDescriptor->u.BusNumber.MinBusNumber + IoDescriptor->u.BusNumber.Length - 1);
        else
            IoDescriptor->u.Memory.MaximumAddress.LowPart = (IoDescriptor->u.Memory.MinimumAddress.LowPart - IoDescriptor->u.Memory.Length - 1);
    }

Finish:

    if (AcpiDesc->GeneralFlags & 1)
        return STATUS_SUCCESS;

    Status = PnpiUpdateResourceList(&ResourceListArray[Index], &IoDescriptor);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnpiBiosAddressHandleGlobalFlags: Status %X\n", Status);
        return Status;
    }
    RtlZeroMemory(IoDescriptor, sizeof(IO_RESOURCE_DESCRIPTOR));

    IoDescriptor->Type = 0x81;
    IoDescriptor->Flags = 1;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PnpiBiosAddressToIoDescriptor(
    _In_ PVOID Data,
    _In_ PIO_RESOURCE_LIST* ResourceListArray,
    _In_ ULONG Index,
    _In_ ULONG Param4)
{
    PACPI_WORD_ADDRESS_SPACE_DESCRIPTOR AcpiDesc = Data;
    PIO_RESOURCE_DESCRIPTOR IoDescriptor;
    PIO_RESOURCE_DESCRIPTOR DevicePrivateIoDescriptor;
    ULONG Length;
    ULONG Alignment;
    NTSTATUS Status;

    DPRINT("PnpiBiosAddressToIoDescriptor: %p, %X\n", AcpiDesc, AcpiDesc->ResourceType);

    PAGED_CODE();
    ASSERT(ResourceListArray != NULL);

    if ((AcpiDesc->GeneralFlags & 1) && AcpiDesc->ResourceType == 1 && (Param4 & 1))
        return STATUS_SUCCESS;

    if (!AcpiDesc->AddressLength)
        return STATUS_SUCCESS;

    Status = PnpiUpdateResourceList(&ResourceListArray[Index], &IoDescriptor);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnpiBiosAddressToIoDescriptor: Status %X\n", Status);
        return Status;
    }

    if (AcpiDesc->ResourceType == 0 || AcpiDesc->ResourceType == 1)
    {
        Status = PnpiUpdateResourceList(&ResourceListArray[Index], &DevicePrivateIoDescriptor);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("PnpiBiosAddressToIoDescriptor: Status %X\n", Status);
            return Status;
        }

        ASSERT(ResourceListArray[Index]->Count >= 2);

        IoDescriptor = (DevicePrivateIoDescriptor - 1);

        DevicePrivateIoDescriptor->Type = CmResourceTypeDevicePrivate;
        DevicePrivateIoDescriptor->Flags = 0x6000;
        DevicePrivateIoDescriptor->u.DevicePrivate.Data[2] = 0;
    }

    if (AcpiDesc->Length < 0xD)
    {
        DPRINT1("PnpiBiosAddressToIoDescriptor: KeBugCheckEx! Descriptor too small %X\n", AcpiDesc->Length);
        KeBugCheckEx(0xA5, 0xF, (ULONG_PTR)AcpiDesc, AcpiDesc->Tag, AcpiDesc->Length);
    }

    Length = AcpiDesc->AddressLength;
    Alignment = (AcpiDesc->Granularity + 1);

    if ((AcpiDesc->GeneralFlags & 4) && (AcpiDesc->GeneralFlags & 8))
    {
        if (AcpiDesc->AddressLength != (AcpiDesc->Maximum - AcpiDesc->Minimum + 1))
        {
            DPRINT1("PnpiBiosAddressToIoDescriptor: Length does not match fixed attributes\n");
            Length = (AcpiDesc->Maximum - AcpiDesc->Minimum + 1);
        }

        if ((AcpiDesc->Minimum & (ULONG)AcpiDesc->Granularity))
        {
            DPRINT1("PnpiBiosAddressToIoDescriptor: Granularity does not match fixed attributes\n");
            Alignment = 1;
        }
    }

    switch (AcpiDesc->ResourceType)
    {
        case 0:
        {
            DPRINT1("PnpiBiosAddressToIoDescriptor: FIXME\n");
            ASSERT(FALSE);

            IoDescriptor->u.Memory.Alignment = 1;
            break;
        }
        case 1:
        {
            IoDescriptor->Type = CmResourceTypePort;
            IoDescriptor->u.Port.Length = Length;
            IoDescriptor->u.Port.Alignment = Alignment;
            IoDescriptor->u.Port.MinimumAddress.LowPart = AcpiDesc->Minimum;
            IoDescriptor->u.Port.MinimumAddress.HighPart = 0;
            IoDescriptor->u.Port.MaximumAddress.LowPart = AcpiDesc->Maximum;
            IoDescriptor->u.Port.MaximumAddress.HighPart = 0;

            if (AcpiDesc->SpecificFlags & 0x20)
                DevicePrivateIoDescriptor->Flags |= 1;

            if (AcpiDesc->SpecificFlags & 0x10)
                DevicePrivateIoDescriptor->u.DevicePrivate.Data[0] = CmResourceTypeMemory;
            else
                DevicePrivateIoDescriptor->u.DevicePrivate.Data[0] = CmResourceTypePort;

            DevicePrivateIoDescriptor->u.DevicePrivate.Data[1] = AcpiDesc->Minimum;

            PnpiBiosAddressHandlePortFlags(AcpiDesc, IoDescriptor);

            IoDescriptor->u.Port.Alignment = 1;
            break;
        }
        case 2:
        {
            IoDescriptor->Type = CmResourceTypeBusNumber;

            IoDescriptor->u.BusNumber.MinBusNumber = AcpiDesc->Minimum;
            IoDescriptor->u.BusNumber.MaxBusNumber = AcpiDesc->Maximum;
            IoDescriptor->u.BusNumber.Length = Length;

            PnpiBiosAddressHandleBusFlags(AcpiDesc, IoDescriptor);

            break;
        }
        default:
        {
            DPRINT("PnpiBiosAddressToIoDescriptor: Unknown ResourceType %X\n", AcpiDesc->ResourceType);
            ASSERT(FALSE);
            break;
        }
    }

    PnpiBiosAddressHandleGlobalFlags(AcpiDesc, ResourceListArray, Index, IoDescriptor);

    return STATUS_SUCCESS;
}

VOID
NTAPI
PnpiBiosAddressHandleMemoryFlags(
    _In_ PVOID Data,
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    PACPI_WORD_ADDRESS_SPACE_DESCRIPTOR AcpiDesc = Data;

    PAGED_CODE();

    if (!(AcpiDesc->SpecificFlags & 0x1E))
        goto Finish;

    switch (AcpiDesc->SpecificFlags & 0x1E)
    {
        case 2:
            IoDescriptor->Flags |= 0x20;
            break;

        case 4:
            IoDescriptor->Flags |= 8;
            break;

        case 6:
            IoDescriptor->Flags |= 4;
            break;

        default:
            DPRINT1("PnpiBiosAddressHandleMemoryFlags: Unknown Memory TFlag 0x%02x\n", AcpiDesc->SpecificFlags);
            break;
    }

Finish:

    if (!(AcpiDesc->SpecificFlags & 1))
        IoDescriptor->Flags |= 1;
}

NTSTATUS
NTAPI
PnpiBiosAddressDoubleToIoDescriptor(
    _In_ PVOID Data,
    _In_ PIO_RESOURCE_LIST* ResourceListArray,
    _In_ ULONG Index,
    _In_ ULONG Param4)
{
    PACPI_DWORD_ADDRESS_SPACE_DESCRIPTOR AcpiDesc = Data;
    PIO_RESOURCE_DESCRIPTOR IoDescriptor;
    PIO_RESOURCE_DESCRIPTOR DevicePrivateIoDescriptor;
    ULONG Alignment;
    ULONG Length;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(ResourceListArray != NULL);

    if ((AcpiDesc->GeneralFlags & 1) && AcpiDesc->ResourceType == 1 && (Param4 & 1))
        return STATUS_SUCCESS;

    if (!AcpiDesc->AddressLength)
        return STATUS_SUCCESS;

    Status = PnpiUpdateResourceList(&ResourceListArray[Index], &IoDescriptor);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnpiBiosAddressDoubleToIoDescriptor: Status %X\n", Status);
        return Status;
    }

    if (AcpiDesc->ResourceType == 0 || AcpiDesc->ResourceType == 1)
    {
        Status = PnpiUpdateResourceList(&ResourceListArray[Index], &DevicePrivateIoDescriptor);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("PnpiBiosAddressDoubleToIoDescriptor: Status %X\n", Status);
            return Status;
        }

        ASSERT(ResourceListArray[Index]->Count >= 2);

        IoDescriptor = (DevicePrivateIoDescriptor - 1);

        DevicePrivateIoDescriptor->Type = CmResourceTypeDevicePrivate;
        DevicePrivateIoDescriptor->Flags = 0x6000;
        DevicePrivateIoDescriptor->u.DevicePrivate.Data[2] = 0;
    }

    if (AcpiDesc->Length < 0x17)
    {
        DPRINT("PnpiBiosAddressDoubleToIoDescriptor: Descriptor too small %X\n", AcpiDesc->Length);
        KeBugCheckEx(0xA5, 0xF, (ULONG_PTR)AcpiDesc, AcpiDesc->Tag, AcpiDesc->Length);
    }

    Length =  AcpiDesc->AddressLength;
    Alignment = (AcpiDesc->Granularity + 1);

    if ((AcpiDesc->GeneralFlags & 4) && (AcpiDesc->GeneralFlags & 8))
    {
        if (Length != (AcpiDesc->Maximum - AcpiDesc->Minimum + 1))
        {
            DPRINT1("PnpiBiosAddressDoubleToIoDescriptor: Length does not match fixed attributes\n");
            Length = (AcpiDesc->Maximum - AcpiDesc->Minimum + 1);
        }

        if (AcpiDesc->Minimum & AcpiDesc->Granularity)
        {
            DPRINT1("PnpiBiosAddressDoubleToIoDescriptor: Granularity does not match fixed attributes\n");
            Alignment = 1;
        }
    }

    switch (AcpiDesc->ResourceType)
    {
        case 0:
        {
            IoDescriptor->Type = CmResourceTypeMemory;
            IoDescriptor->u.Memory.Length = Length;
            IoDescriptor->u.Memory.Alignment = Alignment;
            IoDescriptor->u.Memory.MinimumAddress.LowPart = AcpiDesc->Minimum;
            IoDescriptor->u.Memory.MinimumAddress.HighPart = 0;
            IoDescriptor->u.Memory.MaximumAddress.LowPart = AcpiDesc->Maximum;
            IoDescriptor->u.Memory.MaximumAddress.HighPart = 0;

            if (AcpiDesc->SpecificFlags & 0x20)
                DevicePrivateIoDescriptor->u.DevicePrivate.Data[0] = CmResourceTypePort;
            else
                DevicePrivateIoDescriptor->u.DevicePrivate.Data[0] = CmResourceTypeMemory;

            DevicePrivateIoDescriptor->u.DevicePrivate.Data[1] = (AcpiDesc->Minimum + AcpiDesc->Offset);

            PnpiBiosAddressHandleMemoryFlags(AcpiDesc, IoDescriptor);

            IoDescriptor->u.Memory.Alignment = 1;
            break;
        }
        case 1:
        {
            IoDescriptor->Type = CmResourceTypePort;
            IoDescriptor->u.Port.Length = Length;
            IoDescriptor->u.Port.Alignment = Alignment;
            IoDescriptor->u.Port.MinimumAddress.LowPart = AcpiDesc->Minimum;
            IoDescriptor->u.Port.MinimumAddress.HighPart = 0;
            IoDescriptor->u.Port.MaximumAddress.LowPart = AcpiDesc->Maximum;
            IoDescriptor->u.Port.MaximumAddress.HighPart = 0;

            if (AcpiDesc->SpecificFlags & 0x20)
                DevicePrivateIoDescriptor->Flags |= 1;

            if (AcpiDesc->SpecificFlags & 0x10)
                DevicePrivateIoDescriptor->u.DevicePrivate.Data[0] = CmResourceTypeMemory;
            else
                DevicePrivateIoDescriptor->u.DevicePrivate.Data[0] = CmResourceTypePort;

            DevicePrivateIoDescriptor->u.DevicePrivate.Data[1] = (AcpiDesc->Minimum + AcpiDesc->Offset);

            PnpiBiosAddressHandlePortFlags(AcpiDesc, IoDescriptor);

            IoDescriptor->u.Port.Alignment = 1;
            break;
        }
        case 2:
        {
            IoDescriptor->Type = CmResourceTypeBusNumber;
            IoDescriptor->u.BusNumber.Length = Length;
            IoDescriptor->u.BusNumber.MinBusNumber = AcpiDesc->Minimum;
            IoDescriptor->u.BusNumber.MaxBusNumber = AcpiDesc->Maximum;

            PnpiBiosAddressHandleBusFlags(AcpiDesc, IoDescriptor);

            break;
        }
        default:
        {
            DPRINT1("PnpiBiosAddressDoubleToIoDescriptor: Unknown ResourceType %X\n", AcpiDesc->ResourceType);
            break;
        }
    }

    PnpiBiosAddressHandleGlobalFlags(AcpiDesc, ResourceListArray, Index, IoDescriptor);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PnpiBiosPortToIoDescriptor(
    _In_ PACPI_IO_PORT_DESCRIPTOR AcpiDesc,
    _In_ PIO_RESOURCE_LIST* ResourceListArray,
    _In_ ULONG Index,
    _In_ ULONG Param4)
{
    PIO_RESOURCE_DESCRIPTOR IoDescriptor;
    NTSTATUS Status;

    DPRINT("PnpiBiosPortToIoDescriptor: %p, %X\n", AcpiDesc, Param4);

    PAGED_CODE();
    ASSERT(ResourceListArray != NULL);

    if ((Param4 & 1) || !AcpiDesc->RangeLength)
        return STATUS_SUCCESS;

    Status = PnpiUpdateResourceList(&ResourceListArray[Index], &IoDescriptor);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnpiBiosPortToIoDescriptor: Status %X\n", Status);
        return Status;
    }

    IoDescriptor->Type = CmResourceTypePort;
    IoDescriptor->Flags = 1;
    IoDescriptor->ShareDisposition = 1;

    IoDescriptor->u.Port.MinimumAddress.LowPart = AcpiDesc->Minimum;
    IoDescriptor->u.Port.MaximumAddress.LowPart = (AcpiDesc->Maximum + AcpiDesc->RangeLength - 1);
    IoDescriptor->u.Port.Length = AcpiDesc->RangeLength;
    IoDescriptor->u.Port.Alignment = AcpiDesc->Alignment;

    if (AcpiDesc)
        IoDescriptor->Flags |= CM_RESOURCE_PORT_16_BIT_DECODE;
    else
        IoDescriptor->Flags |= CM_RESOURCE_PORT_10_BIT_DECODE;

    return STATUS_SUCCESS;
}

VOID
NTAPI
PnpiClearAllocatedMemory(
    _In_ PIO_RESOURCE_LIST* ResourceListArray,
    _In_ ULONG ResourceListArraySize)
{
    ULONG Index = 0;

    DPRINT("PnpiClearAllocatedMemory: %p, %X\n", ResourceListArray, ResourceListArraySize);
    PAGED_CODE();

    if (ResourceListArray == NULL)
        return;

    if (ResourceListArraySize > Index)
    {
        do
        {
            if (ResourceListArray[Index])
                ExFreePool(ResourceListArray[Index]);

            Index++;
        }
        while (Index < ResourceListArraySize);
    }

    ExFreePool(ResourceListArray);
}

NTSTATUS
NTAPI
PnpBiosResourcesToNtResources(
    _In_ PVOID Data,
    _In_ ULONG Param2,
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST* OutIoResource)
{
    PIO_RESOURCE_LIST* ResourceListArray = NULL;
    PIO_RESOURCE_LIST IoList;
    PACPI_RESOURCE_DATA_TYPE ResDataType;
    ULONG ResourceListArraySize = 0;
    ULONG Increment;
    ULONG Index = 0;
    ULONG MaxIndex = 0;
    ULONG ListSize;
    ULONG Size;
    ULONG ix = 0;
    ULONG jx = 0;
    UCHAR TagName;
    NTSTATUS Status;

    DPRINT("PnpBiosResourcesToNtResources: %p, %X\n", Data, Param2);

    PAGED_CODE();
    ASSERT(Data != NULL);

    Status = PnpiGrowResourceList(&ResourceListArray, &ResourceListArraySize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnpBiosResourcesToNtResources: Status %X\n", Status);
        return Status;
    }

    for (ResDataType = Data; ; )
    {
        if (!ResDataType->Small.Type)
        {
            Increment = (ResDataType->Small.Length + 1);
            TagName = ResDataType->Small.Name;
            DPRINT("PnpBiosResourcesToNtResources: Small TagName %X, Increment %X\n", TagName, Increment);
        }
        else
        {
            Increment = (ResDataType->Large.Length + 3);
            TagName = ResDataType->Large.Name;
            DPRINT("PnpBiosResourcesToNtResources: Large TagName %X, Increment %X\n", TagName, Increment);
        }

        if ((ResDataType->Small.Tag & 0xF8) == 0x78)
        {
            DPRINT("PnpBiosResourcesToNtResources: TAG_END\n");
            break;
        }

        ix++;

        if (!ResDataType->Small.Type)
        {
            switch (TagName)
            {
                case 0x04:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x05:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x06:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x07:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x08:
                {
                    Status = PnpiBiosPortToIoDescriptor(Data, ResourceListArray, Index, Param2);
                    DPRINT("PnpBiosResourcesToNtResources: TAG_IO, Status %X\n", Status);
                    break;
                }
                case 0x09:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x0E:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                default:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                }
            }
        }
        else
        {
            switch (TagName)
            {
                case 0x01:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x02:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x03:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x04:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x05:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x06:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
                case 0x07:
                {
                    Status = PnpiBiosAddressDoubleToIoDescriptor(Data, ResourceListArray, Index, Param2);
                    DPRINT("PnpBiosResourcesToNtResources: TAG_DOUBLE_ADDRESS = %X\n", Status);
                    break;
                }
                case 0x08:
                {
                    Status = PnpiBiosAddressToIoDescriptor(Data, ResourceListArray, Index, Param2);
                    DPRINT("PnpBiosResourcesToNtResources: TAG_WORD_ADDRESS = %X\n", Status);
                    break;
                }
                default:
                {
                    DPRINT1("PnpBiosResourcesToNtResources: FIXME! (TagName %X)\n", TagName);
                    ASSERT(FALSE);
                    break;
                }
            }
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("PnpBiosResourcesToNtResources: Failed on TagName %X, Status %X\n", TagName, Status);

            PnpiClearAllocatedMemory(ResourceListArray, ResourceListArraySize);
            return Status;
        }

        Data = ResDataType = Add2Ptr(ResDataType, Increment);
    }

    DPRINT("PnpBiosResourcesToNtResources: TAG_END\n");

    if (!NT_SUCCESS(Status))
    {
        DPRINT("PnpBiosResourcesToNtResources: Failed on TagName %X, Status %X\n", TagName, Status);

        PnpiClearAllocatedMemory(ResourceListArray, ResourceListArraySize);
        return Status;
    }

    if (ix && ix == jx)
    {
        DPRINT("PnpBiosResourcesToNtResources: This _CRS contains vendor defined tags only. No resources will be allocated.\n");

        PnpiClearAllocatedMemory(ResourceListArray, ResourceListArraySize);
        *OutIoResource = NULL;

        return Status;
    }

    ListSize = sizeof(IO_RESOURCE_DESCRIPTOR);

    if (*ResourceListArray)
        Size = (*ResourceListArray)->Count;
    else
        Size = 0;

    Index = 1;

    for (Index = 1; Index <= MaxIndex; Index++)
    {
        if (!(ResourceListArray[Index]))
        {
            DPRINT1("PnpBiosResourcesToNtResources: Bad List at Array[%X]\n", Index);

            PnpiClearAllocatedMemory(ResourceListArray, ResourceListArraySize);
            *OutIoResource = NULL;

            return STATUS_UNSUCCESSFUL;
        }

        if (ResourceListArray[Index]->Count)
        {
            DPRINT("PnpBiosResourcesToNtResources: Index %X, ListSize %X\n", Index, ListSize);
            ListSize += (sizeof(IO_RESOURCE_LIST) + ((ResourceListArray[Index]->Count + Size - 1) * sizeof(IO_RESOURCE_DESCRIPTOR)));
        }
    }

    if (!MaxIndex)
    {
        if (!(*ResourceListArray) || (!(*ResourceListArray)->Count))
        {
            DPRINT1("PnpBiosResourcesToNtResources: No Resources to Report\n");

            PnpiClearAllocatedMemory(ResourceListArray, ResourceListArraySize);
            *OutIoResource = NULL;

            return STATUS_UNSUCCESSFUL;
        }

        ListSize += (((*ResourceListArray)->Count - 1) + sizeof(IO_RESOURCE_LIST) * sizeof(IO_RESOURCE_DESCRIPTOR));
    }

    if (ListSize < sizeof(IO_RESOURCE_REQUIREMENTS_LIST))
    {
        DPRINT1("PnpBiosResourcesToNtResources: Resources smaller than a List\n");

        PnpiClearAllocatedMemory(ResourceListArray, ResourceListArraySize);
        *OutIoResource = NULL;

        return STATUS_UNSUCCESSFUL;
    }

    *OutIoResource = ExAllocatePoolWithTag(PagedPool, ListSize, 'RpcA');

    DPRINT("PnpBiosResourceToNtResources: %p, %X\n", *OutIoResource, ListSize);

    if (!(*OutIoResource))
    {
        DPRINT1("PnpBiosResourceToNtResources: Could not allocate memory for ResourceRequirementList\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Finish;
    }
    RtlZeroMemory(*OutIoResource, ListSize);

    (*OutIoResource)->InterfaceType = 0xF;
    (*OutIoResource)->BusNumber = 0;
    (*OutIoResource)->ListSize = ListSize;

    IoList = (*OutIoResource)->List;

    for (Index = 1; Index <= MaxIndex; Index++)
    {
        if (!(ResourceListArray[Index]->Count))
            continue;

        ListSize = (sizeof(IO_RESOURCE_LIST) + (ResourceListArray[Index]->Count - 1) * sizeof(IO_RESOURCE_DESCRIPTOR));
        (ResourceListArray[Index])->Count += Size;

        DPRINT1("PnpBiosResourcesToNtResources: [%X] %p, %X, %X\n", Index, IoList, ListSize, ResourceListArray[Index]->Count);
        RtlCopyMemory(IoList, ResourceListArray[Index], ListSize);

        IoList = Add2Ptr(IoList, ListSize);

        if (Size)
        {
            RtlCopyMemory(IoList, (*ResourceListArray)->Descriptors, (Size * sizeof(IO_RESOURCE_DESCRIPTOR)));
            IoList = Add2Ptr(IoList, (Size * sizeof(IO_RESOURCE_DESCRIPTOR)));
        }

        ((*OutIoResource)->AlternativeLists)++;
    }

    if (!MaxIndex)
    {
        ASSERT(Size != 0);
        RtlCopyMemory(IoList, *ResourceListArray, (sizeof(IO_RESOURCE_LIST) + ((Size - 1) * sizeof(IO_RESOURCE_DESCRIPTOR))));

        ((*OutIoResource)->AlternativeLists)++;
    }

Finish:

    PnpiClearAllocatedMemory(ResourceListArray, ResourceListArraySize);

    return Status;
}

NTSTATUS
NTAPI
PnpDeviceBiosResourcesToNtResources(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PVOID Data,
    _In_ ULONG Param3,
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST* OutIoResource)
{
    KIRQL Irql;
    BOOLEAN IsFound;
    NTSTATUS Status;

    DPRINT("PnpDeviceBiosResourcesToNtResources: %p, %X\n", DeviceExtension, Param3);

    Status = PnpBiosResourcesToNtResources(Data, Param3, OutIoResource);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnpDeviceBiosResourcesToNtResources: Status %X\n", Status);
        return Status;
    }

    if (!(*OutIoResource))
    {
        DPRINT1("PnpDeviceBiosResourcesToNtResources: IoResource is NULL!\n");
        return Status;
    }

    IsFound = FALSE;

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &Irql);

    while (DeviceExtension)
    {
        if (DeviceExtension->Flags & 0x0000002000000000)
        {
            IsFound = TRUE;
            break;
        }

        DeviceExtension = DeviceExtension->ParentExtension;
    }

    KeReleaseSpinLock(&AcpiDeviceTreeLock, Irql);

    if (!IsFound)
        return Status;

    DPRINT1("PnpDeviceBiosResourcesToNtResources: FIXME\n");
    ASSERT(FALSE);

    return Status;
}

NTSTATUS
NTAPI
ACPIRangeSortCmList(
    _In_ PCM_RESOURCE_LIST CmResource)
{
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor1;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor2;
    CM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
    ULONG DescriptorCount;
    ULONG DescriptorSize;
    ULONG ix;
    ULONG jx;

    DPRINT("ACPIRangeSortCmList: %p\n", CmResource);

    PartialList = &CmResource->List[0].PartialResourceList;

    DescriptorCount = PartialList->Count;
    DescriptorSize = sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

    for (ix = 0; ix < DescriptorCount; ix++)
    {
        Descriptor1 = &PartialList->PartialDescriptors[ix];

        for (jx = (ix + 1); jx < DescriptorCount; jx++)
        {
            Descriptor2 = &PartialList->PartialDescriptors[jx];

            if (Descriptor1->Type != Descriptor2->Type)
                continue;

            if (Descriptor1->Type == CmResourceTypePort)
            {
                if (Descriptor2->u.Port.Start.QuadPart < Descriptor1->u.Port.Start.QuadPart)
                    Descriptor1 = Descriptor2;

                continue;
            }

            if (Descriptor1->Type == CmResourceTypeMemory)
            {
                if (Descriptor2->u.Memory.Start.QuadPart < Descriptor1->u.Memory.Start.QuadPart)
                    Descriptor1 = Descriptor2;

                continue;
            }

            if (Descriptor1->Type == CmResourceTypeInterrupt)
            {
                if (Descriptor2->u.Interrupt.Vector < Descriptor1->u.Interrupt.Vector)
                    Descriptor1 = Descriptor2;

                continue;
            }

            if (Descriptor1->Type == CmResourceTypeDma)
            {
                if (Descriptor2->u.Dma.Channel < Descriptor1->u.Dma.Channel)
                    Descriptor1 = Descriptor2;
            }
        }

        if (Descriptor1 == &PartialList->PartialDescriptors[ix])
            continue;

        RtlCopyMemory(&descriptor, &PartialList->PartialDescriptors[ix], DescriptorSize);
        RtlCopyMemory(&PartialList->PartialDescriptors[ix], Descriptor1, DescriptorSize);
        RtlCopyMemory(Descriptor1, &descriptor, DescriptorSize);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIRangeSortIoList(
    _In_ PIO_RESOURCE_LIST IoList)
{
    PIO_RESOURCE_DESCRIPTOR Descriptor1;
    PIO_RESOURCE_DESCRIPTOR Descriptor2;
    IO_RESOURCE_DESCRIPTOR descriptor;
    ULONG Count;
    ULONG ix;
    ULONG jx;

    DPRINT("ACPIRangeSortIoList: %p\n", IoList);

    Count = IoList->Count;

    for (ix = 0; ix < Count; ix++)
    {
        Descriptor1 = &IoList->Descriptors[ix];

        for (jx = (ix + 1); jx < Count; jx++)
        {
            Descriptor2 = &IoList->Descriptors[jx];

            if (Descriptor1->Type != Descriptor2->Type)
                continue;

            if (Descriptor1->Type == CmResourceTypePort)
            {
                if (Descriptor2->u.Port.MinimumAddress.QuadPart < Descriptor1->u.Port.MinimumAddress.QuadPart)
                    Descriptor1 = Descriptor2;

                continue;
            }

            if (Descriptor1->Type == CmResourceTypeMemory)
            {
                if (Descriptor2->u.Memory.MinimumAddress.QuadPart < Descriptor1->u.Memory.MinimumAddress.QuadPart)
                    Descriptor1 = Descriptor2;

                continue;
            }

            if (Descriptor1->Type == CmResourceTypeInterrupt)
            {
                if (Descriptor2->u.Interrupt.MinimumVector < Descriptor1->u.Interrupt.MinimumVector)
                    Descriptor1 = Descriptor2;

                continue;
            }

            if (Descriptor1->Type == CmResourceTypeDma)
            {
                if (Descriptor2->u.Dma.MinimumChannel < Descriptor1->u.Dma.MaximumChannel)
                    Descriptor1 = Descriptor2;
            }
        }

        if (Descriptor1 == &IoList->Descriptors[ix])
            continue;

        RtlCopyMemory(&descriptor, &IoList->Descriptors[ix], sizeof(IO_RESOURCE_DESCRIPTOR));
        RtlCopyMemory(&IoList->Descriptors[ix], Descriptor1, sizeof(IO_RESOURCE_DESCRIPTOR));
        RtlCopyMemory(Descriptor1, &descriptor, sizeof(IO_RESOURCE_DESCRIPTOR));
    }

    return STATUS_SUCCESS;

}

NTSTATUS
NTAPI
ACPIRangeSubtractIoList(
    _In_ PIO_RESOURCE_LIST InIoList,
    _In_ PCM_RESOURCE_LIST CmResource,
    _Out_ PIO_RESOURCE_LIST* OutIoList)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor;
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PIO_RESOURCE_DESCRIPTOR IoDescriptor;
    PIO_RESOURCE_LIST IoList;
    ULONG DescriptorCount;
    ULONG Count;
    ULONG Size;
    ULONG ix;
    ULONG jx = 0;
    ULONG kx;

    DPRINT("ACPIRangeSubtractIoList: %p, %p\n", InIoList, CmResource);

    PartialList = &CmResource->List[0].PartialResourceList;

    DescriptorCount = PartialList->Count;
    Count = InIoList->Count;
    Size = (sizeof(IO_RESOURCE_LIST) + ((((DescriptorCount + Count) * 2) - 1)) * sizeof(IO_RESOURCE_DESCRIPTOR));

    IoList = ExAllocatePoolWithTag(NonPagedPool, Size, 'RpcA');
    if (!IoList)
    {
        DPRINT1("ACPIRangeSubtractIoList: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(IoList, Size);

    IoList->Version = InIoList->Version;
    IoList->Revision = InIoList->Revision;
    IoList->Count = InIoList->Count;

    for (ix = 0; ix < Count; ix++)
    {
        RtlCopyMemory(&IoList->Descriptors[jx], &InIoList->Descriptors[ix], sizeof(IO_RESOURCE_DESCRIPTOR));

        DPRINT("ACPIRangeSubtractIoList: InIoDesc[%X] %X -> IoDesc[%X] %X\n", ix, &InIoList->Descriptors[ix], jx, &IoList->Descriptors[jx]);

        IoDescriptor = &IoList->Descriptors[jx];
        jx++;

        for (kx = 0; kx < DescriptorCount; kx++)
        {
            if (!IoDescriptor)
                break;

            CmDescriptor = &PartialList->PartialDescriptors[kx];

            if (CmDescriptor->Type != IoDescriptor->Type)
                continue;

            if (IoDescriptor->Type == CmResourceTypePort ||
                IoDescriptor->Type == CmResourceTypeMemory)
            {
                DPRINT1("ACPIRangeSubtractIoList: FIXME\n");
                ASSERT(FALSE);
                continue;
            }

            if (IoDescriptor->Type == CmResourceTypeInterrupt)
            {
                DPRINT1("ACPIRangeSubtractIoList: FIXME\n");
                ASSERT(FALSE);
                continue;
            }

            if (IoDescriptor->Type == CmResourceTypeDma)
            {
                DPRINT1("ACPIRangeSubtractIoList: FIXME\n");
                ASSERT(FALSE);
                continue;
            }
        }

        IoDescriptor = &IoList->Descriptors[jx];
        RtlCopyMemory(IoDescriptor, &InIoList->Descriptors[ix], sizeof(IO_RESOURCE_DESCRIPTOR));
        IoDescriptor->Type = CmResourceTypeDevicePrivate;

        DPRINT("ACPIRangeSubtractIoList: InIoDesc[%X] %X -> IoDesc[%X] %X for backup\n", ix, &InIoList->Descriptors[ix], jx, IoDescriptor);

        jx++;
    }

    IoList->Count = jx;

    Size = (sizeof(IO_RESOURCE_LIST) + (jx - 1) * sizeof(IO_RESOURCE_DESCRIPTOR));

    *OutIoList = ExAllocatePoolWithTag(NonPagedPool, Size, 'RpcA');
    if (*OutIoList == NULL)
    {
        DPRINT1("ACPIRangeSubtractIoList: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(*OutIoList, IoList, Size);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIRangeSubtract(
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST* OutIoResource,
    _In_ PCM_RESOURCE_LIST CmResource)
{
    PIO_RESOURCE_REQUIREMENTS_LIST IoResource;
    PIO_RESOURCE_LIST* ResourceListArray;
    PIO_RESOURCE_LIST IoList;
    ULONG ListCounter;
    ULONG ResourceSize;
    ULONG Size;
    ULONG ix;
    NTSTATUS Status;

    DPRINT("ACPIRangeSubtract: %p, %p\n", OutIoResource, CmResource);

    ListCounter = (*OutIoResource)->AlternativeLists;

    Status = ACPIRangeSortCmList(CmResource);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIRangeSubtract: Status %X\n", Status);
        return Status;
    }

    Size = (ListCounter * sizeof(PIO_RESOURCE_LIST));

    ResourceListArray = ExAllocatePoolWithTag(NonPagedPool, Size, 'RpcA');
    if (!ResourceListArray)
    {
        DPRINT1("ACPIRangeSubtract: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(ResourceListArray, Size);

    IoList = (*OutIoResource)->List;
    ResourceSize = (sizeof(IO_RESOURCE_REQUIREMENTS_LIST) - sizeof(IO_RESOURCE_LIST));

    Status = ACPIRangeSortIoList(IoList);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIRangeSubtract: Status %X\n", Status);
        return Status;
    }

    for (ix = 0; ix < ListCounter; ix++)
    {
        Status = ACPIRangeSubtractIoList(IoList, CmResource, &ResourceListArray[ix]);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIRangeSubtract: Status %X\n", Status);

            while (ix)
            {
                ExFreePool(ResourceListArray[ix]);
                ix--;
            }

            ExFreePool(ResourceListArray);

            return Status;
        }

        ResourceSize += (sizeof(IO_RESOURCE_LIST) + (((ResourceListArray[ix])->Count - 1) * sizeof(IO_RESOURCE_DESCRIPTOR)));
        Size = (sizeof(IO_RESOURCE_LIST) + (IoList->Count - 1) * sizeof(IO_RESOURCE_DESCRIPTOR));
        IoList = Add2Ptr(IoList, Size);
    }

    IoResource = ExAllocatePoolWithTag(NonPagedPool, ResourceSize, 'RpcA');
    if (!IoResource)
    {
        DPRINT1("ACPIRangeSubtract: STATUS_INSUFFICIENT_RESOURCES\n");

        do
        {
            ListCounter--;
            ExFreePool(ResourceListArray[ListCounter]);
        }
        while (ListCounter);

        ExFreePool(ResourceListArray);

        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(IoResource, ResourceSize);

    RtlCopyMemory(IoResource, *OutIoResource, (sizeof(IO_RESOURCE_REQUIREMENTS_LIST) - sizeof(IO_RESOURCE_LIST)));

    IoResource->ListSize = ResourceSize;
    IoList = IoResource->List;

    for (ix = 0; ix < ListCounter; ix++)
    {
        Size = sizeof(IO_RESOURCE_LIST) + ((((ResourceListArray[ix])->Count) - 1) * sizeof(IO_RESOURCE_DESCRIPTOR));
        RtlCopyMemory(IoList, ResourceListArray[ix], Size);
        IoList = Add2Ptr(IoList, Size);
        ExFreePool(ResourceListArray[ix]);
    }

    ExFreePool(ResourceListArray);
    ExFreePool(*OutIoResource);

    *OutIoResource = IoResource;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIBusIrpQueryResourceRequirements(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_RESOURCE_REQUIREMENTS_LIST IoResource = NULL;
    PDEVICE_EXTENSION DeviceExtension;
    PVOID CrsDataBuff = NULL;
    PVOID PrsDataBuff = NULL;
    ULONG CrsDataLen;
    ULONG PrsDataLen;
    NTSTATUS CrsStatus;
    NTSTATUS PrsStatus;
    NTSTATUS Status = Irp->IoStatus.Status;

    PAGED_CODE();
    DPRINT("ACPIBusIrpQueryResourceRequirements: %p, %p\n", DeviceObject, Irp);

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    CrsStatus = ACPIGet(DeviceExtension, 'SRC_', 0x20010008, NULL, 0, NULL, NULL, &CrsDataBuff, &CrsDataLen);
    PrsStatus = ACPIGet(DeviceExtension, 'SRP_', 0x20010008, NULL, 0, NULL, NULL, &PrsDataBuff, &PrsDataLen);

    if (NT_SUCCESS(CrsStatus))
    {
        Status = STATUS_NOT_SUPPORTED;

        if (!NT_SUCCESS(PrsStatus))
        {
            Status = PnpDeviceBiosResourcesToNtResources(DeviceExtension,
                                                         CrsDataBuff,
                                                         ((DeviceExtension->Flags & 0x0000000002000000) != 0),
                                                         &IoResource);
        
            ASSERTMSG("The BIOS has reported inconsistent resources (_PRS). Please upgrade your BIOS.", NT_SUCCESS(Status));
            DPRINT("ACPIBusIrpQueryResourceRequirements: Status %X\n", Status);

            if (NT_SUCCESS(CrsStatus))
                ExFreePool(CrsDataBuff);
        }
        else
        {
            DPRINT1("ACPIBusIrpQueryResourceRequirements: FIXME\n");
            ASSERT(FALSE);
        }
    }
    else
    {
        if (!NT_SUCCESS(PrsStatus))
        {
            if (PrsStatus == STATUS_INSUFFICIENT_RESOURCES || CrsStatus == STATUS_INSUFFICIENT_RESOURCES)
                Status = STATUS_INSUFFICIENT_RESOURCES;

            goto Exit;
        }

        DPRINT1("ACPIBusIrpQueryResourceRequirements: CrsStatus %X\n", CrsStatus);
        ASSERT(FALSE);
    }

    if (!IoResource)
    {
        if (!(DeviceExtension->Flags & 0x0000000002000000))
            goto Exit;

        Status = STATUS_UNSUCCESSFUL;
    }

    if (DeviceExtension->Flags & 0x0000000002000000)
    {
        //ACPIRangeValidatePciResources(DeviceExtension, IoResource);

        Status = ACPIRangeSubtract(&IoResource, RootDeviceExtension->ResourceList);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIBusIrpQueryResourceRequirements: Status %X\n", Status);
            ASSERT(NT_SUCCESS(Status));
            ExFreePool(IoResource);
            IoResource = NULL;
        }

        //ACPIRangeValidatePciResources(DeviceExtension, IoResource);
    }
    else if (DeviceExtension->Flags & 0x0000000200000000)
    {
        DPRINT1("ACPIBusIrpQueryResourceRequirements: FIXME\n");
        ASSERT(FALSE);
    }

    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Information = 0;
        goto Exit;
    }

    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information = (ULONG_PTR)IoResource;
    else
        Irp->IoStatus.Information = 0;

Exit:

    if (!NT_SUCCESS(Status) &&
        Status != STATUS_INSUFFICIENT_RESOURCES &&
        (DeviceExtension->Flags & 0x0000000002000000))
    {
        DPRINT1("ACPIBusIrpQueryResourceRequirements: Status %X\n", Status);
        ASSERT(FALSE);
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, 0);

    return Status;
}

NTSTATUS
NTAPI
ACPIBusIrpEject(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpSetLock(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpQueryId(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IoStack;
    BUS_QUERY_ID_TYPE IdType;
    PVOID DataBuff;
    ULONG dummy;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPIBusIrpQueryId: %p, %p\n", DeviceObject, Irp);

    Status = Irp->IoStatus.Status;
    IoStack = Irp->Tail.Overlay.CurrentStackLocation;

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);
    IdType = IoStack->Parameters.QueryId.IdType;

    if (IdType == 0)
    {
        Status = ACPIGet(DeviceExtension, 'DIH_', 0x20080036, NULL, 0, NULL, NULL, &DataBuff, &dummy);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            Status = STATUS_NOT_SUPPORTED;
            goto Finish;
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIBusIrpQueryId: Status %X\n", Status);
            goto Finish;
        }

        Irp->IoStatus.Information = (ULONG_PTR)DataBuff;
        goto Finish;
    }

    if (IdType == 1)
    {
        Status = ACPIGet(DeviceExtension, 'DIH_', 0x20080056, NULL, 0, NULL, NULL, &DataBuff, &dummy);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            Status = STATUS_NOT_SUPPORTED;
            goto Finish;
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIBusIrpQueryId: Status %X\n", Status);
            goto Finish;
        }

        Irp->IoStatus.Information = (ULONG_PTR)DataBuff;
        goto Finish;
    }

    if (IdType == 2)
    {
        Status = ACPIGet(DeviceExtension, 'DIC_', 0x20080117, NULL, 0, NULL, NULL, &DataBuff, &dummy);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            Status = STATUS_NOT_SUPPORTED;
            goto Finish;
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIBusIrpQueryId: Status %X\n", Status);
            goto Finish;
        }

        Irp->IoStatus.Information = (ULONG_PTR)DataBuff;
        goto Finish;
    }

    if (IdType == 3)
    {
        Status = ACPIGet(DeviceExtension, 'DIU_', 0x20080096, NULL, 0, NULL, NULL, &DataBuff, &dummy);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            Status = STATUS_NOT_SUPPORTED;
            goto Finish;
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ACPIBusIrpQueryId: Status %X\n", Status);
            goto Finish;
        }

        Irp->IoStatus.Information = (ULONG_PTR)DataBuff;
        goto Finish;
    }

Finish:

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, 0);

    return Status;
}

NTSTATUS
NTAPI
ACPIBusAndFilterIrpQueryPnpDeviceState(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ ULONG Param3,
    _In_ BOOLEAN Param4)
{
    PDEVICE_EXTENSION DeviceExtension;
    PAMLI_NAME_SPACE_OBJECT NsObject = NULL;
    PVOID DataBuff;
    BOOLEAN IsFoundChild;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPIBusAndFilterIrpQueryPnpDeviceState: %p, %p\n", DeviceObject, Irp);
    PAGED_CODE();

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    if (!(DeviceExtension->Flags & 0x0008000000000000))
    {
        NsObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'ATS_');
    }

    IsFoundChild = (NsObject != NULL);

    Status = ACPIGet(DeviceExtension, 'ATS_', 0x20040802, NULL, 0, NULL, NULL, &DataBuff, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIBusAndFilterIrpQueryPnpDeviceState: Status %X\n", Status);
        goto Exit;
    }

    if (DeviceExtension->Flags & 0x0000000040000000)
        Irp->IoStatus.Information |= 2;
    else  if (DeviceExtension->Flags & 0x0000000020000000)
        Irp->IoStatus.Information |= 2;
    else if (IsFoundChild || !Param4)
        Irp->IoStatus.Information &= ~2;

    if (DeviceExtension->Flags & 0x0080000000000000)
        Irp->IoStatus.Information |= 4;
    else if (IsFoundChild && !Param4)
        Irp->IoStatus.Information &= ~4;

    if ((DeviceExtension->Flags & 0x0008000000000000) ||
        (DeviceExtension->Flags & 0x0000001000000000) ||
        (DeviceExtension->Flags & 0x0000000008000000) ||
        (DeviceExtension->Flags & 0x0000000000040000))
    {
        if (DeviceExtension->Flags & 0x0000000000200000)
            Irp->IoStatus.Information |= 0x20;

        goto Exit;
    }

    NsObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, 'SID_');

    if (Param4)
    {
        if (!NsObject)
        {
            NsObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, '3SP_');
            if (!NsObject)
                NsObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, '0RP_');
        }

        if (DeviceExtension->Flags & 0x0000000000200000)
            NsObject = NULL;

        if (NsObject)
            Irp->IoStatus.Information &= ~0x20;

        goto Exit;
    }

    if (!NsObject)
    {
        NsObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, '3SP_');
        if (!NsObject)
            NsObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, '0RP_');
    }

    if (DeviceExtension->Flags & 0x0000000000200000)
        NsObject = NULL;

    if (!NsObject)
        Irp->IoStatus.Information |= 0x20;

Exit:
    DPRINT("ACPIBusAndFilterIrpQueryPnpDeviceState: Irp->IoStatus.Information %p\n", Irp->IoStatus.Information);

    return Status;
}

NTSTATUS
NTAPI
ACPIBusIrpQueryPnpDeviceState(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PAGED_CODE();
    return ACPIIrpInvokeDispatchRoutine(DeviceObject, Irp, 0, ACPIBusAndFilterIrpQueryPnpDeviceState, TRUE, TRUE);
}

NTSTATUS
NTAPI
ACPIBusIrpQueryBusInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpDeviceUsageNotification(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpSurpriseRemoval(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* PDO Power FUNCTIOS *******************************************************/

NTSTATUS
NTAPI
ACPIDispatchPowerIrpUnhandled(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpSetPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpQueryPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* Internal Device FUNCTIOS *************************************************/

NTSTATUS
NTAPI
ACPIInternalDeviceQueryDeviceRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    DPRINT("ACPIInternalDeviceQueryDeviceRelations: DeviceObject %p\n", DeviceObject);
    PAGED_CODE();

    IoStack = Irp->Tail.Overlay.CurrentStackLocation;

    if (IoStack->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
    {
        DPRINT("ACPIInternalDeviceQueryDeviceRelations: Unhandled Type %X\n", IoStack->Parameters.QueryDeviceRelations.Type);
        Status = Irp->IoStatus.Status;
        goto Exit;
    }

    DPRINT1("ACPIInternalDeviceQueryDeviceRelations: FIXME\n");
    ASSERT(FALSE);

Exit:

    IoCompleteRequest(Irp, 0);
    return Status;
}
NTSTATUS
NTAPI
ACPIInternalDeviceQueryCapabilities(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IoStack;
    PDEVICE_CAPABILITIES Capabilities;
    NTSTATUS Status;

    DPRINT("ACPIInternalDeviceQueryCapabilities: DeviceObject %p\n", DeviceObject);
    PAGED_CODE();

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    Capabilities = IoStack->Parameters.DeviceCapabilities.Capabilities;

    if (DeviceExtension->InstanceID)
        Capabilities->UniqueID = 1;
    else
        Capabilities->UniqueID = 0;

    if (DeviceExtension->Flags & 0x0000000000020000)
        Capabilities->RawDeviceOK = 1;
    else
        Capabilities->RawDeviceOK = 0;

    Capabilities->SilentInstall = 1;

    Status = ACPISystemPowerQueryDeviceCapabilities(DeviceExtension, Capabilities);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIInternalDeviceQueryCapabilities: Could query device capabilities - %X", Status);
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

NTSTATUS
NTAPI
ACPIInternalDeviceClockIrpStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* Internal Device Power FUNCTIOS *******************************************/

NTSTATUS
NTAPI
ACPIDispatchPowerIrpInvalid(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDispatchPowerIrpSuccess(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* Fixed Button FUNCTIOS ****************************************************/

NTSTATUS
NTAPI
ACPIButtonDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIInternalSetDeviceInterface(
    IN PDEVICE_OBJECT DeviceObject,
    IN LPGUID InterfaceGuid)
{
    UNICODE_STRING SymbolicLinkName;
    NTSTATUS Status;

    Status = IoRegisterDeviceInterface(DeviceObject, InterfaceGuid, NULL, &SymbolicLinkName);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIInternalSetDeviceInterface: IoRegisterDeviceInterface ret %X", Status);
        return Status;
    }

    Status = IoSetDeviceInterfaceState(&SymbolicLinkName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIInternalSetDeviceInterface: IoSetDeviceInterfaceState ret %X", Status);
    }

    return Status;
}

NTSTATUS
NTAPI
ACPIButtonStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    NTSTATUS Status;

    Status = ACPIInternalSetDeviceInterface(DeviceObject, (LPGUID)&GUID_DEVICE_SYS_BUTTON);

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, 0);

    return Status;
}

NTSTATUS
NTAPI
ACPICMPowerButtonStart(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPICMButtonSetPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPICMSleepButtonStart(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* Thermal Device FUNCTIOS **************************************************/

NTSTATUS
NTAPI
ACPIThermalFanStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIThermalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIThermalStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIThermalWmi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
ACPIThermalWorker(
    _In_ struct _DEVICE_EXTENSION* DeviceExtension,
    _In_ ULONG Param2)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* Lid FUNCTIOS **************************************************************/

NTSTATUS
NTAPI
ACPICMLidStart(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPICMLidSetPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
ACPICMLidWorker(
    _In_ struct _DEVICE_EXTENSION* DeviceExtension,
    _In_ ULONG Param2)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* Dock Pdo FUNCTIOS ********************************************************/

NTSTATUS
NTAPI
ACPIDockIrpStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpRemoveDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpQueryDeviceRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpQueryInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpQueryCapabilities(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpEject(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpSetLock(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpQueryID(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpQueryPnpDeviceState(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpSetPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDockIrpQueryPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* Processor Device FUNCTIOS ************************************************/

NTSTATUS
NTAPI
ACPIProcessorDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIProcessorStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* IRP dispatch FUNCTIOS ****************************************************/

ULONG
NTAPI
RtlSizeOfCmResourceList(
    _In_ PCM_RESOURCE_LIST CmResource)
{
    PCM_FULL_RESOURCE_DESCRIPTOR FullList;
    ULONG FinalSize;
    ULONG ix;
    ULONG jx;

    PAGED_CODE();

    FinalSize = sizeof(CM_RESOURCE_LIST);

    for (ix = 0; ix < CmResource->Count; ix++)
    {
        FullList = &CmResource->List[ix];

        if (ix != 0)
            FinalSize += sizeof(CM_FULL_RESOURCE_DESCRIPTOR);

        for (jx = 0; jx < FullList->PartialResourceList.Count; jx++)
        {
            if (jx != 0)
                FinalSize += sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
        }
    }

    return FinalSize;
}

PCM_RESOURCE_LIST
NTAPI
RtlDuplicateCmResourceList(
    _In_ POOL_TYPE PoolType,
    _In_ PCM_RESOURCE_LIST CmResource,
    _In_ ULONG Tag)
{
    PCM_RESOURCE_LIST OutCmResource;
    ULONG Size;

    PAGED_CODE();
    DPRINT("RtlDuplicateCmResourceList: %X, %p, %X\n", PoolType, CmResource, Tag);

    Size = RtlSizeOfCmResourceList(CmResource);

    OutCmResource = ExAllocatePoolWithTag(PoolType, Size, Tag);
    if (OutCmResource)
        RtlCopyMemory(OutCmResource, CmResource, Size);

    return OutCmResource;
}

PCM_PARTIAL_RESOURCE_DESCRIPTOR
NTAPI
RtlUnpackPartialDesc(
    _In_ UCHAR Type,
    _In_ PCM_RESOURCE_LIST CmResource,
    _Inout_ ULONG* OutStartIndex)
{
    ULONG Index = 0;
    ULONG ix;
    ULONG jx;

    if (OutStartIndex)
    {
        DPRINT("RtlUnpackPartialDesc: %X, %p, %X\n", Type, CmResource, *OutStartIndex);
    }
    else
    {
        DPRINT("RtlUnpackPartialDesc: %X, %p\n", Type, CmResource);
    }

    for (ix = 0; ix < CmResource->Count; ix++)
    {
        for (jx = 0; jx < CmResource->List[ix].PartialResourceList.Count; jx++)
        {
            if (CmResource->List[ix].PartialResourceList.PartialDescriptors[jx].Type == Type)
            {
                if (Index == *OutStartIndex)
                {
                    (*OutStartIndex)++;
                    return &CmResource->List[ix].PartialResourceList.PartialDescriptors[jx];
                }

                Index++;
            }
        }
    }

    return NULL;
}

VOID
NTAPI
ACPIInterruptServiceRoutineDPC(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    UNIMPLEMENTED_DBGBREAK();
}

BOOLEAN
NTAPI
ACPIInterruptServiceRoutine(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
OSInterruptVector(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDesc;
    PDEVICE_EXTENSION DeviceExtension;
    ULONG StartIndex = 0;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("OSInterruptVector: %p\n", DeviceObject);

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    PartialDesc = RtlUnpackPartialDesc(CmResourceTypeInterrupt, DeviceExtension->ResourceList, &StartIndex);
    if (!PartialDesc)
    {
        DPRINT1("OSInterruptVector: Could not find interrupt descriptor\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 1, (ULONG_PTR)DeviceExtension, (ULONG_PTR)&DeviceExtension->ResourceList->Count, 1);
    }

    KeInitializeDpc(&DeviceExtension->Fdo.InterruptDpc, ACPIInterruptServiceRoutineDPC, DeviceExtension);

    Status = IoConnectInterrupt(&DeviceExtension->Fdo.InterruptObject,
                                ACPIInterruptServiceRoutine,
                                DeviceExtension,
                                NULL,
                                PartialDesc->u.Interrupt.Vector,
                                (KIRQL)PartialDesc->u.Generic.Start.LowPart,
                                (KIRQL)PartialDesc->u.Generic.Start.LowPart,
                                LevelSensitive,
                                CmResourceShareShared,
                                PartialDesc->u.Interrupt.Affinity,
                                FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSInterruptVector: Could not connected to interrupt (%X)\n", Status);
        return FALSE;
    }

    ((PHAL_ACPI_TIMER_INIT)(PmHalDispatchTable->Function[0]))(NULL, FALSE);

    return TRUE;
}

/* DDB - Differentiated Definition Block */
NTSTATUS
NTAPI
ACPIInitializeDDB(
    _In_ ULONG Index)
{
    HANDLE Handle = NULL;
    PDSDT Dsdt;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPIInitializeDDB: Index %X\n", Index);

    Dsdt = RsdtInformation->Tables[Index].Address;

    DPRINT("ACPIInitializeDDB: FIXME ACPILoadTableCheckSum()\n");

    Status = AMLILoadDDB(Dsdt, &Handle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIInitializeDDB: AMLILoadDDB failed 0x%8x\n", Status);
        ASSERTMSG("ACPIInitializeDDB: AMLILoadDDB failed to load DDB\n", 0);
        KeBugCheckEx(0xA5, 0x11, 8, (ULONG_PTR)Dsdt, Dsdt->Header.CreatorRev);
    }

    RsdtInformation->Tables[Index].Flags |= 2;
    RsdtInformation->Tables[Index].Handle = Handle;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIInitializeDDBs(VOID)
{
    ULONG NumElements;
    ULONG index;
    ULONG ix;
    ULONG Flags;
    NTSTATUS Status;

    PAGED_CODE();

    NumElements = RsdtInformation->NumElements;
    if (!NumElements)
    {
        DPRINT1("ACPInitializeDDBs: No tables found in RSDT\n");
        ASSERTMSG("ACPIInitializeDDBs: No tables found in RSDT\n", NumElements != 0);
        return STATUS_ACPI_INVALID_TABLE;
    }

    index = (NumElements - 1);
    Flags = RsdtInformation->Tables[index].Flags;

    if (!(Flags & 1) || !(Flags & 4))
    {
        DPRINT1("ACPInitializeDDB: DSDT not mapped or loadable\n");

        ASSERTMSG("ACPIInitializeDDB: DSDT not mapped\n", (RsdtInformation->Tables[index].Flags & 1));//RSDTELEMENT_MAPPED
        ASSERTMSG("ACPIInitializeDDB: DSDT not loadable\n", (RsdtInformation->Tables[index].Flags & 4));//RSDTELEMENT_LOADABLE

        return STATUS_ACPI_INVALID_TABLE;
    }

    Status = ACPIInitializeDDB(index);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPInitializeDDBs: Status %X\n", Status);
        return Status;
    }

    if (NumElements == 1)
        return STATUS_SUCCESS;

    ix = 0;
    while (TRUE)
    {
        Flags = RsdtInformation->Tables[ix].Flags;

        if ((Flags & 1) && (Flags & 4))
        {
            Status = ACPIInitializeDDB(ix);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("ACPInitializeDDBs: Status %X\n", Status);
                break;
            }
        }

        ix++;
        if (ix >= index)
            return STATUS_SUCCESS;
    }

    return Status;
}

BOOLEAN
NTAPI
ACPIInitialize(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PRSDT RootSystemDescTable;
    NTSTATUS Status;
    BOOLEAN Result;

    PAGED_CODE();

    Status = ACPIInitializeAMLI();
    if (!NT_SUCCESS(Status))
    {
        ASSERTMSG("ACPIInitialize: AMLI failed initialization\n", NT_SUCCESS(Status));
        KeBugCheckEx(0xA5, 0x11, 0, 0, 0);
    }

    RootSystemDescTable = ACPILoadFindRSDT();
    if (!RootSystemDescTable)
    {
        ASSERTMSG("ACPIInitialize: ACPI RSDT Not Found\n", RootSystemDescTable);
        KeBugCheckEx(0xA5, 0x11, 1, 0, 0);
    }

    DPRINT("ACPIInitalize: ACPI RSDT found at %p \n", RootSystemDescTable);

    ACPIInterfaceTable.Size = sizeof(ACPIInterfaceTable);
    ACPIInterfaceTable.Version = 1;
    ACPIInterfaceTable.Context = DeviceObject;

    ACPIInterfaceTable.InterfaceReference = AcpiNullReference;
    ACPIInterfaceTable.InterfaceDereference = AcpiNullReference;

    ACPIInterfaceTable.GpeConnectVector = ACPIVectorConnect;
    ACPIInterfaceTable.GpeDisconnectVector = ACPIVectorDisconnect;
    ACPIInterfaceTable.GpeEnableEvent = ACPIVectorEnable;
    ACPIInterfaceTable.GpeDisableEvent = ACPIVectorDisable;
    ACPIInterfaceTable.GpeClearStatus = ACPIVectorClear;

    ACPIInterfaceTable.RegisterForDeviceNotifications = ACPIRegisterForDeviceNotifications;
    ACPIInterfaceTable.UnregisterForDeviceNotifications = ACPIUnregisterForDeviceNotifications;

    KeInitializeSpinLock(&NotifyHandlerLock);
    KeInitializeSpinLock(&GpeTableLock);

    RtlZeroMemory(ProcessorList, sizeof(ProcessorList));

    AcpiInformation = ExAllocatePoolWithTag(NonPagedPool, sizeof(*AcpiInformation), 'ipcA');
    if (!AcpiInformation)
    {
        ASSERTMSG("ACPIInitialize: Could not allocate AcpiInformation\n", AcpiInformation);
        KeBugCheckEx(0xA5, 0x11, 2, 0, 0);
    }

    RtlZeroMemory(AcpiInformation, sizeof(*AcpiInformation));

    AcpiInformation->ACPIOnly = TRUE;
    AcpiInformation->RootSystemDescTable = RootSystemDescTable;

    KeInitializeSpinLock(&AcpiInformation->GlobalLockQueueLock);
    InitializeListHead(&AcpiInformation->GlobalLockQueue);

    AcpiInformation->GlobalLockOwnerContext = 0;
    AcpiInformation->GlobalLockOwnerDepth = 0;

    Status = ACPILoadProcessRSDT();
    if (!NT_SUCCESS(Status))
    {
        ASSERTMSG("ACPIInitialize: ACPILoadProcessRSDT Failed\n", NT_SUCCESS(Status));
        KeBugCheckEx(0xA5, 0x11, 3, 0, 0);
    }

    ACPIEnableInitializeACPI(FALSE);

    Status = ACPIInitializeDDBs();
    if (!NT_SUCCESS(Status))
    {
        ASSERTMSG("ACPIInitialize: ACPIInitializeLoadDDBs Failed\n", NT_SUCCESS(Status));
        KeBugCheckEx(0xA5, 0x11, 4, 0, 0);
    }

    Result = OSInterruptVector(DeviceObject);
    if (!Result)
    {
        ASSERTMSG("ACPIInitialize: OSInterruptVector Failed!!\n", Result);
        KeBugCheckEx(0xA5, 0x11, 5, 0, 0);
    }

    DPRINT("ACPIInitialize: FIXME ACPIInitializeKernelTableHandler()\n");

    return TRUE;
}

NTSTATUS
NTAPI
NotifyHalWithMachineStates(VOID)
{
    PACPI_PM_DISPATCH_TABLE HalAcpiDispatchTable = (PVOID)PmHalDispatchTable;
    PAMLI_NAME_SPACE_OBJECT NsObject = NULL;
    PHALP_STATE_DATA StateData = NULL;
    AMLI_OBJECT_DATA DataArgs;
    SYSTEM_POWER_STATE State;
    ULONG ix;
    NTSTATUS Status;

    PAGED_CODE();

    for (ix = 0; ix < 32; ix++)
    {
        if (!ProcessorList[ix])
            break;
    }

    DPRINT("NotifyHalWithMachineStates: Number of processors - %X\n", ix);

    StateData = ExAllocatePoolWithTag(NonPagedPool, (5 * sizeof(*StateData)), 'MpcA');
    if (!StateData)
    {
        DPRINT1("NotifyHalWithMachineStates: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (AcpiOverrideAttributes & 4)
    {
        DPRINT1("NotifyHalWithMachineStates: FIXME\n");
        ASSERT(FALSE);
    }

    AcpiSupportedSystemStates = 0x62;

    State = 2;

    for (ix = 0; ix < 5; ix++, State++)
    {
        if ((State == 2 && (AcpiOverrideAttributes & 0x10)) ||
            (State == 3 && (AcpiOverrideAttributes & 0x20)) ||
            (State == 4 && (AcpiOverrideAttributes & 0x40)))
        {
            DPRINT("NotifyHalWithMachineStates: SleepState '%s' disabled due to override\n", StateName[ix]);
            ASSERT(FALSE);

            continue;
        }

        Status = AMLIGetNameSpaceObject(StateName[ix], NULL, &NsObject, 0);
        if (!NT_SUCCESS(Status))
        {
            DPRINT("NotifyHalWithMachineStates: SleepState '%s' not supported\n", StateName[ix]);
            //ASSERT(FALSE);

            StateData[ix].Data0 = 0;

            DPRINT("NotifyHalWithMachineStates: FIXME ZwPowerInformation(SystemPowerLoggingEntry)\n");
            continue;
        }

        DPRINT("NotifyHalWithMachineStates: FIXME (check override State)\n");

        AcpiSupportedSystemStates |= (1 << State);
        StateData[ix].Data0 = 1;

        AMLIEvalPackageElement(NsObject, 0, &DataArgs);
        StateData[ix].Data1 = (UCHAR)(ULONG)DataArgs.DataValue;
        AMLIFreeDataBuffs(&DataArgs, 1);

        AMLIEvalPackageElement(NsObject, 1, &DataArgs);
        StateData[ix].Data2 = (UCHAR)(ULONG)DataArgs.DataValue;
        AMLIFreeDataBuffs(&DataArgs, 1);
    }

    HalAcpiDispatchTable->HalAcpiMachineStateInit(0, StateData, &InterruptModel);

    ExFreePoolWithTag(StateData, 'MpcA');

    if (!InterruptModel)
        return Status;

    Status = AMLIGetNameSpaceObject("\\_PIC", NULL, &NsObject, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NotifyHalWithMachineStates: Status %X\n", Status);
        return Status;
    }

    RtlZeroMemory(&DataArgs, sizeof(DataArgs));

    DataArgs.DataValue = (PVOID)InterruptModel;
    DataArgs.DataType = 1;

    Status = AMLIEvalNameSpaceObject(NsObject, NULL, 1, &DataArgs);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NotifyHalWithMachineStates: KeBugCheckEx()\n");
        ASSERT(FALSE);
        KeBugCheckEx(0xA5, 0x2001, InterruptModel, Status, (ULONG_PTR)NsObject);
    }

    return Status;
}

NTSTATUS
NTAPI
ACPIInternalRegisterPowerCallBack(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PCALLBACK_FUNCTION CallbackFunction)
{
    PCALLBACK_OBJECT CallbackObject;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING NameString;
    NTSTATUS Status;

    if (DeviceExtension->Flags & 0x4000000000000000)
        return STATUS_SUCCESS;

    ACPIInternalUpdateFlags(DeviceExtension, 0x4000000000000000, FALSE);

    RtlInitUnicodeString( &NameString, L"\\Callback\\PowerState" );

    InitializeObjectAttributes(&ObjectAttributes, &NameString, (OBJ_PERMANENT | OBJ_CASE_INSENSITIVE), NULL, NULL);

    Status = ExCreateCallback(&CallbackObject, &ObjectAttributes, FALSE, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIInternalRegisterPowerCallBack: Failed to register callback %X", Status);
        ACPIInternalUpdateFlags(DeviceExtension, 0x4000000000000000, TRUE);
        return STATUS_SUCCESS;
    }

    ExRegisterCallback(CallbackObject, CallbackFunction, DeviceExtension);

    return Status;
}

VOID
NTAPI
ACPIRootPowerCallBack(
    _In_ PVOID CallbackContext,
    _In_ PVOID Argument1,
    _In_ PVOID Argument2)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
ACPIInitStartACPI(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_EXTENSION DeviceExtension;
    KEVENT Event;
    KIRQL DeviceTreeIrql;
    KIRQL PowerQueueIrql;
    NTSTATUS Status;

    DPRINT("ACPIInitStartACPI: DeviceObject %p\n", DeviceObject);

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &DeviceTreeIrql);
    AcpiSystemInitialized = FALSE;
    KeReleaseSpinLock(&AcpiDeviceTreeLock, DeviceTreeIrql);

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Status = ACPIBuildSynchronizationRequest(DeviceExtension, ACPIDevicePowerNotifyEvent, &Event, &AcpiBuildDeviceList, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ACPIInitStartACPI: Status %X\n", Status);
        return Status;
    }

    if (!ACPIInitialize(DeviceObject))
    {
        DPRINT1("ACPIInitStartACPI: STATUS_DEVICE_DOES_NOT_EXIST\n");
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    DPRINT("ACPIInitStartACPI: Status %X\n", Status);

    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    DPRINT("ACPIInitStartACPI: Status %X\n", Status);

    NotifyHalWithMachineStates();

    ACPIInternalRegisterPowerCallBack(DeviceExtension, ACPIRootPowerCallBack);

    KeAcquireSpinLock(&AcpiPowerQueueLock, &PowerQueueIrql);
    if (!AcpiPowerDpcRunning)
        KeInsertQueueDpc(&AcpiPowerDpc, NULL, NULL);
    KeReleaseSpinLock(&AcpiPowerQueueLock, PowerQueueIrql);

    KeAcquireSpinLock(&AcpiDeviceTreeLock, &DeviceTreeIrql);
    AcpiSystemInitialized = TRUE;
    KeReleaseSpinLock(&AcpiDeviceTreeLock, DeviceTreeIrql);

    AcpiInitIrqArbiter(DeviceObject);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIRootIrpStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_EXTENSION DeviceExtension;
    PCM_RESOURCE_LIST CmTranslated;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("ACPIRootIrpStartDevice: %p, %p\n", DeviceObject, Irp);

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, ACPIRootIrpCompleteRoutine, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(DeviceExtension->TargetDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    IoStack = Irp->Tail.Overlay.CurrentStackLocation;

    if (NT_SUCCESS(Status))
    {
        CmTranslated = IoStack->Parameters.StartDevice.AllocatedResourcesTranslated;
        if (CmTranslated)
            CmTranslated = RtlDuplicateCmResourceList(NonPagedPool, CmTranslated, 'RpcA');

        DeviceExtension->ResourceList = CmTranslated;
        if (!DeviceExtension->ResourceList)
        {
            DPRINT1("ACPIRootIrpStartDevice: Did not find a resource list! KeBugCheckEx()\n");
            ASSERT(FALSE);
            KeBugCheckEx(0xA5, 1, (ULONG_PTR)DeviceExtension, 0, 0);
        }

        Status = ACPIInitStartACPI(DeviceObject);
        if (NT_SUCCESS(Status))
            DeviceExtension->DeviceState = Started;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, 0);

    return Status;
}

PAMLI_NAME_SPACE_OBJECT
NTAPI
OSConvertDeviceHandleToPNSOBJ(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_EXTENSION DeviceExtension;

    if (!DeviceObject)
    {
        ASSERT(DeviceObject != NULL);
        return NULL;
    }

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);
    if (!DeviceExtension)
    {
        ASSERT(DeviceExtension != NULL);
        return NULL;
    }

    return DeviceExtension->AcpiObject;
}

NTSTATUS
NTAPI
ACPIIoctlEvalPreProcessing(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IoStack,
    _In_ POOL_TYPE PoolType,
    _Out_ PAMLI_NAME_SPACE_OBJECT* OutNsObject,
    _Out_ PAMLI_OBJECT_DATA* OutDataResult,
    _Out_ PAMLI_OBJECT_DATA* OutDataArgs,
    _Out_ ULONG* OutArgsCount)
{
    PACPI_EVAL_INPUT_BUFFER InBuffer;
    PAMLI_NAME_SPACE_OBJECT ScopeNsObject;
    PAMLI_NAME_SPACE_OBJECT NsObject;
    PAMLI_OBJECT_DATA DataArgs = NULL;
    PAMLI_OBJECT_DATA DataResult = NULL;
    CHAR ObjPath[8];
    ULONG ArgsCount = 0;
    ULONG InBufferLength;
    ULONG OutBufferLength;
    NTSTATUS Status;

    DPRINT("ACPIIoctlEvalPreProcessing: %p, %p\n", DeviceObject, Irp);

    InBufferLength = IoStack->Parameters.DeviceIoControl.InputBufferLength;
    OutBufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    Irp->IoStatus.Information = 0;

    if (InBufferLength < sizeof(ACPI_EVAL_INPUT_BUFFER))
    {
        DPRINT1("ACPIIoctlEvalPreProcessing: STATUS_INFO_LENGTH_MISMATCH. InBufferLength %X\n", InBufferLength);
        ASSERT(FALSE);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (OutBufferLength && OutBufferLength < sizeof(ACPI_EVAL_OUTPUT_BUFFER))
    {
        DPRINT1("ACPIIoctlEvalPreProcessing: STATUS_INFO_LENGTH_MISMATCH. OutBufferLength %X\n", OutBufferLength);
        ASSERT(FALSE);
        return STATUS_BUFFER_TOO_SMALL;
    }

    InBuffer = Irp->AssociatedIrp.SystemBuffer;

    RtlZeroMemory(ObjPath, sizeof(ObjPath));

    ASSERT(sizeof(InBuffer->MethodName) <= sizeof(ObjPath));
    RtlCopyMemory(ObjPath, InBuffer->MethodName, sizeof(InBuffer->MethodName));

    ScopeNsObject = OSConvertDeviceHandleToPNSOBJ(DeviceObject);
    if (!ScopeNsObject)
    {
        DPRINT("ACPIIoctlEvalPreProcessing: STATUS_NO_SUCH_DEVICE\n");
        ASSERT(FALSE);
        return STATUS_NO_SUCH_DEVICE;
    }

    Status = AMLIGetNameSpaceObject(ObjPath, ScopeNsObject, &NsObject, 1);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIIoctlEvalPreProcessing: Status %X\n", Status);
        return Status;
    }

    DataResult = ExAllocatePoolWithTag(PoolType, sizeof(*DataResult), 'RcpA');
    if (!DataResult)
    {
        DPRINT("ACPIIoctlEvalPreProcessing: STATUS_INSUFFICIENT_RESOURCES\n");
        ASSERT(FALSE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (InBuffer->Signature == 'BieA')
        goto Exit;

    if (InBuffer->Signature == 'CieA')
    {
        DPRINT1("ACPIIoctlEvalPreProcessing: FIXME\n");
        ASSERT(FALSE);
        goto Exit;
    }

    if (InBuffer->Signature != 'IieA' &&
        InBuffer->Signature != 'SieA')
    {
        DPRINT("ACPIIoctlEvalPreProcessing: Unknown signature %X\n", InBuffer->Signature);
        ASSERT(FALSE);
        return STATUS_INVALID_PARAMETER_1;
    }

    DPRINT1("ACPIIoctlEvalPreProcessing: FIXME\n");
    ASSERT(FALSE);

Exit:

    *OutNsObject = NsObject;
    *OutDataResult = DataResult;
    *OutDataArgs = DataArgs;
    *OutArgsCount = ArgsCount;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ACPIIoctlEvalPostProcessing(
    _In_ PIRP Irp,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIIoctlEvalControlMethod(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IoStack)
{
    PAMLI_NAME_SPACE_OBJECT NsObject;
    PAMLI_OBJECT_DATA DataResult = NULL;
    PAMLI_OBJECT_DATA DataArgs = NULL;
    ULONG ArgsCount = 0;
    NTSTATUS Status;

    DPRINT("ACPIIoctlEvalControlMethod: %p, %p, %p\n", DeviceObject, Irp, IoStack->Parameters.DeviceIoControl.IoControlCode);

    Status = ACPIIoctlEvalPreProcessing(DeviceObject, Irp, IoStack, PagedPool, &NsObject, &DataResult, &DataArgs, &ArgsCount);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIIoctlEvalControlMethod: Status %X\n", Status);
        goto Exit;
    }

    Status = AMLIEvalNameSpaceObject(NsObject, DataResult, ArgsCount, DataArgs);

    if (DataArgs)
        ExFreePool(DataArgs);

    if (NT_SUCCESS(Status))
    {
        Status = ACPIIoctlEvalPostProcessing(Irp, DataResult);
        AMLIFreeDataBuffs(DataResult, 1);
    }

Exit:

    if (DataResult)
        ExFreePool(DataResult);

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, 0);

    return Status;
}

NTSTATUS
NTAPI
ACPIIrpDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    DPRINT("ACPIIrpDispatchDeviceControl: %p, %p, %X\n", DeviceObject, Irp, IoStack->Parameters.DeviceIoControl.IoControlCode);

    if (Irp->RequestorMode != KernelMode)
        return ACPIDispatchForwardIrp(DeviceObject, Irp);

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case 0x32C000:
            DPRINT1("ACPIIrpDispatchDeviceControl: FIXME\n");
            ASSERT(FALSE);
            break;

        case 0x32C004:
            Status = ACPIIoctlEvalControlMethod(DeviceObject, Irp, IoStack);
            break;

        case 0x32C008:
            DPRINT1("ACPIIrpDispatchDeviceControl: FIXME\n");
            ASSERT(FALSE);
            break;

        case 0x32C00C:
            DPRINT1("ACPIIrpDispatchDeviceControl: FIXME\n");
            ASSERT(FALSE);
            break;

        case 0x32C010:
            DPRINT1("ACPIIrpDispatchDeviceControl: FIXME\n");
            ASSERT(FALSE);
            break;

        case 0x32C014:
            DPRINT1("ACPIIrpDispatchDeviceControl: FIXME\n");
            ASSERT(FALSE);
            break;

        default:
            return ACPIDispatchForwardIrp(DeviceObject, Irp);
    }

    return Status;
}

NTSTATUS
NTAPI
ACPIDispatchForwardIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT TargetDeviceObject;
    PIO_STACK_LOCATION IoStack;
    UCHAR MajorFunction;
    NTSTATUS Status;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    MajorFunction = IoStack->MajorFunction;

    DPRINT("ACPIDispatchForwardIrp: %p, %p, (%X:%X)\n", DeviceObject, Irp, MajorFunction, IoStack->MinorFunction);

    DeviceExtension = ACPIInternalGetDeviceExtension(DeviceObject);

    TargetDeviceObject = DeviceExtension->TargetDeviceObject;
    if (TargetDeviceObject)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        Status = IoCallDriver(TargetDeviceObject, Irp);
        return Status;
    }

    ASSERT(MajorFunction == IRP_MJ_PNP ||
           MajorFunction == IRP_MJ_DEVICE_CONTROL ||
           MajorFunction == IRP_MJ_SYSTEM_CONTROL);

    Status = Irp->IoStatus.Status;
    IoCompleteRequest(Irp, 0);

    return Status;
}

NTSTATUS
NTAPI
ACPIDispatchIrpSuccess(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDispatchWmiLog(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDispatchIrpInvalid(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIBusIrpUnhandled(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    IoCompleteRequest(Irp, 0);
    return Irp->IoStatus.Status;
}

/* FUNCTIOS *****************************************************************/

ULONGLONG
NTAPI
ACPIInternalUpdateFlags(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG InputFlags,
    _In_ BOOLEAN IsResetFlags)
{
    ULONGLONG ReturnFlags;
    ULONGLONG ExChange;
    ULONGLONG Comperand;

    if (IsResetFlags)
    {
        ReturnFlags = DeviceExtension->Flags;
        do
        {
            Comperand = ReturnFlags;
            ExChange = Comperand & ~InputFlags;

            ReturnFlags = ExInterlockedCompareExchange64((PLONGLONG)DeviceExtension, (PLONGLONG)&ExChange, (PLONGLONG)&Comperand, NULL);
        }
        while (Comperand != ReturnFlags);
    }
    else
    {
        ReturnFlags = DeviceExtension->Flags;
        do
        {
            Comperand = ReturnFlags;
            ExChange = Comperand | InputFlags;

            ReturnFlags = ExInterlockedCompareExchange64((PLONGLONG)DeviceExtension, (PLONGLONG)&ExChange, (PLONGLONG)&Comperand, NULL);
        }
        while (Comperand != ReturnFlags);
    }

    return ReturnFlags;
}


/* EOF */
