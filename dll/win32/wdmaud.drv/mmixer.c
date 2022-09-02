/*
 * PROJECT:     ReactOS Audio Subsystem
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDM Audio Mixer API (User-mode part)
 * COPYRIGHT:   Copyright 2009-2010 Johannes Anderwald
 *              Copyright 2022 Oleg Dubinskiy (oleg.dubinskij30@gmail.com)
 */

#include "wdmaud.h"

#include <winreg.h>
#include <setupapi.h>
#include <mmixer.h>

#define NDEBUG
#include <debug.h>


BOOL MMixerLibraryInitialized = FALSE;

PVOID Alloc(ULONG NumBytes);
MIXER_STATUS Close(HANDLE hDevice);
VOID Free(PVOID Block);
VOID Copy(PVOID Src, PVOID Dst, ULONG NumBytes);
MIXER_STATUS Open(_In_ LPWSTR DevicePath, _Out_ PHANDLE hDevice);
MIXER_STATUS Control(_In_ HANDLE hMixer, _In_ ULONG dwIoControlCode, _In_ PVOID lpInBuffer, _In_ ULONG nInBufferSize, _Out_ PVOID lpOutBuffer, ULONG nOutBufferSize, PULONG lpBytesReturned);
MIXER_STATUS Enum(_In_  PVOID EnumContext, _In_  ULONG DeviceIndex, _Out_ LPWSTR * DeviceName, _Out_ PHANDLE OutHandle, _Out_ PHANDLE OutKey);
MIXER_STATUS OpenKey(_In_ HANDLE hKey, _In_ LPWSTR SubKey, _In_ ULONG DesiredAccess, _Out_ PHANDLE OutKey);
MIXER_STATUS CloseKey(_In_ HANDLE hKey);
MIXER_STATUS QueryKeyValue(_In_ HANDLE hKey, _In_ LPWSTR KeyName, _Out_ PVOID * ResultBuffer, _Out_ PULONG ResultLength, _Out_ PULONG KeyType);
PVOID AllocEventData(_In_ ULONG ExtraSize);
VOID FreeEventData(_In_ PVOID EventData);

MIXER_CONTEXT MixerContext =
{
    sizeof(MIXER_CONTEXT),
    NULL,
    Alloc,
    Control,
    Free,
    Open,
    Close,
    Copy,
    OpenKey,
    QueryKeyValue,
    CloseKey,
    AllocEventData,
    FreeEventData
};

GUID CategoryGuid = {STATIC_KSCATEGORY_AUDIO};

MIXER_STATUS
QueryKeyValue(
    _In_ HANDLE hKey,
    _In_ LPWSTR KeyName,
    _Out_ PVOID * ResultBuffer,
    _Out_ PULONG ResultLength,
    _Out_ PULONG KeyType)
{
    if (RegQueryValueExW((HKEY)hKey, KeyName, NULL, KeyType, NULL, ResultLength) == ERROR_FILE_NOT_FOUND)
        return MM_STATUS_UNSUCCESSFUL;

    *ResultBuffer = HeapAlloc(GetProcessHeap(), 0, *ResultLength);
    if (*ResultBuffer == NULL)
        return MM_STATUS_NO_MEMORY;

    if (RegQueryValueExW((HKEY)hKey, KeyName, NULL, KeyType, *ResultBuffer, ResultLength) != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, *ResultBuffer);
        return MM_STATUS_UNSUCCESSFUL;
    }
    return MM_STATUS_SUCCESS;
}

MIXER_STATUS
OpenKey(
    _In_ HANDLE hKey,
    _In_ LPWSTR SubKey,
    _In_ ULONG DesiredAccess,
    _Out_ PHANDLE OutKey)
{
    if (RegOpenKeyExW((HKEY)hKey, SubKey, 0, DesiredAccess, (PHKEY)OutKey) == ERROR_SUCCESS)
        return MM_STATUS_SUCCESS;

    return MM_STATUS_UNSUCCESSFUL;
}

MIXER_STATUS
CloseKey(
    _In_ HANDLE hKey)
{
    RegCloseKey((HKEY)hKey);
    return MM_STATUS_SUCCESS;
}


PVOID Alloc(ULONG NumBytes)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NumBytes);
}

MIXER_STATUS
Close(HANDLE hDevice)
{
    if (CloseHandle(hDevice))
        return MM_STATUS_SUCCESS;
    else
        return MM_STATUS_UNSUCCESSFUL;
}

