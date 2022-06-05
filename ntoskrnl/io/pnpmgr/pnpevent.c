
/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "../pnpio.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

BOOLEAN PiNotificationInProgress = FALSE;

extern PPNP_DEVICE_EVENT_LIST PpDeviceEventList;
extern KGUARDED_MUTEX PiNotificationInProgressLock;

extern KEVENT PiEventQueueEmpty;
extern BOOLEAN PpPnpShuttingDown;

/* FUNCTIONS *****************************************************************/

PVOID
NTAPI
PiAllocateCriticalMemory(
    _In_ PIP_TYPE_REMOVAL_DEVICE DeleteType,
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    PVOID Block;
    LARGE_INTEGER Interval;

    DPRINT("PiAllocateCriticalMemory: DeleteType %X, NumberOfBytes %X\n", DeleteType, NumberOfBytes);
    PAGED_CODE();

    ASSERT(KeGetCurrentIrql() != DISPATCH_LEVEL);

    while (TRUE)
    {
        Block = ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);

        if (Block || DeleteType == PipQueryRemove || DeleteType == PipEject)
            break;

        Interval.QuadPart = (-10000ll * 1); // 1 msec
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    return Block;
}

VOID
NTAPI
PpCompleteDeviceEvent(
    _In_ PPNP_DEVICE_EVENT_ENTRY EventEntry,
    _In_ NTSTATUS Status)
{
    DPRINT("PpCompleteDeviceEvent: EventEntry %p, Status %X\n", EventEntry, Status);
    PAGED_CODE();

    if (EventEntry->CallerEvent)
    {
        *EventEntry->Data.Result = Status;
        KeSetEvent(EventEntry->CallerEvent, IO_NO_INCREMENT, FALSE);
    }

    if (EventEntry->Callback)
    {
        DPRINT1("PpCompleteDeviceEvent: FIXME Callback\n");
        ASSERT(FALSE);
        //EventEntry->Callback(EventEntry->Context);
    }

    if (EventEntry->Data.DeviceObject)
        ObDereferenceObject(EventEntry->Data.DeviceObject);

    ExFreePoolWithTag(EventEntry, 'EEpP');
}

BOOLEAN
NTAPI
IopNotifyPnpWhenChainDereferenced(
    _In_ PDEVICE_OBJECT* RemovalDevices,
    _In_ ULONG DevicesCount,
    _In_ BOOLEAN IsQueryRemove,
    _In_ BOOLEAN IsAllowRunWorker,
    _Out_ PDEVICE_OBJECT* OutDeviceObject)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // IoDbgBreakPointEx();
    return FALSE;
}

