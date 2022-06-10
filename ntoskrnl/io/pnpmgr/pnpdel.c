
/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "../pnpio.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern ULONG IopMaxDeviceNodeLevel; 

/* DATA **********************************************************************/

/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
IopEnumerateRelations(
    _In_ PRELATION_LIST RelationsList,
    _In_ PULONG Marker,
    _Out_ PDEVICE_OBJECT * OutEnumDevice,
    _Out_ PUCHAR OutIsDirectDescendant,
    _Out_ PBOOLEAN OutIsTagged,
    _In_ BOOLEAN Direction)
{
    PRELATION_LIST_ENTRY Entry;
    PDEVICE_OBJECT * pDevObject;
    LONG OldIndex;
    ULONG Index;
    LONG Levels;
    LONG ix;

    DPRINT("IopEnumerateRelations: [%p] Direction %X, FirstLevel %X, MaxLevel %X\n", RelationsList, Direction, RelationsList->FirstLevel, RelationsList->MaxLevel);
    PAGED_CODE();

    OldIndex = -1;

    if (*Marker == -1)
    {
        DPRINT("IopEnumerateRelations: *Marker == -1. return FALSE\n");
        return FALSE;
    }

    if (*Marker)
    {
        ASSERT(*Marker & ((ULONG)1 << 31));

        ix = ((*Marker >> 24) & 0x7F);
        OldIndex = (*Marker & 0xFFFFFF);

        DPRINT("IopEnumerateRelations: *Marker %X, ix %X, OldIndex %X\n", *Marker, ix, OldIndex);
    }
    else if (Direction)
    {
        ix = (RelationsList->MaxLevel - RelationsList->FirstLevel);

        DPRINT("IopEnumerateRelations: ix %X, MaxLevel %X, FirstLevel %X\n",
               ix, RelationsList->MaxLevel, RelationsList->FirstLevel);
    }
    else
    {
        DPRINT("IopEnumerateRelations: ix is 0\n");
        ix = 0;
    }

    if (!Direction)
    {
        Levels = (RelationsList->MaxLevel - RelationsList->FirstLevel);

        for (; ix <= Levels; ix++)
        {
            Entry = RelationsList->Entries[ix];
            if (Entry)
            {
                Index = (OldIndex + 1);
                if (Index < Entry->Count)
                {
                    DPRINT("IopEnumerateRelations: Index %X, Entry->Count %X\n", Index, Entry->Count);
                    goto FindOk;
                }
            }

            OldIndex = -1;
        }
    }
    else
    {
        for (; ix >= 0; ix--)
        {
            Entry = RelationsList->Entries[ix];
            if (Entry)
            {
                if (OldIndex > Entry->Count)
                    OldIndex = Entry->Count;

                if (OldIndex > 0)
                {
                    Index = (OldIndex - 1);
                    DPRINT("IopEnumerateRelations: Index %X, Entry->Count %X\n", Index, Entry->Count);
                    goto FindOk;
                }
            }

            OldIndex = -1;
        }
    }

    *Marker = -1;
    *OutEnumDevice = 0;

    if (OutIsTagged)
        *OutIsTagged = 0;

    if (OutIsDirectDescendant)
        *OutIsDirectDescendant = 0;

    return FALSE;

FindOk:

    pDevObject = &Entry->Devices[Index];

    *OutEnumDevice = (PDEVICE_OBJECT)((ULONG_PTR)(*pDevObject) & ~3);

    if (OutIsTagged)
        *OutIsTagged = ((*(PUCHAR)pDevObject & 1) != 0);

    if (OutIsDirectDescendant)
        *OutIsDirectDescendant = (*(PUCHAR)pDevObject & 2);

    *Marker = (((ix | 0xFFFFFF80) << 24) | (Index & 0xFFFFFF));

    return TRUE;
}

