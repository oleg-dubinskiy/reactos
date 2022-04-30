
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpBuildPartialFromIdt)
  #pragma alloc_text(INIT, HalpBuildPartialFromAddress)
  #pragma alloc_text(INIT, HalpRegisterVector)
  #pragma alloc_text(INIT, HalpReportResourceUsage)
#endif

/* GLOBALS ********************************************************************/

/* This determines the HAL type */
BOOLEAN HalDisableFirmwareMapper = TRUE;

PUCHAR KdComPortInUse;
BOOLEAN HalpGetInfoFromACPI;

IDTUsageFlags HalpIDTUsageFlags[MAXIMUM_IDTVECTOR + 1];
IDTUsage HalpIDTUsage[MAXIMUM_IDTVECTOR + 1];

USHORT HalpComPortIrqMapping[5][2] =
{
    {0x03F8, 0x0004},
    {0x02F8, 0x0003},
    {0x03E8, 0x0004},
    {0x02E8, 0x0003},
    {0x0000, 0x0000}
};

ADDRESS_USAGE HalpComIoSpace =
{
    NULL, CmResourceTypePort, IDT_DEVICE, // 0x21
    {
        {0x000002F8, 0x00000008},     /* COM 1 */
        {0x00000000, 0x00000000},
    }
};

