/*
 * PROJECT:     Volume manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main file
 * COPYRIGHT:   Copyright 2018, 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "ftdisk.h"

//#define NDEBUG
#include <debug.h>

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DriverEntry)
  #pragma alloc_text(INIT, FtDiskAddDevice)
#endif

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, FtDiskUnload)
  #pragma alloc_text(PAGE, FtDiskDeviceControl)
  #pragma alloc_text(PAGE, FtWmi)
  #pragma alloc_text(PAGE, FtDiskPnp)
  #pragma alloc_text(PAGE, IoRegisterBootDriverReinitialization)
  #pragma alloc_text(PAGE, IoRegisterDriverReinitialization)
  #pragma alloc_text(PAGE, FtpQueryRootId)
  #pragma alloc_text(PAGE, FtpPartitionArrived)
  #pragma alloc_text(PAGE, FtpPartitionArrivedHelper)
  #pragma alloc_text(PAGE, FtpQueryPartitionInformation)
  #pragma alloc_text(PAGE, FtpCreateNewDevice)
  #pragma alloc_text(PAGE, FtpQueryDiskSignature)
  #pragma alloc_text(PAGE, FtpQueryDiskSignatureCache)
  #pragma alloc_text(PAGE, FtpCreateOldNameLinks)
  #pragma alloc_text(PAGE, FtpQueryId)
#endif

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGELK, FtpReadPartitionTableEx)
#endif

/* GLOBALS *******************************************************************/

GUID VOLMGR_VOLUME_MANAGER_GUID = {0x53F5630E, 0xB6BF, 0x11D0, {0X94, 0XF2, 0X00, 0XA0, 0XC9, 0X1E, 0XFB, 0X8B}};

/* FUNCTIONS ****************************************************************/

NTSTATUS
NTAPI
FtpSignalCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = Context;

    KeSetEvent(Event, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
NTAPI
FtpAcquire(
    _In_ PROOT_EXTENSION RootExtension)
{
    KeWaitForSingleObject(&RootExtension->RootSemaphore, Executive, KernelMode, FALSE, NULL);
}

VOID
NTAPI
FtpRelease(
    _In_ PROOT_EXTENSION RootExtension)
{
    KeReleaseSemaphore(&RootExtension->RootSemaphore, 0, 1, FALSE);
}

VOID
NTAPI
FtDiskUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
FtpCancelRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    IoReleaseCancelSpinLock(Irp->CancelIrql);

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_CANCELLED;

    IoCompleteRequest(Irp, 0);
}

