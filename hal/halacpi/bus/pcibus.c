
/* INCLUDES ******************************************************************/

#include <hal.h>
//#include "pcibus.h"

//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpSetupPciDeviceForDebugging)
  #pragma alloc_text(INIT, HalpReleasePciDeviceForDebugging)
  #pragma alloc_text(INIT, HalpQueryPciRegistryInfo)
#endif

/* GLOBALS *******************************************************************/

KSPIN_LOCK HalpPCIConfigLock;

/* Type 1 PCI Bus */
PCI_CONFIG_HANDLER PCIConfigHandlerType1 =
{
    /* Synchronization */
    (FncSync)HalpPCISynchronizeType1,
    (FncReleaseSync)HalpPCIReleaseSynchronzationType1,

    /* Read */
    {
        (FncConfigIO)HalpPCIReadUlongType1,
        (FncConfigIO)HalpPCIReadUcharType1,
        (FncConfigIO)HalpPCIReadUshortType1
    },

    /* Write */
    {
        (FncConfigIO)HalpPCIWriteUlongType1,
        (FncConfigIO)HalpPCIWriteUcharType1,
        (FncConfigIO)HalpPCIWriteUshortType1
    }
};

/* Type 2 PCI Bus */
PCI_CONFIG_HANDLER PCIConfigHandlerType2 =
{
    /* Synchronization */
    (FncSync)HalpPCISynchronizeType2,
    (FncReleaseSync)HalpPCIReleaseSynchronizationType2,

    /* Read */
    {
        (FncConfigIO)HalpPCIReadUlongType2,
        (FncConfigIO)HalpPCIReadUcharType2,
        (FncConfigIO)HalpPCIReadUshortType2
    },

    /* Write */
    {
        (FncConfigIO)HalpPCIWriteUlongType2,
        (FncConfigIO)HalpPCIWriteUcharType2,
        (FncConfigIO)HalpPCIWriteUshortType2
    }
};

PCI_CONFIG_HANDLER PCIConfigHandler;

/* PCI Operation Matrix */
UCHAR PCIDeref[4][4] =
{
    {0, 1, 2, 2},   // ULONG-aligned offset
    {1, 1, 1, 1},   // UCHAR-aligned offset
    {2, 1, 2, 2},   // USHORT-aligned offset
    {1, 1, 1, 1}    // UCHAR-aligned offset
};

/* TYPE 1 FUNCTIONS **********************************************************/

VOID
NTAPI
HalpPCISynchronizeType1(IN PBUS_HANDLER BusHandler,
                        IN PCI_SLOT_NUMBER Slot,
                        IN PKIRQL Irql,
                        IN PPCI_TYPE1_CFG_BITS PciCfg1)
{
    /* Setup the PCI Configuration Register */
    PciCfg1->u.AsULONG = 0;
    PciCfg1->u.bits.BusNumber = BusHandler->BusNumber;
    PciCfg1->u.bits.DeviceNumber = Slot.u.bits.DeviceNumber;
    PciCfg1->u.bits.FunctionNumber = Slot.u.bits.FunctionNumber;
    PciCfg1->u.bits.Enable = TRUE;

    /* Acquire the lock */
    KeRaiseIrql(HIGH_LEVEL, Irql);
    KiAcquireSpinLock(&HalpPCIConfigLock);
}

VOID
NTAPI
HalpPCIReleaseSynchronzationType1(IN PBUS_HANDLER BusHandler,
                                  IN KIRQL Irql)
{
    PCI_TYPE1_CFG_BITS PciCfg1;
    PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;

    /* Clear the PCI Configuration Register */
    PciCfg1.u.AsULONG = 0;
    WRITE_PORT_ULONG(BusData->Config.Type1.Address, PciCfg1.u.AsULONG);

    /* Release the lock */
    KiReleaseSpinLock(&HalpPCIConfigLock);
    KeLowerIrql(Irql);
}

TYPE1_READ(HalpPCIReadUcharType1, UCHAR)
TYPE1_READ(HalpPCIReadUshortType1, USHORT)
TYPE1_READ(HalpPCIReadUlongType1, ULONG)

TYPE1_WRITE(HalpPCIWriteUcharType1, UCHAR)
TYPE1_WRITE(HalpPCIWriteUshortType1, USHORT)
TYPE1_WRITE(HalpPCIWriteUlongType1, ULONG)

/* TYPE 2 FUNCTIONS **********************************************************/

VOID
NTAPI
HalpPCISynchronizeType2(IN PBUS_HANDLER BusHandler,
                        IN PCI_SLOT_NUMBER Slot,
                        IN PKIRQL Irql,
                        IN PPCI_TYPE2_ADDRESS_BITS PciCfg)
{
    PCI_TYPE2_CSE_BITS PciCfg2Cse;
    PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;

    /* Setup the configuration register */
    PciCfg->u.AsUSHORT = 0;
    PciCfg->u.bits.Agent = (USHORT)Slot.u.bits.DeviceNumber;
    PciCfg->u.bits.AddressBase = (USHORT)BusData->Config.Type2.Base;

    /* Acquire the lock */
    KeRaiseIrql(HIGH_LEVEL, Irql);
    KiAcquireSpinLock(&HalpPCIConfigLock);

    /* Setup the CSE Register */
    PciCfg2Cse.u.AsUCHAR = 0;
    PciCfg2Cse.u.bits.Enable = TRUE;
    PciCfg2Cse.u.bits.FunctionNumber = (UCHAR)Slot.u.bits.FunctionNumber;
    PciCfg2Cse.u.bits.Key = -1;

    /* Write the bus number and CSE */
    WRITE_PORT_UCHAR(BusData->Config.Type2.Forward, (UCHAR)BusHandler->BusNumber);
    WRITE_PORT_UCHAR(BusData->Config.Type2.CSE, PciCfg2Cse.u.AsUCHAR);
}

