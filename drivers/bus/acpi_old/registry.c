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
  #pragma alloc_text(PAGE, OSReadRegValue)
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

NTSTATUS
NTAPI
OSReadRegValue(
    _In_ PSZ NameString,
    _In_ HANDLE Handle,
    _Out_ PVOID OutValue,
    _Out_ ULONG* OutMaximumLength)
{
    PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64 KeyValueInfo = NULL;
    UNICODE_STRING ValueName;
    ANSI_STRING AnsiName;
    HANDLE KeyHandle = NULL;
    ULONG ResultLength = 0;
    ULONG Length;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("OSReadRegValue: '%s', %p\n", NameString, Handle);

    if (Handle)
    {
        KeyHandle = Handle;
    }
    else
    {
        Status = OSOpenHandle("\\Registry\\Machine\\System\\CurrentControlSet\\Services\\ACPI\\Parameters", NULL, &KeyHandle);
        if (!NT_SUCCESS(Status) || !KeyHandle)
        {
            DPRINT1("OSReadRegValue: Status %X\n", Status);
            return Status;
        }
    }

    RtlInitAnsiString(&AnsiName, NameString);

    Status = RtlAnsiStringToUnicodeString(&ValueName, &AnsiName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSReadRegValue: Status %X\n", Status);

        if (!Handle)
            OSCloseHandle(KeyHandle);

        return Status;
    }

    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformationAlign64, NULL, 0, &ResultLength);

    if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
    {
        DPRINT1("OSReadRegValue: Status %X\n", Status);

        RtlFreeUnicodeString(&ValueName);

        if (!Handle)
            OSCloseHandle(KeyHandle);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("OSReadRegValue: Status %X\n", Status);
            return Status;
        }

        return STATUS_UNSUCCESSFUL;
    }

    while (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
    {
        Length = ResultLength;

        KeyValueInfo = ExAllocatePoolWithTag(PagedPool, ResultLength, 'MpcA');
        if (!KeyValueInfo)
        {
            DPRINT1("OSReadRegValue: Allocate failed\n");

            RtlFreeUnicodeString(&ValueName);

            if (!Handle)
                OSCloseHandle(KeyHandle);

            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformationAlign64, KeyValueInfo, Length, &ResultLength);

        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
        {
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("OSReadRegValue: Status %X\n", Status);

                RtlFreeUnicodeString(&ValueName);

                if (!Handle)
                    OSCloseHandle(KeyHandle);

                ExFreePool(KeyValueInfo);
                return Status;
            }

            break;
        }

        ExFreePool(KeyValueInfo);
    }

    RtlFreeUnicodeString(&ValueName);

    if (Handle)
        OSCloseHandle(KeyHandle);

    if (KeyValueInfo->Type != REG_SZ && KeyValueInfo->Type != REG_MULTI_SZ)
    {
        if (*OutMaximumLength >= KeyValueInfo->DataLength)
        {
            RtlCopyMemory(OutValue, KeyValueInfo->Data, KeyValueInfo->DataLength);
            *OutMaximumLength = KeyValueInfo->DataLength;

            ExFreePool(KeyValueInfo);
            STATUS_SUCCESS;
        }

        ExFreePool(KeyValueInfo);
        return STATUS_BUFFER_OVERFLOW;
    }

    RtlInitUnicodeString(&ValueName, (PWSTR)KeyValueInfo->Data);

    Status = RtlUnicodeStringToAnsiString(&AnsiName, &ValueName, TRUE);

    DPRINT("OSReadRegValue: '%s'\n", AnsiName.Buffer);

    ExFreePool(KeyValueInfo);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSReadRegValue: Status %X\n", Status);
        return Status;
    }

    if (*OutMaximumLength >= AnsiName.MaximumLength)
    {
        *OutMaximumLength = AnsiName.MaximumLength;
        RtlCopyMemory(OutValue, AnsiName.Buffer, AnsiName.MaximumLength);

        RtlFreeAnsiString(&AnsiName);
        STATUS_SUCCESS;
    }

    DPRINT1("OSReadRegValue: %X < %X\n", *OutMaximumLength, AnsiName.MaximumLength);

    RtlFreeAnsiString(&AnsiName);

    return STATUS_BUFFER_OVERFLOW;
}

VOID
NTAPI
ACPIInitReadRegistryKeys(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
