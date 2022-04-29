
/* INCLUDES ******************************************************************/

#include <hal.h>
//#include "pcibus.h"

//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpSetupPciDeviceForDebugging)
  #pragma alloc_text(INIT, HalpReleasePciDeviceForDebugging)
#endif

/* GLOBALS *******************************************************************/


/* HAL PCI FOR DEBUGGING *****************************************************/

INIT_FUNCTION
NTSTATUS
NTAPI
HalpSetupPciDeviceForDebugging(IN PVOID LoaderBlock,
                               IN OUT PDEBUG_DEVICE_DESCRIPTOR PciDevice)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

INIT_FUNCTION
NTSTATUS
NTAPI
HalpReleasePciDeviceForDebugging(IN OUT PDEBUG_DEVICE_DESCRIPTOR PciDevice)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* PCI CONFIGURATION SPACE ***************************************************/


/* HAL PCI CALLBACKS *********************************************************/

ULONG
NTAPI
HaliPciInterfaceReadConfig(_In_ PVOID Context,
                           _In_ ULONG BusNumber,
                           _In_ ULONG SlotNumber,
                           _In_ PVOID Buffer,
                           _In_ ULONG Offset,
                           _In_ ULONG Length)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

ULONG
NTAPI
HaliPciInterfaceWriteConfig(_In_ PVOID Context,
                            _In_ ULONG BusNumber,
                            _In_ ULONG SlotNumber,
                            _In_ PVOID Buffer,
                            _In_ ULONG Offset,
                            _In_ ULONG Length)
{
    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
    return 0;
}

