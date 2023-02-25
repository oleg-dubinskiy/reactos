/*
 * PROJECT:     Partition manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main file
 * COPYRIGHT:   Copyright 2018, 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "partmgr.h"

//#define NDEBUG
#include <debug.h>

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DriverEntry)
#endif

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, PmAddDevice)
  #pragma alloc_text(PAGE, PmDeviceControl)
  #pragma alloc_text(PAGE, PmPower)
  #pragma alloc_text(PAGE, PmWmi)
  #pragma alloc_text(PAGE, PmkPnp)
  #pragma alloc_text(PAGE, PmDriverReinit)
  #pragma alloc_text(PAGE, PmTableSignatureCompareRoutine)
  #pragma alloc_text(PAGE, PmTableGuidCompareRoutine)
  #pragma alloc_text(PAGE, PmTableAllocateRoutine)
  #pragma alloc_text(PAGE, PmTableFreeRoutine)
  #pragma alloc_text(PAGE, PmVolumeManagerNotification)
  #pragma alloc_text(PAGE, PmQueryDeviceRelations)
  #pragma alloc_text(PAGE, PmDetermineDeviceNameAndNumber)
  #pragma alloc_text(PAGE, PmReadPartitionTableEx)
  #pragma alloc_text(PAGE, LockDriverWithTimeout)
  #pragma alloc_text(PAGE, PmQueryDeviceId)
  #pragma alloc_text(PAGE, PmAddSignatures)
  #pragma alloc_text(PAGE, PmCheckAndUpdateSignature)
  #pragma alloc_text(PAGE, PmRegisterDevice)
  #pragma alloc_text(PAGE, PmStartPartition)
  #pragma alloc_text(PAGE, PmGivePartition)
#endif

/* GLOBALS *******************************************************************/

GUID VOLMGR_VOLUME_MANAGER_GUID = {0x53F5630E, 0xB6BF, 0x11D0, {0X94, 0XF2, 0X00, 0XA0, 0XC9, 0X1E, 0XFB, 0X8B}};

/* FUNCTIONS ****************************************************************/

NTSTATUS
NTAPI
PmSignalCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = Context;

    KeSetEvent(Event, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
PmPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PPM_DEVICE_EXTENSION DeviceExtension;

    DPRINT("PmPassThrough: %p, %p\n", DeviceObject, Irp);

    DeviceExtension = DeviceObject->DeviceExtension;
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->AttachedToDevice, Irp);
}

