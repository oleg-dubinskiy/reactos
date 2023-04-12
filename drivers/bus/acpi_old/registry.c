/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Working with the registry
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "acpi.h"

//#define NDEBUG
#include <debug.h>

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, OSCloseHandle)
  #pragma alloc_text(PAGE, OSOpenUnicodeHandle)
#endif

/* GLOBALS *******************************************************************/


/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
OSCloseHandle(
    _In_ HANDLE Handle)
{
    PAGED_CODE();
    DPRINT("OSCloseHandle: %p\n", Handle);
    return ZwClose(Handle);
}

NTSTATUS
NTAPI
OSOpenUnicodeHandle(
    _In_ PUNICODE_STRING Name,
    _In_ HANDLE ParentKeyHandle,
    _In_ PHANDLE KeyHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("OSOpenUnicodeHandle: '%wZ'\n", Name);

    InitializeObjectAttributes(&ObjectAttributes,
                               Name,
                               (OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE),
                               ParentKeyHandle,
                               NULL);

    Status = ZwOpenKey(KeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSOpenUnicodeHandle: Status %X\n", Status);
    }

    return Status;
}

VOID
NTAPI
ACPIInitReadRegistryKeys(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
