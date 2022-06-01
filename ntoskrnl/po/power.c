/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/po/power.c
 * PURPOSE:         Power Manager
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 *                  Hervé Poussineau (hpoussin@reactos.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

typedef struct _POWER_STATE_TRAVERSE_CONTEXT
{
    SYSTEM_POWER_STATE SystemPowerState;
    POWER_ACTION PowerAction;
    PDEVICE_OBJECT PowerDevice;
} POWER_STATE_TRAVERSE_CONTEXT, *PPOWER_STATE_TRAVERSE_CONTEXT;

typedef struct _SYSTEM_POWER_LEVEL
{
    BOOLEAN Enable;
    UCHAR Spare[3];
    ULONG BatteryLevel;
    POWER_ACTION_POLICY PowerPolicy;
    SYSTEM_POWER_STATE MinSystemState;
} SYSTEM_POWER_LEVEL, *PSYSTEM_POWER_LEVEL;

typedef struct _SYSTEM_POWER_POLICY
{
    ULONG Revision;
    POWER_ACTION_POLICY PowerButton;
    POWER_ACTION_POLICY SleepButton;
    POWER_ACTION_POLICY LidClose;
    SYSTEM_POWER_STATE LidOpenWake;
    ULONG Reserved;
    POWER_ACTION_POLICY Idle;
    ULONG IdleTimeout;
    UCHAR IdleSensitivity;
    UCHAR DynamicThrottle;
    UCHAR Spare2[2];
    SYSTEM_POWER_STATE MinSleep;
    SYSTEM_POWER_STATE MaxSleep;
    SYSTEM_POWER_STATE ReducedLatencySleep;
    ULONG WinLogonFlags;
    ULONG Spare3;
    ULONG DozeS4Timeout;
    ULONG BroadcastCapacityResolution;
    SYSTEM_POWER_LEVEL DischargePolicy[4];
    ULONG VideoTimeout;
    BOOLEAN VideoDimDisplay;
    UCHAR Pad[0x3];
    ULONG VideoReserved[3];
    ULONG SpindownTimeout;
    BOOLEAN OptimizeForPower;
    UCHAR FanThrottleTolerance;
    UCHAR ForcedThrottle;
    UCHAR MinThrottle;
    POWER_ACTION_POLICY OverThrottled;
} SYSTEM_POWER_POLICY, *PSYSTEM_POWER_POLICY;

PDEVICE_NODE PopSystemPowerDeviceNode = NULL;
BOOLEAN PopAcpiPresent = FALSE;
POP_POWER_ACTION PopAction;
WORK_QUEUE_ITEM PopShutdownWorkItem;
SYSTEM_POWER_CAPABILITIES PopCapabilities;
ERESOURCE PopPolicyLock;
PKTHREAD PopPolicyLockThread = NULL;
SYSTEM_POWER_POLICY PopAcPolicy;
SYSTEM_POWER_POLICY PopDcPolicy;
PSYSTEM_POWER_POLICY PopPolicy;
POWER_STATE_HANDLER PopPowerStateHandlers[7];

extern KSPIN_LOCK IopPnPSpinLock;
extern LIST_ENTRY IopPnpEnumerationRequestList;
extern KEVENT PiEnumerationLock;
extern BOOLEAN PipEnumerationInProgress;

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
PopAcquirePolicyLock(VOID)
{
    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PopPolicyLock, TRUE);

    ASSERT(PopPolicyLockThread == NULL);
    PopPolicyLockThread = KeGetCurrentThread();
}

VOID
NTAPI
PopReleasePolicyLock(
    _In_ BOOLEAN IsQueuePolicyWorker)
{
    ASSERT(PopPolicyLockThread == KeGetCurrentThread());

    PopPolicyLockThread = NULL;
    ExReleaseResourceLite(&PopPolicyLock);

    if (IsQueuePolicyWorker)
    {
        DPRINT("PopReleasePolicyLock: FIXME! IsQueuePolicyWorker is TRUE.\n");
        ASSERT(FALSE); // PoDbgBreakPointEx();
    }

    KeLeaveCriticalRegion();
}

VOID
NTAPI
PopDefaultPolicy(
    _In_ PSYSTEM_POWER_POLICY Policy)
{
    ULONG ix;

    RtlZeroMemory(Policy, sizeof(*Policy));

    Policy->Revision = 1;
    Policy->LidOpenWake = PowerSystemWorking;
    Policy->PowerButton.Action = PowerActionShutdownOff;
    Policy->SleepButton.Action = PowerActionSleep;
    Policy->LidClose.Action = PowerActionNone;
    Policy->MinSleep = PowerSystemSleeping1;
    Policy->MaxSleep = PowerActionShutdown;
    Policy->ReducedLatencySleep = PowerSystemSleeping1;
    Policy->WinLogonFlags = 0;
    Policy->FanThrottleTolerance = 100;
    Policy->ForcedThrottle = 100;
    Policy->OverThrottled.Action = PowerActionNone;
    Policy->BroadcastCapacityResolution = 25;

    for (ix = 0; ix < NUM_DISCHARGE_POLICIES; ix++)
        Policy->DischargePolicy[ix].MinSystemState = PowerSystemSleeping1;
}

