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

const GUID KSPROPSETID_Sysaudio                 = {0xCBE3FAA0L, 0xCC75, 0x11D0, {0xB4, 0x65, 0x00, 0x00, 0x1A, 0x18, 0x18, 0xE6}};

NTSTATUS
WdmAudControlInitialize(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    NTSTATUS Status;
    LPWSTR SymbolicLinkList;
    PWDMAUD_DEVICE_EXTENSION DeviceExtension;

    /* Get device extension */
    DeviceExtension = (PWDMAUD_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    /* Get SysAudio device interface */
    Status = GetSysAudioDeviceInterface(&SymbolicLinkList);
    if (NT_SUCCESS(Status))
    {
        /* Wait for initialization finishing */
        KeWaitForSingleObject(&DeviceExtension->InitializationCompletionEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    ExFreePool(SymbolicLinkList);

    return SetIrpIoStatus(Irp, Status, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
WdmAudControlOpen(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    NTSTATUS Status;

    if (DeviceInfo->DeviceType == MIXER_DEVICE_TYPE)
    {
        Status = WdmAudControlOpenMixer(DeviceObject, Irp, DeviceInfo, ClientInfo);
    }
    else if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE)
    {
        Status = WdmAudControlOpenWave(DeviceObject, Irp, DeviceInfo, ClientInfo);
    }
    else if (DeviceInfo->DeviceType == MIDI_OUT_DEVICE_TYPE || DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE)
    {
        Status = WdmAudControlOpenMidi(DeviceObject, Irp, DeviceInfo, ClientInfo);
    }

    if (NT_SUCCESS(Status) && DeviceInfo->DeviceType != MIXER_DEVICE_TYPE)
    {
        /* Start audio device after opening */
        WdmAudSetDeviceState(DeviceObject, Irp, DeviceInfo, KSSTATE_RUN, FALSE);
    }

    return SetIrpIoStatus(Irp, Status, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
WdmAudControlDeviceType(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    ULONG Result = 0;

    if (DeviceInfo->DeviceType == MIXER_DEVICE_TYPE)
    {
        Result = WdmAudGetMixerDeviceCount();
    }
    else if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        Result = WdmAudGetWaveOutDeviceCount();
    }
    else if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE)
    {
        Result = WdmAudGetWaveInDeviceCount();
    }
    else if (DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE)
    {
        Result = WdmAudGetMidiInDeviceCount();
    }
    else if (DeviceInfo->DeviceType == MIDI_OUT_DEVICE_TYPE)
    {
        Result = WdmAudGetMidiOutDeviceCount();
    }


    /* store result count */
    DeviceInfo->DeviceIndex = Result;

    DPRINT("WdmAudControlDeviceType Devices %u\n", DeviceInfo->DeviceIndex);
    return SetIrpIoStatus(Irp, STATUS_SUCCESS, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
WdmAudCapabilities(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    PWDMAUD_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    DPRINT("WdmAudCapabilities entered\n");

    DeviceExtension = (PWDMAUD_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (DeviceInfo->DeviceType == MIXER_DEVICE_TYPE)
    {
        Status = WdmAudMixerCapabilities(DeviceObject, Irp, DeviceInfo, ClientInfo, DeviceExtension);
    }
    else if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        Status = WdmAudWaveCapabilities(DeviceObject, Irp, DeviceInfo, ClientInfo, DeviceExtension);
    }
    else if (DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE || DeviceInfo->DeviceType == MIDI_OUT_DEVICE_TYPE)
    {
        Status = WdmAudMidiCapabilities(DeviceObject, Irp, DeviceInfo, ClientInfo, DeviceExtension);
    }

    return SetIrpIoStatus(Irp, Status, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
NTAPI
WdmAudIoctlClose(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    ULONG Index;

    if (DeviceInfo->DeviceType != MIXER_DEVICE_TYPE)
    {
        /* Stop audio device before closing */
        WdmAudSetDeviceState(DeviceObject, Irp, DeviceInfo, KSSTATE_STOP, FALSE);
    }

    for(Index = 0; Index < ClientInfo->NumPins; Index++)
    {
        if (ClientInfo->hPins[Index].Handle == DeviceInfo->hDevice && ClientInfo->hPins[Index].Type != MIXER_DEVICE_TYPE)
        {
            DPRINT1("Closing device %p\n", DeviceInfo->hDevice);
            ClosePin(ClientInfo, Index);
            DeviceInfo->hDevice = NULL;
            return SetIrpIoStatus(Irp, STATUS_SUCCESS, sizeof(WDMAUD_DEVICE_INFO));
        }
        else if (ClientInfo->hPins[Index].Handle == DeviceInfo->hDevice && ClientInfo->hPins[Index].Type == MIXER_DEVICE_TYPE)
        {
            DPRINT1("Closing mixer %p\n", DeviceInfo->hDevice);
            return WdmAudControlCloseMixer(DeviceObject, Irp, DeviceInfo, ClientInfo, Index);
        }
    }

    return SetIrpIoStatus(Irp, STATUS_INVALID_PARAMETER, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
WdmAudSetDeviceState(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  KSSTATE State,
    IN  BOOL CompleteIrp) // Avoids multiple IRP complete requests
                          // when calling internally from wdmaud.
{
    NTSTATUS Status;
    KSPROPERTY Property;
    ULONG BytesReturned;
    PFILE_OBJECT FileObject;

    DPRINT("WdmAudControlDeviceState\n");

    Status = ObReferenceObjectByHandle(DeviceInfo->hDevice, GENERIC_READ | GENERIC_WRITE, *IoFileObjectType, KernelMode, (PVOID*)&FileObject, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Error: invalid device handle provided %p Type %x\n", DeviceInfo->hDevice, DeviceInfo->DeviceType);
        return CompleteIrp ? SetIrpIoStatus(Irp, Status, 0) : Status;
    }

    Property.Set = KSPROPSETID_Connection;
    Property.Id = KSPROPERTY_CONNECTION_STATE;
    Property.Flags = KSPROPERTY_TYPE_SET;

    Status = KsSynchronousIoControlDevice(FileObject, KernelMode, IOCTL_KS_PROPERTY, (PVOID)&Property, sizeof(KSPROPERTY), (PVOID)&State, sizeof(KSSTATE), &BytesReturned);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("%x failed with status 0x%lx\n", State, Status);
        ObDereferenceObject(FileObject);
        return CompleteIrp ? SetIrpIoStatus(Irp, Status, 0) : Status;
    }

    ObDereferenceObject(FileObject);

    DPRINT("WdmAudControlDeviceState Status 0x%lx BytesReturned %lu\n", Status, BytesReturned);
    return CompleteIrp ? SetIrpIoStatus(Irp, STATUS_SUCCESS, sizeof(WDMAUD_DEVICE_INFO)) : STATUS_SUCCESS;
}

NTSTATUS
NTAPI
WdmAudResetStream(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  BOOL CompleteIrp) // Avoids multiple IRP complete requests
                          // when calling internally from wdmaud.
{
    KSSTATE State;
    NTSTATUS Status;
    KSRESET ResetStream;
    KSPROPERTY Property;
    ULONG BytesReturned;
    PFILE_OBJECT FileObject;

    DPRINT("WdmAudResetStream\n");

    Status = ObReferenceObjectByHandle(DeviceInfo->hDevice, GENERIC_READ | GENERIC_WRITE, *IoFileObjectType, KernelMode, (PVOID*)&FileObject, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Error: invalid device handle provided %p Type %x\n", DeviceInfo->hDevice, DeviceInfo->DeviceType);
        return CompleteIrp ? SetIrpIoStatus(Irp, Status, 0) : Status;
    }

    Property.Set = KSPROPSETID_Connection;
    Property.Id = KSPROPERTY_CONNECTION_STATE;
    Property.Flags = KSPROPERTY_TYPE_SET;

    State = DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ? KSSTATE_PAUSE : KSSTATE_STOP;

    Status = KsSynchronousIoControlDevice(FileObject, KernelMode, IOCTL_KS_PROPERTY, (PVOID)&Property, sizeof(KSPROPERTY), (PVOID)&State, sizeof(KSSTATE), &BytesReturned);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("%ls failed with status 0x%lx\n",
                DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
                L"KSSTATE_PAUSE" : L"KSSTATE_STOP",
                Status);
        ObDereferenceObject(FileObject);
        return CompleteIrp ? SetIrpIoStatus(Irp, Status, 0) : Status;
    }

    if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        ResetStream = KSRESET_BEGIN;
        Status = KsSynchronousIoControlDevice(FileObject, KernelMode, IOCTL_KS_RESET_STATE, (PVOID)&ResetStream, sizeof(KSRESET), NULL, 0, &BytesReturned);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("KSRESET_BEGIN failed with status 0x%lx\n", Status);
            ObDereferenceObject(FileObject);
            return CompleteIrp ? SetIrpIoStatus(Irp, Status, 0) : Status;
        }
        ResetStream = KSRESET_END;
        Status = KsSynchronousIoControlDevice(FileObject, KernelMode, IOCTL_KS_RESET_STATE, (PVOID)&ResetStream, sizeof(KSRESET), NULL, 0, &BytesReturned);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("KSRESET_END failed with status 0x%lx\n", Status);
            ObDereferenceObject(FileObject);
            return CompleteIrp ? SetIrpIoStatus(Irp, Status, 0) : Status;
        }
    }

    ObDereferenceObject(FileObject);

    DPRINT("WdmAudResetStream Status 0x%lx BytesReturned %lu\n", Status, BytesReturned);
    return CompleteIrp ? SetIrpIoStatus(Irp, STATUS_SUCCESS, sizeof(WDMAUD_DEVICE_INFO)) : STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Ctx)
{
    PKSSTREAM_HEADER Header;
    PMDL Mdl, NextMdl;
    PWDMAUD_COMPLETION_CONTEXT Context = (PWDMAUD_COMPLETION_CONTEXT)Ctx;

    /* Get stream header */
    Header = (PKSSTREAM_HEADER)Irp->UserBuffer;

    /* Sanity check */
    ASSERT(Header);

    /* Time to free all allocated mdls */
    Mdl = Irp->MdlAddress;

    while(Mdl)
    {
        /* Get next mdl */
        NextMdl = Mdl->Next;

        /* Unlock pages */
        MmUnlockPages(Mdl);

        /* Grab next mdl */
        Mdl = NextMdl;
    }

    /* Clear mdl list */
    Irp->MdlAddress = Context->Mdl;

    DPRINT("IoCompletion Irp %p IoStatus %lx Information %lx\n", Irp, Irp->IoStatus.Status, Irp->IoStatus.Information);

    if (!NT_SUCCESS(Irp->IoStatus.Status))
    {
        /* failed */
        Irp->IoStatus.Information = 0;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
WdmAudReadWrite(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    PWDMAUD_COMPLETION_CONTEXT Context;
    PWDMAUD_DEVICE_INFO DeviceInfo;
    PWDMAUD_CLIENT ClientInfo;
    PKSSTREAM_HEADER StreamHeader;
    PFILE_OBJECT FileObject;
    PIO_STACK_LOCATION IoStack;
    PWAVEHDR WaveHeader;
    NTSTATUS Status;
    ULONG Length;
    ULONG PinId;
    PMDL Mdl;

    /* get device info */
    DeviceInfo = (PWDMAUD_DEVICE_INFO)Irp->AssociatedIrp.SystemBuffer;
    ASSERT(DeviceInfo);

    /* Get wave header passed by the caller */
    WaveHeader = (PWAVEHDR)DeviceInfo->Buffer;
    ASSERT(WaveHeader);

    /* get current irp stack location */
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    ASSERT(IoStack->FileObject);

    /* get client context struct */
    ClientInfo = (PWDMAUD_CLIENT)IoStack->FileObject->FsContext;
    ASSERT(ClientInfo);

    /* get active pin */
    PinId = GetActivePin(ClientInfo);
    ASSERT(PinId != MAXULONG);

    /* get pin file object */
    FileObject = ClientInfo->hPins[PinId].FileObject;
    ASSERT(FileObject);

    /* allocate stream header */
    StreamHeader = AllocateItem(NonPagedPool, sizeof(KSSTREAM_HEADER));
    if (!StreamHeader)
    {
        /* not enough memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    StreamHeader->Size = sizeof(KSSTREAM_HEADER);
    StreamHeader->PresentationTime.Numerator = 1;
    StreamHeader->PresentationTime.Denominator = 1;
    StreamHeader->Data = WaveHeader->lpData;
    StreamHeader->FrameExtent = WaveHeader->dwBufferLength;

    if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        StreamHeader->DataUsed = WaveHeader->dwBufferLength;
    }
    else
    {
        StreamHeader->DataUsed = 0;
    }

    /* store the input buffer in SystemBuffer - as KsProbeStreamIrp operates on IRP_MJ_DEVICE_CONTROL */
    Irp->AssociatedIrp.SystemBuffer = StreamHeader;

    /* sanity check */
    ASSERT(Irp->AssociatedIrp.SystemBuffer);

    /* get the length of the request length */
    Length = StreamHeader->Size;

    /* store outputbuffer length */
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = Length;

    /* allocate completion context */
    Context = AllocateItem(NonPagedPool, sizeof(WDMAUD_COMPLETION_CONTEXT));

    if (!Context)
    {
        /* not enough memory */
        FreeItem(StreamHeader);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* setup context */
    Context->Length = sizeof(KSSTREAM_HEADER);
    Context->Function = DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE ? IOCTL_KS_READ_STREAM : IOCTL_KS_WRITE_STREAM;
    Context->Mdl = Irp->MdlAddress;

    /* store mdl address */
    Mdl = Irp->MdlAddress;

    /* clear mdl address */
    Irp->MdlAddress = NULL;

    if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        /* probe the write stream irp */
        Status = KsProbeStreamIrp(Irp, KSPROBE_STREAMWRITE | KSPROBE_ALLOCATEMDL | KSPROBE_PROBEANDLOCK, Length);
    }
    else
    {
        /* probe the read stream irp */
        Status = KsProbeStreamIrp(Irp, KSPROBE_STREAMREAD | KSPROBE_ALLOCATEMDL | KSPROBE_PROBEANDLOCK, Length);
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("KsProbeStreamIrp failed with Status 0x%lx Cancel %u\n", Status, Irp->Cancel);
        Irp->MdlAddress = Mdl;
        FreeItem(Context);
        FreeItem(StreamHeader);
        return SetIrpIoStatus(Irp, Status, 0);
    }

    /* store file object whose reference is released in the completion callback */
    Context->FileObject = FileObject;

    /* skip current irp stack location */
    IoSkipCurrentIrpStackLocation(Irp);

    /* get next stack location */
    IoStack = IoGetNextIrpStackLocation(Irp);

    /* prepare stack location */
    IoStack->FileObject = FileObject;
    IoStack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = Length;
    IoStack->Parameters.DeviceIoControl.IoControlCode = (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE) ? IOCTL_KS_READ_STREAM : IOCTL_KS_WRITE_STREAM;
    IoSetCompletionRoutine(Irp, IoCompletion, (PVOID)Context, TRUE, TRUE, TRUE);

    /* call the driver */
    IoCallDriver(IoGetRelatedDeviceObject(FileObject), Irp);

    /* done */
    return STATUS_PENDING;
}

NTSTATUS
NTAPI
WdmAudDeviceControl(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PWDMAUD_DEVICE_INFO DeviceInfo;
    PWDMAUD_CLIENT ClientInfo;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    DPRINT("WdmAudDeviceControl entered\n");
    DPRINT("IOCTL 0x%lx\n", IoStack->Parameters.DeviceIoControl.IoControlCode);

    DeviceInfo = (PWDMAUD_DEVICE_INFO)Irp->AssociatedIrp.SystemBuffer;
    if (DeviceInfo->DeviceType < MIN_SOUND_DEVICE_TYPE || DeviceInfo->DeviceType > MAX_SOUND_DEVICE_TYPE)
    {
        /* invalid parameter */
        DPRINT1("Error: device type not set\n");
        return SetIrpIoStatus(Irp, STATUS_INVALID_PARAMETER, 0);
    }

    if (!IoStack->FileObject || !IoStack->FileObject->FsContext)
    {
        /* file object parameter */
        DPRINT1("Error: file object is not attached\n");
        return SetIrpIoStatus(Irp, STATUS_UNSUCCESSFUL, 0);
    }
    ClientInfo = (PWDMAUD_CLIENT)IoStack->FileObject->FsContext;

    switch(IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_INIT_WDMAUD:
            return WdmAudControlInitialize(DeviceObject, Irp);
        case IOCTL_EXIT_WDMAUD:
            /* No op */
            return SetIrpIoStatus(Irp, STATUS_SUCCESS, sizeof(WDMAUD_DEVICE_INFO));
        case IOCTL_OPEN_WDMAUD:
        case IOCTL_OPEN_MIXER:
            return WdmAudControlOpen(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETNUMDEVS_TYPE:
            return WdmAudControlDeviceType(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETCAPABILITIES:
            return WdmAudCapabilities(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_CLOSE_WDMAUD:
        case IOCTL_CLOSE_MIXER:
            return WdmAudIoctlClose(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GET_MIXER_EVENT:
            return WdmAudGetMixerEvent(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETLINEINFO:
            return WdmAudGetLineInfo(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETLINECONTROLS:
            return WdmAudGetLineControls(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_SETCONTROLDETAILS:
            return WdmAudSetControlDetails(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETCONTROLDETAILS:
            return WdmAudGetControlDetails(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_RESET_CAPTURE:
            return WdmAudSetDeviceState(DeviceObject, Irp, DeviceInfo, KSSTATE_STOP, TRUE);
        case IOCTL_RESET_PLAYBACK:
            return WdmAudResetStream(DeviceObject, Irp, DeviceInfo, TRUE);
        case IOCTL_GETINPOS:
        case IOCTL_GETOUTPOS:
            return WdmAudGetPosition(DeviceObject, Irp, DeviceInfo);
        case IOCTL_PAUSE_CAPTURE:
            return WdmAudSetDeviceState(DeviceObject, Irp, DeviceInfo, KSSTATE_STOP, TRUE);
        case IOCTL_PAUSE_PLAYBACK:
            return WdmAudSetDeviceState(DeviceObject, Irp, DeviceInfo, KSSTATE_PAUSE, TRUE);
        case IOCTL_START_CAPTURE:
        case IOCTL_START_PLAYBACK:
            return WdmAudSetDeviceState(DeviceObject, Irp, DeviceInfo, KSSTATE_RUN, TRUE);
        case IOCTL_READDATA:
        case IOCTL_WRITEDATA:
            return WdmAudReadWrite(DeviceObject, Irp);
        case IOCTL_ADD_DEVNODE:
        case IOCTL_REMOVE_DEVNODE:
        case IOCTL_GETVOLUME:
        case IOCTL_SETVOLUME:

        default:
           DPRINT1("Unhandled 0x%x\n", IoStack->Parameters.DeviceIoControl.IoControlCode);
           break;
    }

    return SetIrpIoStatus(Irp, STATUS_NOT_IMPLEMENTED, 0);
}