PADDRESS_USAGE HalpAddressUsageList;

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
    PCM_RESOURCE_LIST RawList, TranslatedList;
    PCM_FULL_RESOURCE_DESCRIPTOR RawFull, TranslatedFull;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR CurrentRaw, CurrentTranslated, SortedRaw, SortedTranslated;
    CM_PARTIAL_RESOURCE_DESCRIPTOR RawPartial, TranslatedPartial;
    PCM_PARTIAL_RESOURCE_LIST RawPartialList = NULL, TranslatedPartialList = NULL;
    INTERFACE_TYPE Interface;
    ULONG ListSize, Count, Port, CurrentScale, SortScale, ReportType, FlagMatch;
    ULONG ix, jx, kx;
    ADDRESS_USAGE* CurrentAddress;
    LARGE_INTEGER CurrentSortValue, SortValue;

    DPRINT1("HalpReportResourceUsage: %wZ Detected\n", HalName);
    //ASSERT(0);// HalpDbgBreakPointEx();

    /* Check if KD is using a COM port */
    if (KdComPortInUse)
    {
        /* Enter it into the I/O space */
        HalpComIoSpace.Element[0].Start = PtrToUlong(KdComPortInUse);
        HalpComIoSpace.Next = HalpAddressUsageList;
        HalpAddressUsageList = &HalpComIoSpace;

        /* Use the debug port table if we have one */
        HalpGetInfoFromACPI = HalpGetDebugPortTable();
        DPRINT1("HalpGetInfoFromACPI %X\n", HalpGetInfoFromACPI);

        /* Check if we're using ACPI */
        if (!HalpGetInfoFromACPI)
        {
            /* No, so use our local table */
            for (ix = 0, Port = HalpComPortIrqMapping[ix][0];
                 (Port != 0);
                 ix++, Port = HalpComPortIrqMapping[ix][0])
            {
                /* Is this the port we want? */
                if (Port == (ULONG_PTR)KdComPortInUse)
                {
                    /* Register it */
                    DPRINT1("HalpReportResourceUsage: ix - %X\n", ix);
                    HalpRegisterVector(IDT_DEVICE | IDT_LATCHED,
                                       HalpComPortIrqMapping[ix][1],
                                       HalpComPortIrqMapping[ix][1] +
                                       PRIMARY_VECTOR_BASE,
                                       HIGH_LEVEL);
                }
            }
        }
    }

    /* Allocate the master raw and translated lists */
    RawList = ExAllocatePoolWithTag(NonPagedPool, (2 * PAGE_SIZE), TAG_HAL);
    TranslatedList = ExAllocatePoolWithTag(NonPagedPool, (2 * PAGE_SIZE), TAG_HAL);

    if (!(RawList) || !(TranslatedList))
    {
        DPRINT1("HalpReportResourceUsage: HAL_MEMORY_ALLOCATION - Bugcheck the system!\n");
        KeBugCheckEx(HAL_MEMORY_ALLOCATION, 4 * PAGE_SIZE, 1, (ULONG_PTR)__FILE__, __LINE__);
    }

    /* Zero out the lists */
    RtlZeroMemory(RawList, (2 * PAGE_SIZE));
    RtlZeroMemory(TranslatedList, (2 * PAGE_SIZE));

    /* Set the interface type to begin with */
    RawList->List[0].InterfaceType = InterfaceTypeUndefined;

    /* Loop all IDT entries that are not IRQs */
    for (ix = 0; ix < PRIMARY_VECTOR_BASE; ix++)
    {
        /* Check if the IDT isn't owned */
        if (!(HalpIDTUsageFlags[ix].Flags & IDT_REGISTERED))
        {
            /* Then register it for internal usage */
            HalpIDTUsageFlags[ix].Flags = IDT_INTERNAL;
            HalpIDTUsage[ix].BusReleativeVector = (UCHAR)ix;
        }
    }

    /* Our full descriptors start here */
    RawFull = &RawList->List[0];
    TranslatedFull = &TranslatedList->List[0];

    /* Do two passes */
    for (ReportType = 0; ReportType < 2; ReportType++)
    {
        RawList->Count++;
        RawPartialList = &RawFull->PartialResourceList;
        CurrentRaw = RawPartialList->PartialDescriptors;

        TranslatedList->Count++;
        TranslatedPartialList = &TranslatedFull->PartialResourceList;
        CurrentTranslated = TranslatedPartialList->PartialDescriptors;

        if (ReportType == 0)
        {
            /* Pass 0 is for device usage */
            FlagMatch = IDT_DEVICE & ~IDT_REGISTERED;
            Interface = InterfaceType;
        }
        else
        {
            /* Pass 1 is for internal HAL usage */
            FlagMatch = IDT_INTERNAL & ~IDT_REGISTERED;
            Interface = Internal;
        }

        /* And it is of this new interface type */
        RawFull->InterfaceType = Interface;
        TranslatedFull->InterfaceType = Interface;

        /* Start looping our interrupts */
        for (ix = 0; ix <= MAXIMUM_IDTVECTOR; ix++)
        {
            /* Check if this entry should be parsed */
            if (!(HalpIDTUsageFlags[ix].Flags & FlagMatch))
            {
                /* Skip this entry */
                continue;
            }

            /* Parse it */
            HalpBuildPartialFromIdt(ix, &RawPartial, &TranslatedPartial);

            HalpAddDescriptors(RawPartialList, &CurrentRaw, &RawPartial);
            HalpAddDescriptors(TranslatedPartialList, &CurrentTranslated, &TranslatedPartial);
        }

        /* Start looping our address uage list */
        for (CurrentAddress = HalpAddressUsageList;
             CurrentAddress;
             CurrentAddress = CurrentAddress->Next)
        {
            for (ix = 0; (CurrentAddress->Element[ix].Length != 0); ix++)
            {
                /* Check if the address should be reported */
                if (!(CurrentAddress->Flags & FlagMatch) ||
                    !(CurrentAddress->Element[ix].Length))
                {
                    /* Nope, skip it */
                    continue;
                }

                /* Otherwise, parse the entry */
                HalpBuildPartialFromAddress(Interface,
                                            CurrentAddress,
                                            ix,
                                            &RawPartial,
                                            &TranslatedPartial);

                HalpAddDescriptors(RawPartialList, &CurrentRaw, &RawPartial);
                HalpAddDescriptors(TranslatedPartialList, &CurrentTranslated, &TranslatedPartial);
            }
        }

        /* Our full descriptors start here */
        RawFull = (PCM_FULL_RESOURCE_DESCRIPTOR)CurrentRaw;
        TranslatedFull = (PCM_FULL_RESOURCE_DESCRIPTOR)CurrentTranslated;
    }

    /* Get the final list of the size for the kernel call later */
    ListSize = (ULONG)((ULONG_PTR)CurrentRaw - (ULONG_PTR)RawList);

    /* Now reset back to the first full descriptor */
    RawFull = RawList->List;
    TranslatedFull = TranslatedList->List;