VOID
Free(PVOID Block)
{
    HeapFree(GetProcessHeap(), 0, Block);
}

VOID
Copy(PVOID Src, PVOID Dst, ULONG NumBytes)
{
    RtlMoveMemory(Src, Dst, NumBytes);
}

MIXER_STATUS
Open(
    _In_ LPWSTR DevicePath,
    _Out_ PHANDLE hDevice)
{
     DevicePath[1] = L'\\';
    *hDevice = CreateFileW(DevicePath,
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                           NULL);
    if (*hDevice == INVALID_HANDLE_VALUE)
    {
        return MM_STATUS_UNSUCCESSFUL;
    }

    return MM_STATUS_SUCCESS;
}

MIXER_STATUS
Control(
    _In_ HANDLE hMixer,
    _In_ ULONG dwIoControlCode,
    _In_ PVOID lpInBuffer,
    _In_ ULONG nInBufferSize,
    _Out_ PVOID lpOutBuffer,
    ULONG nOutBufferSize,
    PULONG lpBytesReturned)
{
    OVERLAPPED Overlapped;
    BOOLEAN IoResult;
    DWORD Transferred = 0;

    /* Overlapped I/O is done here - this is used for waiting for completion */
    ZeroMemory(&Overlapped, sizeof(OVERLAPPED));
    Overlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if ( ! Overlapped.hEvent )
        return MM_STATUS_NO_MEMORY;

    /* Talk to the device */
    IoResult = DeviceIoControl(hMixer,
                               dwIoControlCode,
                               lpInBuffer,
                               nInBufferSize,
                               lpOutBuffer,
                               nOutBufferSize,
                               &Transferred,
                               &Overlapped);

    /* If failure occurs, make sure it's not just due to the overlapped I/O */
    if ( ! IoResult )
    {
        if ( GetLastError() != ERROR_IO_PENDING )
        {
            CloseHandle(Overlapped.hEvent);

            if (GetLastError() == ERROR_MORE_DATA || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                if ( lpBytesReturned )
                    *lpBytesReturned = Transferred;
                return MM_STATUS_MORE_ENTRIES;
            }

            return MM_STATUS_UNSUCCESSFUL;
        }
        else
        {
            DPRINT("Waiting...\n");
            WaitForSingleObject(Overlapped.hEvent, INFINITE);
        }
    }

    /* Wait for the I/O to complete */
    IoResult = GetOverlappedResult(hMixer,
                                   &Overlapped,
                                   &Transferred,
                                   TRUE);

    /* Don't need this any more */
    CloseHandle(Overlapped.hEvent);

    if ( ! IoResult )
        return MM_STATUS_UNSUCCESSFUL;

    if ( lpBytesReturned )
        *lpBytesReturned = Transferred;

    return MM_STATUS_SUCCESS;
}

MIXER_STATUS
Enum(
    _In_  PVOID EnumContext,
    _In_  ULONG DeviceIndex,
    _Out_ LPWSTR * DeviceName,
    _Out_ PHANDLE OutHandle,
    _Out_ PHANDLE OutKey)
{
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    SP_DEVINFO_DATA DeviceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W DetailData;
    BOOL Result;
    DWORD Length;
    MIXER_STATUS Status;

    //printf("Enum EnumContext %p DeviceIndex %lu OutHandle %p\n", EnumContext, DeviceIndex, OutHandle);

    InterfaceData.cbSize = sizeof(InterfaceData);
    InterfaceData.Reserved = 0;

    Result = SetupDiEnumDeviceInterfaces(EnumContext,
                                         NULL,
                                         &CategoryGuid,
                                         DeviceIndex,
                                         &InterfaceData);

    if (!Result)
    {
        if (GetLastError() == ERROR_NO_MORE_ITEMS)
        {
            return MM_STATUS_NO_MORE_DEVICES;
        }
        return MM_STATUS_UNSUCCESSFUL;
    }

    Length = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + MAX_PATH * sizeof(WCHAR);
    DetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(GetProcessHeap(),
                                                             0,
                                                             Length);
    DetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    DeviceData.cbSize = sizeof(DeviceData);
    DeviceData.Reserved = 0;

    Result = SetupDiGetDeviceInterfaceDetailW(EnumContext,
                                    &InterfaceData,
                                    DetailData,
                                    Length,
                                    NULL,
                                    &DeviceData);

    if (!Result)
    {
        DPRINT("SetupDiGetDeviceInterfaceDetailW failed with %lu\n", GetLastError());
        return MM_STATUS_UNSUCCESSFUL;
    }


    *OutKey = SetupDiOpenDeviceInterfaceRegKey(EnumContext, &InterfaceData, 0, KEY_READ);
     if ((HKEY)*OutKey == INVALID_HANDLE_VALUE)
     {
        HeapFree(GetProcessHeap(), 0, DetailData);
        return MM_STATUS_UNSUCCESSFUL;
    }

    Status = Open(DetailData->DevicePath, OutHandle);

    if (Status != MM_STATUS_SUCCESS)
    {
        RegCloseKey((HKEY)*OutKey);
        HeapFree(GetProcessHeap(), 0, DetailData);
        return Status;
    }

    *DeviceName = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, (wcslen(DetailData->DevicePath)+1) * sizeof(WCHAR));
    if (*DeviceName == NULL)
    {
        CloseHandle(*OutHandle);
        RegCloseKey((HKEY)*OutKey);
        HeapFree(GetProcessHeap(), 0, DetailData);
        return MM_STATUS_NO_MEMORY;
    }
    wcscpy(*DeviceName, DetailData->DevicePath);
    HeapFree(GetProcessHeap(), 0, DetailData);

    return Status;
}