static WORKER_THREAD_ROUTINE PopPassivePowerCall;
_Use_decl_annotations_
static
VOID
NTAPI
PopPassivePowerCall(
    PVOID Parameter)
{
    PIRP Irp = Parameter;
    PIO_STACK_LOCATION IoStack;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    _Analysis_assume_(Irp != NULL);
    IoStack = IoGetNextIrpStackLocation(Irp);

    (VOID)IoCallDriver(IoStack->DeviceObject, Irp);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
static
NTSTATUS
PopPresentIrp(
    _In_ PIO_STACK_LOCATION NextStack,
    _In_ PIRP Irp)
{
    NTSTATUS Status;
    BOOLEAN CallAtPassiveLevel;
    PDEVICE_OBJECT DeviceObject;
    PWORK_QUEUE_ITEM WorkQueueItem;

    ASSERT(NextStack->MajorFunction == IRP_MJ_POWER);

    DeviceObject = NextStack->DeviceObject;

    /* Determine whether the IRP must be handled at PASSIVE_LEVEL.
     * Only SET_POWER to working state can happen at raised IRQL. */
    CallAtPassiveLevel = TRUE;
    if ((NextStack->MinorFunction == IRP_MN_SET_POWER) &&
        !(DeviceObject->Flags & DO_POWER_PAGABLE))
    {
        if (NextStack->Parameters.Power.Type == DevicePowerState &&
            NextStack->Parameters.Power.State.DeviceState == PowerDeviceD0)
        {
            CallAtPassiveLevel = FALSE;
        }
        if (NextStack->Parameters.Power.Type == SystemPowerState &&
            NextStack->Parameters.Power.State.SystemState == PowerSystemWorking)
        {
            CallAtPassiveLevel = FALSE;
        }
    }

    if (CallAtPassiveLevel)
    {
        /* We need to fit a work item into the DriverContext below */
        C_ASSERT(sizeof(Irp->Tail.Overlay.DriverContext) >= sizeof(WORK_QUEUE_ITEM));

        if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        {
            /* Already at passive, call next driver directly */
            return IoCallDriver(DeviceObject, Irp);
        }

        /* Need to schedule a work item and return pending */
        NextStack->Control |= SL_PENDING_RETURNED;

        WorkQueueItem = (PWORK_QUEUE_ITEM)&Irp->Tail.Overlay.DriverContext;
        ExInitializeWorkItem(WorkQueueItem,
                             PopPassivePowerCall,
                             Irp);
        ExQueueWorkItem(WorkQueueItem, DelayedWorkQueue);

        return STATUS_PENDING;
    }

    /* Direct call. Raise IRQL in debug to catch invalid paged memory access. */
#if DBG
    {
    KIRQL OldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
#endif

    Status = IoCallDriver(DeviceObject, Irp);

#if DBG
    KeLowerIrql(OldIrql);
    }
#endif

    return Status;
}

static
NTSTATUS
NTAPI
PopRequestPowerIrpCompletion(IN PDEVICE_OBJECT DeviceObject,
                             IN PIRP Irp,
                             IN PVOID Context)
{
    PIO_STACK_LOCATION Stack;
    PREQUEST_POWER_COMPLETE CompletionRoutine;
    POWER_STATE PowerState;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    CompletionRoutine = Context;

    PowerState.DeviceState = (ULONG_PTR)Stack->Parameters.Others.Argument3;
    CompletionRoutine(Stack->Parameters.Others.Argument1,
                      (UCHAR)(ULONG_PTR)Stack->Parameters.Others.Argument2,
                      PowerState,
                      Stack->Parameters.Others.Argument4,
                      &Irp->IoStatus);

    IoSkipCurrentIrpStackLocation(Irp);
    IoFreeIrp(Irp);
    ObDereferenceObject(DeviceObject);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
NTAPI
PopCleanupPowerState(IN PPOWER_STATE PowerState)
{
    //UNIMPLEMENTED;
}

NTSTATUS
PopSendQuerySystemPowerState(PDEVICE_OBJECT DeviceObject, SYSTEM_POWER_STATE SystemState, POWER_ACTION PowerAction)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpSp;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event,
                      NotificationEvent,
                      FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_POWER,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);
    if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;

    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->MinorFunction = IRP_MN_QUERY_POWER;
    IrpSp->Parameters.Power.Type = SystemPowerState;
    IrpSp->Parameters.Power.State.SystemState = SystemState;
    IrpSp->Parameters.Power.ShutdownType = PowerAction;

    Status = PoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

NTSTATUS
PopSendSetSystemPowerState(PDEVICE_OBJECT DeviceObject, SYSTEM_POWER_STATE SystemState, POWER_ACTION PowerAction)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpSp;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event,
                      NotificationEvent,
                      FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_POWER,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);
    if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;

    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->MinorFunction = IRP_MN_SET_POWER;
    IrpSp->Parameters.Power.Type = SystemPowerState;
    IrpSp->Parameters.Power.State.SystemState = SystemState;
    IrpSp->Parameters.Power.ShutdownType = PowerAction;

    Status = PoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

NTSTATUS
PopQuerySystemPowerStateTraverse(PDEVICE_NODE DeviceNode,
                                 PVOID Context)
{
    PPOWER_STATE_TRAVERSE_CONTEXT PowerStateContext = Context;
    PDEVICE_OBJECT TopDeviceObject;
    NTSTATUS Status;

    DPRINT("PopQuerySystemPowerStateTraverse(%p, %p)\n", DeviceNode, Context);

    if (DeviceNode == IopRootDeviceNode)
        return STATUS_SUCCESS;

    if (DeviceNode->Flags & DNF_LEGACY_DRIVER)
        return STATUS_SUCCESS;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceNode->PhysicalDeviceObject);

    Status = PopSendQuerySystemPowerState(TopDeviceObject,
                                          PowerStateContext->SystemPowerState,
                                          PowerStateContext->PowerAction);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Device '%wZ' failed IRP_MN_QUERY_POWER\n", &DeviceNode->InstancePath);
    }
    ObDereferenceObject(TopDeviceObject);

#if 0
    return Status;
#else
    return STATUS_SUCCESS;
#endif
}

NTSTATUS
PopSetSystemPowerStateTraverse(PDEVICE_NODE DeviceNode,
                               PVOID Context)
{
    PPOWER_STATE_TRAVERSE_CONTEXT PowerStateContext = Context;
    PDEVICE_OBJECT TopDeviceObject;
    NTSTATUS Status;

    DPRINT("PopSetSystemPowerStateTraverse(%p, %p)\n", DeviceNode, Context);

    if (DeviceNode == IopRootDeviceNode)
        return STATUS_SUCCESS;

    if (DeviceNode->PhysicalDeviceObject == PowerStateContext->PowerDevice)
        return STATUS_SUCCESS;

    if (DeviceNode->Flags & DNF_LEGACY_DRIVER)
        return STATUS_SUCCESS;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceNode->PhysicalDeviceObject);
    if (TopDeviceObject == PowerStateContext->PowerDevice)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_SUCCESS;
    }

    Status = PopSendSetSystemPowerState(TopDeviceObject,
                                        PowerStateContext->SystemPowerState,
                                        PowerStateContext->PowerAction);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Device '%wZ' failed IRP_MN_SET_POWER\n", &DeviceNode->InstancePath);
    }

    ObDereferenceObject(TopDeviceObject);

#if 0
    return Status;
#else
    return STATUS_SUCCESS;
#endif
}