INIT_FUNCTION
PPCI_REGISTRY_INFO_INTERNAL
NTAPI
HalpQueryPciRegistryInfo(VOID)
{
    WCHAR NameBuffer[8];
    OBJECT_ATTRIBUTES  ObjectAttributes;
    UNICODE_STRING KeyName, ConfigName, IdentName;
    HANDLE KeyHandle, BusKeyHandle, CardListHandle;
    NTSTATUS Status;
    UCHAR KeyBuffer[sizeof(CM_FULL_RESOURCE_DESCRIPTOR) + 100];
    PKEY_VALUE_FULL_INFORMATION ValueInfo = (PVOID)KeyBuffer;
    UCHAR PartialKeyBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(PCI_CARD_DESCRIPTOR)];
    PKEY_VALUE_PARTIAL_INFORMATION PartialValueInfo = (PVOID)PartialKeyBuffer;
    KEY_FULL_INFORMATION KeyInformation;
    ULONG ResultLength;
    PWSTR Tag;
    ULONG ix, ElementCount;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PPCI_REGISTRY_INFO PciRegInfo;
    PPCI_REGISTRY_INFO_INTERNAL PciRegistryInfo;
    PPCI_CARD_DESCRIPTOR CardDescriptor;
    ULONG Size;

    /* Setup the object attributes for the key */
    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\Hardware\\Description\\System\\MultiFunctionAdapter");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    /* Open the key */
    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpQueryPciRegistryInfo: Status %X\n", Status);
        return NULL;
    }

    /* Setup the receiving string */
    KeyName.Buffer = NameBuffer;
    KeyName.MaximumLength = sizeof(NameBuffer);

    /* Setup the configuration and identifier key names */
    RtlInitUnicodeString(&ConfigName, L"Configuration Data");
    RtlInitUnicodeString(&IdentName, L"Identifier");

    /* Keep looping for each ID */
    for (ix = 0; TRUE; ix++)
    {
        /* Setup the key name */
        RtlIntegerToUnicodeString(ix, 10, &KeyName);
        InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, KeyHandle, NULL);

        /* Open it */
        Status = ZwOpenKey(&BusKeyHandle, KEY_READ, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            /* None left, fail */
            ZwClose(KeyHandle);
            return NULL;
        }

        /* Read the registry data */
        Status = ZwQueryValueKey(BusKeyHandle,
                                 &IdentName,
                                 KeyValueFullInformation,
                                 ValueInfo,
                                 sizeof(KeyBuffer),
                                 &ResultLength);
        if (!NT_SUCCESS(Status))
        {
            /* Failed, try the next one */
            ZwClose(BusKeyHandle);
            continue;
        }

        /* Get the PCI Tag and validate it */
        Tag = (PWSTR)((ULONG_PTR)ValueInfo + ValueInfo->DataOffset);

        if ((Tag[0] != L'P') ||
            (Tag[1] != L'C') ||
            (Tag[2] != L'I') ||
            (Tag[3]))
        {
            /* Not a valid PCI entry, skip it */
            ZwClose(BusKeyHandle);
            continue;
        }

        /* Now read our PCI structure */
        Status = ZwQueryValueKey(BusKeyHandle,
                                 &ConfigName,
                                 KeyValueFullInformation,
                                 ValueInfo,
                                 sizeof(KeyBuffer),
                                 &ResultLength);
        ZwClose(BusKeyHandle);
        if (!NT_SUCCESS(Status))
            continue;

        /* We read it OK! Get the actual resource descriptors */
        FullDescriptor  = (PCM_FULL_RESOURCE_DESCRIPTOR)((ULONG_PTR)ValueInfo + ValueInfo->DataOffset);
        PartialDescriptor = (PCM_PARTIAL_RESOURCE_DESCRIPTOR)((ULONG_PTR)FullDescriptor->PartialResourceList.PartialDescriptors);

        /* Check if this is our PCI Registry Information */
        if (PartialDescriptor->Type == CmResourceTypeDeviceSpecific)
            break;
    }

    ZwClose(KeyHandle);

    /* Save the PCI information for later */
    PciRegInfo = (PPCI_REGISTRY_INFO)(PartialDescriptor + 1);

    /* Assume no Card List entries */
    ElementCount = 0;

    /* Set up for checking the PCI Card List key */
    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\PnP\\PCI\\CardList");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    /* Attempt to open it */
    Status = ZwOpenKey(&CardListHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        /* No key, no Card List */
        PciRegistryInfo = NULL;
        goto Finish;
    }

    /* It exists, so let's query it */
    Status = ZwQueryKey(CardListHandle, KeyFullInformation, &KeyInformation, sizeof(KEY_FULL_INFORMATION), &ResultLength);
    if (!NT_SUCCESS(Status))
    {
        /* Failed to query, so no info */
        PciRegistryInfo = NULL;
        ZwClose(CardListHandle);
        goto Finish;
    }

    /* Allocate the full structure */
    Size = (sizeof(PCI_REGISTRY_INFO_INTERNAL) + (KeyInformation.Values * sizeof(PCI_CARD_DESCRIPTOR)));

    PciRegistryInfo = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_HAL);
    if (!PciRegistryInfo)
    {
        ZwClose(CardListHandle);
        goto Finish;
    }

    /* Get the first card descriptor entry */
    CardDescriptor = (PPCI_CARD_DESCRIPTOR)(PciRegistryInfo + 1);

    /* Loop all the values */
    for (ix = 0; ix < KeyInformation.Values; ix++)
    {
        /* Attempt to get the value */
        Status = ZwEnumerateValueKey(CardListHandle,
                                     ix,
                                     KeyValuePartialInformation,
                                     PartialValueInfo,
                                     sizeof(PartialKeyBuffer),
                                     &ResultLength);
        if (!NT_SUCCESS(Status))
            break;

        /* Make sure it is correctly sized */
        if (PartialValueInfo->DataLength == sizeof(PCI_CARD_DESCRIPTOR))
        {
            /* Sure is, copy it over */
            *CardDescriptor = *(PPCI_CARD_DESCRIPTOR)PartialValueInfo->Data;

            /* One more Card List entry */
            ElementCount++;

            /* Move to the next descriptor */
            CardDescriptor = (CardDescriptor + 1);
        }
    }

    ZwClose(CardListHandle);

Finish:

    /* Check if we failed to get the full structure */
    if (!PciRegistryInfo)
    {
        /* Just allocate the basic structure then */
        PciRegistryInfo = ExAllocatePoolWithTag(NonPagedPool, sizeof(PCI_REGISTRY_INFO_INTERNAL), TAG_HAL);
        if (!PciRegistryInfo)
        {
            DPRINT1("HalpQueryPciRegistryInfo: Status %X\n", Status);
            return NULL;
        }
    }

    PciRegistryInfo->MajorRevision = PciRegInfo->MajorRevision;
    PciRegistryInfo->MinorRevision = PciRegInfo->MinorRevision;
    PciRegistryInfo->NoBuses = PciRegInfo->NoBuses;
    PciRegistryInfo->HardwareMechanism = PciRegInfo->HardwareMechanism;
    PciRegistryInfo->ElementCount = ElementCount;

    return PciRegistryInfo;
}

/* EOF */

