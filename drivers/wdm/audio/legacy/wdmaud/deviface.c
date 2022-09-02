/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel Streaming
 * FILE:            drivers/wdm/audio/legacy/wdmaud/deviface.c
 * PURPOSE:         System Audio graph builder
 * PROGRAMMER:      Andrew Greenwood
 *                  Johannes Anderwald
 */

#include "wdmaud.h"

#define NDEBUG
#include <debug.h>


NTSTATUS
WdmAudOpenSysAudioDevice(
    IN LPWSTR DeviceName,
    OUT PHANDLE Handle)
{
    UNICODE_STRING SymbolicLink;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    RtlInitUnicodeString(&SymbolicLink, DeviceName);
    InitializeObjectAttributes(&ObjectAttributes, &SymbolicLink, OBJ_OPENIF | OBJ_KERNEL_HANDLE, NULL, NULL);

    Status = IoCreateFile(Handle,
                          SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          0,
                          0,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL,
                          0,
                          CreateFileTypeNone,
                          NULL,
                          IO_NO_PARAMETER_CHECKING | IO_FORCE_ACCESS_CHECK);
    DPRINT("IoCreateFile status 0x%lx\n", Status);
    return Status;
}

NTSTATUS
WdmAudOpenSysAudioDevices(
    OUT PHANDLE phSysAudio,
    OUT PFILE_OBJECT *pFileObject)
{
    NTSTATUS Status = STATUS_SUCCESS;
    LPWSTR SymbolicLinkList, SymbolicLink;

    Status = IoGetDeviceInterfaces(&KSCATEGORY_SYSAUDIO,
                                   NULL,
                                   0,
                                   &SymbolicLinkList);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (SymbolicLink = SymbolicLinkList;
         *SymbolicLink != UNICODE_NULL;
         SymbolicLink += wcslen(SymbolicLink) + 1)
    {
        DPRINT("Opening device %S\n", SymbolicLink);
        Status = WdmAudOpenSysAudioDevice(SymbolicLink, phSysAudio);
        if (NT_SUCCESS(Status))
        {
            DPRINT("Successfully opened %S, handle %p\n", SymbolicLink, *phSysAudio);
            break;
        }
    }
    ExFreePool(SymbolicLinkList);

    if (!*phSysAudio)
    {
        DPRINT1("Failed to find sysaudio devices 0x%lx\n", Status);
        return Status;
    }

    /* get the file object */
    Status = ObReferenceObjectByHandle(*phSysAudio,
                                       FILE_READ_DATA | FILE_WRITE_DATA,
                                       *IoFileObjectType,
                                       KernelMode,
                                       (PVOID*)pFileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to reference FileObject 0x%lx\n", Status);
        ZwClose(*phSysAudio);
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
GetSysAudioDeviceInterface(
    OUT LPWSTR* SymbolicLinkList)
{
    NTSTATUS Status;

    /* Get SysAudio device interface */
    Status = IoGetDeviceInterfaces(&KSCATEGORY_SYSAUDIO, NULL, 0, SymbolicLinkList);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoGetDeviceInterfaces failed with 0x%lx\n", Status);
        return Status;
    }

    DPRINT("Got SysAudio device interface %ls\n", *SymbolicLinkList);
    return STATUS_SUCCESS;
}

NTSTATUS
WdmAudRegisterDeviceInterface(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN PWDMAUD_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;
    UNICODE_STRING SymbolicLinkName;

    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &KSCATEGORY_WDMAUD, NULL, &SymbolicLinkName);
    if (NT_SUCCESS(Status))
    {
        IoSetDeviceInterfaceState(&SymbolicLinkName, TRUE);
        RtlFreeUnicodeString(&SymbolicLinkName);
        return Status;
    }

    return Status;
}

NTSTATUS
WdmAudAllocateContext(
    IN PDEVICE_OBJECT DeviceObject,
    IN PWDMAUD_CLIENT *pClient)
{
    PWDMAUD_CLIENT Client;
    PWDMAUD_DEVICE_EXTENSION DeviceExtension;

    /* get device extension */
    DeviceExtension = (PWDMAUD_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    /* allocate client context struct */
    Client = AllocateItem(NonPagedPool, sizeof(WDMAUD_CLIENT));

    /* check for allocation failure */
    if (!Client)
    {
        /* not enough memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* zero client context struct */
    RtlZeroMemory(Client, sizeof(WDMAUD_CLIENT));

    /* initialize mixer event list */
    InitializeListHead(&Client->MixerEventList);

    /* store result */
    *pClient = Client;

    /* insert client into list */
    ExInterlockedInsertTailList(&DeviceExtension->WdmAudClientList, &Client->Entry, &DeviceExtension->Lock);

    /* done */
    return STATUS_SUCCESS;
}
