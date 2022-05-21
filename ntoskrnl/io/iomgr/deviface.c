/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/iomgr/deviface.c
 * PURPOSE:         Device interface functions
 *
 * PROGRAMMERS:     Filip Navara (xnavara@volny.cz)
 *                  Matthew Brace (ismarc@austin.rr.com)
 *                  Hervé Poussineau (hpoussin@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "../pnpio.h"

//#define NDEBUG
#include <debug.h>

/* FIXME: This should be somewhere global instead of having 20 different versions */
#define GUID_STRING_CHARS 38
#define GUID_STRING_BYTES (GUID_STRING_CHARS * sizeof(WCHAR))
C_ASSERT(sizeof(L"{01234567-89ab-cdef-0123-456789abcdef}") == GUID_STRING_BYTES + sizeof(UNICODE_NULL));

extern ERESOURCE PpRegistryDeviceResource;

/* FUNCTIONS *****************************************************************/

PDEVICE_OBJECT
IopGetDeviceObjectFromDeviceInstance(PUNICODE_STRING DeviceInstance);

static PWCHAR BaseKeyString = L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\DeviceClasses\\";

static
NTSTATUS
OpenRegistryHandlesFromSymbolicLink(IN PUNICODE_STRING SymbolicLinkName,
                                    IN ACCESS_MASK DesiredAccess,
                                    IN OPTIONAL PHANDLE GuidKey,
                                    IN OPTIONAL PHANDLE DeviceKey,
                                    IN OPTIONAL PHANDLE InstanceKey)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING BaseKeyU;
    UNICODE_STRING GuidString, SubKeyName, ReferenceString;
    PWCHAR StartPosition, EndPosition;
    HANDLE ClassesKey;
    PHANDLE GuidKeyRealP, DeviceKeyRealP, InstanceKeyRealP;
    HANDLE GuidKeyReal, DeviceKeyReal, InstanceKeyReal;
    NTSTATUS Status;

    SubKeyName.Buffer = NULL;

    if (GuidKey != NULL)
        GuidKeyRealP = GuidKey;
    else
        GuidKeyRealP = &GuidKeyReal;

    if (DeviceKey != NULL)
        DeviceKeyRealP = DeviceKey;
    else
        DeviceKeyRealP = &DeviceKeyReal;

    if (InstanceKey != NULL)
        InstanceKeyRealP = InstanceKey;
    else
        InstanceKeyRealP = &InstanceKeyReal;

    *GuidKeyRealP = NULL;
    *DeviceKeyRealP = NULL;
    *InstanceKeyRealP = NULL;

    RtlInitUnicodeString(&BaseKeyU, BaseKeyString);

    /* Open the DeviceClasses key */
    InitializeObjectAttributes(&ObjectAttributes,
                               &BaseKeyU,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenKey(&ClassesKey,
                       DesiredAccess | KEY_ENUMERATE_SUB_KEYS,
                       &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to open %wZ\n", &BaseKeyU);
        goto cleanup;
    }

    StartPosition = wcschr(SymbolicLinkName->Buffer, L'{');
    EndPosition = wcschr(SymbolicLinkName->Buffer, L'}');
    if (!StartPosition || !EndPosition || StartPosition > EndPosition)
    {
        DPRINT1("Bad symbolic link: %wZ\n", SymbolicLinkName);
        return STATUS_INVALID_PARAMETER_1;
    }
    GuidString.Buffer = StartPosition;
    GuidString.MaximumLength = GuidString.Length = (USHORT)((ULONG_PTR)(EndPosition + 1) - (ULONG_PTR)StartPosition);

    InitializeObjectAttributes(&ObjectAttributes,
                               &GuidString,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               ClassesKey,
                               NULL);
    Status = ZwCreateKey(GuidKeyRealP,
                         DesiredAccess | KEY_ENUMERATE_SUB_KEYS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         NULL);
    ZwClose(ClassesKey);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to open %wZ%wZ (%x)\n", &BaseKeyU, &GuidString, Status);
        goto cleanup;
    }

    SubKeyName.MaximumLength = SymbolicLinkName->Length + sizeof(WCHAR);
    SubKeyName.Length = 0;
    SubKeyName.Buffer = ExAllocatePool(PagedPool, SubKeyName.MaximumLength);
    if (!SubKeyName.Buffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }

    RtlAppendUnicodeStringToString(&SubKeyName,
                                   SymbolicLinkName);

    SubKeyName.Buffer[SubKeyName.Length / sizeof(WCHAR)] = UNICODE_NULL;

    SubKeyName.Buffer[0] = L'#';
    SubKeyName.Buffer[1] = L'#';
    SubKeyName.Buffer[2] = L'?';
    SubKeyName.Buffer[3] = L'#';

    ReferenceString.Buffer = wcsrchr(SubKeyName.Buffer, '\\');
    if (ReferenceString.Buffer != NULL)
    {
        ReferenceString.Buffer[0] = L'#';

        SubKeyName.Length = (USHORT)((ULONG_PTR)(ReferenceString.Buffer) - (ULONG_PTR)SubKeyName.Buffer);
        ReferenceString.Length = SymbolicLinkName->Length - SubKeyName.Length;
    }
    else
    {
        RtlInitUnicodeString(&ReferenceString, L"#");
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &SubKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               *GuidKeyRealP,
                               NULL);
    Status = ZwCreateKey(DeviceKeyRealP,
                         DesiredAccess | KEY_ENUMERATE_SUB_KEYS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to open %wZ%wZ\\%wZ Status %x\n", &BaseKeyU, &GuidString, &SubKeyName, Status);
        goto cleanup;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &ReferenceString,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               *DeviceKeyRealP,
                               NULL);
    Status = ZwCreateKey(InstanceKeyRealP,
                         DesiredAccess,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to open %wZ%wZ\\%wZ%\\%wZ (%x)\n", &BaseKeyU, &GuidString, &SubKeyName, &ReferenceString, Status);
        goto cleanup;
    }

    Status = STATUS_SUCCESS;

cleanup:
    if (SubKeyName.Buffer != NULL)
        ExFreePool(SubKeyName.Buffer);

    if (NT_SUCCESS(Status))
    {
        if (!GuidKey)
            ZwClose(*GuidKeyRealP);

        if (!DeviceKey)
            ZwClose(*DeviceKeyRealP);

        if (!InstanceKey)
            ZwClose(*InstanceKeyRealP);
    }
    else
    {
        if (*GuidKeyRealP != NULL)
            ZwClose(*GuidKeyRealP);

        if (*DeviceKeyRealP != NULL)
            ZwClose(*DeviceKeyRealP);

        if (*InstanceKeyRealP != NULL)
            ZwClose(*InstanceKeyRealP);
    }

    return Status;
}

/*++
 * @name IoOpenDeviceInterfaceRegistryKey
 * @unimplemented
 *
 * Provides a handle to the device's interface instance registry key.
 * Documented in WDK.
 *
 * @param SymbolicLinkName
 *        Pointer to a string which identifies the device interface instance
 *
 * @param DesiredAccess
 *        Desired ACCESS_MASK used to access the key (like KEY_READ,
 *        KEY_WRITE, etc)
 *
 * @param DeviceInterfaceKey
 *        If a call has been succesfull, a handle to the registry key
 *        will be stored there
 *
 * @return Three different NTSTATUS values in case of errors, and STATUS_SUCCESS
 *         otherwise (see WDK for details)
 *
 * @remarks Must be called at IRQL = PASSIVE_LEVEL in the context of a system thread
 *
 *--*/
NTSTATUS
NTAPI
IoOpenDeviceInterfaceRegistryKey(IN PUNICODE_STRING SymbolicLinkName,
                                 IN ACCESS_MASK DesiredAccess,
                                 OUT PHANDLE DeviceInterfaceKey)
{
    HANDLE InstanceKey, DeviceParametersKey;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DeviceParametersU = RTL_CONSTANT_STRING(L"Device Parameters");

    Status = OpenRegistryHandlesFromSymbolicLink(SymbolicLinkName,
                                                 KEY_CREATE_SUB_KEY,
                                                 NULL,
                                                 NULL,
                                                 &InstanceKey);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               &DeviceParametersU,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_OPENIF,
                               InstanceKey,
                               NULL);
    Status = ZwCreateKey(&DeviceParametersKey,
                         DesiredAccess,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    ZwClose(InstanceKey);

    if (NT_SUCCESS(Status))
        *DeviceInterfaceKey = DeviceParametersKey;

    return Status;
}

/*++
 * @name IoGetDeviceInterfaceAlias
 * @unimplemented
 *
 * Returns the alias device interface of the specified device interface
 * instance, if the alias exists.
 * Documented in WDK.
 *
 * @param SymbolicLinkName
 *        Pointer to a string which identifies the device interface instance
 *
 * @param AliasInterfaceClassGuid
 *        See WDK
 *
 * @param AliasSymbolicLinkName
 *        See WDK
 *
 * @return Three different NTSTATUS values in case of errors, and STATUS_SUCCESS
 *         otherwise (see WDK for details)
 *
 * @remarks Must be called at IRQL = PASSIVE_LEVEL in the context of a system thread
 *
 *--*/
