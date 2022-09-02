/*
 * PROJECT:     ReactOS Sound System "MME Buddy" Library
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        lib/sound/mmebuddy/mixer/mxdMessage.c
 *
 * PURPOSE:     Provides the mxdMessage exported function, as required by
 *              the MME API, for mixer device support.
 *
 * PROGRAMMERS: Andrew Greenwood (silverblade@reactos.org)
*/

#include "wdmaud.h"

#define NDEBUG
#include <debug.h>

MMRESULT
MmeQueryMixerInfo(
    IN UINT DeviceId,
    IN  UINT Message,
    IN  DWORD_PTR PrivateHandle,
    IN  DWORD_PTR Parameter1,
    IN  DWORD_PTR Parameter2)
{
    PWDMAUD_DEVICE_INFO DeviceInfo;
    MMRESULT Result;

    DPRINT("Getting mixer info %u\n", Message);

    if ( PrivateHandle == 0 )
    {
        DeviceInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WDMAUD_DEVICE_INFO));
        if (DeviceInfo)
        {
            Result = FUNC_NAME(WdmAudQueryMixerInfo)(DeviceInfo, DeviceId, Message, (LPVOID)Parameter1, Parameter2);
            HeapFree(GetProcessHeap(), 0, DeviceInfo);
            return TranslateInternalMmResult(Result);
        }
    }

    VALIDATE_MMSYS_PARAMETER( PrivateHandle );
    DeviceInfo = (PWDMAUD_DEVICE_INFO)PrivateHandle;

    return FUNC_NAME(WdmAudQueryMixerInfo)(DeviceInfo, DeviceId, Message, (LPVOID)Parameter1, Parameter2);
}


/*
    Standard MME driver entry-point for messages relating to mixers.
*/
DWORD
APIENTRY
mxdMessage(
    UINT DeviceId,
    UINT Message,
    DWORD_PTR PrivateHandle,
    DWORD_PTR Parameter1,
    DWORD_PTR Parameter2)
{
    MMRESULT Result = MMSYSERR_NOTSUPPORTED;

    AcquireEntrypointMutex(MIXER_DEVICE_TYPE);

    DPRINT("mxdMessage - Message type %d\n", Message);

    switch ( Message )
    {
#ifndef USE_MMIXER_LIB
        case MXDM_INIT:
        {
            Result = WdmAudAddRemoveDeviceNode(MIXER_DEVICE_TYPE, (LPCWSTR)Parameter2, TRUE);
            break;
        }

        case DRVM_EXIT:
        {
            Result = WdmAudAddRemoveDeviceNode(MIXER_DEVICE_TYPE, (LPCWSTR)Parameter2, FALSE);
            break;
        }
#endif

        case MXDM_GETNUMDEVS :
        {
            Result = MmeGetNumDevs(MIXER_DEVICE_TYPE, (LPCWSTR)Parameter1);
            break;
        }

        case MXDM_GETDEVCAPS :
        {
            Result = MmeGetSoundDeviceCapabilities(MIXER_DEVICE_TYPE,
                                                   DeviceId,
                                                   (LPCWSTR)Parameter2,
                                                   (MDEVICECAPSEX*)Parameter1);
            break;
        }

        case MXDM_OPEN :
        {
            Result = MmeOpenDevice(MIXER_DEVICE_TYPE,
                                   DeviceId,
                                   (PVOID)Parameter1,
                                   Parameter2,
                                   (DWORD_PTR*) PrivateHandle);

            break;
        }

        case MXDM_CLOSE :
        {
            Result = MmeCloseDevice(PrivateHandle);

            break;
        }

        case MXDM_GETCONTROLDETAILS :
        {
            Result = MmeQueryMixerInfo(DeviceId,
                                       Message,
                                       PrivateHandle,
                                       Parameter1,
                                       Parameter2);

            break;
        }

        case MXDM_SETCONTROLDETAILS :
        {
            Result = MmeQueryMixerInfo(DeviceId,
                                       Message,
                                       PrivateHandle,
                                       Parameter1,
                                       Parameter2);

            break;
        }

        case MXDM_GETLINECONTROLS :
        {
            Result = MmeQueryMixerInfo(DeviceId,
                                       Message,
                                       PrivateHandle,
                                       Parameter1,
                                       Parameter2);

            break;
        }

        case MXDM_GETLINEINFO :
        {
            Result = MmeQueryMixerInfo(DeviceId,
                                       Message,
                                       PrivateHandle,
                                       Parameter1,
                                       Parameter2);

            break;
        }
    }

    DPRINT("mxdMessage returning MMRESULT %d\n", Result);

    ReleaseEntrypointMutex(MIXER_DEVICE_TYPE);

    return Result;
}
