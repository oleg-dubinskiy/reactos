#ifndef _PNPIO_H
#define _PNPIO_H

#define PIP_SUBKEY_FLAG_SKIP_ERROR  1
#define PIP_SUBKEY_FLAG_DELETE_KEY  2

/* Dump resources flags */
#define PIP_DUMP_FL_ALL_NODES        1
#define PIP_DUMP_FL_RES_ALLOCATED    2
#define PIP_DUMP_FL_RES_REQUIREMENTS 4
#define PIP_DUMP_FL_RES_TRANSLATED   8

typedef struct _PNP_DEVICE_INSTANCE_CONTEXT
{
    PDEVICE_OBJECT DeviceObject;
    PUNICODE_STRING InstancePath;
} PNP_DEVICE_INSTANCE_CONTEXT, *PPNP_DEVICE_INSTANCE_CONTEXT;

/* Request types for PIP_ENUM_REQUEST */
typedef enum _PIP_ENUM_TYPE
{
    PipEnumAddBootDevices,
    PipEnumAssignResources,//PipEnumResourcesAssign
    PipEnumGetSetDeviceStatus,
    PipEnumClearProblem,
    PipEnumInvalidateRelationsInList,
    PipEnumHaltDevice,
    PipEnumBootDevices,//PipEnumBootProcess
    PipEnumDeviceOnly,//PipEnumInvalidateRelations
    PipEnumDeviceTree,//PipEnumInvalidateBusRelations
    PipEnumRootDevices,//PipEnumInitPnpServices
    PipEnumInvalidateDeviceState,
    PipEnumResetDevice,
    PipEnumIoResourceChanged,//PipEnumResourceChange
    PipEnumSystemHiveLimitChange,
    PipEnumSetProblem,
    PipEnumShutdownPnpDevices,
    PipEnumStartDevice,
    PipEnumStartSystemDevices
} PIP_ENUM_TYPE;

typedef struct _PIP_ENUM_REQUEST
{
    LIST_ENTRY RequestLink;
    PDEVICE_OBJECT DeviceObject;
    PIP_ENUM_TYPE RequestType;
    UCHAR ReorderingBarrier;
    UCHAR Padded[3];
    ULONG_PTR RequestArgument;
    PKEVENT CompletionEvent;
    NTSTATUS * CompletionStatus;
} PIP_ENUM_REQUEST, *PPIP_ENUM_REQUEST;

typedef struct _PIP_RESOURCE_REQUEST
{
    PDEVICE_OBJECT PhysicalDevice;
    ULONG Flags;
    ARBITER_REQUEST_SOURCE AllocationType;
    ULONG Priority;
    ULONG Position;
    PIO_RESOURCE_REQUIREMENTS_LIST ResourceRequirements;
    PVOID ReqList;
    PCM_RESOURCE_LIST ResourceAssignment;
    PCM_RESOURCE_LIST TranslatedResourceAssignment;
    NTSTATUS Status;
} PIP_RESOURCE_REQUEST, *PPIP_RESOURCE_REQUEST;

typedef struct _PIP_ASSIGN_RESOURCES_CONTEXT
{
    ULONG DeviceCount;
    BOOLEAN IncludeFailedDevices;
    UCHAR Padded[3];
    PDEVICE_OBJECT DeviceList[1];
} PIP_ASSIGN_RESOURCES_CONTEXT, *PPIP_ASSIGN_RESOURCES_CONTEXT;

typedef BOOLEAN
(NTAPI * PIP_FUNCTION_TO_SUBKEYS)(
    _In_ HANDLE Handle,
    _In_ PUNICODE_STRING Name,
    _In_ PVOID Context
);

/* debug.c */
VOID
NTAPI
PipDumpCmResourceList(
    _In_ PCM_RESOURCE_LIST CmResource,
    _In_ ULONG DebugLevel
);

PCM_PARTIAL_RESOURCE_DESCRIPTOR
NTAPI
PipGetNextCmPartialDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor
);

VOID
NTAPI
PipDumpCmResourceDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG DebugLevel
);

VOID
NTAPI
PipDumpResourceRequirementsList(
    _In_ PIO_RESOURCE_REQUIREMENTS_LIST IoResource,
    _In_ ULONG DebugLevel
);

VOID
NTAPI
PipDumpIoResourceDescriptor(
    _In_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG DebugLevel
);

VOID
NTAPI
PipDumpDeviceNodes(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Flags,
    _In_ ULONG DebugLevel
);

/* pnpenum.c */
NTSTATUS
NTAPI
PipRequestDeviceAction(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIP_ENUM_TYPE RequestType,
    _In_ UCHAR ReorderingBarrier,
    _In_ ULONG_PTR RequestArgument,
    _In_ PKEVENT CompletionEvent,
    _Inout_ NTSTATUS * CompletionStatus
);

/* pnpirp.c */
NTSTATUS
NTAPI
IopQueryDeviceRelations(
    _In_ DEVICE_RELATION_TYPE RelationsType,
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PDEVICE_RELATIONS * OutPendingDeviceRelations
);

/* pnpnode.c */
VOID
NTAPI
PpDevNodeLockTree(
    _In_ ULONG LockLevel
);

VOID
NTAPI
PpDevNodeUnlockTree(
    _In_ ULONG LockLevel
);

VOID
NTAPI
PipSetDevNodeState(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PNP_DEVNODE_STATE NewState,
    _Out_ PNP_DEVNODE_STATE *OutPreviousState
);

VOID
NTAPI
PipSetDevNodeProblem(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Problem
);

VOID
NTAPI
PipClearDevNodeProblem(
    _In_ PDEVICE_NODE DeviceNode
);

VOID
NTAPI
PpDevNodeAssertLockLevel(
    _In_ LONG Level
);

/* pnpres.c */
NTSTATUS
NTAPI
IopWriteResourceList(
    _In_ HANDLE ResourceHandle,
    _In_ PUNICODE_STRING ResourceName,
    _In_ PUNICODE_STRING Description,
    _In_ PUNICODE_STRING ValueName,
    _In_ PCM_RESOURCE_LIST CmResource,
    _In_ ULONG ListSize
);

/* pnputil.c */
NTSTATUS
NTAPI
PnpAllocateUnicodeString(
    _Out_ PUNICODE_STRING String,
    _In_ USHORT Size
);

NTSTATUS
NTAPI
PnpConcatenateUnicodeStrings(
    _Out_ PUNICODE_STRING DestinationString,
    _In_ PUNICODE_STRING SourceString,
    _In_ PUNICODE_STRING AppendString
);

NTSTATUS
NTAPI
IopMapDeviceObjectToDeviceInstance(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PUNICODE_STRING InstancePath
);

PDEVICE_OBJECT
NTAPI
IopDeviceObjectFromDeviceInstance(
    _In_ PUNICODE_STRING DeviceInstance
);

NTSTATUS
NTAPI
PipApplyFunctionToSubKeys(
    _In_opt_ HANDLE RootHandle,
    _In_opt_ PUNICODE_STRING KeyName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ UCHAR Flags,
    _In_ PIP_FUNCTION_TO_SUBKEYS Function,
    _In_ PVOID Context
);

#endif  /* _PNPIO_H */