VOID
NTAPI
HalpPCIReleaseSynchronizationType2(IN PBUS_HANDLER BusHandler,
                                   IN KIRQL Irql)
{
    PCI_TYPE2_CSE_BITS PciCfg2Cse;
    PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;

    /* Clear CSE and bus number */
    PciCfg2Cse.u.AsUCHAR = 0;
    WRITE_PORT_UCHAR(BusData->Config.Type2.CSE, PciCfg2Cse.u.AsUCHAR);
    WRITE_PORT_UCHAR(BusData->Config.Type2.Forward, 0);

    /* Release the lock */
    KiReleaseSpinLock(&HalpPCIConfigLock);
    KeLowerIrql(Irql);
}

TYPE2_READ(HalpPCIReadUcharType2, UCHAR)
TYPE2_READ(HalpPCIReadUshortType2, USHORT)
TYPE2_READ(HalpPCIReadUlongType2, ULONG)

TYPE2_WRITE(HalpPCIWriteUcharType2, UCHAR)
TYPE2_WRITE(HalpPCIWriteUshortType2, USHORT)
TYPE2_WRITE(HalpPCIWriteUlongType2, ULONG)

/* PCI CONFIGURATION SPACE ***************************************************/

VOID
NTAPI
HalpPCIConfig(IN PBUS_HANDLER BusHandler,
              IN PCI_SLOT_NUMBER Slot,
              IN PUCHAR Buffer,
              IN ULONG Offset,
              IN ULONG Length,
              IN FncConfigIO * ConfigIO)
{
    KIRQL OldIrql;
    ULONG ix;
    ULONG Bytes;
    UCHAR State[20];

    /* Synchronize the operation */
    PCIConfigHandler.Synchronize(BusHandler, Slot, &OldIrql, State);

    /* Loop every increment */
    while (Length)
    {
        /* Find out the type of read/write we need to do */
        ix = PCIDeref[Offset % sizeof(ULONG)][Length % sizeof(ULONG)];

        /* Do the read/write and return the number of bytes */
        Bytes = ConfigIO[ix]((PPCIPBUSDATA)BusHandler->BusData, State, Buffer, Offset);

        /* Increment the buffer position and offset, and decrease the length */
        Offset += Bytes;
        Buffer += Bytes;
        Length -= Bytes;
    }

    /* Release the lock and PCI bus */
    PCIConfigHandler.ReleaseSynchronzation(BusHandler, OldIrql);
}

BOOLEAN
NTAPI
HalpValidPCISlot(IN PBUS_HANDLER BusHandler,
                 IN PCI_SLOT_NUMBER Slot)
{
    PCI_SLOT_NUMBER MultiSlot;
    PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;
    UCHAR HeaderType;
    //ULONG Device;

    /* Simple validation */
    if (Slot.u.bits.Reserved)
        return FALSE;

    if (Slot.u.bits.DeviceNumber >= BusData->MaxDevice)
        return FALSE;

    /* Function 0 doesn't need checking */
    if (!Slot.u.bits.FunctionNumber)
        return TRUE;

    /* Functions 0+ need Multi-Function support, so check the slot */
    //Device = Slot.u.bits.DeviceNumber;
    MultiSlot = Slot;
    MultiSlot.u.bits.FunctionNumber = 0;

    /* Send function 0 request to get the header back */
    HalpReadPCIConfig(BusHandler,
                      MultiSlot,
                      &HeaderType,
                      FIELD_OFFSET(PCI_COMMON_CONFIG, HeaderType),
                      sizeof(UCHAR));

    /* Now make sure the header is multi-function */
    if (!(HeaderType & PCI_MULTIFUNCTION) || (HeaderType == 0xFF))
        return FALSE;

    return TRUE;
}

VOID
NTAPI
HalpReadPCIConfig(IN PBUS_HANDLER BusHandler,
                  IN PCI_SLOT_NUMBER Slot,
                  IN PVOID Buffer,
                  IN ULONG Offset,
                  IN ULONG Length)
{
    /* Validate the PCI Slot */
    if (!HalpValidPCISlot(BusHandler, Slot))
    {
        /* Fill the buffer with invalid data */
        RtlFillMemory(Buffer, Length, -1);
        return;
    }

    /* Send the request */
    HalpPCIConfig(BusHandler, Slot, Buffer, Offset, Length, PCIConfigHandler.ConfigRead);
}

VOID
NTAPI
HalpWritePCIConfig(IN PBUS_HANDLER BusHandler,
                   IN PCI_SLOT_NUMBER Slot,
                   IN PVOID Buffer,
                   IN ULONG Offset,
                   IN ULONG Length)
{
    /* Validate the PCI Slot */
    if (!HalpValidPCISlot(BusHandler, Slot))
        return;

    /* Send the request */
    HalpPCIConfig(BusHandler, Slot, Buffer, Offset, Length, PCIConfigHandler.ConfigWrite);
}

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