PVOID
AllocEventData(
    _In_ ULONG ExtraSize)
{
    PKSEVENTDATA Data = (PKSEVENTDATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(KSEVENTDATA) + ExtraSize);
    if (!Data)
        return NULL;

    Data->EventHandle.Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!Data->EventHandle.Event)
    {
        HeapFree(GetProcessHeap(), 0, Data);
        return NULL;
    }

    Data->NotificationType = KSEVENTF_EVENT_HANDLE;
    return Data;
}

VOID
FreeEventData(_In_ PVOID EventData)
{
    PKSEVENTDATA Data = (PKSEVENTDATA)EventData;

    CloseHandle(Data->EventHandle.Event);
    HeapFree(GetProcessHeap(), 0, Data);
}


BOOL
WdmAudInitUserModeMixer()
{
    HDEVINFO DeviceHandle;
    MIXER_STATUS Status;

    if (MMixerLibraryInitialized)
    {
        /* library is already initialized */
        return TRUE;
    }


    /* create a device list */
    DeviceHandle = SetupDiGetClassDevs(&CategoryGuid,
                                       NULL,
                                       NULL,
                                       DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

    if (DeviceHandle == INVALID_HANDLE_VALUE)
    {
        /* failed to create a device list */
        return FALSE;
    }


    /* initialize the mixer library */
    Status = MMixerInitialize(&MixerContext, Enum, (PVOID)DeviceHandle);

    /* free device list */
    SetupDiDestroyDeviceInfoList(DeviceHandle);

    if (Status != MM_STATUS_SUCCESS)
    {
        /* failed to initialize mixer library */
        DPRINT1("Failed to initialize mixer library with %x\n", Status);
        return FALSE;
    }

    /* library is now initialized */
    MMixerLibraryInitialized = TRUE;

    /* completed successfully */
    return TRUE;
}

MMRESULT
WdmAudCleanupByMMixer(VOID)
{
    /* TODO */
    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudGetMixerCapabilities(
    _In_ ULONG DeviceId,
    _Out_ LPMIXERCAPSW Capabilities)
{
    if (MMixerGetCapabilities(&MixerContext, DeviceId, Capabilities) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_BADDEVICEID;
}

MMRESULT
WdmAudGetLineInfo(
    _In_ HANDLE hMixer,
    _In_ DWORD MixerId,
    _In_ LPMIXERLINEW MixLine,
    _In_ ULONG Flags)
{
    if (MMixerGetLineInfo(&MixerContext, hMixer, MixerId, Flags, MixLine)  == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudGetLineControls(
    _In_ HANDLE hMixer,
    _In_ DWORD MixerId,
    _In_ LPMIXERLINECONTROLSW MixControls,
    _In_ ULONG Flags)
{
    if (MMixerGetLineControls(&MixerContext, hMixer, MixerId, Flags, MixControls) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudSetControlDetails(
    _In_ HANDLE hMixer,
    _In_ DWORD MixerId,
    _In_ LPMIXERCONTROLDETAILS MixDetails,
    _In_ ULONG Flags)
{
    if (MMixerSetControlDetails(&MixerContext, hMixer, MixerId, Flags, MixDetails) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;

}

MMRESULT
WdmAudGetControlDetails(
    _In_ HANDLE hMixer,
    _In_ DWORD MixerId,
    _In_ LPMIXERCONTROLDETAILS MixDetails,
    _In_ ULONG Flags)
{
    if (MMixerGetControlDetails(&MixerContext, hMixer, MixerId, Flags, MixDetails) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudGetMidiOutCapabilities(
    _In_ ULONG DeviceId,
    _Out_ LPMIDIOUTCAPSW Capabilities)
{
    if (MMixerMidiOutCapabilities(&MixerContext, DeviceId, Capabilities) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;

}

MMRESULT
WdmAudGetMidiInCapabilities(
    _In_ ULONG DeviceId,
    _Out_ LPMIDIINCAPSW Capabilities)
{
    if (MMixerMidiInCapabilities(&MixerContext, DeviceId, Capabilities) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;
}


MMRESULT
WdmAudGetWaveOutCapabilities(
    _In_ ULONG DeviceId,
    _Out_ LPWAVEOUTCAPSW Capabilities)
{
    if (MMixerWaveOutCapabilities(&MixerContext, DeviceId, Capabilities) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;

}

MMRESULT
WdmAudGetWaveInCapabilities(
    _In_ ULONG DeviceId,
    _Out_ LPWAVEINCAPSW Capabilities)
{
    if (MMixerWaveInCapabilities(&MixerContext, DeviceId, Capabilities) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudOpenSoundDeviceByMMixer(
    _In_  PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_  PWAVEFORMATEX WaveFormat)
{
    BOOL bInput;

    if (DeviceInfo->DeviceType == MIXER_DEVICE_TYPE)
    {
        if (MMixerOpen(&MixerContext, DeviceInfo->DeviceIndex, &DeviceInfo->hDevice) == MM_STATUS_SUCCESS)
        {
            return MMSYSERR_NOERROR;
        }
    }
    else if (DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE || DeviceInfo->DeviceType == MIDI_OUT_DEVICE_TYPE)
    {
        bInput = DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE;

        if (MMixerOpenMidi(&MixerContext, DeviceInfo->DeviceIndex, bInput, NULL, NULL, &DeviceInfo->hDevice) == MM_STATUS_SUCCESS)
        {
            return MMSYSERR_NOERROR;
        }
    }
    else if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        bInput = DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE;

        if (MMixerOpenWave(&MixerContext, DeviceInfo->DeviceIndex, bInput, WaveFormat, NULL, NULL, &DeviceInfo->hDevice) == MM_STATUS_SUCCESS)
        {
            MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_ACQUIRE);
            MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_PAUSE);
            MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_RUN);

#ifdef RESAMPLING_ENABLED
            DeviceInfo->Buffer = WaveFormat;
            DeviceInfo->BufferSize = sizeof(WAVEFORMATEX);
#endif

            return MMSYSERR_NOERROR;
        }
    }
    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudGetCapabilitiesByMMixer(
    _In_  PWDMAUD_DEVICE_INFO DeviceInfo,
    _Out_ MDEVICECAPSEX* Capabilities)
{
    if (DeviceInfo->DeviceType == MIXER_DEVICE_TYPE)
    {
        return WdmAudGetMixerCapabilities(DeviceInfo->DeviceIndex, (LPMIXERCAPSW)Capabilities);
    }
    else if (DeviceInfo->DeviceType == MIDI_OUT_DEVICE_TYPE)
    {
        return WdmAudGetMidiOutCapabilities(DeviceInfo->DeviceIndex, (LPMIDIOUTCAPSW)Capabilities);
    }
    else if (DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE)
    {
        return WdmAudGetMidiInCapabilities(DeviceInfo->DeviceIndex, (LPMIDIINCAPSW)Capabilities);
    }
    else if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        return WdmAudGetWaveOutCapabilities(DeviceInfo->DeviceIndex, (LPWAVEOUTCAPSW)Capabilities);
    }
    else if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE)
    {
        return WdmAudGetWaveInCapabilities(DeviceInfo->DeviceIndex, (LPWAVEINCAPSW)Capabilities);
    }
    else
    {
        // not supported
        return MMSYSERR_NOTSUPPORTED;
    }
}

MMRESULT
WdmAudOpenKernelSoundDeviceByMMixer(VOID)
{
    if (WdmAudInitUserModeMixer())
        return MMSYSERR_NOERROR;
    else
        return MMSYSERR_ERROR;
}

MMRESULT
WdmAudCloseSoundDeviceByMMixer(
    _In_  PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_  PVOID Handle)
{
    MMRESULT Result;

    if (DeviceInfo->DeviceType == MIXER_DEVICE_TYPE)
    {
        CloseHandle(DeviceInfo->hDevice);
        return MMSYSERR_NOERROR;
    }
    else if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        /* Destroy I/O thread */
        Result = WdmAudDestroyCompletionThread(DeviceInfo);
        ASSERT(Result == MMSYSERR_NOERROR);

        /* make sure the pin is stopped */
        MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_PAUSE);
        MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_ACQUIRE);
        MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_STOP);

        CloseHandle(DeviceInfo->hDevice);
        return MMSYSERR_NOERROR;
    }

    /* midi is not supported */
    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudGetNumWdmDevsByMMixer(
    _In_  SOUND_DEVICE_TYPE DeviceType,
    _In_ LPCWSTR DeviceInterface,
    _Out_ DWORD* DeviceCount)
{
    switch(DeviceType)
    {
        case MIXER_DEVICE_TYPE:
            *DeviceCount = MMixerGetCount(&MixerContext);
            break;
        case WAVE_OUT_DEVICE_TYPE:
            *DeviceCount = MMixerGetWaveOutCount(&MixerContext);
            break;
        case WAVE_IN_DEVICE_TYPE:
            *DeviceCount = MMixerGetWaveInCount(&MixerContext);
            break;
        default:
            *DeviceCount = 0;
    }
    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudQueryMixerInfoByMMixer(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ DWORD MixerId,
    _In_ UINT uMsg,
    _In_ LPVOID Parameter,
    _In_ DWORD Flags)
{
    LPMIXERLINEW MixLine;
    LPMIXERLINECONTROLSW MixControls;
    LPMIXERCONTROLDETAILS MixDetails;

    MixLine = (LPMIXERLINEW)Parameter;
    MixControls = (LPMIXERLINECONTROLSW)Parameter;
    MixDetails = (LPMIXERCONTROLDETAILS)Parameter;

    /* FIXME param checks */

    switch(uMsg)
    {
        case MXDM_GETLINEINFO:
            return WdmAudGetLineInfo(DeviceInfo->hDevice, MixerId, MixLine, Flags);
        case MXDM_GETLINECONTROLS:
            return WdmAudGetLineControls(DeviceInfo->hDevice, MixerId, MixControls, Flags);
        case MXDM_SETCONTROLDETAILS:
            return WdmAudSetControlDetails(DeviceInfo->hDevice, MixerId, MixDetails, Flags);
        case MXDM_GETCONTROLDETAILS:
            return WdmAudGetControlDetails(DeviceInfo->hDevice, MixerId, MixDetails, Flags);
        default:
            DPRINT1("MixerId %lu, uMsg %lu, Parameter %p, Flags %lu\n", MixerId, uMsg, Parameter, Flags);
            ASSERT(0);
            return MMSYSERR_NOTSUPPORTED;
    }
}

MMRESULT
WdmAudSetWaveStateByMMixer(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ BOOL bStart)
{
    MMRESULT Result;

    /* Are we requested to start the audio device? */
    if (bStart)
    {
        /* Create I/O thread */
        Result = WdmAudCreateCompletionThread(DeviceInfo);
        if (!MMSUCCESS(Result))
        {
            /* Failed */
            DPRINT1("Failed to create sound thread with error %d\n", GetLastError());
            return TranslateInternalMmResult(Result);
        }

        /* Mark device as started */
        DeviceInfo->DeviceState->bStart = TRUE;
    }

    if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        if (bStart)
        {
            MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_ACQUIRE);
            MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_PAUSE);
            MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_RUN);
        }
        else
        {
            MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_PAUSE);
            MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_ACQUIRE);
            MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_STOP);
        }
    }
    else if (DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE || DeviceInfo->DeviceType == MIDI_OUT_DEVICE_TYPE)
    {
        if (bStart)
        {
            MMixerSetMidiStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_ACQUIRE);
            MMixerSetMidiStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_PAUSE);
            MMixerSetMidiStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_RUN);
        }
        else
        {
            MMixerSetMidiStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_PAUSE);
            MMixerSetMidiStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_ACQUIRE);
            MMixerSetMidiStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_STOP);
        }
    }

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudResetStreamByMMixer(
    _In_  PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_  BOOL bStartReset)
{
    MIXER_STATUS Status;
    MMRESULT Result;

    if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        Status = MMixerSetWaveResetState(&MixerContext, DeviceInfo->hDevice, bStartReset);
        if (Status == MM_STATUS_SUCCESS)
        {
            /* Mark device as stopped */
            DeviceInfo->DeviceState->bStart = FALSE;

            /* Destroy I/O thread */
            Result = WdmAudDestroyCompletionThread(DeviceInfo);
            ASSERT(Result == MMSYSERR_NOERROR);

            /* Completed successfully */
            return MMSYSERR_NOERROR;
        }
    }

    return MMSYSERR_NOTSUPPORTED;
}

