/*
 * PROJECT:     Partition manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main file
 * COPYRIGHT:   Copyright 2018, 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "partmgr.h"

//#define NDEBUG
#include <debug.h>

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DriverEntry)
#endif

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, PmAddDevice)
  #pragma alloc_text(PAGE, PmDeviceControl)
  #pragma alloc_text(PAGE, PmPower)
  #pragma alloc_text(PAGE, PmWmi)
  #pragma alloc_text(PAGE, PmkPnp)
  #pragma alloc_text(PAGE, PmDriverReinit)
  #pragma alloc_text(PAGE, PmTableSignatureCompareRoutine)
  #pragma alloc_text(PAGE, PmTableGuidCompareRoutine)
  #pragma alloc_text(PAGE, PmTableAllocateRoutine)
  #pragma alloc_text(PAGE, PmTableFreeRoutine)
  #pragma alloc_text(PAGE, PmVolumeManagerNotification)
#endif

/* GLOBALS *******************************************************************/

GUID VOLMGR_VOLUME_MANAGER_GUID = {0x53F5630E, 0xB6BF, 0x11D0, {0X94, 0XF2, 0X00, 0XA0, 0XC9, 0X1E, 0XFB, 0X8B}};

/* FUNCTIONS ****************************************************************/

NTSTATUS
NTAPI
PmPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
PmUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
PmAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DiskPdo)
{
    PPM_DEVICE_EXTENSION Extension;
    PDEVICE_OBJECT PartitionFido;
    PDEVICE_OBJECT TopDevice;
    NTSTATUS Status;

    DPRINT("PmAddDevice: %p, %p\n", DriverObject, DiskPdo);

    TopDevice = IoGetAttachedDeviceReference(DiskPdo);
    if (TopDevice)
    {
        ObDereferenceObject(TopDevice);

        if (TopDevice->Characteristics & FILE_REMOVABLE_MEDIA)
            return STATUS_SUCCESS;
    }

    Status = IoCreateDevice(DriverObject,
                            sizeof(*Extension),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            0,
                            FALSE,
                            &PartitionFido);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PmAddDevice: %p, %p\n", DriverObject, DiskPdo);
        return Status;
    }

    PartitionFido->Flags |= DO_DIRECT_IO;

    if (TopDevice->Flags & DO_POWER_INRUSH)
        PartitionFido->Flags |= DO_POWER_INRUSH;
    else
        PartitionFido->Flags |= DO_POWER_PAGABLE;

    Extension = PartitionFido->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));

    Extension->PartitionFido = PartitionFido;
    Extension->DriverExtension = IoGetDriverObjectExtension(DriverObject, PmAddDevice);

    Extension->AttachedToDevice = IoAttachDeviceToDeviceStack(PartitionFido, DiskPdo);
    if (!Extension->AttachedToDevice)
    {
        DPRINT1("PmAddDevice: AttachedToDevice is NULL! (%p, %p)\n", DriverObject, DiskPdo);
        IoDeleteDevice(PartitionFido);
        return STATUS_NO_SUCH_DEVICE;
    }

    Extension->WholeDiskPdo = DiskPdo;

    KeInitializeEvent(&Extension->Event, SynchronizationEvent, TRUE);

    InitializeListHead(&Extension->PartitionList);
    InitializeListHead(&Extension->ListOfSignatures);
    InitializeListHead(&Extension->ListOfGuids);

    KeWaitForSingleObject(&Extension->DriverExtension->Mutex, Executive, KernelMode, FALSE, NULL);
    InsertTailList(&Extension->DriverExtension->ExtensionList, &Extension->Link);
    KeReleaseMutex(&Extension->DriverExtension->Mutex, TRUE);

    PartitionFido->DeviceType = Extension->AttachedToDevice->DeviceType;
    PartitionFido->AlignmentRequirement = Extension->AttachedToDevice->AlignmentRequirement;

    Extension->NameString.Buffer = Extension->NameBuffer;

    PartitionFido->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PmVolumeManagerNotification(
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* AVL TABLE ROUTINES *******************************************************/

RTL_GENERIC_COMPARE_RESULTS
NTAPI
PmTableSignatureCompareRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

RTL_GENERIC_COMPARE_RESULTS
NTAPI
PmTableGuidCompareRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

PVOID
NTAPI
PmTableAllocateRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ CLONG ByteSize)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

