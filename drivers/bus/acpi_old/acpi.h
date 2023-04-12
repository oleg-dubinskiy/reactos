/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main header file
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#ifndef _ACPI_H_
#define _ACPI_H_

#include <ntddk.h>

typedef struct _DEVICE_EXTENSION
{
    ULONG Signature;

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;


/* acpiinit.c */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

/* dispatch.c */
NTSTATUS
NTAPI
ACPIDispatchIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
);

VOID
NTAPI
ACPIFilterFastIoDetachCallback(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice
);

VOID
NTAPI
ACPIInitHalDispatchTable(
     VOID
);

/* registry.c */
VOID
NTAPI
ACPIInitReadRegistryKeys(
    VOID
);


#endif /* _ACPI_H_ */

/* EOF */
