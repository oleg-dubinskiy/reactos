/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     IRP dispatching
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "acpi.h"

//#define NDEBUG
#include <debug.h>

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, ACPIInitHalDispatchTable)
#endif

/* GLOBALS *******************************************************************/

ACPI_HAL_DISPATCH_TABLE AcpiHalDispatchTable;
PPM_DISPATCH_TABLE PmHalDispatchTable;

/* FUNCTIOS *****************************************************************/

NTSTATUS
NTAPI
ACPIDispatchIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
ACPIFilterFastIoDetachCallback(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* HAL FUNCTIOS *************************************************************/

VOID
NTAPI
ACPIGpeHalEnableDisableEvents(
    _In_ BOOLEAN IsEnable)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ACPIEnableInitializeACPI(
    _In_ BOOLEAN Param1)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ACPIWakeEnableWakeEvents(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ACPIInitHalDispatchTable(VOID)
{
    AcpiHalDispatchTable.Signature = 'ACPI';
    AcpiHalDispatchTable.Version = 1;
    AcpiHalDispatchTable.Function1 = ACPIGpeHalEnableDisableEvents;
    AcpiHalDispatchTable.Function2 = ACPIEnableInitializeACPI;
    AcpiHalDispatchTable.Function3 = ACPIWakeEnableWakeEvents;

    HalInitPowerManagement((PPM_DISPATCH_TABLE)&AcpiHalDispatchTable, &PmHalDispatchTable);
}

/* EOF */