NTSTATUS
NTAPI
IoGetDeviceInterfaceAlias(IN PUNICODE_STRING SymbolicLinkName,
                          IN CONST GUID *AliasInterfaceClassGuid,
                          OUT PUNICODE_STRING AliasSymbolicLinkName)
{
    return STATUS_NOT_IMPLEMENTED;
}

/*++
 * @name IopOpenInterfaceKey
 *
 * Returns the alias device interface of the specified device interface
 *
 * @param InterfaceClassGuid
 *        FILLME
 *
 * @param DesiredAccess
 *        FILLME
 *
 * @param pInterfaceKey
 *        FILLME
 *
 * @return Usual NTSTATUS
 *
 * @remarks None
 *
 *--*/
static NTSTATUS
IopOpenInterfaceKey(IN CONST GUID *InterfaceClassGuid,
                    IN ACCESS_MASK DesiredAccess,
                    OUT HANDLE *pInterfaceKey)
{
    UNICODE_STRING LocalMachine = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\");
    UNICODE_STRING GuidString;
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE InterfaceKey = NULL;
    NTSTATUS Status;

    GuidString.Buffer = KeyName.Buffer = NULL;

    Status = RtlStringFromGUID(InterfaceClassGuid, &GuidString);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("RtlStringFromGUID() failed with status 0x%08lx\n", Status);
        goto cleanup;
    }

    KeyName.Length = 0;
    KeyName.MaximumLength = LocalMachine.Length + ((USHORT)wcslen(REGSTR_PATH_DEVICE_CLASSES) + 1) * sizeof(WCHAR) + GuidString.Length;
    KeyName.Buffer = ExAllocatePool(PagedPool, KeyName.MaximumLength);
    if (!KeyName.Buffer)
    {
        DPRINT("ExAllocatePool() failed\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }

    Status = RtlAppendUnicodeStringToString(&KeyName, &LocalMachine);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("RtlAppendUnicodeStringToString() failed with status 0x%08lx\n", Status);
        goto cleanup;
    }
    Status = RtlAppendUnicodeToString(&KeyName, REGSTR_PATH_DEVICE_CLASSES);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("RtlAppendUnicodeToString() failed with status 0x%08lx\n", Status);
        goto cleanup;
    }
    Status = RtlAppendUnicodeToString(&KeyName, L"\\");
    if (!NT_SUCCESS(Status))
    {
        DPRINT("RtlAppendUnicodeToString() failed with status 0x%08lx\n", Status);
        goto cleanup;
    }
    Status = RtlAppendUnicodeStringToString(&KeyName, &GuidString);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("RtlAppendUnicodeStringToString() failed with status 0x%08lx\n", Status);
        goto cleanup;
    }

    InitializeObjectAttributes(
        &ObjectAttributes,
        &KeyName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL);
    Status = ZwOpenKey(
        &InterfaceKey,
        DesiredAccess,
        &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ZwOpenKey() failed with status 0x%08lx\n", Status);
        goto cleanup;
    }

    *pInterfaceKey = InterfaceKey;
    Status = STATUS_SUCCESS;

cleanup:
    if (!NT_SUCCESS(Status))
    {
        if (InterfaceKey != NULL)
            ZwClose(InterfaceKey);
    }
    RtlFreeUnicodeString(&GuidString);
    RtlFreeUnicodeString(&KeyName);
    return Status;
}

