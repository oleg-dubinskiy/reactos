/*
 * PROJECT:         ReactOS Kernel
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/pnpmgr/pnpnotify.c
 * PURPOSE:         Plug & Play notification functions
 * PROGRAMMERS:     Filip Navara (xnavara@volny.cz)
 *                  Hervé Poussineau (hpoussin@reactos.org)
 *                  Pierre Schweitzer
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "../pnpio.h"

//#define NDEBUG
#include <debug.h>

#define HashGuid(Guid) ((((PULONG)Guid)[0] + ((PULONG)Guid)[1]  + ((PULONG)Guid)[2]  + ((PULONG)Guid)[3]) % 13)

/* TYPES *******************************************************************/

typedef struct _PNP_NOTIFY_ENTRY
{
    LIST_ENTRY PnpNotifyList;
    IO_NOTIFICATION_EVENT_CATEGORY EventCategory;
    PVOID Context;
    UNICODE_STRING Guid;
    PFILE_OBJECT FileObject;
    PDRIVER_OBJECT DriverObject;
    PDRIVER_NOTIFICATION_CALLBACK_ROUTINE PnpNotificationProc;
} PNP_NOTIFY_ENTRY, *PPNP_NOTIFY_ENTRY;

/* GLOBALS *******************************************************************/

LIST_ENTRY PnpNotifyListHead;
KGUARDED_MUTEX PnpNotifyListLock;

extern LIST_ENTRY IopProfileNotifyList;
extern LIST_ENTRY IopDeviceClassNotifyList[13];
extern LIST_ENTRY IopDeferredRegistrationList;

extern KGUARDED_MUTEX IopHwProfileNotifyLock;
extern KGUARDED_MUTEX IopDeviceClassNotifyLock;
extern KGUARDED_MUTEX IopTargetDeviceNotifyLock;
extern KGUARDED_MUTEX IopDeferredRegistrationLock;
extern KGUARDED_MUTEX PiNotificationInProgressLock;

extern BOOLEAN PiNotificationInProgress;

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
IopReferenceNotify(
    _In_ PPNP_NOTIFY_HEADER NotifyHeader)
{
    DPRINT("IopReferenceNotify: NotifyHeader %p\n", NotifyHeader);
    PAGED_CODE();

    ASSERT(NotifyHeader);
    ASSERT(NotifyHeader->RefCount);

    NotifyHeader->RefCount++;
}

VOID
NTAPI
IopDereferenceNotify(
    _In_ PPNP_NOTIFY_HEADER NotifyHeader)
{
    DPRINT("IopDereferenceNotify: NotifyHeader %p\n", NotifyHeader);
    PAGED_CODE();

    ASSERT(NotifyHeader);
    ASSERT(NotifyHeader->RefCount > 0);

    NotifyHeader->RefCount--;

    if (NotifyHeader->RefCount)
    {
        DPRINT("IopDereferenceNotify: NotifyHeader->RefCount %X\n", NotifyHeader->RefCount);
        return;
    }

    ASSERT(NotifyHeader->Unregistered);
    RemoveEntryList(&NotifyHeader->Link);

    ObDereferenceObject(NotifyHeader->DriverObject);

    if (NotifyHeader->EventCategory == EventCategoryTargetDeviceChange)
    {
        PTARGET_DEVICE_NOTIFY Notify = (PTARGET_DEVICE_NOTIFY)NotifyHeader;

        if (Notify->PhysicalDeviceObject)
        {
            ObDereferenceObject(Notify->PhysicalDeviceObject);
            Notify->PhysicalDeviceObject = NULL;
        }
    }

    if (NotifyHeader->OpaqueSession)
    {
        MmQuitNextSession(NotifyHeader->OpaqueSession);
        NotifyHeader->OpaqueSession = NULL;
    }

    ExFreePoolWithTag(NotifyHeader, '  pP');
}

