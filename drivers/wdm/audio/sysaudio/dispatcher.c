/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel Streaming
 * FILE:            drivers/wdm/audio/sysaudio/dispatcher.c
 * PURPOSE:         System Audio graph builder
 * PROGRAMMER:      Johannes Anderwald
 */

#include "sysaudio.h"

#define NDEBUG
#include <debug.h>

const GUID KSCATEGORY_MIXER   = {0xAD809C00L, 0x7B88, 0x11D0, {0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00}};

NTSTATUS
NTAPI
Dispatch_fnDeviceIoControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    if (IoStack->Parameters.DeviceIoControl.IoControlCode == IOCTL_KS_PROPERTY)
    {
       return SysAudioHandleProperty(DeviceObject, Irp);
    }

    /* unsupported request */
    Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
NTAPI
Dispatch_fnClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    DPRINT("Dispatch_fnClose called DeviceObject %p Irp %p\n", DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static KSDISPATCH_TABLE DispatchTable =
{
    Dispatch_fnDeviceIoControl,
    KsDispatchInvalidDeviceRequest,
    KsDispatchInvalidDeviceRequest,
    KsDispatchInvalidDeviceRequest,
    Dispatch_fnClose,
    KsDispatchInvalidDeviceRequest,
    KsDispatchInvalidDeviceRequest,
    KsDispatchFastIoDeviceControlFailure,
    KsDispatchFastReadFailure,
    KsDispatchFastWriteFailure,
};

NTSTATUS
NTAPI
DispatchCreateSysAudio(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    NTSTATUS Status;
    KSOBJECT_HEADER ObjectHeader;
    PKSOBJECT_CREATE_ITEM CreateItem;

    DPRINT("DispatchCreateSysAudio entered\n");

    /* allocate create item */
    CreateItem = AllocateItem(NonPagedPool, sizeof(KSOBJECT_CREATE_ITEM));
    if (!CreateItem)
    {
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* zero create struct */
    RtlZeroMemory(CreateItem, sizeof(KSOBJECT_CREATE_ITEM));

    /* setup create context */
    CreateItem->Create = DispatchCreateSysAudioPin;
    RtlInitUnicodeString(&CreateItem->ObjectClass, KSSTRING_Pin);

    /* allocate object header */
    Status = KsAllocateObjectHeader(&ObjectHeader, 1, CreateItem, Irp, &DispatchTable);

    DPRINT("KsAllocateObjectHeader result %x\n", Status);
    /* complete the irp */
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
SysAudioAllocateDeviceHeader(
    IN SYSAUDIODEVEXT *DeviceExtension)
{
    NTSTATUS Status;
    PKSOBJECT_CREATE_ITEM CreateItem;

    /* allocate create item */
    CreateItem = AllocateItem(NonPagedPool, sizeof(KSOBJECT_CREATE_ITEM));
    if (!CreateItem)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* initialize create item struct */
    RtlZeroMemory(CreateItem, sizeof(KSOBJECT_CREATE_ITEM));
    CreateItem->Create = DispatchCreateSysAudio;

    /* FIXME Sysaudio doesnt need a named create item because it installs itself
     * via the device interface
     */
    RtlInitUnicodeString(&CreateItem->ObjectClass, L"GLOBAL");
    CreateItem->Flags = KSCREATE_ITEM_WILDCARD;

    Status = KsAllocateDeviceHeader(&DeviceExtension->KsDeviceHeader,
                                    1,
                                    CreateItem);
    return Status;
}

NTSTATUS
SysAudioOpenKMixer(
    IN SYSAUDIODEVEXT *DeviceExtension)
{
    NTSTATUS Status;
    LPWSTR SymbolicLinkList, SymbolicLink;
    UNICODE_STRING SymbolicLinkU;
    UNICODE_STRING DevicePath = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\kmixer");

    Status = ZwLoadDriver(&DevicePath);

    if (NT_SUCCESS(Status))
    {
        Status = IoGetDeviceInterfaces(&KSCATEGORY_MIXER,
                                       NULL,
                                       0,
                                       &SymbolicLinkList);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IoGetDeviceInterfaces failed with status 0x%lx\n", Status);
            DeviceExtension->KMixerHandle = NULL;
            DeviceExtension->KMixerFileObject = NULL;
            return Status;
        }

        for (SymbolicLink = SymbolicLinkList;
             *SymbolicLink != UNICODE_NULL;
             SymbolicLink += wcslen(SymbolicLink) + 1)
        {
            DPRINT("Opening device %S\n", SymbolicLink);
            RtlInitUnicodeString(&SymbolicLinkU, SymbolicLink);
            Status = OpenDevice(&SymbolicLinkU, &DeviceExtension->KMixerHandle, &DeviceExtension->KMixerFileObject);
            if (NT_SUCCESS(Status))
            {
                DPRINT("Successfully opened %S, handle %p\n", SymbolicLink, DeviceExtension->KMixerHandle);
                break;
            }
            else
            {
                RtlFreeUnicodeString(&SymbolicLinkU);
                DeviceExtension->KMixerHandle = NULL;
                DeviceExtension->KMixerFileObject = NULL;
            }
        }
    }

    DPRINT("Status 0x%lx KMixerHandle %p KMixerFileObject %p\n", Status, DeviceExtension->KMixerHandle, DeviceExtension->KMixerFileObject);
    return Status;
}
