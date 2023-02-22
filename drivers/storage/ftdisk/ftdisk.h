/*
 * PROJECT:     Volume manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main header file
 * COPYRIGHT:   Copyright 2018, 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#ifndef _FTDISK_H_
#define _FTDISK_H_

#include <ntifs.h>

typedef struct _ROOT_EXTENSION
{
    PDEVICE_OBJECT VolControlRootPdo;
    UNICODE_STRING SymbolicLinkName;
} ROOT_EXTENSION, *PROOT_EXTENSION;

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

#endif /* _FTDISK_H_ */

/* EOF */
