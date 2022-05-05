/*
 * PROJECT:         ReactOS Kernel
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/pnpmgr/pnpmgr.c
 * PURPOSE:         Initializes the PnP manager
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 *                  Copyright 2007 Hervé Poussineau (hpoussin@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "../pnpio.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

#define MAX_DEVICE_ID_LEN          200
#define MAX_SEPARATORS_INSTANCEID  0
#define MAX_SEPARATORS_DEVICEID    1

PDEVICE_NODE IopRootDeviceNode;
KSPIN_LOCK IopDeviceTreeLock;
ERESOURCE PpRegistryDeviceResource;
KGUARDED_MUTEX PpDeviceReferenceTableLock;
RTL_AVL_TABLE PpDeviceReferenceTable;

extern ERESOURCE IopDriverLoadResource;
extern ULONG ExpInitializationPhase;
extern BOOLEAN PnpSystemInit;
extern BOOLEAN PpDisableFirmwareMapper;

/* DATA **********************************************************************/

PDRIVER_OBJECT IopRootDriverObject;
PIO_BUS_TYPE_GUID_LIST PnpBusTypeGuidList = NULL;
LIST_ENTRY IopDeviceActionRequestList;
WORK_QUEUE_ITEM IopDeviceActionWorkItem;
BOOLEAN IopDeviceActionInProgress;
KSPIN_LOCK IopDeviceActionLock;

typedef struct _DEVICE_REGISTRATION_CONTEXT {
    PUNICODE_STRING InstancePath;
    PVOID Function;
    PBOOLEAN EnableInstance;
} DEVICE_REGISTRATION_CONTEXT, *PDEVICE_REGISTRATION_CONTEXT;

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
IopCreateDeviceKeyPath(IN PCUNICODE_STRING RegistryPath,
                       IN ULONG CreateOptions,
                       OUT PHANDLE Handle);

VOID
IopCancelPrepareDeviceForRemoval(PDEVICE_OBJECT DeviceObject);

NTSTATUS
IopPrepareDeviceForRemoval(PDEVICE_OBJECT DeviceObject, BOOLEAN Force);

PDEVICE_OBJECT
IopGetDeviceObjectFromDeviceInstance(PUNICODE_STRING DeviceInstance);

VOID
IopFixupDeviceId(PWCHAR String)
{
    SIZE_T Length = wcslen(String), i;

    for (i = 0; i < Length; i++)
    {
        if (String[i] == L'\\')
            String[i] = L'#';
    }
}

VOID
NTAPI
IopInstallCriticalDevice(PDEVICE_NODE DeviceNode)
{
    NTSTATUS Status;
    HANDLE CriticalDeviceKey, InstanceKey;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING CriticalDeviceKeyU = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\CriticalDeviceDatabase");
    UNICODE_STRING CompatibleIdU = RTL_CONSTANT_STRING(L"CompatibleIDs");
    UNICODE_STRING HardwareIdU = RTL_CONSTANT_STRING(L"HardwareID");
    UNICODE_STRING ServiceU = RTL_CONSTANT_STRING(L"Service");
    UNICODE_STRING ClassGuidU = RTL_CONSTANT_STRING(L"ClassGUID");
    PKEY_VALUE_PARTIAL_INFORMATION PartialInfo;
    ULONG HidLength = 0, CidLength = 0, BufferLength;
    PWCHAR IdBuffer, OriginalIdBuffer;

    /* Open the device instance key */
    Status = IopCreateDeviceKeyPath(&DeviceNode->InstancePath, REG_OPTION_NON_VOLATILE, &InstanceKey);
    if (Status != STATUS_SUCCESS)
        return;

    Status = ZwQueryValueKey(InstanceKey,
                             &HardwareIdU,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &HidLength);
    if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
    {
        ZwClose(InstanceKey);
        return;
    }

    Status = ZwQueryValueKey(InstanceKey,
                             &CompatibleIdU,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &CidLength);
    if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
    {
        CidLength = 0;
    }

    BufferLength = HidLength + CidLength;
    BufferLength -= (((CidLength != 0) ? 2 : 1) * FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data));

    /* Allocate a buffer to hold data from both */
    OriginalIdBuffer = IdBuffer = ExAllocatePool(PagedPool, BufferLength);
    if (!IdBuffer)
    {
        ZwClose(InstanceKey);
        return;
    }

    /* Compute the buffer size */
    if (HidLength > CidLength)
        BufferLength = HidLength;
    else
        BufferLength = CidLength;

    PartialInfo = ExAllocatePool(PagedPool, BufferLength);
    if (!PartialInfo)
    {
        ZwClose(InstanceKey);
        ExFreePool(OriginalIdBuffer);
        return;
    }

    Status = ZwQueryValueKey(InstanceKey,
                             &HardwareIdU,
                             KeyValuePartialInformation,
                             PartialInfo,
                             HidLength,
                             &HidLength);
    if (Status != STATUS_SUCCESS)
    {
        ExFreePool(PartialInfo);
        ExFreePool(OriginalIdBuffer);
        ZwClose(InstanceKey);
        return;
    }

    /* Copy in HID info first (without 2nd terminating NULL if CID is present) */
    HidLength = PartialInfo->DataLength - ((CidLength != 0) ? sizeof(WCHAR) : 0);
    RtlCopyMemory(IdBuffer, PartialInfo->Data, HidLength);

    if (CidLength != 0)
    {
        Status = ZwQueryValueKey(InstanceKey,
                                 &CompatibleIdU,
                                 KeyValuePartialInformation,
                                 PartialInfo,
                                 CidLength,
                                 &CidLength);
        if (Status != STATUS_SUCCESS)
        {
            ExFreePool(PartialInfo);
            ExFreePool(OriginalIdBuffer);
            ZwClose(InstanceKey);
            return;
        }

        /* Copy CID next */
        CidLength = PartialInfo->DataLength;
        RtlCopyMemory(((PUCHAR)IdBuffer) + HidLength, PartialInfo->Data, CidLength);
    }

    /* Free our temp buffer */
    ExFreePool(PartialInfo);

    InitializeObjectAttributes(&ObjectAttributes,
                               &CriticalDeviceKeyU,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = ZwOpenKey(&CriticalDeviceKey,
                       KEY_ENUMERATE_SUB_KEYS,
                       &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        /* The critical device database doesn't exist because
         * we're probably in 1st stage setup, but it's ok */
        ExFreePool(OriginalIdBuffer);
        ZwClose(InstanceKey);
        return;
    }

    while (*IdBuffer)
    {
        USHORT StringLength = (USHORT)wcslen(IdBuffer) + 1, Index;

        IopFixupDeviceId(IdBuffer);

        /* Look through all subkeys for a match */
        for (Index = 0; TRUE; Index++)
        {
            ULONG NeededLength;
            PKEY_BASIC_INFORMATION BasicInfo;

            Status = ZwEnumerateKey(CriticalDeviceKey,
                                    Index,
                                    KeyBasicInformation,
                                    NULL,
                                    0,
                                    &NeededLength);
            if (Status == STATUS_NO_MORE_ENTRIES)
                break;
            else if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
            {
                UNICODE_STRING ChildIdNameU, RegKeyNameU;

                BasicInfo = ExAllocatePool(PagedPool, NeededLength);
                if (!BasicInfo)
                {
                    /* No memory */
                    ExFreePool(OriginalIdBuffer);
                    ZwClose(CriticalDeviceKey);
                    ZwClose(InstanceKey);
                    return;
                }

                Status = ZwEnumerateKey(CriticalDeviceKey,
                                        Index,
                                        KeyBasicInformation,
                                        BasicInfo,
                                        NeededLength,
                                        &NeededLength);
                if (Status != STATUS_SUCCESS)
                {
                    /* This shouldn't happen */
                    ExFreePool(BasicInfo);
                    continue;
                }

                ChildIdNameU.Buffer = IdBuffer;
                ChildIdNameU.MaximumLength = ChildIdNameU.Length = (StringLength - 1) * sizeof(WCHAR);
                RegKeyNameU.Buffer = BasicInfo->Name;
                RegKeyNameU.MaximumLength = RegKeyNameU.Length = (USHORT)BasicInfo->NameLength;

                if (RtlEqualUnicodeString(&ChildIdNameU, &RegKeyNameU, TRUE))
                {
                    HANDLE ChildKeyHandle;

                    InitializeObjectAttributes(&ObjectAttributes,
                                               &ChildIdNameU,
                                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                               CriticalDeviceKey,
                                               NULL);

                    Status = ZwOpenKey(&ChildKeyHandle,
                                       KEY_QUERY_VALUE,
                                       &ObjectAttributes);
                    if (Status != STATUS_SUCCESS)
                    {
                        ExFreePool(BasicInfo);
                        continue;
                    }

                    /* Check if there's already a driver installed */
                    Status = ZwQueryValueKey(InstanceKey,
                                             &ClassGuidU,
                                             KeyValuePartialInformation,
                                             NULL,
                                             0,
                                             &NeededLength);
                    if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
                    {
                        ExFreePool(BasicInfo);
                        continue;
                    }

                    Status = ZwQueryValueKey(ChildKeyHandle,
                                             &ClassGuidU,
                                             KeyValuePartialInformation,
                                             NULL,
                                             0,
                                             &NeededLength);
                    if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
                    {
                        ExFreePool(BasicInfo);
                        continue;
                    }

                    PartialInfo = ExAllocatePool(PagedPool, NeededLength);
                    if (!PartialInfo)
                    {
                        ExFreePool(OriginalIdBuffer);
                        ExFreePool(BasicInfo);
                        ZwClose(InstanceKey);
                        ZwClose(ChildKeyHandle);
                        ZwClose(CriticalDeviceKey);
                        return;
                    }

                    /* Read ClassGUID entry in the CDDB */
                    Status = ZwQueryValueKey(ChildKeyHandle,
                                             &ClassGuidU,
                                             KeyValuePartialInformation,
                                             PartialInfo,
                                             NeededLength,
                                             &NeededLength);
                    if (Status != STATUS_SUCCESS)
                    {
                        ExFreePool(BasicInfo);
                        continue;
                    }

                    /* Write it to the ENUM key */
                    Status = ZwSetValueKey(InstanceKey,
                                           &ClassGuidU,
                                           0,
                                           REG_SZ,
                                           PartialInfo->Data,
                                           PartialInfo->DataLength);
                    if (Status != STATUS_SUCCESS)
                    {
                        ExFreePool(BasicInfo);
                        ExFreePool(PartialInfo);
                        ZwClose(ChildKeyHandle);
                        continue;
                    }

                    Status = ZwQueryValueKey(ChildKeyHandle,
                                             &ServiceU,
                                             KeyValuePartialInformation,
                                             NULL,
                                             0,
                                             &NeededLength);
                    if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
                    {
                        ExFreePool(PartialInfo);
                        PartialInfo = ExAllocatePool(PagedPool, NeededLength);
                        if (!PartialInfo)
                        {
                            ExFreePool(OriginalIdBuffer);
                            ExFreePool(BasicInfo);
                            ZwClose(InstanceKey);
                            ZwClose(ChildKeyHandle);
                            ZwClose(CriticalDeviceKey);
                            return;
                        }

                        /* Read the service entry from the CDDB */
                        Status = ZwQueryValueKey(ChildKeyHandle,
                                                 &ServiceU,
                                                 KeyValuePartialInformation,
                                                 PartialInfo,
                                                 NeededLength,
                                                 &NeededLength);
                        if (Status != STATUS_SUCCESS)
                        {
                            ExFreePool(BasicInfo);
                            ExFreePool(PartialInfo);
                            ZwClose(ChildKeyHandle);
                            continue;
                        }

                        /* Write it to the ENUM key */
                        Status = ZwSetValueKey(InstanceKey,
                                               &ServiceU,
                                               0,
                                               REG_SZ,
                                               PartialInfo->Data,
                                               PartialInfo->DataLength);
                        if (Status != STATUS_SUCCESS)
                        {
                            ExFreePool(BasicInfo);
                            ExFreePool(PartialInfo);
                            ZwClose(ChildKeyHandle);
                            continue;
                        }

                        DPRINT("Installed service '%S' for critical device '%wZ'\n", PartialInfo->Data, &ChildIdNameU);
                    }
                    else
                    {
                        DPRINT1("Installed NULL service for critical device '%wZ'\n", &ChildIdNameU);
                    }

                    ExFreePool(OriginalIdBuffer);
                    ExFreePool(PartialInfo);
                    ExFreePool(BasicInfo);
                    ZwClose(InstanceKey);
                    ZwClose(ChildKeyHandle);
                    ZwClose(CriticalDeviceKey);

                    /* That's it */
                    return;
                }

                ExFreePool(BasicInfo);
            }
            else
            {
                /* Umm, not sure what happened here */
                continue;
            }
        }

        /* Advance to the next ID */
        IdBuffer += StringLength;
    }

    ExFreePool(OriginalIdBuffer);
    ZwClose(InstanceKey);
    ZwClose(CriticalDeviceKey);
}

