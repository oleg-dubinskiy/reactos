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

extern NPAGED_LOOKASIDE_LIST BuildRequestLookAsideList;
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
extern KDPC AcpiBuildDpc;
extern BOOLEAN AcpiBuildDpcRunning;
extern BOOLEAN AcpiBuildWorkDone;
extern PRSDTINFORMATION RsdtInformation;

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

NTSTATUS
NTAPI
ACPIBuildProcessGenericComplete(
    _In_ PACPI_BUILD_REQUEST Entry)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
        DPRINT("ACPIGetWorkerForInteger: FIXME\n");
        ASSERT(FALSE);
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
        DPRINT1("ACPIGet: FIXME\n");
        ASSERT(FALSE);
    }
    else if ((Flags & 0x1F0000) == 0x20000)
    {
        DPRINT1("ACPIGet: FIXME\n");
        ASSERT(FALSE);
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
        DPRINT1("ACPIGet: FIXME\n");
        ASSERT(FALSE);
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
        DPRINT1("ACPIGet: FIXME (%p)\n", DataArgs);
        ASSERT(FALSE);
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
    ULONG Flags; // ?
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

    Flags = (ULONG)BuildRequest->Context;
    if ( !(Flags & 1))
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
    ULONG Flags;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPIBuildProcessRunMethodPhaseCheckBridge: %p\n", BuildRequest);

    Flags = (ULONG)BuildRequest->Context;

    if ((Flags & 1) && (DeviceExtension->Flags & 2))
    {
        BuildRequest->BuildReserved1 = 0;
        ACPIBuildCompleteMustSucceed(NULL, Status, 0, BuildRequest);
        return Status;
    }

    BuildRequest->BuildReserved1 = 5;

    if (!(Flags & 0x40))
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
    ULONG flags;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ACPIBuildProcessRunMethodPhaseRunMethod: %p\n", BuildRequest);

    if (!(((ULONG)BuildRequest->Context & 0x40) == 0) && BuildRequest->ListHeadForInsert)
    {
        DPRINT("ACPIBuildProcessRunMethodPhaseRunMethod: Is PCI-PCI bridge\n");
        BuildRequest->BuildReserved1 = 0;
        goto Exit;
    }

    BuildRequest->BuildReserved1 = 6;

    NsObject = ACPIAmliGetNamedChild(DeviceExtension->AcpiObject, (ULONG)BuildRequest->ListHead1);//?
    if (!NsObject)
    {
        goto Exit;
    }

    if ((ULONG)BuildRequest->Context & 2)
    {
        if ((ACPIInternalUpdateFlags(DeviceExtension, 0x0020000000000000, FALSE) >> 0x20) & 0x200000)
            goto Exit;
    }

    else if ((ULONG)BuildRequest->Context & 8)
    {
        if (!DeviceExtension->PowerInfo.WakeSupportCount)
            goto Exit;

        RtlZeroMemory(amliData, sizeof(AMLI_OBJECT_DATA));

        amliData[0].DataType = 1;
        amliData[0].DataValue = ULongToPtr(1);

        AmliData = amliData;
        ArgsCount = 1;
    }
    else if ((ULONG)BuildRequest->Context & 0x30)
    {
        flags = ((ULONG)BuildRequest->Context | 0x40);
        BuildRequest->Context = (PVOID)flags;

        RtlZeroMemory(amliData, sizeof(amliData));

        amliData[0].DataType = 1;
        amliData[0].DataValue = (PVOID)2;

        amliData[1].DataType = 1;
        amliData[1].DataValue = (PVOID)(((ULONG)(UCHAR)flags >> 4) & 1);

        AmliData = amliData;
        ArgsCount = 2;
    }

    BuildRequest->BuildReserved2 = (ULONG)NsObject;

    Status = AMLIAsyncEvalObject(NsObject, NULL, ArgsCount, AmliData, (PVOID)ACPIBuildCompleteMustSucceed, BuildRequest);

Exit:

    if (Status == STATUS_PENDING)
        Status = STATUS_SUCCESS;
    else
        ACPIBuildCompleteMustSucceed(NsObject, Status, 0, BuildRequest);

    DPRINT("ACPIBuildProcessRunMethodPhaseRunMethod: retStatus %X\n", Status);

    return Status;
}

NTSTATUS
NTAPI
ACPIBuildProcessRunMethodPhaseRecurse(
    _In_ PACPI_BUILD_REQUEST BuildRequest)
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
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
        {
            DPRINT1("ACPIBuildDeviceDpc: FIXME\n");
            ASSERT(FALSE);
        }

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
                DPRINT1("ACPIBuildDeviceDpc: FIXME\n");
                ASSERT(FALSE);
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