INIT_FUNCTION
BOOLEAN
NTAPI
PoInitSystem(IN ULONG BootPhase)
{
    PVOID NotificationEntry;
    PCHAR CommandLine;
    BOOLEAN ForceAcpiDisable = FALSE;

    /* Check if this is phase 1 init */
    if (BootPhase == 1)
    {
        /* Register power button notification */
        IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                       PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                       (PVOID)&GUID_DEVICE_SYS_BUTTON,
                                       IopRootDeviceNode->
                                       PhysicalDeviceObject->DriverObject,
                                       PopAddRemoveSysCapsCallback,
                                       NULL,
                                       &NotificationEntry);

        /* Register lid notification */
        IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                       PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                       (PVOID)&GUID_DEVICE_LID,
                                       IopRootDeviceNode->
                                       PhysicalDeviceObject->DriverObject,
                                       PopAddRemoveSysCapsCallback,
                                       NULL,
                                       &NotificationEntry);
        return TRUE;
    }

    /* Initialize the power capabilities */
    RtlZeroMemory(&PopCapabilities, sizeof(SYSTEM_POWER_CAPABILITIES));

    /* Get the Command Line */
    CommandLine = KeLoaderBlock->LoadOptions;

    /* Upcase it */
    _strupr(CommandLine);

    /* Check for ACPI disable */
    if (strstr(CommandLine, "NOACPI")) ForceAcpiDisable = TRUE;

    if (ForceAcpiDisable)
    {
        /* Set the ACPI State to False if it's been forced that way */
        PopAcpiPresent = FALSE;
    }
    else
    {
        /* Otherwise check if the LoaderBlock has a ACPI Table  */
        PopAcpiPresent = KeLoaderBlock->Extension->AcpiTable != NULL ? TRUE : FALSE;
    }

    /* Enable shutdown by power button */
    if (PopAcpiPresent)
        PopCapabilities.SystemS5 = TRUE;

    /* Initialize support for shutdown waits and work-items */
    PopInitShutdownList();

    /* Initialize volume support */
    InitializeListHead(&PopVolumeDevices);
    KeInitializeGuardedMutex(&PopVolumeLock);

    /* Initialize support for dope */
    KeInitializeSpinLock(&PopDopeGlobalLock);

    ExInitializeResourceLite(&PopPolicyLock);

    PopDefaultPolicy(&PopAcPolicy);
    PopDefaultPolicy(&PopDcPolicy);
    PopPolicy = &PopAcPolicy;


    return TRUE;
}

VOID
NTAPI
PopPerfIdle(PPROCESSOR_POWER_STATE PowerState)
{
    DPRINT1("PerfIdle function: %p\n", PowerState);
}

VOID
NTAPI
PopPerfIdleDpc(IN PKDPC Dpc,
               IN PVOID DeferredContext,
               IN PVOID SystemArgument1,
               IN PVOID SystemArgument2)
{
    /* Call the Perf Idle function */
    PopPerfIdle(&((PKPRCB)DeferredContext)->PowerState);
}

VOID
FASTCALL
PopIdle0(IN PPROCESSOR_POWER_STATE PowerState)
{
    /* FIXME: Extremly naive implementation */
    HalProcessorIdle();
}

INIT_FUNCTION
VOID
NTAPI
PoInitializePrcb(IN PKPRCB Prcb)
{
    /* Initialize the Power State */
    RtlZeroMemory(&Prcb->PowerState, sizeof(Prcb->PowerState));
    Prcb->PowerState.Idle0KernelTimeLimit = 0xFFFFFFFF;
    Prcb->PowerState.CurrentThrottle = 100;
    Prcb->PowerState.CurrentThrottleIndex = 0;
    Prcb->PowerState.IdleFunction = PopIdle0;

    /* Initialize the Perf DPC and Timer */
    KeInitializeDpc(&Prcb->PowerState.PerfDpc, PopPerfIdleDpc, Prcb);
    KeSetTargetProcessorDpc(&Prcb->PowerState.PerfDpc, Prcb->Number);
    KeInitializeTimerEx(&Prcb->PowerState.PerfTimer, SynchronizationTimer);
}

VOID
NTAPI
PopResetActionDefaults(
    VOID)
{
    DPRINT("PopResetActionDefaults()\n");

    PopAction.Updates = 0;
    PopAction.Shutdown = FALSE;
    PopAction.Action = PowerActionNone;
    PopAction.LightestState = PowerSystemUnspecified;
    PopAction.Status = STATUS_SUCCESS;
    PopAction.IrpMinor = 0;
    PopAction.SystemState = PowerSystemUnspecified;
    PopAction.Flags = 0x10000003;
}

PCHAR
NTAPI
PopSystemStateString(
    _In_ SYSTEM_POWER_STATE SystemState)
{
    PCHAR String;

    if (SystemState == PowerSystemUnspecified)
    {
        String = "Unspecified";
        return String;
    }

    switch (SystemState)
    {
        case PowerSystemWorking:
            String = "Working";
            break;

        case PowerSystemSleeping1:
            String = "Sleeping1";
            break;

        case PowerSystemSleeping2:
            String = "Sleeping2";
            break;

        case PowerSystemSleeping3:
            String = "Sleeping3";
            break;

        case PowerSystemHibernate:
            String = "Hibernate";
            break;

        case PowerSystemShutdown:
            String = "Shutdown";
            break;

        default:
            String = "?";
            break;
    }

    return String;
}

PCHAR
NTAPI
PopPowerActionString(
    _In_ POWER_ACTION Action)
{
    PCHAR String;

    if (Action == PowerActionNone)
    {
        String = "None";
        return String;
    }

    switch (Action)
    {
        case PowerActionSleep:
            String = "Sleep";
            break;

        case PowerActionHibernate:
            String = "Hibernate";
            break;

        case PowerActionShutdown:
            String = "Shutdown";
            break;

        case PowerActionShutdownReset:
            String = "ShutdownReset";
            break;

        case PowerActionShutdownOff:
            String = "ShutdownOff";
            break;

        case PowerActionWarmEject:
            String = "WarmEject";
            break;

        default:
            String = "?";
            break;
    }

    return String;
}

VOID
NTAPI
PopAssertPolicyLockOwned(VOID)
{
    PAGED_CODE();
    ASSERT(PopPolicyLockThread == KeGetCurrentThread());
}

VOID
NTAPI
PopCompleteAction(
    _In_ PPOP_ACTION_TRIGGER ActionTrigger,
    _In_ NTSTATUS Status)
{
    DPRINT("PopCompleteAction: ActionTrigger %X, Status %X\n", ActionTrigger, Status);
  
    if (ActionTrigger->Flags & 0x20)
    {
        PPOP_TRIGGER_WAIT Wait;

        ActionTrigger->Flags &= ~0x20;

        Wait = ActionTrigger->Wait;
        Wait->Status = Status;

        KeSetEvent(&Wait->Event, IO_NO_INCREMENT, FALSE);
    }
}

