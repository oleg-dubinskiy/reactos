/*
 * PROJECT:     ReactOS Audio Subsystem
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDM Audio Driver Mapper (User-mode part)
 * COPYRIGHT:   Copyright 2007-2008 Andrew Greenwood (silverblade@reactos.org)
 *              Copyright 2009-2010 Johannes Anderwald
 *              Copyright 2022-2024 Oleg Dubinskiy (oleg.dubinskij30@gmail.com)
 */

#include "wdmaud.h"

#include <mmixer.h>

#define NDEBUG
#include <debug.h>

extern MIXER_CONTEXT MixerContext;
HANDLE KernelHandle = INVALID_HANDLE_VALUE;
DWORD OpenCount = 0;

/*
    This is a wrapper around DeviceIoControl which provides control over
    instantiated sound devices. It waits for I/O to complete (since an
    instantiated sound device is opened _In_ overlapped mode, this is necessary).
*/
MMRESULT
WdmAudIoControl(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_opt_ ULONG BufferSize,
    _In_opt_ PVOID Buffer,
    _In_ DWORD IoControlCode)
{
    MMRESULT Result = MMSYSERR_NOERROR;
    OVERLAPPED Overlapped;
    DWORD Transferred = 0;
    BOOL IoResult;

    /* Overlapped I/O is done here - this is used for waiting for completion */
    ZeroMemory(&Overlapped, sizeof(OVERLAPPED));
    Overlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if ( ! Overlapped.hEvent )
        return Win32ErrorToMmResult(GetLastError());

    /* Store input data */
    DeviceInfo->BufferSize = BufferSize;
    DeviceInfo->Buffer = Buffer;

    /* Talk to the device */
    IoResult = DeviceIoControl(KernelHandle,
                               IoControlCode,
                               DeviceInfo,
                               sizeof(WDMAUD_DEVICE_INFO)
                               + (lstrlenW(DeviceInfo->DeviceInterfaceString) + 1) * sizeof(WCHAR),
                               DeviceInfo,
                               sizeof(WDMAUD_DEVICE_INFO),
                               &Transferred,
                               &Overlapped);

    /* If failure occurs, make sure it's not just due to the overlapped I/O */
    if ( ! IoResult )
    {
        if (GetLastError() == ERROR_IO_PENDING)
        {
            /* Wait for I/O complete */
            WaitForSingleObject(Overlapped.hEvent, INFINITE);
        }
        Result = Win32ErrorToMmResult(GetLastError());
    }

    /* Don't need this any more */
    CloseHandle(Overlapped.hEvent);

    DPRINT("Transferred %d bytes in Sync overlapped I/O\n", Transferred);

    return Result;
}

