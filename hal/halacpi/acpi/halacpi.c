
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalAcpiGetTable)
  #pragma alloc_text(INIT, HalpCheckPowerButton)
  #pragma alloc_text(INIT, HalpGetNMICrashFlag)
  #pragma alloc_text(INIT, HalpInitializePciBus)
  #pragma alloc_text(INIT, HalReportResourceUsage)
#endif

/* GLOBALS ********************************************************************/

PWCHAR HalName = L"ACPI Compatible Eisa/Isa HAL";

PHYSICAL_ADDRESS HalpMaxHotPlugMemoryAddress;
PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
FAST_MUTEX HalpAcpiTableCacheLock;
LIST_ENTRY HalpAcpiTableCacheList;
LIST_ENTRY HalpAcpiTableMatchList;
PACPI_BIOS_MULTI_NODE HalpAcpiMultiNode;
FADT HalpFixedAcpiDescTable;
PDEBUG_PORT_TABLE HalpDebugPortTable;
PACPI_SRAT HalpAcpiSrat;
PBOOT_TABLE HalpSimpleBootFlagTable;
PHARDWARE_PTE HalpPteForFlush;
PVOID HalpVirtAddrForFlush;
PVOID HalpLowStub;
ULONG HalpWAETDeviceFlags = 0;
ULONG HalpInvalidAcpiTable;
//ULONG HalpShutdownContext = 0;
BOOLEAN HalpProcessedACPIPhase0;
BOOLEAN HalpNMIDumpFlag;

extern BOOLEAN LessThan16Mb;
extern BOOLEAN HalpPhysicalMemoryMayAppearAbove4GB;
extern ULONG HalpBusType;

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
HalpAcpiFindRsdtPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                       OUT PACPI_BIOS_MULTI_NODE* AcpiMultiNode)
{
    PCONFIGURATION_COMPONENT_DATA ComponentEntry;
    PCONFIGURATION_COMPONENT_DATA Next = NULL;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;
    PACPI_BIOS_MULTI_NODE NodeData;
    SIZE_T NodeLength;
    PFN_COUNT PageCount;
    PVOID MappedAddress;
    PHYSICAL_ADDRESS PhysicalAddress;

    DPRINT("HalpAcpiFindRsdtPhase0: ... \n");

    /* Did we already do this once? */
    if (HalpAcpiMultiNode)
    {
        /* Return what we know */
        *AcpiMultiNode = HalpAcpiMultiNode;
        return STATUS_SUCCESS;
    }

    /* Assume failure */
    *AcpiMultiNode = NULL;

    /* Find the multi function adapter key */
    ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot, AdapterClass, MultiFunctionAdapter, NULL, &Next);

    while (ComponentEntry)
    {
        /* Find the ACPI BIOS key */
        if (!_stricmp(ComponentEntry->ComponentEntry.Identifier, "ACPI BIOS"))
        {
            /* Found it */
            break;
        }

        /* Keep searching */
        Next = ComponentEntry;
        ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot, AdapterClass, MultiFunctionAdapter, NULL, &Next);
    }

    /* Make sure we found it */
    if (!ComponentEntry)
    {
        DPRINT1("**** HalpAcpiFindRsdtPhase0: did NOT find RSDT\n");
        return STATUS_NOT_FOUND;
    }

    /* The configuration data is a resource list, and the BIOS node follows */
    ResourceList = ComponentEntry->ConfigurationData;
    NodeData = (PACPI_BIOS_MULTI_NODE)(ResourceList + 1);

    /* How many E820 memory entries are there? */
    NodeLength = sizeof(ACPI_BIOS_MULTI_NODE) + ((NodeData->Count - 1) * sizeof(ACPI_E820_ENTRY));

    /* Convert to pages */
    PageCount = (PFN_COUNT)BYTES_TO_PAGES(NodeLength);

    /* Allocate the memory */
    PhysicalAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock, 0x1000000, PageCount, FALSE);

    if (PhysicalAddress.QuadPart)
    {
        /* Map it if the allocation worked */
        MappedAddress = HalpMapPhysicalMemory64(PhysicalAddress, PageCount);
    }
    else
    {
        /* Otherwise we'll have to fail */
        MappedAddress = NULL;
    }

    /* Save the multi node, bail out if we didn't find it */
    HalpAcpiMultiNode = MappedAddress;
    if (!HalpAcpiMultiNode) {
        DPRINT1("STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Copy the multi-node data */
    RtlCopyMemory(MappedAddress, NodeData, NodeLength);

    /* Return the data */
    *AcpiMultiNode = HalpAcpiMultiNode;

    return STATUS_SUCCESS;
}

PVOID
NTAPI
HalpAcpiCopyBiosTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                      IN PDESCRIPTION_HEADER TableHeader)
{
    ULONG Size;
    PFN_COUNT PageCount;
    PHYSICAL_ADDRESS PhysAddress;
    PACPI_CACHED_TABLE CachedTable;
    PDESCRIPTION_HEADER CopiedTable;

    /* Size we'll need for the cached table */
    Size = TableHeader->Length + FIELD_OFFSET(ACPI_CACHED_TABLE, Header);

    if (LoaderBlock)
    {
        /* Phase 0: Convert to pages and use the HAL heap */
        PageCount = BYTES_TO_PAGES(Size);
        PhysAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock, 0x1000000, PageCount, FALSE);

        if (PhysAddress.QuadPart)
        {
            /* Map it */
            CachedTable = HalpMapPhysicalMemory64(PhysAddress, PageCount);
        }
        else
        {
            /* No memory, so nothing to map */
            CachedTable = NULL;
        }
    }
    else
    {
        /* Use Mm pool */
        CachedTable = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_HAL);
    }

    /* Do we have the cached table? */
    if (CachedTable)
    {
        /* Copy the data */
        CopiedTable = &CachedTable->Header;
        RtlCopyMemory(CopiedTable, TableHeader, TableHeader->Length);
    }
    else
    {
        /* Nothing to return */
        CopiedTable = NULL;
    }

    /* Return the table */
    return CopiedTable;
}

