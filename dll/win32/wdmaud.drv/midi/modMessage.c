/*
 * PROJECT:     ReactOS Sound System "MME Buddy" Library
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        lib/sound/mmebuddy/midi/modMessage.c
 *
 * PURPOSE:     Provides the modMessage exported function, as required by
 *              the MME API, for MIDI output device support.
 *
 * PROGRAMMERS: Andrew Greenwood (silverblade@reactos.org)
*/

#include "wdmaud.h"

#define NDEBUG
#include <debug.h>

/*
    Standard MME driver entry-point for messages relating to MIDI output.
*/
DWORD
APIENTRY
modMessage(
    UINT DeviceId,
    UINT Message,
    DWORD_PTR PrivateHandle,
    DWORD_PTR Parameter1,
    DWORD_PTR Parameter2)
{
    MMRESULT Result = MMSYSERR_NOTSUPPORTED;

    AcquireEntrypointMutex(MIDI_OUT_DEVICE_TYPE);

    DPRINT("modMessage - Message type %d\n", Message);

    switch ( Message )
    {
#ifndef USE_MMIXER_LIB
        case MODM_INIT:
        {
            Result = WdmAudAddRemoveDeviceNode(MIDI_OUT_DEVICE_TYPE, (LPCWSTR)Parameter2, TRUE);
            break;
        }

        case DRVM_EXIT:
        {
            Result = WdmAudAddRemoveDeviceNode(MIDI_OUT_DEVICE_TYPE, (LPCWSTR)Parameter2, FALSE);
            break;
        }
#endif

        case MODM_GETNUMDEVS :
        {
            Result = MmeGetNumDevs(MIDI_OUT_DEVICE_TYPE, (LPCWSTR)Parameter1);
            break;
        }

        case MODM_GETDEVCAPS :
        {
            Result = MmeGetSoundDeviceCapabilities(MIDI_OUT_DEVICE_TYPE,
                                                   DeviceId,
                                                   (LPCWSTR)Parameter2,
                                                   (MDEVICECAPSEX*)Parameter1);
            break;
        }

        case MODM_OPEN :
        {
            Result = MmeOpenDevice(MIDI_OUT_DEVICE_TYPE,
                                   DeviceId,
                                   (PVOID)Parameter1,
                                   Parameter2,
                                   (DWORD_PTR*)PrivateHandle);
            break;
        }

        case MODM_CLOSE :
        {
            Result = MmeCloseDevice(PrivateHandle);

            break;
        }

        case MODM_GETVOLUME:
        {
            Result = MmeGetVolume(MIDI_OUT_DEVICE_TYPE,
                                  DeviceId,
                                  PrivateHandle,
                                  Parameter1);
            break;
        }

        case MODM_SETVOLUME:
        {
            Result = MmeSetVolume(MIDI_OUT_DEVICE_TYPE,
                                  DeviceId,
                                  PrivateHandle,
                                  Parameter1);
            break;
        }
    }

    DPRINT("modMessage returning MMRESULT %d\n", Result);

    ReleaseEntrypointMutex(MIDI_OUT_DEVICE_TYPE);

    return Result;
}
