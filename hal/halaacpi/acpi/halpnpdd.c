
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* INCLUDES *******************************************************************/


/* GLOBALS ********************************************************************/


/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
HalpDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
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