/* Returns a list of device interfaces of a particular device interface class.
   Documented in WDK

   InterfaceClassGuid
      Points to a class GUID specifying the device interface class
   PhysicalDeviceObject
      Points to an optional PDO that narrows the search to only the device interfaces of the device represented by the PDO
   Flags
      Specifies flags that modify the search for device interfaces.
      The DEVICE_INTERFACE_INCLUDE_NONACTIVE flag specifies that the list of returned symbolic links
      should contain also disabled device interfaces in addition to the enabled ones.
   SymbolicLinkList
      Points to a character pointer that is filled in on successful return with a list
      of unicode strings identifying the device interfaces that match the search criteria.
      The newly allocated buffer contains a list of symbolic link names.
      Each unicode string in the list is null-terminated; the end of the whole list is marked by an additional NULL.
      The caller is responsible for freeing the buffer (ExFreePool) when it is no longer needed.
      If no device interfaces match the search criteria, this routine returns STATUS_SUCCESS and the string contains a single NULL character.
*/
NTSTATUS
NTAPI
IoGetDeviceInterfaces(
    _In_ CONST GUID *InterfaceClassGuid,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject OPTIONAL,
    _In_ ULONG Flags,
    _Out_ PWSTR* SymbolicLinkList)
{
    UNICODE_STRING Control = RTL_CONSTANT_STRING(L"Control");
    UNICODE_STRING SymbolicLink = RTL_CONSTANT_STRING(L"SymbolicLink");
    UNICODE_STRING DeviceInstanceName = RTL_CONSTANT_STRING(L"DeviceInstance");
    UNICODE_STRING LinkedName = RTL_CONSTANT_STRING(L"Linked");
    HANDLE InterfaceKey = NULL;
    HANDLE DeviceKey = NULL;
    HANDLE ReferenceKey = NULL;
    HANDLE ControlKey = NULL;
    PKEY_BASIC_INFORMATION DeviceBi = NULL;
    PKEY_BASIC_INFORMATION ReferenceBi = NULL;
    PKEY_VALUE_PARTIAL_INFORMATION bip = NULL;
    PKEY_VALUE_PARTIAL_INFORMATION PartialInfo;
    PEXTENDED_DEVOBJ_EXTENSION DeviceObjectExtension;
    PUNICODE_STRING InstanceDevicePath = NULL;
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    BOOLEAN FoundRightPDO = FALSE;
    ULONG ix, jx, Size, NeededLength, ActualLength, LinkedValue;
    UNICODE_STRING ReturnBuffer = { 0, 0, NULL };
    USHORT Length;
    NTSTATUS Status;

    PAGED_CODE();

    if (PhysicalDeviceObject)
    {
        /* Parameters must pass three border of checks */
        DeviceObjectExtension = (PEXTENDED_DEVOBJ_EXTENSION)PhysicalDeviceObject->DeviceObjectExtension;

        /* 1st level: Presence of a Device Node */
        if (!DeviceObjectExtension->DeviceNode)
        {
            DPRINT("PhysicalDeviceObject %p doesn't have a DeviceNode\n", PhysicalDeviceObject);
            return STATUS_INVALID_DEVICE_REQUEST;
        }

        /* 2nd level: Presence of an non-zero length InstancePath */
        if (!DeviceObjectExtension->DeviceNode->InstancePath.Length)
        {
            DPRINT("PhysicalDeviceObject %p's DOE has zero-length InstancePath\n", PhysicalDeviceObject);
            return STATUS_INVALID_DEVICE_REQUEST;
        }

        InstanceDevicePath = &DeviceObjectExtension->DeviceNode->InstancePath;
    }


    Status = IopOpenInterfaceKey(InterfaceClassGuid, KEY_ENUMERATE_SUB_KEYS, &InterfaceKey);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("IopOpenInterfaceKey() failed with status %X\n", Status);
        goto cleanup;
    }

    /* Enumerate subkeys (i.e. the different device objects) */
    for (ix = 0; ; ix++)
    {
        Status = ZwEnumerateKey(InterfaceKey, ix, KeyBasicInformation, NULL, 0, &Size);
        if (Status == STATUS_NO_MORE_ENTRIES)
            break;

        if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL)
        {
            DPRINT1("ZwEnumerateKey() failed with status %X\n", Status);
            goto cleanup;
        }

        DeviceBi = ExAllocatePool(PagedPool, Size);
        if (!DeviceBi)
        {
            DPRINT1("ExAllocatePool() failed\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto cleanup;
        }

        Status = ZwEnumerateKey(InterfaceKey, ix, KeyBasicInformation, DeviceBi, Size, &Size);
        if (!NT_SUCCESS(Status))
        {
            DPRINT("ZwEnumerateKey() failed with status %X\n", Status);
            goto cleanup;
        }

        /* Open device key */
        KeyName.Length = KeyName.MaximumLength = (USHORT)DeviceBi->NameLength;
        KeyName.Buffer = DeviceBi->Name;

        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
                                   InterfaceKey,
                                   NULL);

        Status = ZwOpenKey(&DeviceKey, KEY_ENUMERATE_SUB_KEYS, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ZwOpenKey() failed with status %X\n", Status);
            goto cleanup;
        }

        if (PhysicalDeviceObject)
        {
            /* Check if we are on the right physical device object, by reading the DeviceInstance string */
            Status = ZwQueryValueKey(DeviceKey, &DeviceInstanceName, KeyValuePartialInformation, NULL, 0, &NeededLength);
            if (Status != STATUS_BUFFER_TOO_SMALL)
                break; /* error */

            ActualLength = NeededLength;
            PartialInfo = ExAllocatePool(NonPagedPool, ActualLength);
            if (!PartialInfo)
            {
                DPRINT1("IoGetDeviceInterfaces: STATUS_INSUFFICIENT_RESOURCES\n");
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto cleanup;
            }

            Status = ZwQueryValueKey(DeviceKey, &KeyName, KeyValuePartialInformation, PartialInfo, ActualLength, &NeededLength);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("ZwQueryValueKey #2 failed (%x)\n", Status);
                ExFreePool(PartialInfo);
                goto cleanup;
            }

            Length = InstanceDevicePath->Length;

            if (PartialInfo->DataLength == Length)
            {
                if (RtlCompareMemory(PartialInfo->Data, InstanceDevicePath->Buffer, Length) == Length)
                    /* found right pdo */
                    FoundRightPDO = TRUE;
            }

            ExFreePool(PartialInfo);
            PartialInfo = NULL;

            if (!FoundRightPDO)
                continue; /* not yet found */
        }

        /* Enumerate subkeys (ie the different reference strings) */
        for (jx = 0; ; jx++)
        {
            Status = ZwEnumerateKey(DeviceKey, jx, KeyBasicInformation, NULL, 0, &Size);
            if (Status == STATUS_NO_MORE_ENTRIES)
                break;

            if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL)
            {
                DPRINT1("ZwEnumerateKey() failed with status %X\n", Status);
                goto cleanup;
            }

            ReferenceBi = ExAllocatePool(PagedPool, Size);
            if (!ReferenceBi)
            {
                DPRINT1("IoGetDeviceInterfaces: STATUS_INSUFFICIENT_RESOURCES\n");
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto cleanup;
            }

            Status = ZwEnumerateKey(DeviceKey, jx, KeyBasicInformation, ReferenceBi, Size, &Size);
            if (!NT_SUCCESS(Status))
            {
                DPRINT("ZwEnumerateKey() failed with status %X\n", Status);
                goto cleanup;
            }

            KeyName.Length = KeyName.MaximumLength = (USHORT)ReferenceBi->NameLength;
            KeyName.Buffer = ReferenceBi->Name;

            if (RtlEqualUnicodeString(&KeyName, &Control, TRUE))
                /* Skip Control subkey */
                goto NextReferenceString;

            /* Open reference key */
            InitializeObjectAttributes(&ObjectAttributes,
                                       &KeyName,
                                       (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
                                       DeviceKey,
                                       NULL);

            Status = ZwOpenKey(&ReferenceKey, KEY_QUERY_VALUE, &ObjectAttributes);
            if (!NT_SUCCESS(Status))
            {
                DPRINT("ZwOpenKey() failed with status %X\n", Status);
                goto cleanup;
            }

            if (!(Flags & DEVICE_INTERFACE_INCLUDE_NONACTIVE))
            {
                /* We have to check if the interface is enabled, by reading the Linked value in the Control subkey */
                InitializeObjectAttributes(&ObjectAttributes,
                                           &Control,
                                           (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
                                           ReferenceKey,
                                           NULL);

                Status = ZwOpenKey(&ControlKey, KEY_QUERY_VALUE, &ObjectAttributes);
                if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
                    /* That's OK. The key doesn't exist (yet) because the interface is not activated. */
                    goto NextReferenceString;

                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("ZwOpenKey() failed with status %X\n", Status);
                    goto cleanup;
                }

                Status = ZwQueryValueKey(ControlKey,
                                         &LinkedName,
                                         KeyValuePartialInformation,
                                         NULL,
                                         0,
                                         &NeededLength);

                if (Status != STATUS_BUFFER_TOO_SMALL)
                {
                    DPRINT1("ZwQueryValueKey #1 failed (%x)\n", Status);
                    goto cleanup;
                }

                ActualLength = NeededLength;
                PartialInfo = ExAllocatePool(NonPagedPool, ActualLength);
                if (!PartialInfo)
                {
                    DPRINT1("IoGetDeviceInterfaces: STATUS_INSUFFICIENT_RESOURCES\n");
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto cleanup;
                }

                Status = ZwQueryValueKey(ControlKey, &LinkedName, KeyValuePartialInformation, PartialInfo, ActualLength,&NeededLength);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("ZwQueryValueKey #2 failed (%x)\n", Status);
                    ExFreePool(PartialInfo);
                    goto cleanup;
                }

                if (PartialInfo->Type != REG_DWORD || PartialInfo->DataLength != sizeof(ULONG))
                {
                    DPRINT1("Bad registry read\n");
                    ExFreePool(PartialInfo);
                    goto cleanup;
                }

                RtlCopyMemory(&LinkedValue, PartialInfo->Data, PartialInfo->DataLength);
                ExFreePool(PartialInfo);

                if (!LinkedValue)
                    /* This interface isn't active */
                    goto NextReferenceString;
            }

            /* Read the SymbolicLink string and add it into SymbolicLinkList */
            Status = ZwQueryValueKey(ReferenceKey, &SymbolicLink, KeyValuePartialInformation, NULL, 0, &Size);
            if (!NT_SUCCESS(Status))
            {
                if (Status != STATUS_BUFFER_TOO_SMALL)
                {
                    DPRINT("ZwQueryValueKey() failed with status %X\n", Status);
                    goto cleanup;
                }
            }

            bip = ExAllocatePool(PagedPool, Size);
            if (!bip)
            {
                DPRINT1("IoGetDeviceInterfaces: STATUS_INSUFFICIENT_RESOURCES\n");
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto cleanup;
            }

            Status = ZwQueryValueKey(ReferenceKey, &SymbolicLink, KeyValuePartialInformation, bip, Size, &Size);
            if (!NT_SUCCESS(Status))
            {
                DPRINT("ZwQueryValueKey() failed with status %X\n", Status);
                goto cleanup;
            }

            if (bip->Type != REG_SZ)
            {
                DPRINT("Unexpected registry type 0x%lx (expected 0x%lx)\n", bip->Type, REG_SZ);
                Status = STATUS_UNSUCCESSFUL;
                goto cleanup;
            }

            if (bip->DataLength < (5 * sizeof(WCHAR)))
            {
                DPRINT("Registry string too short (length %lu, expected %lu at least)\n", bip->DataLength, (5 * sizeof(WCHAR)));
                Status = STATUS_UNSUCCESSFUL;
                goto cleanup;
            }

            KeyName.Length = KeyName.MaximumLength = (USHORT)bip->DataLength;
            KeyName.Buffer = (PWSTR)bip->Data;

            /* Fixup the prefix (from "\\?\") */
            RtlCopyMemory(KeyName.Buffer, L"\\??\\", 4 * sizeof(WCHAR));

            /* Add new symbolic link to symbolic link list */
            if ((ReturnBuffer.Length + KeyName.Length + sizeof(WCHAR)) > ReturnBuffer.MaximumLength)
            {
                PWSTR NewBuffer;
                ReturnBuffer.MaximumLength = (USHORT)max((2 * ReturnBuffer.MaximumLength),
                                                         (USHORT)(ReturnBuffer.Length + KeyName.Length + 2 * sizeof(WCHAR)));

                NewBuffer = ExAllocatePool(PagedPool, ReturnBuffer.MaximumLength);
                if (!NewBuffer)
                {
                    DPRINT1("IoGetDeviceInterfaces: STATUS_INSUFFICIENT_RESOURCES\n");
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto cleanup;
                }

                if (ReturnBuffer.Buffer)
                {
                    RtlCopyMemory(NewBuffer, ReturnBuffer.Buffer, ReturnBuffer.Length);
                    ExFreePool(ReturnBuffer.Buffer);
                }

                ReturnBuffer.Buffer = NewBuffer;
            }

            DPRINT("Adding symbolic link %wZ\n", &KeyName);

            Status = RtlAppendUnicodeStringToString(&ReturnBuffer, &KeyName);
            if (!NT_SUCCESS(Status))
            {
                DPRINT("RtlAppendUnicodeStringToString() failed with status %X\n", Status);
                goto cleanup;
            }

            /* RtlAppendUnicodeStringToString added a NULL at the end of the destination string,
               but didn't increase the Length field. Do it for it.
            */
            ReturnBuffer.Length += sizeof(WCHAR);

NextReferenceString:

            ExFreePool(ReferenceBi);
            ReferenceBi = NULL;

            if (bip)
                ExFreePool(bip);
            bip = NULL;

            if (ReferenceKey)
            {
                ZwClose(ReferenceKey);
                ReferenceKey = NULL;
            }

            if (ControlKey)
            {
                ZwClose(ControlKey);
                ControlKey = NULL;
            }
        }

        if (FoundRightPDO)
            /* No need to go further, as we already have found what we searched */
            break;

        ExFreePool(DeviceBi);
        DeviceBi = NULL;

        ZwClose(DeviceKey);
        DeviceKey = NULL;
    }

    /* Add final NULL to ReturnBuffer */
    ASSERT(ReturnBuffer.Length <= ReturnBuffer.MaximumLength);

    if (ReturnBuffer.Length >= ReturnBuffer.MaximumLength)
    {
        PWSTR NewBuffer;
        ReturnBuffer.MaximumLength += sizeof(WCHAR);

        NewBuffer = ExAllocatePool(PagedPool, ReturnBuffer.MaximumLength);
        if (!NewBuffer)
        {
            DPRINT1("IoGetDeviceInterfaces: STATUS_INSUFFICIENT_RESOURCES\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto cleanup;
        }

        if (ReturnBuffer.Buffer)
        {
            RtlCopyMemory(NewBuffer, ReturnBuffer.Buffer, ReturnBuffer.Length);
            ExFreePool(ReturnBuffer.Buffer);
        }

        ReturnBuffer.Buffer = NewBuffer;
    }

    ReturnBuffer.Buffer[ReturnBuffer.Length / sizeof(WCHAR)] = UNICODE_NULL;
    *SymbolicLinkList = ReturnBuffer.Buffer;

    Status = STATUS_SUCCESS;

cleanup:

    if (!NT_SUCCESS(Status) && ReturnBuffer.Buffer)
        ExFreePool(ReturnBuffer.Buffer);

    if (InterfaceKey)
        ZwClose(InterfaceKey);

    if (DeviceKey)
        ZwClose(DeviceKey);

    if (ReferenceKey)
        ZwClose(ReferenceKey);

    if (ControlKey)
        ZwClose(ControlKey);

    if (DeviceBi)
        ExFreePool(DeviceBi);

    if (ReferenceBi)
        ExFreePool(ReferenceBi);

    if (bip)
        ExFreePool(bip);

    return Status;
}