static
NTSTATUS
NTAPI
IopQueryRemoveDevice(IN PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_NODE DeviceNode = IopGetDeviceNode(DeviceObject);
    IO_STACK_LOCATION Stack;
    PVOID Dummy;
    NTSTATUS Status;

    ASSERT(DeviceNode);

    IopQueueTargetDeviceEvent(&GUID_DEVICE_REMOVE_PENDING,
                              &DeviceNode->InstancePath);

    RtlZeroMemory(&Stack, sizeof(IO_STACK_LOCATION));
    Stack.MajorFunction = IRP_MJ_PNP;
    Stack.MinorFunction = IRP_MN_QUERY_REMOVE_DEVICE;

    Status = IopSynchronousCall(DeviceObject, &Stack, &Dummy);

    IopNotifyPlugPlayNotification(DeviceObject,
                                  EventCategoryTargetDeviceChange,
                                  &GUID_TARGET_DEVICE_QUERY_REMOVE,
                                  NULL,
                                  NULL);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Removal vetoed by %wZ\n", &DeviceNode->InstancePath);
        IopQueueTargetDeviceEvent(&GUID_DEVICE_REMOVAL_VETOED,
                                  &DeviceNode->InstancePath);
    }

    return Status;
}

static
VOID
NTAPI
IopSendRemoveDevice(IN PDEVICE_OBJECT DeviceObject)
{
ASSERT(FALSE);
#if 0
    IO_STACK_LOCATION Stack;
    PVOID Dummy;
    PDEVICE_NODE DeviceNode = IopGetDeviceNode(DeviceObject);

    /* Drop all our state for this device in case it isn't really going away */
    DeviceNode->Flags &= DNF_ENUMERATED | DNF_PROCESSED;

    RtlZeroMemory(&Stack, sizeof(IO_STACK_LOCATION));
    Stack.MajorFunction = IRP_MJ_PNP;
    Stack.MinorFunction = IRP_MN_REMOVE_DEVICE;

    /* Drivers should never fail a IRP_MN_REMOVE_DEVICE request */
    IopSynchronousCall(DeviceObject, &Stack, &Dummy);

    IopNotifyPlugPlayNotification(DeviceObject,
                                  EventCategoryTargetDeviceChange,
                                  &GUID_TARGET_DEVICE_REMOVE_COMPLETE,
                                  NULL,
                                  NULL);
    ObDereferenceObject(DeviceObject);
#endif
}

static
VOID
NTAPI
IopCancelRemoveDevice(IN PDEVICE_OBJECT DeviceObject)
{
    IO_STACK_LOCATION Stack;
    PVOID Dummy;

    RtlZeroMemory(&Stack, sizeof(IO_STACK_LOCATION));
    Stack.MajorFunction = IRP_MJ_PNP;
    Stack.MinorFunction = IRP_MN_CANCEL_REMOVE_DEVICE;

    /* Drivers should never fail a IRP_MN_CANCEL_REMOVE_DEVICE request */
    IopSynchronousCall(DeviceObject, &Stack, &Dummy);

    IopNotifyPlugPlayNotification(DeviceObject,
                                  EventCategoryTargetDeviceChange,
                                  &GUID_TARGET_DEVICE_REMOVE_CANCELLED,
                                  NULL,
                                  NULL);
}

static
VOID
NTAPI
IopDeviceActionWorker(
    _In_ PVOID Context)
{
    PLIST_ENTRY ListEntry;
    PDEVICE_ACTION_DATA Data;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceActionLock, &OldIrql);
    while (!IsListEmpty(&IopDeviceActionRequestList))
    {
        ListEntry = RemoveHeadList(&IopDeviceActionRequestList);
        KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);
        Data = CONTAINING_RECORD(ListEntry,
                                 DEVICE_ACTION_DATA,
                                 RequestListEntry);

        switch (Data->Action)
        {
            case DeviceActionInvalidateDeviceRelations:
                IoSynchronousInvalidateDeviceRelations(Data->DeviceObject,
                                                       Data->InvalidateDeviceRelations.Type);
                break;

            default:
                DPRINT1("Unimplemented device action %u\n", Data->Action);
                break;
        }

        ObDereferenceObject(Data->DeviceObject);
        ExFreePoolWithTag(Data, TAG_IO);
        KeAcquireSpinLock(&IopDeviceActionLock, &OldIrql);
    }
    IopDeviceActionInProgress = FALSE;
    KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);
}

VOID
IopQueueDeviceAction(
    _In_ PDEVICE_ACTION_DATA ActionData)
{
    PDEVICE_ACTION_DATA Data;
    KIRQL OldIrql;

    DPRINT("IopQueueDeviceAction(%p)\n", ActionData);

    Data = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(DEVICE_ACTION_DATA),
                                 TAG_IO);
    if (!Data)
        return;

    ObReferenceObject(ActionData->DeviceObject);
    RtlCopyMemory(Data, ActionData, sizeof(DEVICE_ACTION_DATA));

    DPRINT("Action %u\n", Data->Action);

    KeAcquireSpinLock(&IopDeviceActionLock, &OldIrql);
    InsertTailList(&IopDeviceActionRequestList, &Data->RequestListEntry);
    if (IopDeviceActionInProgress)
    {
        KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);
        return;
    }
    IopDeviceActionInProgress = TRUE;
    KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);

    ExInitializeWorkItem(&IopDeviceActionWorkItem,
                         IopDeviceActionWorker,
                         NULL);
    ExQueueWorkItem(&IopDeviceActionWorkItem,
                    DelayedWorkQueue);
}

NTSTATUS
IopGetSystemPowerDeviceObject(PDEVICE_OBJECT *DeviceObject)
{
    KIRQL OldIrql;

    if (PopSystemPowerDeviceNode)
    {
        KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
        *DeviceObject = PopSystemPowerDeviceNode->PhysicalDeviceObject;
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

USHORT
NTAPI
IopGetBusTypeGuidIndex(_In_ LPGUID BusTypeGuid)
{
    USHORT i = 0, FoundIndex = 0xFFFF;
    ULONG NewSize;
    PVOID NewList;

    /* Acquire the lock */
    ExAcquireFastMutex(&PnpBusTypeGuidList->Lock);

    /* Loop all entries */
    while (i < PnpBusTypeGuidList->GuidCount)
    {
         /* Try to find a match */
         if (RtlCompareMemory(BusTypeGuid,
                              &PnpBusTypeGuidList->Guids[i],
                              sizeof(GUID)) == sizeof(GUID))
         {
              /* Found it */
              FoundIndex = i;
              goto Quickie;
         }
         i++;
    }

    /* Check if we have to grow the list */
    if (PnpBusTypeGuidList->GuidCount)
    {
        /* Calculate the new size */
        NewSize = sizeof(IO_BUS_TYPE_GUID_LIST) +
                 (sizeof(GUID) * PnpBusTypeGuidList->GuidCount);

        /* Allocate the new copy */
        NewList = ExAllocatePool(PagedPool, NewSize);

        if (!NewList)
        {
            /* Fail */
            ExFreePool(PnpBusTypeGuidList);
            goto Quickie;
        }

        /* Now copy them, decrease the size too */
        NewSize -= sizeof(GUID);
        RtlCopyMemory(NewList, PnpBusTypeGuidList, NewSize);

        /* Free the old list */
        ExFreePool(PnpBusTypeGuidList);

        /* Use the new buffer */
        PnpBusTypeGuidList = NewList;
    }

    /* Copy the new GUID */
    RtlCopyMemory(&PnpBusTypeGuidList->Guids[PnpBusTypeGuidList->GuidCount],
                  BusTypeGuid,
                  sizeof(GUID));

    /* The new entry is the index */
    FoundIndex = (USHORT)PnpBusTypeGuidList->GuidCount;
    PnpBusTypeGuidList->GuidCount++;

Quickie:
    ExReleaseFastMutex(&PnpBusTypeGuidList->Lock);
    return FoundIndex;
}

NTSTATUS
NTAPI
IopInitiatePnpIrp(IN PDEVICE_OBJECT DeviceObject,
                  IN OUT PIO_STATUS_BLOCK IoStatusBlock,
                  IN UCHAR MinorFunction,
                  IN PIO_STACK_LOCATION Stack OPTIONAL)
{
    IO_STACK_LOCATION IoStackLocation;

    /* Fill out the stack information */
    RtlZeroMemory(&IoStackLocation, sizeof(IO_STACK_LOCATION));
    IoStackLocation.MajorFunction = IRP_MJ_PNP;
    IoStackLocation.MinorFunction = MinorFunction;
    if (Stack)
    {
        /* Copy the rest */
        RtlCopyMemory(&IoStackLocation.Parameters,
                      &Stack->Parameters,
                      sizeof(Stack->Parameters));
    }

    /* Do the PnP call */
    IoStatusBlock->Status = IopSynchronousCall(DeviceObject,
                                               &IoStackLocation,
                                               (PVOID)&IoStatusBlock->Information);
    return IoStatusBlock->Status;
}

NTSTATUS
IopTraverseDeviceTreeNode(PDEVICETREE_TRAVERSE_CONTEXT Context)
{
    PDEVICE_NODE ParentDeviceNode;
    PDEVICE_NODE ChildDeviceNode;
    PDEVICE_NODE NextDeviceNode;
    NTSTATUS Status;

    /* Copy context data so we don't overwrite it in subsequent calls to this function */
    ParentDeviceNode = Context->DeviceNode;

    /* HACK: Keep a reference to the PDO so we can keep traversing the tree
     * if the device is deleted. In a perfect world, children would have to be
     * deleted before their parents, and we'd restart the traversal after
     * deleting a device node. */
    ObReferenceObject(ParentDeviceNode->PhysicalDeviceObject);

    /* Call the action routine */
    Status = (Context->Action)(ParentDeviceNode, Context->Context);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(ParentDeviceNode->PhysicalDeviceObject);
        return Status;
    }

    /* Traversal of all children nodes */
    for (ChildDeviceNode = ParentDeviceNode->Child;
         ChildDeviceNode != NULL;
         ChildDeviceNode = NextDeviceNode)
    {
        /* HACK: We need this reference to ensure we can get Sibling below. */
        ObReferenceObject(ChildDeviceNode->PhysicalDeviceObject);

        /* Pass the current device node to the action routine */
        Context->DeviceNode = ChildDeviceNode;

        Status = IopTraverseDeviceTreeNode(Context);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(ChildDeviceNode->PhysicalDeviceObject);
            ObDereferenceObject(ParentDeviceNode->PhysicalDeviceObject);
            return Status;
        }

        NextDeviceNode = ChildDeviceNode->Sibling;
        ObDereferenceObject(ChildDeviceNode->PhysicalDeviceObject);
    }

    ObDereferenceObject(ParentDeviceNode->PhysicalDeviceObject);
    return Status;
}


