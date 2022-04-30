
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpRegisterVector)
  #pragma alloc_text(INIT, HalpReportResourceUsage)
#endif

/* GLOBALS ********************************************************************/

/* This determines the HAL type */
BOOLEAN HalDisableFirmwareMapper = TRUE;

PUCHAR KdComPortInUse;

IDTUsageFlags HalpIDTUsageFlags[MAXIMUM_IDTVECTOR + 1];
IDTUsage HalpIDTUsage[MAXIMUM_IDTVECTOR + 1];

UCHAR HalpSerialLen;
CHAR HalpSerialNumber[31];

extern KAFFINITY HalpActiveProcessors;

/* FUNCTIONS ******************************************************************/

static
NTSTATUS
HalpMarkAcpiHal(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING KeyString;
    HANDLE KeyHandle;
    HANDLE Handle;
    ULONG Value = (HalDisableFirmwareMapper ? 1 : 0);

    /* Open the control set key */
    RtlInitUnicodeString(&KeyString, L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET");

    Status = HalpOpenRegistryKey(&Handle, 0, &KeyString, KEY_ALL_ACCESS, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpMarkAcpiHal: Status %X\n", Status);
        return Status;
    }

    /* Open the PNP key */
    RtlInitUnicodeString(&KeyString, L"Control\\Pnp");
    Status = HalpOpenRegistryKey(&KeyHandle, Handle, &KeyString, KEY_ALL_ACCESS, TRUE);

    /* Close root key */
    ZwClose(Handle);

    /* Check if PNP BIOS key exists */
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpMarkAcpiHal: Status %X\n", Status);
        return Status;
    }

    /* Set the disable value to false -- we need the mapper */
    RtlInitUnicodeString(&KeyString, L"DisableFirmwareMapper");
    Status = ZwSetValueKey(KeyHandle, &KeyString, 0, REG_DWORD, &Value, sizeof(Value));

    /* Close subkey */
    ZwClose(KeyHandle);

    if (!NT_SUCCESS(Status))
        DPRINT1("HalpMarkAcpiHal: Status %X\n", Status);

    /* Return status */
    return Status;
}

static
VOID
HalpReportSerialNumber(VOID)
{
    UNICODE_STRING KeyString;
    HANDLE Handle;
    NTSTATUS Status;

    /* Make sure there is a serial number */
    if (!HalpSerialLen)
        return;

    /* Open the system key */
    RtlInitUnicodeString(&KeyString, L"\\Registry\\Machine\\Hardware\\Description\\System");

    Status = HalpOpenRegistryKey(&Handle, 0, &KeyString, KEY_ALL_ACCESS, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HalpReportSerialNumber: Status %X\n", Status);
        return;
    }

    /* Add the serial number */
    RtlInitUnicodeString(&KeyString, L"Serial Number");
    ZwSetValueKey(Handle, &KeyString, 0, REG_BINARY, HalpSerialNumber, HalpSerialLen);

    /* Close the handle */
    ZwClose(Handle);
}

INIT_FUNCTION
VOID
NTAPI
HalpRegisterVector(IN UCHAR Flags,
                   IN ULONG BusVector,
                   IN ULONG SystemVector,
                   IN KIRQL Irql)
{
    DPRINT("HalpRegisterVector: %X, %X, %X, %X\n", Flags, BusVector, SystemVector, Irql);

    /* Save the vector flags */
    HalpIDTUsageFlags[SystemVector].Flags = Flags;

    /* Save the vector data */
    HalpIDTUsage[SystemVector].Irql  = Irql;
    HalpIDTUsage[SystemVector].BusReleativeVector = (UCHAR)BusVector;
}

INIT_FUNCTION
VOID
NTAPI
HalpBuildPartialFromIdt(IN ULONG Entry,
                        IN PCM_PARTIAL_RESOURCE_DESCRIPTOR RawDescriptor,
                        IN PCM_PARTIAL_RESOURCE_DESCRIPTOR TranslatedDescriptor)
{
    /* Exclusive interrupt entry */
    RawDescriptor->Type = CmResourceTypeInterrupt;
    RawDescriptor->ShareDisposition = CmResourceShareDriverExclusive;

    /* Check the interrupt type */
    if (HalpIDTUsageFlags[Entry].Flags & IDT_LATCHED)
        RawDescriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
    else
        RawDescriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;

    /* Get vector and level from IDT usage */
    RawDescriptor->u.Interrupt.Vector = HalpIDTUsage[Entry].BusReleativeVector;
    RawDescriptor->u.Interrupt.Level = HalpIDTUsage[Entry].BusReleativeVector;

    /* Affinity is all the CPUs */
    RawDescriptor->u.Interrupt.Affinity = HalpActiveProcessors;

    /* The translated copy is identical */
    RtlCopyMemory(TranslatedDescriptor, RawDescriptor, sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));

    /* But the vector and IRQL must be set correctly */
    TranslatedDescriptor->u.Interrupt.Vector = Entry;
    TranslatedDescriptor->u.Interrupt.Level = HalpIDTUsage[Entry].Irql;
}