VOID
NTAPI
PmUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
PmAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DiskPdo)
{
    PPM_DEVICE_EXTENSION Extension;
    PDEVICE_OBJECT PartitionFido;
    PDEVICE_OBJECT TopDevice;
    NTSTATUS Status;

    DPRINT("PmAddDevice: %p, %p\n", DriverObject, DiskPdo);

    TopDevice = IoGetAttachedDeviceReference(DiskPdo);
    if (TopDevice)
    {
        ObDereferenceObject(TopDevice);

        if (TopDevice->Characteristics & FILE_REMOVABLE_MEDIA)
            return STATUS_SUCCESS;
    }

    Status = IoCreateDevice(DriverObject,
                            sizeof(*Extension),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            0,
                            FALSE,
                            &PartitionFido);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PmAddDevice: %p, %p\n", DriverObject, DiskPdo);
        return Status;
    }

    PartitionFido->Flags |= DO_DIRECT_IO;

    if (TopDevice->Flags & DO_POWER_INRUSH)
        PartitionFido->Flags |= DO_POWER_INRUSH;
    else
        PartitionFido->Flags |= DO_POWER_PAGABLE;

    Extension = PartitionFido->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));

    Extension->PartitionFido = PartitionFido;
    Extension->DriverExtension = IoGetDriverObjectExtension(DriverObject, PmAddDevice);

    Extension->AttachedToDevice = IoAttachDeviceToDeviceStack(PartitionFido, DiskPdo);
    if (!Extension->AttachedToDevice)
    {
        DPRINT1("PmAddDevice: AttachedToDevice is NULL! (%p, %p)\n", DriverObject, DiskPdo);
        IoDeleteDevice(PartitionFido);
        return STATUS_NO_SUCH_DEVICE;
    }

    Extension->WholeDiskPdo = DiskPdo;

    KeInitializeEvent(&Extension->Event, SynchronizationEvent, TRUE);

    InitializeListHead(&Extension->PartitionList);
    InitializeListHead(&Extension->ListOfSignatures);
    InitializeListHead(&Extension->ListOfGuids);

    KeWaitForSingleObject(&Extension->DriverExtension->Mutex, Executive, KernelMode, FALSE, NULL);
    InsertTailList(&Extension->DriverExtension->ExtensionList, &Extension->Link);
    KeReleaseMutex(&Extension->DriverExtension->Mutex, FALSE);

    PartitionFido->DeviceType = Extension->AttachedToDevice->DeviceType;
    PartitionFido->AlignmentRequirement = Extension->AttachedToDevice->AlignmentRequirement;

    Extension->NameString.Buffer = Extension->NameBuffer;

    PartitionFido->Flags &= ~DO_DEVICE_INITIALIZING;

    DPRINT("PmAddDevice: STATUS_SUCCESS\n");

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PmVolumeManagerNotification(
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION InterfaceChange = NotificationStructure;
    PPM_DRIVER_EXTENSION DriverExtension = Context;
    PPM_NOTIFICATION_DATA PmNotify;
    PLIST_ENTRY Entry;
    PRKMUTEX Mutex;
    USHORT Size;

    DPRINT("PmVolumeManagerNotification: %p, %p\n", InterfaceChange, DriverExtension);

    Mutex = &DriverExtension->Mutex;
    KeWaitForSingleObject(&DriverExtension->Mutex, Executive, KernelMode, FALSE, NULL);

    if (IsEqualGUID(&InterfaceChange->Event, &GUID_DEVICE_INTERFACE_ARRIVAL))
    {
        for (Entry = DriverExtension->NotifyList.Flink;
             Entry != &DriverExtension->NotifyList;
             Entry = Entry->Flink)
        {
            PmNotify = CONTAINING_RECORD(Entry, PM_NOTIFICATION_DATA, Link);

            DPRINT("PmVolumeManagerNotification: SymbolicLinkName '%wZ'\n", InterfaceChange->SymbolicLinkName);
            DPRINT("PmVolumeManagerNotification: ObjectName '%wZ'\n", &PmNotify->ObjectName);

            if (RtlEqualUnicodeString(InterfaceChange->SymbolicLinkName, &PmNotify->ObjectName, FALSE))
            {
                /* SymbolicLinkName strings are equal */
                goto Exit;
            }
        }

        PmNotify = ExAllocatePoolWithTag(NonPagedPool, sizeof(*PmNotify), 'VRcS');
        if (!PmNotify)
        {
            DPRINT1("PmVolumeManagerNotification: Allocate failed\n");
            goto Exit;
        }

        Size = InterfaceChange->SymbolicLinkName->Length;

        PmNotify->ObjectName.Length = Size;
        PmNotify->ObjectName.MaximumLength = (Size + sizeof(WCHAR));

        PmNotify->ObjectName.Buffer = ExAllocatePoolWithTag(PagedPool, (Size + sizeof(WCHAR)), 'VRcS');
        if (!PmNotify->ObjectName.Buffer)
        {
            DPRINT1("PmVolumeManagerNotification: Allocate failed\n");
            ExFreePoolWithTag(PmNotify, 'VRcS');
            goto Exit;
        }

        RtlCopyMemory(PmNotify->ObjectName.Buffer, InterfaceChange->SymbolicLinkName->Buffer, PmNotify->ObjectName.Length);

        PmNotify->ObjectName.Buffer[PmNotify->ObjectName.Length / sizeof(WCHAR)] = 0;

        PmNotify->Counter = 0;

        PmNotify->DeviceObject = NULL;
        PmNotify->FileObject = NULL;

        InsertTailList(&DriverExtension->NotifyList, &PmNotify->Link);

        for (Entry = DriverExtension->ExtensionList.Flink;
             Entry != &DriverExtension->ExtensionList;
             Entry = Entry->Flink)
        {
            DPRINT1("PmVolumeManagerNotification: FIXME\n");
            ASSERT(FALSE);
        }
    }
    else if (IsEqualGUID(&InterfaceChange->Event, &GUID_DEVICE_INTERFACE_REMOVAL))
    {
        DPRINT1("PmVolumeManagerNotification: FIXME\n");
        ASSERT(FALSE);
    }

Exit:

    KeReleaseMutex(Mutex, FALSE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PmDetermineDeviceNameAndNumber(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG* OutPartitionData)
{
    STORAGE_DEVICE_NUMBER DeviceNumberBuffer;
    PPM_DEVICE_EXTENSION Extension;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;
    ULONG PartitionData = 0;
    ULONG Size;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("PmDetermineDeviceNameAndNumber: %p\n", DeviceObject);

    Extension = DeviceObject->DeviceExtension;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                        Extension->AttachedToDevice,
                                        NULL,
                                        0,
                                        &DeviceNumberBuffer,
                                        sizeof(DeviceNumberBuffer),
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (!Irp)
    {
        DPRINT1("PmDetermineDeviceNameAndNumber: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(Extension->AttachedToDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PmDetermineDeviceNameAndNumber: Status %X\n", Status);
        return Status;
    }

    Extension->DeviceNumber = DeviceNumberBuffer.DeviceNumber;

    Extension->NameString.MaximumLength = sizeof(Extension->NameBuffer);
    Extension->NameString.Buffer = Extension->NameBuffer;

    Size = _snwprintf(Extension->NameBuffer,
                      (sizeof(Extension->NameBuffer) / sizeof(WCHAR)),
                      L"\\Device\\Harddisk%d\\Partition%d",
                      DeviceNumberBuffer.DeviceNumber,
                      DeviceNumberBuffer.PartitionNumber);

    Extension->NameString.Length = (Size * sizeof(WCHAR));

    if (!DeviceNumberBuffer.PartitionNumber)
        PartitionData = 0x110000;

    *OutPartitionData = PartitionData;

    return Status;
}

NTSTATUS
NTAPI
PmReadPartitionTableEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PDRIVE_LAYOUT_INFORMATION_EX* OutDriveLayout)
{
    IO_STATUS_BLOCK IoStatusBlock;
    PVOID IoCtlBuffer;
    KEVENT Event;
    PIRP Irp;
    ULONG IoCtlBufferSize;
    ULONG ix;
    NTSTATUS Status;

    DPRINT("PmReadPartitionTableEx: DeviceObject %p\n", DeviceObject);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCtlBuffer = ExAllocatePoolWithTag(0, PAGE_SIZE, 'iRcS');
    if (!IoCtlBuffer)
    {
        DPRINT1("PmReadPartitionTableEx: Allocate failed\n");
        return IoReadPartitionTableEx(DeviceObject, OutDriveLayout);
    }

    IoCtlBufferSize = PAGE_SIZE;

    for (ix = 0; ix <= 0x20; ix++)
    {
        KeClearEvent(&Event);

        Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                            DeviceObject,
                                            NULL,
                                            0,
                                            IoCtlBuffer,
                                            IoCtlBufferSize,
                                            FALSE,
                                            &Event,
                                            &IoStatusBlock);
        if (!Irp)
        {
            DPRINT1("PmReadPartitionTableEx: STATUS_INSUFFICIENT_RESOURCES\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto ErrorExit;
        }

        Status = IoCallDriver(DeviceObject, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }

        if (NT_SUCCESS(Status))
        {
            ASSERT(IoCtlBuffer && IoCtlBufferSize);
            *OutDriveLayout = IoCtlBuffer;
            return STATUS_SUCCESS;
        }

        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            DPRINT1("PmReadPartitionTableEx: Status %X\n", Status);
            goto ErrorExit;
        }

        ASSERT(IoCtlBuffer && IoCtlBufferSize);
        ExFreePoolWithTag(IoCtlBuffer, 'iRcS');

        IoCtlBufferSize *= 2;

        IoCtlBuffer = ExAllocatePoolWithTag(NonPagedPool, IoCtlBufferSize, 'iRcS');
        if (!IoCtlBuffer)
        {
            DPRINT1("PmReadPartitionTableEx: Allocate failed\n");
            return IoReadPartitionTableEx(DeviceObject, OutDriveLayout);
        }
    }

    Status = STATUS_UNSUCCESSFUL;
    DPRINT1("PmReadPartitionTableEx: STATUS_UNSUCCESSFUL\n");

ErrorExit:

    if (IoCtlBuffer)
    {
        ASSERT(IoCtlBufferSize);
        ExFreePoolWithTag(IoCtlBuffer, 'iRcS');
    }

    return IoReadPartitionTableEx(DeviceObject, OutDriveLayout);
}

