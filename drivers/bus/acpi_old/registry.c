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
  #pragma alloc_text(PAGE, OSOpenHandle)
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

NTSTATUS
NTAPI
OSOpenHandle(
    _In_ PSZ NameString,
    _In_ HANDLE ParentKeyHandle,
    _In_ PHANDLE KeyHandle)
{
    UNICODE_STRING Name;
    ANSI_STRING AnsiName;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("OSOpenHandle: '%s'\n", NameString);

    RtlInitAnsiString(&AnsiName, NameString);

    Status = RtlAnsiStringToUnicodeString(&Name, &AnsiName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSOpenHandle: Status %X\n", Status);
        return Status;
    }

    Status = OSOpenUnicodeHandle(&Name, ParentKeyHandle, KeyHandle);

    RtlFreeUnicodeString(&Name);

    return Status;
}

VOID
NTAPI
ACPIInitReadRegistryKeys(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
