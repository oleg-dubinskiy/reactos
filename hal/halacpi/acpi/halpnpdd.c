
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PDRIVER_OBJECT HalpDriverObject;

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
HalpAddDevice(IN PDRIVER_OBJECT DriverObject,
              IN PDEVICE_OBJECT TargetDevice)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalpDispatchPnp(IN PDEVICE_OBJECT DeviceObject,
                IN PIRP Irp)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalpDispatchWmi(IN PDEVICE_OBJECT DeviceObject,
                IN PIRP Irp)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalpDispatchPower(IN PDEVICE_OBJECT DeviceObject,
                  IN PIRP Irp)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalpDriverEntry(IN PDRIVER_OBJECT DriverObject,
                IN PUNICODE_STRING RegistryPath)
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

NTSTATUS
NTAPI
HaliInitPnpDriver(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING DriverString;

    PAGED_CODE();
    DPRINT("HaliInitPnpDriver()\n");

    /* Create the driver */
    RtlInitUnicodeString(&DriverString, L"\\Driver\\ACPI_HAL");

    Status = IoCreateDriver(&DriverString, HalpDriverEntry);
    ASSERT(NT_SUCCESS(Status));

    /* Return status */
    return Status;
}

/* EOF */