BOOLEAN
NTAPI
LockDriverWithTimeout(
    _In_ PPM_DRIVER_EXTENSION DriverExtension)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    Timeout.QuadPart = (-6000 * 10000ull);
    Status = KeWaitForSingleObject(&DriverExtension->Mutex, Executive, KernelMode, FALSE, &Timeout);
    return (Status != STATUS_TIMEOUT);
}

NTSTATUS
NTAPI
PmQueryDeviceId(
    _In_ PPM_DEVICE_EXTENSION Extension,
    _In_ PSTORAGE_PROPERTY_QUERY *OutDeviceId)
{
    STORAGE_PROPERTY_QUERY InputBuffer;
    PSTORAGE_PROPERTY_QUERY DeviceId;
    STORAGE_DESCRIPTOR_HEADER OutputBuffer;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    DPRINT("PmQueryDeviceId: Extension %p\n", Extension);

    *OutDeviceId = NULL;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    InputBuffer.PropertyId = StorageDeviceIdProperty;
    InputBuffer.QueryType = PropertyStandardQuery;

    Irp = IoBuildDeviceIoControlRequest(IOCTL_STORAGE_QUERY_PROPERTY,
                                        Extension->AttachedToDevice,
                                        &InputBuffer,
                                        sizeof(InputBuffer),
                                        &OutputBuffer,
                                        sizeof(OutputBuffer),
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp)
    {
        Status = IoCallDriver(Extension->AttachedToDevice, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PmQueryDeviceId: Status %p\n", Status);
        return Status;
    }

    DeviceId = ExAllocatePoolWithTag(NonPagedPool, OutputBuffer.Size, 'iRcS');
    if (!DeviceId)
    {
        DPRINT1("PmQueryDeviceId: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    InputBuffer.PropertyId = StorageDeviceIdProperty;
    InputBuffer.QueryType = PropertyStandardQuery;

    Irp = IoBuildDeviceIoControlRequest(IOCTL_STORAGE_QUERY_PROPERTY,
                                        Extension->AttachedToDevice,
                                        &InputBuffer,
                                        sizeof(InputBuffer),
                                        DeviceId,
                                        OutputBuffer.Size,
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp)
    {
        Status = IoCallDriver(Extension->AttachedToDevice, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PmQueryDeviceId: Status %p\n", Status);
        ExFreePoolWithTag(DeviceId, 'iRcS');
        return Status;
    }

    *OutDeviceId = DeviceId;

    return Status;
}

VOID
NTAPI
PmAddSignatures(
    _In_ PPM_DEVICE_EXTENSION Extension,
    _In_ PDRIVE_LAYOUT_INFORMATION_EX DriveLayout)
{
    PPM_DRIVER_EXTENSION DriverExtension;
    TABLE_SEARCH_RESULT ResultSignatures;
    PSTORAGE_PROPERTY_QUERY DeviceId;
    PPM_SIGNATURE RetSignature;
    PM_SIGNATURE Signature;
    PVOID SignatureNode;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    DPRINT("PmAddSignatures: Extension %p, DriveLayout %p\n", Extension, DriveLayout);

    DriverExtension = Extension->DriverExtension;

    while (!IsListEmpty(&Extension->ListOfSignatures))
    {
        Entry = RemoveHeadList(&Extension->ListOfSignatures);
        RetSignature = CONTAINING_RECORD(Entry, PM_SIGNATURE, Link);
        RtlDeleteElementGenericTableAvl(&DriverExtension->TableSignature, RetSignature);
    }

    while (!IsListEmpty(&Extension->ListOfGuids))
    {
        DPRINT1("PmAddSignatures: FIXME\n");
        ASSERT(FALSE);
    }

    if (!DriveLayout)
        return;

    if (Extension->Reserved00)
        return;

    if (DriveLayout->PartitionStyle == 0) // PARTITION_STYLE_MBR
    {
        if (!DriveLayout->PartitionCount && !DriveLayout->Mbr.Signature)
            return;

        if (DriveLayout->PartitionCount &&
            DriveLayout->PartitionEntry[0].PartitionLength.QuadPart > 0 &&
            DriveLayout->PartitionEntry[0].StartingOffset.QuadPart == 0)
        {
            return;
        }

        if (!Extension->IsDeviceIdRequested)
        {
            DeviceId = NULL;

            Status = PmQueryDeviceId(Extension, &DeviceId);
            if (NT_SUCCESS(Status))
            {
                DPRINT1("PmAddSignatures: FIXME\n");
                ASSERT(FALSE);
            }

            Extension->IsDeviceIdRequested = TRUE;
        }

        Signature.Value = DriveLayout->Mbr.Signature;

        RetSignature = RtlLookupElementGenericTableFullAvl(&DriverExtension->TableSignature,
                                                           &Signature,
                                                           &SignatureNode,
                                                           &ResultSignatures);
        if (!RetSignature && Signature.Value)
        {
            goto FinishMbrStyle;
        }

        DPRINT1("PmAddSignatures: FIXME\n");
        ASSERT(FALSE);

FinishMbrStyle:

        RetSignature = RtlInsertElementGenericTableFullAvl(&DriverExtension->TableSignature,
                                                           &Signature,
                                                           sizeof(PM_SIGNATURE),
                                                           0,
                                                           SignatureNode,
                                                           ResultSignatures);
        if (RetSignature)
        {
            InsertTailList(&Extension->ListOfSignatures, &RetSignature->Link);
            RetSignature->DeviceExtension = Extension;
        }

        return;
    }

    if (DriveLayout->PartitionStyle != PARTITION_STYLE_GPT)
    {
        DPRINT1("PmAddSignatures: ? PARTITION_STYLE ? (%p, %p, %X)\n", Extension, DriveLayout, DriveLayout->PartitionStyle);
        ASSERT("Layout->PartitionStyle == PARTITION_STYLE_GPT");
        return;
    }

    DPRINT1("PmAddSignatures: PARTITION_STYLE_GPT. FIXME\n");
    ASSERT(FALSE);

}

NTSTATUS
NTAPI
PmCheckAndUpdateSignature(
    _In_ PPM_DEVICE_EXTENSION Extension,
    _In_ BOOLEAN Param2,
    _In_ BOOLEAN Param3)
{
    PDRIVE_LAYOUT_INFORMATION_EX DriveLayout;
    BOOLEAN IsFailed;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("PmCheckAndUpdateSignature: Extension %p, Param2 %X, Param3 %X\n", Extension, Param2, Param3);

    if (!Param3 && !Extension->IsPartitionNotFound)
        return Status;

    Status = PmReadPartitionTableEx(Extension->AttachedToDevice, &DriveLayout);

    if (!Param3 && !Extension->IsPartitionNotFound)
    {
        if (NT_SUCCESS(Status))
            ExFreePool(DriveLayout);

        return STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(Status) && Extension->IsPartitionNotFound)
    {
        return Status;
    }

    if (!LockDriverWithTimeout(Extension->DriverExtension))
    {
        if (NT_SUCCESS(Status))
            ExFreePool(DriveLayout);

        return Status;
    }

    IsFailed = (NT_SUCCESS(Status) == FALSE);

    if (NT_SUCCESS(Status))
    {
        if (!Extension->Reserved00)
        {
            DPRINT("PmCheckAndUpdateSignature: FIXME PmSigCheckUpdateEpoch()\n");

            PmAddSignatures(Extension, DriveLayout);
            ExFreePool(DriveLayout);
        }

        //IsFailed = (NT_SUCCESS(Status) == FALSE);
    }

    Extension->IsPartitionNotFound = IsFailed;

    KeReleaseMutex(&Extension->DriverExtension->Mutex, FALSE);

    DPRINT("PmCheckAndUpdateSignature: FIXME PmSigCheckCompleteNotificationIrps()\n");

    return Status;
}

NTSTATUS
NTAPI
PmRegisterDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG PartitionData)
{
    PPM_DEVICE_EXTENSION Extension;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    DPRINT("PmRegisterDevice: DeviceObject %p, PartitionData %X\n", DeviceObject, PartitionData);

    Extension = DeviceObject->DeviceExtension;

    if (Extension->NameString.Length)
    {
        Status = IoWMIRegistrationControl(DeviceObject, (PartitionData | 1));
        if (NT_SUCCESS(Status))
        {
            DPRINT1("PmRegisterDevice: FIXME PmWmiCounter...\n");
        }
    }

    return Status;
}

NTSTATUS
NTAPI
PmStartPartition(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PmGivePartition(
    _In_ PPM_NOTIFICATION_DATA NotifyData,
    _In_ PDEVICE_OBJECT PartitionPdo,
    _In_ PDEVICE_OBJECT WholeDiskPdo)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PmQueryDeviceRelations(
    _In_ PPM_DEVICE_EXTENSION Extension,
    _In_ PIRP Irp)
{
    PPM_PARTITION_DATA PartitionData;
    PPM_NOTIFICATION_DATA NotifyData;
    PDEVICE_RELATIONS DeviceRelation;
    PLIST_ENTRY Entry;
    KEVENT Event;
    ULONG ix;
    NTSTATUS Status;

    DPRINT("PmQueryDeviceRelations: Extension %p, Irp %p\n", Extension, Irp);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, PmSignalCompletion, &Event, TRUE, TRUE, TRUE);

    IoCallDriver(Extension->AttachedToDevice, Irp);
    KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    Status = Irp->IoStatus.Status;
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PmQueryDeviceRelations: Status %X\n", Status);
        return Status;
    }

    DeviceRelation = (PDEVICE_RELATIONS)Irp->IoStatus.Information;

    PmCheckAndUpdateSignature(Extension, TRUE, TRUE);

    KeWaitForSingleObject(&Extension->DriverExtension->Mutex, Executive, KernelMode, FALSE, NULL);

    for (Entry = Extension->PartitionList.Flink;
         Entry != &Extension->PartitionList;
         Entry = Entry->Flink)
    {
        PartitionData = CONTAINING_RECORD(Entry, PM_PARTITION_DATA, Link);

        for (ix = 0; ix < DeviceRelation->Count; ix++)
        {
            if (PartitionData->PartitionPdo == DeviceRelation->Objects[ix])
                break;
        }

        if (ix < DeviceRelation->Count)
            continue;

        DPRINT1("PmQueryDeviceRelations: FIXME\n");
        ASSERT(FALSE);

    }

    DPRINT("PmQueryDeviceRelations: DeviceRelation->Count %X\n", DeviceRelation->Count);

    for (ix = 0; ix < DeviceRelation->Count; ix++)
    {
        for (Entry = Extension->PartitionList.Flink;
             Entry != &Extension->PartitionList;
             Entry = Entry->Flink)
        {
            PartitionData = CONTAINING_RECORD(Entry, PM_PARTITION_DATA, Link);

            if (DeviceRelation->Objects[ix] == PartitionData->PartitionPdo)
                break;
        }

        if (Entry != &Extension->PartitionList)
        {
            ObDereferenceObject(DeviceRelation->Objects[ix]);
            PmStartPartition(DeviceRelation->Objects[ix]);
            continue;
        }

        if (Extension->DriverExtension->IsReinitialized)
            DeviceRelation->Objects[ix]->Flags |= DO_DEVICE_INITIALIZING;

        Status = PmStartPartition(DeviceRelation->Objects[ix]);
        if (!NT_SUCCESS(Status))
        {
            continue;
        }

        PartitionData = ExAllocatePoolWithTag(NonPagedPool, sizeof(*PartitionData), 'pRcS');
        if (!PartitionData)
        {
            continue;
        }

        PartitionData->PartitionPdo = DeviceRelation->Objects[ix];
        PartitionData->WholeDiskPdo = Extension->WholeDiskPdo;
        PartitionData->NotifyData = NULL;

        InsertHeadList(&Extension->PartitionList, &PartitionData->Link);

        if (Extension->Reserved02)
            continue;

        for (Entry = Extension->DriverExtension->NotifyList.Flink;
             Entry != &Extension->DriverExtension->NotifyList;
             Entry = Entry->Flink)
        {
            NotifyData = CONTAINING_RECORD(Entry, PM_NOTIFICATION_DATA, Link);

            Status = PmGivePartition(NotifyData, PartitionData->PartitionPdo, PartitionData->WholeDiskPdo);
            if (NT_SUCCESS(Status))
            {
                PartitionData->NotifyData = NotifyData;
                break;
            }
        }
    }

    KeReleaseMutex(&Extension->DriverExtension->Mutex, FALSE);

    DeviceRelation->Count = 0;
    return Irp->IoStatus.Status;
}

/* AVL TABLE ROUTINES *******************************************************/

RTL_GENERIC_COMPARE_RESULTS
NTAPI
PmTableSignatureCompareRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct)
{
    PPM_SIGNATURE First = FirstStruct;
    PPM_SIGNATURE Second = SecondStruct;
    ULONG FirstSignature;
    ULONG SecondSignature;
    RTL_GENERIC_COMPARE_RESULTS Result;

    FirstSignature = First->Value;
    SecondSignature = Second->Value;

    DPRINT("PmTableSignatureCompareRoutine: %X, %X\n", FirstSignature, SecondSignature);

    if (FirstSignature < SecondSignature)
    {
        Result = GenericLessThan;
    }
    else if (FirstSignature > SecondSignature)
    {
        Result = GenericGreaterThan;
    }
    else
    {
        Result = GenericEqual;
    }

    return Result;
}

RTL_GENERIC_COMPARE_RESULTS
NTAPI
PmTableGuidCompareRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

PVOID
NTAPI
PmTableAllocateRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ CLONG ByteSize)
{
    return ExAllocatePoolWithTag(PagedPool, ByteSize, 'tRcS');
}