NTSTATUS
NTAPI
PiDeferNotification(
    _In_ PPNP_NOTIFY_HEADER NotifyHeader)
{
    PPNP_DEFER_NOTIFY DeferNotification;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("PiDeferNotification: NotifyHeader %p\n", NotifyHeader);
    PAGED_CODE();

    KeAcquireGuardedMutex(&PiNotificationInProgressLock);
    if (!PiNotificationInProgress)
    {
        DPRINT1("PiNotificationInProgress is FALSE\n");
        ASSERT(IsListEmpty(&IopDeferredRegistrationList));
        goto Exit;
    }

    DeferNotification = ExAllocatePoolWithTag(PagedPool, sizeof(*DeferNotification), '  pP');
    if (!DeferNotification)
    {
        DPRINT1("PiDeferNotification: STATUS_INSUFFICIENT_RESOURCES\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    DeferNotification->NotifyHeader = NotifyHeader;
    NotifyHeader->Unregistered = TRUE;

    IopReferenceNotify(NotifyHeader);

    KeAcquireGuardedMutex(&IopDeferredRegistrationLock);
    InsertTailList(&IopDeferredRegistrationList, &DeferNotification->Link);
    KeReleaseGuardedMutex(&IopDeferredRegistrationLock);

Exit:

    KeReleaseGuardedMutex(&PiNotificationInProgressLock);

    return Status;
}

NTSTATUS
NTAPI
PiNotifyDriverCallback(
    _In_ PDRIVER_NOTIFICATION_CALLBACK_ROUTINE CallbackRoutine,
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context,
    _In_ ULONG SessionId,
    _In_ PVOID OpaqueSession,
    _Out_ NTSTATUS* OutStatus)
{
    PEPROCESS Process;
    KAPC_STATE ApcState;
    ULONG CombinedApcDisable;
    KIRQL OldIrql;
    KIRQL NewIrql;
    NTSTATUS callbackStatus;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (!CallbackRoutine)
    {
        DPRINT1("PiNotifyDriverCallback: STATUS_INVALID_PARAMETER\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (!NotificationStructure)
    {
        DPRINT1("PiNotifyDriverCallback: STATUS_INVALID_PARAMETER\n");
        return STATUS_INVALID_PARAMETER;
    }

    OldIrql = KeGetCurrentIrql();
    CombinedApcDisable = KeGetCurrentThread()->CombinedApcDisable;

    Process = (PEPROCESS)KeGetCurrentThread()->ApcState.Process;

    if (!OpaqueSession ||
        (Process->ProcessInSession && SessionId == PsGetCurrentProcessSessionId()))
    {
        ASSERT(!MmIsSessionAddress(CallbackRoutine) || OpaqueSession);

        DPRINT("PiNotifyDriverCallback: calling CallbackRoutine %p\n", CallbackRoutine);

        callbackStatus = CallbackRoutine(NotificationStructure, Context);
        if (OutStatus)
            *OutStatus = callbackStatus;

        if (!NT_SUCCESS(callbackStatus))
        {
            DPRINT1("PiNotifyDriverCallback: callbackStatus %X\n", callbackStatus);
            ASSERT(FALSE); // IoDbgBreakPointEx();
        }

        goto Exit;
    }

    ASSERT(MmIsSessionAddress(CallbackRoutine));

    Status = MmAttachSession(OpaqueSession, &ApcState);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PiNotifyDriverCallback: Status %X\n", Status);
        ASSERT(NT_SUCCESS(Status));
    }

    DPRINT("PiNotifyDriverCallback: CallbackRoutine %p, SessionId %X\n", CallbackRoutine, SessionId);

    callbackStatus = CallbackRoutine(NotificationStructure, Context);
    if (OutStatus)
        *OutStatus = callbackStatus;

    if (!NT_SUCCESS(callbackStatus))
    {
        DPRINT1("PiNotifyDriverCallback: callbackStatus %X\n", callbackStatus);
        ASSERT(FALSE); // IoDbgBreakPointEx();
    }

    Status = 0;MmDetachSession(OpaqueSession, &ApcState); // TODO: add ret NTSTATUS for MmDetachSession().
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PiNotifyDriverCallback: Status %X\n", Status);
        ASSERT(NT_SUCCESS(Status));
    }

Exit:

    if (OldIrql != KeGetCurrentIrql())
    {
        NewIrql = KeGetCurrentIrql();
        DPRINT1("PiNotifyDriverCallback: CallbackRoutine %p, NewIrql %X, OldIrql %X\n", CallbackRoutine, NewIrql, OldIrql);
        ASSERT(FALSE); // IoDbgBreakPointEx();
    }

    if (CombinedApcDisable != KeGetCurrentThread()->CombinedApcDisable)
    {
        DPRINT1("PiNotifyDriverCallback: CallbackRoutine %p, Current %X, Old %X\n", CallbackRoutine, KeGetCurrentThread()->CombinedApcDisable, CombinedApcDisable);
        ASSERT(FALSE); // IoDbgBreakPointEx();
    }

    return Status;
}

VOID
IopNotifyPlugPlayNotification(
    IN PDEVICE_OBJECT DeviceObject,
    IN IO_NOTIFICATION_EVENT_CATEGORY EventCategory,
    IN LPCGUID Event,
    IN PVOID EventCategoryData1,
    IN PVOID EventCategoryData2)
{
    PPNP_NOTIFY_ENTRY ChangeEntry;
    PLIST_ENTRY ListEntry;
    PVOID NotificationStructure;
    BOOLEAN CallCurrentEntry;
    UNICODE_STRING GuidString;
    NTSTATUS Status;
    PDEVICE_OBJECT EntryDeviceObject = NULL;

    ASSERT(DeviceObject);

    KeAcquireGuardedMutex(&PnpNotifyListLock);
    if (IsListEmpty(&PnpNotifyListHead))
    {
        KeReleaseGuardedMutex(&PnpNotifyListLock);
        return;
    }

    switch (EventCategory)
    {
        case EventCategoryDeviceInterfaceChange:
        {
            PDEVICE_INTERFACE_CHANGE_NOTIFICATION NotificationInfos;
            NotificationStructure = NotificationInfos = ExAllocatePoolWithTag(
                PagedPool,
                sizeof(DEVICE_INTERFACE_CHANGE_NOTIFICATION),
                TAG_PNP_NOTIFY);
            if (!NotificationInfos)
            {
                KeReleaseGuardedMutex(&PnpNotifyListLock);
                return;
            }
            NotificationInfos->Version = 1;
            NotificationInfos->Size = sizeof(DEVICE_INTERFACE_CHANGE_NOTIFICATION);
            RtlCopyMemory(&NotificationInfos->Event, Event, sizeof(GUID));
            RtlCopyMemory(&NotificationInfos->InterfaceClassGuid, EventCategoryData1, sizeof(GUID));
            NotificationInfos->SymbolicLinkName = (PUNICODE_STRING)EventCategoryData2;
            Status = RtlStringFromGUID(&NotificationInfos->InterfaceClassGuid, &GuidString);
            if (!NT_SUCCESS(Status))
            {
                KeReleaseGuardedMutex(&PnpNotifyListLock);
                ExFreePoolWithTag(NotificationStructure, TAG_PNP_NOTIFY);
                return;
            }
            break;
        }
        case EventCategoryHardwareProfileChange:
        {
            PHWPROFILE_CHANGE_NOTIFICATION NotificationInfos;
            NotificationStructure = NotificationInfos = ExAllocatePoolWithTag(
                PagedPool,
                sizeof(HWPROFILE_CHANGE_NOTIFICATION),
                TAG_PNP_NOTIFY);
            if (!NotificationInfos)
            {
                KeReleaseGuardedMutex(&PnpNotifyListLock);
                return;
            }
            NotificationInfos->Version = 1;
            NotificationInfos->Size = sizeof(HWPROFILE_CHANGE_NOTIFICATION);
            RtlCopyMemory(&NotificationInfos->Event, Event, sizeof(GUID));
            break;
        }
        case EventCategoryTargetDeviceChange:
        {
            if (Event != &GUID_PNP_CUSTOM_NOTIFICATION)
            {
                PTARGET_DEVICE_REMOVAL_NOTIFICATION NotificationInfos;
                NotificationStructure = NotificationInfos = ExAllocatePoolWithTag(
                    PagedPool,
                    sizeof(TARGET_DEVICE_REMOVAL_NOTIFICATION),
                    TAG_PNP_NOTIFY);
                if (!NotificationInfos)
                {
                    KeReleaseGuardedMutex(&PnpNotifyListLock);
                    return;
                }
                NotificationInfos->Version = 1;
                NotificationInfos->Size = sizeof(TARGET_DEVICE_REMOVAL_NOTIFICATION);
                RtlCopyMemory(&NotificationInfos->Event, Event, sizeof(GUID));
            }
            else
            {
                PTARGET_DEVICE_CUSTOM_NOTIFICATION NotificationInfos;
                NotificationStructure = NotificationInfos = ExAllocatePoolWithTag(
                    PagedPool,
                    sizeof(TARGET_DEVICE_CUSTOM_NOTIFICATION),
                    TAG_PNP_NOTIFY);
                if (!NotificationInfos)
                {
                    KeReleaseGuardedMutex(&PnpNotifyListLock);
                    return;
                }
                RtlCopyMemory(NotificationInfos, EventCategoryData1, sizeof(TARGET_DEVICE_CUSTOM_NOTIFICATION));
            }
            break;
        }
        default:
        {
            DPRINT1("IopNotifyPlugPlayNotification(): unknown EventCategory 0x%x UNIMPLEMENTED\n", EventCategory);
            KeReleaseGuardedMutex(&PnpNotifyListLock);
            return;
        }
    }

    /* Loop through procedures registred in PnpNotifyListHead
     * list to find those that meet some criteria.
     */
    ListEntry = PnpNotifyListHead.Flink;
    while (ListEntry != &PnpNotifyListHead)
    {
        ChangeEntry = CONTAINING_RECORD(ListEntry, PNP_NOTIFY_ENTRY, PnpNotifyList);
        CallCurrentEntry = FALSE;

        if (ChangeEntry->EventCategory != EventCategory)
        {
            ListEntry = ListEntry->Flink;
            continue;
        }

        switch (EventCategory)
        {
            case EventCategoryDeviceInterfaceChange:
            {
                if (RtlCompareUnicodeString(&ChangeEntry->Guid, &GuidString, FALSE) == 0)
                {
                    CallCurrentEntry = TRUE;
                }
                break;
            }
            case EventCategoryHardwareProfileChange:
            {
                CallCurrentEntry = TRUE;
                break;
            }
            case EventCategoryTargetDeviceChange:
            {
                Status = IoGetRelatedTargetDevice(ChangeEntry->FileObject, &EntryDeviceObject);
                if (NT_SUCCESS(Status))
                {
                    if (DeviceObject == EntryDeviceObject)
                    {
                        if (Event == &GUID_PNP_CUSTOM_NOTIFICATION)
                        {
                            ((PTARGET_DEVICE_CUSTOM_NOTIFICATION)NotificationStructure)->FileObject = ChangeEntry->FileObject;
                        }
                        else
                        {
                            ((PTARGET_DEVICE_REMOVAL_NOTIFICATION)NotificationStructure)->FileObject = ChangeEntry->FileObject;
                        }
                        CallCurrentEntry = TRUE;
                    }
                }
                break;
            }
            default:
            {
                DPRINT1("IopNotifyPlugPlayNotification(): unknown EventCategory 0x%x UNIMPLEMENTED\n", EventCategory);
                break;
            }
        }

        /* Move to the next element now, as callback may unregister itself */
        ListEntry = ListEntry->Flink;
        /* FIXME: If ListEntry was the last element and that callback registers
         * new notifications, those won't be checked... */

        if (CallCurrentEntry)
        {
            /* Call entry into new allocated memory */
            DPRINT("IopNotifyPlugPlayNotification(): found suitable callback %p\n",
                ChangeEntry);

            KeReleaseGuardedMutex(&PnpNotifyListLock);
            (ChangeEntry->PnpNotificationProc)(NotificationStructure,
                                               ChangeEntry->Context);
            KeAcquireGuardedMutex(&PnpNotifyListLock);
        }

    }
    KeReleaseGuardedMutex(&PnpNotifyListLock);
    ExFreePoolWithTag(NotificationStructure, TAG_PNP_NOTIFY);
    if (EventCategory == EventCategoryDeviceInterfaceChange)
        RtlFreeUnicodeString(&GuidString);
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @unimplemented
 */
ULONG
NTAPI
IoPnPDeliverServicePowerNotification(ULONG VetoedPowerOperation OPTIONAL,
                                     ULONG PowerNotification,
                                     ULONG Unknown OPTIONAL,
                                     BOOLEAN Synchronous)
{
    UNIMPLEMENTED;
    return 0;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
IoRegisterPlugPlayNotification(IN IO_NOTIFICATION_EVENT_CATEGORY EventCategory,
                               IN ULONG EventCategoryFlags,
                               IN PVOID EventCategoryData OPTIONAL,
                               IN PDRIVER_OBJECT DriverObject,
                               IN PDRIVER_NOTIFICATION_CALLBACK_ROUTINE CallbackRoutine,
                               IN PVOID Context,
                               OUT PVOID *NotificationEntry)
{
    PPNP_NOTIFY_ENTRY Entry;
    PWSTR SymbolicLinkList;
    NTSTATUS Status;
    PAGED_CODE();

    DPRINT("%s(EventCategory 0x%x, EventCategoryFlags 0x%lx, DriverObject %p) called.\n",
           __FUNCTION__,
           EventCategory,
           EventCategoryFlags,
           DriverObject);

    ObReferenceObject(DriverObject);

    /* Try to allocate entry for notification before sending any notification */
    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(PNP_NOTIFY_ENTRY),
                                  TAG_PNP_NOTIFY);

    if (!Entry)
    {
        DPRINT("ExAllocatePool() failed\n");
        ObDereferenceObject(DriverObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (EventCategory == EventCategoryDeviceInterfaceChange &&
        EventCategoryFlags & PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES)
    {
        DEVICE_INTERFACE_CHANGE_NOTIFICATION NotificationInfos;
        UNICODE_STRING SymbolicLinkU;
        PWSTR SymbolicLink;

        Status = IoGetDeviceInterfaces((LPGUID)EventCategoryData,
                                       NULL, /* PhysicalDeviceObject OPTIONAL */
                                       0, /* Flags */
                                       &SymbolicLinkList);
        if (NT_SUCCESS(Status))
        {
            /* Enumerate SymbolicLinkList */
            NotificationInfos.Version = 1;
            NotificationInfos.Size = sizeof(DEVICE_INTERFACE_CHANGE_NOTIFICATION);
            RtlCopyMemory(&NotificationInfos.Event,
                          &GUID_DEVICE_INTERFACE_ARRIVAL,
                          sizeof(GUID));
            RtlCopyMemory(&NotificationInfos.InterfaceClassGuid,
                          EventCategoryData,
                          sizeof(GUID));
            NotificationInfos.SymbolicLinkName = &SymbolicLinkU;

            for (SymbolicLink = SymbolicLinkList;
                 *SymbolicLink;
                 SymbolicLink += wcslen(SymbolicLink) + 1)
            {
                RtlInitUnicodeString(&SymbolicLinkU, SymbolicLink);
                DPRINT("Calling callback routine for %S\n", SymbolicLink);
                (*CallbackRoutine)(&NotificationInfos, Context);
            }

            ExFreePool(SymbolicLinkList);
        }
    }

    Entry->PnpNotificationProc = CallbackRoutine;
    Entry->EventCategory = EventCategory;
    Entry->Context = Context;
    Entry->DriverObject = DriverObject;
    switch (EventCategory)
    {
        case EventCategoryDeviceInterfaceChange:
        {
            Status = RtlStringFromGUID(EventCategoryData, &Entry->Guid);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);
                ObDereferenceObject(DriverObject);
                return Status;
            }
            break;
        }
        case EventCategoryHardwareProfileChange:
        {
            /* nothing to do */
           break;
        }
        case EventCategoryTargetDeviceChange:
        {
            Entry->FileObject = (PFILE_OBJECT)EventCategoryData;
            break;
        }
        default:
        {
            DPRINT1("%s: unknown EventCategory 0x%x UNIMPLEMENTED\n",
                    __FUNCTION__, EventCategory);
            break;
        }
    }

    KeAcquireGuardedMutex(&PnpNotifyListLock);
    InsertHeadList(&PnpNotifyListHead,
                   &Entry->PnpNotifyList);
    KeReleaseGuardedMutex(&PnpNotifyListLock);

    DPRINT("%s returns NotificationEntry %p\n", __FUNCTION__, Entry);

    *NotificationEntry = Entry;

    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
IoUnregisterPlugPlayNotification(IN PVOID NotificationEntry)
{
    PPNP_NOTIFY_ENTRY Entry;
    PAGED_CODE();

    Entry = (PPNP_NOTIFY_ENTRY)NotificationEntry;
    DPRINT("%s(NotificationEntry %p) called\n", __FUNCTION__, Entry);

    KeAcquireGuardedMutex(&PnpNotifyListLock);
    RemoveEntryList(&Entry->PnpNotifyList);
    KeReleaseGuardedMutex(&PnpNotifyListLock);

    RtlFreeUnicodeString(&Entry->Guid);

    ObDereferenceObject(Entry->DriverObject);

    ExFreePoolWithTag(Entry, TAG_PNP_NOTIFY);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PiNotifyDriverCallback(
    _In_ PDRIVER_NOTIFICATION_CALLBACK_ROUTINE CallbackRoutine,
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context,
    _In_ ULONG SessionId,
    _In_ PVOID OpaqueSession,
    _Out_ NTSTATUS* OutStatus)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // IoDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
IopNotifyDeviceClassChange(
    _In_ PPLUGPLAY_EVENT_BLOCK EventBlock,
    _In_ GUID* ClassGuid,
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    DEVICE_INTERFACE_CHANGE_NOTIFICATION InterfaceNotification;
    PDEVICE_INTERFACE_NOTIFY Notify;
    PLIST_ENTRY Head;
    PLIST_ENTRY Entry;
    NTSTATUS CallbackStatus;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("IopNotifyDeviceClassChange: EventBlock %p, SymbolicLink '%wZ'\n", EventBlock, SymbolicLinkName);

    InterfaceNotification.Version = 1;
    InterfaceNotification.Size = sizeof(InterfaceNotification);
    InterfaceNotification.Event = EventBlock->EventGuid;
    InterfaceNotification.InterfaceClassGuid = *ClassGuid;
    InterfaceNotification.SymbolicLinkName = SymbolicLinkName;

    KeAcquireGuardedMutex(&IopDeviceClassNotifyLock);

    Head = &IopDeviceClassNotifyList[HashGuid(ClassGuid)];

    for (Entry = Head->Flink; (Entry != Head); Entry = Entry->Flink)
    {
        Notify = CONTAINING_RECORD(Entry, DEVICE_INTERFACE_NOTIFY, Header.Link);

        if (Notify->Header.Unregistered)
            continue;

        if (!PiCompareGuid(&(Notify->Interface), ClassGuid))
            continue;

        IopReferenceNotify(&Notify->Header);
        KeReleaseGuardedMutex(&IopDeviceClassNotifyLock);

        Status = PiNotifyDriverCallback(Notify->Header.PnpNotificationRoutine,
                                        &InterfaceNotification,
                                        Notify->Header.Context,
                                        Notify->Header.SessionId,
                                        Notify->Header.OpaqueSession,
                                        &CallbackStatus);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IopNotifyDeviceClassChange: Status %X\n", Status);
            ASSERT(NT_SUCCESS(Status));
        }

        KeAcquireGuardedMutex(&IopDeviceClassNotifyLock);
        IopDereferenceNotify(&Notify->Header);
    }

    KeReleaseGuardedMutex(&IopDeviceClassNotifyLock);
}

VOID
NTAPI
IopProcessDeferredRegistrations(VOID)
{
    PPNP_DEFER_NOTIFY DeferNotification;
    PKGUARDED_MUTEX NotifyLock;
    PLIST_ENTRY Entry;

    PAGED_CODE();
    DPRINT("IopProcessDeferredRegistrations()\n");

    KeAcquireGuardedMutex(&IopDeferredRegistrationLock);

    while (!IsListEmpty(&IopDeferredRegistrationList))
    {
        Entry = RemoveHeadList(&IopDeferredRegistrationList);
        DeferNotification = CONTAINING_RECORD(Entry, PNP_DEFER_NOTIFY, Link);
        DPRINT("IopProcessDeferredRegistrations: DeferNotification %X\n", DeferNotification);

        NotifyLock = DeferNotification->NotifyHeader->NotifyLock;
        if (NotifyLock)
            KeAcquireGuardedMutex(NotifyLock);

        DeferNotification->NotifyHeader->Unregistered = FALSE;
        IopDereferenceNotify(DeferNotification->NotifyHeader);
        ExFreePoolWithTag(DeferNotification, 0);

        if (NotifyLock)
            KeReleaseGuardedMutex(NotifyLock);
    }

    KeReleaseGuardedMutex(&IopDeferredRegistrationLock);
}

/* EOF */