NTSTATUS
IopTraverseDeviceTree(PDEVICETREE_TRAVERSE_CONTEXT Context)
{
    NTSTATUS Status;

    DPRINT("Context 0x%p\n", Context);

    DPRINT("IopTraverseDeviceTree(DeviceNode 0x%p  FirstDeviceNode 0x%p  Action %p  Context 0x%p)\n",
           Context->DeviceNode, Context->FirstDeviceNode, Context->Action, Context->Context);

    /* Start from the specified device node */
    Context->DeviceNode = Context->FirstDeviceNode;

    /* Recursively traverse the device tree */
    Status = IopTraverseDeviceTreeNode(Context);
    if (Status == STATUS_UNSUCCESSFUL)
    {
        /* The action routine just wanted to terminate the traversal with status
        code STATUS_SUCCESS */
        Status = STATUS_SUCCESS;
    }

    return Status;
}


/*
 * IopCreateDeviceKeyPath
 *
 * Creates a registry key
 *
 * Parameters
 *    RegistryPath
 *        Name of the key to be created.
 *    Handle
 *        Handle to the newly created key
 *
 * Remarks
 *     This method can create nested trees, so parent of RegistryPath can
 *     be not existant, and will be created if needed.
 */
NTSTATUS
NTAPI
IopCreateDeviceKeyPath(IN PCUNICODE_STRING RegistryPath,
                       IN ULONG CreateOptions,
                       OUT PHANDLE Handle)
{
    UNICODE_STRING EnumU = RTL_CONSTANT_STRING(ENUM_ROOT);
    HANDLE hParent = NULL, hKey;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    PCWSTR Current, Last;
    USHORT Length;
    NTSTATUS Status;

    /* Assume failure */
    *Handle = NULL;

    /* Open root key for device instances */
    Status = IopOpenRegistryKeyEx(&hParent, NULL, &EnumU, KEY_CREATE_SUB_KEY);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ZwOpenKey('%wZ') failed with status 0x%08lx\n", &EnumU, Status);
        return Status;
    }

    Current = KeyName.Buffer = RegistryPath->Buffer;
    Last = &RegistryPath->Buffer[RegistryPath->Length / sizeof(WCHAR)];

    /* Go up to the end of the string */
    while (Current <= Last)
    {
        if (Current != Last && *Current != L'\\')
        {
            /* Not the end of the string and not a separator */
            Current++;
            continue;
        }

        /* Prepare relative key name */
        Length = (USHORT)((ULONG_PTR)Current - (ULONG_PTR)KeyName.Buffer);
        KeyName.MaximumLength = KeyName.Length = Length;
        DPRINT("Create '%wZ'\n", &KeyName);

        /* Open key */
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   hParent,
                                   NULL);
        Status = ZwCreateKey(&hKey,
                             Current == Last ? KEY_ALL_ACCESS : KEY_CREATE_SUB_KEY,
                             &ObjectAttributes,
                             0,
                             NULL,
                             CreateOptions,
                             NULL);

        /* Close parent key handle, we don't need it anymore */
        if (hParent)
            ZwClose(hParent);

        /* Key opening/creating failed? */
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ZwCreateKey('%wZ') failed with status 0x%08lx\n", &KeyName, Status);
            return Status;
        }

        /* Check if it is the end of the string */
        if (Current == Last)
        {
            /* Yes, return success */
            *Handle = hKey;
            return STATUS_SUCCESS;
        }

        /* Start with this new parent key */
        hParent = hKey;
        Current++;
        KeyName.Buffer = (PWSTR)Current;
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
IopSetDeviceInstanceData(HANDLE InstanceKey,
                         PDEVICE_NODE DeviceNode)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE LogConfKey, ControlKey, DeviceParamsKey;
    ULONG ResCount;
    ULONG ResultLength;
    NTSTATUS Status;

    DPRINT("IopSetDeviceInstanceData() called\n");

    /* Create the 'LogConf' key */
    RtlInitUnicodeString(&KeyName, L"LogConf");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               InstanceKey,
                               NULL);
    Status = ZwCreateKey(&LogConfKey,
                         KEY_ALL_ACCESS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         // FIXME? In r53694 it was silently turned from non-volatile into this,
                         // without any extra warning. Is this still needed??
                         REG_OPTION_VOLATILE,
                         NULL);
    if (NT_SUCCESS(Status))
    {
        /* Set 'BootConfig' value */
        if (DeviceNode->BootResources != NULL)
        {
            ResCount = DeviceNode->BootResources->Count;
            if (ResCount != 0)
            {
                RtlInitUnicodeString(&KeyName, L"BootConfig");
                Status = ZwSetValueKey(LogConfKey,
                                       &KeyName,
                                       0,
                                       REG_RESOURCE_LIST,
                                       DeviceNode->BootResources,
                                       PnpDetermineResourceListSize(DeviceNode->BootResources));
            }
        }

        /* Set 'BasicConfigVector' value */
        if (DeviceNode->ResourceRequirements != NULL &&
            DeviceNode->ResourceRequirements->ListSize != 0)
        {
            RtlInitUnicodeString(&KeyName, L"BasicConfigVector");
            Status = ZwSetValueKey(LogConfKey,
                                   &KeyName,
                                   0,
                                   REG_RESOURCE_REQUIREMENTS_LIST,
                                   DeviceNode->ResourceRequirements,
                                   DeviceNode->ResourceRequirements->ListSize);
        }

        ZwClose(LogConfKey);
    }

    /* Set the 'ConfigFlags' value */
    RtlInitUnicodeString(&KeyName, L"ConfigFlags");
    Status = ZwQueryValueKey(InstanceKey,
                             &KeyName,
                             KeyValueBasicInformation,
                             NULL,
                             0,
                             &ResultLength);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        /* Write the default value */
        ULONG DefaultConfigFlags = 0;
        Status = ZwSetValueKey(InstanceKey,
                               &KeyName,
                               0,
                               REG_DWORD,
                               &DefaultConfigFlags,
                               sizeof(DefaultConfigFlags));
    }

    /* Create the 'Control' key */
    RtlInitUnicodeString(&KeyName, L"Control");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               InstanceKey,
                               NULL);
    Status = ZwCreateKey(&ControlKey,
                         0,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         NULL);
    if (NT_SUCCESS(Status))
        ZwClose(ControlKey);

    /* Create the 'Device Parameters' key and set the 'FirmwareIdentified' value for all ACPI-enumerated devices */
    if (_wcsnicmp(DeviceNode->InstancePath.Buffer, L"ACPI\\", 5) == 0)
    {
        RtlInitUnicodeString(&KeyName, L"Device Parameters");
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   InstanceKey,
                                   NULL);
        Status = ZwCreateKey(&DeviceParamsKey,
                             0,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             NULL);
        if (NT_SUCCESS(Status))
        {
            ULONG FirmwareIdentified = 1;
            RtlInitUnicodeString(&KeyName, L"FirmwareIdentified");
            Status = ZwSetValueKey(DeviceParamsKey,
                                   &KeyName,
                                   0,
                                   REG_DWORD,
                                   &FirmwareIdentified,
                                   sizeof(FirmwareIdentified));

            ZwClose(DeviceParamsKey);
        }
    }

    DPRINT("IopSetDeviceInstanceData() done\n");

    return Status;
}

/*
 * IopInitializePnpServices
 *
 * Initialize services for discovered children
 *
 * Parameters
 *    DeviceNode
 *       Top device node to start initializing services.
 *
 * Return Value
 *    Status
 */
NTSTATUS
IopInitializePnpServices(IN PDEVICE_NODE DeviceNode)
{
    DEVICETREE_TRAVERSE_CONTEXT Context;

    DPRINT("IopInitializePnpServices(%p)\n", DeviceNode);

    IopInitDeviceTreeTraverseContext(
        &Context,
        DeviceNode,
        IopActionInitChildServices,
        DeviceNode);

    return IopTraverseDeviceTree(&Context);
}

NTSTATUS
NTAPI
IopCreateRegistryKeyEx(OUT PHANDLE Handle,
                       IN HANDLE RootHandle OPTIONAL,
                       IN PUNICODE_STRING KeyName,
                       IN ACCESS_MASK DesiredAccess,
                       IN ULONG CreateOptions,
                       OUT PULONG Disposition OPTIONAL)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG KeyDisposition, RootHandleIndex = 0, i = 1, NestedCloseLevel = 0;
    USHORT Length;
    HANDLE HandleArray[2];
    BOOLEAN Recursing = TRUE;
    PWCHAR pp, p, p1;
    UNICODE_STRING KeyString;
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();

    /* P1 is start, pp is end */
    p1 = KeyName->Buffer;
    pp = (PVOID)((ULONG_PTR)p1 + KeyName->Length);

    /* Create the target key */
    InitializeObjectAttributes(&ObjectAttributes,
                               KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               RootHandle,
                               NULL);
    Status = ZwCreateKey(&HandleArray[i],
                         DesiredAccess,
                         &ObjectAttributes,
                         0,
                         NULL,
                         CreateOptions,
                         &KeyDisposition);

    /* Now we check if this failed */
    if ((Status == STATUS_OBJECT_NAME_NOT_FOUND) && (RootHandle))
    {
        /* Target key failed, so we'll need to create its parent. Setup array */
        HandleArray[0] = NULL;
        HandleArray[1] = RootHandle;

        /* Keep recursing for each missing parent */
        while (Recursing)
        {
            /* And if we're deep enough, close the last handle */
            if (NestedCloseLevel > 1) ZwClose(HandleArray[RootHandleIndex]);

            /* We're setup to ping-pong between the two handle array entries */
            RootHandleIndex = i;
            i = (i + 1) & 1;

            /* Clear the one we're attempting to open now */
            HandleArray[i] = NULL;

            /* Process the parent key name */
            for (p = p1; ((p < pp) && (*p != OBJ_NAME_PATH_SEPARATOR)); p++);
            Length = (USHORT)(p - p1) * sizeof(WCHAR);

            /* Is there a parent name? */
            if (Length)
            {
                /* Build the unicode string for it */
                KeyString.Buffer = p1;
                KeyString.Length = KeyString.MaximumLength = Length;

                /* Now try opening the parent */
                InitializeObjectAttributes(&ObjectAttributes,
                                           &KeyString,
                                           OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                           HandleArray[RootHandleIndex],
                                           NULL);
                Status = ZwCreateKey(&HandleArray[i],
                                     DesiredAccess,
                                     &ObjectAttributes,
                                     0,
                                     NULL,
                                     CreateOptions,
                                     &KeyDisposition);
                if (NT_SUCCESS(Status))
                {
                    /* It worked, we have one more handle */
                    NestedCloseLevel++;
                }
                else
                {
                    /* Parent key creation failed, abandon loop */
                    Recursing = FALSE;
                    continue;
                }
            }
            else
            {
                /* We don't have a parent name, probably corrupted key name */
                Status = STATUS_INVALID_PARAMETER;
                Recursing = FALSE;
                continue;
            }

            /* Now see if there's more parents to create */
            p1 = p + 1;
            if ((p == pp) || (p1 == pp))
            {
                /* We're done, hopefully successfully, so stop */
                Recursing = FALSE;
            }
        }

        /* Outer loop check for handle nesting that requires closing the top handle */
        if (NestedCloseLevel > 1) ZwClose(HandleArray[RootHandleIndex]);
    }

    /* Check if we broke out of the loop due to success */
    if (NT_SUCCESS(Status))
    {
        /* Return the target handle (we closed all the parent ones) and disposition */
        *Handle = HandleArray[i];
        if (Disposition) *Disposition = KeyDisposition;
    }

    /* Return the success state */
    return Status;
}