VOID
NTAPI
PmTableFreeRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID Buffer)
{
    ExFreePoolWithTag(Buffer, 'tRcS');
}

/* DRIVER DISPATCH ROUTINES *************************************************/

NTSTATUS
NTAPI
PmReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PmDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PPM_DEVICE_EXTENSION Extension;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    Extension = DeviceObject->DeviceExtension;
    IoStack = Irp->Tail.Overlay.CurrentStackLocation;

    DPRINT("PmDeviceControl: DeviceObject %p, Irp %p, Code %X\n", DeviceObject, Irp, IoStack->Parameters.DeviceIoControl.IoControlCode);

    if (Extension->AttachedToDevice->Characteristics & FILE_REMOVABLE_MEDIA)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->AttachedToDevice, Irp);
    }

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_DISK_GET_DRIVE_GEOMETRY:
            DPRINT1("PmDeviceControl: FIXME\n");
            ASSERT(FALSE);Status=STATUS_NOT_IMPLEMENTED;
            break;

        case IOCTL_DISK_PERFORMANCE:
            DPRINT1("PmDeviceControl: FIXME\n");
            ASSERT(FALSE);Status=STATUS_NOT_IMPLEMENTED;
            break;

        case IOCTL_DISK_GET_DRIVE_LAYOUT_EX:
        case IOCTL_DISK_GET_DRIVE_LAYOUT:
        case 0x704008:
            DPRINT1("PmDeviceControl: FIXME\n");
            ASSERT(FALSE);Status=STATUS_NOT_IMPLEMENTED;
            break;

        case IOCTL_DISK_PERFORMANCE_OFF:
            DPRINT1("PmDeviceControl: FIXME\n");
            ASSERT(FALSE);Status=STATUS_NOT_IMPLEMENTED;
            break;

        case IOCTL_DISK_UPDATE_PROPERTIES:
        case IOCTL_DISK_SET_DRIVE_LAYOUT:
        case IOCTL_DISK_CREATE_DISK:
        case IOCTL_DISK_DELETE_DRIVE_LAYOUT:
        case IOCTL_DISK_SET_DRIVE_LAYOUT_EX:
            DPRINT1("PmDeviceControl: FIXME\n");
            ASSERT(FALSE);Status=STATUS_NOT_IMPLEMENTED;
            break;

        case IOCTL_DISK_GROW_PARTITION:
            DPRINT1("PmDeviceControl: FIXME\n");
            ASSERT(FALSE);Status=STATUS_NOT_IMPLEMENTED;
            break;

        case 0x70400C:
            DPRINT1("PmDeviceControl: FIXME\n");
            ASSERT(FALSE);Status=STATUS_NOT_IMPLEMENTED;
            break;

        case 0x70C000:
            DPRINT1("PmDeviceControl: FIXME\n");
            ASSERT(FALSE);Status=STATUS_NOT_IMPLEMENTED;
            break;

        case 0x70C004:
            DPRINT1("PmDeviceControl: FIXME\n");
            ASSERT(FALSE);Status=STATUS_NOT_IMPLEMENTED;
            break;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(Extension->AttachedToDevice, Irp);
    }

    DPRINT("PmDeviceControl: Code %X, Status %X\n", IoStack->Parameters.DeviceIoControl.IoControlCode, Status);

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, 0);

    return Status;
}

