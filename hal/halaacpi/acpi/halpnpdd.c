
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* INCLUDES *******************************************************************/

typedef enum _EXTENSION_TYPE
{
    PdoExtensionType = 0xC0,
    FdoExtensionType
} EXTENSION_TYPE;

typedef enum _PDO_TYPE
{
    AcpiPdo = 0x81,
    WdPdo = 0x82
} PDO_TYPE;

typedef struct _FDO_EXTENSION
{
    EXTENSION_TYPE ExtensionType;
    struct _PDO_EXTENSION* ChildPdoList;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT FunctionalDeviceObject;
    PDEVICE_OBJECT AttachedDeviceObject;
} FDO_EXTENSION, *PFDO_EXTENSION;

typedef struct _PDO_EXTENSION
{
    EXTENSION_TYPE ExtensionType;
    struct _PDO_EXTENSION* Next;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PFDO_EXTENSION ParentFdoExtension;
    PDO_TYPE PdoType;
    PDESCRIPTION_HEADER WdTable;
    LONG InterfaceReferenceCount;
} PDO_EXTENSION, *PPDO_EXTENSION;

/* GLOBALS ********************************************************************/

PDRIVER_OBJECT HalpDriverObject;

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
HalpAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalpDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalpDispatchWmi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalpDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalpDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;
    PDEVICE_OBJECT TargetDevice = NULL;

    PAGED_CODE();
    DPRINT("HalpDriverEntry: DriverObject %p, '%wZ'\n", DriverObject, RegistryPath);

    /* This is us */
    HalpDriverObject = DriverObject;

    /* Set up add device */
    DriverObject->DriverExtension->AddDevice = HalpAddDevice;

    /* Set up the callouts */
    DriverObject->MajorFunction[IRP_MJ_PNP] = HalpDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HalpDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HalpDispatchWmi;

    /* Tell the PnP manager about us */
    Status = IoReportDetectedDevice(DriverObject,
                                    InterfaceTypeUndefined,
                                    -1,
                                    -1,
                                    NULL,
                                    NULL,
                                    FALSE,
                                    &TargetDevice);

    DPRINT("HalpDriverEntry: TargetDevice %p\n", TargetDevice);
    ASSERT(TargetDevice);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpDriverEntry: Status %X\n", Status);
        ASSERT(FALSE);
        return Status;
    }

    HalpAddDevice(DriverObject, TargetDevice);

    /* Return to kernel */
    return STATUS_SUCCESS;
}

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
HaliInitPnpDriver(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING DriverString;

    DPRINT("HaliInitPnpDriver()\n");
    PAGED_CODE();

    /* Create the driver */
    RtlInitUnicodeString(&DriverString, L"\\Driver\\ACPI_HAL");

    Status = IoCreateDriver(&DriverString, HalpDriverEntry);
    ASSERT(NT_SUCCESS(Status));

    /* Return status */
    return Status;
}

/* EOF */
