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
#endif

/* GLOBALS *******************************************************************/

ACPI_HAL_DISPATCH_TABLE AcpiHalDispatchTable;
PPM_DISPATCH_TABLE PmHalDispatchTable;

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

PDRIVER_DISPATCH ACPIDispatchFdoPowerTable[] =
{
    ACPIWakeWaitIrp,
    ACPIDispatchForwardPowerIrp,
    ACPIRootIrpSetPower,
    ACPIRootIrpQueryPower,
    ACPIDispatchForwardPowerIrp
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

/* FUNCTIOS *****************************************************************/

VOID
NTAPI
ACPIInternalGetDispatchTable(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDEVICE_EXTENSION* OutDeviceExtension,
    _Out_ PIRP_DISPATCH_TABLE* OutIrpDispatch)
{
    UNIMPLEMENTED_DBGBREAK();
}

LONG
NTAPI
ACPIInternalDecrementIrpReferenceCount(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
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
ACPIEnableInitializeACPI(
    _In_ BOOLEAN Param1)
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

/* FDO PNP FUNCTIOS *********************************************************/

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

NTSTATUS
NTAPI
ACPIRootIrpQueryDeviceRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIRootIrpQueryInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIRootIrpQueryCapabilities(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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

/* IRP dispatch FUNCTIOS ****************************************************/

NTSTATUS
NTAPI
ACPIRootIrpStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIIrpDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ACPIDispatchForwardIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