VOID
NTAPI
HalpAcpiCacheTable(IN PDESCRIPTION_HEADER TableHeader)
{
    PACPI_CACHED_TABLE CachedTable;

    /* Get the cached table and link it */
    CachedTable = CONTAINING_RECORD(TableHeader, ACPI_CACHED_TABLE, Header);
    InsertTailList(&HalpAcpiTableCacheList, &CachedTable->Links);
}

NTSTATUS
NTAPI
HalpAcpiTableCacheInit(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_EXTENSION LoaderExtension;
    PACPI_BIOS_MULTI_NODE AcpiMultiNode;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID MappedAddress;
    ULONG TableLength;
    PRSDT Rsdt;
    NTSTATUS Status = STATUS_SUCCESS;

    //DPRINT("HalpAcpiTableCacheInit: LoaderBlock %X\n", LoaderBlock);

    /* Only initialize once */
    if (HalpAcpiTableCacheList.Flink)
        return Status;

    /* Setup the lock and table */
    ExInitializeFastMutex(&HalpAcpiTableCacheLock);
    InitializeListHead(&HalpAcpiTableCacheList);

    /* Find the RSDT */
    Status = HalpAcpiFindRsdtPhase0(LoaderBlock, &AcpiMultiNode);
    if (!NT_SUCCESS(Status))
        return Status;

    PhysicalAddress.QuadPart = AcpiMultiNode->RsdtAddress.QuadPart;

    /* Map the RSDT */
    if (LoaderBlock)
    {
        /* Phase0: Use HAL Heap to map the RSDT, we assume it's about 2 pages */
        MappedAddress = HalpMapPhysicalMemory64(PhysicalAddress, 2);
    }
    else
    {
        /* Use an I/O map */
        MappedAddress = MmMapIoSpace(PhysicalAddress, PAGE_SIZE * 2, MmNonCached);
    }

    /* Get the RSDT */
    Rsdt = MappedAddress;
    if (!MappedAddress)
    {
        /* Fail, no memory */
        DPRINT1("HAL: Failed to map RSDT\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Validate it */
    if ((Rsdt->Header.Signature != RSDT_SIGNATURE) &&
        (Rsdt->Header.Signature != XSDT_SIGNATURE))
    {
        /* Very bad: crash */
        HalDisplayString("Bad RSDT pointer\r\n");
        KeBugCheckEx(MISMATCHED_HAL, 4, __LINE__, 0, 0);
    }

    /* We assumed two pages -- do we need less or more? */
    TableLength = ADDRESS_AND_SIZE_TO_SPAN_PAGES(PhysicalAddress.LowPart, Rsdt->Header.Length);

    if (TableLength != 2)
    {
        /* Are we in phase 0 or 1? */
        if (!LoaderBlock)
        {
            /* Unmap the old table, remap the new one, using Mm I/O space */
            MmUnmapIoSpace(MappedAddress, 2 * PAGE_SIZE);
            MappedAddress = MmMapIoSpace(PhysicalAddress, (TableLength << PAGE_SHIFT), MmNonCached);
        }
        else
        {
            /* Unmap the old table, remap the new one, using HAL heap */
            HalpUnmapVirtualAddress(MappedAddress, 2);
            MappedAddress = HalpMapPhysicalMemory64(PhysicalAddress, TableLength);
        }

        /* Get the remapped table */
        Rsdt = MappedAddress;
        if (!MappedAddress)
        {
            /* Fail, no memory */
            DPRINT1("HAL: Couldn't remap RSDT\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    /* Now take the BIOS copy and make our own local copy */
    Rsdt = HalpAcpiCopyBiosTable(LoaderBlock, &Rsdt->Header);
    if (!Rsdt)
    {
        /* Fail, no memory */
        DPRINT1("HAL: Couldn't remap RSDT\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Get rid of the BIOS mapping */
    if (LoaderBlock)
    {
        /* Use HAL heap */
        HalpUnmapVirtualAddress(MappedAddress, TableLength);
        LoaderExtension = LoaderBlock->Extension;
    }
    else
    {
        /* Use Mm */
        MmUnmapIoSpace(MappedAddress, TableLength << PAGE_SHIFT);
        LoaderExtension = NULL;
    }

    /* Cache the RSDT */
    HalpAcpiCacheTable(&Rsdt->Header);

    /* Check for compatible loader block extension */
    if (LoaderExtension && (LoaderExtension->Size >= 0x58))
    {
        /* Compatible loader: did it provide an ACPI table override? */
        if ((LoaderExtension->AcpiTable) && (LoaderExtension->AcpiTableSize))
        {
            /* Great, because we don't support it! */
            DPRINT1("ACPI Table Overrides Not Supported!\n");
        }
    }

    /* Done */
    return Status;
}

VOID
NTAPI
HalpNumaInitializeStaticConfiguration(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_SRAT SratTable;

    /* Get the SRAT */
    SratTable = HalAcpiGetTable(LoaderBlock, SRAT_SIGNATURE);

    HalpAcpiSrat = SratTable;
    if (!HalpAcpiSrat)
        return;

    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

VOID
NTAPI
HalpGetHotPlugMemoryInfo(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_SRAT SratTable;

    /* Get the SRAT */
    SratTable = HalAcpiGetTable(LoaderBlock, SRAT_SIGNATURE);
    if (!SratTable)
        return;

    UNIMPLEMENTED;
    ASSERT(0);// HalpDbgBreakPointEx();
}

VOID
NTAPI
HalpDynamicSystemResourceConfiguration(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* For this HAL, it means to get hot plug memory information */
    HalpGetHotPlugMemoryInfo(LoaderBlock);
}

VOID
NTAPI
HalpEndOfBoot(VOID)
{
    DPRINT1("HalpEndOfBoot: not implemented, FIXME!\n");
    ASSERT(0);// HalpDbgBreakPointEx();
}

VOID
NTAPI
HalpInitBootTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PBOOT_TABLE BootTable;

    DPRINT("HalpInitBootTable()\n");

    /* Get the boot table */
    BootTable = HalAcpiGetTable(LoaderBlock, BOOT_SIGNATURE);
    HalpSimpleBootFlagTable = BootTable;

    /* Validate it */
    if ((BootTable) &&
        (BootTable->Header.Length >= sizeof(BOOT_TABLE)) &&
        (BootTable->CMOSIndex >= 9))
    {
        DPRINT1("ACPI Boot table found, but not supported!\n");
    }
    else
    {
        /* Invalid or doesn't exist, ignore it */
        HalpSimpleBootFlagTable = 0;
    }

    /* Install the end of boot handler */
    HalEndOfBoot = HalpEndOfBoot;
}

INIT_FUNCTION
NTSTATUS
NTAPI
HalpSetupAcpiPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG_PTR EmulatedDevicesTable = 0;
    PFADT Fadt;
    ULONG TableLength;
    NTSTATUS Status;

    DPRINT("HalpSetupAcpiPhase0()\n");

    /* Only do this once */
    if (HalpProcessedACPIPhase0) {
        DPRINT("HalpProcessedACPIPhase0 - TRUE\n");
        return STATUS_SUCCESS;
    }

    /* Setup the ACPI table cache */
    Status = HalpAcpiTableCacheInit(LoaderBlock);
    if (!NT_SUCCESS(Status)) {
        DPRINT1("HalpSetupAcpiPhase0: Status %X\n", Status);
        return Status;
    }

    /* Grab the FADT */
    Fadt = HalAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
    if (!Fadt) {
        DPRINT1("HAL: Didn't find the FACP\n");
        return STATUS_NOT_FOUND;
    }

    /* Assume typical size, otherwise whatever the descriptor table says */
    TableLength = sizeof(FADT);
    if (Fadt->Header.Length < TableLength) {
        TableLength = Fadt->Header.Length;
    }

    /* Copy it in the HAL static buffer */
    RtlCopyMemory(&HalpFixedAcpiDescTable, Fadt, TableLength);

    /* Anything special this HAL needs to do? */
    //HalpAcpiDetectMachineSpecificActions(LoaderBlock, &HalpFixedAcpiDescTable); // not need for halacpi.dll

    /* Get the debug table for KD */
    HalpDebugPortTable = HalAcpiGetTable(LoaderBlock, DBGP_SIGNATURE);
    if (HalpDebugPortTable) {
        DPRINT1("HalpDebugPortTable %X\n", HalpDebugPortTable);
    }

    /* Initialize NUMA through the SRAT */
    HalpNumaInitializeStaticConfiguration(LoaderBlock);

    /* Initialize hotplug through the SRAT */
    HalpDynamicSystemResourceConfiguration(LoaderBlock);
    if (HalpAcpiSrat) {
        DPRINT1("HalpSetupAcpiPhase0: SRAT, but NUMA/HotPlug are not supported!\n");
    }

    EmulatedDevicesTable = (ULONG_PTR)HalAcpiGetTable(LoaderBlock, 'TEAW');
    if (EmulatedDevicesTable) {
        HalpWAETDeviceFlags = *(PULONG)(EmulatedDevicesTable + 36); // FIXME
        DPRINT1("HalpSetupAcpiPhase0: FIXME HalpWAETDeviceFlags %X\n", HalpWAETDeviceFlags);
    }

    /* Can there be memory higher than 4GB? */
    if (HalpMaxHotPlugMemoryAddress.HighPart >= 1) {
        /* We'll need this for DMA later */
        HalpPhysicalMemoryMayAppearAbove4GB = TRUE;
        DPRINT1("HalpPhysicalMemoryMayAppearAbove4GB - TRUE\n");
    }

    /* Setup the ACPI timer */
    HaliAcpiTimerInit(NULL, FALSE);

    /* Do we have a low stub address yet? */
    if (!HalpLowStubPhysicalAddress.QuadPart)
    {
        /* Allocate it */
        HalpLowStubPhysicalAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock, 0x100000, 1, FALSE);
        if (HalpLowStubPhysicalAddress.QuadPart)
        {
            /* Map it */
            HalpLowStub = HalpMapPhysicalMemory64(HalpLowStubPhysicalAddress, 1);
        }
    }

    /* Grab a page for flushes */
    PhysicalAddress.QuadPart = 0x100000;
    HalpVirtAddrForFlush = HalpMapPhysicalMemory64(PhysicalAddress, 1);
    HalpPteForFlush = HalAddressToPte(HalpVirtAddrForFlush);

    /* Don't do this again */
    HalpProcessedACPIPhase0 = TRUE;

    /* Setup the boot table */
    HalpInitBootTable(LoaderBlock);

#if DBG
    /* Debugging code */
    {
        PLIST_ENTRY ListHead, NextEntry;
        PACPI_CACHED_TABLE CachedTable;

        /* Loop cached tables */
        ListHead = &HalpAcpiTableCacheList;
        NextEntry = ListHead->Flink;

        while (NextEntry != ListHead)
        {
            /* Get the table */
            CachedTable = CONTAINING_RECORD(NextEntry, ACPI_CACHED_TABLE, Links);

            /* Compare signatures */
            if ((CachedTable->Header.Signature == RSDT_SIGNATURE) ||
                (CachedTable->Header.Signature == XSDT_SIGNATURE))
            {
                DPRINT1("ACPI %d.0 Detected. ", (CachedTable->Header.Revision + 1));
            }

            DbgPrint("[%c%c%c%c] ",
                    (CachedTable->Header.Signature & 0xFF),
                    (CachedTable->Header.Signature & 0xFF00) >> 8,
                    (CachedTable->Header.Signature & 0xFF0000) >> 16,
                    (CachedTable->Header.Signature & 0xFF000000) >> 24);

            /* Keep going */
            NextEntry = NextEntry->Flink;
        }

        DbgPrint("\n");
    }
#endif

    return STATUS_SUCCESS;
}

PDESCRIPTION_HEADER
NTAPI
HalpAcpiGetCachedTable(IN ULONG Signature)
{
    PLIST_ENTRY ListHead, NextEntry;
    PACPI_CACHED_TABLE CachedTable;

    //DPRINT("HalpAcpiGetCachedTable: Signature %X\n", Signature);

    /* Loop cached tables */
    ListHead = &HalpAcpiTableCacheList;
    NextEntry = ListHead->Flink;

    while (NextEntry != ListHead)
    {
        /* Get the table */
        CachedTable = CONTAINING_RECORD(NextEntry, ACPI_CACHED_TABLE, Links);

        /* Compare signatures */
        if (CachedTable->Header.Signature == Signature)
            return &CachedTable->Header;

        /* Keep going */
        NextEntry = NextEntry->Flink;
    }

    /* Nothing found */
    return NULL;
}

PVOID
NTAPI
HalpAcpiGetTableFromBios(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                         IN ULONG Signature)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PXSDT Xsdt;
    PRSDT Rsdt;
    PFADT Fadt;
    PDESCRIPTION_HEADER Header = NULL;
    ULONG TableLength;
    CHAR CheckSum = 0;
    ULONG Offset;
    ULONG EntryCount, CurrentEntry;
    PCHAR CurrentByte;
    PFN_COUNT PageCount;

    //DPRINT("HalpAcpiGetTableFromBios: Signature %X\n", Signature);

    /* Should not query the RSDT/XSDT by itself */
    if ((Signature == RSDT_SIGNATURE) || (Signature == XSDT_SIGNATURE))
        return NULL;

    /* Special case request for DSDT, because the FADT points to it */
    if (Signature == DSDT_SIGNATURE)
    {
        /* Grab the FADT */
        Fadt = HalpAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
        if (!Fadt)
            /* Couldn't find it */
            return NULL;

        /* Grab the DSDT address and assume 2 pages */
        PhysicalAddress.HighPart = 0;
        PhysicalAddress.LowPart = Fadt->dsdt;
        TableLength = 2 * PAGE_SIZE;

        /* Map it */
        if (LoaderBlock)
            /* Phase 0, use HAL heap */
            Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
        else
            /* Phase 1, use Mm */
            Header = MmMapIoSpace(PhysicalAddress, 2 * PAGE_SIZE, 0);

        /* Fail if we couldn't map it */
        if (!Header)
        {
            DPRINT1("HAL: Failed to map ACPI table.\n");
            return NULL;
        }

        /* Validate the signature */
        if (Header->Signature != DSDT_SIGNATURE)
        {
            /* Fail and unmap */
            if (LoaderBlock)
                /* Using HAL heap */
                HalpUnmapVirtualAddress(Header, 2);
            else
                /* Using Mm */
                MmUnmapIoSpace(Header, 2 * PAGE_SIZE);

            /* Didn't find anything */
            return NULL;
        }
    }
    else
    {
        /* To find tables, we need the RSDT */
        Rsdt = HalpAcpiGetTable(LoaderBlock, RSDT_SIGNATURE);
        if (Rsdt)
        {
            /* Won't be using the XSDT */
            Xsdt = NULL;
        }
        else
        {
            /* Only other choice is to use the XSDT */
            Xsdt = HalpAcpiGetTable(LoaderBlock, XSDT_SIGNATURE);
            if (!Xsdt)
                return NULL;

            /* Won't be using the RSDT */
            Rsdt = NULL;
        }

        /* Smallest RSDT/XSDT is one without table entries */
        Offset = FIELD_OFFSET(RSDT, Tables);
        if (Xsdt)
        {
            /* Figure out total size of table and the offset */
            TableLength = Xsdt->Header.Length;
            if (TableLength < Offset)
                Offset = Xsdt->Header.Length;

            /* The entries are each 64-bits, so count them */
            EntryCount = ((TableLength - Offset) / sizeof(PHYSICAL_ADDRESS));
        }
        else
        {
            /* Figure out total size of table and the offset */
            TableLength = Rsdt->Header.Length;
            if (TableLength < Offset)
                Offset = Rsdt->Header.Length;

            /* The entries are each 32-bits, so count them */
            EntryCount = ((TableLength - Offset) / sizeof(ULONG));
        }

        /* Start at the beginning of the array and loop it */
        for (CurrentEntry = 0; CurrentEntry < EntryCount; CurrentEntry++)
        {
            /* Are we using the XSDT? */
            if (!Xsdt)
            {
                /* Read the 32-bit physical address */
                PhysicalAddress.LowPart = Rsdt->Tables[CurrentEntry];
                PhysicalAddress.HighPart = 0;
            }
            else
            {
                /* Read the 64-bit physical address */
                PhysicalAddress = Xsdt->Tables[CurrentEntry];
            }

            /* Had we already mapped a table? */
            if (Header)
            {
                /* Yes, unmap it */
                if (LoaderBlock)
                    /* Using HAL heap */
                    HalpUnmapVirtualAddress(Header, 2);
                else
                    /* Using Mm */
                    MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
            }

            /* Now map this table */

            if (!LoaderBlock)
                /* Phase 1: Use HAL heap */
                Header = MmMapIoSpace(PhysicalAddress, 2 * PAGE_SIZE, MmNonCached);
            else
                /* Phase 0: Use Mm */
                Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);

            /* Check if we mapped it */
            if (!Header)
            {
                DPRINT1("HAL: Failed to map ACPI table.\n");
                return NULL;
            }

            /* We found it, break out */
            DPRINT("Found ACPI table %c%c%c%c at 0x%p\n",
                    Header->Signature & 0xFF,
                    (Header->Signature & 0xFF00) >> 8,
                    (Header->Signature & 0xFF0000) >> 16,
                    (Header->Signature & 0xFF000000) >> 24,
                    Header);

            if (Header->Signature == Signature)
                break;
        }

        /* Did we end up here back at the last entry? */
        if (CurrentEntry == EntryCount)
        {
            /* Yes, unmap the last table we processed */
            if (LoaderBlock)
                HalpUnmapVirtualAddress(Header, 2);
            else
                MmUnmapIoSpace(Header, 2 * PAGE_SIZE);

            return NULL;
        }
    }

    /* Past this point, we assume something was found */
    ASSERT(Header);

    /* How many pages do we need? */
    PageCount = BYTES_TO_PAGES(Header->Length);
    if (PageCount != 2)
    {
        /* We assumed two, but this is not the case, free the current mapping */
        if (LoaderBlock)
            HalpUnmapVirtualAddress(Header, 2);
        else
            MmUnmapIoSpace(Header, 2 * PAGE_SIZE);

        /* Now map this table using its correct size */

        if (!LoaderBlock)
            /* Phase 1: Use HAL heap */
            Header = MmMapIoSpace(PhysicalAddress, PageCount << PAGE_SHIFT, MmNonCached);
        else
            /* Phase 0: Use Mm */
            Header = HalpMapPhysicalMemory64(PhysicalAddress, PageCount);
    }

    /* Fail if the remapped failed */
    if (!Header)
        return NULL;

    /* All tables in ACPI 3.0 other than the FACP should have correct checksum */
    if ((Header->Signature == FADT_SIGNATURE) && (Header->Revision < 3))
        return Header;

    /* Go to the end of the table */
    CheckSum = 0;
    CurrentByte = (PCHAR)Header + Header->Length;

    while (CurrentByte-- != (PCHAR)Header)
        CheckSum += *CurrentByte;

    /* The correct checksum is always 0, anything else is illegal */
    if (CheckSum)
    {
        HalpInvalidAcpiTable = Header->Signature;
        DPRINT1("Checksum failed on ACPI table %c%c%c%c\n",
                (Signature & 0xFF),
                (Signature & 0xFF00) >> 8,
                (Signature & 0xFF0000) >> 16,
                (Signature & 0xFF000000) >> 24);
    }

    /* Return the table */
    return Header;
}

PVOID
NTAPI
HalpAcpiGetTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                 IN ULONG Signature)
{
    PFN_COUNT PageCount;
    PDESCRIPTION_HEADER TableAddress, BiosCopy;

    //DPRINT("HalpAcpiGetTable: Signature %X\n", Signature);

    /* See if we have a cached table? */
    TableAddress = HalpAcpiGetCachedTable(Signature);
    if (TableAddress)
        return TableAddress;

    /* No cache, search the BIOS */
    TableAddress = HalpAcpiGetTableFromBios(LoaderBlock, Signature);
    if (!TableAddress)
        return NULL;

    /* Found it, copy it into our own memory */
    BiosCopy = HalpAcpiCopyBiosTable(LoaderBlock, TableAddress);

    /* Get the pages, and unmap the BIOS copy */
    PageCount = BYTES_TO_PAGES(TableAddress->Length);

    if (LoaderBlock)
        /* Phase 0, use the HAL heap */
        HalpUnmapVirtualAddress(TableAddress, PageCount);
    else
        /* Phase 1, use Mm */
        MmUnmapIoSpace(TableAddress, PageCount << PAGE_SHIFT);

    /* Cache the bios copy */
    TableAddress = BiosCopy;
    if (BiosCopy)
        HalpAcpiCacheTable(BiosCopy);

    return TableAddress;
}

INIT_FUNCTION
PVOID
NTAPI
HalAcpiGetTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                IN ULONG Signature)
{
    PDESCRIPTION_HEADER TableHeader;
    NTSTATUS Status;

    DPRINT("HalAcpiGetTable: Signature %X\n", Signature);

    /* Is this phase0 */
    if (LoaderBlock)
    {
        /* Initialize the cache first */
        Status = HalpAcpiTableCacheInit(LoaderBlock);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HalAcpiGetTable: Status %X\n", Status);
            return NULL;
        }
    }
    else
    {
        /* Lock the cache */
        ExAcquireFastMutex(&HalpAcpiTableCacheLock);
    }

    /* Get the table */
    TableHeader = HalpAcpiGetTable(LoaderBlock, Signature);

    /* Release the lock in phase 1 */
    if (!LoaderBlock)
        ExReleaseFastMutex(&HalpAcpiTableCacheLock);

    /* Return the table */
    return TableHeader;
}

