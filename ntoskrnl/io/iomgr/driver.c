/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/iomgr/driver.c
 * PURPOSE:         Driver Object Management
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Filip Navara (navaraf@reactos.org)
 *                  Herv� Poussineau (hpoussin@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "../pnpio.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ERESOURCE IopDriverLoadResource;

LIST_ENTRY DriverReinitListHead;
KSPIN_LOCK DriverReinitListLock;
PLIST_ENTRY DriverReinitTailEntry;

PLIST_ENTRY DriverBootReinitTailEntry;
LIST_ENTRY DriverBootReinitListHead;
KSPIN_LOCK DriverBootReinitListLock;

POBJECT_TYPE IoDriverObjectType = NULL;

extern BOOLEAN ExpInTextModeSetup;
extern BOOLEAN PnpSystemInit;
extern ULONG InitSafeBootMode;

USHORT IopGroupIndex;
PLIST_ENTRY IopGroupTable;

/* PRIVATE FUNCTIONS **********************************************************/

BOOLEAN
NTAPI
IopIsLegacyDriver(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    if (DriverObject->DriverExtension->AddDevice)
        return FALSE;

    if ((DriverObject->Flags & DRVO_LEGACY_DRIVER) != DRVO_LEGACY_DRIVER)
        return FALSE;

    return TRUE;
}

NTSTATUS
NTAPI
IopInvalidDeviceRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS
NTAPI
PpDriverObjectDereferenceComplete(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();
    DPRINT("PpDriverObjectDereferenceComplete: Driver '%wZ'\n", &DriverObject->DriverName);

    return PipRequestDeviceAction(IopRootDeviceNode->PhysicalDeviceObject,
                                  PipEnumClearProblem,
                                  0,
                                  CM_PROB_DRIVER_FAILED_PRIOR_UNLOAD, // RequestArgument 
                                  NULL,
                                  NULL);
}

VOID
NTAPI
IopDeleteDriver(
    _In_ PVOID ObjectBody)
{
    PDRIVER_OBJECT DriverObject = ObjectBody;
    PIO_CLIENT_EXTENSION DriverExtension, NextDriverExtension;

    PAGED_CODE();
    DPRINT1("Deleting driver object '%wZ'\n", &DriverObject->DriverName);

    /* There must be no device objects remaining at this point */
    ASSERT(!DriverObject->DeviceObject);

    /* Get the extension and loop them */
    DriverExtension = IoGetDrvObjExtension(DriverObject)->ClientDriverExtension;
    while (DriverExtension)
    {
        /* Get the next one */
        NextDriverExtension = DriverExtension->NextExtension;
        ExFreePoolWithTag(DriverExtension, TAG_DRIVER_EXTENSION);

        /* Move on */
        DriverExtension = NextDriverExtension;
    }

    /* Check if the driver image is still loaded */
    if (DriverObject->DriverSection)
    {
        /* Unload it */
        KeFlushQueuedDpcs();
        MmUnloadSystemImage(DriverObject->DriverSection);
        PpDriverObjectDereferenceComplete(DriverObject);
    }

    /* Check if it has a name */
    if (DriverObject->DriverName.Buffer)
    {
        /* Free it */
        ExFreePool(DriverObject->DriverName.Buffer);
    }

    /* Check if it has a service key name */
    if (DriverObject->DriverExtension->ServiceKeyName.Buffer)
    {
        /* Free it */
        ExFreePool(DriverObject->DriverExtension->ServiceKeyName.Buffer);
    }
}

/*
 * RETURNS
 *  TRUE if String2 contains String1 as a suffix.
 */
BOOLEAN
NTAPI
IopSuffixUnicodeString(
    IN PCUNICODE_STRING String1,
    IN PCUNICODE_STRING String2)
{
    PWCHAR pc1;
    PWCHAR pc2;
    ULONG Length;

    if (String2->Length < String1->Length)
        return FALSE;

    Length = String1->Length / 2;
    pc1 = String1->Buffer;
    pc2 = &String2->Buffer[String2->Length / sizeof(WCHAR) - Length];

    if (pc1 && pc2)
    {
        while (Length--)
        {
            if( *pc1++ != *pc2++ )
                return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}

/*
 * IopDisplayLoadingMessage
 *
 * Display 'Loading XXX...' message.
 */
VOID
FASTCALL
IopDisplayLoadingMessage(PUNICODE_STRING ServiceName)
{
    CHAR TextBuffer[256];
    UNICODE_STRING DotSys = RTL_CONSTANT_STRING(L".SYS");

    if (ExpInTextModeSetup) return;
    if (!KeLoaderBlock) return;
    RtlUpcaseUnicodeString(ServiceName, ServiceName, FALSE);
    snprintf(TextBuffer, sizeof(TextBuffer),
            "%s%sSystem32\\Drivers\\%wZ%s\r\n",
            KeLoaderBlock->ArcBootDeviceName,
            KeLoaderBlock->NtBootPathName,
            ServiceName,
            IopSuffixUnicodeString(&DotSys, ServiceName) ? "" : ".SYS");
    HalDisplayString(TextBuffer);
}

/*
 * IopNormalizeImagePath
 *
 * Normalize an image path to contain complete path.
 *
 * Parameters
 *    ImagePath
 *       The input path and on exit the result path. ImagePath.Buffer
 *       must be allocated by ExAllocatePool on input. Caller is responsible
 *       for freeing the buffer when it's no longer needed.
 *
 *    ServiceName
 *       Name of the service that ImagePath belongs to.
 *
 * Return Value
 *    Status
 *
 * Remarks
 *    The input image path isn't freed on error.
 */
NTSTATUS
FASTCALL
IopNormalizeImagePath(
    _Inout_ _When_(return>=0, _At_(ImagePath->Buffer, _Post_notnull_ __drv_allocatesMem(Mem)))
         PUNICODE_STRING ImagePath,
    _In_ PUNICODE_STRING ServiceName)
{
    UNICODE_STRING SystemRootString = RTL_CONSTANT_STRING(L"\\SystemRoot\\");
    UNICODE_STRING DriversPathString = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\drivers\\");
    UNICODE_STRING DotSysString = RTL_CONSTANT_STRING(L".sys");
    UNICODE_STRING InputImagePath;

    DPRINT("Normalizing image path '%wZ' for service '%wZ'\n", ImagePath, ServiceName);

    InputImagePath = *ImagePath;
    if (InputImagePath.Length == 0)
    {
        ImagePath->Length = 0;
        ImagePath->MaximumLength = DriversPathString.Length +
                                   ServiceName->Length +
                                   DotSysString.Length +
                                   sizeof(UNICODE_NULL);
        ImagePath->Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                                  ImagePath->MaximumLength,
                                                  TAG_IO);
        if (ImagePath->Buffer == NULL)
            return STATUS_NO_MEMORY;

        RtlCopyUnicodeString(ImagePath, &DriversPathString);
        RtlAppendUnicodeStringToString(ImagePath, ServiceName);
        RtlAppendUnicodeStringToString(ImagePath, &DotSysString);
    }
    else if (InputImagePath.Buffer[0] != L'\\')
    {
        ImagePath->Length = 0;
        ImagePath->MaximumLength = SystemRootString.Length +
                                   InputImagePath.Length +
                                   sizeof(UNICODE_NULL);
        ImagePath->Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                                  ImagePath->MaximumLength,
                                                  TAG_IO);
        if (ImagePath->Buffer == NULL)
            return STATUS_NO_MEMORY;

        RtlCopyUnicodeString(ImagePath, &SystemRootString);
        RtlAppendUnicodeStringToString(ImagePath, &InputImagePath);

        /* Free caller's string */
        ExFreePoolWithTag(InputImagePath.Buffer, TAG_RTLREGISTRY);
    }

    DPRINT("Normalized image path is '%wZ' for service '%wZ'\n", ImagePath, ServiceName);

    return STATUS_SUCCESS;
}

/* Load a module specified by registry settings for service.

   ServiceName
      Name of the service to load.
 */
NTSTATUS
FASTCALL
IopLoadServiceModule(
    _In_ PUNICODE_STRING ServiceName,
    _Out_ PLDR_DATA_TABLE_ENTRY * ModuleObject)
{
    UNICODE_STRING ServicesName = RTL_CONSTANT_STRING(IO_REG_KEY_SERVICES);
    UNICODE_STRING ServiceImagePath;
    RTL_QUERY_REGISTRY_TABLE QueryTable[3];
    HANDLE RootHandle;
    HANDLE ServiceHandle;
    PVOID BaseAddress;
    ULONG ServiceStart;
    NTSTATUS Status;

    DPRINT("IopLoadServiceModule: '%wZ', %p\n", ServiceName, ModuleObject);

    ASSERT(ExIsResourceAcquiredExclusiveLite(&IopDriverLoadResource));
    ASSERT(ServiceName->Length);

    if (ExpInTextModeSetup)
    {
        /* We have no registry, but luckily we know where all the drivers are */
        DPRINT1("IopLoadServiceModule: '%wZ', %p. ExpInTextModeSetup mode.\n", ServiceName, ModuleObject);

        /* ServiceStart < 4 is all that matters */
        ServiceStart = 0;

        /* IopNormalizeImagePath will do all of the work for us if we give it an empty string */
        RtlInitEmptyUnicodeString(&ServiceImagePath, NULL, 0);
    }
    else
    {
        /* Open CurrentControlSet */
        Status = IopOpenRegistryKeyEx(&RootHandle, NULL, &ServicesName, KEY_READ);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IopLoadServiceModule: Failed '%wZ' with %X\n", &ServicesName, Status);
            return Status;
        }

        /* Open service key */
        Status = IopOpenRegistryKeyEx(&ServiceHandle, RootHandle, ServiceName, KEY_READ);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IopLoadServiceModule: Failed '%wZ' with %X\n", ServiceName, Status);
            ZwClose(RootHandle);
            return Status;
        }

        /* Get information about the service. */
        RtlZeroMemory(QueryTable, sizeof(QueryTable));

        RtlInitUnicodeString(&ServiceImagePath, NULL);

        QueryTable[0].Name = L"Start";
        QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
        QueryTable[0].EntryContext = &ServiceStart;

        QueryTable[1].Name = L"ImagePath";
        QueryTable[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
        QueryTable[1].EntryContext = &ServiceImagePath;

        Status = RtlQueryRegistryValues(RTL_REGISTRY_HANDLE,
                                        (PWSTR)ServiceHandle,
                                        QueryTable,
                                        NULL,
                                        NULL);
        ZwClose(ServiceHandle);
        ZwClose(RootHandle);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IopLoadServiceModule: RtlQueryRegistryValues() failed %X\n", Status);
            return Status;
        }
    }

    /* Normalize the image path for all later processing. */
    Status = IopNormalizeImagePath(&ServiceImagePath, ServiceName);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopLoadServiceModule: IopNormalizeImagePath() failed %X\n", Status);
        return Status;
    }

    /* Case for disabled drivers */
    if (ServiceStart >= 4)
    {
        /* We can't load this */
        Status = STATUS_DRIVER_UNABLE_TO_LOAD;
    }
    else
    {
        DPRINT("IopLoadServiceModule: Loading '%wZ'\n", &ServiceImagePath);

        Status = MmLoadSystemImage(&ServiceImagePath, NULL, NULL, 0, (PVOID)ModuleObject, &BaseAddress);
        if (NT_SUCCESS(Status))
            IopDisplayLoadingMessage(ServiceName);
    }

    ExFreePool(ServiceImagePath.Buffer);

    /* Now check if the module was loaded successfully. */
    if (!NT_SUCCESS(Status))
    {
        DPRINT("IopLoadServiceModule: Loading failed %X\n", Status);
    }
    else
    {
        DPRINT("IopLoadServiceModule: Loading Ok %X\n", Status);
    }

    return Status;
}

