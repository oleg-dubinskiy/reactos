/*
 * PROJECT:         ReactOS Kernel
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/pnpmgr/plugplay.c
 * PURPOSE:         Plug-and-play interface routines
 * PROGRAMMERS:     Eric Kohl <eric.kohl@t-online.de>
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#if defined (ALLOC_PRAGMA)
#pragma alloc_text(INIT, IopInitPlugPlayEvents)
#endif

typedef struct _PNP_EVENT_ENTRY
{
    LIST_ENTRY ListEntry;
    PLUGPLAY_EVENT_BLOCK Event;
} PNP_EVENT_ENTRY, *PPNP_EVENT_ENTRY;


/* GLOBALS *******************************************************************/

static LIST_ENTRY IopPnpEventQueueHead;
static KEVENT IopPnpNotifyEvent;

/* FUNCTIONS *****************************************************************/

NTSTATUS INIT_FUNCTION
IopInitPlugPlayEvents(VOID)
{
    InitializeListHead(&IopPnpEventQueueHead);

    KeInitializeEvent(&IopPnpNotifyEvent,
                      SynchronizationEvent,
                      FALSE);

    return STATUS_SUCCESS;
}

NTSTATUS
IopQueueTargetDeviceEvent(const GUID *Guid,
                          PUNICODE_STRING DeviceIds)
{
    PPNP_EVENT_ENTRY EventEntry;
    UNICODE_STRING Copy;
    ULONG TotalSize;
    NTSTATUS Status;

    ASSERT(DeviceIds);

    /* Allocate a big enough buffer */
    Copy.Length = 0;
    Copy.MaximumLength = DeviceIds->Length + sizeof(UNICODE_NULL);
    TotalSize =
        FIELD_OFFSET(PLUGPLAY_EVENT_BLOCK, TargetDevice.DeviceIds) +
        Copy.MaximumLength;

    EventEntry = ExAllocatePool(NonPagedPool,
                                TotalSize + FIELD_OFFSET(PNP_EVENT_ENTRY, Event));
    if (!EventEntry)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Fill the buffer with the event GUID */
    RtlCopyMemory(&EventEntry->Event.EventGuid,
                  Guid,
                  sizeof(GUID));
    EventEntry->Event.EventCategory = TargetDeviceChangeEvent;
    EventEntry->Event.TotalSize = TotalSize;

    /* Fill the device id */
    Copy.Buffer = EventEntry->Event.TargetDevice.DeviceIds;
    Status = RtlAppendUnicodeStringToString(&Copy, DeviceIds);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(EventEntry);
        return Status;
    }

    InsertHeadList(&IopPnpEventQueueHead,
                   &EventEntry->ListEntry);
    KeSetEvent(&IopPnpNotifyEvent,
               0,
               FALSE);

    return STATUS_SUCCESS;
}


static PDEVICE_OBJECT
IopTraverseDeviceNode(PDEVICE_NODE Node, PUNICODE_STRING DeviceInstance)
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_NODE ChildNode;

    if (RtlEqualUnicodeString(&Node->InstancePath,
                              DeviceInstance, TRUE))
    {
        ObReferenceObject(Node->PhysicalDeviceObject);
        return Node->PhysicalDeviceObject;
    }

    /* Traversal of all children nodes */
    for (ChildNode = Node->Child;
         ChildNode != NULL;
         ChildNode = ChildNode->Sibling)
    {
        DeviceObject = IopTraverseDeviceNode(ChildNode, DeviceInstance);
        if (DeviceObject != NULL)
        {
            return DeviceObject;
        }
    }

    return NULL;
}