#if 0 //DBG
{
    DPRINT1("HalpReportResourceUsage: Dump RawList:\n");
    HalpDumpCmResourceList(RawList);
    DPRINT1("HalpReportResourceUsage: Dump TranslatedList:\n");
    HalpDumpCmResourceList(TranslatedList);
    //ASSERT(FALSE);
}
#endif

    /* And loop all the full descriptors */
    for (ix = 0; ix < RawList->Count; ix++)
    {
        /* Get the first partial descriptor in this list */
        CurrentRaw = RawFull->PartialResourceList.PartialDescriptors;
        CurrentTranslated = TranslatedFull->PartialResourceList.PartialDescriptors;

        /* Get the count of partials in this list */
        Count = RawFull->PartialResourceList.Count;

        /* Loop all the partials in this list */
        for (jx = 0; jx < Count; jx++)
        {
            /* Get the sort value at this point */
            HalpGetResourceSortValue(CurrentRaw, &CurrentScale, &CurrentSortValue);

            /* Save the current sort pointer */
            SortedRaw = CurrentRaw;
            SortedTranslated = CurrentTranslated;

            /* Loop all descriptors starting from this one */
            for (kx = jx; kx < Count; kx++)
            {
                /* Get the sort value at the sort point */
                HalpGetResourceSortValue(SortedRaw, &SortScale, &SortValue);

                /* Check if a swap needs to occur */
                if ((SortScale < CurrentScale) ||
                    ((SortScale == CurrentScale) &&
                     (SortValue.QuadPart <= CurrentSortValue.QuadPart)))
                {
                    /* Swap raw partial with the sort location partial */
                    RtlCopyMemory(&RawPartial, CurrentRaw, sizeof(RawPartial));
                    RtlCopyMemory(CurrentRaw, SortedRaw, sizeof(RawPartial));
                    RtlCopyMemory(SortedRaw, &RawPartial, sizeof(RawPartial));

                    /* Swap translated partial in the same way */
                    RtlCopyMemory(&TranslatedPartial, CurrentTranslated, sizeof(TranslatedPartial));
                    RtlCopyMemory(CurrentTranslated, SortedTranslated, sizeof(TranslatedPartial));
                    RtlCopyMemory(SortedTranslated, &TranslatedPartial, sizeof(TranslatedPartial));

                    /* Update the sort value at this point */
                    HalpGetResourceSortValue(CurrentRaw, &CurrentScale, &CurrentSortValue);
                }

                /* The sort location has been updated */
                SortedRaw++;
                SortedTranslated++;
            }

            /* Move to the next partial */
            CurrentRaw++;
            CurrentTranslated++;
        }

        /* Move to the next full descriptor */
        RawFull = (PCM_FULL_RESOURCE_DESCRIPTOR)CurrentRaw;
        TranslatedFull = (PCM_FULL_RESOURCE_DESCRIPTOR)CurrentTranslated;
    }

    /* Mark this is an ACPI system, if it is */
    HalpMarkAcpiHal();

    /* Tell the kernel about all this */
    IoReportHalResourceUsage(HalName, RawList, TranslatedList, ListSize);

    /* Free our lists */
    ExFreePoolWithTag(RawList, TAG_HAL);
    ExFreePoolWithTag(TranslatedList, TAG_HAL);

    /* Get the machine's serial number */
    HalpReportSerialNumber();

    //HalpTestAcpiHal();
}

/* EOF */

