/*
 * PROJECT:     Partition manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main header file
 * COPYRIGHT:   Copyright 2018, 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#ifndef _PARTMGR_H_
#define _PARTMGR_H_

#include <ntifs.h>

typedef struct _PM_DRIVER_EXTENSION
{
    PDRIVER_OBJECT SelfDriverObject;
    LIST_ENTRY NotifyList;
    LIST_ENTRY ExtensionList;
    PVOID NotificationEntry;
    KMUTEX Mutex;
    LONG IsReinitialized;
    RTL_AVL_TABLE TableSignature;
    RTL_AVL_TABLE TableGuid;
    UNICODE_STRING RegistryPath;
} PM_DRIVER_EXTENSION, *PPM_DRIVER_EXTENSION;

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);


#endif /* _PARTMGR_H_ */

/* EOF */