ULONG
NTAPI
FtpQueryDiskSignature(
    _In_ PDEVICE_OBJECT WholeDiskPdo)
{
    PDEVICE_OBJECT TopDeviceObject;
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG DiskSignature;

    DPRINT("FtpQueryDiskSignature: WholeDiskPdo %p\n", WholeDiskPdo);

    TopDeviceObject = IoGetAttachedDeviceReference(WholeDiskPdo);
    if (!TopDeviceObject)
    {
        ASSERT(FALSE);
        return 0;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(0x704008,
                                        TopDeviceObject,
                                        NULL,
                                        0,
                                        &DiskSignature,
                                        sizeof(DiskSignature),
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (!Irp)
    {
        DPRINT1("FtpQueryDiskSignature: Build irp failed\n");
        ObDereferenceObject(TopDeviceObject);
        return 0;
    }

    Status = IoCallDriver(TopDeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FtpQueryDiskSignature: Status %X\n", Status);
        ObDereferenceObject(TopDeviceObject);
        return 0;
    }

    ObDereferenceObject(TopDeviceObject);

    DPRINT("FtpQueryDiskSignature: DiskSignature %X\n", DiskSignature);

    return DiskSignature;
}

ULONG
NTAPI
FtpQueryDiskSignatureCache(
    _In_ PVOLUME_EXTENSION VolumeExtension)
{
    if (!VolumeExtension->DiskSignature)
    {
        VolumeExtension->DiskSignature = FtpQueryDiskSignature(VolumeExtension->WholeDiskPdo);
    }

    return VolumeExtension->DiskSignature;
}

VOID
NTAPI
FtpCreateOldNameLinks(
    _In_ PVOLUME_EXTENSION VolumeExtension)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
FtpQueryPartitionInformation(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PDEVICE_OBJECT PartitionPdo,
    _Out_ ULONG* OutDeviceNumber,
    _Out_ ULONGLONG* OutStartingOffset,
    _Out_ ULONG* OutPartitionNumber,
    _Out_ UCHAR* OutMbrPartitionType,
    _Out_ LONGLONG* OutPartitionLength,
    _Out_ GUID* OutGptPartitionType,
    _Out_ GUID* OutGptPartitionId,
    _Out_ UCHAR* OutIsGptPartition,
    _Out_ ULONGLONG* OutGptAttributes)
{
    PARTITION_INFORMATION_EX PartitionInfoEx;
    STORAGE_DEVICE_NUMBER OutputBuffer;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    DPRINT("FtpQueryPartitionInformation: %p, %p\n", RootExtension, PartitionPdo);

    if (OutDeviceNumber || OutPartitionNumber)
    {
        KeInitializeEvent(&Event, NotificationEvent, FALSE);

        Irp = IoBuildDeviceIoControlRequest(IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                            PartitionPdo,
                                            NULL,
                                            0,
                                            &OutputBuffer,
                                            sizeof(OutputBuffer),
                                            FALSE,
                                            &Event,
                                            &IoStatusBlock);
        if (!Irp)
        {
            DPRINT1("FtpQueryPartitionInformation: Build irp failed\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = IoCallDriver(PartitionPdo, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("FtpQueryPartitionInformation: Status %X\n", Status);
            return Status;
        }

        if (OutDeviceNumber)
            *OutDeviceNumber = OutputBuffer.DeviceNumber;

        if (OutPartitionNumber)
            *OutPartitionNumber = OutputBuffer.PartitionNumber;
    }

    if (!OutStartingOffset &&
        !OutMbrPartitionType &&
        !OutPartitionLength &&
        !OutGptPartitionType &&
        !OutGptPartitionId &&
        !OutIsGptPartition &&
        !OutGptAttributes)
    {
        return STATUS_SUCCESS;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_PARTITION_INFO_EX,
                                        PartitionPdo,
                                        NULL,
                                        0,
                                        &PartitionInfoEx,
                                        sizeof(PartitionInfoEx),
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (!Irp)
    {
        DPRINT1("FtpQueryPartitionInformation: Build irp failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(PartitionPdo, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FtpQueryPartitionInformation: Status %X\n", Status);
        return Status;
    }

    if (OutStartingOffset)
        *OutStartingOffset = PartitionInfoEx.StartingOffset.QuadPart;

    if (OutMbrPartitionType)
        *OutMbrPartitionType = ((PartitionInfoEx.PartitionStyle != 0) ? 0 : PartitionInfoEx.Mbr.PartitionType);

    if (OutPartitionLength)
        *OutPartitionLength = PartitionInfoEx.PartitionLength.QuadPart;

    if (OutGptPartitionType && PartitionInfoEx.PartitionStyle == 1)
        *OutGptPartitionType = PartitionInfoEx.Gpt.PartitionType;

    if (OutGptPartitionId && PartitionInfoEx.PartitionStyle == 1)
        *OutGptPartitionId = PartitionInfoEx.Gpt.PartitionId;

    if (OutIsGptPartition)
        *OutIsGptPartition = (PartitionInfoEx.PartitionStyle == 1);

    if (!OutGptAttributes)
        return STATUS_SUCCESS;

    if (PartitionInfoEx.PartitionStyle == 1)
        *OutGptAttributes = PartitionInfoEx.Gpt.Attributes;
    else
        *OutGptAttributes = 0;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FtpReadPartitionTableEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDRIVE_LAYOUT_INFORMATION_EX* OutDriveLayout)
{
    IO_STATUS_BLOCK IoStatusBlock;
    PVOID DriveLayoutBuffer;
    KEVENT Event;
    PIRP Irp;
    SIZE_T IoCtlBufferSize;
    SIZE_T NumberOfBytes;
    ULONG ix = 0;
    NTSTATUS Status;

    DPRINT("FtpReadPartitionTableEx: %p\n", DeviceObject);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    DriveLayoutBuffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, 'iFcS');
    if (!DriveLayoutBuffer)
    {
        ASSERT(FALSE);
        return IoReadPartitionTableEx(DeviceObject, OutDriveLayout);
    }

    IoCtlBufferSize = PAGE_SIZE;
    KeClearEvent(&Event);

    for (Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                              DeviceObject,
                                              NULL,
                                              0,
                                              DriveLayoutBuffer,
                                              PAGE_SIZE,
                                              0,
                                              &Event,
                                              &IoStatusBlock);
          ;
          Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                              DeviceObject,
                                              NULL,
                                              0,
                                              DriveLayoutBuffer,
                                              NumberOfBytes,
                                              0,
                                              &Event,
                                              &IoStatusBlock))
    {
        if (!Irp)
        {
            DPRINT1("FtpReadPartitionTableEx: Ipr not created!\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = IoCallDriver(DeviceObject, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }

        if (NT_SUCCESS(Status))
        {
            ASSERT(DriveLayoutBuffer && IoCtlBufferSize);
            *OutDriveLayout = DriveLayoutBuffer;
            return STATUS_SUCCESS;
        }

        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            DPRINT1("FtpReadPartitionTableEx: Status %X\n", Status);
            break;
        }

        ASSERT(DriveLayoutBuffer && IoCtlBufferSize);
        ExFreePoolWithTag(DriveLayoutBuffer, 'iFcS');

        NumberOfBytes = (2 * IoCtlBufferSize);
        ASSERT(NumberOfBytes != 0);

        DriveLayoutBuffer = ExAllocatePoolWithTag(NonPagedPool, NumberOfBytes, 'iFcS');
        if (!DriveLayoutBuffer)
        {
            DPRINT1("FtpReadPartitionTableEx: DriveLayoutBuffer not created!\n");
            return IoReadPartitionTableEx(DeviceObject, OutDriveLayout);
        }

        IoCtlBufferSize = NumberOfBytes;

        ix++;
        if (ix > 0x20)
        {
            DPRINT1("FtpReadPartitionTableEx: ix %X\n", ix);
            Status = STATUS_UNSUCCESSFUL;
            break;
        }

        KeClearEvent(&Event);
    }

    if (DriveLayoutBuffer)
    {
        ASSERT(IoCtlBufferSize);
        ExFreePoolWithTag(DriveLayoutBuffer, 'iFcS');
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FtpReadPartitionTableEx: Status %X\n", Status);
        return IoReadPartitionTableEx(DeviceObject, OutDriveLayout);
    }

    return Status;
}

BOOLEAN
NTAPI
FtpCreateNewDevice(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PDEVICE_OBJECT PartitionPdo,
    _In_ PVOID FtVolume, // FT_VOLUME*
    _In_ PDEVICE_OBJECT WholeDiskPdo,
    _In_ ULONG Alignment,
    _In_ BOOLEAN IsEmptyDevice,
    _In_ BOOLEAN IsHidenPartition,
    _In_ BOOLEAN IsReadOnlyPartition,
    _In_ BOOLEAN IsSystemPartition,
    _In_ ULONGLONG GptAttributes)
{
    PARTITION_INFORMATION_EX PartitionInfo;
    PVOLUME_EXTENSION VolumeExtension;
    PVOLUME_EXTENSION CurrentExtension;
    PDEVICE_OBJECT NewDeviceObject;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING VolumeName;
    GUID GptPartitionId;
    WCHAR SourceString[0x1E]; // 30
    PLIST_ENTRY Entry;
    PWSTR NameBuffer;
    KEVENT Event;
    PIRP Irp;
    ULONG SizeOfRundownCache;
    ULONG Signature;
    LONG IsDifferentVolumes;
    BOOLEAN IsGptPartition;
    NTSTATUS Status;

    DPRINT("FtpCreateNewDevice: %p, %p, %p, %p, %X, %X, %X, %X, %X\n",
           RootExtension, PartitionPdo, FtVolume, WholeDiskPdo, Alignment, IsEmptyDevice, IsHidenPartition, IsReadOnlyPartition, IsSystemPartition);

    ASSERT(!(PartitionPdo && FtVolume));
    ASSERT(!PartitionPdo || WholeDiskPdo);
    ASSERT(PartitionPdo || FtVolume);

    swprintf(SourceString, L"\\Device\\HarddiskVolume%d", RootExtension->VolumeCounter);
    RtlInitUnicodeString(&VolumeName, SourceString);

    SizeOfRundownCache = ExSizeOfRundownProtectionCacheAware();

    Status = IoCreateDevice(RootExtension->DriverObject,
                            (sizeof(*VolumeExtension) + SizeOfRundownCache),
                            &VolumeName,
                            FILE_DEVICE_DISK,
                            0,
                            FALSE,
                            &NewDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FtpCreateNewDevice: Status %X\n", Status);
        return FALSE;
    }

    VolumeExtension = NewDeviceObject->DeviceExtension;
    RtlZeroMemory(NewDeviceObject->DeviceExtension, sizeof(*VolumeExtension));

    VolumeExtension->DeviceExtensionType = 1;
    VolumeExtension->RootExtension = RootExtension;

    VolumeExtension->SelfDeviceObject = NewDeviceObject;
    VolumeExtension->PartitionPdo = PartitionPdo;
    VolumeExtension->FtVolume = FtVolume;

    KeInitializeSpinLock(&VolumeExtension->SpinLock);

    VolumeExtension->RundownCache = &VolumeExtension[1];
    ExInitializeRundownProtectionCacheAware(VolumeExtension->RundownCache, SizeOfRundownCache);

    InitializeListHead(&VolumeExtension->IrpList);
    InitializeListHead(&VolumeExtension->UniqueIdNotifyList);

    VolumeExtension->IsStartCallback = FALSE;
    VolumeExtension->IsVolumeOffline = TRUE;

    VolumeExtension->IsHidenPartition = IsHidenPartition;
    VolumeExtension->IsReadOnlyPartition = IsReadOnlyPartition;
    VolumeExtension->IsSystemPartition = IsSystemPartition;

    VolumeExtension->VolumeNumber = RootExtension->VolumeCounter++;
    VolumeExtension->GptAttributes = GptAttributes;

    //DPRINT("FtpCreateNewDevice: FIXME TRANSFER_PACKET_new\n");

    NameBuffer = ExAllocatePoolWithTag(PagedPool, 0xA0, 'tFcS'); // (160 = 80 * 2)
    if (!NameBuffer)
    {
        DPRINT1("FtpCreateNewDevice: Allocate failed\n");
        goto ExitError;
    }

    if (PartitionPdo)
    {
        VolumeExtension->WholeDiskPdo = WholeDiskPdo;

        VolumeExtension->WholeDiskDevice = IoGetAttachedDeviceReference(WholeDiskPdo);
        ObDereferenceObject(VolumeExtension->WholeDiskDevice);

        KeInitializeEvent(&Event, NotificationEvent, FALSE);

        Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_PARTITION_INFO_EX,
                                            PartitionPdo,
                                            NULL,
                                            0,
                                            &PartitionInfo,
                                            sizeof(PartitionInfo),
                                            FALSE,
                                            &Event,
                                            &IoStatusBlock);
        if (!Irp)
        {
            DPRINT1("FtpCreateNewDevice: Build irp failed\n");
            goto ExitError;
        }

        Status = IoCallDriver(PartitionPdo, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("FtpCreateNewDevice: Status %X\n", Status);
            goto ExitError;
        }

        if (PartitionInfo.PartitionStyle == 1)
        {
            GptPartitionId = PartitionInfo.Gpt.PartitionId;
            IsGptPartition = TRUE;
        }
        else
        {
            IsGptPartition = FALSE;
        }

        VolumeExtension->StartingOffset = PartitionInfo.StartingOffset.QuadPart;
        VolumeExtension->PartitionLength = PartitionInfo.PartitionLength.QuadPart;

        if (IsGptPartition)
        {
            VolumeExtension->IsGptPartition = TRUE;
            VolumeExtension->GptPartitionId = GptPartitionId;

            DPRINT1("FtpCreateNewDevice: FIXME\n");
            ASSERT(FALSE);
        }
        else
        {
            if (VolumeExtension->StartingOffset)
            {
                Signature = FtpQueryDiskSignatureCache(VolumeExtension);
                if (!Signature)
                {
                    DPRINT1("FtpCreateNewDevice: Signature is 0!\n");
                    goto ExitError;
                }

                swprintf(NameBuffer, L"Signature%XOffset%I64XLength%I64X", Signature, VolumeExtension->StartingOffset, VolumeExtension->PartitionLength);
            }
            else
            {
                DPRINT1("FtpCreateNewDevice: FIXME\n");
                ASSERT(FALSE);
            }
        }
    }
    else
    {
        DPRINT1("FtpCreateNewDevice: FIXME\n");
        ASSERT(FALSE);
    }

    RtlInitUnicodeString(&VolumeExtension->DevnodeName, NameBuffer);

    KeInitializeSemaphore(&VolumeExtension->ZeroRefSemaphore, 1, 1);
    KeInitializeSemaphore(&VolumeExtension->Semaphore, 1, 1);

    InsertTailList(&RootExtension->VolumeList, &VolumeExtension->Link);

    NewDeviceObject->Flags |= DO_DIRECT_IO;
    NewDeviceObject->AlignmentRequirement = Alignment;

    if (FtVolume)
    {
        DPRINT1("FtpCreateNewDevice: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        NewDeviceObject->StackSize = (PartitionPdo->StackSize + 1);
    }

    if (IsEmptyDevice)
    {
        DPRINT1("FtpCreateNewDevice: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        FtpCreateOldNameLinks(VolumeExtension);
    }

    if (!IsEmptyDevice && RootExtension->EmptyDeviceCount)
    {
        IsDifferentVolumes = 1;

        for (Entry = RootExtension->VolumeList.Flink;
             Entry != &RootExtension->VolumeList;
             Entry = Entry->Flink)
        {
            CurrentExtension = CONTAINING_RECORD(Entry, VOLUME_EXTENSION, Link);

            if (CurrentExtension != VolumeExtension && !CurrentExtension->IsEmptyVolume)
            {
                IsDifferentVolumes = RtlCompareUnicodeString(&CurrentExtension->DevnodeName, &VolumeExtension->DevnodeName, TRUE);
                if (!IsDifferentVolumes)
                    break;
            }
        }

        if (!IsDifferentVolumes)
        {
            DPRINT1("FtpCreateNewDevice: FIXME\n");
            ASSERT(FALSE);
        }
    }

    NewDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return TRUE;

ExitError:

    DPRINT1("FtpCreateNewDevice: FIXME\n");
    ASSERT(FALSE);
    IoDeleteDevice(NewDeviceObject);
    return FALSE;
}

NTSTATUS
NTAPI
FtpPartitionArrivedHelper(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PDEVICE_OBJECT PartitionPdo,
    _In_ PDEVICE_OBJECT WholeDiskPdo)
{
    GUID GptPartitionId;
    GUID GptPartitionType;
    ULONGLONG GptAttributes;
    ULONGLONG StartingOffset;
    ULONG DeviceNumber;
    UCHAR PartitionType;
    BOOLEAN IsSystemPartition;
    BOOLEAN IsReadOnlyPartition;
    BOOLEAN IsHidenPartition;
    BOOLEAN IsGptPartition;
    BOOLEAN Result;
    NTSTATUS Status;

    DPRINT("FtpPartitionArrivedHelper: %p, %p, %p\n", RootExtension, PartitionPdo, WholeDiskPdo);

    Status = FtpQueryPartitionInformation(RootExtension,
                                          PartitionPdo,
                                          &DeviceNumber,
                                          &StartingOffset,
                                          NULL,
                                          &PartitionType,
                                          NULL,
                                          &GptPartitionType,
                                          &GptPartitionId,
                                          &IsGptPartition,
                                          &GptAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FtpPartitionArrivedHelper: Status %X\n", Status);
        return Status;
    }

    IsHidenPartition = FALSE;
    IsReadOnlyPartition = FALSE;

    if (IsGptPartition)
    {
        DPRINT1("FtpPartitionArrivedHelper: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        if (PartitionType == PARTITION_LDM)
        {
            DPRINT1("FtpPartitionArrivedHelper: STATUS_INVALID_PARAMETER\n");
            return STATUS_INVALID_PARAMETER;
        }

        if (!IsRecognizedPartition_(PartitionType))
            IsHidenPartition = TRUE;

        IsSystemPartition = FALSE;
    }

    Result = FtpCreateNewDevice(RootExtension,
                                PartitionPdo,
                                NULL,
                                WholeDiskPdo,
                                PartitionPdo->AlignmentRequirement,
                                FALSE,
                                IsHidenPartition,
                                IsReadOnlyPartition,
                                IsSystemPartition,
                                GptAttributes);
    if (!Result)
    {
        DPRINT1("FtpPartitionArrivedHelper: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoInvalidateDeviceRelations(RootExtension->VolControlRootPdo, BusRelations);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FtpPartitionArrived(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PIRP Irp)
{
    PFT_PARTITION_ARRIVED Partitions;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    DPRINT("FtpPartitionArrived: RootExtension %p, Irp %p\n", RootExtension, Irp);

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Partitions))
    {
        DPRINT1("FtpPartitionArrived: STATUS_INVALID_PARAMETER (%X)\n", IoStack->Parameters.DeviceIoControl.InputBufferLength);
        return STATUS_INVALID_PARAMETER;
    }

    Partitions = Irp->AssociatedIrp.SystemBuffer;

    DPRINT("FtpPartitionArrived: %p, %p\n", Partitions->PartitionPdo, Partitions->WholeDiskPdo);

    Status = FtpPartitionArrivedHelper(RootExtension, Partitions->PartitionPdo, Partitions->WholeDiskPdo);

    DPRINT("FtpPartitionArrived: Status %X\n", Status);

    return Status;
}

NTSTATUS
NTAPI
FtpQueryId(
    _In_ PVOLUME_EXTENSION VolumeExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    UNICODE_STRING IdString;
    PWSTR IdBuffer;
    ULONG IdType;

    DPRINT("FtpQueryId: %p, %p\n", VolumeExtension, Irp);

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IdType = IoStack->Parameters.QueryId.IdType;

    if (IdType == 0)
    {
        RtlInitUnicodeString(&IdString, L"STORAGE\\Volume");
    }
    else if (IdType == 1)
    {
        RtlInitUnicodeString(&IdString, L"STORAGE\\Volume");
    }
    else if (IdType == 3)
    {
        IdString = VolumeExtension->DevnodeName;
    }
    else
    {
        DPRINT1("FtpQueryId: not vaild IdType %X for (%p, %p)\n", IdType, VolumeExtension, Irp);
        return STATUS_NOT_SUPPORTED;
    }

    IdBuffer = ExAllocatePoolWithTag(PagedPool, (IdString.Length + (2 * sizeof(WCHAR))), 'tFcS');
    if (!IdBuffer)
    {
        DPRINT1("FtpQueryId: Allocate failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(IdBuffer, IdString.Buffer, IdString.Length);

    IdBuffer[IdString.Length / sizeof(WCHAR)] = 0;
    IdBuffer[IdString.Length / sizeof(WCHAR) + 1] = 0;

    Irp->IoStatus.Information = (ULONG)IdBuffer;

    return STATUS_SUCCESS;
}

/* DRIVER DISPATCH ROUTINES *************************************************/

NTSTATUS
NTAPI
FtDiskCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    DPRINT("FtDiskCreate: %p, %p\n", DeviceObject, Irp);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, 0);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FtDiskReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PVOLUME_EXTENSION Extension;
    PROOT_EXTENSION RootExtension;
    PIO_STACK_LOCATION IoStack;
    ULONG ControlCode;
    NTSTATUS Status;

    DPRINT("FtDiskInternalDeviceControl: %p, %p\n", DeviceObject, Irp);

    Extension = DeviceObject->DeviceExtension;
    RootExtension = Extension->RootExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    Irp->IoStatus.Information = 0;

    FtpAcquire(RootExtension);

    if (Extension->DeviceExtensionType != 0)
    {
        DPRINT1("FtDiskInternalDeviceControl: STATUS_INVALID_PARAMETER\n");
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    ControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;

    switch (ControlCode)
    {
        case 0x760000:
            Status = FtpPartitionArrived(RootExtension, Irp);
            break;

        case 0x760004:
            DPRINT1("FtDiskInternalDeviceControl: %p, %p, %X\n", DeviceObject, Irp, ControlCode);
            ASSERT(FALSE);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;

        case 0x760008:
            DPRINT1("FtDiskInternalDeviceControl: %p, %p, %X\n", DeviceObject, Irp, ControlCode);
            ASSERT(FALSE);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;

        case 0x76000C:
            DPRINT1("FtDiskInternalDeviceControl: %p, %p, %X\n", DeviceObject, Irp, ControlCode);
            ASSERT(FALSE);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;

        case 0x760018:
            DPRINT1("FtDiskInternalDeviceControl: %p, %p, %X\n", DeviceObject, Irp, ControlCode);
            ASSERT(FALSE);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;

        case 0x76001C:
            DPRINT1("FtDiskInternalDeviceControl: %p, %p, %X\n", DeviceObject, Irp, ControlCode);
            ASSERT(FALSE);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;

        case 0x760020:
            DPRINT1("FtDiskInternalDeviceControl: %p, %p, %X\n", DeviceObject, Irp, ControlCode);
            ASSERT(FALSE);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;

        case 0x760024:
            ASSERT(FALSE);
            Status = 0;//FtpPmWmiCounterLibContext(RootExtension, Irp);
            break;

        case 0x760028:
            DPRINT1("FtDiskInternalDeviceControl: %p, %p, %X\n", DeviceObject, Irp, ControlCode);
            ASSERT(FALSE);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;

        default:
            DPRINT1("FtDiskInternalDeviceControl: %p, %p, %X\n", DeviceObject, Irp, ControlCode);
            ASSERT(FALSE);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

Exit:

    FtpRelease(RootExtension);
    Irp->IoStatus.Status = Status;

    IoCompleteRequest(Irp, 0);
    return Status;
}

NTSTATUS
NTAPI
FtDiskShutdownFlush(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtDiskCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PROOT_EXTENSION RootExtension;
    PVOLUME_EXTENSION VolumeExtension;
    PIO_STACK_LOCATION IoStack;
    PIO_STACK_LOCATION CancelIrpStack;
    ULONG DeviceExtensionType;
    PLIST_ENTRY Entry;
    PIRP CancelIrp;
    KIRQL OldIrql;

    DPRINT("FtDiskCleanup: %p, %p\n", DeviceObject, Irp);

    RootExtension = DeviceObject->DeviceExtension;
    VolumeExtension = DeviceObject->DeviceExtension;
    DeviceExtensionType = VolumeExtension->DeviceExtensionType;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    if (DeviceExtensionType == 1) // DEVICE_EXTENSION_VOLUME
    {
        IoCompleteRequest(Irp, 0);
        return STATUS_SUCCESS;
    }

    ASSERT(RootExtension->DeviceExtensionType == 0);

    IoAcquireCancelSpinLock(&OldIrql);

    while (TRUE)
    {
        for (Entry = RootExtension->ChangeNotifyIrpList.Flink;
             Entry != &RootExtension->ChangeNotifyIrpList;
             Entry = Entry->Flink)
        {
            CancelIrp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
            CancelIrpStack = IoGetCurrentIrpStackLocation(CancelIrp);

            if (CancelIrpStack->FileObject == IoStack->FileObject)
                break;
        }

        if (Entry == &RootExtension->ChangeNotifyIrpList)
            break;

        CancelIrp->Cancel = TRUE;
        CancelIrp->CancelIrql = OldIrql;
        CancelIrp->CancelRoutine = NULL;

        FtpCancelRoutine(DeviceObject, CancelIrp);
        IoAcquireCancelSpinLock(&OldIrql);
    }

    IoReleaseCancelSpinLock(OldIrql);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, 0);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FtDiskPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtWmi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* PNP */

static
NTSTATUS
FASTCALL
FtpPnpPdo(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
FtpQueryRootId(
    _In_ PROOT_EXTENSION RootExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    UNICODE_STRING IdString;
    ULONG IdType;
    PWSTR RootId;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IdType = IoStack->Parameters.QueryId.IdType;

    DPRINT("FtpQueryRootId: %p, %p, IdType %X\n", RootExtension, Irp, IdType);

    if (IdType == BusQueryDeviceID)
    {
        RtlInitUnicodeString(&IdString, L"ROOT\\FTDISK");
    }
    else if (IdType == BusQueryHardwareIDs)
    {
        RtlInitUnicodeString(&IdString, L"ROOT\\FTDISK");
    }
    else if (IdType == BusQueryInstanceID)
    {
        RtlInitUnicodeString(&IdString, L"0000");
    }
    else
    {
        DPRINT1("FtpQueryRootId: STATUS_NOT_SUPPORTED\n");
        return STATUS_NOT_SUPPORTED;
    }

    RootId = ExAllocatePoolWithTag(PagedPool, (IdString.Length + 2 * sizeof(WCHAR)), 'tFcS');
    if (!RootId)
    {
        DPRINT1("FtpQueryRootId: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(RootId, IdString.Buffer, IdString.Length);

    RootId[IdString.Length / 2] = 0;
    RootId[(IdString.Length / 2) + 1] = 0;

    Irp->IoStatus.Information = (ULONG_PTR)RootId;
    DPRINT("FtpQueryRootId: %p, %p, '%S'\n", RootExtension, Irp, Irp->IoStatus.Information);

    return STATUS_SUCCESS;
}

static
NTSTATUS
FASTCALL
FtpPnpFdo(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDEVICE_OBJECT AttachedToDevice;
    PROOT_EXTENSION RootExtension;
    PVOLUME_EXTENSION VolumeExtension;
    PIO_STACK_LOCATION IoStack;
    PDEVICE_RELATIONS DeviceRelation;
    PDEVICE_CAPABILITIES Capabilities;
    PLIST_ENTRY Entry;
    KEVENT Event;
    ULONG size;
    ULONG ix;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    DPRINT("FtpPnpFdo: DeviceObject %p, Irp %p\n", DeviceObject, Irp);

    RootExtension = DeviceObject->DeviceExtension;
    AttachedToDevice = RootExtension->AttachedToDevice;

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

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_RESOURCES:
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(AttachedToDevice, Irp);
        }
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
        {
            DPRINT("FtpPnpFdo: STATUS_INVALID_DEVICE_REQUEST\n");
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }
        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        {
            DPRINT1("FtpPnpFdo: FIXME\n");
            ASSERT(FALSE);
            break;
        }
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            if (IoStack->Parameters.QueryDeviceRelations.Type != BusRelations)
            {
                IoSkipCurrentIrpStackLocation(Irp);
                return IoCallDriver(AttachedToDevice, Irp);
            }

            FtpAcquire(RootExtension);

            ix = 0;

            for (Entry = RootExtension->VolumeList.Flink;
                 Entry != &RootExtension->VolumeList;
                 Entry = Entry->Flink)
            {
                ix++;
            }

            size = (sizeof(DEVICE_RELATIONS) - sizeof(PDEVICE_OBJECT));
            size += (ix * sizeof(PDEVICE_OBJECT));

            DeviceRelation = ExAllocatePoolWithTag(PagedPool, size, 'tFcS');
            if (!DeviceRelation)
            {
                FtpRelease(RootExtension);

                Irp->IoStatus.Information = 0;
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;

                IoCompleteRequest(Irp, 0);

                return STATUS_INSUFFICIENT_RESOURCES;
            }

            DeviceRelation->Count = ix;
            ix = 0;

            for (Entry = RootExtension->VolumeList.Flink;
                 Entry != &RootExtension->VolumeList;
                 Entry = Entry->Flink)
            {
                VolumeExtension = CONTAINING_RECORD(Entry, VOLUME_EXTENSION, Link);

                DeviceRelation->Objects[ix] = VolumeExtension->SelfDeviceObject;
                ObReferenceObject(DeviceRelation->Objects[ix]);

                ix++;
            }

            while (!IsListEmpty(&RootExtension->EmptyVolumesList))
            {
                ASSERT(FALSE);
                //Entry = RemoveHeadList(&RootExtension->EmptyVolumesList);
                //VolumeExtension = CONTAINING_RECORD(Entry, VOLUME_EXTENSION, Link);
                //VolumeExtension->VolExt31 = 1;
            }

            FtpRelease(RootExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = (ULONG_PTR)DeviceRelation;

            IoSkipCurrentIrpStackLocation(Irp);

            return IoCallDriver(AttachedToDevice, Irp);
        }
        case IRP_MN_QUERY_CAPABILITIES:
        {
              KeInitializeEvent(&Event, NotificationEvent, FALSE);

              IoCopyCurrentIrpStackLocationToNext(Irp);
              IoSetCompletionRoutine(Irp, FtpSignalCompletion, &Event, TRUE, TRUE, TRUE);

              IoCallDriver(AttachedToDevice, Irp);

              KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

              Capabilities = IoStack->Parameters.DeviceCapabilities.Capabilities;

              Capabilities->RawDeviceOK = 1;
              Capabilities->SilentInstall = 1;

              return Irp->IoStatus.Status;
        }
        case IRP_MN_QUERY_ID:
        {
            Status = FtpQueryRootId(RootExtension, Irp);
            if (NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoSkipCurrentIrpStackLocation(Irp);

                Status = IoCallDriver(AttachedToDevice, Irp);
                DPRINT1("FtpPnpFdo: Status (%X)\n", Status);
                return Status;
            }

            if (Status == STATUS_NOT_SUPPORTED)
            {
                IoSkipCurrentIrpStackLocation(Irp);
                Status = IoCallDriver(AttachedToDevice, Irp);
                DPRINT1("FtpPnpFdo: Status (%X)\n", Status);
                return Status;
            }

            break;
        }
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
        {
              KeInitializeEvent(&Event, NotificationEvent, FALSE);

              IoCopyCurrentIrpStackLocationToNext(Irp);
              IoSetCompletionRoutine(Irp, FtpSignalCompletion, &Event, TRUE, TRUE, TRUE);

              IoCallDriver(AttachedToDevice, Irp);
              KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

              Status = Irp->IoStatus.Status;

              /* Do not show device in device manager. */
              if (!NT_SUCCESS(Status))
              {
                  Status = STATUS_SUCCESS;
                  Irp->IoStatus.Information = (PNP_DEVICE_DONT_DISPLAY_IN_UI | PNP_DEVICE_NOT_DISABLEABLE);
              }
              else
              {
                  Irp->IoStatus.Information |= (PNP_DEVICE_DONT_DISPLAY_IN_UI | PNP_DEVICE_NOT_DISABLEABLE);
              }

              return Status;
        }
        default:
        {
            DPRINT1("FtpPnpFdo: Unknown PNP IRP_MN_ (%X)\n", IoStack->MinorFunction);
            ASSERT(FALSE);
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(AttachedToDevice, Irp);
        }
    }

    if (Status != STATUS_NOT_SUPPORTED)
        Irp->IoStatus.Status = Status;
    else
        Status = Irp->IoStatus.Status;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    DPRINT("FtpPnpFdo: Status (%X)\n", Status);
    return Status;
}

NTSTATUS
NTAPI
FtDiskPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PROOT_EXTENSION Extension;
    NTSTATUS Status;

    DPRINT("FtDiskPnp: %p, %p\n", DeviceObject, Irp);

    Extension = DeviceObject->DeviceExtension;

    if (Extension->DeviceExtensionType == 0) // ROOT
    {
        Status = FtpPnpFdo(DeviceObject, Irp);
        DPRINT("FtDiskPnp: %p, %p, %X\n", DeviceObject, Irp, Status);
        return Status;
    }

    if (Extension->DeviceExtensionType == 1) // VOLUME
    {
        Status = FtpPnpPdo(DeviceObject, Irp);
        DPRINT("FtDiskPnp: %p, %p, %X\n", DeviceObject, Irp, Status);
        return Status;
    }

    DPRINT("FtDiskPnp: %p, %p, DeviceExtensionType %X\n", DeviceObject, Irp, Extension->DeviceExtensionType);

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;

    IoCompleteRequest(Irp, 0);

    return STATUS_INVALID_DEVICE_REQUEST;
}

/* REINITIALIZE DRIVER ROUTINES *********************************************/

VOID
NTAPI
FtpDriverReinitialization(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
FtpBootDriverReinitialization(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* INIT DRIVER ROUTINES *****************************************************/

NTSTATUS
NTAPI
FtDiskAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT VolControlRootPdo,
    _In_ PUNICODE_STRING RegistryPath)
{
    PROOT_EXTENSION RootExtension;
    PDEVICE_OBJECT RootFdo;
    UNICODE_STRING NameString;
    UNICODE_STRING SymbolicLinkName;
    USHORT Size;
    NTSTATUS Status;

    DPRINT("FtDiskAddDevice: %p, %p, '%wZ'\n", DriverObject, VolControlRootPdo, RegistryPath);

    RtlInitUnicodeString(&NameString, L"\\Device\\FtControl");

    Status = IoCreateDevice(DriverObject,
                            sizeof(*RootExtension),
                            &NameString,
                            FILE_DEVICE_NETWORK,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &RootFdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FtDiskAddDevice: Status %X\n", Status);
        return Status;
    }

    RtlInitUnicodeString(&SymbolicLinkName, L"\\DosDevices\\FtControl");
    IoCreateSymbolicLink(&SymbolicLinkName, &NameString);

    RootExtension = RootFdo->DeviceExtension;
    RtlZeroMemory(RootExtension, sizeof(*RootExtension));

    DPRINT("FtDiskAddDevice: RootFdo %p, %p\n", RootFdo, RootExtension);

    RootExtension->DriverObject = DriverObject;
    RootExtension->SelfDeviceObject = RootFdo;
    RootExtension->RootExtension = RootExtension;

    RootExtension->DeviceExtensionType = 0; // Root ext.

    KeInitializeSpinLock(&RootExtension->SpinLock);

    RootExtension->AttachedToDevice = IoAttachDeviceToDeviceStack(RootFdo, VolControlRootPdo);
    if (!RootExtension->AttachedToDevice)
    {
        DPRINT1("FtDiskAddDevice: AttachedToDevice is NULL!\n");
        IoDeleteSymbolicLink(&SymbolicLinkName);
        IoDeleteDevice(RootFdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    DPRINT("FtDiskAddDevice: RootPdo %p, RootFdo attached to %p\n", VolControlRootPdo, RootExtension->AttachedToDevice);

    RootExtension->VolControlRootPdo = VolControlRootPdo;

    InitializeListHead(&RootExtension->VolumeList);
    InitializeListHead(&RootExtension->EmptyVolumesList);

    RootExtension->VolumeCounter = 1;

    RootExtension->LogicalDiskInfoSet = ExAllocatePoolWithTag(NonPagedPool, sizeof(*RootExtension->LogicalDiskInfoSet), 'tFcS');
    if (!RootExtension->LogicalDiskInfoSet)
    {
        DPRINT1("FtDiskAddDevice: STATUS_INSUFFICIENT_RESOURCES\n");
        IoDeleteSymbolicLink(&SymbolicLinkName);
        IoDetachDevice(RootExtension->AttachedToDevice);
        IoDeleteDevice(RootFdo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(RootExtension->LogicalDiskInfoSet, sizeof(*RootExtension->LogicalDiskInfoSet));

    InitializeListHead(&RootExtension->WorkerQueue);
    InitializeListHead(&RootExtension->ChangeNotifyIrpList);
    KeInitializeSemaphore(&RootExtension->RootSemaphore, 1, 1);

    Size = (RegistryPath->Length + sizeof(WCHAR));
    RootExtension->RegistryPath.MaximumLength = Size;

    RootExtension->RegistryPath.Buffer = ExAllocatePoolWithTag(PagedPool, Size, 'tFcS');
    if (!RootExtension->RegistryPath.Buffer)
    {
        DPRINT1("FtDiskAddDevice: STATUS_INSUFFICIENT_RESOURCES\n");

        if (RootExtension->LogicalDiskInfoSet)
            ExFreePoolWithTag(RootExtension->LogicalDiskInfoSet, 'tFcS');

        IoDeleteSymbolicLink(&SymbolicLinkName);
        IoDetachDevice(RootExtension->AttachedToDevice);
        IoDeleteDevice(RootFdo);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyUnicodeString(&RootExtension->RegistryPath, RegistryPath);

    Status = IoRegisterShutdownNotification(RootFdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FtDiskAddDevice: Status %X\n", Status);

        ExFreePoolWithTag(RootExtension->RegistryPath.Buffer, 'tFcS');

        if (RootExtension->LogicalDiskInfoSet)
            ExFreePoolWithTag(RootExtension->LogicalDiskInfoSet, 'tFcS');

        IoDeleteSymbolicLink(&SymbolicLinkName);
        IoDetachDevice(RootExtension->AttachedToDevice);
        IoDeleteDevice(RootFdo);

        return Status;
    }

    RootFdo->Flags &= ~DO_DEVICE_INITIALIZING;

    DPRINT1("FtDiskAddDevice: return STATUS_SUCCESS\n");
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    PDEVICE_OBJECT VolControlRootPdo = NULL;
    PDEVICE_OBJECT AttachedToDevice;
    PROOT_EXTENSION RootExtension;
    NTSTATUS Status;

    DPRINT("DriverEntry: %p, '%wZ'\n", DriverObject, RegistryPath);

    DriverObject->DriverUnload = FtDiskUnload;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = FtDiskCreate;
    DriverObject->MajorFunction[IRP_MJ_READ] = FtDiskReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = FtDiskReadWrite;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = FtDiskShutdownFlush;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = FtDiskDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = FtDiskInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = FtDiskShutdownFlush;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = FtDiskCleanup;
    DriverObject->MajorFunction[IRP_MJ_POWER] = FtDiskPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = FtWmi;
    DriverObject->MajorFunction[IRP_MJ_PNP] = FtDiskPnp;

    ObReferenceObject(DriverObject);

    Status = IoReportDetectedDevice(DriverObject, InterfaceTypeUndefined, -1, -1, NULL, NULL, TRUE, &VolControlRootPdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        return Status;
    }

    Status = FtDiskAddDevice(DriverObject, VolControlRootPdo, RegistryPath);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        return Status;
    }

    AttachedToDevice = IoGetAttachedDeviceReference(VolControlRootPdo);
    ObDereferenceObject(AttachedToDevice);

    RootExtension = AttachedToDevice->DeviceExtension;

    Status = IoRegisterDeviceInterface(RootExtension->VolControlRootPdo,
                                       &VOLMGR_VOLUME_MANAGER_GUID,
                                       NULL,
                                       &RootExtension->SymbolicLinkName);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        RootExtension->SymbolicLinkName.Buffer = NULL;
    }
    else
    {
        IoSetDeviceInterfaceState(&RootExtension->SymbolicLinkName, TRUE);
    }

    IoRegisterBootDriverReinitialization(DriverObject, FtpBootDriverReinitialization, RootExtension);
    IoRegisterDriverReinitialization(DriverObject, FtpDriverReinitialization, RootExtension);

    //FtpQueryRegistryRevertEntries(...);

    RtlDeleteRegistryValue(0, RegistryPath->Buffer, L"GptAttributeRevertEntries");

    IoInvalidateDeviceState(RootExtension->VolControlRootPdo);

    DPRINT("DriverEntry: exit with STATUS_SUCCESS\n");

    return STATUS_SUCCESS;
}

/* EOF */