VOID
NTAPI
PopVerifySystemPowerState(
    _In_ PSYSTEM_POWER_STATE pPowerState,
    _In_ ULONG SubstitutionPolicy)
{
    SYSTEM_POWER_STATE PowerState;
    BOOLEAN IsHibernate;

    PAGED_CODE();

    if (!pPowerState)
    {
        ASSERT(pPowerState);
        return;
    }

    PowerState = *pPowerState;

    DPRINT("PopVerifySystemPowerState: PowerState %X, SubstitutionPolicy %X\n", PowerState, SubstitutionPolicy);

    if (!PowerState || PowerState >= PowerSystemShutdown)
    {
        DPRINT1("PopVerifySystemPowerState: Invalid PowerState\n");
        ASSERT(FALSE); // PoDbgBreakPointEx();
        return;
    }

    if (PowerState == PowerSystemWorking)
        return;

    IsHibernate = 1;

    if (SubstitutionPolicy == 0 || SubstitutionPolicy == 1)
    {
        if (PowerState == PowerSystemHibernate)
        {
            if (PopCapabilities.SystemS4 && PopCapabilities.HiberFilePresent)
                goto Exit;

            PowerState = PowerSystemSleeping3;
        }

        if (PowerState == PowerSystemSleeping3)
        {
            if (PopCapabilities.SystemS3)
                goto Exit;

            PowerState = PowerSystemSleeping2;
        }

        if (PowerState == PowerSystemSleeping2)
        {
            if (PopCapabilities.SystemS2)
                goto Exit;

            PowerState = PowerSystemSleeping1;
        }

        if (PowerState == PowerSystemSleeping1)
        {
            if (PopCapabilities.SystemS1)
                goto Exit;

            PowerState = PowerSystemWorking;
        }

        if (PowerState != PowerSystemWorking || SubstitutionPolicy != 1)
            goto Exit;

        PowerState = PowerSystemSleeping1;

        IsHibernate = 0;
    }
    else if (SubstitutionPolicy != 2)
    {
        DPRINT1("PopVerifySystemPowerState: Invalid substitution policy\n");
        ASSERT(FALSE); // PoDbgBreakPointEx();
        goto Exit;
    }

    /* SubstitutionPolicy == 2 */

    if (PowerState == PowerSystemSleeping1)
    {
        if (PopCapabilities.SystemS1)
            goto Exit;

        PowerState = PowerSystemSleeping2;
    }

    if (PowerState == PowerSystemSleeping2)
    {
        if (PopCapabilities.SystemS2)
            goto Exit;

        PowerState = PowerSystemSleeping3;
    }

    if (PowerState == PowerSystemSleeping3)
    {
        if (PopCapabilities.SystemS3)
            goto Exit;

        PowerState = PowerSystemHibernate;

        if ((!IsHibernate || !PopCapabilities.SystemS4 || !PopCapabilities.HiberFilePresent))
            PowerState = PowerSystemWorking;

        goto Exit;
    }

    if (PowerState == PowerSystemHibernate)
    {
        if (!IsHibernate || !PopCapabilities.SystemS4 || !PopCapabilities.HiberFilePresent)
            PowerState = PowerSystemWorking;
    }

Exit:

    *pPowerState = PowerState;
    return;
}

VOID
NTAPI
PopFilterCapabilities(
    _In_ PSYSTEM_POWER_CAPABILITIES Capabilities,
    _Out_ PSYSTEM_POWER_CAPABILITIES OutCapabilities)
{
    DPRINT("PopFilterCapabilities: Capabilities %p, OutCapabilities %p\n", Capabilities, OutCapabilities);
    PAGED_CODE();

    RtlCopyMemory(OutCapabilities, Capabilities, sizeof(*OutCapabilities));

    DPRINT("PopFilterCapabilities: FIXME IoGetLegacyVetoList()\n");

    if (MmHighestPhysicalPage >= 0x00100000)
    {
        DPRINT1("PopFilterCapabilities: FIXME\n");
    }

    DPRINT("PopFilterCapabilities: FIXME PopFindLoadedModule(VGAPNP.SYS)\n");

    if (PopFailedHibernationAttempt)
    {
        OutCapabilities->SystemS4 = FALSE;
        DPRINT("PopFilterCapabilities: FIXME PopInsertLoggingEntry()\n");
    }
}

BOOLEAN
NTAPI
PopVerifyPowerActionPolicy(
    _In_ PPOWER_ACTION_POLICY ActionPolicy)
{
    SYSTEM_POWER_CAPABILITIES Capabilities;
    POWER_ACTION OldAction;
    ULONG Level;
    BOOLEAN IsNotHibernate;
    BOOLEAN Result = FALSE;

    DPRINT("PopVerifyPowerActionPolicy: ActionPolicy %X\n", ActionPolicy);
    PAGED_CODE();

    if (!ActionPolicy)
        return FALSE;

    if (ActionPolicy->Flags & ~(POWER_ACTION_QUERY_ALLOWED  |
                                POWER_ACTION_UI_ALLOWED     |
                                POWER_ACTION_OVERRIDE_APPS  |
                                POWER_ACTION_LIGHTEST_FIRST |
                                POWER_ACTION_LOCK_CONSOLE   |
                                POWER_ACTION_DISABLE_WAKES  |
                                POWER_ACTION_CRITICAL))
    {
        DPRINT1("PopVerifyPowerActionPolicy: Bad incoming Action\n");
        ASSERT(FALSE); // PoDbgBreakPointEx();
        return FALSE;
    }

    if (ActionPolicy->Flags & POWER_ACTION_CRITICAL)
    {
        ActionPolicy->Flags &= ~(POWER_ACTION_QUERY_ALLOWED | POWER_ACTION_UI_ALLOWED);
        ActionPolicy->Flags |= POWER_ACTION_OVERRIDE_APPS;
    }

    if (ActionPolicy->Action == PowerActionSleep ||
        ActionPolicy->Action == PowerActionHibernate)
    {
        DPRINT("PopVerifyPowerActionPolicy: FIXME IoGetLegacyVetoList()\n");
        ASSERT(FALSE); // PoDbgBreakPointEx();
    }

    PopFilterCapabilities(&PopCapabilities, &Capabilities);

    Level = 0;
    IsNotHibernate = FALSE;

    if (Capabilities.SystemS1)
        Level = 1;

    if (Capabilities.SystemS2)
        Level++;

    if (Capabilities.SystemS3)
        Level++;

    if (Capabilities.SystemS4 && Capabilities.HiberFilePresent)
        IsNotHibernate = TRUE;

    do
    {
        OldAction = ActionPolicy->Action;

        switch (ActionPolicy->Action)
        {
            case PowerActionReserved:
            {
                ActionPolicy->Action = PowerActionSleep;

                if (Level < 1)
                {
                    Result = 1;
                    ActionPolicy->Action = 0;
                }

                break;
            }
            case PowerActionSleep:
            {
                if (Level < 1)
                {
                    Result = 1;
                    ActionPolicy->Action = 0;
                }

                break;
            }
            case PowerActionHibernate:
            {
                if (IsNotHibernate)
                    break;

                ActionPolicy->Action = PowerActionSleep;

                if (Level < 1)
                {
                    Result = 1;
                    ActionPolicy->Action = 0;
                }

                break;
            }
            case PowerActionShutdownOff:
            {
                if (!Capabilities.SystemS5)
                    ActionPolicy->Action = PowerActionShutdown;

                break;
            }
            case PowerActionNone:
            case PowerActionShutdown:
            case PowerActionShutdownReset:
            case PowerActionWarmEject:
                break;

            default:
                ASSERT(FALSE); // PoDbgBreakPointEx();
                break;
        }
    }
    while (OldAction != ActionPolicy->Action);

    return Result;
}