DWORD
WINAPI
WaveThreadRoutine(
    LPVOID Parameter)
{
    PWDMAUD_DEVICE_INFO DeviceInfo = (PWDMAUD_DEVICE_INFO)Parameter;
    PWAVEHDR_EXTENSION HeaderExtension;
    DWORD dwResult = WAIT_FAILED;
    PWAVEHDR WaveHeader;
#ifndef USE_MMIXER_LIB
    MMRESULT Result;
#endif
    HANDLE hEvent;

    while (TRUE)
    {
        DPRINT("WaveQueue %p\n", DeviceInfo->DeviceState->WaveQueue);
        /* Validate the header */
        WaveHeader = DeviceInfo->DeviceState->WaveQueue;
        if (WaveHeader)
        {
            DPRINT("1\n");
            /* Validate the flags */
            if (WaveHeader->dwFlags & (WHDR_INQUEUE | WHDR_PREPARED))
            {
                DPRINT("2\n");
                /* Validate header extension */
                HeaderExtension = (PWAVEHDR_EXTENSION)WaveHeader->reserved;
                if (HeaderExtension)
                {
                    hEvent = HeaderExtension->Overlapped.hEvent;
                    DPRINT("3\n");

                    if (hEvent)
                    {
                        /* Wait for I/O complete */
                        dwResult = WaitForSingleObject(hEvent, INFINITE);
                        DPRINT("dwResult %d\n", dwResult);
                    }

                    DPRINT("4\n");
                    if (DeviceInfo)
                    {
                        /* Complete current header */
                        CompleteWaveHeader(DeviceInfo);
                    }
                    else
                    {
                        /* Shouldn't be called */
                        DPRINT1("Invalid device info data %p\n", DeviceInfo);
                        ASSERT(FALSE);
                        break;
                    }
                }
                else
                {
                    /* Shouldn't be called */
                    DPRINT("5\n");
                    ASSERT(FALSE);
                    break;
                }
            }
            else
            {
                /* Shouldn't be called */
                DPRINT("6\n");
                ASSERT(FALSE);
                break;
            }
        }
        else
        {
            DPRINT("7\n");
            if (DeviceInfo->DeviceState->bStart)
            {
#ifdef USE_MMIXER_LIB
                /* Make sure the pin is stopped */
                MMixerSetWaveStatus(&MixerContext, DeviceInfo->hDevice, KSSTATE_STOP);
#else
                /* Stop streaming if no more data to stream */
                Result = WdmAudIoControl(DeviceInfo,
                                         0,
                                         NULL,
                                         DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
                                         IOCTL_PAUSE_PLAYBACK : IOCTL_PAUSE_CAPTURE);
                if (!MMSUCCESS(Result))
                {
                    DPRINT1("Call to %s failed with %d\n",
                            DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
                            "IOCTL_PAUSE_PLAYBACK" : "IOCTL_PAUSE_CAPTURE",
                            GetLastError());
                    break;
                }
#endif
                /* Mark device as stopped */
                DeviceInfo->DeviceState->bStart = FALSE;
		    }

            WaitForSingleObject(DeviceInfo->DeviceState->hNotifyEvent, INFINITE);

            /* Streaming is finished */
            if (DeviceInfo->DeviceState->bDone) break;
        }
    }

    /* Close queue notification event */
    CloseHandle(DeviceInfo->DeviceState->hNotifyEvent);
    DeviceInfo->DeviceState->bStartInThread = FALSE;

    /* Signal stop event */
    SetEvent(DeviceInfo->DeviceState->hStopEvent);

    /* Done */
    return 0;
}

