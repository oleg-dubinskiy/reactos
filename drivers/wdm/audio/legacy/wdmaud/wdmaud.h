#ifndef _WDMAUD_PCH_
#define _WDMAUD_PCH_

#include <ntifs.h>
#include <portcls.h>
#include <swenum.h>
#include <mmsystem.h>

#include "interface.h"

typedef struct
{
    PMDL Mdl;
    ULONG Length;
    ULONG Function;
    PFILE_OBJECT FileObject;
}WDMAUD_COMPLETION_CONTEXT, *PWDMAUD_COMPLETION_CONTEXT;

typedef struct
{
    HANDLE Handle;
    PFILE_OBJECT FileObject;
    SOUND_DEVICE_TYPE Type;
    ULONG FilterId;
    ULONG PinId;
    BOOL Active;
    PRKEVENT NotifyEvent;
}WDMAUD_HANDLE, *PWDMAUD_HANDLE;

typedef struct
{
    LIST_ENTRY Entry;
    HANDLE hProcess;
    ULONG NumPins;
    WDMAUD_HANDLE * hPins;

    LIST_ENTRY MixerEventList;
}WDMAUD_CLIENT, *PWDMAUD_CLIENT;

typedef struct
{
    LIST_ENTRY Entry;
    ULONG NotificationType;
    ULONG Value;
    HANDLE hMixer;
}EVENT_ENTRY, *PEVENT_ENTRY;

typedef struct
{
    KSDEVICE_HEADER DeviceHeader;
    PVOID SysAudioNotification;

    KSPIN_LOCK Lock;
    LIST_ENTRY WdmAudClientList;

    ULONG SysAudioDeviceCount;
    PIO_WORKITEM WorkItem;
    KEVENT InitializationCompletionEvent;
    ULONG WorkItemActive;

    PDEVICE_OBJECT NextDeviceObject;
}WDMAUD_DEVICE_EXTENSION, *PWDMAUD_DEVICE_EXTENSION;

typedef struct
{
    PWDMAUD_CLIENT ClientInfo;
    SOUND_DEVICE_TYPE DeviceType;
}PIN_CREATE_CONTEXT, *PPIN_CREATE_CONTEXT;

NTSTATUS
WdmAudRegisterDeviceInterface(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN PWDMAUD_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
WdmAudOpenSysAudioDevices(
    OUT PHANDLE phSysAudio,
    OUT PFILE_OBJECT *pFileObject);

NTSTATUS
WdmAudAllocateContext(
    IN PDEVICE_OBJECT DeviceObject,
    IN PWDMAUD_CLIENT *pClient);

NTSTATUS
NTAPI
WdmAudDeviceControl(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp);

NTSTATUS
NTAPI
WdmAudReadWrite(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp);

NTSTATUS
WdmAudControlOpenMixer(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo);

NTSTATUS
WdmAudControlCloseMixer(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo,
    IN  ULONG Index);

VOID
WdmAudCloseAllMixers(
    IN PDEVICE_OBJECT DeviceObject,
    IN PWDMAUD_CLIENT ClientInfo,
    IN ULONG Index);

NTSTATUS
WdmAudControlOpenWave(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo);

NTSTATUS
WdmAudControlOpenMidi(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo);

ULONG
GetNumOfMixerDevices(
    IN  PDEVICE_OBJECT DeviceObject);

NTSTATUS
SetIrpIoStatus(
    IN PIRP Irp,
    IN NTSTATUS Status,
    IN ULONG Length);

NTSTATUS
GetSysAudioDeviceInterface(
    OUT LPWSTR* SymbolicLonkList);

NTSTATUS
WdmAudOpenSysAudioDevice(
    IN LPWSTR DeviceName,
    OUT PHANDLE Handle);

NTSTATUS
FindProductName(
    IN LPWSTR PnpName,
    IN ULONG ProductNameSize,
    OUT LPWSTR ProductName);

NTSTATUS
WdmAudMixerCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo,
    IN PWDMAUD_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
WdmAudWaveCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo,
    IN PWDMAUD_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
WdmAudMidiCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PWDMAUD_DEVICE_INFO DeviceInfo,
    IN PWDMAUD_CLIENT ClientInfo,
    IN PWDMAUD_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
NTAPI
WdmAudGetPosition(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PWDMAUD_DEVICE_INFO DeviceInfo);

NTSTATUS
WdmAudSetDeviceState(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  KSSTATE State,
    IN  BOOL CompleteIrp);

NTSTATUS
NTAPI
WdmAudResetStream(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  BOOL CompleteIrp);

NTSTATUS
NTAPI
WdmAudGetLineInfo(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo);

NTSTATUS
NTAPI
WdmAudGetLineControls(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo);

NTSTATUS
NTAPI
WdmAudSetControlDetails(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo);

NTSTATUS
NTAPI
WdmAudGetMixerEvent(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo);

NTSTATUS
NTAPI
WdmAudGetControlDetails(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo);

NTSTATUS
WdmAudControlInitialize(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp);

NTSTATUS
WdmAudMixerInitialize(
    IN PFILE_OBJECT FileObject);

NTSTATUS
NTAPI
WdmAudWaveInitialize(
    IN PDEVICE_OBJECT DeviceObject);

ULONG
ClosePinByIndex(
    IN  PWDMAUD_CLIENT ClientInfo,
    IN  ULONG FilterId,
    IN  ULONG PinId,
    IN  SOUND_DEVICE_TYPE DeviceType);

VOID
ClosePin(
    IN  PWDMAUD_CLIENT ClientInfo,
    IN  ULONG Index);

ULONG
GetActivePin(
    IN  PWDMAUD_CLIENT ClientInfo);

NTSTATUS
InsertPinHandle(
    IN  PWDMAUD_CLIENT ClientInfo,
    IN  ULONG FilterId,
    IN  ULONG PinId,
    IN  SOUND_DEVICE_TYPE DeviceType,
    IN  HANDLE PinHandle,
    IN  PFILE_OBJECT PinFileObject,
    IN  ULONG FreeIndex);

NTSTATUS
GetSysAudioDevicePnpName(
    IN  PFILE_OBJECT FileObject,
    IN  ULONG DeviceIndex,
    OUT LPWSTR * Device);

NTSTATUS
OpenSysAudioDeviceByIndex(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  ULONG DeviceIndex,
    IN  PHANDLE DeviceHandle,
    IN  PFILE_OBJECT * FileObject);

NTSTATUS
OpenDevice(
    IN LPWSTR Device,
    OUT PHANDLE DeviceHandle,
    OUT PFILE_OBJECT * FileObject);

ULONG
WdmAudGetMixerDeviceCount(VOID);

ULONG
WdmAudGetWaveInDeviceCount(VOID);

ULONG
WdmAudGetWaveOutDeviceCount(VOID);

ULONG
WdmAudGetMidiInDeviceCount(VOID);

ULONG
WdmAudGetMidiOutDeviceCount(VOID);

NTSTATUS
WdmAudGetPnpNameByIndexAndType(
    IN ULONG DeviceIndex,
    IN SOUND_DEVICE_TYPE DeviceType,
    OUT LPWSTR *Device);


/* sup.c */

ULONG
GetSysAudioDeviceCount(
    IN  PFILE_OBJECT FileObject);

NTSTATUS
SetSysAudioDeviceInstance(
    IN PFILE_OBJECT FileObject,
    IN ULONG VirtualDeviceId);

PVOID
AllocateItem(
    IN POOL_TYPE PoolType,
    IN SIZE_T NumberOfBytes);

VOID
FreeItem(
    IN PVOID Item);

#endif /* _WDMAUD_PCH_ */
