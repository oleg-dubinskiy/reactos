/*
 * PROJECT:     ReactOS Sound System "MME Buddy" Library
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        lib/drivers/sound/mmebuddy/wave/header.c
 *
 * PURPOSE:     Wave header preparation and submission routines
 *
 * PROGRAMMERS: Andrew Greenwood (silverblade@reactos.org)
*/

#include "wdmaud.h"

#define NDEBUG
#include <debug.h>


/*
    The following routines are basically handlers for:
    - WODM_PREPARE
    - WODM_UNPREPARE
    - WODM_WRITE

    All of these calls are ultimately dealt with in the context of the
    appropriate sound thread, so the implementation should expect itself to
    be running in this other thread when any of these operations take place.
*/

MMRESULT
PrepareWaveHeader(
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWAVEHDR Header)
{
    PWAVEHDR_EXTENSION HeaderExtension;

    VALIDATE_MMSYS_PARAMETER( DeviceInfo );
    VALIDATE_MMSYS_PARAMETER( Header );

    DPRINT("Preparing wave header\n");

    /* Initialize fields for new header */
    Header->lpNext = NULL;
    Header->reserved = 0;

    /* Allocate header extension */
    HeaderExtension = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WAVEHDR_EXTENSION));
    if (!HeaderExtension)
    {
        /* No memory */
        return MMSYSERR_NOMEM;
    }

    /* Create stream event */
    HeaderExtension->Overlapped.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!HeaderExtension->Overlapped.hEvent)
    {
        /* No memory */
        HeapFree(GetProcessHeap(), 0, HeaderExtension);
        return MMSYSERR_NOMEM;
    }

    Header->reserved = (DWORD_PTR)HeaderExtension;

    /* This is what winmm expects to be returned */
    return MMSYSERR_NOTSUPPORTED;
}

MMRESULT
UnprepareWaveHeader(
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWAVEHDR Header)
{
    PWAVEHDR_EXTENSION HeaderExtension;

    VALIDATE_MMSYS_PARAMETER( DeviceInfo );
    VALIDATE_MMSYS_PARAMETER( Header );

    DPRINT("Un-preparing wave header\n");

    HeaderExtension = (PWAVEHDR_EXTENSION)Header->reserved;

    Header->reserved = 0;

    /* Destroy stream event */
    CloseHandle(HeaderExtension->Overlapped.hEvent);

    /* Free header extension */
    HeapFree(GetProcessHeap(), 0, HeaderExtension);

    /* This is what winmm expects to be returned */
    return MMSYSERR_NOTSUPPORTED;
}

MMRESULT
WriteWaveHeader(
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWAVEHDR Header)
{
    VALIDATE_MMSYS_PARAMETER( DeviceInfo );
    VALIDATE_MMSYS_PARAMETER( Header );

    DPRINT("Submitting wave header\n");

    /*
        A few minor sanity checks - any custom checks should've been carried
        out during wave header preparation etc.
    */
    VALIDATE_MMSYS_PARAMETER( Header->lpData != NULL );
    VALIDATE_MMSYS_PARAMETER( Header->dwBufferLength > 0 );
    VALIDATE_MMSYS_PARAMETER( Header->dwFlags & WHDR_PREPARED );
    VALIDATE_MMSYS_PARAMETER( ! (Header->dwFlags & WHDR_INQUEUE) );

    return EnqueueWaveHeader(DeviceInfo, Header);
}


/*
    EnqueueWaveHeader
        Put the header in the record/playback queue. This is performed within
        the context of the sound thread, it must NEVER be called from another
        thread.

    CompleteWaveHeader
        Set the header information to indicate that it has finished playing,
        and return it to the client application. This again must be called
        within the context of the sound thread.
*/

MMRESULT
EnqueueWaveHeader(
    IN PWDMAUD_DEVICE_INFO DeviceInfo,
    IN PWAVEHDR Header)
{
    VALIDATE_MMSYS_PARAMETER( DeviceInfo );
    VALIDATE_MMSYS_PARAMETER( Header );

    /* Set the "in queue" flag */
    Header->dwFlags |= WHDR_INQUEUE;

    /* Clear the "done" flag for the buffer */
    Header->dwFlags &= ~WHDR_DONE;

    if (!DeviceInfo->DeviceState->WaveQueue)
    {
        /* This is the first header in the queue */
        DPRINT("Enqueued first wave header\n");
        DeviceInfo->DeviceState->WaveQueue = Header;

        if (DeviceInfo->DeviceState->hNotifyEvent)
        {
            /* Set queue event */
            DPRINT("Setting queue event...\n");
            SetEvent(DeviceInfo->DeviceState->hNotifyEvent);
        }
    }
    else
    {
        /* This is the 2nd or another header */
        DPRINT("Enqueued next wave header\n");

        /* Enumerate the whole queue */
        PWAVEHDR TempHeader = DeviceInfo->DeviceState->WaveQueue;
        while (TempHeader->lpNext) TempHeader = TempHeader->lpNext;

        /* Insert the header in the end of it */
        TempHeader->lpNext = Header;
    }

    /* Do wave streaming */
    return DoWaveStreaming(DeviceInfo, Header);
}

VOID
CompleteWaveHeader(
    IN  PWDMAUD_DEVICE_INFO DeviceInfo)
{
    PWAVEHDR_EXTENSION HeaderExtension;
    PWAVEHDR Header;

    Header = DeviceInfo->DeviceState->WaveQueue;

    DPRINT("Completing wave header\n");

    if (Header)
    {
        /* Move to the next header */
        DeviceInfo->DeviceState->WaveQueue = DeviceInfo->DeviceState->WaveQueue->lpNext;

        DPRINT("Returning buffer to client...\n");

        /* Free header's device info */
        HeaderExtension = (PWAVEHDR_EXTENSION)Header->reserved;
        if (HeaderExtension->DeviceInfo)
        {
            HeapFree(GetProcessHeap(), 0, HeaderExtension->DeviceInfo);
        }

        /* Update the header */
        Header->lpNext = NULL;
        Header->dwFlags &= ~WHDR_INQUEUE;
        Header->dwFlags |= WHDR_DONE;

        /* Safe to do this without thread protection, as we're done with the header */
        NotifyMmeClient(DeviceInfo,
                        DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE ? WOM_DONE : WIM_DATA,
                        (DWORD_PTR)Header);
    }
}