MMRESULT
WdmAudCreateCompletionThread(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo)
{
    if (!DeviceInfo->DeviceState->hThread)
    {
        /* Create queue notification event */
        DeviceInfo->DeviceState->hNotifyEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!DeviceInfo->DeviceState->hNotifyEvent)
        {
            DPRINT1("CreateEventW failed with %d\n", GetLastError());
            DeviceInfo->DeviceState->bStartInThread = FALSE;
            return Win32ErrorToMmResult(GetLastError());
        }

        /* Create stop event */
        DeviceInfo->DeviceState->hStopEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!DeviceInfo->DeviceState->hStopEvent)
        {
            DPRINT1("CreateEventW failed with %d\n", GetLastError());
            DeviceInfo->DeviceState->bStartInThread = FALSE;
            /* Close previous handle */
            CloseHandle(DeviceInfo->DeviceState->hNotifyEvent);
            return Win32ErrorToMmResult(GetLastError());
        }

        /* Create sound thread if it isn't yet */
        DeviceInfo->DeviceState->hThread = CreateThread(NULL,
                                                        0,
                                                        WaveThreadRoutine,
                                                        DeviceInfo,
                                                        0,
                                                        NULL);
        if (!DeviceInfo->DeviceState->hThread)
        {
            DPRINT1("CreateThread failed with %d\n", GetLastError());
            DeviceInfo->DeviceState->bStartInThread = FALSE;
            /* Close previous handles */
            CloseHandle(DeviceInfo->DeviceState->hNotifyEvent);
            CloseHandle(DeviceInfo->DeviceState->hStopEvent);
            return Win32ErrorToMmResult(GetLastError());
        }
        /* Set priority for a thread */
        SetThreadPriority(DeviceInfo->DeviceState->hThread, THREAD_PRIORITY_TIME_CRITICAL);
    }

    DeviceInfo->DeviceState->bStartInThread = TRUE;

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudDestroyCompletionThread(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo)
{
    if (DeviceInfo->DeviceState->hThread)
    {
        /* Mark stream as finished */
        DeviceInfo->DeviceState->bDone = TRUE;

        /* Signal the queue */
        if (DeviceInfo->DeviceState->bStartInThread)
            SetEvent(DeviceInfo->DeviceState->hNotifyEvent);

        /* Wait for stop */
        WaitForSingleObject(DeviceInfo->DeviceState->hStopEvent, INFINITE);

        /* Close thread handles */
        CloseHandle(DeviceInfo->DeviceState->hThread);
        CloseHandle(DeviceInfo->DeviceState->hStopEvent);
    }

    /* Restore state */
    DeviceInfo->DeviceState->bDone = FALSE;

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudCleanupByLegacy(VOID)
{
    --OpenCount;
    if ( OpenCount < 1 )
    {
        CloseHandle(KernelHandle);
        KernelHandle = INVALID_HANDLE_VALUE;
    }

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudAddRemoveDeviceNode(
    _In_ SOUND_DEVICE_TYPE DeviceType,
    _In_ LPCWSTR DeviceInterface,
    _In_ BOOL bAdd)
{
    MMRESULT Result;
    PWDMAUD_DEVICE_INFO DeviceInfo;

    VALIDATE_MMSYS_PARAMETER( IS_VALID_SOUND_DEVICE_TYPE(DeviceType) );

    DPRINT("WDMAUD - AddRemoveDeviceNode DeviceType %u\n", DeviceType);

    DeviceInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WDMAUD_DEVICE_INFO)
                                                               + (lstrlenW(DeviceInterface) + 1) * sizeof(WCHAR));
    if (!DeviceInfo)
    {
        /* No memory */
        DPRINT1("Failed to allocate WDMAUD_DEVICE_INFO structure\n");
        return MMSYSERR_NOMEM;
    }

    DeviceInfo->DeviceType = DeviceType;
    lstrcpyW(DeviceInfo->DeviceInterfaceString, DeviceInterface);

    Result = WdmAudIoControl(DeviceInfo,
                             0,
                             NULL,
                             bAdd ?
                             IOCTL_ADD_DEVNODE :
                             IOCTL_REMOVE_DEVNODE);

    HeapFree(GetProcessHeap(), 0, DeviceInfo);

    if ( ! MMSUCCESS( Result ) )
    {
        DPRINT1("Call to %ls failed with %d\n",
                bAdd ? L"IOCTL_ADD_DEVNODE" : L"IOCTL_REMOVE_DEVNODE",
                GetLastError());
        return TranslateInternalMmResult(Result);
    }

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudGetNumWdmDevsByLegacy(
    _In_  SOUND_DEVICE_TYPE DeviceType,
    _In_ LPCWSTR DeviceInterface,
    _Out_ DWORD* DeviceCount)
{
    MMRESULT Result;
    PWDMAUD_DEVICE_INFO DeviceInfo;

    VALIDATE_MMSYS_PARAMETER( KernelHandle != INVALID_HANDLE_VALUE );
    VALIDATE_MMSYS_PARAMETER( IS_VALID_SOUND_DEVICE_TYPE(DeviceType) );
    VALIDATE_MMSYS_PARAMETER( DeviceCount );

    DPRINT("WDMAUD - GetNumWdmDevs DeviceType %u\n", DeviceType);

    DeviceInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WDMAUD_DEVICE_INFO)
                                                               + (lstrlenW(DeviceInterface) + 1) * sizeof(WCHAR));
    if (!DeviceInfo)
    {
        /* No memory */
        DPRINT1("Failed to allocate WDMAUD_DEVICE_INFO structure\n");
        return MMSYSERR_NOMEM;
    }

    DeviceInfo->DeviceType = DeviceType;
    lstrcpyW(DeviceInfo->DeviceInterfaceString, DeviceInterface);

    Result = WdmAudIoControl(DeviceInfo,
                             0,
                             NULL,
                             IOCTL_GETNUMDEVS_TYPE);

    if ( ! MMSUCCESS( Result ) )
    {
        DPRINT1("Call to IOCTL_GETNUMDEVS_TYPE failed with %d\n", GetLastError());
    }

    *DeviceCount = DeviceInfo->DeviceIndex;
    HeapFree(GetProcessHeap(), 0, DeviceInfo);

    return Result;
}

MMRESULT
WdmAudGetCapabilitiesByLegacy(
    _In_  PWDMAUD_DEVICE_INFO DeviceInfo,
    _Out_ MDEVICECAPSEX* Capabilities)
{
    MMRESULT Result;

    ASSERT( Capabilities );

    DPRINT("WDMAUD - GetWdmDeviceCapabilities DeviceType %u DeviceId %u\n", DeviceInfo->DeviceType, DeviceInfo->DeviceIndex);

    Result = WdmAudIoControl(DeviceInfo,
                             Capabilities->cbSize,
                             Capabilities->pCaps,
                             IOCTL_GETCAPABILITIES);

    if ( ! MMSUCCESS(Result) )
    {
        DPRINT1("Call to IOCTL_GETCAPABILITIES failed with %d\n", GetLastError());
    }

    return Result;
}