NTSTATUS
NTAPI
PmPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PmWmi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PmPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PPM_DEVICE_EXTENSION Extension;
    PIO_STACK_LOCATION IoStack;
    ULONG PartitionData;
    KEVENT Event;
    BOOLEAN IsRemoveInPath = FALSE;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    DPRINT("PmPnp: DeviceObject %p, Irp %p\n", DeviceObject, Irp);

    Extension = DeviceObject->DeviceExtension;
    IoStack = Irp->Tail.Overlay.CurrentStackLocation;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

  #if DBG
    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            DPRINT("IRP_MN_START_DEVICE\n");
            break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
            DPRINT("IRP_MN_QUERY_REMOVE_DEVICE\n");
            break;

        case IRP_MN_REMOVE_DEVICE:
            DPRINT("USBPORT_FdoPnP: IRP_MN_REMOVE_DEVICE\n");
            break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
            DPRINT("IRP_MN_CANCEL_REMOVE_DEVICE\n");
            break;

        case IRP_MN_STOP_DEVICE:
            DPRINT("IRP_MN_STOP_DEVICE\n");
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
            DPRINT("IRP_MN_QUERY_STOP_DEVICE\n");
            break;

        case IRP_MN_CANCEL_STOP_DEVICE:
            DPRINT("IRP_MN_CANCEL_STOP_DEVICE\n");
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            DPRINT("IRP_MN_QUERY_DEVICE_RELATIONS\n");
            break;

        case IRP_MN_QUERY_INTERFACE:
            DPRINT("IRP_MN_QUERY_INTERFACE\n");
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            DPRINT("IRP_MN_QUERY_CAPABILITIES\n");
            break;

        case IRP_MN_QUERY_RESOURCES:
            DPRINT("IRP_MN_QUERY_RESOURCES\n");
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_QUERY_RESOURCE_REQUIREMENTS\n");
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            DPRINT("IRP_MN_QUERY_DEVICE_TEXT\n");
            break;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
            break;

        case IRP_MN_READ_CONFIG:
            DPRINT("IRP_MN_READ_CONFIG\n");
            break;

        case IRP_MN_WRITE_CONFIG:
            DPRINT("IRP_MN_WRITE_CONFIG\n");
            break;

        case IRP_MN_EJECT:
            DPRINT("IRP_MN_EJECT\n");
            break;

        case IRP_MN_SET_LOCK:
            DPRINT("IRP_MN_SET_LOCK\n");
            break;

        case IRP_MN_QUERY_ID:
            DPRINT("IRP_MN_QUERY_ID\n");
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            DPRINT("IRP_MN_QUERY_PNP_DEVICE_STATE\n");
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
            DPRINT("IRP_MN_QUERY_BUS_INFORMATION\n");
            break;

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            DPRINT("IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            DPRINT1("IRP_MN_SURPRISE_REMOVAL\n");
            break;

        case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
            DPRINT("IRP_MN_QUERY_LEGACY_BUS_INFORMATION\n");
            break;

        default:
            DPRINT1("FtpPnpFdo: Unknown PNP IRP_MN_ (%X)\n", IoStack->MinorFunction);
            break;
    }
  #endif

    if (IoStack->MinorFunction == IRP_MN_DEVICE_USAGE_NOTIFICATION &&
        IoStack->Parameters.UsageNotification.Type == DeviceUsageTypePaging)
    {
        KeWaitForSingleObject(&Extension->Event, Executive, KernelMode, FALSE, NULL);

        if (!IoStack->Parameters.UsageNotification.InPath && !Extension->PagingPathCount)
        {
            if (!(DeviceObject->Flags & DO_POWER_INRUSH))
            {
                DeviceObject->Flags |= DO_POWER_PAGABLE;
                IsRemoveInPath = TRUE;
            }
        }

        KeInitializeEvent(&Event, NotificationEvent, FALSE);

        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp, PmSignalCompletion, &Event, TRUE, TRUE, TRUE);

        Status = IoCallDriver(Extension->AttachedToDevice, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = Irp->IoStatus.Status;
        }

        if (!NT_SUCCESS(Status))
        {
            if (IsRemoveInPath)
            {
                DeviceObject->Flags &= ~DO_POWER_PAGABLE;
            }
        }
        else
        {
            if (IoStack->Parameters.UsageNotification.InPath)
            {
                InterlockedIncrement(&Extension->PagingPathCount);

                if (Extension->PagingPathCount == 1)
                    DeviceObject->Flags &= ~DO_POWER_PAGABLE;
            }
            else
            {
                InterlockedDecrement(&Extension->PagingPathCount);
            }
        }

        KeSetEvent(&Extension->Event, 0, FALSE);
        IoCompleteRequest(Irp, 0);
        return Status;
    }

    if (Extension->AttachedToDevice->Characteristics & FILE_REMOVABLE_MEDIA)
    {
        if (IoStack->MinorFunction == IRP_MN_REMOVE_DEVICE)
        {
            DPRINT1("PmPnp: FIXME\n");
            ASSERT(FALSE);
        }

        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->AttachedToDevice, Irp);
    }

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            KeInitializeEvent(&Event, NotificationEvent, FALSE);

            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, PmSignalCompletion, &Event, TRUE, TRUE, TRUE);

            IoCallDriver(Extension->AttachedToDevice, Irp);

            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

            Status = Irp->IoStatus.Status;
            if (NT_SUCCESS(Status) && !(Extension->AttachedToDevice->Characteristics & FILE_REMOVABLE_MEDIA))
            {
                PmDetermineDeviceNameAndNumber(DeviceObject, &PartitionData);

                KeWaitForSingleObject(&Extension->DriverExtension->Mutex, Executive, KernelMode, FALSE, NULL);
                Extension->IsDeviceRunning = TRUE;
                KeReleaseMutex(&Extension->DriverExtension->Mutex, FALSE);

                PmCheckAndUpdateSignature(Extension, TRUE, TRUE);
                PmRegisterDevice(DeviceObject, PartitionData);
            }

            break;
        }
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        {
            DPRINT1("PmPnp: FIXME\n");
            ASSERT(FALSE);
            break;
        }
        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        {
            DPRINT1("PmPnp: FIXME\n");
            ASSERT(FALSE);

            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(Extension->AttachedToDevice, Irp);
        }
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            if (IoStack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                Status = PmQueryDeviceRelations(Extension, Irp);
            }
            else if (IoStack->Parameters.QueryDeviceRelations.Type == RemovalRelations)
            {
                DPRINT1("PmPnp: FIXME\n");
                ASSERT(FALSE);
            }
            else
            {
                IoSkipCurrentIrpStackLocation(Irp);
                return IoCallDriver(Extension->AttachedToDevice, Irp);
            }

            break;
        }
        default:
        {
            DPRINT1("FtpPnpFdo: Unknown PNP IRP_MN_ (%X)\n", IoStack->MinorFunction);
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(Extension->AttachedToDevice, Irp);
        }
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, 0);

    DPRINT1("PmPnp: Status (%X)\n", Status);
    return Status;
}

