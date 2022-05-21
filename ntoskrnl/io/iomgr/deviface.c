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

/*++
 * @name IoSetDeviceInterfaceState
 * @implemented
 *
 * Enables or disables an instance of a previously registered device
 * interface class.
 * Documented in WDK.
 *
 * @param SymbolicLinkName
 *        Pointer to the string identifying instance to enable or disable
 *
 * @param Enable
 *        TRUE = enable, FALSE = disable
 *
 * @return Usual NTSTATUS
 *
 * @remarks Must be called at IRQL = PASSIVE_LEVEL in the context of a
 *          system thread
 *
 *--*/
NTSTATUS
NTAPI
IoSetDeviceInterfaceState(IN PUNICODE_STRING SymbolicLinkName,
                          IN BOOLEAN Enable)
{
    PDEVICE_OBJECT PhysicalDeviceObject;
    UNICODE_STRING GuidString;
    NTSTATUS Status;
    LPCGUID EventGuid;
    HANDLE InstanceHandle, ControlHandle;
    UNICODE_STRING KeyName, DeviceInstance;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG LinkedValue, Index;
    GUID DeviceGuid;
    UNICODE_STRING DosDevicesPrefix1 = RTL_CONSTANT_STRING(L"\\??\\");
    UNICODE_STRING DosDevicesPrefix2 = RTL_CONSTANT_STRING(L"\\\\?\\");
    UNICODE_STRING LinkNameNoPrefix;
    USHORT i;
    USHORT ReferenceStringOffset;

    if (SymbolicLinkName == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("IoSetDeviceInterfaceState('%wZ', %u)\n", SymbolicLinkName, Enable);

    /* Symbolic link name is \??\ACPI#PNP0501#1#{GUID}\ReferenceString */
    /* Make sure it starts with the expected prefix */
    if (!RtlPrefixUnicodeString(&DosDevicesPrefix1, SymbolicLinkName, FALSE) &&
        !RtlPrefixUnicodeString(&DosDevicesPrefix2, SymbolicLinkName, FALSE))
    {
        DPRINT1("IoSetDeviceInterfaceState() invalid link name '%wZ'\n", SymbolicLinkName);
        return STATUS_INVALID_PARAMETER;
    }

    /* Make a version without the prefix for further processing */
    ASSERT(DosDevicesPrefix1.Length == DosDevicesPrefix2.Length);
    ASSERT(SymbolicLinkName->Length >= DosDevicesPrefix1.Length);
    LinkNameNoPrefix.Buffer = SymbolicLinkName->Buffer + DosDevicesPrefix1.Length / sizeof(WCHAR);
    LinkNameNoPrefix.Length = SymbolicLinkName->Length - DosDevicesPrefix1.Length;
    LinkNameNoPrefix.MaximumLength = LinkNameNoPrefix.Length;

    /* Find the reference string, if any */
    for (i = 0; i < LinkNameNoPrefix.Length / sizeof(WCHAR); i++)
    {
        if (LinkNameNoPrefix.Buffer[i] == L'\\')
        {
            break;
        }
    }
    ReferenceStringOffset = i * sizeof(WCHAR);

    /* The GUID is before the reference string or at the end */
    ASSERT(LinkNameNoPrefix.Length >= ReferenceStringOffset);
    if (ReferenceStringOffset < GUID_STRING_BYTES + sizeof(WCHAR))
    {
        DPRINT1("IoSetDeviceInterfaceState() invalid link name '%wZ'\n", SymbolicLinkName);
        return STATUS_INVALID_PARAMETER;
    }

    GuidString.Buffer = LinkNameNoPrefix.Buffer + (ReferenceStringOffset - GUID_STRING_BYTES) / sizeof(WCHAR);
    GuidString.Length = GUID_STRING_BYTES;
    GuidString.MaximumLength = GuidString.Length;
    Status = RtlGUIDFromString(&GuidString, &DeviceGuid);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RtlGUIDFromString() invalid GUID '%wZ' in link name '%wZ'\n", &GuidString, SymbolicLinkName);
        return Status;
    }

    /* Open registry keys */
    Status = OpenRegistryHandlesFromSymbolicLink(SymbolicLinkName,
                                                 KEY_CREATE_SUB_KEY,
                                                 NULL,
                                                 NULL,
                                                 &InstanceHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, L"Control");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               InstanceHandle,
                               NULL);
    Status = ZwCreateKey(&ControlHandle,
                         KEY_SET_VALUE,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         NULL);
    ZwClose(InstanceHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create the Control subkey\n");
        return Status;
    }

    LinkedValue = (Enable ? 1 : 0);

    RtlInitUnicodeString(&KeyName, L"Linked");
    Status = ZwSetValueKey(ControlHandle,
                           &KeyName,
                           0,
                           REG_DWORD,
                           &LinkedValue,
                           sizeof(ULONG));
    ZwClose(ControlHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to write the Linked value\n");
        return Status;
    }

    ASSERT(GuidString.Buffer >= LinkNameNoPrefix.Buffer + 1);
    DeviceInstance.Length = (GuidString.Buffer - LinkNameNoPrefix.Buffer - 1) * sizeof(WCHAR);
    if (DeviceInstance.Length == 0)
    {
        DPRINT1("No device instance in link name '%wZ'\n", SymbolicLinkName);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    DeviceInstance.MaximumLength = DeviceInstance.Length;
    DeviceInstance.Buffer = ExAllocatePoolWithTag(PagedPool,
                                                  DeviceInstance.MaximumLength,
                                                  TAG_IO);
    if (DeviceInstance.Buffer == NULL)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(DeviceInstance.Buffer,
                  LinkNameNoPrefix.Buffer,
                  DeviceInstance.Length);

    for (Index = 0; Index < DeviceInstance.Length / sizeof(WCHAR); Index++)
    {
        if (DeviceInstance.Buffer[Index] == L'#')
        {
            DeviceInstance.Buffer[Index] = L'\\';
        }
    }

    PhysicalDeviceObject = IopGetDeviceObjectFromDeviceInstance(&DeviceInstance);

    if (!PhysicalDeviceObject)
    {
        DPRINT1("IopGetDeviceObjectFromDeviceInstance failed to find device object for %wZ\n", &DeviceInstance);
        ExFreePoolWithTag(DeviceInstance.Buffer, TAG_IO);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    ExFreePoolWithTag(DeviceInstance.Buffer, TAG_IO);

    EventGuid = Enable ? &GUID_DEVICE_INTERFACE_ARRIVAL : &GUID_DEVICE_INTERFACE_REMOVAL;
    IopNotifyPlugPlayNotification(
        PhysicalDeviceObject,
        EventCategoryDeviceInterfaceChange,
        EventGuid,
        &DeviceGuid,
        (PVOID)SymbolicLinkName);

    ObDereferenceObject(PhysicalDeviceObject);
    DPRINT("Status %x\n", Status);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopProcessSetInterfaceState(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ BOOLEAN Enable,
    _In_ BOOLEAN PdoNotStarted)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);//IoDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
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