INIT_FUNCTION
VOID
NTAPI
HalpCheckPowerButton(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

VOID
NTAPI
HaliHaltSystem(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
}

INIT_FUNCTION
VOID
NTAPI
HalpGetNMICrashFlag(VOID)
{
    UNICODE_STRING ValueName;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\CrashControl");
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG ResultLength;
    HANDLE Handle;
    NTSTATUS Status;
    KEY_VALUE_PARTIAL_INFORMATION KeyValueInformation;

    /* Set default */
    HalpNMIDumpFlag = 0;

    /* Initialize attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    /* Open crash key */
    Status = ZwOpenKey(&Handle, KEY_READ, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        /* Query key value */
        RtlInitUnicodeString(&ValueName, L"NMICrashDump");
        Status = ZwQueryValueKey(Handle,
                                 &ValueName,
                                 KeyValuePartialInformation,
                                 &KeyValueInformation,
                                 sizeof(KeyValueInformation),
                                 &ResultLength);
        if (NT_SUCCESS(Status))
        {
            /* Check for valid data */
            if (ResultLength == sizeof(KEY_VALUE_PARTIAL_INFORMATION))
            {
                /* Read the flag */
                HalpNMIDumpFlag = KeyValueInformation.Data[0];
            }
        }

        /* We're done */
        ZwClose(Handle);
    }
}

INIT_FUNCTION
VOID
NTAPI
HalpInitializePciBus(VOID)
{
    /* Setup the PCI stub support */
    HalpInitializePciStubs();

    /* Set the NMI crash flag */
    HalpGetNMICrashFlag();
}

/BOOLEAN
NTAPI
HalpGetDebugPortTable(VOID)
{
    DPRINT("HalpGetDebugPortTable()\n");
    if (!HalpDebugPortTable)
        return FALSE;

    return (HalpDebugPortTable->BaseAddress.AddressSpaceID == 1);
}

* PUBLIC FUNCTIONS **********************************************************/

INIT_FUNCTION
VOID
NTAPI
HalReportResourceUsage(VOID)
{
    INTERFACE_TYPE InterfaceType;
    UNICODE_STRING HalString;

    DPRINT("HalReportResourceUsage()\n");

    /* FIXME: HalpDmaFinalizeDoubleBufferingDisposition() */
    /* FIXME: Initialize DMA 64-bit support */
    /* FIXME: Initialize MCA bus */

    /* Initialize PCI bus. */
    HalpInitializePciBus();

    /* What kind of bus is this? */
    switch (HalpBusType)
    {
        /* ISA Machine */
        case MACHINE_TYPE_ISA:
            InterfaceType = Isa;
            break;

        /* EISA Machine */
        case MACHINE_TYPE_EISA:
            InterfaceType = Eisa;
            break;

        /* MCA Machine */
        case MACHINE_TYPE_MCA:
            InterfaceType = MicroChannel;
            break;

        /* Unknown */
        default:
            InterfaceType = Internal;
            break;
    }

    /* Build HAL usage */
    RtlInitUnicodeString(&HalString, HalName);
    HalpReportResourceUsage(&HalString, InterfaceType);

    /* Setup PCI debugging and Hibernation */
    HalpRegisterPciDebuggingDeviceInfo();
}

ULONG
NTAPI
HalSetTimeIncrement(IN ULONG Increment)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return 0;
}

/* EOF */