NTSTATUS
NTAPI
IopGetRegistryValue(IN HANDLE Handle,
                    IN PWSTR ValueName,
                    OUT PKEY_VALUE_FULL_INFORMATION *Information)
{
    UNICODE_STRING ValueString;
    NTSTATUS Status;
    PKEY_VALUE_FULL_INFORMATION FullInformation;
    ULONG Size;
    PAGED_CODE();

    RtlInitUnicodeString(&ValueString, ValueName);

    Status = ZwQueryValueKey(Handle,
                             &ValueString,
                             KeyValueFullInformation,
                             NULL,
                             0,
                             &Size);
    if ((Status != STATUS_BUFFER_OVERFLOW) &&
        (Status != STATUS_BUFFER_TOO_SMALL))
    {
        return Status;
    }

    FullInformation = ExAllocatePool(NonPagedPool, Size);
    if (!FullInformation) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ZwQueryValueKey(Handle,
                             &ValueString,
                             KeyValueFullInformation,
                             FullInformation,
                             Size,
                             &Size);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(FullInformation);
        return Status;
    }

    *Information = FullInformation;
    return STATUS_SUCCESS;
}

RTL_GENERIC_COMPARE_RESULTS
NTAPI
PiCompareInstancePath(IN PRTL_AVL_TABLE Table,
                      IN PVOID FirstStruct,
                      IN PVOID SecondStruct)
{
    PPNP_DEVICE_INSTANCE_CONTEXT FirstContext = FirstStruct;
    PPNP_DEVICE_INSTANCE_CONTEXT SecondContext = SecondStruct;
    PUNICODE_STRING First = FirstContext->InstancePath;
    PUNICODE_STRING Second = SecondContext->InstancePath;
    LONG Result;
    RTL_GENERIC_COMPARE_RESULTS CompareResult;

    Result = RtlCompareUnicodeString(First, Second, TRUE);

    if (Result < 0)
        CompareResult = GenericLessThan;
    else if (Result == 0)
        CompareResult = GenericEqual;
    else
        CompareResult = GenericGreaterThan;

    return CompareResult;
}

//
//  The allocation function is called by the generic table package whenever
//  it needs to allocate memory for the table.
//

PVOID
NTAPI
PiAllocateGenericTableEntry(IN PRTL_AVL_TABLE Table,
                            IN CLONG ByteSize)
{
    PAGED_CODE();
    return ExAllocatePoolWithTag(PagedPool, ByteSize, 'uspP');
}

VOID
NTAPI
PiFreeGenericTableEntry(IN PRTL_AVL_TABLE Table,
                        IN PVOID Buffer)
{
    ExFreePoolWithTag(Buffer, 'uspP');
}

VOID
NTAPI
PpInitializeDeviceReferenceTable(VOID)
{
    /* Setup the guarded mutex and AVL table */
    KeInitializeGuardedMutex(&PpDeviceReferenceTableLock);
    RtlInitializeGenericTableAvl(
        &PpDeviceReferenceTable,
        (PRTL_AVL_COMPARE_ROUTINE)PiCompareInstancePath,
        (PRTL_AVL_ALLOCATE_ROUTINE)PiAllocateGenericTableEntry,
        (PRTL_AVL_FREE_ROUTINE)PiFreeGenericTableEntry,
        NULL);
}

BOOLEAN
NTAPI
PiInitPhase0(VOID)
{
    /* Initialize the resource when accessing device registry data */
    ExInitializeResourceLite(&PpRegistryDeviceResource);

    /* Setup the device reference AVL table */
    PpInitializeDeviceReferenceTable();
    return TRUE;
}

BOOLEAN
NTAPI
PiInitPhase1(VOID)
{
    PKEY_VALUE_FULL_INFORMATION KeyInfo;
    UNICODE_STRING KeyName;
    HANDLE RootKeyHandle;
    HANDLE Handle;
    NTSTATUS Status;

    RtlInitUnicodeString(&KeyName, IO_REG_KEY_CURRENTCONTROLSET);
    Status = IopOpenRegistryKeyEx(&RootKeyHandle,
                                  NULL,
                                  &KeyName,
                                  KEY_ALL_ACCESS);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PiInitPhase1: Status - %X\n", Status);
        ASSERT(0);
        goto Exit;
    }

    RtlInitUnicodeString(&KeyName, L"Control\\Pnp");
    Status = IopCreateRegistryKeyEx(&Handle,
                                    RootKeyHandle,
                                    &KeyName,
                                    KEY_ALL_ACCESS,
                                    0,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PiInitPhase1: Status - %X\n", Status);
        ASSERT(0);
        ZwClose(RootKeyHandle);
        goto Exit;
    }

    Status = IopGetRegistryValue(Handle,
                                 L"DisableFirmwareMapper",
                                 &KeyInfo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PiInitPhase1: Status - %X\n", Status);
        ASSERT(0);
        ZwClose(Handle);
        ZwClose(RootKeyHandle);
        goto Exit;
    }

    if (KeyInfo->Type == REG_DWORD && KeyInfo->DataLength == sizeof(ULONG))
    {
        PpDisableFirmwareMapper = *(PULONG)((PUCHAR)KeyInfo + KeyInfo->DataOffset);
        DPRINT("PiInitPhase1: PpDisableFirmwareMapper - %X\n", PpDisableFirmwareMapper);
    }
    else
    {
        DPRINT1("PiInitPhase1: KeyInfo->Type - %X, KeyInfo->DataLength - %X\n", KeyInfo->Type, KeyInfo->DataLength);
        ASSERT(0);
    }

    ExFreePoolWithTag(KeyInfo, 'uspP');

    ZwClose(Handle);
    ZwClose(RootKeyHandle);

Exit:

    if (!PpDisableFirmwareMapper)
    {
        DPRINT1("PiInitPhase1: FIXME PnPBiosInitializePnPBios()\n");
        //PnPBiosInitializePnPBios();
    }

    return TRUE;
}

BOOLEAN
NTAPI
PpInitSystem(VOID)
{
    /* Check the initialization phase */
    switch (ExpInitializationPhase)
    {
    case 0:

        /* Do Phase 0 */
        return PiInitPhase0();

    case 1:

        /* Do Phase 1 */
        return PiInitPhase1();

    default:

        /* Don't know any other phase! Bugcheck! */
        KeBugCheck(UNEXPECTED_INITIALIZATION_CALL);
        return FALSE;
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
PnpBusTypeGuidGet(IN USHORT Index,
                  IN LPGUID BusTypeGuid)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /* Acquire the lock */
    ExAcquireFastMutex(&PnpBusTypeGuidList->Lock);

    /* Validate size */
    if (Index < PnpBusTypeGuidList->GuidCount)
    {
        /* Copy the data */
        RtlCopyMemory(BusTypeGuid, &PnpBusTypeGuidList->Guids[Index], sizeof(GUID));
    }
    else
    {
        /* Failure path */
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }

    /* Release lock and return status */
    ExReleaseFastMutex(&PnpBusTypeGuidList->Lock);
    return Status;
}

NTSTATUS
NTAPI
PnpDeviceObjectToDeviceInstance(IN PDEVICE_OBJECT DeviceObject,
                                IN PHANDLE DeviceInstanceHandle,
                                IN ACCESS_MASK DesiredAccess)
{
    NTSTATUS Status;
    HANDLE KeyHandle;
    PDEVICE_NODE DeviceNode;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(ENUM_ROOT);

    PAGED_CODE();

    /* Open the enum key */
    Status = IopOpenRegistryKeyEx(&KeyHandle, NULL, &KeyName, KEY_READ);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("PnpDeviceObjectToDeviceInstance: Status %X\n", Status);
        return Status;
    }

    /* Make sure we have an instance path */
    DeviceNode = IopGetDeviceNode(DeviceObject);
    if ((DeviceNode) && (DeviceNode->InstancePath.Length))
    {
        /* Get the instance key */
        Status = IopOpenRegistryKeyEx(DeviceInstanceHandle,
                                      KeyHandle,
                                      &DeviceNode->InstancePath,
                                      DesiredAccess);
    }
    else
    {
        /* Fail */
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }

    /* Close the handle and return status */
    ZwClose(KeyHandle);
    return Status;
}

ULONG
NTAPI
PnpDetermineResourceListSize(IN PCM_RESOURCE_LIST ResourceList)
{
    ULONG FinalSize, PartialSize, EntrySize, i, j;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;

    /* If we don't have one, that's easy */
    if (!ResourceList) return 0;

    /* Start with the minimum size possible */
    FinalSize = FIELD_OFFSET(CM_RESOURCE_LIST, List);

    /* Loop each full descriptor */
    FullDescriptor = ResourceList->List;
    for (i = 0; i < ResourceList->Count; i++)
    {
        /* Start with the minimum size possible */
        PartialSize = FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) +
        FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors);

        /* Loop each partial descriptor */
        PartialDescriptor = FullDescriptor->PartialResourceList.PartialDescriptors;
        for (j = 0; j < FullDescriptor->PartialResourceList.Count; j++)
        {
            /* Start with the minimum size possible */
            EntrySize = sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

            /* Check if there is extra data */
            if (PartialDescriptor->Type == CmResourceTypeDeviceSpecific)
            {
                /* Add that data */
                EntrySize += PartialDescriptor->u.DeviceSpecificData.DataSize;
            }

            /* The size of partial descriptors is bigger */
            PartialSize += EntrySize;

            /* Go to the next partial descriptor */
            PartialDescriptor = (PVOID)((ULONG_PTR)PartialDescriptor + EntrySize);
        }

        /* The size of full descriptors is bigger */
        FinalSize += PartialSize;

        /* Go to the next full descriptor */
        FullDescriptor = (PVOID)((ULONG_PTR)FullDescriptor + PartialSize);
    }

    /* Return the final size */
    return FinalSize;
}