PDEVICE_OBJECT
IopGetDeviceObjectFromDeviceInstance(PUNICODE_STRING DeviceInstance)
{
    if (IopRootDeviceNode == NULL)
        return NULL;

    if (DeviceInstance == NULL ||
        DeviceInstance->Length == 0)
    {
        if (IopRootDeviceNode->PhysicalDeviceObject)
        {
            ObReferenceObject(IopRootDeviceNode->PhysicalDeviceObject);
            return IopRootDeviceNode->PhysicalDeviceObject;
        }
        else
            return NULL;
    }

    return IopTraverseDeviceNode(IopRootDeviceNode, DeviceInstance);
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * Plug and Play event structure used by NtGetPlugPlayEvent.
 *
 * EventGuid
 *    Can be one of the following values:
 *       GUID_HWPROFILE_QUERY_CHANGE
 *       GUID_HWPROFILE_CHANGE_CANCELLED
 *       GUID_HWPROFILE_CHANGE_COMPLETE
 *       GUID_TARGET_DEVICE_QUERY_REMOVE
 *       GUID_TARGET_DEVICE_REMOVE_CANCELLED
 *       GUID_TARGET_DEVICE_REMOVE_COMPLETE
 *       GUID_PNP_CUSTOM_NOTIFICATION
 *       GUID_PNP_POWER_NOTIFICATION
 *       GUID_DEVICE_* (see above)
 *
 * EventCategory
 *    Type of the event that happened.
 *
 * Result
 *    ?
 *
 * Flags
 *    ?
 *
 * TotalSize
 *    Size of the event block including the device IDs and other
 *    per category specific fields.
 */

/*
 * NtGetPlugPlayEvent
 *
 * Returns one Plug & Play event from a global queue.
 *
 * Parameters
 *    Reserved1
 *    Reserved2
 *       Always set to zero.
 *
 *    Buffer
 *       The buffer that will be filled with the event information on
 *       successful return from the function.
 *
 *    BufferSize
 *       Size of the buffer pointed by the Buffer parameter. If the
 *       buffer size is not large enough to hold the whole event
 *       information, error STATUS_BUFFER_TOO_SMALL is returned and
 *       the buffer remains untouched.
 *
 * Return Values
 *    STATUS_PRIVILEGE_NOT_HELD
 *    STATUS_BUFFER_TOO_SMALL
 *    STATUS_SUCCESS
 *
 * Remarks
 *    This function isn't multi-thread safe!
 *
 * @implemented
 */
NTSTATUS
NTAPI
NtGetPlugPlayEvent(IN ULONG Reserved1,
                   IN ULONG Reserved2,
                   OUT PPLUGPLAY_EVENT_BLOCK Buffer,
                   IN ULONG BufferSize)
{
    PPNP_EVENT_ENTRY Entry;
    NTSTATUS Status;

    DPRINT("NtGetPlugPlayEvent() called\n");

    /* Function can only be called from user-mode */
    if (KeGetPreviousMode() == KernelMode)
    {
        DPRINT1("NtGetPlugPlayEvent cannot be called from kernel mode!\n");
        return STATUS_ACCESS_DENIED;
    }

    /* Check for Tcb privilege */
    if (!SeSinglePrivilegeCheck(SeTcbPrivilege,
                                UserMode))
    {
        DPRINT1("NtGetPlugPlayEvent: Caller does not hold the SeTcbPrivilege privilege!\n");
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    /* Wait for a PnP event */
    DPRINT("Waiting for pnp notification event\n");
    Status = KeWaitForSingleObject(&IopPnpNotifyEvent,
                                   UserRequest,
                                   UserMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status) || Status == STATUS_USER_APC)
    {
        DPRINT("KeWaitForSingleObject() failed (Status %lx)\n", Status);
        ASSERT(Status == STATUS_USER_APC);
        return Status;
    }

    /* Get entry from the tail of the queue */
    Entry = CONTAINING_RECORD(IopPnpEventQueueHead.Blink,
                              PNP_EVENT_ENTRY,
                              ListEntry);

    /* Check the buffer size */
    if (BufferSize < Entry->Event.TotalSize)
    {
        DPRINT1("Buffer is too small for the pnp-event\n");
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Copy event data to the user buffer */
    _SEH2_TRY
    {
        ProbeForWrite(Buffer,
                      Entry->Event.TotalSize,
                      sizeof(UCHAR));
        RtlCopyMemory(Buffer,
                      &Entry->Event,
                      Entry->Event.TotalSize);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    DPRINT("NtGetPlugPlayEvent() done\n");

    return STATUS_SUCCESS;
}

/* unimplemented
 * NtPlugPlayControl
 *
 * A function for doing various Plug & Play operations from user mode.
 *
 * Parameters
 *    PlugPlayControlClass
 *       0x00   Reenumerate device tree
 *
 *              Buffer points to UNICODE_STRING decribing the instance
 *              path (like "HTREE\ROOT\0" or "Root\ACPI_HAL\0000"). For
 *              more information about instance paths see !devnode command
 *              in kernel debugger or look at "Inside Windows 2000" book,
 *              chapter "Driver Loading, Initialization, and Installation".
 *
 *       0x01   Register new device
 *       0x02   Deregister device
 *       0x03   Initialize device
 *       0x04   Start device
 *       0x06   Query and remove device
 *       0x07   User response
 *
 *              Called after processing the message from NtGetPlugPlayEvent.
 *
 *       0x08   Generate legacy device
 *       0x09   Get interface device list
 *       0x0A   Get property data
 *       0x0B   Device class association (Registration)
 *       0x0C   Get related device
 *       0x0D   Get device interface alias
 *       0x0E   Get/set/clear device status
 *       0x0F   Get device depth
 *       0x10   Query device relations
 *       0x11   Query target device relation
 *       0x12   Query conflict list
 *       0x13   Retrieve dock data
 *       0x14   Reset device
 *       0x15   Halt device
 *       0x16   Get blocked driver data
 *
 *    Buffer
 *       The buffer contains information that is specific to each control
 *       code. The buffer is read-only.
 *
 *    BufferSize
 *       Size of the buffer pointed by the Buffer parameter. If the
 *       buffer size specifies incorrect value for specified control
 *       code, error ??? is returned.
 *
 * Return Values
 *    STATUS_PRIVILEGE_NOT_HELD
 *    STATUS_SUCCESS
 *    ...
 */
NTSTATUS
NTAPI
NtPlugPlayControl(IN PLUGPLAY_CONTROL_CLASS PlugPlayControlClass,
                  IN OUT PVOID Buffer,
                  IN ULONG BufferLength)
{
    DPRINT1("NtPlugPlayControl(%d %p %lu) called\n", PlugPlayControlClass, Buffer, BufferLength);
    UNIMPLEMENTED;
    ASSERT(FALSE); // IoDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}