NTSTATUS
NTAPI
PiResizeTargetDeviceBlock(
    _Out_ PPNP_DEVICE_EVENT_ENTRY*  pEventEntry,
    _In_ PIP_TYPE_REMOVAL_DEVICE RemovalType,
    _In_ PRELATION_LIST RelationsList,
    _In_ BOOLEAN IsNoInstance)
{
    PPNP_DEVICE_EVENT_ENTRY NewEventEntry;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_NODE DeviceNode;
    PWCHAR pChar;
    ULONG SizeHead;
    ULONG CurrentSize;
    ULONG NewSize;
    ULONG Marker;
    UCHAR OutIsDirectDescendant;
  
    DPRINT("PiResizeTargetDeviceBlock: EventEntry %p\n", *pEventEntry);
    PAGED_CODE();

    if (!RelationsList)
    {
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return STATUS_SUCCESS;
    }

    SizeHead = (sizeof(**pEventEntry) - sizeof(PLUGPLAY_EVENT_BLOCK));

    CurrentSize = (SizeHead + (*pEventEntry)->Data.TotalSize);
    NewSize = (CurrentSize - wcslen((*pEventEntry)->Data.TargetDevice.DeviceIds) * sizeof(WCHAR));

    DPRINT("PiResizeTargetDeviceBlock: CurrentSize %X NewSize %X\n", CurrentSize, NewSize);

    Marker = 0;
    while (IopEnumerateRelations(RelationsList, &Marker, &DeviceObject, &OutIsDirectDescendant, NULL, FALSE))
    {
        if (!IsNoInstance || OutIsDirectDescendant)
        {
            DeviceNode = IopGetDeviceNode(DeviceObject);
            if (DeviceNode)
            {
                if (DeviceNode->InstancePath.Length)
                    NewSize += (DeviceNode->InstancePath.Length + sizeof(WCHAR));
            }
        }
    }

    DPRINT("PiResizeTargetDeviceBlock: NewSize %X CurrentSize %X\n", NewSize, CurrentSize);

    ASSERT(NewSize >= CurrentSize);

    if (NewSize == CurrentSize)
        return STATUS_SUCCESS;

    if (NewSize < CurrentSize)
        NewSize = CurrentSize;

    NewEventEntry = PiAllocateCriticalMemory(RemovalType, PagedPool, NewSize, 'EEpP');
    if (!NewEventEntry)
    {
        DPRINT1("PiResizeTargetDeviceBlock: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NewEventEntry, NewSize);
    RtlCopyMemory(NewEventEntry, *pEventEntry, CurrentSize);

    NewEventEntry->Data.TotalSize = (NewSize - SizeHead);
    pChar = (NewEventEntry->Data.TargetDevice.DeviceIds + wcslen((*pEventEntry)->Data.TargetDevice.DeviceIds) + 1);

    Marker = 0;
    while (IopEnumerateRelations(RelationsList, &Marker, &DeviceObject, &OutIsDirectDescendant, NULL, FALSE))
    {
        if (DeviceObject != NewEventEntry->Data.DeviceObject && (!IsNoInstance || OutIsDirectDescendant))
        {
            DeviceNode = IopGetDeviceNode(DeviceObject);

            if (DeviceNode && DeviceNode->InstancePath.Length)
            {
                RtlCopyMemory(pChar, DeviceNode->InstancePath.Buffer, DeviceNode->InstancePath.Length);
                pChar += ((DeviceNode->InstancePath.Length / sizeof(WCHAR)) + 1);
            }
        }
    }
 
    *pChar = UNICODE_NULL;

    ExFreePool(*pEventEntry);
    *pEventEntry = NewEventEntry;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PiProcessQueryRemoveAndEject(
    _In_ PPNP_DEVICE_EVENT_ENTRY* pEventEntry)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // IoDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PiProcessTargetDeviceEvent(
    _In_ PPNP_DEVICE_EVENT_ENTRY* pEventEntry)
{
    PPNP_DEVICE_EVENT_ENTRY EventEntry;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    DPRINT("PiProcessTargetDeviceEvent: pEventEntry %p, Status %X\n", pEventEntry, Status);

    EventEntry = *pEventEntry;

    if (PiCompareGuid(&EventEntry->Data.EventGuid, &GUID_DEVICE_QUERY_AND_REMOVE))
    {
        DPRINT("PiProcessTargetDeviceEvent: GUID_DEVICE_QUERY_AND_REMOVE\n");
        Status = PiProcessQueryRemoveAndEject(pEventEntry);
        return Status;
    }

    if (PiCompareGuid(&EventEntry->Data.EventGuid, &GUID_DEVICE_EJECT))
    {
        DPRINT("PiProcessTargetDeviceEvent: GUID_DEVICE_EJECT\n");
        Status = PiProcessQueryRemoveAndEject(pEventEntry);
        return Status;
    }

    if (PiCompareGuid(&EventEntry->Data.EventGuid, &GUID_DEVICE_ARRIVAL))
    {
        DPRINT("PiProcessTargetDeviceEvent: GUID_DEVICE_ARRIVAL\n");
        DPRINT("PiProcessTargetDeviceEvent: FIXME PiNotifyUserMode()\n");
        //PiNotifyUserMode(EventEntry);
        return Status;
    }

    if (PiCompareGuid(&EventEntry->Data.EventGuid, &GUID_DEVICE_NOOP))
    {
        DPRINT("PiProcessTargetDeviceEvent: GUID_DEVICE_NOOP\n");
        return Status;
    }

    if (PiCompareGuid(&EventEntry->Data.EventGuid, &GUID_DEVICE_SAFE_REMOVAL))
    {
        DPRINT("PiProcessTargetDeviceEvent: GUID_DEVICE_SAFE_REMOVAL\n");
        DPRINT("PiProcessTargetDeviceEvent: FIXME PiNotifyUserMode()\n");
        //PiNotifyUserMode(EventEntry);
    }

    return Status;
}

VOID
NTAPI
PiWalkDeviceList(
    _In_ PVOID Context)
{
    PPNP_DEVICE_EVENT_ENTRY EventEntry;
    UNICODE_STRING SymbolicLinkName;
    PWORK_QUEUE_ITEM WorkItem = Context;
    NTSTATUS Status;

    DPRINT("PiWalkDeviceList: WorkItem %p\n", WorkItem);
    PAGED_CODE();

    Status = KeWaitForSingleObject(&PpDeviceEventList->EventQueueMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PiWalkDeviceList: Status %X\n", Status);
        KeAcquireGuardedMutex(&PiNotificationInProgressLock);
        KeSetEvent(&PiEventQueueEmpty, IO_NO_INCREMENT, FALSE);
        PiNotificationInProgress = FALSE;
        KeReleaseGuardedMutex(&PiNotificationInProgressLock);
        return;
    }

    while (TRUE)
    {
        KeAcquireGuardedMutex(&PpDeviceEventList->Lock);

        if (IsListEmpty(&PpDeviceEventList->List))
        {
            DPRINT("PiWalkDeviceList: IsListEmpty(&PpDeviceEventList->List)\n");
            break;
        }

        EventEntry = CONTAINING_RECORD(RemoveHeadList(&PpDeviceEventList->List),
                                       PNP_DEVICE_EVENT_ENTRY,
                                       ListEntry);

        DPRINT("PiWalkDeviceList: EventEntry %p, EventCategory %X\n", EventEntry, EventEntry->Data.EventCategory);

        KeReleaseGuardedMutex(&PpDeviceEventList->Lock);

        if (EventEntry->Data.DeviceObject)
        {
            PDEVICE_NODE DeviceNode;

            DeviceNode = IopGetDeviceNode(EventEntry->Data.DeviceObject);
            if (!DeviceNode)
            {
                DPRINT1("PiWalkDeviceList: continue. STATUS_NO_SUCH_DEVICE\n");
                PpCompleteDeviceEvent(EventEntry, STATUS_NO_SUCH_DEVICE);
                IopProcessDeferredRegistrations();
                continue;
            }
        }

        switch (EventEntry->Data.EventCategory)
        {
            case DeviceClassChangeEvent:
                DPRINT("PiWalkDeviceList: DeviceClassChangeEvent - kernel notifying\n");

                RtlInitUnicodeString(&SymbolicLinkName, EventEntry->Data.DeviceClass.SymbolicLinkName);

                IopNotifyDeviceClassChange(&EventEntry->Data,
                                           &EventEntry->Data.DeviceClass.ClassGuid,
                                           &SymbolicLinkName);

                DPRINT("PiWalkDeviceList: FIXME PiNotifyUserMode()\n");
                //PiNotifyUserMode(EventEntry);
                Status = STATUS_SUCCESS;
                break;

            case CustomDeviceEvent:
                DPRINT1("PiWalkDeviceList: FIXME CustomDeviceEvent()\n");
                ASSERT(FALSE); // IoDbgBreakPointEx();
                Status = STATUS_NOT_IMPLEMENTED;
                break;

            case TargetDeviceChangeEvent:
                Status = PiProcessTargetDeviceEvent(&EventEntry);
                break;

            case DeviceInstallEvent:
                DPRINT1("PiWalkDeviceList: FIXME DeviceInstallEvent - user-mode notifying\n");
                ASSERT(FALSE); // IoDbgBreakPointEx();
                //PiNotifyUserMode(EventEntry);
                Status = STATUS_SUCCESS;
                break;

            case HardwareProfileChangeEvent:
                DPRINT1("PiWalkDeviceList: FIXME HardwareProfileChangeEvent\n");
                ASSERT(FALSE); // IoDbgBreakPointEx();
                //Status = PiNotifyUserMode(EventEntry);
                break;

            case PowerEvent:
                DPRINT1("PiWalkDeviceList: FIXME PowerEvent\n");
                ASSERT(FALSE); // IoDbgBreakPointEx();
                //Status = PiNotifyUserMode(EventEntry);
                break;

            case VetoEvent:
                DPRINT1("PiWalkDeviceList: FIXME VetoEvent\n");
                ASSERT(FALSE); // IoDbgBreakPointEx();
                //Status = PiNotifyUserMode(EventEntry);
                break;

            case BlockedDriverEvent:
                DPRINT1("PiWalkDeviceList: FIXME BlockedDriverEvent\n");
                ASSERT(FALSE); // IoDbgBreakPointEx();
                //Status = PiNotifyUserMode(EventEntry);
                break;

            case InvalidIDEvent:
                DPRINT1("PiWalkDeviceList: FIXME InvalidIDEvent\n");
                ASSERT(FALSE); // IoDbgBreakPointEx();
                //Status = PiNotifyUserMode(EventEntry);
                break;

            default:
                DPRINT1("PiWalkDeviceList: Unknown EventCategory %X\n", EventEntry->Data.EventCategory);
                ASSERT(FALSE); // IoDbgBreakPointEx();
                Status = STATUS_UNSUCCESSFUL;
                break;
        }

        if (Status != STATUS_PENDING)
        {
            PpCompleteDeviceEvent(EventEntry, Status);
        }
        else
        {
            DPRINT1("PiWalkDeviceList: Status %X\n", Status);
            ASSERT(FALSE);
        }

        IopProcessDeferredRegistrations();
    }

    KeAcquireGuardedMutex(&PiNotificationInProgressLock);
    KeSetEvent(&PiEventQueueEmpty, IO_NO_INCREMENT, FALSE);
    PiNotificationInProgress = FALSE;
    IopProcessDeferredRegistrations();
    KeReleaseGuardedMutex(&PiNotificationInProgressLock);

    KeReleaseGuardedMutex(&PpDeviceEventList->Lock);

    if (WorkItem)
        ExFreePoolWithTag(WorkItem, 'IWpP');

    KeReleaseMutex(&PpDeviceEventList->EventQueueMutex, FALSE);

    DPRINT("PiWalkDeviceList: exit\n");
}

NTSTATUS
NTAPI
PiInsertEventInQueue(
    _In_ PPNP_DEVICE_EVENT_ENTRY EventEntry)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PWORK_QUEUE_ITEM WorkItem = NULL;

    DPRINT("PiInsertEventInQueue: EventEntry %p, EventCategory %X\n", EventEntry, EventEntry->Data.EventCategory);
    PAGED_CODE();

    KeAcquireGuardedMutex(&PpDeviceEventList->Lock);
    KeAcquireGuardedMutex(&PiNotificationInProgressLock);

    if (PiNotificationInProgress)
    {
        DPRINT("PiInsertEventInQueue: PiNotificationInProgress TRUE\n");
    }
    else
    {
        WorkItem = ExAllocatePoolWithTag(NonPagedPool, sizeof(*WorkItem), 'IWpP');
        if (WorkItem)
        {
            PiNotificationInProgress = TRUE;
            KeClearEvent(&PiEventQueueEmpty);
        }
        else
        {
            DPRINT1("PiInsertEventInQueue: STATUS_INSUFFICIENT_RESOURCES\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    InsertTailList(&PpDeviceEventList->List, &EventEntry->ListEntry);

    KeReleaseGuardedMutex(&PiNotificationInProgressLock);
    KeReleaseGuardedMutex(&PpDeviceEventList->Lock);

    if (!WorkItem)
        return Status;

    WorkItem->WorkerRoutine = PiWalkDeviceList;
    WorkItem->Parameter = WorkItem;
    WorkItem->List.Flink = NULL;

    ExQueueWorkItem(WorkItem, DelayedWorkQueue);

    DPRINT("PiInsertEventInQueue: queue WorkItem %X\n", WorkItem);
    return Status;
}

NTSTATUS
NTAPI
PpSynchronizeDeviceEventQueue(VOID)
{
    PPNP_DEVICE_EVENT_ENTRY EventEntry;
    NTSTATUS Status;
    KEVENT Event;
    NTSTATUS RetStatus;

    PAGED_CODE();
    DPRINT("PpSynchronizeDeviceEventQueue()\n");

    EventEntry = ExAllocatePoolWithTag(PagedPool, sizeof(*EventEntry), 'EEpP');
    if (!EventEntry)
    {
        DPRINT1("PpSynchronizeDeviceEventQueue: return STATUS_NO_MEMORY\n");
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(EventEntry, sizeof(*EventEntry));

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    EventEntry->CallerEvent = &Event;

    RtlCopyMemory(&EventEntry->Data.EventGuid, &GUID_DEVICE_NOOP, sizeof(GUID));

    EventEntry->Data.EventCategory = 1;
    EventEntry->Data.Result = (PULONG)&RetStatus;
    EventEntry->Data.TotalSize = sizeof(EventEntry->Data);

    Status = PiInsertEventInQueue(EventEntry);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PpSynchronizeDeviceEventQueue: return STATUS_NO_MEMORY\n");
        return Status;
    }

    return KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
}

NTSTATUS
NTAPI
PpSetDeviceClassChange(
    _In_ CONST GUID* EventGuid,
    _In_ GUID* ClassGuid,
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    PPNP_DEVICE_EVENT_ENTRY EventEntry;
    ULONG EventEntrySize;
    ULONG DataTotalSize;
    ULONG Length;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("PpSetDeviceClassChange: SymbolicLink '%wZ'\n", SymbolicLinkName);

    if (PpPnpShuttingDown)
    {
        ASSERT(FALSE);
        return STATUS_TOO_LATE;
    }

    //_SEH2_TRY

    ASSERT(EventGuid != NULL);
    ASSERT(ClassGuid != NULL);
    ASSERT(SymbolicLinkName != NULL);

    Length = SymbolicLinkName->Length;
    DataTotalSize = Length + sizeof(PLUGPLAY_EVENT_BLOCK);
    EventEntrySize = Length + sizeof(PNP_DEVICE_EVENT_ENTRY);

    EventEntry = ExAllocatePoolWithTag(PagedPool, EventEntrySize, 'EEpP');
    if (!EventEntry)
    {
        DPRINT1("PpSetDeviceClassChange: return STATUS_NO_MEMORY\n");
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(EventEntry, EventEntrySize);

    EventEntry->Data.EventCategory = DeviceClassChangeEvent;
    EventEntry->Data.TotalSize = DataTotalSize;

    RtlCopyMemory(&EventEntry->Data.EventGuid, EventGuid, sizeof(GUID));
    RtlCopyMemory(&EventEntry->Data.DeviceClass.ClassGuid, ClassGuid, sizeof(GUID));
    RtlCopyMemory(EventEntry->Data.DeviceClass.SymbolicLinkName, SymbolicLinkName->Buffer, Length);

    EventEntry->Data.DeviceClass.SymbolicLinkName[Length / sizeof(WCHAR)] = UNICODE_NULL;

    Status = PiInsertEventInQueue(EventEntry);

    //_SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
PpSetTargetDeviceRemove(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN IsRemove,
    _In_ BOOLEAN IsNoRestart,
    _In_ BOOLEAN RemoveNoRestart,
    _In_ BOOLEAN IsEjectRequest,
    _In_ ULONG Problem,
    _In_ PKEVENT SyncEvent,
    _Out_ NTSTATUS* OutResult,
    _In_ PPNP_VETO_TYPE VetoType,
    _In_ PUNICODE_STRING VetoName)
{
    PPNP_DEVICE_EVENT_ENTRY EventEntry;
    PDEVICE_NODE DeviceNode;
    ULONG EventEntrySize;
    ULONG TotalSize;
    ULONG InstanceLength;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("PpSetTargetDeviceRemove: [%p] %X, %X, %X, %X, %X\n", DeviceObject, IsRemove, IsNoRestart, RemoveNoRestart, IsEjectRequest, Problem);

    ASSERT(DeviceObject != NULL);

    if (SyncEvent)
    {
        ASSERT(OutResult);
        *OutResult = STATUS_PENDING;
    }

    if (PpPnpShuttingDown)
    {
        DPRINT1("PpSetTargetDeviceRemove: return STATUS_TOO_LATE\n");
        return STATUS_TOO_LATE;
    }

    ObReferenceObject(DeviceObject);

    DeviceNode = IopGetDeviceNode(DeviceObject);
    ASSERT(DeviceNode);

    InstanceLength = DeviceNode->InstancePath.Length;
    TotalSize = (sizeof(PLUGPLAY_EVENT_BLOCK) + InstanceLength + sizeof(WCHAR));

    EventEntrySize = (TotalSize + (sizeof(PNP_DEVICE_EVENT_ENTRY) - sizeof(PLUGPLAY_EVENT_BLOCK)));

    EventEntry = ExAllocatePoolWithTag(PagedPool, EventEntrySize, 'EEpP');
    if (!EventEntry)
    {
        DPRINT1("PpSetTargetDeviceRemove: return STATUS_INSUFFICIENT_RESOURCES\n");
        ObDereferenceObject(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(EventEntry, EventEntrySize);

    EventEntry->CallerEvent = SyncEvent;
    EventEntry->VetoType = VetoType;
    EventEntry->Argument = Problem;
    EventEntry->VetoName = VetoName;

    if (IsEjectRequest)
        RtlCopyMemory(&EventEntry->Data.EventGuid, &GUID_DEVICE_EJECT, sizeof(GUID));
    else
        RtlCopyMemory(&EventEntry->Data.EventGuid, &GUID_DEVICE_QUERY_AND_REMOVE, sizeof(GUID));

    EventEntry->Data.EventCategory = TargetDeviceChangeEvent;
    EventEntry->Data.Result = (PVOID)OutResult;

    if (IsNoRestart)
        EventEntry->Data.Flags |= 2;

    if (IsRemove)
        EventEntry->Data.Flags |= 4;

    if (RemoveNoRestart)
    {
        ASSERT(IsNoRestart == FALSE);
        EventEntry->Data.Flags |= 8;
    }

    EventEntry->Data.TotalSize = TotalSize;
    EventEntry->Data.DeviceObject = DeviceObject;

    if (InstanceLength)
    {
        RtlCopyMemory(&EventEntry->Data.TargetDevice.DeviceIds,
                      DeviceNode->InstancePath.Buffer,
                      InstanceLength);
    }

    EventEntry->Data.TargetDevice.DeviceIds[InstanceLength / sizeof(WCHAR)] = UNICODE_NULL;

    Status = PiInsertEventInQueue(EventEntry);

    DPRINT("PpSetTargetDeviceRemove: return %X\n", Status);
    return Status;
}

/* EOF */