NTSTATUS
NTAPI
PiGetDeviceRegistryProperty(IN PDEVICE_OBJECT DeviceObject,
                            IN ULONG ValueType,
                            IN PWSTR ValueName,
                            IN PWSTR KeyName,
                            OUT PVOID Buffer,
                            IN PULONG BufferLength)
{
    NTSTATUS Status;
    HANDLE KeyHandle, SubHandle;
    UNICODE_STRING KeyString;
    PKEY_VALUE_FULL_INFORMATION KeyValueInfo = NULL;
    ULONG Length;
    PAGED_CODE();

    /* Find the instance key */
    Status = PnpDeviceObjectToDeviceInstance(DeviceObject, &KeyHandle, KEY_READ);
    if (NT_SUCCESS(Status))
    {
        /* Check for name given by caller */
        if (KeyName)
        {
            /* Open this key */
            RtlInitUnicodeString(&KeyString, KeyName);
            Status = IopOpenRegistryKeyEx(&SubHandle,
                                          KeyHandle,
                                          &KeyString,
                                          KEY_READ);
            if (NT_SUCCESS(Status))
            {
                /* And use this handle instead */
                ZwClose(KeyHandle);
                KeyHandle = SubHandle;
            }
        }

        /* Check if sub-key handle succeeded (or no-op if no key name given) */
        if (NT_SUCCESS(Status))
        {
            /* Now get the size of the property */
            Status = IopGetRegistryValue(KeyHandle,
                                         ValueName,
                                         &KeyValueInfo);
        }

        /* Close the key */
        ZwClose(KeyHandle);
    }

    /* Fail if any of the registry operations failed */
    if (!NT_SUCCESS(Status)) return Status;

    /* Check how much data we have to copy */
    Length = KeyValueInfo->DataLength;
    if (*BufferLength >= Length)
    {
        /* Check for a match in the value type */
        if (KeyValueInfo->Type == ValueType)
        {
            /* Copy the data */
            RtlCopyMemory(Buffer,
                          (PVOID)((ULONG_PTR)KeyValueInfo +
                          KeyValueInfo->DataOffset),
                          Length);
        }
        else
        {
            /* Invalid registry property type, fail */
           Status = STATUS_INVALID_PARAMETER_2;
        }
    }
    else
    {
        /* Buffer is too small to hold data */
        Status = STATUS_BUFFER_TOO_SMALL;
    }

    /* Return the required buffer length, free the buffer, and return status */
    *BufferLength = Length;
    ExFreePool(KeyValueInfo);
    return Status;
}

#define PIP_RETURN_DATA(x, y)   {ReturnLength = x; Data = y; Status = STATUS_SUCCESS; break;}
#define PIP_REGISTRY_DATA(x, y) {ValueName = x; ValueType = y; break;}
#define PIP_UNIMPLEMENTED()     {UNIMPLEMENTED_DBGBREAK(); break;}

