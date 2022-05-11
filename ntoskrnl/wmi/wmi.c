/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/wmi/wmi.c
 * PURPOSE:         I/O Windows Management Instrumentation (WMI) Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define INITGUID
#include <wmiguid.h>
#include <wmidata.h>
#include <wmistr.h>

#include "wmip.h"

#define NDEBUG
#include <debug.h>

typedef PVOID PWMI_LOGGER_INFORMATION; // FIXME

typedef enum _WMI_CLOCK_TYPE
{
    WMICT_DEFAULT,
    WMICT_SYSTEMTIME,
    WMICT_PERFCOUNTER,
    WMICT_PROCESS,
    WMICT_THREAD,
    WMICT_CPUCYCLE
} WMI_CLOCK_TYPE;

extern PDEVICE_OBJECT WmipServiceDeviceObject;

/* FUNCTIONS *****************************************************************/

NTSTATUS FASTCALL WmiTraceEvent(IN PVOID InputBuffer, IN KPROCESSOR_MODE PreviousMode);
NTSTATUS FASTCALL WmiTraceFastEvent(IN PWNODE_HEADER Wnode);

BOOLEAN
NTAPI
WmiInitialize(
    VOID)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\WMIxWDM");
    NTSTATUS Status;

    /* Initialize the GUID object type */
    Status = WmipInitializeGuidObjectType();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WmipInitializeGuidObjectType() failed: 0x%lx\n", Status);
        return FALSE;
    }

    /* Create the WMI driver */
    Status = IoCreateDriver(&DriverName, WmipDriverEntry);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create WMI driver: 0x%lx\n", Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIRegistrationControl(IN PDEVICE_OBJECT DeviceObject,
                         IN ULONG Action)
{
    DPRINT("IoWMIRegistrationControl() called for DO %p, requesting %lu action, returning success\n",
        DeviceObject, Action);

    return STATUS_SUCCESS;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIAllocateInstanceIds(IN GUID *Guid,
                         IN ULONG InstanceCount,
                         OUT ULONG *FirstInstanceId)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMISuggestInstanceName(IN PDEVICE_OBJECT PhysicalDeviceObject OPTIONAL,
                         IN PUNICODE_STRING SymbolicLinkName OPTIONAL,
                         IN BOOLEAN CombineNames,
                         OUT PUNICODE_STRING SuggestedInstanceName)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

#if DBG
VOID
NTAPI
IopWmiDumpWnodeHeader(_In_ PWNODE_HEADER WnodeHeader)
{
    //UNICODE_STRING GuidString;
    //NTSTATUS Status;

    //DPRINT1("IopWmiDumpWnodeHeader: WnodeHeader %p\n", WnodeHeader);

if (WnodeHeader->BufferSize)
    DPRINT1("BufferSize    %X\n", WnodeHeader->BufferSize);
if (WnodeHeader->ProviderId)
    DPRINT1("ProviderId    %X\n", WnodeHeader->ProviderId);
if (WnodeHeader->Version)
    DPRINT1("Version       %X\n", WnodeHeader->Version);
if (WnodeHeader->Linkage)
    DPRINT1("Linkage       %X\n", WnodeHeader->Linkage);
if (WnodeHeader->CountLost)
    DPRINT1("CountLost     %X\n", WnodeHeader->CountLost);
if (WnodeHeader->ClientContext)
    DPRINT1("ClientContext %X\n", WnodeHeader->ClientContext);
if (WnodeHeader->Flags)
    DPRINT1("Flags         %X\n", WnodeHeader->Flags);

#if 0
    GuidString.Buffer = NULL;

    Status = RtlStringFromGUID(&WnodeHeader->Guid, &GuidString);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("\nIopWmiDprintWnodeHeader: RtlStringFromGUID() failed %X\n", Status);
        return;
    }

    DPRINT1("Guid          '%S'\n", &GuidString.Buffer);

    RtlFreeUnicodeString(&GuidString);
#endif
}
#endif