MMRESULT
WdmAudGetWavePositionByMMixer(
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  MMTIME* Time)
{
    MIXER_STATUS Status;
    DWORD Position;

    if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        Status = MMixerGetWavePosition(&MixerContext, DeviceInfo->hDevice, &Position);
        if (Status == MM_STATUS_SUCCESS)
        {
            /* Store position */
            Time->wType = TIME_BYTES;
            Time->u.cb = Position;

            /* Completed successfully */
            return MMSYSERR_NOERROR;
        }
    }
    return MMSYSERR_NOTSUPPORTED;
}

MMRESULT
WdmAudGetVolumeByMMixer(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ DWORD DeviceId,
    _Out_ PDWORD pdwVolume)
{
    MMRESULT Result;
    MIXERLINE MixLine;
    MIXERCONTROL MixControl;
    MIXERLINECONTROLS MixLineControls;
    MIXERCONTROLDETAILS MixControlDetails;
    MIXERCONTROLDETAILS_UNSIGNED MixControlDetailsU[2]; // For 2 (stereo) channels

    MixLine.cbStruct = sizeof(MixLine);
    MixLine.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;

    /* Get line info */
    Result = WdmAudGetLineInfo(DeviceInfo->hDevice,
                               DeviceId,
                               &MixLine,
                               MIXER_OBJECTF_MIXER | MIXER_GETLINEINFOF_COMPONENTTYPE);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    MixLineControls.cbStruct = sizeof(MixLineControls);
    MixLineControls.dwLineID = MixLine.dwLineID;
    MixLineControls.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    MixLineControls.cControls = 1;
    MixLineControls.cbmxctrl = sizeof(MixControl);
    MixLineControls.pamxctrl = &MixControl;

    /* Get line controls */
    Result = WdmAudGetLineControls(DeviceInfo->hDevice,
                                   DeviceId,
                                   &MixLineControls,
                                   MIXER_OBJECTF_MIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    MixControlDetails.cbStruct = sizeof(MixControlDetails);
    MixControlDetails.dwControlID = MixControl.dwControlID;
    MixControlDetails.cChannels = MixLine.cChannels;
    MixControlDetails.cMultipleItems = 0;
    MixControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
    MixControlDetails.paDetails = MixControlDetailsU;

    /* Get volume control details */
    Result = WdmAudGetControlDetails(DeviceInfo->hDevice,
                                     DeviceId,
                                     &MixControlDetails,
                                     MIXER_OBJECTF_MIXER);
    if (MMSUCCESS(Result))
        *pdwVolume = MAKELONG(LOWORD(MixControlDetailsU[0].dwValue), HIWORD(MixControlDetailsU[1].dwValue));

    return Result;
}

MMRESULT
WdmAudSetVolumeByMMixer(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ DWORD DeviceId,
    _In_ DWORD dwVolume)
{
    MMRESULT Result;
    MIXERLINE MixLine;
    MIXERCONTROL MixControl;
    MIXERLINECONTROLS MixLineControls;
    MIXERCONTROLDETAILS MixControlDetails;
    MIXERCONTROLDETAILS_UNSIGNED MixControlDetailsU[2]; // For 2 (stereo) channels

    MixLine.cbStruct = sizeof(MixLine);
    MixLine.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;

    /* Get line info */
    Result = WdmAudGetLineInfo(DeviceInfo->hDevice,
                               DeviceId,
                               &MixLine,
                               MIXER_OBJECTF_MIXER | MIXER_GETLINEINFOF_COMPONENTTYPE);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    MixLineControls.cbStruct = sizeof(MixLineControls);
    MixLineControls.dwLineID = MixLine.dwLineID;
    MixLineControls.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    MixLineControls.cControls = 1;
    MixLineControls.cbmxctrl = sizeof(MixControl);
    MixLineControls.pamxctrl = &MixControl;

    /* Get line controls */
    Result = WdmAudGetLineControls(DeviceInfo->hDevice,
                                   DeviceId,
                                   &MixLineControls,
                                   MIXER_OBJECTF_MIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    /* Convert volume level to be set */
    MixControlDetailsU[0].dwValue = LOWORD(dwVolume); // Left channel
    MixControlDetailsU[1].dwValue = HIWORD(dwVolume); // Right channel

    MixControlDetails.cbStruct = sizeof(MixControlDetails);
    MixControlDetails.dwControlID = MixControl.dwControlID;
    MixControlDetails.cChannels = MixLine.cChannels;
    MixControlDetails.cMultipleItems = 0;
    MixControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
    MixControlDetails.paDetails = MixControlDetailsU;

    /* Set volume control details */
    Result = WdmAudSetControlDetails(DeviceInfo->hDevice,
                                     DeviceId,
                                     &MixControlDetails,
                                     MIXER_OBJECTF_MIXER);
    return Result;
}

MMRESULT
WdmAudSubmitWaveHeaderByMMixer(
    _In_  PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_  PWAVEHDR WaveHeader)
{
    MMRESULT Result = MMSYSERR_NOERROR;
    PWAVEHDR_EXTENSION HeaderExtension;
    PKSSTREAM_HEADER lpHeader;
    DWORD Transferred = 0;
    BOOL IoResult;
    DWORD IoCtl;

#ifdef RESAMPLING_ENABLED
    /* Resample the stream */
    Result = WdmAudResampleStream(DeviceInfo, WaveHeader);
    ASSERT( Result == MMSYSERR_NOERROR );
#endif

    lpHeader = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(KSSTREAM_HEADER));
    if ( ! lpHeader )
    {
        /* No memory */
        return MMSYSERR_NOMEM;
    }

    HeaderExtension = (PWAVEHDR_EXTENSION)WaveHeader->reserved;
    HeaderExtension->DeviceInfo = NULL;

    /* Setup stream header */
    lpHeader->Size = sizeof(KSSTREAM_HEADER);
    lpHeader->PresentationTime.Numerator = 1;
    lpHeader->PresentationTime.Denominator = 1;
    lpHeader->Data = WaveHeader->lpData;
    lpHeader->FrameExtent = WaveHeader->dwBufferLength;

    if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        lpHeader->DataUsed = WaveHeader->dwBufferLength;
    }
    else
    {
        lpHeader->DataUsed = 0;
    }

    /* Set IOCTL */
    if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
        IoCtl = IOCTL_KS_WRITE_STREAM;
    else if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE)
        IoCtl = IOCTL_KS_READ_STREAM;

    /* Talk to the device */
    IoResult = DeviceIoControl(DeviceInfo->hDevice,
                               IoCtl,
                               NULL,
                               0,
                               lpHeader,
                               sizeof(KSSTREAM_HEADER),
                               &Transferred,
                               &HeaderExtension->Overlapped);
    HeapFree(GetProcessHeap(), 0, lpHeader);

    /* If failure occurs, make sure it's not just due to the overlapped I/O */
    if (!IoResult)
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            DPRINT1("Call to %s failed with %d\n",
                    DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
                    "IOCTL_KS_WRITE_STREAM" : "IOCTL_KS_READ_STREAM",
                    GetLastError());
        }
        Result = Win32ErrorToMmResult(GetLastError());
    }

    if (MMSUCCESS(Result))
    {
        /* Create I/O thread */
        Result = WdmAudCreateCompletionThread(DeviceInfo);
        if (!MMSUCCESS(Result))
        {
            /* Failed */
            DPRINT1("Failed to create sound thread with error %d\n", GetLastError());
            return TranslateInternalMmResult(Result);
        }
    }

    DPRINT("Transferred %d bytes in Sync overlapped I/O\n", Transferred);

    return Result;
}