/* Registers a device interface class, if it has not been previously registered, and creates a new instance of the interface class,
   which a driver can subsequently enable for use by applications or other system components.
   Documented in WDK.

   PhysicalDeviceObject
      Points to an optional PDO that narrows the search to only the device interfaces of the device represented by the PDO
   InterfaceClassGuid
      Points to a class GUID specifying the device interface class
   ReferenceString
      Optional parameter, pointing to a unicode string.
      For a full description of this rather rarely used param (usually drivers pass NULL here) see WDK
   SymbolicLinkName
      Pointer to the resulting unicode string

   Must be called at IRQL = PASSIVE_LEVEL in the context of a system thread
*/
NTSTATUS
NTAPI
IoRegisterDeviceInterface(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ CONST GUID* InterfaceClassGuid,
    _In_ PUNICODE_STRING ReferenceString OPTIONAL,
    _Out_ PUNICODE_STRING SymbolicLinkName)
{
    UCHAR PdoNameInfoBuffer[sizeof(OBJECT_NAME_INFORMATION) + (256 * sizeof(WCHAR))];
    POBJECT_NAME_INFORMATION PdoNameInfo = (POBJECT_NAME_INFORMATION)PdoNameInfoBuffer;
    UNICODE_STRING DeviceInstance = RTL_CONSTANT_STRING(L"DeviceInstance");
    UNICODE_STRING SymbolicLink = RTL_CONSTANT_STRING(L"SymbolicLink");
    UNICODE_STRING GuidString;
    UNICODE_STRING SubKeyName;
    UNICODE_STRING InterfaceKeyName;
    UNICODE_STRING BaseKeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PUNICODE_STRING InstancePath;
    PDEVICE_NODE DeviceNode;
    HANDLE ClassKey;
    HANDLE InterfaceKey;
    HANDLE SubKey;
    ULONG StartIndex;
    ULONG ix;
    NTSTATUS Status, SymLinkStatus;

    PAGED_CODE();

    Status = RtlStringFromGUID(InterfaceClassGuid, &GuidString);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RtlStringFromGUID() failed with status %X\n", Status);
        return Status;
    }

    DPRINT1("IoRegisterDeviceInterface: [%p], Guid '%wZ'\n", PhysicalDeviceObject,  &GuidString);

    if (ReferenceString && ReferenceString->Buffer)
    {
        DPRINT1("IoRegisterDeviceInterface(): RefString: %wZ\n", ReferenceString);
    }

    DeviceNode = IopGetDeviceNode(PhysicalDeviceObject);

    /* 1st level: Presence of a Device Node */
    if (!DeviceNode)
    {
        DPRINT("PDO %p doesn't have a DeviceNode\n", PhysicalDeviceObject);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (DeviceNode->Flags & DNF_LEGACY_RESOURCE_DEVICENODE)
    {
        DPRINT("DeviceNode %p have a DNF_LEGACY_RESOURCE_DEVICENODE\n", DeviceNode);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    /* 2nd level: Presence of an non-zero length InstancePath */
    if (!DeviceNode->InstancePath.Length)
    {
        DPRINT("PDO's %p DOE has zero-length InstancePath\n", PhysicalDeviceObject);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    /* 3rd level: Optional, based on WDK documentation */
    if (ReferenceString)
    {
        /* Reference string must not contain path-separator symbols */
        for (ix = 0; ix < ReferenceString->Length / sizeof(WCHAR); ix++)
        {
            if ((ReferenceString->Buffer[ix] == '\\') ||
                (ReferenceString->Buffer[ix] == '/'))
            {
                DPRINT("Reference string must not contain path-separator symbols\n");
                return STATUS_INVALID_DEVICE_REQUEST;
            }
        }
    }

    Status = RtlStringFromGUID(InterfaceClassGuid, &GuidString);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RtlStringFromGUID() failed with status %X\n", Status);
        return Status;
    }

    /* Create Pdo name: \Device\xxxxxxxx (unnamed device) */
    Status = ObQueryNameString(PhysicalDeviceObject, PdoNameInfo, sizeof(PdoNameInfoBuffer), &ix);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ObQueryNameString() failed with status %X\n", Status);
        return Status;
    }

    ASSERT(PdoNameInfo->Name.Length);

    /* Create base key name for this interface: HKLM\SYSTEM\CurrentControlSet\Control\DeviceClasses\{GUID} */
    ASSERT(((PEXTENDED_DEVOBJ_EXTENSION)PhysicalDeviceObject->DeviceObjectExtension)->DeviceNode);
    InstancePath = &((PEXTENDED_DEVOBJ_EXTENSION)PhysicalDeviceObject->DeviceObjectExtension)->DeviceNode->InstancePath;

    BaseKeyName.Length = ((USHORT)wcslen(BaseKeyString) * sizeof(WCHAR));
    BaseKeyName.MaximumLength = (BaseKeyName.Length + GuidString.Length);

    BaseKeyName.Buffer = ExAllocatePool(PagedPool, BaseKeyName.MaximumLength);
    if (!BaseKeyName.Buffer)
    {
        DPRINT1("IoRegisterDeviceInterface: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    wcscpy(BaseKeyName.Buffer, BaseKeyString);
    RtlAppendUnicodeStringToString(&BaseKeyName, &GuidString);

    /* Create BaseKeyName key in registry */
    InitializeObjectAttributes(&ObjectAttributes,
                               &BaseKeyName,
                               (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_OPENIF),
                               NULL,
                               NULL);

    Status = ZwCreateKey(&ClassKey, KEY_WRITE, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ZwCreateKey() failed with status %X\n", Status);
        ExFreePool(BaseKeyName.Buffer);
        return Status;
    }

    /* Create key name for this interface: ##?#ACPI#PNP0501#1#{GUID} */
    InterfaceKeyName.Length = 0;
    InterfaceKeyName.MaximumLength = (4 * sizeof(WCHAR))  + /* 4 = size of ##?# */
                                     InstancePath->Length +
                                     sizeof(WCHAR)        + /* 1 = size of # */
                                     GuidString.Length;

    InterfaceKeyName.Buffer = ExAllocatePool(PagedPool, InterfaceKeyName.MaximumLength);
    if (!InterfaceKeyName.Buffer)
    {
        DPRINT("ExAllocatePool() failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlAppendUnicodeToString(&InterfaceKeyName, L"##?#");
    StartIndex = InterfaceKeyName.Length / sizeof(WCHAR);
    RtlAppendUnicodeStringToString(&InterfaceKeyName, InstancePath);

    for (ix = 0; ix < InstancePath->Length / sizeof(WCHAR); ix++)
    {
        if (InterfaceKeyName.Buffer[StartIndex + ix] == '\\')
            InterfaceKeyName.Buffer[StartIndex + ix] = '#';
    }

    RtlAppendUnicodeToString(&InterfaceKeyName, L"#");
    RtlAppendUnicodeStringToString(&InterfaceKeyName, &GuidString);

    /* Create the interface key in registry */
    InitializeObjectAttributes(&ObjectAttributes,
                               &InterfaceKeyName,
                               (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_OPENIF),
                               ClassKey,
                               NULL);

    Status = ZwCreateKey(&InterfaceKey, KEY_WRITE, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ZwCreateKey() failed with status %X\n", Status);
        ZwClose(ClassKey);
        ExFreePool(BaseKeyName.Buffer);
        return Status;
    }

    /* Write DeviceInstance entry. Value is InstancePath */
    Status = ZwSetValueKey(InterfaceKey, &DeviceInstance, 0, REG_SZ, InstancePath->Buffer, InstancePath->Length);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ZwSetValueKey() failed with status %X\n", Status);

        ZwClose(InterfaceKey);
        ZwClose(ClassKey);

        ExFreePool(InterfaceKeyName.Buffer);
        ExFreePool(BaseKeyName.Buffer);

        return Status;
    }

    /* Create subkey. Name is #ReferenceString */
    SubKeyName.Length = 0;
    SubKeyName.MaximumLength = sizeof(WCHAR);

    if (ReferenceString && ReferenceString->Length)
        SubKeyName.MaximumLength += ReferenceString->Length;

    SubKeyName.Buffer = ExAllocatePool(PagedPool, SubKeyName.MaximumLength);
    if (!SubKeyName.Buffer)
    {
        DPRINT1("IoRegisterDeviceInterface: STATUS_INSUFFICIENT_RESOURCES\n");

        ZwClose(InterfaceKey);
        ZwClose(ClassKey);

        ExFreePool(InterfaceKeyName.Buffer);
        ExFreePool(BaseKeyName.Buffer);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlAppendUnicodeToString(&SubKeyName, L"#");

    if (ReferenceString && ReferenceString->Length)
        RtlAppendUnicodeStringToString(&SubKeyName, ReferenceString);

    /* Create SubKeyName key in registry */
    InitializeObjectAttributes(&ObjectAttributes,
                               &SubKeyName,
                               (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
                               InterfaceKey,
                               NULL);

    Status = ZwCreateKey(&SubKey, KEY_WRITE, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ZwCreateKey() failed with status %X\n", Status);

        ZwClose(InterfaceKey);
        ZwClose(ClassKey);

        ExFreePool(InterfaceKeyName.Buffer);
        ExFreePool(BaseKeyName.Buffer);

        return Status;
    }

    /* Create symbolic link name: \??\ACPI#PNP0501#1#{GUID}\ReferenceString */
    SymbolicLinkName->Length = 0;
    SymbolicLinkName->MaximumLength = SymbolicLinkName->Length +
                                      (4 * sizeof(WCHAR))      + /* size of \??\ */
                                      InstancePath->Length     +
                                      sizeof(WCHAR)            + /* size of # */
                                      GuidString.Length        +
                                      sizeof(WCHAR);             /* final NULL */

    if (ReferenceString && ReferenceString->Length)
        SymbolicLinkName->MaximumLength += (sizeof(WCHAR) + ReferenceString->Length);

    SymbolicLinkName->Buffer = ExAllocatePool(PagedPool, SymbolicLinkName->MaximumLength);
    if (!SymbolicLinkName->Buffer)
    {
        DPRINT1("IoRegisterDeviceInterface: STATUS_INSUFFICIENT_RESOURCES\n");

        ZwClose(SubKey);
        ZwClose(InterfaceKey);
        ZwClose(ClassKey);

        ExFreePool(InterfaceKeyName.Buffer);
        ExFreePool(SubKeyName.Buffer);
        ExFreePool(BaseKeyName.Buffer);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlAppendUnicodeToString(SymbolicLinkName, L"\\??\\");
    StartIndex = SymbolicLinkName->Length / sizeof(WCHAR);
    RtlAppendUnicodeStringToString(SymbolicLinkName, InstancePath);

    for (ix = 0; ix < InstancePath->Length / sizeof(WCHAR); ix++)
    {
        if (SymbolicLinkName->Buffer[StartIndex + ix] == '\\')
            SymbolicLinkName->Buffer[StartIndex + ix] = '#';
    }

    RtlAppendUnicodeToString(SymbolicLinkName, L"#");
    RtlAppendUnicodeStringToString(SymbolicLinkName, &GuidString);
    SymbolicLinkName->Buffer[SymbolicLinkName->Length / sizeof(WCHAR)] = UNICODE_NULL;

    /* Create symbolic link */
    DPRINT("IoRegisterDeviceInterface(): creating symbolic link %wZ -> %wZ\n", SymbolicLinkName, &PdoNameInfo->Name);
    SymLinkStatus = IoCreateSymbolicLink(SymbolicLinkName, &PdoNameInfo->Name);

    /* If the symbolic link already exists, return an informational success status */
    if (SymLinkStatus == STATUS_OBJECT_NAME_COLLISION)
    {
        /* HACK: Delete the existing symbolic link and update it to the new PDO name */
        IoDeleteSymbolicLink(SymbolicLinkName);
        IoCreateSymbolicLink(SymbolicLinkName, &PdoNameInfo->Name);

        SymLinkStatus = STATUS_OBJECT_NAME_EXISTS;
    }

    if (!NT_SUCCESS(SymLinkStatus))
    {
        DPRINT1("IoCreateSymbolicLink() failed with status %X\n", SymLinkStatus);

        ZwClose(SubKey);
        ZwClose(InterfaceKey);
        ZwClose(ClassKey);

        ExFreePool(SubKeyName.Buffer);
        ExFreePool(InterfaceKeyName.Buffer);
        ExFreePool(BaseKeyName.Buffer);
        ExFreePool(SymbolicLinkName->Buffer);

        return SymLinkStatus;
    }

    if (ReferenceString && ReferenceString->Length)
    {
        RtlAppendUnicodeToString(SymbolicLinkName, L"\\");
        RtlAppendUnicodeStringToString(SymbolicLinkName, ReferenceString);
    }

    SymbolicLinkName->Buffer[SymbolicLinkName->Length / sizeof(WCHAR)] = UNICODE_NULL;

    /* Write symbolic link name in registry */
    SymbolicLinkName->Buffer[1] = '\\';

    Status = ZwSetValueKey(SubKey, &SymbolicLink, 0, REG_SZ, SymbolicLinkName->Buffer, SymbolicLinkName->Length);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ZwSetValueKey() failed with status %X\n", Status);
        ExFreePool(SymbolicLinkName->Buffer);
    }
    else
    {
        SymbolicLinkName->Buffer[1] = '?';
    }

    ZwClose(SubKey);
    ZwClose(InterfaceKey);
    ZwClose(ClassKey);

    ExFreePool(SubKeyName.Buffer);
    ExFreePool(InterfaceKeyName.Buffer);
    ExFreePool(BaseKeyName.Buffer);

    return (NT_SUCCESS(Status) ? SymLinkStatus : Status);
}

NTSTATUS
NTAPI
IopParseSymbolicLinkName(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _Out_ PUNICODE_STRING OutPrefixName,
    _Out_ PUNICODE_STRING OutEnumName,
    _Out_ PUNICODE_STRING OutGuidName,
    _Out_ PUNICODE_STRING OutRefName,
    _Out_ PBOOLEAN OutIsRefString,
    _Out_ GUID* OutGuid)
{
    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING GuidString;
    GUID Guid;
    PWCHAR Ptr;
    ULONG ix;
    USHORT Count;
    USHORT ReferenceStart = 0;
    BOOLEAN IsRefString;

    PAGED_CODE();
    DPRINT("IopParseSymbolicLinkName: SymbolicLink '%wZ'\n", SymbolicLinkName);

    if (!SymbolicLinkName)
    {
        DPRINT1("IopParseSymbolicLinkName: STATUS_INVALID_PARAMETER\n");
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return STATUS_INVALID_PARAMETER;
    }

    if (!SymbolicLinkName->Buffer)
    {
        DPRINT1("IopParseSymbolicLinkName: STATUS_INVALID_PARAMETER\n");
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return STATUS_INVALID_PARAMETER;
    }

    if (!SymbolicLinkName->Length)
    {
        DPRINT1("IopParseSymbolicLinkName: STATUS_INVALID_PARAMETER\n");
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return STATUS_INVALID_PARAMETER;
    }

    if (SymbolicLinkName->Length < 0x55)
    {
        DPRINT1("IopParseSymbolicLinkName: SymbolicLinkName->Length %X\n", SymbolicLinkName->Length);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return STATUS_INVALID_PARAMETER;
    }

    if (RtlCompareMemory(SymbolicLinkName->Buffer, L"\\\\?\\", (4 * sizeof(WCHAR))) != (4 * sizeof(WCHAR)) &&
        RtlCompareMemory(SymbolicLinkName->Buffer, L"\\??\\", (4 * sizeof(WCHAR))) != (4 * sizeof(WCHAR)))
    {
        DPRINT1("IopParseSymbolicLinkName: STATUS_INVALID_PARAMETER\n");
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return STATUS_INVALID_PARAMETER;
    }

    Ptr = &SymbolicLinkName->Buffer[4]; // skip "\??\"
    Count = (SymbolicLinkName->Length / sizeof(WCHAR));

    for (ix = 0; ix < (Count - 4); ix++, Ptr++)
    {
        if(*Ptr == L'\\')
        {
            DPRINT("IopParseSymbolicLinkName: ReferenceStart %X, Link '%wZ'\n", ReferenceStart, SymbolicLinkName);
            ReferenceStart = (ix + 5);
            break;
        }
    }

    if (ReferenceStart)
    {
        IsRefString = TRUE;
    }
    else
    {
        ReferenceStart = (Count + 1); // to end string
        IsRefString = FALSE;
    }

    GuidString.Length = 0x4C;
    GuidString.MaximumLength = 0x4C;
    GuidString.Buffer = &SymbolicLinkName->Buffer[ReferenceStart - 0x27];

    Status = RtlGUIDFromString(&GuidString, &Guid);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopParseSymbolicLinkName: RtlGUIDFromString() failed for GUID '%wZ' in '%wZ'\n", &GuidString, SymbolicLinkName);
        DPRINT1("IopParseSymbolicLinkName: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return STATUS_INVALID_PARAMETER;
    }

    if (OutPrefixName)
    {
        OutPrefixName->Length = 8;
        OutPrefixName->MaximumLength = OutPrefixName->Length;
        OutPrefixName->Buffer = SymbolicLinkName->Buffer;

        DPRINT1("IopParseSymbolicLinkName: OutPrefix '%wZ'\n", OutPrefixName);
    }

    if (OutEnumName)
    {
        OutEnumName->Length = (ReferenceStart * sizeof(WCHAR) - 0x58);
        OutEnumName->MaximumLength = OutEnumName->Length;
        OutEnumName->Buffer = &SymbolicLinkName->Buffer[4];

        DPRINT1("IopParseSymbolicLinkName: OutEnum '%wZ'\n", OutEnumName);
    }

    if (OutGuidName)
    {
        OutGuidName->Length = 0x4C;
        OutGuidName->MaximumLength = OutGuidName->Length;
        OutGuidName->Buffer = &SymbolicLinkName->Buffer[ReferenceStart - 0x27];

        DPRINT1("IopParseSymbolicLinkName: OutGuid '%wZ'\n", OutGuidName);
    }

    if (OutRefName)
    {
        if (IsRefString)
        {
            OutRefName->Length = (SymbolicLinkName->Length - ReferenceStart * sizeof(WCHAR));
            OutRefName->MaximumLength = OutRefName->Length;
            OutRefName->Buffer = &SymbolicLinkName->Buffer[ReferenceStart];

            DPRINT1("IopParseSymbolicLinkName: OutRef '%wZ'\n", OutRefName);
        }
        else
        {
            OutRefName->Length = 0;
            OutRefName->MaximumLength = OutRefName->Length;
            OutRefName->Buffer = NULL;

            DPRINT1("IopParseSymbolicLinkName: OutRef is ''\n");
        }
    }

    if (OutIsRefString)
        *OutIsRefString = IsRefString;

    if (OutGuid)
        RtlCopyMemory(OutGuid, &Guid, sizeof(*OutGuid));

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopDropReferenceString(
    _Out_ PUNICODE_STRING OutString,
    _In_ PUNICODE_STRING InString)
{
    UNICODE_STRING RefName;
    BOOLEAN IsRefString;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(InString);
    ASSERT(OutString);

    Status = IopParseSymbolicLinkName(InString, NULL, NULL, NULL, &RefName, &IsRefString, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopDropReferenceString: Status %X\n", Status);
        RtlZeroMemory(&OutString, sizeof(OutString));
        return Status;
    }

    if (IsRefString)
        OutString->Length = (InString->Length - RefName.Length - sizeof(WCHAR));
    else
        OutString->Length = InString->Length;

    OutString->MaximumLength = OutString->Length;
    OutString->Buffer = InString->Buffer;

    DPRINT1("IopDropReferenceString: In  '%wZ'\n", InString);
    DPRINT1("IopDropReferenceString: Out '%wZ'\n", OutString);

    return Status;
}

NTSTATUS
NTAPI
IopBuildGlobalSymbolicLinkString(
    _In_ PUNICODE_STRING InString,
    _Out_ PUNICODE_STRING OutGlobalString)
{
    UNICODE_STRING Source;
    NTSTATUS Status;
    USHORT length;

    PAGED_CODE();
    DPRINT("IopDropReferenceString: InString '%wZ'\n", InString);

    if (RtlCompareMemory(InString->Buffer, L"\\\\?\\", (4 * sizeof(WCHAR))) != (4 * sizeof(WCHAR)) &&
        RtlCompareMemory(InString->Buffer, L"\\??\\", (4 * sizeof(WCHAR))) != (4 * sizeof(WCHAR)))
    {
        DPRINT1("IopDropReferenceString: STATUS_INVALID_PARAMETER\n");
        return STATUS_INVALID_PARAMETER;
    }

    length = (InString->Length + (6 * sizeof(WCHAR)));

    Status = PnpAllocateUnicodeString(OutGlobalString, length);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopDropReferenceString: Status %X\n", Status);
        return Status;
    }

    Status = RtlAppendUnicodeToString(OutGlobalString, L"\\GLOBAL??\\");
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopDropReferenceString: Status %X\n", Status);
        ASSERT(NT_SUCCESS(Status));
        RtlFreeUnicodeString(OutGlobalString);
        return Status;
    }

    Source.Length = (InString->Length - (4 * sizeof(WCHAR)));
    Source.MaximumLength = (InString->MaximumLength - (4 * sizeof(WCHAR)));
    Source.Buffer = &InString->Buffer[4];

    Status = RtlAppendUnicodeStringToString(OutGlobalString, &Source);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopDropReferenceString: Status %X\n", Status);
        ASSERT(NT_SUCCESS(Status));
        RtlFreeUnicodeString(OutGlobalString);
        return Status;
    }

    ASSERT(OutGlobalString->Length == length);

    return Status;
}

NTSTATUS
NTAPI
IopReplaceSeperatorWithPound(
    _In_ PUNICODE_STRING InString,
    _Out_ PUNICODE_STRING OutString)
{
    PWSTR InChar;
    PWSTR OutChar;
    ULONG ix;
    USHORT InStringLen;
    WCHAR Char;

    PAGED_CODE();
    //DPRINT("IopReplaceSeperatorWithPound: InString '%wZ'\n", InString);

    ASSERT(InString);
    ASSERT(OutString);

    if (InString->Length > OutString->MaximumLength)
    {
        DPRINT1("IopReplaceSeperatorWithPound: STATUS_BUFFER_TOO_SMALL. In %X, Out %X\n", InString->Length, OutString->MaximumLength);
        return STATUS_BUFFER_TOO_SMALL;
    }

    InChar = InString->Buffer;
    OutChar = OutString->Buffer;

    InStringLen = InString->Length / sizeof(WCHAR);

    for (ix = 0; ix < InStringLen; ix++, InChar++, OutChar++)
    {
        Char = *InChar;

        if (*InChar == '\\' || Char == '/')
            *OutChar = '#';
        else
            *OutChar = Char;
    }

    OutString->Length = InString->Length;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopOpenOrCreateDeviceInterfaceSubKeys(
    _Out_ PHANDLE OutInterfaceHandle,
    _Out_ PULONG OutInterfaceDisposition,
    _Out_ PHANDLE OutInterfaceInstanceHandle,
    _Out_ PULONG OutInterfaceInstanceDisposition,
    _In_ HANDLE InterfaceClassHandle,
    _In_ PUNICODE_STRING InterfaceName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN IsCreateOrOpen)
{
    UNICODE_STRING DestinationString;
    UNICODE_STRING RefName;
    HANDLE ParentHandle;
    HANDLE KeyHandle;
    ULONG Disposition;
    WCHAR CharBuffer;
    BOOLEAN IsRefString = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("IopOpenOrCreateDeviceInterfaceSubKeys: Interface %wZ, IsCreateOrOpen %X\n", InterfaceName, IsCreateOrOpen);

    Status = PnpAllocateUnicodeString(&DestinationString, InterfaceName->Length);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopOpenOrCreateDeviceInterfaceSubKeys: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return Status;
    }

    RtlCopyUnicodeString(&DestinationString, InterfaceName);
    DPRINT("IopOpenOrCreateDeviceInterfaceSubKeys: Destination '%wZ'\n", &DestinationString);

    Status = IopParseSymbolicLinkName(&DestinationString, NULL, NULL, NULL, &RefName, &IsRefString, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopOpenOrCreateDeviceInterfaceSubKeys: Status %X\n", Status);
        ASSERT(NT_SUCCESS(Status));
        RtlFreeUnicodeString(&DestinationString);
        return Status;
    }

    if (IsRefString)
    {
        RefName.Length += sizeof(WCHAR);
        RefName.MaximumLength += sizeof(WCHAR);
        RefName.Buffer--;

        DPRINT1("IopOpenOrCreateDeviceInterfaceSubKeys: IsRefString\n");

        DestinationString.Length = (USHORT)((ULONG_PTR)RefName.Buffer - (ULONG_PTR)DestinationString.Buffer);
        DestinationString.MaximumLength = DestinationString.Length;
    }
    else
    {
        RefName.Length = sizeof(CharBuffer);
        RefName.MaximumLength = RefName.Length;
        RefName.Buffer = &CharBuffer;
    }

    DestinationString.Buffer[0] = '#';
    DestinationString.Buffer[1] = '#';
    DestinationString.Buffer[2] = '?';
    DestinationString.Buffer[3] = '#';

    IopReplaceSeperatorWithPound(&DestinationString, &DestinationString);

    if (IsCreateOrOpen)
    {
        Status = IopCreateRegistryKeyEx(&ParentHandle,
                                        InterfaceClassHandle,
                                        &DestinationString,
                                        DesiredAccess,
                                        REG_OPTION_NON_VOLATILE,
                                        &Disposition);
    }
    else
    {
        Status = IopOpenRegistryKeyEx(&ParentHandle, InterfaceClassHandle, &DestinationString, DesiredAccess);
        Disposition = 2;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopOpenOrCreateDeviceInterfaceSubKeys: [%X] Iface '%wZ'\n", IsCreateOrOpen, InterfaceName);
        DPRINT1("IopOpenOrCreateDeviceInterfaceSubKeys: Status %X for '%wZ'\n", Status, &DestinationString);
        goto Exit;
    }

    RefName.Buffer[0] = '#';

    if (IsCreateOrOpen)
    {
         Status = IopCreateRegistryKeyEx(&KeyHandle,
                                         ParentHandle,
                                         &RefName,
                                         DesiredAccess,
                                         REG_OPTION_NON_VOLATILE,
                                         OutInterfaceInstanceDisposition);
    }
    else
    {
        Status = IopOpenRegistryKeyEx(&KeyHandle, ParentHandle, &RefName, DesiredAccess);
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopOpenOrCreateDeviceInterfaceSubKeys: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();

        if (Disposition == 1)
            ZwDeleteKey(ParentHandle);

        ZwClose(ParentHandle);
        goto Exit;
    }

    if (OutInterfaceHandle)
        *OutInterfaceHandle = ParentHandle;
    else
        ZwClose(ParentHandle);

    if (OutInterfaceDisposition)
        *OutInterfaceDisposition = Disposition;

    if (OutInterfaceInstanceHandle)
        *OutInterfaceInstanceHandle = KeyHandle;
    else
        ZwClose(KeyHandle);

Exit:
    RtlFreeUnicodeString(&DestinationString);
    return Status;
}

NTSTATUS
NTAPI
IopDeviceInterfaceKeysFromSymbolicLink(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE OutInterfaceClassHandle,
    _Out_ PHANDLE OutInterfaceHandle,
    _Out_ PHANDLE OutInterfaceInstanceHandle)
{
    UNICODE_STRING DeviceClassesName = RTL_CONSTANT_STRING(IO_REG_KEY_DEVICECLASSES);
    UNICODE_STRING GuidName;
    HANDLE InterfaceClassHandle;
    HANDLE KeyHandle;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("IopDeviceInterfaceKeysFromSymbolicLink: SymbolicLink '%wZ'\n", SymbolicLinkName);

    Status = IopParseSymbolicLinkName(SymbolicLinkName, NULL, NULL, &GuidName, NULL, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopDeviceInterfaceKeysFromSymbolicLink: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        return Status;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PpRegistryDeviceResource, TRUE);

    Status = IopOpenRegistryKeyEx(&KeyHandle, NULL, &DeviceClassesName, KEY_READ);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopDeviceInterfaceKeysFromSymbolicLink: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit;
    }

    Status = IopOpenRegistryKeyEx(&InterfaceClassHandle, KeyHandle, &GuidName, KEY_READ);
    ZwClose(KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopDeviceInterfaceKeysFromSymbolicLink: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit;
    }

    Status = IopOpenOrCreateDeviceInterfaceSubKeys(OutInterfaceHandle,
                                                   NULL,
                                                   OutInterfaceInstanceHandle,
                                                   NULL,
                                                   InterfaceClassHandle,
                                                   SymbolicLinkName,
                                                   DesiredAccess,
                                                   FALSE);

    if (NT_SUCCESS(Status) && OutInterfaceClassHandle)
        *OutInterfaceClassHandle = InterfaceClassHandle;
    else
        ZwClose(InterfaceClassHandle);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopDeviceInterfaceKeysFromSymbolicLink: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
    }

Exit:
    ExReleaseResourceLite(&PpRegistryDeviceResource);
    KeLeaveCriticalRegion();
    return Status;
}

NTSTATUS
NTAPI
PiDeferSetInterfaceState(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    PDEVNODE_INTERFACE_STATE State;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("PiDeferSetInterfaceState: [%p] SymbolicLink '%wZ'\n", DeviceNode, SymbolicLinkName);

    ASSERT(ExIsResourceAcquiredExclusiveLite(&PpRegistryDeviceResource));
    //ASSERT(PiIsPnpRegistryLocked(TRUE));

    State = ExAllocatePoolWithTag(PagedPool, sizeof(*State), '  pP');
    if (!State)
    {
        DPRINT1("PiDeferSetInterfaceState: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = PnpAllocateUnicodeString(&State->SymbolicLinkName, SymbolicLinkName->Length);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PiDeferSetInterfaceState: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(State, '  pP');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyUnicodeString(&State->SymbolicLinkName, SymbolicLinkName);

    InsertTailList(&DeviceNode->PendedSetInterfaceState, &State->Link);

    return STATUS_SUCCESS;
}

VOID 
NTAPI
PiRemoveDeferredSetInterfaceState(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PCUNICODE_STRING SymbolicLinkName)
{
    PDEVNODE_INTERFACE_STATE State;
    PLIST_ENTRY Entry;

    PAGED_CODE();
    DPRINT("PiRemoveDeferredSetInterfaceState: [%p] SymbolicLink %wZ\n", DeviceNode, SymbolicLinkName);

    ASSERT(ExIsResourceAcquiredExclusiveLite(&PpRegistryDeviceResource));
    //ASSERT(PiIsPnpRegistryLocked(TRUE));

    for (Entry = DeviceNode->PendedSetInterfaceState.Flink;
         Entry != &DeviceNode->PendedSetInterfaceState;
         Entry = Entry->Flink)
    {
        State = CONTAINING_RECORD(Entry, DEVNODE_INTERFACE_STATE, Link);

        if (RtlEqualUnicodeString(&State->SymbolicLinkName, SymbolicLinkName, TRUE))
        {
            RemoveEntryList(&State->Link);

            ExFreePoolWithTag(State->SymbolicLinkName.Buffer, '  pP');
            ExFreePoolWithTag(State, '  pP');

            break;
        }
    }

    return;
}

NTSTATUS
NTAPI
IopProcessSetInterfaceState(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ BOOLEAN Enable,
    _In_ BOOLEAN PdoNotStarted)
{
    UNICODE_STRING ReferenceCountName = RTL_CONSTANT_STRING(L"ReferenceCount");
    UNICODE_STRING ControlName = RTL_CONSTANT_STRING(L"Control");
    UNICODE_STRING LinkedName = RTL_CONSTANT_STRING(L"Linked");
    UNICODE_STRING MinusReferenceString;
    UNICODE_STRING GlobalSymbolicLink;
    UNICODE_STRING DestinationString;
    UNICODE_STRING ValueName;
    PEXTENDED_DEVOBJ_EXTENSION DeviceObjectExtension;
    PKEY_VALUE_FULL_INFORMATION ValueInfo = NULL;
    HANDLE InterfaceInstanceHandle = NULL;
    HANDLE InstanceControlHandle = NULL;
    HANDLE InterfaceClassHandle = NULL;
    HANDLE ParentControlHandle = NULL;
    HANDLE InterfaceHandle = NULL;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_NODE DeviceNode;
    PVOID PropertyBuffer = NULL;
    GUID DeviceGuid;
    ULONG ReferenceCount;
    ULONG Linked;
    USHORT Length;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("IopProcessSetInterfaceState: SymbolicLink '%wZ', Enable %X\n", SymbolicLinkName, Enable);

    Status = IopParseSymbolicLinkName(SymbolicLinkName, NULL, NULL, NULL, NULL, NULL, &DeviceGuid);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit;
    }

    Status = IopDropReferenceString(&MinusReferenceString, SymbolicLinkName);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit;
    }

    Status = IopBuildGlobalSymbolicLinkString(&MinusReferenceString, &GlobalSymbolicLink);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit;
    }

    Status = IopDeviceInterfaceKeysFromSymbolicLink(SymbolicLinkName,
                                                    (KEY_READ | KEY_WRITE),
                                                    &InterfaceClassHandle,
                                                    &InterfaceHandle,
                                                    &InterfaceInstanceHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit1;
    }

    Status = IopCreateRegistryKeyEx(&ParentControlHandle, InterfaceHandle, &ControlName, KEY_READ, REG_OPTION_VOLATILE, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit1;
    }

    Status = IopGetRegistryValue(InterfaceHandle, L"DeviceInstance", &ValueInfo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit2;
    }

    Status = IopCreateRegistryKeyEx(&InstanceControlHandle, InterfaceInstanceHandle, &ControlName, KEY_READ, REG_OPTION_VOLATILE, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        ExFreePool(ValueInfo);
        InstanceControlHandle = NULL;
        goto Exit2;
    }

    if (ValueInfo->Type != REG_SZ)
    {
        DPRINT1("IopProcessSetInterfaceState: ValueInfo->Type %X\n", ValueInfo->Type);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    else
    {
        PnpRegSzToString((PWCHAR)((ULONG_PTR)ValueInfo + ValueInfo->DataOffset), ValueInfo->DataLength, &Length);

        ValueName.Length = Length;
        ValueName.MaximumLength = (USHORT)ValueInfo->DataLength;
        ValueName.Buffer = (PWCHAR)((ULONG_PTR)ValueInfo + ValueInfo->DataOffset);

        DeviceObject = IopDeviceObjectFromDeviceInstance(&ValueName);
        if (!DeviceObject)
        {
            DPRINT1("IopProcessSetInterfaceState: Value '%wZ'\n", &ValueName);
            ASSERT(FALSE); // IoDbgBreakPointEx();
            Status = STATUS_INVALID_DEVICE_REQUEST;
        }
        else
        {
            DPRINT("IopProcessSetInterfaceState: Value '%wZ'\n", &ValueName);

            if (PdoNotStarted)
            {
                DeviceObjectExtension = IoGetDevObjExtension(DeviceObject);

                if (DeviceObjectExtension->ExtensionFlags & DOE_START_PENDING)
                {
                    DeviceNode = DeviceObjectExtension->DeviceNode;

                    if (Enable)
                    {
                        Status = PiDeferSetInterfaceState(DeviceNode, SymbolicLinkName);
                        if (NT_SUCCESS(Status))
                        {
                            ExFreePool(ValueInfo);
                            ObDereferenceObject(DeviceObject);
                            Status = STATUS_SUCCESS;
                            goto Exit2;
                        }
                    }
                    else
                    {
                        PiRemoveDeferredSetInterfaceState(DeviceNode, SymbolicLinkName);
                        ExFreePool(ValueInfo);
                        ObDereferenceObject(DeviceObject);
                        Status = STATUS_SUCCESS;
                        goto Exit2;
                    }
                }
            }

            if (!Enable || !NT_SUCCESS(Status))
                ObDereferenceObject(DeviceObject);
        }
    }

    if (!Enable)
        Status = STATUS_SUCCESS;

    ExFreePool(ValueInfo);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit2;
    }

    if (Enable)
    {
        ULONG BufferLength = 0x200;
        ULONG size;

        for (size = 0x200; ; size = BufferLength)
        {
            PropertyBuffer = ExAllocatePoolWithTag(PagedPool, size, '  pP');
            if (!PropertyBuffer)
            {
                DPRINT1("IopProcessSetInterfaceState: STATUS_INSUFFICIENT_RESOURCES\n");
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            Status = IoGetDeviceProperty(DeviceObject,
                                         DevicePropertyPhysicalDeviceObjectName,
                                         BufferLength,
                                         PropertyBuffer,
                                         &BufferLength);
            if (NT_SUCCESS(Status))
                break;

            ExFreePoolWithTag(PropertyBuffer, '  pP');

            if (Status != STATUS_BUFFER_TOO_SMALL)
                break;
        }

        ObDereferenceObject(DeviceObject);

        if (!NT_SUCCESS(Status) || !BufferLength)
        {
            DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
            ASSERT(FALSE); // IoDbgBreakPointEx();
            goto Exit2;
        }

        RtlInitUnicodeString(&DestinationString, PropertyBuffer);
    }

    ValueInfo = NULL;

    Status = IopGetRegistryValue(InstanceControlHandle, L"Linked", &ValueInfo);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        Linked = 0;
    }
    else if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
        ASSERT(FALSE); // IoDbgBreakPointEx();
        goto Exit3;
    }
    else if ((ValueInfo->Type != REG_DWORD) || (ValueInfo->DataLength != sizeof(ULONG)))
    {
        Linked = 0;
    }
    else
    {
        Linked = *(PULONG)((ULONG_PTR)ValueInfo + ValueInfo->DataOffset);
    }

    if (ValueInfo)
        ExFreePool(ValueInfo);

    Status= IopGetRegistryValue(ParentControlHandle, L"ReferenceCount", &ValueInfo);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        ReferenceCount = 0;
    }
    else
    {
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IopProcessSetInterfaceState: Status %X\n", Status);
            ASSERT(FALSE); // IoDbgBreakPointEx();
            goto Exit3;
        }

        if ((ValueInfo->Type != REG_DWORD) || (ValueInfo->DataLength != sizeof(ULONG)))
            ReferenceCount = 0;
        else
            ReferenceCount = *(PULONG)((ULONG_PTR)ValueInfo + ValueInfo->DataOffset);

        ExFreePool(ValueInfo);
    }

    if (!Enable)
    {
        if (Linked)
        {
            if (ReferenceCount > 1)
            {
                ReferenceCount--;
            }
            else
            {
                ReferenceCount = 0;

                Status = IoDeleteSymbolicLink(&GlobalSymbolicLink);
                if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
                {
                    DPRINT("IopProcessSetInterfaceState: STATUS_OBJECT_NAME_NOT_FOUND for '%ws' to delete!\n", GlobalSymbolicLink.Buffer);
                    Status = STATUS_SUCCESS;
                }
            }

            Linked = 0;
        }
        else
        {
            DPRINT("IopProcessSetInterfaceState: STATUS_OBJECT_NAME_NOT_FOUND\n");
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
    }
    else
    {
        if (Linked)
        {
            Status = STATUS_OBJECT_NAME_EXISTS;
            goto Exit3;
        }

        if (ReferenceCount > 0)
        {
            ReferenceCount++;
        }
        else
        {
            ReferenceCount = 1;

            Status = IoCreateSymbolicLink(&GlobalSymbolicLink, &DestinationString);
            if (Status == STATUS_OBJECT_NAME_COLLISION)
            {
                DPRINT("IopProcessSetInterfaceState: STATUS_OBJECT_NAME_COLLISION. '%ws' already exists!\n", GlobalSymbolicLink.Buffer);
                Status = STATUS_SUCCESS;
            }
        }

        Linked = 1;
    }

    if (!NT_SUCCESS(Status))
        goto Exit3;

    ZwSetValueKey(InstanceControlHandle, &LinkedName, 0, REG_DWORD, &Linked, sizeof(Linked));
    Status = ZwSetValueKey(ParentControlHandle, &ReferenceCountName, 0, REG_DWORD, &ReferenceCount, sizeof(ReferenceCount));

    if (Linked)
        PpSetDeviceClassChange(&GUID_DEVICE_INTERFACE_ARRIVAL, &DeviceGuid, SymbolicLinkName);
    else
        PpSetDeviceClassChange(&GUID_DEVICE_INTERFACE_REMOVAL, &DeviceGuid, SymbolicLinkName);

Exit3:
    if (PropertyBuffer)
        ExFreePoolWithTag(PropertyBuffer, '  pP');

Exit2:
    if (ParentControlHandle)
        ZwClose(ParentControlHandle);

    if (InstanceControlHandle)
        ZwClose(InstanceControlHandle);

Exit1:
    RtlFreeUnicodeString(&GlobalSymbolicLink);

    if (InterfaceHandle)
        ZwClose(InterfaceHandle);

    if (InterfaceInstanceHandle)
        ZwClose(InterfaceInstanceHandle);

    if (InterfaceClassHandle)
        ZwClose(InterfaceClassHandle);

    if (NT_SUCCESS(Status))
        return Status;

Exit:
    if (!Enable)
        Status = STATUS_SUCCESS;

    return Status;
}