PRELATION_LIST
NTAPI
IopAllocateRelationList(
    _In_ PIP_TYPE_REMOVAL_DEVICE RemovalType)
{
    PRELATION_LIST RelationsList;
    ULONG Size;

    PAGED_CODE();
    DPRINT("IopAllocateRelationList: RemovalType - %X, IopMaxDeviceNodeLevel - %X\n", RemovalType, IopMaxDeviceNodeLevel);

    Size = sizeof(RELATION_LIST) + IopMaxDeviceNodeLevel * sizeof(PRELATION_LIST_ENTRY);

    RelationsList = PiAllocateCriticalMemory(RemovalType, PagedPool, Size, 'rcpP');
    if (!RelationsList)
    {
        DPRINT1("IopAllocateRelationList: fail PiAllocateCriticalMemory()\n");
        ASSERT(FALSE);
        return RelationsList;
    }

    RtlZeroMemory(RelationsList, Size);
    RelationsList->MaxLevel = IopMaxDeviceNodeLevel;

    return RelationsList;
}

NTSTATUS
NTAPI
IopAddRelationToList(
    _In_ PRELATION_LIST RelationsList,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN IsDirectDescendant,
    _In_ BOOLEAN IsMarkObject)
{
    PRELATION_LIST_ENTRY Entry;
    PDEVICE_NODE DeviceNode;
    ULONG TagCount;
    ULONG Flags;
    ULONG FirstLevel;
    ULONG Level;
    ULONG Size;
    ULONG ix;

    DPRINT("IopAddRelationToList: RelationsList %p, DeviceObject %p, IsDirectDescendant %X IsMarkObject %X\n",
           RelationsList, DeviceObject, IsDirectDescendant, IsMarkObject);

    PAGED_CODE();

    if (IsMarkObject == FALSE)
    {
        TagCount = 0;
        Flags = 0;
    }
    else
    {
        TagCount = 1;
        Flags = 1;
    }

    if (IsDirectDescendant)
        Flags |= 2;

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (!DeviceNode)
    {
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return STATUS_NO_SUCH_DEVICE;
    }

    Level = DeviceNode->Level;
    FirstLevel = RelationsList->FirstLevel;

    DPRINT("IopAddRelationToList: DeviceNode %p, Level %X\n", DeviceNode, DeviceNode->Level);
    DPRINT("IopAddRelationToList: RelationsList %p, FirstLevel %X MaxLevel %X\n",
           RelationsList, RelationsList->FirstLevel, RelationsList->MaxLevel);

    if (Level < FirstLevel || Level > RelationsList->MaxLevel)
    {
        ASSERT(Level >= FirstLevel && Level <= RelationsList->MaxLevel);
        return STATUS_INVALID_PARAMETER;
    }

    Entry = RelationsList->Entries[Level - FirstLevel];
    if (!Entry)
    {
        Size = (sizeof(RELATION_LIST_ENTRY) + (IopNumberDeviceNodes * sizeof(PDEVICE_OBJECT)));

        Entry = ExAllocatePoolWithTag(PagedPool, Size, 'lrpP');
        if (!Entry)
        {
            DPRINT1("IopAddRelationToList: STATUS_INSUFFICIENT_RESOURCES\n");
            ASSERT(FALSE); // IoDbgBreakPointEx();
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Entry->Count = 0;
        Entry->MaxCount = IopNumberDeviceNodes;

        RelationsList->Entries[Level - FirstLevel] = Entry;
    }

    if (Entry->Count >= Entry->MaxCount)
    {
        ASSERT(Entry->Count < Entry->MaxCount);
        return STATUS_INVALID_PARAMETER;
    }

    for (ix = 0; ix < Entry->Count; ix++)
    {
        if (((ULONG_PTR)Entry->Devices[ix] & (~3)) == (ULONG_PTR)DeviceObject)
        {
            if (IsDirectDescendant)
                Entry->Devices[ix] = (PDEVICE_OBJECT)((ULONG_PTR)Entry->Devices[ix] | 2);

            DPRINT1("IopAddRelationToList: STATUS_OBJECT_NAME_COLLISION\n");
            ASSERT(FALSE); // IoDbgBreakPointEx();
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }

    ObReferenceObject(DeviceObject);

    if (!IsDirectDescendant)
    {
        DPRINT("IopAddRelationToList: %wZ added as a relation\n", &DeviceNode->InstancePath);
    }
    else
    {
        DPRINT("IopAddRelationToList: %wZ added as a relation (direct descendant)\n", &DeviceNode->InstancePath);
    }

    Entry->Devices[ix] = (PDEVICE_OBJECT)((ULONG_PTR)DeviceObject | Flags);
    Entry->Count++;

    RelationsList->Count++;
    RelationsList->TagCount += TagCount;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PiProcessBusRelations(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PIP_TYPE_REMOVAL_DEVICE RemovalType,
    _In_ BOOLEAN IsDirectDescendant,
    _In_ PPNP_VETO_TYPE VetoType,
    _In_ PUNICODE_STRING VetoName,
    _In_ PRELATION_LIST RelationsList)
{
    NTSTATUS Status;

    DPRINT("PiProcessBusRelations: [%p] %X, %X, %p\n", DeviceNode, RemovalType, IsDirectDescendant, RelationsList);
    PAGED_CODE();

    for (DeviceNode = DeviceNode->Child; ; DeviceNode = DeviceNode->Sibling)
    {
        if (!DeviceNode)
            return STATUS_SUCCESS;

        Status = IopProcessRelation(DeviceNode, RemovalType, IsDirectDescendant, VetoType, VetoName, RelationsList);

        ASSERT(Status == STATUS_SUCCESS || Status == STATUS_UNSUCCESSFUL);

        if (Status == STATUS_SUCCESS)
            continue;

        if (!NT_SUCCESS(Status))
            break;
    }

    return Status;
}

NTSTATUS
NTAPI
IopProcessRelation(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PIP_TYPE_REMOVAL_DEVICE RemovalType,
    _In_ BOOLEAN IsDirectDescendant,
    _In_ PPNP_VETO_TYPE VetoType,
    _In_ PUNICODE_STRING VetoName,
    _In_ PRELATION_LIST RelationsList)
{
    PDEVICE_RELATIONS DeviceRelations;
    PNP_DEVNODE_STATE DeviceNodeState;
    NTSTATUS Status;

    DPRINT("IopProcessRelation: [%p] %X, %X, %X, %p\n",
           DeviceNode, RemovalType, IsDirectDescendant, *VetoType, RelationsList);

    PAGED_CODE();

    if (RemovalType == PipQueryRemove || RemovalType == PipEject)
    {
        if (DeviceNode->State == DeviceNodeDeleted ||
            DeviceNode->State == DeviceNodeAwaitingQueuedRemoval ||
            DeviceNode->State == DeviceNodeAwaitingQueuedDeletion)
        {
            DPRINT1("IopProcessRelation: STATUS_UNSUCCESSFUL\n");
            ASSERT(FALSE); // IoDbgBreakPointEx();
            return STATUS_UNSUCCESSFUL;
        }

        if (DeviceNode->State == DeviceNodeRemovePendingCloses ||
            DeviceNode->State == DeviceNodeDeletePendingCloses)
        {
            DPRINT1("IopProcessRelation: STATUS_UNSUCCESSFUL\n");
            ASSERT(FALSE); // IoDbgBreakPointEx();

            *VetoType = PNP_VetoOutstandingOpen;
            RtlCopyUnicodeString(VetoName, &DeviceNode->InstancePath);
            return STATUS_UNSUCCESSFUL;
        }

        if (DeviceNode->State == DeviceNodeStopped ||
            DeviceNode->State == DeviceNodeRestartCompletion)
        {
            DPRINT1("IopProcessRelation: STATUS_INVALID_DEVICE_REQUEST\n");
            ASSERT(FALSE); // IoDbgBreakPointEx();
            return STATUS_INVALID_DEVICE_REQUEST;
        }
    }
    else
    {
        if (DeviceNode->State == DeviceNodeDeleted)
        {
            DPRINT("IopProcessRelation: DeviceNode->State == DeviceNodeDeleted\n");
            ASSERT(!IsDirectDescendant);
            return STATUS_SUCCESS;
        }
    }

    Status = IopAddRelationToList(RelationsList, DeviceNode->PhysicalDeviceObject, IsDirectDescendant, FALSE);
    if (Status != STATUS_SUCCESS)
    {
        if (Status == STATUS_OBJECT_NAME_COLLISION)
        {
            DPRINT1("IopProcessRelation: FIXME Duplicate relation (%p)\n", DeviceNode->PhysicalDeviceObject);
            ASSERT(FALSE); // IoDbgBreakPointEx();
            return Status;
        }

        if (Status == STATUS_INSUFFICIENT_RESOURCES)
        {
            DPRINT1("IopProcessRelation: STATUS_INSUFFICIENT_RESOURCES\n");
            ASSERT(FALSE); // IoDbgBreakPointEx();
            return Status;
        }

        DPRINT1("IopProcessRelation: KeBugCheckEx(PNP_DETECTED_FATAL_ERROR)\n");
        ASSERT(FALSE); // IoDbgBreakPointEx();

        KeBugCheckEx(PNP_DETECTED_FATAL_ERROR,
                     7,
                     (ULONG_PTR)DeviceNode->PhysicalDeviceObject,
                     (ULONG_PTR)RelationsList,
                     Status);

        return Status;
    }

    if (DeviceNode->Flags & DNF_LOCKED_FOR_EJECT)
    {
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return Status;
    }

    Status = PiProcessBusRelations(DeviceNode, RemovalType, IsDirectDescendant, VetoType, VetoName, RelationsList);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessRelation: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return Status;
    }

    DeviceNodeState = DeviceNode->State;

    if (DeviceNodeState == DeviceNodeAwaitingQueuedRemoval ||
        DeviceNodeState == DeviceNodeAwaitingQueuedDeletion)
    {
        DeviceNodeState = DeviceNode->PreviousState;
    }

    if (DeviceNodeState == DeviceNodeStarted ||
        DeviceNodeState == DeviceNodeStopped ||
        DeviceNodeState == DeviceNodeStartPostWork ||
        DeviceNodeState == DeviceNodeRestartCompletion)
    {
        Status = IopQueryDeviceRelations(RemovalRelations, DeviceNode->PhysicalDeviceObject, &DeviceRelations);

        if (NT_SUCCESS(Status) && DeviceRelations)
        {
            DPRINT1("IopProcessRelation: !!!FIXME!!! Status %X\n", Status);
            ASSERT(FALSE); // IoDbgBreakPointEx();
        }
        else if (Status != STATUS_NOT_SUPPORTED)
        {
            DPRINT1("IopProcessRelation: failed, Device %p, Status %X\n", DeviceNode->PhysicalDeviceObject, Status);
        }
    }

    if (RemovalType == PipQueryRemove ||
        RemovalType == PipRemoveFailed ||
        RemovalType == PipRemoveFailedNotStarted)
    {
        return STATUS_SUCCESS;
    }

    Status = IopQueryDeviceRelations(EjectionRelations, DeviceNode->PhysicalDeviceObject, &DeviceRelations);

    if (NT_SUCCESS(Status) && DeviceRelations)
    {
        ASSERT(FALSE); // IoDbgBreakPointEx();
        ExFreePool(DeviceRelations);
    }
    else if (Status != STATUS_NOT_SUPPORTED)
    {
        DPRINT1("IopProcessRelation: failed, Device %p, Status %X\n", DeviceNode->PhysicalDeviceObject, Status);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
IopCompressRelationList(
    _In_ PRELATION_LIST * OutRelationList)
{
    PRELATION_LIST RelationsList;
    PRELATION_LIST_ENTRY Entry;
    PRELATION_LIST_ENTRY NewEntry;
    PRELATION_LIST NewRelationList;
    ULONG LowestLevel;
    ULONG HighestLevel;
    ULONG Size;
    ULONG ix;

    PAGED_CODE();

    RelationsList = *OutRelationList;

    LowestLevel = RelationsList->MaxLevel;
    HighestLevel = RelationsList->FirstLevel;

    DPRINT("IopCompressRelationList: RelationsList - %p, FirstLevel - %X, MaxLevel - %X\n",
           RelationsList, RelationsList->FirstLevel, RelationsList->MaxLevel);

    for (ix = 0;
         ix <= (RelationsList->MaxLevel - RelationsList->FirstLevel);
         ix++)
    {
        Entry = RelationsList->Entries[ix];
        if (!Entry)
        {
            continue;
        }

        if (LowestLevel > ix)
        {
            LowestLevel = ix;
        }

        if (HighestLevel < ix)
        {
            HighestLevel = ix;
        }

        if (Entry->Count >= Entry->MaxCount)
        {
            continue;
        }

        Size = FIELD_OFFSET(RELATION_LIST_ENTRY, Devices) +
               Entry->Count * sizeof(PDEVICE_OBJECT);

        NewEntry = ExAllocatePoolWithTag(PagedPool, Size, 'lrpP');
        if (!NewEntry)
        {
            DPRINT1("IopCompressRelationList: NewEntry == NULL\n");
            ASSERT(FALSE);
            continue;
        }

        NewEntry->Count = Entry->Count;
        NewEntry->MaxCount = Entry->Count;

        RtlCopyMemory(NewEntry->Devices,
                      Entry->Devices,
                      Entry->Count * sizeof(PDEVICE_OBJECT));

        RelationsList->Entries[ix] = NewEntry;

        ExFreePoolWithTag(Entry, 'lrpP');
    }

    ASSERT(LowestLevel <= HighestLevel);

    if (LowestLevel > HighestLevel)
    {
        LowestLevel = 0;
        HighestLevel = 0;
    }

    if (LowestLevel == RelationsList->FirstLevel &&
        HighestLevel == RelationsList->MaxLevel)
    {
        ASSERT(FALSE);
        return;
    }

    Size = sizeof(RELATION_LIST) +
           (HighestLevel - LowestLevel) * sizeof(PRELATION_LIST_ENTRY);

    NewRelationList = ExAllocatePoolWithTag(PagedPool, Size, 'lrpP');
    if (!NewRelationList)
    {
        DPRINT("IopCompressRelationList: ExAllocatePoolWithTag() failed\n");
        ASSERT(FALSE);
        return;
    }

    NewRelationList->Count = RelationsList->Count;
    NewRelationList->TagCount = RelationsList->TagCount;

    NewRelationList->FirstLevel = LowestLevel;
    NewRelationList->MaxLevel = HighestLevel;

    RtlCopyMemory(NewRelationList->Entries,
                  &RelationsList->Entries[LowestLevel],
                  (HighestLevel - LowestLevel + 1) * sizeof(PRELATION_LIST_ENTRY));

    ExFreePoolWithTag(RelationsList, 'rcpP');

    *OutRelationList = NewRelationList;
}

NTSTATUS
NTAPI
IopBuildRemovalRelationList(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIP_TYPE_REMOVAL_DEVICE RemovalType,
    _In_ PPNP_VETO_TYPE VetoType,
    _In_ PUNICODE_STRING VetoName,
    _Out_ PRELATION_LIST * OutRelationList)
{
    PDEVICE_NODE DeviceNode;
    PRELATION_LIST RelationsList;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("IopBuildRemovalRelationList: DeviceObject - %p, RemovalType - %X\n",
           DeviceObject, RemovalType);

    *OutRelationList = NULL;
    DeviceNode = IopGetDeviceNode(DeviceObject);

    ASSERT(DeviceObject != IopRootDeviceNode->PhysicalDeviceObject);

    RelationsList = IopAllocateRelationList(RemovalType);
    if (!RelationsList)
    {
        DPRINT1("IopBuildRemovalRelationList: return STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IopProcessRelation(DeviceNode,
                                RemovalType,
                                TRUE,
                                VetoType,
                                VetoName,
                                RelationsList);

    ASSERT(Status != STATUS_INVALID_DEVICE_REQUEST);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("IopBuildRemovalRelationList: Status - %X\n", Status);
        IopFreeRelationList(RelationsList);
        return Status;
    }

    IopCompressRelationList(&RelationsList);
    *OutRelationList = RelationsList;

    return Status;
}

NTSTATUS
PipRequestDeviceRemovalWorker(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PVOID Context)
{
    PPNP_REMOVAL_WALK_CONTEXT RemovalContext = Context;
    PNP_DEVNODE_STATE AwaitingState;

    PAGED_CODE();
    DPRINT("PipRequestDeviceRemovalWorker: DeviceNode - %p, DeviceNode->State - %X, TreeDeletion - %X\n",
           DeviceNode, DeviceNode->State, RemovalContext->TreeDeletion);

    switch (DeviceNode->State)
    {
        case DeviceNodeInitialized:
        case DeviceNodeDriversAdded:
        case DeviceNodeStarted:
            break;

        case DeviceNodeUninitialized:
        case DeviceNodeResourcesAssigned:
        case DeviceNodeRemovePendingCloses:
        case DeviceNodeRemoved:
            ASSERT(RemovalContext->TreeDeletion);
            break;

        case DeviceNodeStartCompletion:
        case DeviceNodeStartPostWork:
        case DeviceNodeStopped:
        case DeviceNodeRestartCompletion:
            ASSERT(!RemovalContext->DescendantNode);
            ASSERT(!RemovalContext->TreeDeletion);
            break;

        case DeviceNodeAwaitingQueuedDeletion:
        case DeviceNodeAwaitingQueuedRemoval:
            ASSERT(RemovalContext->TreeDeletion);
            PipRestoreDevNodeState(DeviceNode);
            PipSetDevNodeState(DeviceNode, DeviceNodeAwaitingQueuedDeletion, NULL);
            return STATUS_SUCCESS;

        case DeviceNodeStartPending:
        case DeviceNodeQueryStopped:
        case DeviceNodeEnumeratePending:
        default:
            ASSERT(FALSE);
            break;
    }

    if (RemovalContext->TreeDeletion)
    {
        AwaitingState = DeviceNodeAwaitingQueuedDeletion;
    }
    else
    {
        AwaitingState = DeviceNodeAwaitingQueuedRemoval;
    }

    PipSetDevNodeState(DeviceNode, AwaitingState, NULL);

    RemovalContext->DescendantNode = TRUE;
    RemovalContext->TreeDeletion = TRUE;

    return STATUS_SUCCESS;
}

VOID
NTAPI
PipRequestDeviceRemoval(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BOOLEAN TreeDeletion,
    _In_ ULONG Problem)
{
    DEVICETREE_TRAVERSE_CONTEXT Context;
    PNP_REMOVAL_WALK_CONTEXT RemovalContext;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("PipRequestDeviceRemoval: [%p] TreeDeletion %X, Problem %X\n", DeviceNode, TreeDeletion, Problem);

    if (DeviceNode == NULL)
    {
        DPRINT("PipRequestDeviceRemoval: DeviceNode == NULL\n");
        ASSERT(DeviceNode);
        return;
    }

    if (!DeviceNode->InstancePath.Length == 0)
    {
        DPRINT("PipRequestDeviceRemoval: Driver '%wZ', child DeviceNode %p\n", &DeviceNode->Parent->ServiceName, DeviceNode);
        ASSERT(DeviceNode->InstancePath.Length != 0);
    }

    PpDevNodeAssertLockLevel(1);

    RemovalContext.TreeDeletion = TreeDeletion;
    RemovalContext.DescendantNode = FALSE;

    IopInitDeviceTreeTraverseContext(&Context,
                                     DeviceNode,
                                     PipRequestDeviceRemovalWorker,
                                     &RemovalContext);

    Status = IopTraverseDeviceTree(&Context);

    DPRINT("PipRequestDeviceRemoval: Status %X\n", Status);
    ASSERT(NT_SUCCESS(Status));

    PpSetTargetDeviceRemove(DeviceNode->PhysicalDeviceObject,
                            TRUE,
                            TRUE,
                            FALSE,
                            FALSE,
                            Problem,
                            NULL,
                            NULL,
                            NULL,
                            NULL);
}

VOID
NTAPI
IopSurpriseRemoveLockedDeviceNode(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PRELATION_LIST RelationsList)
{
    PNP_DEVNODE_STATE SchedulerState = DeviceNode->State;
    PNP_DEVNODE_STATE RestoredState;
    PNP_DEVNODE_STATE NewState;
    PDEVICE_NODE ChildNode;
    PDEVICE_NODE Sibling;
    NTSTATUS Status;
  
    DPRINT("IopSurpriseRemoveLockedDeviceNode: [%p] %p, SchedulerState %X\n", DeviceNode, RelationsList, SchedulerState);
    PAGED_CODE();

    ASSERT((SchedulerState == DeviceNodeAwaitingQueuedDeletion) || (SchedulerState == DeviceNodeAwaitingQueuedRemoval));

    PipRestoreDevNodeState(DeviceNode);
    RestoredState = DeviceNode->State;

    PpHotSwapInitRemovalPolicy(DeviceNode);

    if (RestoredState == DeviceNodeRemovePendingCloses)
    {
        ASSERT(SchedulerState == DeviceNodeAwaitingQueuedDeletion);
        PipSetDevNodeState(DeviceNode, DeviceNodeDeletePendingCloses, NULL);
        return;
    }

    for (ChildNode = DeviceNode->Child; ChildNode; ChildNode = Sibling)
    {
        Sibling = ChildNode->Sibling;

        if (ChildNode->Flags & DNF_ENUMERATED)
            ChildNode->Flags &= ~DNF_ENUMERATED;

        if (ChildNode->ResourceList || ChildNode->BootResources || ChildNode->Flags & DNF_HAS_BOOT_CONFIG)
        {
            DPRINT("IopSurpriseRemoveLockedDeviceNode: Releasing resources for %p\n", ChildNode->PhysicalDeviceObject);
            IopReleaseDeviceResources(ChildNode, FALSE);
        }

        PipSetDevNodeState(ChildNode, DeviceNodeDeletePendingCloses, NULL);
    }

    Status = IopRemoveDevice(DeviceNode->PhysicalDeviceObject, IRP_MN_SURPRISE_REMOVAL);

    if (RestoredState != DeviceNodeStarted &&
        RestoredState != DeviceNodeStopped &&
        RestoredState != DeviceNodeStartPostWork &&
        RestoredState != DeviceNodeRestartCompletion)
    {
        ASSERT(DeviceNode->DockInfo.DockStatus != DOCK_ARRIVING);
        return;
    }

    DPRINT("IopSurpriseRemoveLockedDeviceNode: Sending surprise remove to %p\n", DeviceNode->PhysicalDeviceObject);
    IopDisableDeviceInterfaces(&DeviceNode->InstancePath);

    if (NT_SUCCESS(Status))
    {
        DPRINT("IopSurpriseRemoveLockedDeviceNode: Releasing devices resources\n");
        IopReleaseDeviceResources(DeviceNode, FALSE);
    }
    else
    {
        DPRINT("IopSurpriseRemoveLockedDeviceNode: Status %X\n", Status);
    }

    if (DeviceNode->Flags & DNF_ENUMERATED)
    {
        NewState = DeviceNodeRemovePendingCloses;
    }
    else
    {
        ASSERT(SchedulerState == DeviceNodeAwaitingQueuedDeletion);
        NewState = DeviceNodeDeletePendingCloses;
    }

    PipSetDevNodeState(DeviceNode, NewState, NULL);

    ASSERT(DeviceNode->DockInfo.DockStatus != DOCK_ARRIVING);
}

VOID
NTAPI
IopRemoveLockedDeviceNode(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Problem,
    _In_ PRELATION_LIST RelationsList)

{
    UNIMPLEMENTED;
    ASSERT(FALSE); // IoDbgBreakPointEx();
}

BOOLEAN
NTAPI
IopDeleteLockedDeviceNode(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PIP_TYPE_REMOVAL_DEVICE RemovalType,
    _In_ PRELATION_LIST RelationsList,
    _In_ ULONG Problem,
    _In_ PPNP_VETO_TYPE VetoType,
    _In_ PUNICODE_STRING VetoName)
{
    BOOLEAN Result = TRUE;

    DPRINT("IopDeleteLockedDeviceNode: [%p] %p, %X, %X\n", DeviceNode, RelationsList, RemovalType, Problem);
    PAGED_CODE();

    switch (RemovalType)
    {
        case PipQueryRemove:
            ASSERT(VetoType && VetoName);
            DPRINT1("IopDeleteLockedDeviceNode: FIXME IopQueryRemoveLockedDeviceNode()\n");
            ASSERT(FALSE); // IoDbgBreakPointEx();
            //Result = IopQueryRemoveLockedDeviceNode(DeviceNode, VetoType, VetoName);
            return Result;

        case PipCancelRemove:
            DPRINT1("IopDeleteLockedDeviceNode: FIXME IopCancelRemoveLockedDeviceNode()\n");
            ASSERT(FALSE); // IoDbgBreakPointEx();
            //IopCancelRemoveLockedDeviceNode(DeviceNode);
            break;

        case PipRemove:
            IopRemoveLockedDeviceNode(DeviceNode, Problem, RelationsList);
            break;

        case PipSurpriseRemove:
            IopSurpriseRemoveLockedDeviceNode(DeviceNode, RelationsList);
            break;

        default:
            DPRINT("IopDeleteLockedDeviceNode: Unknown RemovalType %X\n", RemovalType);
            ASSERT(FALSE); // IoDbgBreakPointEx();
            break;
    }

    return Result;
}

NTSTATUS
NTAPI
IopDeleteLockedDeviceNodes(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PRELATION_LIST RelationsList,
    _In_ PIP_TYPE_REMOVAL_DEVICE RemovalType,
    _In_ BOOLEAN IsForceDescendant,
    _In_ ULONG Problem,
    _In_ PPNP_VETO_TYPE VetoType,
    _In_ PUNICODE_STRING VetoName)
{
    PDEVICE_OBJECT deviceObject;
    PDEVICE_NODE DeviceNode;
    ULONG Marker = 0;
    BOOLEAN IsDirectDescendant;
    BOOLEAN Result;

    DPRINT("IopDeleteLockedDeviceNodes: [%p] List %p, Type %X\n", DeviceObject, RelationsList, RemovalType);
    PAGED_CODE();

    while (TRUE)
    {
        Result = IopEnumerateRelations(RelationsList, &Marker, &deviceObject, &IsDirectDescendant, NULL, TRUE);
        if (!Result)
            break;

        if (IsDirectDescendant && IsForceDescendant)
            continue;

        DeviceNode = IopGetDeviceNode(deviceObject);
        DPRINT("IopDeleteLockedDeviceNodes: DeviceNode %p\n", DeviceNode);

        if (IopDeleteLockedDeviceNode(DeviceNode, RemovalType, RelationsList, Problem, VetoType, VetoName))
            continue;

        DPRINT("IopDeleteLockedDeviceNodes: RemovalType %X\n", RemovalType);
        ASSERT(RemovalType == PipQueryRemove);

        while (IopEnumerateRelations(RelationsList, &Marker, &deviceObject, NULL, NULL, FALSE))
        {
            DeviceNode = IopGetDeviceNode(deviceObject);
            DPRINT("IopDeleteLockedDeviceNodes: DeviceNode %p\n", DeviceNode);
            IopDeleteLockedDeviceNode(DeviceNode, PipCancelRemove, RelationsList, Problem, VetoType, VetoName);
        }

        DPRINT("IopDeleteLockedDeviceNodes: return STATUS_UNSUCCESSFUL\n");
        return STATUS_UNSUCCESSFUL;
    }

    DPRINT("IopDeleteLockedDeviceNodes: return STATUS_SUCCESS\n");
    return STATUS_SUCCESS;
}

VOID
NTAPI
IopFreeRelationList(
    _In_ PRELATION_LIST RelationsList)
{
    PRELATION_LIST_ENTRY Entry;
    PDEVICE_OBJECT DeviceObject;
    ULONG ix;
    ULONG jx;

    PAGED_CODE();
    DPRINT("IopFreeRelationList: RelationsList %p, %X - %X\n", RelationsList, RelationsList->FirstLevel, RelationsList->MaxLevel);

    for (ix = 0; ix <= (RelationsList->MaxLevel - RelationsList->FirstLevel); ix++)
    {
        Entry = RelationsList->Entries[ix];
        if (!Entry)
            continue;

        for (jx = 0; jx < Entry->Count; jx++)
        {
            DeviceObject = Entry->Devices[jx];
            ObDereferenceObject((PVOID)((ULONG_PTR)DeviceObject & 0xFFFFFFFC));
        }

        ExFreePoolWithTag(Entry, 0);
    }

    ExFreePoolWithTag(RelationsList, 0);
}

VOID
NTAPI
IopInvalidateRelationsInList(
    _In_ PRELATION_LIST RelationsList,
    _In_ PIP_TYPE_REMOVAL_DEVICE RemovalType,
    _In_ BOOLEAN Param3,
    _In_ BOOLEAN Param4)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // IoDbgBreakPointEx();
}

VOID
NTAPI
IopUnlinkDeviceRemovalRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PRELATION_LIST RelationsList,
    _In_ ULONG Type)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // IoDbgBreakPointEx();
}

VOID
NTAPI
IopQueuePendingSurpriseRemoval(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PRELATION_LIST RelationsList,
    _In_ ULONG Problem)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // IoDbgBreakPointEx();
}

/* EOF */