NTSTATUS
NTAPI
IoGetDeviceProperty(IN PDEVICE_OBJECT DeviceObject,
                    IN DEVICE_REGISTRY_PROPERTY DeviceProperty,
                    IN ULONG BufferLength,
                    OUT PVOID PropertyBuffer,
                    OUT PULONG ResultLength)
{
    PDEVICE_NODE DeviceNode = IopGetDeviceNode(DeviceObject);
    POBJECT_NAME_INFORMATION ObjectNameInfo = NULL;
    DEVICE_CAPABILITIES DeviceCaps;
    PWSTR DeviceInstanceName;
    PWSTR ValueName = NULL;
    PWCHAR EnumeratorNameEnd;
    PVOID Data = NULL;
    GUID BusTypeGuid;
    ULONG ReturnLength = 0;
    ULONG Length = 0;
    ULONG ValueType;
    NTSTATUS Status = STATUS_BUFFER_TOO_SMALL;
    BOOLEAN NullTerminate = FALSE;

    DPRINT("IoGetDeviceProperty(0x%p %d)\n", DeviceObject, DeviceProperty);

    /* Assume failure */
    *ResultLength = 0;

    if (!DeviceNode)
    {
        /* Only PDOs can call this */
        DPRINT1("IoGetDeviceProperty: STATUS_INVALID_DEVICE_REQUEST\n");
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    /* Handle all properties */
    switch (DeviceProperty)
    {
        case DevicePropertyBusTypeGuid:
        {
            /* Get the GUID from the internal cache */
            Status = PnpBusTypeGuidGet(DeviceNode->ChildBusTypeIndex, &BusTypeGuid);
            if (!NT_SUCCESS(Status))
                return Status;

            /* This is the format of the returned data */
            PIP_RETURN_DATA(sizeof(GUID), &BusTypeGuid);
        }
        case DevicePropertyLegacyBusType:
        {
            /* Validate correct interface type */
            if (DeviceNode->ChildInterfaceType == InterfaceTypeUndefined)
                return STATUS_OBJECT_NAME_NOT_FOUND;

            /* This is the format of the returned data */
            PIP_RETURN_DATA(sizeof(INTERFACE_TYPE), &DeviceNode->ChildInterfaceType);
        }
        case DevicePropertyBusNumber:
        {
            /* Validate correct bus number */
            if ((DeviceNode->ChildBusNumber & 0x80000000) == 0x80000000)
                return STATUS_OBJECT_NAME_NOT_FOUND;

            /* This is the format of the returned data */
            PIP_RETURN_DATA(sizeof(ULONG), &DeviceNode->ChildBusNumber);
        }
        case DevicePropertyEnumeratorName:
        {
            /* Get the instance path */
            DeviceInstanceName = DeviceNode->InstancePath.Buffer;

            /* Sanity checks */
            ASSERT((BufferLength & 1) == 0);
            ASSERT(DeviceInstanceName != NULL);

            /* Get the name from the path */
            EnumeratorNameEnd = wcschr(DeviceInstanceName, OBJ_NAME_PATH_SEPARATOR);
            ASSERT(EnumeratorNameEnd);

            /* This string needs to be NULL-terminated */
            NullTerminate = TRUE;

            /* This is the format of the returned data */
            PIP_RETURN_DATA((ULONG)(EnumeratorNameEnd - DeviceInstanceName) * sizeof(WCHAR), DeviceInstanceName);
        }
        case DevicePropertyAddress:
        {
            /* Query the device caps */
            Status = PpIrpQueryCapabilities(DeviceObject, &DeviceCaps);
            if (!NT_SUCCESS(Status) || (DeviceCaps.Address == MAXULONG))
                return STATUS_OBJECT_NAME_NOT_FOUND;

            /* This is the format of the returned data */
            PIP_RETURN_DATA(sizeof(ULONG), &DeviceCaps.Address);
        }
        case DevicePropertyBootConfigurationTranslated:
        {
            // w2003 return STATUS_NOT_SUPPORTED
            return STATUS_NOT_SUPPORTED;
        }
        case DevicePropertyPhysicalDeviceObjectName:
        {
            /* Sanity check for Unicode-sized string */
            ASSERT((BufferLength & 1) == 0);

            /* Allocate name buffer */
            Length = BufferLength + sizeof(OBJECT_NAME_INFORMATION);
            ObjectNameInfo = ExAllocatePool(PagedPool, Length);
            if (!ObjectNameInfo)
            {
                DPRINT1("IoGetDeviceProperty: STATUS_INSUFFICIENT_RESOURCES\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            /* Query the PDO name */
            Status = ObQueryNameString(DeviceObject, ObjectNameInfo, Length, ResultLength);
            if (Status == STATUS_INFO_LENGTH_MISMATCH)
                /* It's up to the caller to try again */
                Status = STATUS_BUFFER_TOO_SMALL;

            /* This string needs to be NULL-terminated */
            NullTerminate = TRUE;

            /* Return if successful */
            if (NT_SUCCESS(Status))
                PIP_RETURN_DATA(ObjectNameInfo->Name.Length, ObjectNameInfo->Name.Buffer);

            /* Let the caller know how big the name is */
            *ResultLength -= sizeof(OBJECT_NAME_INFORMATION);
            break;
        }

        /* Handle the registry-based properties */
        case DevicePropertyUINumber:
            PIP_REGISTRY_DATA(REGSTR_VAL_UI_NUMBER, REG_DWORD);

        case DevicePropertyLocationInformation:
            PIP_REGISTRY_DATA(REGSTR_VAL_LOCATION_INFORMATION, REG_SZ);

        case DevicePropertyDeviceDescription:
            PIP_REGISTRY_DATA(REGSTR_VAL_DEVDESC, REG_SZ);

        case DevicePropertyHardwareID:
            PIP_REGISTRY_DATA(REGSTR_VAL_HARDWAREID, REG_MULTI_SZ);

        case DevicePropertyCompatibleIDs:
            PIP_REGISTRY_DATA(REGSTR_VAL_COMPATIBLEIDS, REG_MULTI_SZ);

        case DevicePropertyBootConfiguration:
            PIP_REGISTRY_DATA(REGSTR_VAL_BOOTCONFIG, REG_RESOURCE_LIST);

        case DevicePropertyClassName:
            PIP_REGISTRY_DATA(REGSTR_VAL_CLASS, REG_SZ);

        case DevicePropertyClassGuid:
            PIP_REGISTRY_DATA(REGSTR_VAL_CLASSGUID, REG_SZ);

        case DevicePropertyDriverKeyName:
            PIP_REGISTRY_DATA(REGSTR_VAL_DRIVER, REG_SZ);

        case DevicePropertyManufacturer:
            PIP_REGISTRY_DATA(REGSTR_VAL_MFG, REG_SZ);

        case DevicePropertyFriendlyName:
            PIP_REGISTRY_DATA(REGSTR_VAL_FRIENDLYNAME, REG_SZ);

        case DevicePropertyContainerID:
            //PIP_REGISTRY_DATA(REGSTR_VAL_CONTAINERID, REG_SZ); // Win7
            PIP_UNIMPLEMENTED();

        case DevicePropertyRemovalPolicy:
        {
            DEVICE_REMOVAL_POLICY Policy;

            ASSERT(BufferLength == sizeof(DEVICE_REMOVAL_POLICY));
            *ResultLength = sizeof(DEVICE_REMOVAL_POLICY);

            PpHotSwapGetDevnodeRemovalPolicy(DeviceNode, TRUE, &Policy);
            DPRINT("IoGetDeviceProperty: DevicePropertyRemovalPolicy %X\n", Policy);

            PIP_RETURN_DATA(sizeof(DEVICE_REMOVAL_POLICY), &Policy);
        }
        case DevicePropertyInstallState:
            PIP_REGISTRY_DATA(REGSTR_VAL_CONFIGFLAGS, REG_DWORD);
            break;

        case DevicePropertyResourceRequirements:
            PIP_UNIMPLEMENTED();

        case DevicePropertyAllocatedResources:
            PIP_UNIMPLEMENTED();

        default:
            DPRINT1("IoGetDeviceProperty: [%p] invalid DeviceProperty %d\n", DeviceObject, DeviceProperty);
            return STATUS_INVALID_PARAMETER_2;
    }

    /* Having a registry value name implies registry data */
    if (ValueName)
    {
        /* We know up-front how much data to expect */
        *ResultLength = BufferLength;

        /* Go get the data, use the LogConf subkey if necessary */
        Status = PiGetDeviceRegistryProperty(DeviceObject,
                                             ValueType,
                                             ValueName,
                                             ((DeviceProperty == DevicePropertyBootConfiguration) ? L"LogConf":  NULL),
                                             PropertyBuffer,
                                             ResultLength);
    }
    else if (NT_SUCCESS(Status))
    {
        /* We know up-front how much data to expect, check the caller's buffer */
        *ResultLength = (ReturnLength + (NullTerminate ? sizeof(UNICODE_NULL) : 0));

        if (*ResultLength <= BufferLength)
        {
            /* Buffer is all good, copy the data */
            RtlCopyMemory(PropertyBuffer, Data, ReturnLength);

            /* Check if we need to NULL-terminate the string */
            if (NullTerminate)
                /* Terminate the string */
                ((PWCHAR)PropertyBuffer)[ReturnLength / sizeof(WCHAR)] = UNICODE_NULL;

            /* This is the success path */
            Status = STATUS_SUCCESS;
        }
        else
        {
            /* Failure path */
            Status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    if (ObjectNameInfo)
        ExFreePool(ObjectNameInfo);

    return Status;
}

/**
 * @name IoOpenDeviceRegistryKey
 *
 * Open a registry key unique for a specified driver or device instance.
 *
 * @param DeviceObject   Device to get the registry key for.
 * @param DevInstKeyType Type of the key to return.
 * @param DesiredAccess  Access mask (eg. KEY_READ | KEY_WRITE).
 * @param DevInstRegKey  Handle to the opened registry key on
 *                       successful return.
 *
 * @return Status.
 *
 * @implemented
 */
NTSTATUS
NTAPI
IoOpenDeviceRegistryKey(IN PDEVICE_OBJECT DeviceObject,
                        IN ULONG DevInstKeyType,
                        IN ACCESS_MASK DesiredAccess,
                        OUT PHANDLE DevInstRegKey)
{
    static WCHAR RootKeyName[] =
        L"\\Registry\\Machine\\System\\CurrentControlSet\\";
    static WCHAR ProfileKeyName[] =
        L"Hardware Profiles\\Current\\System\\CurrentControlSet\\";
    static WCHAR ClassKeyName[] = L"Control\\Class\\";
    static WCHAR EnumKeyName[] = L"Enum\\";
    static WCHAR DeviceParametersKeyName[] = L"Device Parameters";
    ULONG KeyNameLength;
    PWSTR KeyNameBuffer;
    UNICODE_STRING KeyName;
    ULONG DriverKeyLength;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PDEVICE_NODE DeviceNode = NULL;
    NTSTATUS Status;

    DPRINT("IoOpenDeviceRegistryKey() called\n");

    if ((DevInstKeyType & (PLUGPLAY_REGKEY_DEVICE | PLUGPLAY_REGKEY_DRIVER)) == 0)
    {
        DPRINT1("IoOpenDeviceRegistryKey(): got wrong params, exiting... \n");
        return STATUS_INVALID_PARAMETER;
    }

    if (!IopIsValidPhysicalDeviceObject(DeviceObject))
        return STATUS_INVALID_DEVICE_REQUEST;
    DeviceNode = IopGetDeviceNode(DeviceObject);

    /*
     * Calculate the length of the base key name. This is the full
     * name for driver key or the name excluding "Device Parameters"
     * subkey for device key.
     */

    KeyNameLength = sizeof(RootKeyName);
    if (DevInstKeyType & PLUGPLAY_REGKEY_CURRENT_HWPROFILE)
        KeyNameLength += sizeof(ProfileKeyName) - sizeof(UNICODE_NULL);
    if (DevInstKeyType & PLUGPLAY_REGKEY_DRIVER)
    {
        KeyNameLength += sizeof(ClassKeyName) - sizeof(UNICODE_NULL);
        Status = IoGetDeviceProperty(DeviceObject, DevicePropertyDriverKeyName,
                                     0, NULL, &DriverKeyLength);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            return Status;
        KeyNameLength += DriverKeyLength;
    }
    else
    {
        KeyNameLength += sizeof(EnumKeyName) - sizeof(UNICODE_NULL) +
                         DeviceNode->InstancePath.Length;
    }

    /*
     * Now allocate the buffer for the key name...
     */

    KeyNameBuffer = ExAllocatePool(PagedPool, KeyNameLength);
    if (KeyNameBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeyName.Length = 0;
    KeyName.MaximumLength = (USHORT)KeyNameLength;
    KeyName.Buffer = KeyNameBuffer;

    /*
     * ...and build the key name.
     */

    KeyName.Length += sizeof(RootKeyName) - sizeof(UNICODE_NULL);
    RtlCopyMemory(KeyNameBuffer, RootKeyName, KeyName.Length);

    if (DevInstKeyType & PLUGPLAY_REGKEY_CURRENT_HWPROFILE)
        RtlAppendUnicodeToString(&KeyName, ProfileKeyName);

    if (DevInstKeyType & PLUGPLAY_REGKEY_DRIVER)
    {
        RtlAppendUnicodeToString(&KeyName, ClassKeyName);
        Status = IoGetDeviceProperty(DeviceObject, DevicePropertyDriverKeyName,
                                     DriverKeyLength, KeyNameBuffer +
                                     (KeyName.Length / sizeof(WCHAR)),
                                     &DriverKeyLength);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Call to IoGetDeviceProperty() failed with Status 0x%08lx\n", Status);
            ExFreePool(KeyNameBuffer);
            return Status;
        }
        KeyName.Length += (USHORT)DriverKeyLength - sizeof(UNICODE_NULL);
    }
    else
    {
        RtlAppendUnicodeToString(&KeyName, EnumKeyName);
        Status = RtlAppendUnicodeStringToString(&KeyName, &DeviceNode->InstancePath);
        if (DeviceNode->InstancePath.Length == 0)
        {
            ExFreePool(KeyNameBuffer);
            return Status;
        }
    }

    /*
     * Open the base key.
     */
    Status = IopOpenRegistryKeyEx(DevInstRegKey, NULL, &KeyName, DesiredAccess);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoOpenDeviceRegistryKey(%wZ): Base key doesn't exist, exiting... (Status 0x%08lx)\n", &KeyName, Status);
        ExFreePool(KeyNameBuffer);
        return Status;
    }
    ExFreePool(KeyNameBuffer);

    /*
     * For driver key we're done now.
     */

    if (DevInstKeyType & PLUGPLAY_REGKEY_DRIVER)
        return Status;

    /*
     * Let's go further. For device key we must open "Device Parameters"
     * subkey and create it if it doesn't exist yet.
     */

    RtlInitUnicodeString(&KeyName, DeviceParametersKeyName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               *DevInstRegKey,
                               NULL);
    Status = ZwCreateKey(DevInstRegKey,
                         DesiredAccess,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    ZwClose(ObjectAttributes.RootDirectory);

    return Status;
}

static
NTSTATUS
IopQueryRemoveChildDevices(PDEVICE_NODE ParentDeviceNode, BOOLEAN Force)
{
    PDEVICE_NODE ChildDeviceNode, NextDeviceNode, FailedRemoveDevice;
    NTSTATUS Status;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    ChildDeviceNode = ParentDeviceNode->Child;
    while (ChildDeviceNode != NULL)
    {
        NextDeviceNode = ChildDeviceNode->Sibling;
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

        Status = IopPrepareDeviceForRemoval(ChildDeviceNode->PhysicalDeviceObject, Force);
        if (!NT_SUCCESS(Status))
        {
            FailedRemoveDevice = ChildDeviceNode;
            goto cleanup;
        }

        KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
        ChildDeviceNode = NextDeviceNode;
    }
    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    return STATUS_SUCCESS;

cleanup:
    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    ChildDeviceNode = ParentDeviceNode->Child;
    while (ChildDeviceNode != NULL)
    {
        NextDeviceNode = ChildDeviceNode->Sibling;
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

        IopCancelPrepareDeviceForRemoval(ChildDeviceNode->PhysicalDeviceObject);

        /* IRP_MN_CANCEL_REMOVE_DEVICE is also sent to the device
         * that failed the IRP_MN_QUERY_REMOVE_DEVICE request */
        if (ChildDeviceNode == FailedRemoveDevice)
            return Status;

        ChildDeviceNode = NextDeviceNode;

        KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    }
    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    return Status;
}

static
VOID
IopSendRemoveChildDevices(PDEVICE_NODE ParentDeviceNode)
{
    PDEVICE_NODE ChildDeviceNode, NextDeviceNode;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    ChildDeviceNode = ParentDeviceNode->Child;
    while (ChildDeviceNode != NULL)
    {
        NextDeviceNode = ChildDeviceNode->Sibling;
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

        IopSendRemoveDevice(ChildDeviceNode->PhysicalDeviceObject);

        ChildDeviceNode = NextDeviceNode;

        KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    }
    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

static
NTSTATUS
IopQueryRemoveDeviceRelations(PDEVICE_RELATIONS DeviceRelations, BOOLEAN Force)
{
    /* This function DOES NOT dereference the device objects on SUCCESS
     * but it DOES dereference device objects on FAILURE */

    ULONG i, j;
    NTSTATUS Status;

    for (i = 0; i < DeviceRelations->Count; i++)
    {
        Status = IopPrepareDeviceForRemoval(DeviceRelations->Objects[i], Force);
        if (!NT_SUCCESS(Status))
        {
            j = i;
            goto cleanup;
        }
    }

    return STATUS_SUCCESS;

cleanup:
    /* IRP_MN_CANCEL_REMOVE_DEVICE is also sent to the device
     * that failed the IRP_MN_QUERY_REMOVE_DEVICE request */
    for (i = 0; i <= j; i++)
    {
        IopCancelPrepareDeviceForRemoval(DeviceRelations->Objects[i]);
        ObDereferenceObject(DeviceRelations->Objects[i]);
        DeviceRelations->Objects[i] = NULL;
    }
    for (; i < DeviceRelations->Count; i++)
    {
        ObDereferenceObject(DeviceRelations->Objects[i]);
        DeviceRelations->Objects[i] = NULL;
    }
    ExFreePool(DeviceRelations);

    return Status;
}

static
VOID
IopSendRemoveDeviceRelations(PDEVICE_RELATIONS DeviceRelations)
{
    /* This function DOES dereference the device objects in all cases */

    ULONG i;

    for (i = 0; i < DeviceRelations->Count; i++)
    {
        IopSendRemoveDevice(DeviceRelations->Objects[i]);
        DeviceRelations->Objects[i] = NULL;
    }

    ExFreePool(DeviceRelations);
}

static
VOID
IopCancelRemoveDeviceRelations(PDEVICE_RELATIONS DeviceRelations)
{
    /* This function DOES dereference the device objects in all cases */

    ULONG i;

    for (i = 0; i < DeviceRelations->Count; i++)
    {
        IopCancelPrepareDeviceForRemoval(DeviceRelations->Objects[i]);
        ObDereferenceObject(DeviceRelations->Objects[i]);
        DeviceRelations->Objects[i] = NULL;
    }

    ExFreePool(DeviceRelations);
}

VOID
IopCancelPrepareDeviceForRemoval(PDEVICE_OBJECT DeviceObject)
{
    IO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_RELATIONS DeviceRelations;
    NTSTATUS Status;

    IopCancelRemoveDevice(DeviceObject);

    Stack.Parameters.QueryDeviceRelations.Type = RemovalRelations;

    Status = IopInitiatePnpIrp(DeviceObject,
                               &IoStatusBlock,
                               IRP_MN_QUERY_DEVICE_RELATIONS,
                               &Stack);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("IopInitiatePnpIrp() failed with status 0x%08lx\n", Status);
        DeviceRelations = NULL;
    }
    else
    {
        DeviceRelations = (PDEVICE_RELATIONS)IoStatusBlock.Information;
    }

    if (DeviceRelations)
        IopCancelRemoveDeviceRelations(DeviceRelations);
}

NTSTATUS
IopPrepareDeviceForRemoval(IN PDEVICE_OBJECT DeviceObject, BOOLEAN Force)
{
    PDEVICE_NODE DeviceNode = IopGetDeviceNode(DeviceObject);
    IO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_RELATIONS DeviceRelations;
    NTSTATUS Status;

    if ((DeviceNode->UserFlags & DNUF_NOT_DISABLEABLE) && !Force)
    {
        DPRINT1("Removal not allowed for %wZ\n", &DeviceNode->InstancePath);
        return STATUS_UNSUCCESSFUL;
    }

    if (!Force && IopQueryRemoveDevice(DeviceObject) != STATUS_SUCCESS)
    {
        DPRINT1("Removal vetoed by failing the query remove request\n");

        IopCancelRemoveDevice(DeviceObject);

        return STATUS_UNSUCCESSFUL;
    }

    Stack.Parameters.QueryDeviceRelations.Type = RemovalRelations;

    Status = IopInitiatePnpIrp(DeviceObject,
                               &IoStatusBlock,
                               IRP_MN_QUERY_DEVICE_RELATIONS,
                               &Stack);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("IopInitiatePnpIrp() failed with status 0x%08lx\n", Status);
        DeviceRelations = NULL;
    }
    else
    {
        DeviceRelations = (PDEVICE_RELATIONS)IoStatusBlock.Information;
    }

    if (DeviceRelations)
    {
        Status = IopQueryRemoveDeviceRelations(DeviceRelations, Force);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = IopQueryRemoveChildDevices(DeviceNode, Force);
    if (!NT_SUCCESS(Status))
    {
        if (DeviceRelations)
            IopCancelRemoveDeviceRelations(DeviceRelations);
        return Status;
    }

    if (DeviceRelations)
        IopSendRemoveDeviceRelations(DeviceRelations);
    IopSendRemoveChildDevices(DeviceNode);

    return STATUS_SUCCESS;
}

/* @unimplemented */
VOID
NTAPI
IoRequestDeviceEject(IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // IoDbgBreakPointEx();
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
IoTranslateBusAddress(IN INTERFACE_TYPE InterfaceType,
                      IN ULONG BusNumber,
                      IN PHYSICAL_ADDRESS BusAddress,
                      IN OUT PULONG AddressSpace,
                      OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    /* FIXME: Notify the resource arbiter */

    return HalTranslateBusAddress(InterfaceType,
                                  BusNumber,
                                  BusAddress,
                                  AddressSpace,
                                  TranslatedAddress);
}

#define PIP_FIRST_LENGHT_INSTANCE 10
#define PIP_VALUE_INFO_SIZE       256

NTSTATUS
NTAPI
PiFindDevInstMatch(
    _In_ HANDLE ServiceHandle,
    _In_ PUNICODE_STRING InstancePath,
    _Out_ PULONG OutData,
    _Out_ PUNICODE_STRING OutString,
    _Out_ PULONG OutInstanceNum)
{
    NTSTATUS Status;
    ULONG BufferLen;
    UNICODE_STRING TmpString;
    UNICODE_STRING ValueName;
    USHORT StrLen;
    ULONG ResultLength;
    PWSTR BufferEnd;
    ULONG Length;
    ULONG Count = 0;
    PWSTR Buffer;
    PKEY_VALUE_FULL_INFORMATION KeyInfo = NULL;
    ULONG InstanceNum;

    PAGED_CODE();
    DPRINT("PiFindDevInstMatch: InstancePath - %wZ\n", InstancePath);

    OutString->Length = 0;
    OutString->Buffer = NULL;

    *OutData = 0;
    *OutInstanceNum = (ULONG)-1;

    Status = IopGetRegistryValue(ServiceHandle, L"Count", &KeyInfo);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("PiFindDevInstMatch: Status - %X\n", Status);
        return Status != STATUS_OBJECT_NAME_NOT_FOUND ? Status : STATUS_SUCCESS;
    }

    if (KeyInfo->Type == REG_DWORD && KeyInfo->DataLength >= sizeof(ULONG))
    {
        Count = *(PULONG)((ULONG_PTR)KeyInfo + KeyInfo->DataOffset);
        *OutData = Count;
    }

    ExFreePoolWithTag(KeyInfo, 'uspP');

    KeyInfo = ExAllocatePoolWithTag(PagedPool, PIP_VALUE_INFO_SIZE, '  pP');

    if (!KeyInfo)
    {
        DPRINT("PiFindDevInstMatch: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Buffer = ExAllocatePoolWithTag(PagedPool,
                                   PIP_FIRST_LENGHT_INSTANCE * sizeof(WCHAR),
                                   '  pP');

    if (!Buffer)
    {
        DPRINT("PiFindDevInstMatch: Status - %X\n", Status);
        ExFreePoolWithTag(KeyInfo, '  pP');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (InstanceNum = 0; InstanceNum < Count; InstanceNum++)
    {
        RtlStringCchPrintfExW(Buffer,
                              PIP_FIRST_LENGHT_INSTANCE,
                              &BufferEnd,
                              0,
                              0,
                              L"%u",
                              InstanceNum);

        BufferLen = BufferEnd - Buffer;

        if (BufferLen == (ULONG)-1)
        {
            ValueName.Length = PIP_FIRST_LENGHT_INSTANCE * sizeof(WCHAR);
        }
        else
        {
            ValueName.Length = BufferLen * sizeof(WCHAR);
        }

        ValueName.MaximumLength = PIP_FIRST_LENGHT_INSTANCE * sizeof(WCHAR);
        ValueName.Buffer = Buffer;

        Length = PIP_VALUE_INFO_SIZE;

        Status = ZwQueryValueKey(ServiceHandle,
                                 &ValueName,
                                 KeyValueFullInformation,
                                 KeyInfo,
                                 Length,
                                 &ResultLength);

        if (NT_SUCCESS(Status))
        {
            if (KeyInfo->Type == REG_SZ && KeyInfo->DataLength > sizeof(WCHAR))
            {
                PnpRegSzToString((PWCHAR)((ULONG_PTR)KeyInfo + KeyInfo->DataOffset),
                                 KeyInfo->DataLength,
                                 &StrLen);

                TmpString.Length = StrLen;
                TmpString.MaximumLength = (USHORT)KeyInfo->DataLength;
                TmpString.Buffer = (PWCHAR)((ULONG_PTR)KeyInfo + KeyInfo->DataOffset);

                if (RtlEqualUnicodeString(&TmpString, InstancePath, TRUE))
                {
                    *OutString = ValueName;
                    *OutInstanceNum = InstanceNum;
                    break;
                }
            }
            else
            {
                DPRINT("PiFindDevInstMatch: Type - %X, Length - %X\n",
                       KeyInfo->Type, KeyInfo->DataLength);
            }
        }
        else
        {
            if (Status == STATUS_BUFFER_OVERFLOW ||
                Status == STATUS_BUFFER_TOO_SMALL)
            {
                DPRINT("PiFindDevInstMatch: Status - %X\n", Status);

                ExFreePoolWithTag(KeyInfo, '  pP');

                Length = ResultLength;

                KeyInfo = ExAllocatePoolWithTag(PagedPool, ResultLength, '  pP');

                if (!KeyInfo)
                {
                    DPRINT("PiFindDevInstMatch: STATUS_INSUFFICIENT_RESOURCES\n");
                    ExFreePoolWithTag(Buffer, '  pP');
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                InstanceNum--;
            }
            else
            {
                DPRINT("PiFindDevInstMatch: Status - %X\n", Status);
            }
        }
    }

    if (KeyInfo)
        ExFreePoolWithTag(KeyInfo, '  pP');

    if (!OutString->Length)
        ExFreePoolWithTag(Buffer, '  pP');

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PiProcessDriverInstance(
    _In_ PUNICODE_STRING InstancePath,
    _In_ PUNICODE_STRING ServiceString,
    _In_ PULONG EntryContext,
    _In_ PBOOLEAN EnableInstance)
{
    NTSTATUS Status;
    PWSTR InstanceBuffer;
    ULONG InstanceLength;
    PWSTR InstanceNewBuffer;
    ULONG Length;
    UNICODE_STRING UnicodeString;
    UNICODE_STRING ValueName;
    HANDLE KeyHandle = NULL;
    ULONG InstanceNum;
    ULONG Data = 0;
    WCHAR ValueBuffer[PIP_FIRST_LENGHT_INSTANCE];
    PWCHAR BufferEnd;
    ULONG entryContext = *EntryContext;

    PAGED_CODE();
    DPRINT("PiProcessDriverInstance: [%X] InstancePath - %wZ, ServiceString - %wZ\n",
           entryContext, InstancePath, ServiceString);

    ASSERT(EnableInstance != NULL);

    Status = PipOpenServiceEnumKeys(ServiceString,
                                    KEY_ALL_ACCESS,
                                    NULL,
                                    &KeyHandle,
                                    TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("PiProcessDriverInstance: Status - %X\n", Status);
        return Status;
    }

    Status = PiFindDevInstMatch(KeyHandle,
                                InstancePath,
                                &Data,
                                &UnicodeString,
                                &InstanceNum);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("PiProcessDriverInstance: Status - %X\n", Status);
        ZwClose(KeyHandle);
        return Status;
    }

    if (UnicodeString.Buffer)
    {
        ASSERT(InstanceNum != (ULONG)-1);

        if (*EnableInstance == TRUE)
            goto Exit;

        ZwDeleteValueKey(KeyHandle, &UnicodeString);
        Data--;

        if (Data)
        {
            DPRINT("PiProcessDriverInstance: FIXME PiRearrangeDeviceInstances\n");
            ASSERT(FALSE);
        }
    }
    else
    {
        if (*EnableInstance == FALSE)
        {
            ZwClose(KeyHandle);
            return STATUS_SUCCESS;
        }

        InstanceBuffer = InstancePath->Buffer;
        InstanceLength = InstancePath->Length;

        if (InstanceBuffer[InstanceLength / sizeof(WCHAR) - 1])
        {
            InstanceNewBuffer = ExAllocatePoolWithTag(PagedPool,
                                                      InstanceLength + sizeof(WCHAR),
                                                      '  pP');

            if (InstanceNewBuffer)
            {
                RtlCopyMemory(InstanceNewBuffer,
                              InstanceBuffer,
                              InstanceLength);

                *(PWCHAR)((ULONG_PTR)InstanceNewBuffer + InstanceLength) = UNICODE_NULL;

                InstanceLength += sizeof(WCHAR);
                InstanceBuffer = InstanceNewBuffer;
            }
        }

        RtlStringCchPrintfExW(ValueBuffer,
                              PIP_FIRST_LENGHT_INSTANCE,
                              &BufferEnd,
                              NULL,
                              0,
                              L"%u",
                              Data);

        ValueName.MaximumLength = PIP_FIRST_LENGHT_INSTANCE * sizeof(WCHAR);

        Length = ((ULONG_PTR)BufferEnd - (ULONG_PTR)ValueBuffer) / sizeof(WCHAR);

        if (Length == (ULONG)-1)
            ValueName.Length = PIP_FIRST_LENGHT_INSTANCE * sizeof(WCHAR);
        else
            ValueName.Length = Length * sizeof(WCHAR);

        ValueName.Buffer = ValueBuffer;

        ZwSetValueKey(KeyHandle,
                      &ValueName,
                      0,
                      REG_SZ,
                      InstanceBuffer,
                      InstanceLength);

        if (InstanceNewBuffer)
            ExFreePoolWithTag(InstanceNewBuffer, '  pP');

        Data++;
    }

    RtlInitUnicodeString(&ValueName, L"Count");

    ZwSetValueKey(KeyHandle,
                  &ValueName,
                  0,
                  REG_DWORD,
                  &Data,
                  sizeof(ULONG));

    RtlInitUnicodeString(&ValueName, L"NextInstance");

    ZwSetValueKey(KeyHandle,
                  &ValueName,
                  0,
                  REG_DWORD,
                  &Data,
                  sizeof(ULONG));

Exit:

    if (UnicodeString.Buffer)
        RtlFreeUnicodeString(&UnicodeString);

    ZwClose(KeyHandle);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PiForEachDriverQueryRoutine(
    _In_ PWSTR ValueName,
    _In_ ULONG ValueType,
    _In_ PVOID ValueData,
    _In_ ULONG ValueLength,
    _In_ PVOID Context,
    _In_ PVOID EntryContext)
{
    PDEVICE_REGISTRATION_CONTEXT DevContext = Context;
    UNICODE_STRING DestinationString;
    NTSTATUS Status;

    DPRINT("PiForEachDriverQueryRoutine: ValueData - %S\n", ValueData);

    if (ValueType != REG_SZ || ValueLength <= sizeof(WCHAR))
    {
        DPRINT("PiForEachDriverQueryRoutine: ValueType - %X, ValueLength - %X\n",
               ValueType, ValueLength);

        return STATUS_SUCCESS;
    }

    RtlInitUnicodeString(&DestinationString, ValueData);

    Status = PiProcessDriverInstance(DevContext->InstancePath,
                                     &DestinationString,
                                     (PULONG)EntryContext,
                                     DevContext->EnableInstance);
    return Status;
}

NTSTATUS
PpForEachDeviceInstanceDriver(
    _In_ PUNICODE_STRING InstancePath,
    _In_ PVOID Function,
    _In_ PBOOLEAN EnableInstance)
{
    UNICODE_STRING ControlClassName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class");
    UNICODE_STRING EnumKeyName = RTL_CONSTANT_STRING(ENUM_ROOT);
    UNICODE_STRING KeyName;
    RTL_QUERY_REGISTRY_TABLE QueryTable[4];
    DEVICE_REGISTRATION_CONTEXT Context;
    PKEY_VALUE_FULL_INFORMATION KeyInfo;
    HANDLE Handle = NULL;
    HANDLE KeyHandle;
    HANDLE EnumHandle;
    HANDLE InstanceHandle;
    ULONG entryContext[3];
    NTSTATUS status;
    NTSTATUS Status;
    USHORT Lenght;

    PAGED_CODE();

    DPRINT("PpForEachDeviceInstanceDriver: InstancePath - %wZ\n", InstancePath);

    Status = IopOpenRegistryKeyEx(&EnumHandle,
                                  NULL,
                                  &EnumKeyName,
                                  KEY_READ);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("PpForEachDeviceInstanceDriver: Status - %X\n", Status);
        return Status;
    }

    Status = IopOpenRegistryKeyEx(&InstanceHandle,
                                  EnumHandle,
                                  InstancePath,
                                  KEY_READ);
    ZwClose(EnumHandle);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("PpForEachDeviceInstanceDriver: Status - %X\n", Status);
        return Status;
    }

    status = IopGetRegistryValue(InstanceHandle, L"ClassGUID", &KeyInfo);

    if (NT_SUCCESS(status))
    {
        if (KeyInfo->Type == REG_SZ && KeyInfo->DataLength)
        {
            PnpRegSzToString((PWCHAR)((ULONG_PTR)KeyInfo + KeyInfo->DataOffset),
                             KeyInfo->DataLength,
                             &Lenght);

            KeyName.Length = Lenght;
            KeyName.MaximumLength = KeyInfo->DataLength;
            KeyName.Buffer = (PWSTR)((ULONG_PTR)KeyInfo + KeyInfo->DataOffset);

            DPRINT("PpForEachDeviceInstanceDriver: KeyName - %wZ\n", &KeyName);

            status = IopOpenRegistryKeyEx(&KeyHandle,
                                          NULL,
                                          &ControlClassName,
                                          KEY_READ);
            if (NT_SUCCESS(status))
            {
                IopOpenRegistryKeyEx(&Handle,
                                     KeyHandle,
                                     &KeyName,
                                     KEY_READ);
                ZwClose(KeyHandle);
            }
        }

        ExFreePoolWithTag(KeyInfo, 'uspP');
        KeyInfo = 0;
    }

    Context.InstancePath = InstancePath;
    Context.Function = Function;
    Context.EnableInstance = EnableInstance;

    if (Handle)
    {
        RtlZeroMemory(&QueryTable, sizeof(QueryTable));

        entryContext[0] = 0;
        QueryTable[0].EntryContext = &entryContext[0];
        QueryTable[0].Name = L"LowerFilters";
        QueryTable[0].QueryRoutine = PiForEachDriverQueryRoutine;

        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_HANDLE,
                                        Handle,
                                        &QueryTable[0],
                                        &Context,
                                        NULL);
    }

    if (!Handle || NT_SUCCESS(Status))
    {
        RtlZeroMemory(&QueryTable, sizeof(QueryTable));

        entryContext[0] = 1;
        QueryTable[0].EntryContext = &entryContext[0];
        QueryTable[0].Name = L"LowerFilters";
        QueryTable[0].QueryRoutine = PiForEachDriverQueryRoutine;

        entryContext[1] = 2;
        QueryTable[1].EntryContext = &entryContext[1];
        QueryTable[1].Name = L"Service";
        QueryTable[1].QueryRoutine = PiForEachDriverQueryRoutine;

        entryContext[2] = 3;
        QueryTable[2].EntryContext = &entryContext[2];
        QueryTable[2].Name = L"UpperFilters";
        QueryTable[2].QueryRoutine = PiForEachDriverQueryRoutine;

        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_HANDLE,
                                        InstanceHandle,
                                        &QueryTable[0],
                                        &Context,
                                        NULL);
        if (NT_SUCCESS(Status))
        {
            if (!Handle)
            {
                ZwClose(InstanceHandle);
                return Status;
            }

            RtlZeroMemory(&QueryTable, sizeof(QueryTable));

            entryContext[0] = 4;
            QueryTable[0].EntryContext = &entryContext[0];
            QueryTable[0].Name = L"UpperFilters";
            QueryTable[0].QueryRoutine = PiForEachDriverQueryRoutine;

            Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_HANDLE,
                                            Handle,
                                            &QueryTable[0],
                                            &Context,
                                            NULL);
        }
    }

    if (Handle)
        ZwClose(Handle);

    ZwClose(InstanceHandle);

    return Status;
}

NTSTATUS
NTAPI
PiDeviceRegistration(
    _In_ PUNICODE_STRING InstancePath,
    _In_ BOOLEAN IsEnableInstance,
    _In_ PUNICODE_STRING ServiceName)
{
    UNICODE_STRING EnumKeyName = RTL_CONSTANT_STRING(ENUM_ROOT);
    UNICODE_STRING SourceString;
    PKEY_VALUE_FULL_INFORMATION KeyInfo;
    HANDLE EnumHandle;
    HANDLE KeyHandle = NULL;
    USHORT Length = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (ServiceName)
    {
        DPRINT("PiDeviceRegistration: InstancePath - %wZ, IsEnableInstance - %X, ServiceName - %wZ\n",
               InstancePath, IsEnableInstance, ServiceName);
    }
    else
    {
        DPRINT("PiDeviceRegistration: InstancePath - %wZ, IsEnableInstance - %X\n",
               InstancePath, IsEnableInstance);
    }

    if (ServiceName)
        RtlZeroMemory(&ServiceName, sizeof(ServiceName));

    if (InstancePath->Length <= sizeof(WCHAR))
    {
        Status = STATUS_INVALID_PARAMETER;
        DPRINT("PiDeviceRegistration: Status - %X\n", Status);
        goto ErrorExit;
    }

    if (InstancePath->Buffer[InstancePath->Length / sizeof(WCHAR) - 1] == '\\')
        InstancePath->Length = (InstancePath->Length - sizeof(WCHAR));

    Status = IopOpenRegistryKeyEx(&EnumHandle,
                                  NULL,
                                  &EnumKeyName,
                                  KEY_READ);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("PiDeviceRegistration: Status - %X\n", Status);
        goto ErrorExit;
    }

    Status = IopOpenRegistryKeyEx(&KeyHandle,
                                  EnumHandle,
                                  InstancePath,
                                  KEY_READ);
    ZwClose(EnumHandle);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("PiDeviceRegistration: Status - %X\n", Status);
        goto ErrorExit;
    }

    Status = IopGetRegistryValue(KeyHandle, L"Service", &KeyInfo);
    ZwClose(KeyHandle);

    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            DPRINT("PiDeviceRegistration: Service - STATUS_OBJECT_NAME_NOT_FOUND\n");
            return STATUS_SUCCESS;
        }
        DPRINT("PiDeviceRegistration: Status - %X\n", Status);
    }
    else
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;

        if (KeyInfo->Type == REG_SZ && KeyInfo->DataLength > sizeof(WCHAR))
        {
            PnpRegSzToString((PWCHAR)((ULONG_PTR)KeyInfo + KeyInfo->DataOffset),
                             KeyInfo->DataLength,
                             &Length);

            Status = STATUS_SUCCESS;

            SourceString.Length = Length;
            SourceString.MaximumLength = KeyInfo->DataLength;
            SourceString.Buffer = (PWSTR)((ULONG_PTR)KeyInfo + KeyInfo->DataOffset);

            if (ServiceName)
            {
                Status = PnpConcatenateUnicodeStrings(ServiceName,
                                                      &SourceString,
                                                      NULL);

                DPRINT("PiDeviceRegistration: ServiceName %wZ\n", ServiceName);
            }
        }

        ExFreePoolWithTag(KeyInfo, 'uspP');
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT("PiDeviceRegistration: Status - %X\n", Status);
        goto ErrorExit;
    }

    Status = PpForEachDeviceInstanceDriver(InstancePath,
                                           PiProcessDriverInstance,
                                           &IsEnableInstance);
    if (NT_SUCCESS(Status))
        return Status;

    DPRINT("PiDeviceRegistration: Status - %X\n", Status);

    if (IsEnableInstance)
    {
        IsEnableInstance = FALSE;
        PpForEachDeviceInstanceDriver(InstancePath,
                                      PiProcessDriverInstance,
                                      &IsEnableInstance);
    }

ErrorExit:

    if (ServiceName && ServiceName->Length)
    {
        ExFreePool(ServiceName->Buffer);
        RtlZeroMemory(&ServiceName, sizeof(ServiceName));
    }

    return Status;
}

NTSTATUS
NTAPI
PpDeviceRegistration(
    _In_ PUNICODE_STRING InstancePath,
    _In_ BOOLEAN IsEnableInstance,
    _In_ PUNICODE_STRING ServiceName)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (ServiceName)
    {
        DPRINT("PpDeviceRegistration: (%X) '%wZ', '%wZ'\n", IsEnableInstance, InstancePath, ServiceName);
    }
    else
    {
        DPRINT("PpDeviceRegistration: (%X) '%wZ'\n", IsEnableInstance, InstancePath);
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PpRegistryDeviceResource, TRUE);

    Status = PiDeviceRegistration(InstancePath, IsEnableInstance, ServiceName);

    ExReleaseResourceLite(&PpRegistryDeviceResource);
    KeLeaveCriticalRegion();

    return Status;
}

/* EOF */