/* Enables or disables an instance of a previously registered device interface class.
   Documented in WDK.

   SymbolicLinkName
      Pointer to the string identifying instance to enable or disable
   Enable
      TRUE = enable, FALSE = disable

   remarks: Must be called at IRQL = PASSIVE_LEVEL in the context of a system thread
*/
NTSTATUS
NTAPI
IoSetDeviceInterfaceState(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ BOOLEAN Enable)
{
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT1("IoSetDeviceInterfaceState: SymbolicLink '%wZ', IsEnable %X\n", SymbolicLinkName, Enable);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PpRegistryDeviceResource, TRUE);

    Status = IopProcessSetInterfaceState(SymbolicLinkName, Enable, TRUE);

    ExReleaseResourceLite(&PpRegistryDeviceResource);
    KeLeaveCriticalRegion();

    if (!NT_SUCCESS(Status) && !Enable)
        Status = STATUS_SUCCESS;

    return Status;
}

VOID
NTAPI
IopDoDeferredSetInterfaceState(
    _In_ PDEVICE_NODE DeviceNode)
{
    PLIST_ENTRY Entry;
    PDEVNODE_INTERFACE_STATE State;

    PAGED_CODE();
    DPRINT("IopDoDeferredSetInterfaceState: DeviceNode - %p\n", DeviceNode);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&PpRegistryDeviceResource, TRUE);

    PpMarkDeviceStackStartPending(DeviceNode->PhysicalDeviceObject, 0);

    DPRINT("IopDoDeferredSetInterfaceState: &DeviceNode->PendedSetInterfaceState %p\n", &DeviceNode->PendedSetInterfaceState);

    for (Entry = &DeviceNode->PendedSetInterfaceState;
         !IsListEmpty(&DeviceNode->PendedSetInterfaceState);
        )
    {
        State = CONTAINING_RECORD(Entry->Flink, DEVNODE_INTERFACE_STATE, Link);
        RemoveHeadList(Entry);

        IopProcessSetInterfaceState(&State->SymbolicLinkName, TRUE, FALSE);

        DPRINT("IopDoDeferredSetInterfaceState: SymbolicLinkName - %wZ\n", &State->SymbolicLinkName);

        ExFreePoolWithTag(State->SymbolicLinkName.Buffer, 0);
        ExFreePoolWithTag(State, 0);
    }

    ExReleaseResourceLite(&PpRegistryDeviceResource);
    KeLeaveCriticalRegion();
}

/* EOF */