VOID
NTAPI
PmTableFreeRoutine(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID Buffer)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* DRIVER DISPATCH ROUTINES *************************************************/

NTSTATUS
NTAPI
PmReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PmDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PmPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PmWmi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
PmPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* REINITIALIZE DRIVER ROUTINES *********************************************/

VOID
NTAPI
PmDriverReinit(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
PmBootDriverReinit(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PVOID Context,
    _In_ ULONG Count)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* INIT DRIVER ROUTINES *****************************************************/

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    PPM_DRIVER_EXTENSION DriverExtension;
    ULONG size;
    ULONG ix;
    NTSTATUS Status;

    DPRINT("DriverEntry: %p, '%wZ'\n", DriverObject, RegistryPath);

    for (ix = 0; ix <= IRP_MJ_MAXIMUM_FUNCTION; ix++)
        DriverObject->MajorFunction[ix] = PmPassThrough;

    DriverObject->MajorFunction[IRP_MJ_READ] = PmReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = PmReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PmDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PmPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = PmWmi;
    DriverObject->MajorFunction[IRP_MJ_PNP] = PmPnp;

    DriverObject->DriverExtension->AddDevice = PmAddDevice;
    DriverObject->DriverUnload = PmUnload;

    Status = IoAllocateDriverObjectExtension(DriverObject, PmAddDevice, sizeof(*DriverExtension), (PVOID *)&DriverExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        return Status;
    }

    size = (RegistryPath->Length + 2);

    DriverExtension->RegistryPath.MaximumLength = size;
    DriverExtension->RegistryPath.Buffer = ExAllocatePoolWithTag(PagedPool, size, 'pRcS');

    if (DriverExtension->RegistryPath.Buffer)
    {
        RtlCopyUnicodeString(&DriverExtension->RegistryPath, RegistryPath);
    }
    else
    {
        DriverExtension->RegistryPath.Length = 0;
        DriverExtension->RegistryPath.MaximumLength = 0;
    }

    DriverExtension->SelfDriverObject = DriverObject;

    InitializeListHead(&DriverExtension->NotifyList);
    InitializeListHead(&DriverExtension->ExtensionList);
    KeInitializeMutex(&DriverExtension->Mutex, 0);

    DriverExtension->IsReinitialized = 0;

    RtlInitializeGenericTableAvl(&DriverExtension->TableSignature,
                                 PmTableSignatureCompareRoutine,
                                 PmTableAllocateRoutine,
                                 PmTableFreeRoutine,
                                 DriverExtension);

    RtlInitializeGenericTableAvl(&DriverExtension->TableGuid,
                                 PmTableGuidCompareRoutine,
                                 PmTableAllocateRoutine,
                                 PmTableFreeRoutine,
                                 DriverExtension);

    IoRegisterBootDriverReinitialization(DriverObject, PmBootDriverReinit, DriverExtension);

    Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                            &VOLMGR_VOLUME_MANAGER_GUID,
                                            DriverObject,
                                            PmVolumeManagerNotification,
                                            DriverExtension,
                                            &DriverExtension->NotificationEntry);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DriverEntry: Status %X\n", Status);
        return Status;
    }

#if 0
    DriverExtension->RegistrySignature = PmQueryRegistrySignature();

    PmQueryRegistryGuid(DriverExtension);
    PmQueryRegistrySnapshotSettings(DriverExtension);

    PmQueryRegistryEpochMode(DriverExtension);
#endif

    DPRINT("DriverEntry: exit with STATUS_SUCCESS\n");
    return STATUS_SUCCESS;
}

/* EOF */