INIT_FUNCTION
VOID
NTAPI
HalpBuildPartialFromAddress(IN INTERFACE_TYPE Interface,
                            IN PADDRESS_USAGE CurrentAddress,
                            IN ULONG Element,
                            IN PCM_PARTIAL_RESOURCE_DESCRIPTOR RawDescriptor,
                            IN PCM_PARTIAL_RESOURCE_DESCRIPTOR TranslatedDescriptor)
{
    ULONG AddressSpace;

    /* Set the type and make it exclusive */
    RawDescriptor->Type = CurrentAddress->Type;
    RawDescriptor->ShareDisposition = CmResourceShareDriverExclusive;

    /* Check what this is */
    if (RawDescriptor->Type == CmResourceTypePort)
    {
        /* Write out port data */
        AddressSpace = 1;
        RawDescriptor->u.Port.Start.HighPart = 0;
        RawDescriptor->u.Port.Start.LowPart = CurrentAddress->Element[Element].Start;
        RawDescriptor->u.Port.Length = CurrentAddress->Element[Element].Length;
        RawDescriptor->Flags = CM_RESOURCE_PORT_IO | CM_RESOURCE_PORT_16_BIT_DECODE;
    }
    else
    {
        /* Write out memory data */
        AddressSpace = 0;
        RawDescriptor->u.Memory.Start.HighPart = 0;
        RawDescriptor->u.Memory.Start.LowPart = CurrentAddress->Element[Element].Start;
        RawDescriptor->u.Memory.Length = CurrentAddress->Element[Element].Length;

        if (CurrentAddress->Flags & IDT_READ_ONLY)
            RawDescriptor->Flags = CM_RESOURCE_MEMORY_READ_ONLY;
        else
            RawDescriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    }

    /* Make an identical copy to begin with */
    RtlCopyMemory(TranslatedDescriptor, RawDescriptor, sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));

    /* Check what this is */
    if (RawDescriptor->Type != CmResourceTypePort)
    {
        /* Translate the memory */
        HalTranslateBusAddress(Interface,
                               0,
                               RawDescriptor->u.Memory.Start,
                               &AddressSpace,
                               &TranslatedDescriptor->u.Memory.Start);
        return;
    }

    /* Translate the port */
    HalTranslateBusAddress(Interface,
                           0,
                           RawDescriptor->u.Port.Start,
                           &AddressSpace,
                           &TranslatedDescriptor->u.Port.Start);

    /* If it turns out this is memory once translated, flag it */
    if (AddressSpace == 0)
        TranslatedDescriptor->Flags = CM_RESOURCE_PORT_MEMORY;
}

static
VOID
HalpAddDescriptors(IN PCM_PARTIAL_RESOURCE_LIST List,
                   IN OUT PCM_PARTIAL_RESOURCE_DESCRIPTOR * Descriptor,
                   IN PCM_PARTIAL_RESOURCE_DESCRIPTOR NewDescriptor)
{
    /* We have written a new partial descriptor */
    List->Count++;

    /* Copy new descriptor into the actual list */
    RtlCopyMemory(*Descriptor, NewDescriptor, sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));

    /* Move pointer to the next partial descriptor */
    (*Descriptor)++;
}

static
VOID
HalpGetResourceSortValue(IN PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
                         OUT PULONG Scale,
                         OUT PLARGE_INTEGER Value)
{
    /* Sorting depends on resource type */
    switch (Descriptor->Type)
    {
        case CmResourceTypeInterrupt:

            /* Interrupt goes by level */
            *Scale = 0;
            *Value = RtlConvertUlongToLargeInteger(Descriptor->u.Interrupt.Level);
            break;

        case CmResourceTypePort:

            /* Port goes by port address */
            *Scale = 1;
            *Value = Descriptor->u.Port.Start;
            break;

        case CmResourceTypeMemory:

            /* Memory goes by base address */
            *Scale = 2;
            *Value = Descriptor->u.Memory.Start;
            break;

        default:

            /* Anything else */
            *Scale = 4;
            *Value = RtlConvertUlongToLargeInteger(0);
            break;
    }
}

INIT_FUNCTION
VOID
NTAPI
HalpReportResourceUsage(IN PUNICODE_STRING HalName,
                        IN INTERFACE_TYPE InterfaceType)
{
    DPRINT1("HalpReportResourceUsage: %wZ Detected\n", HalName);
    ASSERT(0);// HalpDbgBreakPointEx();
}

/* EOF */

