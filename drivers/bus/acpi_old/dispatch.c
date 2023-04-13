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

ACPI_INTERFACE_STANDARD ACPIInterfaceTable;
ACPI_HAL_DISPATCH_TABLE AcpiHalDispatchTable;
PPM_DISPATCH_TABLE PmHalDispatchTable;
PACPI_INFORMATION AcpiInformation;
KSPIN_LOCK NotifyHandlerLock;
KSPIN_LOCK GpeTableLock;
BOOLEAN AcpiSystemInitialized;

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

extern NPAGED_LOOKASIDE_LIST BuildRequestLookAsideList;
extern KSPIN_LOCK AcpiDeviceTreeLock;
extern KSPIN_LOCK AcpiBuildQueueLock;
extern LIST_ENTRY AcpiBuildDeviceList;
extern LIST_ENTRY AcpiBuildSynchronizationList;
extern LIST_ENTRY AcpiBuildQueueList;
extern KDPC AcpiBuildDpc;
extern BOOLEAN AcpiBuildDpcRunning;

/* FUNCTIOS *****************************************************************/

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
ACPIBuildDeviceDpc(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
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
    UNIMPLEMENTED_DBGBREAK();
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
    BuildRequest->ListHead1 = BuildDeviceList;
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

BOOLEAN
NTAPI
OSInterruptVector(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
ACPIInitializeDDBs(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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

    ACPIEnableInitializeACPI(0);

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
ACPIInitStartACPI(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_EXTENSION DeviceExtension;
    KEVENT Event;
    KIRQL DeviceTreeIrql;
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

    DPRINT1("ACPIInitStartACPI: FIXME\n");
    ASSERT(FALSE);


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