VOID
NTAPI
MmFreeDriverInitialization(IN PLDR_DATA_TABLE_ENTRY LdrEntry);

/*
 * IopInitializeDriverModule
 *
 * Initialize a loaded driver.
 *
 * Parameters
 *    DeviceNode
 *       Pointer to device node.
 *
 *    ModuleObject
 *       Module object representing the driver. It can be retrieve by
 *       IopLoadServiceModule.
 *
 *    ServiceName
 *       Name of the service (as in registry).
 *
 *    FileSystemDriver
 *       Set to TRUE for file system drivers.
 *
 *    DriverObject
 *       On successful return this contains the driver object representing
 *       the loaded driver.
 */
NTSTATUS
FASTCALL
IopInitializeDriverModule(
    IN PDEVICE_NODE DeviceNode,
    IN PLDR_DATA_TABLE_ENTRY ModuleObject,
    IN PUNICODE_STRING ServiceName,
    IN BOOLEAN FileSystemDriver,
    OUT PDRIVER_OBJECT *DriverObject)
{
    static const WCHAR ServicesKeyName[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
    UNICODE_STRING DriverName;
    UNICODE_STRING RegistryKey;
    PDRIVER_INITIALIZE DriverEntry;
    PDRIVER_OBJECT Driver;
    NTSTATUS Status;

    DriverEntry = ModuleObject->EntryPoint;

    if (ServiceName != NULL && ServiceName->Length != 0)
    {
        RegistryKey.Length = 0;
        RegistryKey.MaximumLength = sizeof(ServicesKeyName) + ServiceName->Length;
        RegistryKey.Buffer = ExAllocatePoolWithTag(PagedPool,
                                                   RegistryKey.MaximumLength,
                                                   TAG_IO);
        if (RegistryKey.Buffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlAppendUnicodeToString(&RegistryKey, ServicesKeyName);
        RtlAppendUnicodeStringToString(&RegistryKey, ServiceName);
    }
    else
    {
        RtlInitEmptyUnicodeString(&RegistryKey, NULL, 0);
    }

    /* Create ModuleName string */
    if (ServiceName && ServiceName->Length > 0)
    {
        DriverName.Length = 0;
        DriverName.MaximumLength = sizeof(FILESYSTEM_ROOT_NAME) + ServiceName->Length;
        DriverName.Buffer = ExAllocatePoolWithTag(PagedPool,
                                                  DriverName.MaximumLength,
                                                  TAG_IO);
        if (DriverName.Buffer == NULL)
        {
            RtlFreeUnicodeString(&RegistryKey);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (FileSystemDriver != FALSE)
            RtlAppendUnicodeToString(&DriverName, FILESYSTEM_ROOT_NAME);
        else
            RtlAppendUnicodeToString(&DriverName, DRIVER_ROOT_NAME);
        RtlAppendUnicodeStringToString(&DriverName, ServiceName);

        DPRINT("Driver name: '%wZ'\n", &DriverName);
    }
    else
    {
        RtlInitEmptyUnicodeString(&DriverName, NULL, 0);
    }

    Status = IopCreateDriver(DriverName.Length > 0 ? &DriverName : NULL,
                             DriverEntry,
                             &RegistryKey,
                             ServiceName,
                             ModuleObject,
                             &Driver);
    RtlFreeUnicodeString(&RegistryKey);
    RtlFreeUnicodeString(&DriverName);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("IopCreateDriver() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    *DriverObject = Driver;

    MmFreeDriverInitialization((PLDR_DATA_TABLE_ENTRY)Driver->DriverSection);

    /* Set the driver as initialized */
    IopReadyDeviceObjects(Driver);

    if (PnpSystemInit) IopReinitializeDrivers();

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MiResolveImageReferences(IN PVOID ImageBase,
                         IN PUNICODE_STRING ImageFileDirectory,
                         IN PUNICODE_STRING NamePrefix OPTIONAL,
                         OUT PCHAR *MissingApi,
                         OUT PWCHAR *MissingDriver,
                         OUT PLOAD_IMPORTS *LoadImports);

//
// Used for images already loaded (boot drivers)
//
INIT_FUNCTION
NTSTATUS
NTAPI
LdrProcessDriverModule(PLDR_DATA_TABLE_ENTRY LdrEntry,
                       PUNICODE_STRING FileName,
                       PLDR_DATA_TABLE_ENTRY *ModuleObject)
{
    NTSTATUS Status;
    UNICODE_STRING BaseName, BaseDirectory;
    PLOAD_IMPORTS LoadedImports = (PVOID)-2;
    PCHAR MissingApiName, Buffer;
    PWCHAR MissingDriverName;
    PVOID DriverBase = LdrEntry->DllBase;

    /* Allocate a buffer we'll use for names */
    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   MAXIMUM_FILENAME_LENGTH,
                                   TAG_LDR_WSTR);
    if (!Buffer)
    {
        /* Fail */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Check for a separator */
    if (FileName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        PWCHAR p;
        ULONG BaseLength;

        /* Loop the path until we get to the base name */
        p = &FileName->Buffer[FileName->Length / sizeof(WCHAR)];
        while (*(p - 1) != OBJ_NAME_PATH_SEPARATOR) p--;

        /* Get the length */
        BaseLength = (ULONG)(&FileName->Buffer[FileName->Length / sizeof(WCHAR)] - p);
        BaseLength *= sizeof(WCHAR);

        /* Setup the string */
        BaseName.Length = (USHORT)BaseLength;
        BaseName.Buffer = p;
    }
    else
    {
        /* Otherwise, we already have a base name */
        BaseName.Length = FileName->Length;
        BaseName.Buffer = FileName->Buffer;
    }

    /* Setup the maximum length */
    BaseName.MaximumLength = BaseName.Length;

    /* Now compute the base directory */
    BaseDirectory = *FileName;
    BaseDirectory.Length -= BaseName.Length;
    BaseDirectory.MaximumLength = BaseDirectory.Length;

    /* Resolve imports */
    MissingApiName = Buffer;
    Status = MiResolveImageReferences(DriverBase,
                                      &BaseDirectory,
                                      NULL,
                                      &MissingApiName,
                                      &MissingDriverName,
                                      &LoadedImports);

    /* Free the temporary buffer */
    ExFreePoolWithTag(Buffer, TAG_LDR_WSTR);

    /* Check the result of the imports resolution */
    if (!NT_SUCCESS(Status)) return Status;

    /* Return */
    *ModuleObject = LdrEntry;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopCheckUnloadDriver(
    _In_ PDRIVER_OBJECT Object,
    _Out_ PBOOLEAN OutIsSafeToUnload)
{
    KIRQL OldIrql;
    PDEVICE_OBJECT DeviceObject;

    OldIrql = KeAcquireQueuedSpinLock(LockQueueIoDatabaseLock);

    DeviceObject = Object->DeviceObject;

    if ((!DeviceObject && (Object->Flags & DRVO_UNLOAD_INVOKED)) ||
        (!(Object->Flags & DRVO_FILESYSTEM_DRIVER) &&
         DeviceObject &&
         ((IoGetDevObjExtension(DeviceObject))->ExtensionFlags & DOE_UNLOAD_PENDING)))
    {
          KeReleaseQueuedSpinLock(LockQueueIoDatabaseLock, OldIrql);
          ObDereferenceObject(Object);
          return STATUS_SUCCESS;
    }

    *OutIsSafeToUnload = TRUE;

    while (DeviceObject)
    {
        (IoGetDevObjExtension(DeviceObject))->ExtensionFlags |= DOE_UNLOAD_PENDING;

        if (DeviceObject->ReferenceCount || DeviceObject->AttachedDevice)
            *OutIsSafeToUnload = FALSE;

        DeviceObject = DeviceObject->NextDevice;
    }

    if ((Object->Flags & DRVO_FILESYSTEM_DRIVER) && Object->DeviceObject)
        *OutIsSafeToUnload = FALSE;

    if (*OutIsSafeToUnload)
        Object->Flags |= DRVO_UNLOAD_INVOKED;

    KeReleaseQueuedSpinLock(LockQueueIoDatabaseLock, OldIrql);

    return STATUS_UNSUCCESSFUL;
}

/* Unloads a device driver.

   DriverServiceName
      Name of the service to unload (registry key).
   UnloadPnpDrivers
      Whether to unload Plug & Plug or only legacy drivers.
      If this parameter is set to FALSE, the routine will unload only legacy drivers.
*/
NTSTATUS
NTAPI
IopUnloadDriver(
    _In_ PUNICODE_STRING DriverServiceName,
    _In_ BOOLEAN IsUnloadPnpManagers)
{
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode(); 
    LOAD_UNLOAD_PARAMS LoadUnloadDriverContext;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Destination;
    UNICODE_STRING DriverName;
    PDRIVER_OBJECT DriverObject;
    HANDLE Handle;
    HANDLE KeyHandle;
    PVOID Buffer = NULL;
    BOOLEAN IsSafeToUnload;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    DPRINT("IopUnloadDriver: Unload '%wZ'\n", DriverServiceName);

    if (PreviousMode != KernelMode && !IsUnloadPnpManagers)
    {
        if (!SeSinglePrivilegeCheck(SeLoadDriverPrivilege, PreviousMode))
        {
            ASSERT(FALSE); // IoDbgBreakPointEx();
            return STATUS_PRIVILEGE_NOT_HELD;
        }

        _SEH2_TRY
        {
            if (((ULONG_PTR)DriverServiceName >= MmUserProbeAddress))
                *(PUNICODE_STRING)&DriverName = *((PUNICODE_STRING)MmUserProbeAddress);
            else
                *(PUNICODE_STRING)&DriverName = *(DriverServiceName);

            if (!DriverName.Length)
                return STATUS_INVALID_PARAMETER;

            ProbeForRead(DriverName.Buffer, DriverName.Length, sizeof(WCHAR));

            Buffer = ExAllocatePoolWithQuotaTag(PagedPool, DriverName.Length, '  oI');

            RtlCopyMemory(Buffer, DriverName.Buffer, DriverName.Length);
            DriverName.Buffer = Buffer;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            if (Buffer)
                ExFreePoolWithTag(Buffer, '  oI');

            return _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        Status = ZwUnloadDriver(&DriverName);

        ExFreePoolWithTag(Buffer, '  oI');

        return Status;
    }

    Status = IopOpenRegistryKey(&KeyHandle, NULL, DriverServiceName, KEY_READ, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopUnloadDriver: Status %X\n", Status);
        return Status;
    }

    Status = IopGetDriverNameFromKeyNode(KeyHandle, &Destination);
    ObCloseHandle(KeyHandle, KernelMode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopUnloadDriver: Status %X\n", Status);
        return Status;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &Destination,
                               (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
                               NULL,
                               NULL);

    Status = ObOpenObjectByName(&ObjectAttributes,
                                IoDriverObjectType,
                                KernelMode,
                                NULL,
                                FILE_READ_DATA,
                                NULL,
                                &Handle);

    ExFreePool(Destination.Buffer);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopUnloadDriver: Status %X\n", Status);
        return Status;
    }

    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       IoDriverObjectType,
                                       KernelMode,
                                       (PVOID *)&DriverObject,
                                       NULL);
    ObCloseHandle(Handle, KernelMode);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopUnloadDriver: Status %X\n", Status);
        return Status;
    }

    if (!DriverObject->DriverUnload || !DriverObject->DriverSection)
    {
        DPRINT1("IopUnloadDriver: STATUS_INVALID_DEVICE_REQUEST\n");
        ObDereferenceObject(DriverObject);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (!IsUnloadPnpManagers && !IopIsLegacyDriver(DriverObject))
    {
        DPRINT1("IopUnloadDriver: STATUS_INVALID_DEVICE_REQUEST\n");
        ObDereferenceObject(DriverObject);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = IopCheckUnloadDriver(DriverObject, &IsSafeToUnload);
    if (NT_SUCCESS(Status))
        return Status;

    DPRINT1("IopUnloadDriver: Status %X\n", Status);

    if (!IsSafeToUnload)
    {
        DPRINT1("IopUnloadDriver: STATUS_INVALID_DEVICE_REQUEST\n");
        ObDereferenceObject(DriverObject);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (PsGetCurrentProcess() == PsInitialSystemProcess)
    {
        DriverObject->DriverUnload(DriverObject);
        goto Exit;
    }

    LoadUnloadDriverContext.DriverObject = DriverObject;

    KeInitializeEvent(&LoadUnloadDriverContext.Event, NotificationEvent, FALSE);

    ExInitializeWorkItem(&LoadUnloadDriverContext.WorkItem, IopLoadUnloadDriver, &LoadUnloadDriverContext);
    ExQueueWorkItem(&LoadUnloadDriverContext.WorkItem, DelayedWorkQueue);

    KeWaitForSingleObject(&LoadUnloadDriverContext.Event, Executive, KernelMode, FALSE, NULL);

Exit:

    ObMakeTemporaryObject(DriverObject);
    ObDereferenceObject(DriverObject);
    ObDereferenceObject(DriverObject);

    DPRINT("IopUnloadDriver: return %X\n", Status);
    return Status;
}

VOID
NTAPI
IopReinitializeDrivers(VOID)
{
    PDRIVER_REINIT_ITEM ReinitItem;
    PLIST_ENTRY Entry;

    /* Get the first entry and start looping */
    Entry = ExInterlockedRemoveHeadList(&DriverReinitListHead,
                                        &DriverReinitListLock);
    while (Entry)
    {
        /* Get the item */
        ReinitItem = CONTAINING_RECORD(Entry, DRIVER_REINIT_ITEM, ItemEntry);

        /* Increment reinitialization counter */
        ReinitItem->DriverObject->DriverExtension->Count++;

        /* Remove the device object flag */
        ReinitItem->DriverObject->Flags &= ~DRVO_REINIT_REGISTERED;

        /* Call the routine */
        ReinitItem->ReinitRoutine(ReinitItem->DriverObject,
                                  ReinitItem->Context,
                                  ReinitItem->DriverObject->
                                  DriverExtension->Count);

        /* Free the entry */
        ExFreePool(Entry);

        /* Move to the next one */
        Entry = ExInterlockedRemoveHeadList(&DriverReinitListHead,
                                            &DriverReinitListLock);
    }
}

VOID
NTAPI
IopReinitializeBootDrivers(VOID)
{
    PDRIVER_REINIT_ITEM ReinitItem;
    PLIST_ENTRY Entry;

    /* Get the first entry and start looping */
    Entry = ExInterlockedRemoveHeadList(&DriverBootReinitListHead,
                                        &DriverBootReinitListLock);
    while (Entry)
    {
        /* Get the item */
        ReinitItem = CONTAINING_RECORD(Entry, DRIVER_REINIT_ITEM, ItemEntry);

        /* Increment reinitialization counter */
        ReinitItem->DriverObject->DriverExtension->Count++;

        /* Remove the device object flag */
        ReinitItem->DriverObject->Flags &= ~DRVO_BOOTREINIT_REGISTERED;

        /* Call the routine */
        ReinitItem->ReinitRoutine(ReinitItem->DriverObject,
                                  ReinitItem->Context,
                                  ReinitItem->DriverObject->
                                  DriverExtension->Count);

        /* Free the entry */
        ExFreePool(Entry);

        /* Move to the next one */
        Entry = ExInterlockedRemoveHeadList(&DriverBootReinitListHead,
                                            &DriverBootReinitListLock);
    }
}

NTSTATUS
NTAPI
IopCreateDriver(IN PUNICODE_STRING DriverName OPTIONAL,
                IN PDRIVER_INITIALIZE InitializationFunction,
                IN PUNICODE_STRING RegistryPath OPTIONAL,
                IN PCUNICODE_STRING ServiceName,
                IN PLDR_DATA_TABLE_ENTRY ModuleObject OPTIONAL,
                OUT PDRIVER_OBJECT *pDriverObject)
{
    UNICODE_STRING HardwareKeyName = RTL_CONSTANT_STRING(IO_REG_KEY_DESCRIPTIONSYSTEM);
    WCHAR NameBuffer[100];
    USHORT NameLength;
    UNICODE_STRING LocalDriverName;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG ObjectSize;
    PDRIVER_OBJECT DriverObject;
    UNICODE_STRING ServiceKeyName;
    HANDLE hDriver;
    ULONG i, RetryCount = 0;

try_again:
    /* First, create a unique name for the driver if we don't have one */
    if (!DriverName)
    {
        /* Create a random name and set up the string */
        NameLength = (USHORT)swprintf(NameBuffer,
                                      DRIVER_ROOT_NAME L"%08u",
                                      KeTickCount.LowPart);
        LocalDriverName.Length = NameLength * sizeof(WCHAR);
        LocalDriverName.MaximumLength = LocalDriverName.Length + sizeof(UNICODE_NULL);
        LocalDriverName.Buffer = NameBuffer;
    }
    else
    {
        /* So we can avoid another code path, use a local var */
        LocalDriverName = *DriverName;
    }

    /* Initialize the Attributes */
    ObjectSize = sizeof(DRIVER_OBJECT) + sizeof(EXTENDED_DRIVER_EXTENSION);
    InitializeObjectAttributes(&ObjectAttributes,
                               &LocalDriverName,
                               OBJ_PERMANENT | OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    /* Create the Object */
    Status = ObCreateObject(KernelMode,
                            IoDriverObjectType,
                            &ObjectAttributes,
                            KernelMode,
                            NULL,
                            ObjectSize,
                            0,
                            0,
                            (PVOID*)&DriverObject);
    if (!NT_SUCCESS(Status)) return Status;

    DPRINT("IopCreateDriver(): created DO %p\n", DriverObject);

    /* Set up the Object */
    RtlZeroMemory(DriverObject, ObjectSize);
    DriverObject->Type = IO_TYPE_DRIVER;
    DriverObject->Size = sizeof(DRIVER_OBJECT);
    DriverObject->Flags = DRVO_BUILTIN_DRIVER;
    DriverObject->DriverExtension = (PDRIVER_EXTENSION)(DriverObject + 1);
    DriverObject->DriverExtension->DriverObject = DriverObject;
    DriverObject->DriverInit = InitializationFunction;
    DriverObject->DriverSection = ModuleObject;
    /* Loop all Major Functions */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        /* Invalidate each function */
        DriverObject->MajorFunction[i] = IopInvalidDeviceRequest;
    }

    /* Set up the service key name buffer */
    ServiceKeyName.MaximumLength = ServiceName->Length + sizeof(UNICODE_NULL);
    ServiceKeyName.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                                  ServiceKeyName.MaximumLength,
                                                  TAG_IO);
    if (!ServiceKeyName.Buffer)
    {
        /* Fail */
        ObMakeTemporaryObject(DriverObject);
        ObDereferenceObject(DriverObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Copy the name and set it in the driver extension */
    RtlCopyUnicodeString(&ServiceKeyName,
                         ServiceName);
    DriverObject->DriverExtension->ServiceKeyName = ServiceKeyName;

    /* Make a copy of the driver name to store in the driver object */
    DriverObject->DriverName.MaximumLength = LocalDriverName.Length;
    DriverObject->DriverName.Buffer = ExAllocatePoolWithTag(PagedPool,
                                                            DriverObject->DriverName.MaximumLength,
                                                            TAG_IO);
    if (!DriverObject->DriverName.Buffer)
    {
        /* Fail */
        ObMakeTemporaryObject(DriverObject);
        ObDereferenceObject(DriverObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyUnicodeString(&DriverObject->DriverName,
                         &LocalDriverName);

    /* Add the Object and get its handle */
    Status = ObInsertObject(DriverObject,
                            NULL,
                            FILE_READ_DATA,
                            0,
                            NULL,
                            &hDriver);

    /* Eliminate small possibility when this function is called more than
       once in a row, and KeTickCount doesn't get enough time to change */
    if (!DriverName && (Status == STATUS_OBJECT_NAME_COLLISION) && (RetryCount < 100))
    {
        RetryCount++;
        goto try_again;
    }

    if (!NT_SUCCESS(Status)) return Status;

    /* Now reference it */
    Status = ObReferenceObjectByHandle(hDriver,
                                       0,
                                       IoDriverObjectType,
                                       KernelMode,
                                       (PVOID*)&DriverObject,
                                       NULL);

    /* Close the extra handle */
    ZwClose(hDriver);

    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        ObMakeTemporaryObject(DriverObject);
        ObDereferenceObject(DriverObject);
        return Status;
    }

    DriverObject->HardwareDatabase = &HardwareKeyName;
    DriverObject->DriverStart = ModuleObject ? ModuleObject->DllBase : 0;
    DriverObject->DriverSize = ModuleObject ? ModuleObject->SizeOfImage : 0;

    /* Finally, call its init function */
    DPRINT("RegistryKey: %wZ\n", RegistryPath);
    DPRINT("Calling driver entrypoint at %p\n", InitializationFunction);
    Status = (*InitializationFunction)(DriverObject, RegistryPath);
    if (!NT_SUCCESS(Status))
    {
        /* If it didn't work, then kill the object */
        DPRINT1("'%wZ' initialization failed, status (0x%08lx)\n", DriverName, Status);
        DriverObject->DriverSection = NULL;
        ObMakeTemporaryObject(DriverObject);
        ObDereferenceObject(DriverObject);
        return Status;
    }
    else
    {
        /* Returns to caller the object */
        *pDriverObject = DriverObject;
    }

    /* We're going to say if we don't have any DOs from DriverEntry, then we're not legacy.
     * Other parts of the I/O manager depend on this behavior */

    /* Loop all Major Functions */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        /*
         * Make sure the driver didn't set any dispatch entry point to NULL!
         * Doing so is illegal; drivers shouldn't touch entry points they
         * do not implement.
         */

        /* Check if it did so anyway */
        if (!DriverObject->MajorFunction[i])
        {
            /* Print a warning in the debug log */
            DPRINT1("Driver <%wZ> set DriverObject->MajorFunction[%lu] to NULL!\n",
                    &DriverObject->DriverName, i);

            /* Fix it up */
            DriverObject->MajorFunction[i] = IopInvalidDeviceRequest;
        }
    }

    /* Return the Status */
    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
IoCreateDriver(IN PUNICODE_STRING DriverName OPTIONAL,
               IN PDRIVER_INITIALIZE InitializationFunction)
{
    PDRIVER_OBJECT DriverObject;
    return IopCreateDriver(DriverName, InitializationFunction, NULL, DriverName, NULL, &DriverObject);
}

/*
 * @implemented
 */
VOID
NTAPI
IoDeleteDriver(IN PDRIVER_OBJECT DriverObject)
{
    /* Simply dereference the Object */
    ObDereferenceObject(DriverObject);
}

/*
 * @implemented
 */
VOID
NTAPI
IoRegisterBootDriverReinitialization(IN PDRIVER_OBJECT DriverObject,
                                     IN PDRIVER_REINITIALIZE ReinitRoutine,
                                     IN PVOID Context)
{
    PDRIVER_REINIT_ITEM ReinitItem;

    /* Allocate the entry */
    ReinitItem = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(DRIVER_REINIT_ITEM),
                                       TAG_REINIT);
    if (!ReinitItem) return;

    /* Fill it out */
    ReinitItem->DriverObject = DriverObject;
    ReinitItem->ReinitRoutine = ReinitRoutine;
    ReinitItem->Context = Context;

    /* Set the Driver Object flag and insert the entry into the list */
    DriverObject->Flags |= DRVO_BOOTREINIT_REGISTERED;
    ExInterlockedInsertTailList(&DriverBootReinitListHead,
                                &ReinitItem->ItemEntry,
                                &DriverBootReinitListLock);
}

/*
 * @implemented
 */
VOID
NTAPI
IoRegisterDriverReinitialization(IN PDRIVER_OBJECT DriverObject,
                                 IN PDRIVER_REINITIALIZE ReinitRoutine,
                                 IN PVOID Context)
{
    PDRIVER_REINIT_ITEM ReinitItem;

    /* Allocate the entry */
    ReinitItem = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(DRIVER_REINIT_ITEM),
                                       TAG_REINIT);
    if (!ReinitItem) return;

    /* Fill it out */
    ReinitItem->DriverObject = DriverObject;
    ReinitItem->ReinitRoutine = ReinitRoutine;
    ReinitItem->Context = Context;

    /* Set the Driver Object flag and insert the entry into the list */
    DriverObject->Flags |= DRVO_REINIT_REGISTERED;
    ExInterlockedInsertTailList(&DriverReinitListHead,
                                &ReinitItem->ItemEntry,
                                &DriverReinitListLock);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
IoAllocateDriverObjectExtension(IN PDRIVER_OBJECT DriverObject,
                                IN PVOID ClientIdentificationAddress,
                                IN ULONG DriverObjectExtensionSize,
                                OUT PVOID *DriverObjectExtension)
{
    KIRQL OldIrql;
    PIO_CLIENT_EXTENSION DriverExtensions, NewDriverExtension;
    BOOLEAN Inserted = FALSE;

    /* Assume failure */
    *DriverObjectExtension = NULL;

    /* Allocate the extension */
    NewDriverExtension = ExAllocatePoolWithTag(NonPagedPool,
                                               sizeof(IO_CLIENT_EXTENSION) +
                                               DriverObjectExtensionSize,
                                               TAG_DRIVER_EXTENSION);
    if (!NewDriverExtension) return STATUS_INSUFFICIENT_RESOURCES;

    /* Clear the extension for teh caller */
    RtlZeroMemory(NewDriverExtension,
                  sizeof(IO_CLIENT_EXTENSION) + DriverObjectExtensionSize);

    /* Acqure lock */
    OldIrql = KeRaiseIrqlToDpcLevel();

    /* Fill out the extension */
    NewDriverExtension->ClientIdentificationAddress = ClientIdentificationAddress;

    /* Loop the current extensions */
    DriverExtensions = IoGetDrvObjExtension(DriverObject)->
                       ClientDriverExtension;
    while (DriverExtensions)
    {
        /* Check if the identifier matches */
        if (DriverExtensions->ClientIdentificationAddress ==
            ClientIdentificationAddress)
        {
            /* We have a collision, break out */
            break;
        }

        /* Go to the next one */
        DriverExtensions = DriverExtensions->NextExtension;
    }

    /* Check if we didn't collide */
    if (!DriverExtensions)
    {
        /* Link this one in */
        NewDriverExtension->NextExtension =
            IoGetDrvObjExtension(DriverObject)->ClientDriverExtension;
        IoGetDrvObjExtension(DriverObject)->ClientDriverExtension =
            NewDriverExtension;
        Inserted = TRUE;
    }

    /* Release the lock */
    KeLowerIrql(OldIrql);

    /* Check if insertion failed */
    if (!Inserted)
    {
        /* Free the entry and fail */
        ExFreePoolWithTag(NewDriverExtension, TAG_DRIVER_EXTENSION);
        return STATUS_OBJECT_NAME_COLLISION;
    }

    /* Otherwise, return the pointer */
    *DriverObjectExtension = NewDriverExtension + 1;
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
PVOID
NTAPI
IoGetDriverObjectExtension(IN PDRIVER_OBJECT DriverObject,
                           IN PVOID ClientIdentificationAddress)
{
    KIRQL OldIrql;
    PIO_CLIENT_EXTENSION DriverExtensions;

    /* Acquire lock */
    OldIrql = KeRaiseIrqlToDpcLevel();

    /* Loop the list until we find the right one */
    DriverExtensions = IoGetDrvObjExtension(DriverObject)->ClientDriverExtension;
    while (DriverExtensions)
    {
        /* Check for a match */
        if (DriverExtensions->ClientIdentificationAddress ==
            ClientIdentificationAddress)
        {
            /* Break out */
            break;
        }

        /* Keep looping */
        DriverExtensions = DriverExtensions->NextExtension;
    }

    /* Release lock */
    KeLowerIrql(OldIrql);

    /* Return nothing or the extension */
    if (!DriverExtensions) return NULL;
    return DriverExtensions + 1;
}

NTSTATUS
NTAPI
IopPnpDriverStarted(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ HANDLE ServiceHandle,
    _In_ PUNICODE_STRING DriverPath)
{
    DPRINT("IopPnpDriverStarted: DriverObject %X, DeviceObject %X\n", DriverObject, DriverObject->DeviceObject);

    if (DriverObject->DeviceObject)
        goto Exit;

    if (DriverPath->Buffer == NULL)
        goto Exit;

    DPRINT("IopPnpDriverStarted: DriverPath %wZ\n", DriverPath);

    if (IopIsAnyDeviceInstanceEnabled(DriverPath, NULL, FALSE))
        goto Exit;

    DPRINT("IopPnpDriverStarted: DriverObject->Flags %X\n", DriverObject->Flags);

    if (!(DriverObject->Flags & DRVO_REINIT_REGISTERED))
    {
        IopDriverLoadingFailed(ServiceHandle, NULL);
        DPRINT1("IopPnpDriverStarted: return STATUS_PLUGPLAY_NO_DEVICE\n");
        return STATUS_PLUGPLAY_NO_DEVICE;
    }

Exit:
    IopDeleteLegacyKey(DriverObject);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopLoadDriver(
    _In_ HANDLE ServiceHandle,
    _In_ BOOLEAN SafeBootModeFlag,
    _In_ BOOLEAN IsFilter,
    _Out_ NTSTATUS * OutInitStatus)
{
    DPRINT("IopLoadDriver: SafeBootModeFlag - %X, IsFilter - %X\n",
           SafeBootModeFlag, IsFilter);
    ASSERT(FALSE);
    return STATUS_SUCCESS;
}

VOID
NTAPI
IopLoadUnloadDriver(
    _In_ PVOID Context)
{
    PLOAD_UNLOAD_PARAMS LoadUnloadDriverContext = Context;
    PDRIVER_OBJECT DriverObject;
    HANDLE KeyHandle;
    NTSTATUS InitStatus;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("IopLoadUnloadDriver: Context %p\n", LoadUnloadDriverContext);

    DriverObject = LoadUnloadDriverContext->DriverObject;
    if (DriverObject)
    {
        DriverObject->DriverUnload(LoadUnloadDriverContext->DriverObject);
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    Status = IopOpenRegistryKey(&KeyHandle,
                                NULL,
                                (PUNICODE_STRING)LoadUnloadDriverContext->RegistryPath,
                                KEY_READ,
                                FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("IopLoadUnloadDriver: Status %X\n", Status);
        goto Exit;
    }

    Status = IopLoadDriver(KeyHandle, TRUE, FALSE, &InitStatus);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("IopLoadUnloadDriver: Status %X\n", Status);
    }

    if (Status == STATUS_FAILED_DRIVER_ENTRY)
        Status = InitStatus;
    else if (Status == STATUS_DRIVER_FAILED_PRIOR_UNLOAD)
        Status = STATUS_OBJECT_NAME_NOT_FOUND;

    IopReinitializeDrivers();

Exit:

    LoadUnloadDriverContext->Status = Status;
    KeSetEvent(&LoadUnloadDriverContext->Event, IO_NO_INCREMENT, FALSE);
}

/*
 * NtLoadDriver
 *
 * Loads a device driver.
 *
 * Parameters
 *    DriverServiceName
 *       Name of the service to load (registry key).
 *
 * Return Value
 *    Status
 *
 * Status
 *    implemented
 */
NTSTATUS NTAPI
NtLoadDriver(IN PUNICODE_STRING DriverServiceName)
{
    UNICODE_STRING CapturedDriverServiceName = { 0, 0, NULL };
    KPROCESSOR_MODE PreviousMode;
    PDRIVER_OBJECT DriverObject;
    NTSTATUS Status;

    PAGED_CODE();

    PreviousMode = KeGetPreviousMode();

    /*
     * Check security privileges
     */
    if (!SeSinglePrivilegeCheck(SeLoadDriverPrivilege, PreviousMode))
    {
        DPRINT("Privilege not held\n");
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Status = ProbeAndCaptureUnicodeString(&CapturedDriverServiceName,
                                          PreviousMode,
                                          DriverServiceName);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    DPRINT("NtLoadDriver('%wZ')\n", &CapturedDriverServiceName);

    /* Load driver and call its entry point */
    DriverObject = NULL;
    Status = IopLoadUnloadDriver(&CapturedDriverServiceName, &DriverObject);

    ReleaseCapturedUnicodeString(&CapturedDriverServiceName,
                                 PreviousMode);

    return Status;
}

/*
 * NtUnloadDriver
 *
 * Unloads a legacy device driver.
 *
 * Parameters
 *    DriverServiceName
 *       Name of the service to unload (registry key).
 *
 * Return Value
 *    Status
 *
 * Status
 *    implemented
 */

NTSTATUS NTAPI
NtUnloadDriver(IN PUNICODE_STRING DriverServiceName)
{
    return IopUnloadDriver(DriverServiceName, FALSE);
}

/* EOF */