/* REINITIALIZE DRIVER ROUTINES *********************************************/

VOID
NTAPI
PmDriverReinit(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
PmBootDriverReinit(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* INIT DRIVER ROUTINES *****************************************************/

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    PPM_DRIVER_EXTENSION DriverExtension;
    ULONG size;
    ULONG ix;
    NTSTATUS Status;

    DPRINT("DriverEntry: %p, '%wZ'\n", DriverObject, RegistryPath);

    for (ix = 0; ix <= IRP_MJ_MAXIMUM_FUNCTION; ix++)
        DriverObject->MajorFunction[ix] = PmPassThrough;

    DriverObject->MajorFunction[IRP_MJ_READ] = PmReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = PmReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PmDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PmPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = PmWmi;
    DriverObject->MajorFunction[IRP_MJ_PNP] = PmPnp;

    DriverObject->DriverExtension->AddDevice = PmAddDevice;
    DriverObject->DriverUnload = PmUnload;

    Status = IoAllocateDriverObjectExtension(DriverObject, PmAddDevice, sizeof(*DriverExtension), (PVOID *)&DriverExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        return Status;
    }

    size = (RegistryPath->Length + 2);

    DriverExtension->RegistryPath.MaximumLength = size;
    DriverExtension->RegistryPath.Buffer = ExAllocatePoolWithTag(PagedPool, size, 'pRcS');

    if (DriverExtension->RegistryPath.Buffer)
    {
        RtlCopyUnicodeString(&DriverExtension->RegistryPath, RegistryPath);
    }
    else
    {
        DriverExtension->RegistryPath.Length = 0;
        DriverExtension->RegistryPath.MaximumLength = 0;
    }

    DriverExtension->SelfDriverObject = DriverObject;

    InitializeListHead(&DriverExtension->NotifyList);
    InitializeListHead(&DriverExtension->ExtensionList);
    KeInitializeMutex(&DriverExtension->Mutex, 0);

    DriverExtension->IsReinitialized = 0;

    RtlInitializeGenericTableAvl(&DriverExtension->TableSignature,
                                 PmTableSignatureCompareRoutine,
                                 PmTableAllocateRoutine,
                                 PmTableFreeRoutine,
                                 DriverExtension);

    RtlInitializeGenericTableAvl(&DriverExtension->TableGuid,
                                 PmTableGuidCompareRoutine,
                                 PmTableAllocateRoutine,
                                 PmTableFreeRoutine,
                                 DriverExtension);

    IoRegisterBootDriverReinitialization(DriverObject, PmBootDriverReinit, DriverExtension);

    Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                            &VOLMGR_VOLUME_MANAGER_GUID,
                                            DriverObject,
                                            PmVolumeManagerNotification,
                                            DriverExtension,
                                            &DriverExtension->NotificationEntry);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        return Status;
    }

#if 0
    DriverExtension->RegistrySignature = PmQueryRegistrySignature();

    PmQueryRegistryGuid(DriverExtension);
    PmQueryRegistrySnapshotSettings(DriverExtension);

    PmQueryRegistryEpochMode(DriverExtension);
#endif

    DPRINT("DriverEntry: exit with STATUS_SUCCESS\n");
    return STATUS_SUCCESS;
}

/* EOF */