/* halfplemented */
NTSTATUS
NTAPI
IoWMIWriteEvent(_In_ PVOID WnodeEventItem)
{
    PWNODE_HEADER WnodeHeader = WnodeEventItem;
    ULONG BufferSize;
    ULONG Index;
    NTSTATUS Status;

    DPRINT1("IoWMIWriteEvent: WnodeEventItem %p\n", WnodeEventItem);

    if (!WmipServiceDeviceObject )
    {
        DPRINT1("IoWMIWriteEvent: WmipServiceDeviceObject is NULL!\n");
        return STATUS_UNSUCCESSFUL;
    }

    if (!WnodeHeader)
    {
         DPRINT1("IoWMIWriteEvent: Got NULL Item!\n");
         return STATUS_INVALID_PARAMETER;
    }

  #if DBG
    IopWmiDumpWnodeHeader(WnodeHeader);
  #endif

    BufferSize = WnodeHeader->BufferSize;

    if ((BufferSize & 0xC0000000) == 0x80000000) // ?
        return WmiTraceFastEvent(WnodeHeader);

    if ((WnodeHeader->Flags & 0x00020000) ||
        (WnodeHeader->Flags & 0x00040000))
    {
        if (BufferSize < 0x30)
        {
            DPRINT1("IoWMIWriteEvent: STATUS_BUFFER_TOO_SMALL (%X)\n", BufferSize);
            return STATUS_BUFFER_TOO_SMALL;
        }

        if (WnodeHeader->Flags & 0x20000)
        {
            if (BufferSize > 0xFFFF)
            {
                DPRINT1("IoWMIWriteEvent: STATUS_BUFFER_OVERFLOW (%X)\n", BufferSize);
                return STATUS_BUFFER_OVERFLOW;
            }

            WnodeHeader->BufferSize = (BufferSize | 0xC00A0000); // ?
        }
        else if (BufferSize & 0x80000000)
        {
            DPRINT1("IoWMIWriteEvent: STATUS_BUFFER_OVERFLOW (%X)\n", BufferSize);
            return STATUS_BUFFER_OVERFLOW;
        }

        Index = (WnodeHeader->HistoricalContext & 0xFFFF);
        if (Index == 0xFFFF)
            Index = 0xFFFF;

if (Index && (Index < 0x40) /*&& WmipLoggerContext[Index]*/)
    DPRINT1("IoWMIWriteEvent: FIXME WmipLoggerContext[]!\n");

        if (Index && (Index < 0x40) && /*&& WmipLoggerContext[Index]*/ FALSE)
        {
            Status = WmiTraceEvent(WnodeHeader, 0);
        }
        else
        {
            DPRINT1("IoWMIWriteEvent: STATUS_INVALID_HANDLE\n");
            Status = STATUS_INVALID_HANDLE;
        }

        if (WnodeHeader->Flags & 0x20000)
        {
            WnodeHeader->BufferSize = BufferSize;
            return Status;
        }
    }

    DPRINT1("IoWMIWriteEvent: WnodeHeader->Flags %X\n", WnodeHeader->Flags);
    DPRINT1("IoWMIWriteEvent: FIXME\n");
    ASSERT(FALSE); // DbgBreakPoint();

    /* Free the buffer if we are returning success */
    //ExFreePool(WnodeEventItem);

    return STATUS_SUCCESS;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIOpenBlock(
    _In_ LPCGUID DataBlockGuid,
    _In_ ULONG DesiredAccess,
    _Out_ PVOID *DataBlockObject)
{
    HANDLE GuidObjectHandle;
    NTSTATUS Status;

    /* Open the GIOD object */
    Status = WmipOpenGuidObject(DataBlockGuid,
                                DesiredAccess,
                                KernelMode,
                                &GuidObjectHandle,
                                DataBlockObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WmipOpenGuidObject failed: 0x%lx\n", Status);
        return Status;
    }


    return STATUS_SUCCESS;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIQueryAllData(
    IN PVOID DataBlockObject,
    IN OUT ULONG *InOutBufferSize,
    OUT PVOID OutBuffer)
{
    PWMIP_GUID_OBJECT GuidObject;
    NTSTATUS Status;


    Status = ObReferenceObjectByPointer(DataBlockObject,
                                        WMIGUID_QUERY,
                                        WmipGuidObjectType,
                                        KernelMode);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    GuidObject = DataBlockObject;

    /* Huge HACK! */
    if (IsEqualGUID(&GuidObject->Guid, &MSSmBios_RawSMBiosTables_GUID))
    {
        Status = WmipQueryRawSMBiosTables(InOutBufferSize, OutBuffer);
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }

    ObDereferenceObject(DataBlockObject);

    return Status;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIQueryAllDataMultiple(IN PVOID *DataBlockObjectList,
                          IN ULONG ObjectCount,
                          IN OUT ULONG *InOutBufferSize,
                          OUT PVOID OutBuffer)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIQuerySingleInstance(IN PVOID DataBlockObject,
                         IN PUNICODE_STRING InstanceName,
                         IN OUT ULONG *InOutBufferSize,
                         OUT PVOID OutBuffer)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIQuerySingleInstanceMultiple(IN PVOID *DataBlockObjectList,
                                 IN PUNICODE_STRING InstanceNames,
                                 IN ULONG ObjectCount,
                                 IN OUT ULONG *InOutBufferSize,
                                 OUT PVOID OutBuffer)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMISetSingleInstance(IN PVOID DataBlockObject,
                       IN PUNICODE_STRING InstanceName,
                       IN ULONG Version,
                       IN ULONG ValueBufferSize,
                       IN PVOID ValueBuffer)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMISetSingleItem(IN PVOID DataBlockObject,
                   IN PUNICODE_STRING InstanceName,
                   IN ULONG DataItemId,
                   IN ULONG Version,
                   IN ULONG ValueBufferSize,
                   IN PVOID ValueBuffer)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIExecuteMethod(IN PVOID DataBlockObject,
                   IN PUNICODE_STRING InstanceName,
                   IN ULONG MethodId,
                   IN ULONG InBufferSize,
                   IN OUT PULONG OutBufferSize,
                   IN OUT PUCHAR InOutBuffer)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMISetNotificationCallback(IN PVOID Object,
                             IN WMI_NOTIFICATION_CALLBACK Callback,
                             IN PVOID Context)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIHandleToInstanceName(IN PVOID DataBlockObject,
                          IN HANDLE FileHandle,
                          OUT PUNICODE_STRING InstanceName)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
IoWMIDeviceObjectToInstanceName(IN PVOID DataBlockObject,
                                IN PDEVICE_OBJECT DeviceObject,
                                OUT PUNICODE_STRING InstanceName)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
WmiQueryTraceInformation(IN TRACE_INFORMATION_CLASS TraceInformationClass,
                         OUT PVOID TraceInformation,
                         IN ULONG TraceInformationLength,
                         OUT PULONG RequiredLength OPTIONAL,
                         IN PVOID Buffer OPTIONAL)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
__cdecl
WmiTraceMessage(IN TRACEHANDLE LoggerHandle,
                IN ULONG MessageFlags,
                IN LPGUID MessageGuid,
                IN USHORT MessageNumber,
                IN ...)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
WmiTraceMessageVa(IN TRACEHANDLE LoggerHandle,
                  IN ULONG MessageFlags,
                  IN LPGUID MessageGuid,
                  IN USHORT MessageNumber,
                  IN va_list MessageArgList)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
WmiFlushTrace(IN OUT PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

LONG64
FASTCALL
WmiGetClock(IN WMI_CLOCK_TYPE ClockType,
            IN PVOID Context)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
WmiQueryTrace(IN OUT PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
WmiStartTrace(IN OUT PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}
    
NTSTATUS
NTAPI
WmiStopTrace(IN PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
FASTCALL
WmiTraceFastEvent(IN PWNODE_HEADER Wnode)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
WmiUpdateTrace(IN OUT PWMI_LOGGER_INFORMATION LoggerInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
NtTraceEvent(IN ULONG TraceHandle,
             IN ULONG Flags,
             IN ULONG TraceHeaderLength,
             IN struct _EVENT_TRACE_HEADER* TraceHeader)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*Eof*/