MMRESULT
WdmAudOpenKernelSoundDeviceByLegacy(VOID)
{
    DWORD dwSize;
    HDEVINFO hDevInfo;
    SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
    GUID SWBusGuid = {STATIC_KSCATEGORY_WDMAUD};
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData;

    if ( KernelHandle == INVALID_HANDLE_VALUE )
    {
        hDevInfo = SetupDiGetClassDevsW(&SWBusGuid, NULL, NULL,  DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
        if (!hDevInfo)
        {
            // failed
            return MMSYSERR_ERROR;
        }

        DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        if (!SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &SWBusGuid, 0, &DeviceInterfaceData))
        {
            // failed
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return MMSYSERR_ERROR;
        }

        SetupDiGetDeviceInterfaceDetailW(hDevInfo,  &DeviceInterfaceData, NULL, 0, &dwSize, NULL);

        DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(GetProcessHeap(), 0, dwSize);
        if (!DeviceInterfaceDetailData)
        {
            // failed
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return MMSYSERR_ERROR;
        }

        DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(hDevInfo,  &DeviceInterfaceData, DeviceInterfaceDetailData, dwSize, &dwSize, NULL))
        {
            // failed
            HeapFree(GetProcessHeap(), 0, DeviceInterfaceDetailData);
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return MMSYSERR_ERROR;
        }

        DPRINT("Opening wdmaud device '%ls'\n", DeviceInterfaceDetailData->DevicePath);
        KernelHandle = CreateFileW(DeviceInterfaceDetailData->DevicePath,
                                   GENERIC_READ | GENERIC_WRITE,
                                   0,
                                   NULL,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                   NULL);

        HeapFree(GetProcessHeap(), 0, DeviceInterfaceDetailData);
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    if ( KernelHandle == INVALID_HANDLE_VALUE )
        return MMSYSERR_ERROR;

    ++OpenCount;
        return MMSYSERR_NOERROR;

}