LONG
NTAPI
PopCompareActions(
    _In_ POWER_ACTION Action1,
    _In_ POWER_ACTION Action2)
{
    if (Action1 == PowerActionWarmEject)
    {
        Action1 = PowerActionSleep;
    }
    else if (Action1 >= PowerActionSleep)
    {
        Action1++;
    }

    if (Action2 == PowerActionWarmEject)
    {
        Action2 = PowerActionSleep;
    }
    else if (Action2 >= PowerActionSleep)
    {
        Action2++;
    }

    return (Action1 - Action2);
}

VOID
NTAPI
PopPromoteActionFlag(
    _Out_ UCHAR* OutUpdates,
    _In_ UCHAR Updates,
    _In_ ULONG PolicyFlags,
    _In_ BOOLEAN Type,
    _In_ ULONG Flags)
{
    ULONG Mask;

    Mask = (!Type ? Flags : 0);
    PolicyFlags &= (Mask ^ Flags);

    if (~(PopAction.Flags & (Flags ^ Mask)) & PolicyFlags)
    {
        PopAction.Flags = ((PopAction.Flags | PolicyFlags) & ~Mask);
        *OutUpdates |= Updates;
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
PoCancelDeviceNotify(IN PVOID NotifyBlock)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
PoRegisterDeviceNotify(OUT PVOID Unknown0,
                       IN ULONG Unknown1,
                       IN ULONG Unknown2,
                       IN ULONG Unknown3,
                       IN PVOID Unknown4,
                       IN PVOID Unknown5)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoShutdownBugCheck(IN BOOLEAN LogError,
                   IN ULONG BugCheckCode,
                   IN ULONG_PTR BugCheckParameter1,
                   IN ULONG_PTR BugCheckParameter2,
                   IN ULONG_PTR BugCheckParameter3,
                   IN ULONG_PTR BugCheckParameter4)
{
    DPRINT1("PoShutdownBugCheck called\n");

    /* FIXME: Log error if requested */
    /* FIXME: Initiate a shutdown */

    /* Bugcheck the system */
    KeBugCheckEx(BugCheckCode,
                 BugCheckParameter1,
                 BugCheckParameter2,
                 BugCheckParameter3,
                 BugCheckParameter4);
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoSetHiberRange(IN PVOID HiberContext,
                IN ULONG Flags,
                IN OUT PVOID StartPage,
                IN ULONG Length,
                IN ULONG PageTag)
{
    UNIMPLEMENTED;
    return;
}

/*
 * @implemented
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
NTAPI
PoCallDriver(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ __drv_aliasesMem PIRP Irp)
{
    PIO_STACK_LOCATION NextStack;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    ASSERT(DeviceObject);
    ASSERT(Irp);

    NextStack = IoGetNextIrpStackLocation(Irp);
    ASSERT(NextStack->MajorFunction == IRP_MJ_POWER);

    /* Set DeviceObject for PopPresentIrp */
    NextStack->DeviceObject = DeviceObject;

    /* Only QUERY_POWER and SET_POWER use special handling */
    if (NextStack->MinorFunction != IRP_MN_SET_POWER &&
        NextStack->MinorFunction != IRP_MN_QUERY_POWER)
    {
        return IoCallDriver(DeviceObject, Irp);
    }

    /* Call the next driver, either directly or at PASSIVE_LEVEL */
    return PopPresentIrp(NextStack, Irp);
}

/*
 * @unimplemented
 */
PULONG
NTAPI
PoRegisterDeviceForIdleDetection(IN PDEVICE_OBJECT DeviceObject,
                                 IN ULONG ConservationIdleTime,
                                 IN ULONG PerformanceIdleTime,
                                 IN DEVICE_POWER_STATE State)
{
    UNIMPLEMENTED;
    return NULL;
}

/*
 * @unimplemented
 */
PVOID
NTAPI
PoRegisterSystemState(IN PVOID StateHandle,
                      IN EXECUTION_STATE Flags)
{
    UNIMPLEMENTED;
    return NULL;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
PoRequestPowerIrp(IN PDEVICE_OBJECT DeviceObject,
                  IN UCHAR MinorFunction,
                  IN POWER_STATE PowerState,
                  IN PREQUEST_POWER_COMPLETE CompletionFunction,
                  IN PVOID Context,
                  OUT PIRP *pIrp OPTIONAL)
{
    PDEVICE_OBJECT TopDeviceObject;
    PIO_STACK_LOCATION Stack;
    PIRP Irp;

    if (MinorFunction != IRP_MN_QUERY_POWER
        && MinorFunction != IRP_MN_SET_POWER
        && MinorFunction != IRP_MN_WAIT_WAKE)
        return STATUS_INVALID_PARAMETER_2;

    /* Always call the top of the device stack */
    TopDeviceObject = IoGetAttachedDeviceReference(DeviceObject);

    Irp = IoAllocateIrp(TopDeviceObject->StackSize + 2, FALSE);
    if (!Irp)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;

    IoSetNextIrpStackLocation(Irp);

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->Parameters.Others.Argument1 = DeviceObject;
    Stack->Parameters.Others.Argument2 = (PVOID)(ULONG_PTR)MinorFunction;
    Stack->Parameters.Others.Argument3 = (PVOID)(ULONG_PTR)PowerState.DeviceState;
    Stack->Parameters.Others.Argument4 = Context;
    Stack->DeviceObject = TopDeviceObject;
    IoSetNextIrpStackLocation(Irp);

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_POWER;
    Stack->MinorFunction = MinorFunction;
    if (MinorFunction == IRP_MN_WAIT_WAKE)
    {
        Stack->Parameters.WaitWake.PowerState = PowerState.SystemState;
    }
    else
    {
        Stack->Parameters.Power.Type = DevicePowerState;
        Stack->Parameters.Power.State = PowerState;
    }

    if (pIrp != NULL)
        *pIrp = Irp;

    IoSetCompletionRoutine(Irp, PopRequestPowerIrpCompletion, CompletionFunction, TRUE, TRUE, TRUE);
    PoCallDriver(TopDeviceObject, Irp);

    /* Always return STATUS_PENDING. The completion routine
     * will call CompletionFunction and complete the Irp.
     */
    return STATUS_PENDING;
}

/*
 * @unimplemented
 */
POWER_STATE
NTAPI
PoSetPowerState(IN PDEVICE_OBJECT DeviceObject,
                IN POWER_STATE_TYPE Type,
                IN POWER_STATE State)
{
    POWER_STATE ps;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    ps.SystemState = PowerSystemWorking;  // Fully on
    ps.DeviceState = PowerDeviceD0;       // Fully on

    return ps;
}

/* @unimplemented */
VOID
NTAPI
PoSetSystemState(IN EXECUTION_STATE Flags)
{
    if (Flags & ~(ES_SYSTEM_REQUIRED |
                  ES_DISPLAY_REQUIRED |
                  ES_USER_PRESENT))
    {
        ASSERT(FALSE);
        return;
    }

    //UNIMPLEMENTED;
    DPRINT("PoSetSystemState: Flags %X. UNIMPLEMENTED\n", Flags);
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoStartNextPowerIrp(IN PIRP Irp)
{
    UNIMPLEMENTED_ONCE;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoUnregisterSystemState(IN PVOID StateHandle)
{
    UNIMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
NtInitiatePowerAction(IN POWER_ACTION SystemAction,
                      IN SYSTEM_POWER_STATE MinSystemState,
                      IN ULONG Flags,
                      IN BOOLEAN Asynchronous)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
PopChangeCapability(_In_ PBOOLEAN StateSupport,
                    _In_ BOOLEAN IsSupport)
{
    DPRINT("PopChangeCapability: IsSupport %X\n", IsSupport);

    if (*StateSupport == IsSupport)
        return;

    DPRINT("PopChangeCapability: *StateSupport %X\n", *StateSupport);

    *StateSupport = IsSupport;

    DPRINT("PopChangeCapability: FIXME PopResetCurrentPolicies()\n");
    DPRINT("PopChangeCapability: FIXME PopSetNotificationWork()\n");
}

/* halfplemented */
NTSTATUS
NTAPI
NtPowerInformation(IN POWER_INFORMATION_LEVEL PowerInformationLevel,
                   IN PVOID InputBuffer  OPTIONAL,
                   IN ULONG InputBufferLength,
                   OUT PVOID OutputBuffer  OPTIONAL,
                   IN ULONG OutputBufferLength)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    SYSTEM_POWER_STATE State = PowerSystemUnspecified;
    PVOID LoggingInfo = NULL;
    BOOLEAN IsCheckForWork = FALSE;

    PAGED_CODE();

    DPRINT("NtPowerInformation: %X, %p, %X, %p, %X\n", PowerInformationLevel, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(InputBuffer, InputBufferLength, 1);
            ProbeForWrite(OutputBuffer, OutputBufferLength, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    PopAcquirePolicyLock();

    switch (PowerInformationLevel)
    {
        case SystemBatteryState:
        {
            PSYSTEM_BATTERY_STATE BatteryState = (PSYSTEM_BATTERY_STATE)OutputBuffer;

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(SYSTEM_BATTERY_STATE))
                return STATUS_BUFFER_TOO_SMALL;

            _SEH2_TRY
            {
                /* Just zero the struct (and thus set BatteryState->BatteryPresent = FALSE) */
                RtlZeroMemory(BatteryState, sizeof(SYSTEM_BATTERY_STATE));
                BatteryState->EstimatedTime = MAXULONG;
//                BatteryState->AcOnLine = TRUE;

                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }
        case SystemPowerCapabilities:
        {
            PSYSTEM_POWER_CAPABILITIES PowerCapabilities = (PSYSTEM_POWER_CAPABILITIES)OutputBuffer;

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(SYSTEM_POWER_CAPABILITIES))
                return STATUS_BUFFER_TOO_SMALL;

            _SEH2_TRY
            {
                RtlCopyMemory(PowerCapabilities,
                              &PopCapabilities,
                              sizeof(SYSTEM_POWER_CAPABILITIES));

                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }
        case ProcessorInformation:
        {
            PPROCESSOR_POWER_INFORMATION PowerInformation = (PPROCESSOR_POWER_INFORMATION)OutputBuffer;

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(PROCESSOR_POWER_INFORMATION))
                return STATUS_BUFFER_TOO_SMALL;

            _SEH2_TRY
            {
                PowerInformation->Number = 0;
                PowerInformation->MaxMhz = 1000;
                PowerInformation->CurrentMhz = 1000;
                PowerInformation->MhzLimit = 1000;
                PowerInformation->MaxIdleState = 0;
                PowerInformation->CurrentIdleState = 0;

                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }
        case SystemPowerLoggingEntry:
        {
            UNIMPLEMENTED;
            break;
        }
        case SystemPowerStateHandler:
        {
            PPOWER_STATE_HANDLER PowerStateHandler = (PPOWER_STATE_HANDLER)InputBuffer;
            POWER_STATE_HANDLER_TYPE StateType = PowerStateHandler->Type;
            PBOOLEAN Capabilities = NULL;

            Status = STATUS_SUCCESS;

            if (PreviousMode != KernelMode)
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }

            if (OutputBuffer)
            {
                DPRINT1("NtPowerInformation: STATUS_INVALID_PARAMETER. OutputBuffer %p\n", OutputBuffer);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (OutputBufferLength)
            {
                DPRINT1("NtPowerInformation: STATUS_INVALID_PARAMETER. OutputBufferLength %X\n", OutputBufferLength);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (!InputBuffer)
            {
                DPRINT1("NtPowerInformation: STATUS_INVALID_PARAMETER. InputBuffer is NULL\n");
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (InputBufferLength < sizeof(*PowerStateHandler))
            {
                DPRINT1("NtPowerInformation: STATUS_INVALID_PARAMETER. InputBufferLength %X\n", InputBufferLength);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (StateType >= PowerStateMaximum)
            {
                DPRINT1("NtPowerInformation: STATUS_INVALID_PARAMETER. StateType %X\n", StateType);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (PopPowerStateHandlers[StateType].Handler)
            {
                DPRINT1("NtPowerInformation: FIXME PopShutdownHandler()\n");
                ASSERT(FALSE);
            }

            PopPowerStateHandlers[StateType].Type = StateType;
            PopPowerStateHandlers[StateType].RtcWake = PowerStateHandler->RtcWake;
            PopPowerStateHandlers[StateType].Handler = PowerStateHandler->Handler;
            PopPowerStateHandlers[StateType].Context = PowerStateHandler->Context;

            PopPowerStateHandlers[StateType].Spare[0] = 0;
            PopPowerStateHandlers[StateType].Spare[1] = 0;
            PopPowerStateHandlers[StateType].Spare[2] = 0;

            switch (StateType)
            {
                case PowerStateSleeping1:
                    DPRINT1("NtPowerInformation: FIXME PowerStateSleeping1\n");
                    State = PowerSystemSleeping1;
                    break;

                case PowerStateSleeping2:
                    DPRINT1("NtPowerInformation: FIXME PowerStateSleeping2\n");
                    State = PowerSystemSleeping2;
                    break;

                case PowerStateSleeping3:
                    DPRINT1("NtPowerInformation: FIXME PowerStateSleeping3\n");
                    State = PowerSystemSleeping3;
                    break;

                case PowerStateSleeping4:
                    DPRINT1("NtPowerInformation: FIXME PowerStateSleeping4\n");
                    State = PowerSystemHibernate;
                    break;

                case PowerStateShutdownOff:
                    DPRINT1("NtPowerInformation: PowerStateShutdownOff\n");
                    Capabilities = &PopCapabilities.SystemS5;
                    break;

                default:
                    DPRINT1("NtPowerInformation: Unsupported Type %X\n", StateType);
                    break;
            }

            if (!PopPowerStateHandlers[StateType].RtcWake)
                State = PowerSystemUnspecified;

            if (State > PopCapabilities.RtcWake)
                PopCapabilities.RtcWake = State;

            if (Capabilities)
                PopChangeCapability(Capabilities, TRUE);

            break;
        }
        default:
            Status = STATUS_NOT_IMPLEMENTED;
            DPRINT1("NtPowerInformation: Level %X is UNIMPLEMENTED\n", PowerInformationLevel);
            break;
    }

    if (IsCheckForWork == TRUE)
    {
        PopReleasePolicyLock(FALSE);
        DPRINT1("NtPowerInformation: FIXME PopCheckForWork()\n");
        ASSERT(FALSE);
        goto Exit;
    }

    //PopReleasePolicyLock(InputBuffer != NULL);
    if (InputBuffer != NULL)
    {
        DPRINT("NtPowerInformation: FIXME PopReleasePolicyLock(TRUE)\n");PopReleasePolicyLock(FALSE);
        //ASSERT(FALSE);
        //PopReleasePolicyLock(TRUE);
    }
    else
    {
        PopReleasePolicyLock(FALSE);
    }

Exit:

    if (LoggingInfo != NULL)
    {
        DPRINT1("NtPowerInformation: FIXME free LoggingInfo\n");
        ASSERT(FALSE);//ExFreePoolWithTag(LoggingInfo, 0);
    }

    return Status;
}

NTSTATUS
NTAPI
NtGetDevicePowerState(IN HANDLE Device,
                      IN PDEVICE_POWER_STATE PowerState)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
NtIsSystemResumeAutomatic(VOID)
{
    UNIMPLEMENTED;
    return FALSE;
}

NTSTATUS
NTAPI
NtRequestWakeupLatency(IN LATENCY_TIME Latency)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetThreadExecutionState(IN EXECUTION_STATE esFlags,
                          OUT EXECUTION_STATE *PreviousFlags)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    EXECUTION_STATE PreviousState;
    PAGED_CODE();

    /* Validate flags */
    if (esFlags & ~(ES_CONTINUOUS | ES_USER_PRESENT))
    {
        /* Fail the request */
        return STATUS_INVALID_PARAMETER;
    }

    /* Check for user parameters */
    if (PreviousMode != KernelMode)
    {
        /* Protect the probes */
        _SEH2_TRY
        {
            /* Check if the pointer is valid */
            ProbeForWriteUlong(PreviousFlags);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* It isn't -- fail */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* Save the previous state, always masking in the continous flag */
    PreviousState = Thread->PowerState | ES_CONTINUOUS;

    /* Check if we need to update the power state */
    if (esFlags & ES_CONTINUOUS) Thread->PowerState = (UCHAR)esFlags;

    /* Protect the write back to user mode */
    _SEH2_TRY
    {
        /* Return the previous flags */
        *PreviousFlags = PreviousState;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        /* Something's wrong, fail */
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* All is good */
    return STATUS_SUCCESS;
}

VOID
NTAPI
PiLockDeviceActionQueue(VOID)
{
    KIRQL OldIrql;

    DPRINT("PiLockDeviceActionQueue()\n");

    PpDevNodeLockTree(1);
    KeAcquireSpinLock(&IopPnPSpinLock, &OldIrql);

    while (PipEnumerationInProgress)
    {
        KeReleaseSpinLock(&IopPnPSpinLock, OldIrql);
        PpDevNodeUnlockTree(1);

        DPRINT("PiLockDeviceActionQueue: call KeWaitForSingleObject()\n");
        KeWaitForSingleObject(&PiEnumerationLock, Executive, KernelMode, FALSE, NULL);
        DPRINT("PiLockDeviceActionQueue: end wait\n");

        PpDevNodeLockTree(1);
        KeAcquireSpinLock(&IopPnPSpinLock, &OldIrql);
    }

    KeClearEvent(&PiEnumerationLock);
    PipEnumerationInProgress = TRUE;

    KeReleaseSpinLock(&IopPnPSpinLock, OldIrql);

    DPRINT("PiLockDeviceActionQueue: Locked\n");
}

VOID
NTAPI
PiUnlockDeviceActionQueue(VOID)
{
    KIRQL OldIrql;
  
    DPRINT("PiUnlockDeviceActionQueue()\n");

    KeAcquireSpinLock(&IopPnPSpinLock, &OldIrql);

    if (IsListEmpty(&IopPnpEnumerationRequestList))
    {
        PipEnumerationInProgress = FALSE;
        KeSetEvent(&PiEnumerationLock, IO_NO_INCREMENT, FALSE);
    }
    else
    {
        DPRINT1("PiUnlockDeviceActionQueue: FIXME PipDeviceEnumerationWorkItem\n");
        ASSERT(FALSE); // PoDbgBreakPointEx();
    }

    KeReleaseSpinLock(&IopPnPSpinLock, OldIrql);
    PpDevNodeUnlockTree(1);

    DPRINT("PiUnlockDeviceActionQueue: Unlocked\n");
}

VOID
NTAPI
IopFreePoDeviceNotifyListHead(
    _In_ PLIST_ENTRY NotifyListHead)
{
    PPO_DEVICE_NOTIFY Notify;
    PDEVICE_NODE DeviceNode;

    //DPRINT("IopFreePoDeviceNotifyListHead: NotifyListHead %p\n", NotifyListHead);

    while (!IsListEmpty(NotifyListHead))
    {
        Notify = CONTAINING_RECORD(NotifyListHead->Flink, PO_DEVICE_NOTIFY, Link);

        NotifyListHead->Flink = NotifyListHead->Flink->Flink;
        NotifyListHead->Flink->Flink->Blink = NotifyListHead;

        DeviceNode = Notify->Node;
        DeviceNode->Notify = NULL;

        ObDereferenceObject(Notify->DeviceObject);
        ObDereferenceObject(Notify->TargetDevice);

        if (Notify->DeviceName)
            ExFreePool(Notify->DeviceName);

        if (Notify->DriverName)
            ExFreePool(Notify->DriverName);

        ExFreePool(Notify);
    }
}

VOID
NTAPI
IoFreePoDeviceNotifyList(
    _In_ PPO_DEVICE_NOTIFY_ORDER Order)
{
    ULONG ix;

    DPRINT("IoFreePoDeviceNotifyList: Order %p\n", Order);

    if (Order->DevNodeSequence)
    {
        Order->DevNodeSequence = 0;
        PiUnlockDeviceActionQueue();
    }

    for (ix = 0; ix < 8; ix++)
    {
        IopFreePoDeviceNotifyListHead(&Order->OrderLevel[ix].WaitSleep);
        IopFreePoDeviceNotifyListHead(&Order->OrderLevel[ix].ReadySleep);
        IopFreePoDeviceNotifyListHead(&Order->OrderLevel[ix].Pending);
        IopFreePoDeviceNotifyListHead(&Order->OrderLevel[ix].Complete);
        IopFreePoDeviceNotifyListHead(&Order->OrderLevel[ix].ReadyS0);
        IopFreePoDeviceNotifyListHead(&Order->OrderLevel[ix].WaitS0);
    }
}

NTSTATUS
NTAPI
NtSetSystemPowerState(IN POWER_ACTION SystemAction,
                      IN SYSTEM_POWER_STATE MinSystemState,
                      IN ULONG Flags)
{
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    POP_POWER_ACTION Action = {0};
    NTSTATUS Status;
    ULONG Dummy;

    /* Check for invalid parameter combinations */
    if ((MinSystemState >= PowerSystemMaximum) ||
        (MinSystemState <= PowerSystemUnspecified) ||
        (SystemAction > PowerActionWarmEject) ||
        (SystemAction < PowerActionReserved) ||
        (Flags & ~(POWER_ACTION_QUERY_ALLOWED  |
                   POWER_ACTION_UI_ALLOWED     |
                   POWER_ACTION_OVERRIDE_APPS  |
                   POWER_ACTION_LIGHTEST_FIRST |
                   POWER_ACTION_LOCK_CONSOLE   |
                   POWER_ACTION_DISABLE_WAKES  |
                   POWER_ACTION_CRITICAL)))
    {
        DPRINT1("NtSetSystemPowerState: Bad parameters!\n");
        DPRINT1("                       SystemAction: 0x%x\n", SystemAction);
        DPRINT1("                       MinSystemState: 0x%x\n", MinSystemState);
        DPRINT1("                       Flags: 0x%x\n", Flags);
        return STATUS_INVALID_PARAMETER;
    }

    /* Check for user caller */
    if (PreviousMode != KernelMode)
    {
        /* Check for shutdown permission */
        if (!SeSinglePrivilegeCheck(SeShutdownPrivilege, PreviousMode))
        {
            /* Not granted */
            DPRINT1("ERROR: Privilege not held for shutdown\n");
            return STATUS_PRIVILEGE_NOT_HELD;
        }

        /* Do it as a kernel-mode caller for consistency with system state */
        return ZwSetSystemPowerState(SystemAction, MinSystemState, Flags);
    }

    /* Read policy settings (partial shutdown vs. full shutdown) */
    if (SystemAction == PowerActionShutdown) PopReadShutdownPolicy();

    /* Disable lazy flushing of registry */
    DPRINT("Stopping lazy flush\n");
    CmSetLazyFlushState(FALSE);

    /* Setup the power action */
    Action.Action = SystemAction;
    Action.Flags = Flags;

    /* Notify callbacks */
    DPRINT("Notifying callbacks\n");
    ExNotifyCallback(PowerStateCallback, (PVOID)3, NULL);

    /* Swap in any worker thread stacks */
    DPRINT("Swapping worker threads\n");
    ExSwapinWorkerThreads(FALSE);

    /* Make our action global */
    PopAction = Action;

    /* Start power loop */
    Status = STATUS_CANCELLED;
    while (TRUE)
    {
        /* Break out if there's nothing to do */
        if (Action.Action == PowerActionNone) break;

        /* Check for first-pass or restart */
        if (Status == STATUS_CANCELLED)
        {
            /* Check for shutdown action */
            if ((PopAction.Action == PowerActionShutdown) ||
                (PopAction.Action == PowerActionShutdownReset) ||
                (PopAction.Action == PowerActionShutdownOff))
            {
                /* Set the action */
                PopAction.Shutdown = TRUE;
            }

            /* Now we are good to go */
            Status = STATUS_SUCCESS;
        }

        /* Check if we're still in an invalid status */
        if (!NT_SUCCESS(Status)) break;

#ifndef NEWCC
        /* Flush dirty cache pages */
        /* XXX: Is that still mandatory? As now we'll wait on lazy writer to complete? */
        CcRosFlushDirtyPages(-1, &Dummy, FALSE, FALSE); //HACK: We really should wait here!
#else
        Dummy = 0;
#endif

        /* Flush all volumes and the registry */
        DPRINT("Flushing volumes, cache flushed %lu pages\n", Dummy);
        PopFlushVolumes(PopAction.Shutdown);

        /* Set IRP for drivers */
        PopAction.IrpMinor = IRP_MN_SET_POWER;
        if (PopAction.Shutdown)
        {
            DPRINT("Queueing shutdown thread\n");
            /* Check if we are running in the system context */
            if (PsGetCurrentProcess() != PsInitialSystemProcess)
            {
                /* We're not, so use a worker thread for shutdown */
                ExInitializeWorkItem(&PopShutdownWorkItem,
                                     &PopGracefulShutdown,
                                     NULL);

                ExQueueWorkItem(&PopShutdownWorkItem, CriticalWorkQueue);

                /* Spend us -- when we wake up, the system is good to go down */
                KeSuspendThread(KeGetCurrentThread());
                Status = STATUS_SYSTEM_SHUTDOWN;
                goto Exit;

            }
            else
            {
                /* Do the shutdown inline */
                PopGracefulShutdown(NULL);
            }
        }

        /* You should not have made it this far */
        // ASSERTMSG("System is still up and running?!\n", FALSE);
        DPRINT1("System is still up and running, you may not have chosen a yet supported power option: %u\n", PopAction.Action);
        break;
    }

Exit:
    /* We're done, return */
    return Status;
}