MMRESULT
WdmAudOpenSoundDeviceByLegacy(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_opt_ PWAVEFORMATEX WaveFormat)
{
    MMRESULT Result;
    DWORD WaveFormatSize = 0;

    ASSERT( KernelHandle != INVALID_HANDLE_VALUE );

    DPRINT("WDMAUD - OpenWdmSoundDevice DeviceType %u\n", DeviceInfo->DeviceType);

    if ((DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE ||
        DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE) &&
        WaveFormat)
    {
        /* Calculate format size */
        WaveFormatSize = sizeof(WAVEFORMATEX);
        if (WaveFormat->wFormatTag != WAVE_FORMAT_PCM)
            WaveFormatSize += WaveFormat->cbSize;
    }

    /* Open device handle */
    Result = WdmAudIoControl(DeviceInfo,
                             WaveFormatSize,
                             WaveFormat,
                             DeviceInfo->DeviceType == MIXER_DEVICE_TYPE ?
                             IOCTL_OPEN_MIXER : IOCTL_OPEN_WDMAUD);

    if ( ! MMSUCCESS( Result ) )
    {
        DPRINT1("Call to IOCTL_OPEN_WDMAUD failed with %d\n", GetLastError());
        return TranslateInternalMmResult(Result);
    }

#ifdef RESAMPLING_ENABLED
    DeviceInfo->Buffer = WaveFormat;
    DeviceInfo->BufferSize = WaveFormatSize;
#endif

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudCloseSoundDeviceByLegacy(
    _In_  PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_  PVOID Handle)
{
    MMRESULT Result;

    ASSERT( KernelHandle != INVALID_HANDLE_VALUE );

    DPRINT("WDMAUD - CloseWdmSoundDevice DeviceType %u\n", DeviceInfo->DeviceType);

    if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        /* Is stream still in use? */
        if (DeviceInfo->DeviceState->WaveQueue)
        {
            DPRINT1("Stream is still in progress!\n");
            return WAVERR_STILLPLAYING;
        }

        /* Destroy I/O thread */
        Result = WdmAudDestroyCompletionThread(DeviceInfo);
        ASSERT(Result == MMSYSERR_NOERROR);
    }

    /* Close device handle */
    Result = WdmAudIoControl(DeviceInfo,
                             0,
                             NULL,
                             DeviceInfo->DeviceType == MIXER_DEVICE_TYPE ?
                             IOCTL_CLOSE_MIXER : IOCTL_CLOSE_WDMAUD);

    if ( ! MMSUCCESS(Result) )
    {
        DPRINT1("Call to IOCTL_CLOSE_WDMAUD failed with %d\n", GetLastError());
        return TranslateInternalMmResult(Result);
    }

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudSubmitWaveHeaderByLegacy(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ PWAVEHDR WaveHeader)
{
    PWDMAUD_DEVICE_INFO LocalDeviceInfo;
    PWAVEHDR_EXTENSION HeaderExtension;
    MMRESULT Result = MMSYSERR_NOERROR;
    DWORD Transferred = 0;
    BOOL IoResult;
    DWORD IoCtl;

    VALIDATE_MMSYS_PARAMETER( DeviceInfo );

    DPRINT("WDMAUD - SubmitWaveHeader DeviceType %u\n", DeviceInfo->DeviceType);

#ifdef RESAMPLING_ENABLED
    /* Resample the stream */
    Result = WdmAudResampleStream(DeviceInfo, WaveHeader);
    ASSERT( Result == MMSYSERR_NOERROR );
#endif

    LocalDeviceInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WDMAUD_DEVICE_INFO)
                                 + (lstrlenW(DeviceInfo->DeviceInterfaceString) + 1) * sizeof(WCHAR));
    if (!LocalDeviceInfo)
    {
        /* No memory */
        DPRINT1("Failed to allocate WDMAUD_DEVICE_INFO structure\n");
        return MMSYSERR_NOMEM;
    }

    HeaderExtension = (PWAVEHDR_EXTENSION)WaveHeader->reserved;
    HeaderExtension->DeviceInfo = LocalDeviceInfo; // Will be freed on wave header completion.

    LocalDeviceInfo->DeviceType = DeviceInfo->DeviceType;
    LocalDeviceInfo->DeviceIndex = DeviceInfo->DeviceIndex;
    LocalDeviceInfo->hDevice = DeviceInfo->hDevice;
    LocalDeviceInfo->Buffer = WaveHeader;
    LocalDeviceInfo->BufferSize = sizeof(WAVEHDR);
    lstrcpyW(LocalDeviceInfo->DeviceInterfaceString, DeviceInfo->DeviceInterfaceString);

    if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
        IoCtl = IOCTL_WRITEDATA;
    else if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE)
        IoCtl = IOCTL_READDATA;

    IoResult = DeviceIoControl(KernelHandle,
                               IoCtl,
                               LocalDeviceInfo,
                               sizeof(WDMAUD_DEVICE_INFO)
                               + (lstrlenW(DeviceInfo->DeviceInterfaceString) + 1) * sizeof(WCHAR),
                               LocalDeviceInfo,
                               sizeof(WDMAUD_DEVICE_INFO),
                               &Transferred,
                               &HeaderExtension->Overlapped);

    if (!IoResult)
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            DPRINT1("Call to %s failed with %d\n",
                    DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
                    "IOCTL_WRITEDATA" : "IOCTL_READDATA",
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

MMRESULT
WdmAudSetWaveStateByLegacy(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ BOOL bStart)
{
    MMRESULT Result;
    DWORD IoCtl;

    DPRINT("WDMAUD - SetWaveState DeviceType %u\n", DeviceInfo->DeviceType);

    if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE)
    {
        IoCtl = bStart ? IOCTL_START_CAPTURE : IOCTL_PAUSE_CAPTURE;
    }
    else if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        IoCtl = bStart ? IOCTL_START_PLAYBACK : IOCTL_PAUSE_PLAYBACK;
    }

    /* Are we requested to start the audio device? */
    if (IoCtl == IOCTL_START_CAPTURE || IoCtl == IOCTL_START_PLAYBACK)
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

    /* Talk to the device */
    Result = WdmAudIoControl(DeviceInfo,
                             0,
                             NULL,
                             IoCtl);

    if ( ! MMSUCCESS( Result ) )
    {
        DPRINT1("Call to %s failed with %d\n",
                DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
                (bStart ? "IOCTL_START_PLAYBACK" : "IOCTL_PAUSE_PLAYBACK") :
                (bStart ? "IOCTL_START_CAPTURE" : "IOCTL_PAUSE_CAPTURE"),
                GetLastError());
        return TranslateInternalMmResult(Result);
    }

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudResetStreamByLegacy(
    _In_  PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_  BOOL bStartReset)
{
    MMRESULT Result;
    DWORD IoCtl;

    DPRINT("WDMAUD - ResetWaveStream DeviceType %u\n", DeviceInfo->DeviceType);

    IoCtl = DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
            IOCTL_RESET_PLAYBACK : IOCTL_RESET_CAPTURE;

    /* Talk to the device */
    Result = WdmAudIoControl(DeviceInfo,
                             0,
                             NULL,
                             IoCtl);

    if ( ! MMSUCCESS(Result) )
    {
        DPRINT1("Call to %s failed with %d\n",
                DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
                "IOCTL_RESET_PLAYBACK" : "IOCTL_RESET_CAPTURE",
                GetLastError());
        return TranslateInternalMmResult(Result);
    }

    /* Mark device as stopped */
    DeviceInfo->DeviceState->bStart = FALSE;

    /* Destroy I/O thread */
    Result = WdmAudDestroyCompletionThread(DeviceInfo);
    ASSERT(Result == MMSYSERR_NOERROR);

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudGetWavePositionByLegacy(
    _In_  PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_  MMTIME* Time)
{
    MMRESULT Result;
    DWORD Position;
    DWORD IoCtl;

    DPRINT("WDMAUD - GetWavePosition DeviceType %u\n", DeviceInfo->DeviceType);

    IoCtl = DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
            IOCTL_GETOUTPOS : IOCTL_GETINPOS;

    Result = WdmAudIoControl(DeviceInfo,
                             sizeof(DWORD),
                             &Position,
                             IoCtl);

    if ( ! MMSUCCESS(Result) )
    {
        DPRINT1("Call to %s failed with %d\n",
                DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ?
                "IOCTL_GETOUTPOS" : "IOCTL_GETINPOS",
                GetLastError());
        return TranslateInternalMmResult(Result);
    }

    /* Store position */
    Time->wType = TIME_BYTES;
    Time->u.cb = Position;

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudGetVolumeByLegacy(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ DWORD DeviceId,
    _Out_ PDWORD pdwVolume)
{
    /* FIXME */
    return MMSYSERR_NOTSUPPORTED;
}

MMRESULT
WdmAudSetVolumeByLegacy(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ DWORD DeviceId,
    _In_ DWORD dwVolume)
{
    /* FIXME */
    return MMSYSERR_NOTSUPPORTED;
}

MMRESULT
WdmAudQueryMixerInfoByLegacy(
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ DWORD DeviceId,
    _In_ UINT uMsg,
    _In_ LPVOID Parameter,
    _In_ DWORD Flags)
{
    MMRESULT Result;
    DWORD IoControlCode;
    DWORD dwSize;

    DPRINT("WDMAUD - QueryMixerInfo: uMsg %x Flags %x\n", uMsg, Flags);

    DeviceInfo->DeviceIndex = DeviceId;
    DeviceInfo->DeviceType = MIXER_DEVICE_TYPE;
    DeviceInfo->Flags = Flags;

    switch(uMsg)
    {
        case MXDM_GETLINEINFO:
            IoControlCode = IOCTL_GETLINEINFO;
            dwSize = sizeof(MIXERLINEW);
            break;
        case MXDM_GETLINECONTROLS:
            IoControlCode = IOCTL_GETLINECONTROLS;
            dwSize = sizeof(MIXERLINECONTROLSW);
            break;
        case MXDM_SETCONTROLDETAILS:
            IoControlCode = IOCTL_SETCONTROLDETAILS;
            dwSize = sizeof(MIXERCONTROLDETAILS);
            break;
        case MXDM_GETCONTROLDETAILS:
            IoControlCode = IOCTL_GETCONTROLDETAILS;
            dwSize = sizeof(MIXERCONTROLDETAILS);
            break;
        default:
            IoControlCode = 0;
            dwSize = 0;
            ASSERT(0);
            break;
    }

    Result = WdmAudIoControl(DeviceInfo,
                             dwSize,
                             Parameter,
                             IoControlCode);

    if ( ! MMSUCCESS(Result) )
    {
        DPRINT1("Call to 0x%lx failed with %d\n", IoControlCode, GetLastError());
        return TranslateInternalMmResult(Result);
    }

    return MMSYSERR_NOERROR;
}
