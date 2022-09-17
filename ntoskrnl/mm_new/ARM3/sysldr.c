
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

/* Mask for image section page protection */
#define IMAGE_SCN_PROTECTION_MASK (IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE)

LIST_ENTRY PsLoadedModuleList;
LIST_ENTRY MmLoadedUserImageList;
ERESOURCE PsLoadedModuleResource;
ULONG_PTR PsNtosImageBase;
KMUTANT MmSystemLoadLock;
KSPIN_LOCK PsLoadedModuleSpinLock;
PMMPTE MiKernelResourceStartPte;
PMMPTE MiKernelResourceEndPte;
PVOID MmUnloadedDrivers;
PVOID MmLastUnloadedDrivers;
ULONG_PTR ExPoolCodeStart;
ULONG_PTR ExPoolCodeEnd;
ULONG_PTR MmPoolCodeStart;
ULONG_PTR MmPoolCodeEnd;
ULONG_PTR MmPteCodeStart;
ULONG_PTR MmPteCodeEnd;
BOOLEAN MmMakeLowMemory;
BOOLEAN MmEnforceWriteProtection = FALSE; // FIXME: should be TRUE, but that would cause CORE-16387 & CORE-16449

extern ULONG MmTotalFreeSystemPtes[MaximumPtePoolTypes];
extern SIZE_T MmDriverCommit;
extern PFN_NUMBER MmAvailablePages;
extern ULONG MmSecondaryColorMask;
extern MMPTE DemandZeroPte;
extern BOOLEAN MiLargePageAllDrivers;
extern LIST_ENTRY MiLargePageDriverList;

/* FUNCTIONS ******************************************************************/

static
inline
VOID
sprintf_nt(
    _In_ PCHAR Buffer,
    _In_ PCHAR Format,
    _In_ ...)
{
    va_list ap;
    va_start(ap, Format);
    vsprintf(Buffer, Format, ap);
    va_end(ap);
}

PFN_NUMBER
NTAPI
MiAllocatePfn(
    _In_ PMMPTE Pte,
    _In_ ULONG Protection)
{
    PFN_NUMBER PageFrameNumber;
    ULONG Color;
    KIRQL OldIrql;

    DPRINT("MiAllocatePfn: Pte %p, Protection %X\n", Pte, Protection);

    /* Lock the PFN database */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    if (MmAvailablePages < 0x80)
    {
        DPRINT1("MiAllocatePfn: FIXME MiEnsureAvailablePageOrWait()\n");
        ASSERT(FALSE);
    }

    Color = MI_GET_NEXT_COLOR();
    PageFrameNumber = MiRemoveAnyPage(Color);

    MI_WRITE_INVALID_PTE(Pte, DemandZeroPte);
    Pte->u.Soft.Protection |= Protection;

    MiInitializePfn(PageFrameNumber, Pte, TRUE);

    /* Release the PFN lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    return PageFrameNumber;
}

NTSTATUS
NTAPI
MmUnloadSystemImage(
    _In_ PVOID ImageHandle)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MiSnapThunk(
    _In_ PVOID DllBase,
    _In_ PVOID ImageBase,
    _In_ PIMAGE_THUNK_DATA Name,
    _In_ PIMAGE_THUNK_DATA Address,
    _In_ PIMAGE_EXPORT_DIRECTORY ExportDirectory,
    _In_ ULONG ExportSize,
    _In_ BOOLEAN SnapForwarder,
    _Out_ PCHAR* MissingApi)
{
    CHAR NameBuffer[MAXIMUM_FILENAME_LENGTH];
    IMAGE_THUNK_DATA ForwardThunk;
    PIMAGE_EXPORT_DIRECTORY ForwardExportDirectory;
    PIMAGE_IMPORT_BY_NAME NameImport;
    PIMAGE_IMPORT_BY_NAME ForwardName;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLIST_ENTRY NextEntry;
    PULONG NameTable;
    PULONG ExportTable;
    PUSHORT OrdinalTable;
    PCHAR MissingForwarder;
    ANSI_STRING DllName;
    UNICODE_STRING ForwarderName;
    SIZE_T ForwardLength;
    ULONG ForwardExportSize;
    ULONG Low = 0;
    ULONG Mid = 0;
    ULONG High;
    LONG Ret;
    USHORT Ordinal;
    USHORT Hint;
    BOOLEAN IsOrdinal;
    NTSTATUS Status;

    PAGED_CODE();

    /* Check if this is an ordinal */
    IsOrdinal = IMAGE_SNAP_BY_ORDINAL(Name->u1.Ordinal);

    if (IsOrdinal && !SnapForwarder)
    {
        /* Get the ordinal number and set it as missing */
        Ordinal = (USHORT)(IMAGE_ORDINAL(Name->u1.Ordinal) - ExportDirectory->Base);
        *MissingApi = (PCHAR)(ULONG_PTR)Ordinal;
    }
    else
    {
        /* Get the VA if we don't have to snap */
        if (!SnapForwarder)
            Name->u1.AddressOfData += (ULONG_PTR)ImageBase;

        NameImport = (PIMAGE_IMPORT_BY_NAME)Name->u1.AddressOfData;

        /* Copy the procedure name */
        RtlStringCbCopyA(*MissingApi, MAXIMUM_FILENAME_LENGTH, (PCHAR)&NameImport->Name[0]);

        DPRINT("Import name: %s\n", NameImport->Name);

        /* Setup name tables */
        NameTable = (PULONG)((ULONG_PTR)DllBase + ExportDirectory->AddressOfNames);
        OrdinalTable = (PUSHORT)((ULONG_PTR)DllBase + ExportDirectory->AddressOfNameOrdinals);

        /* Get the hint and check if it's valid */
        Hint = NameImport->Hint;

        if (Hint < ExportDirectory->NumberOfNames &&
            !(strcmp((PCHAR)NameImport->Name, (PCHAR)DllBase + NameTable[Hint])))
        {
            /* We have a match, get the ordinal number from here */
            Ordinal = OrdinalTable[Hint];
        }
        else
        {
            /* Do a binary search */
            High = (ExportDirectory->NumberOfNames - 1);

            while (High >= Low)
            {
                /* Get new middle value */
                Mid = ((Low + High) >> 1);

                /* Compare name */
                Ret = strcmp((PCHAR)NameImport->Name, (PCHAR)DllBase + NameTable[Mid]);

                if (Ret < 0)
                    /* Update high */
                    High = (Mid - 1);
                else if (Ret > 0)
                    /* Update low */
                    Low = (Mid + 1);
                else
                    /* We got it */
                    break;
            }

            /* Check if we couldn't find it */
            if (High < Low)
            {
                DPRINT1("Warning: Driver failed to load, %s not found\n", NameImport->Name);
                return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
            }

            /* Otherwise, this is the ordinal */
            Ordinal = OrdinalTable[Mid];
        }
    }

    /* Check if the ordinal is invalid */
    if (Ordinal >= ExportDirectory->NumberOfFunctions)
    {
        DPRINT1("MiSnapThunk: STATUS_DRIVER_ORDINAL_NOT_FOUND\n");
        return STATUS_DRIVER_ORDINAL_NOT_FOUND;
    }

    /* In case the forwarder is missing */
    MissingForwarder = NameBuffer;

    /* Resolve the address and write it */
    ExportTable = (PULONG)((ULONG_PTR)DllBase +ExportDirectory->AddressOfFunctions);
    Address->u1.Function = (ULONG_PTR)DllBase + ExportTable[Ordinal];

    /* Check if the function is actually a forwarder */
    if (Address->u1.Function <= (ULONG_PTR)ExportDirectory ||
        Address->u1.Function >= ((ULONG_PTR)ExportDirectory + ExportSize))
    {
        return STATUS_SUCCESS;
    }

    /* Now assume failure in case the forwarder doesn't exist */
    Status = STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;

    /* Build the forwarder name */
    DllName.Length = (USHORT)((strchr(DllName.Buffer, '.') - DllName.Buffer) + sizeof(ANSI_NULL));
    DllName.MaximumLength = DllName.Length;
    DllName.Buffer = (PCHAR)Address->u1.Function;

    /* Convert it */
    if (!NT_SUCCESS(RtlAnsiStringToUnicodeString(&ForwarderName, &DllName, TRUE)))
    {
        /* We failed, just return an error */
        DPRINT1("MiSnapThunk: return Status %X\n", Status);
        return Status;
    }

    /* Loop the module list */
    for (NextEntry = PsLoadedModuleList.Flink;
         NextEntry != &PsLoadedModuleList;
         NextEntry = NextEntry->Flink)
    {
        /* Get the loader entry */
        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        /* Check if it matches */
        if (RtlPrefixUnicodeString(&ForwarderName, &LdrEntry->BaseDllName, TRUE))
        {
            /* Get the forwarder export directory */
            ForwardExportDirectory = RtlImageDirectoryEntryToData(LdrEntry->DllBase,
                                                                  TRUE,
                                                                  IMAGE_DIRECTORY_ENTRY_EXPORT,
                                                                  &ForwardExportSize);
            if (!ForwardExportDirectory)
                break;

            /* Allocate a name entry */
            ForwardLength = (strlen(DllName.Buffer + DllName.Length) + sizeof(ANSI_NULL));

            ForwardName = ExAllocatePoolWithTag(PagedPool, (sizeof(*ForwardName) + ForwardLength), TAG_LDR_WSTR);
            if (!ForwardName)
            {
                DPRINT1("MiSnapThunk: ExAllocatePoolWithTag() failed\n");
                break;
            }

            /* Copy the data */
            RtlCopyMemory(&ForwardName->Name[0], (DllName.Buffer + DllName.Length), ForwardLength);
            ForwardName->Hint = 0;

            /* Set the new address */
            ForwardThunk.u1.AddressOfData = (ULONG_PTR)ForwardName;

            /* Snap the forwarder */
            Status = MiSnapThunk(LdrEntry->DllBase,
                                 ImageBase,
                                 &ForwardThunk,
                                 &ForwardThunk,
                                 ForwardExportDirectory,
                                 ForwardExportSize,
                                 TRUE,
                                 &MissingForwarder);

            /* Free the forwarder name and set the thunk */
            ExFreePoolWithTag(ForwardName, TAG_LDR_WSTR);
            Address->u1 = ForwardThunk.u1;

            break;
        }
    }

    /* Free the name */
    RtlFreeUnicodeString(&ForwarderName);

    /* Return status */
    return Status;
}

BOOLEAN
NTAPI
MiCallDllUnloadAndUnloadDll(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
MiDereferenceImports(
    _In_ PLOAD_IMPORTS ImportList)
{
    LOAD_IMPORTS SingleEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PVOID CurrentImports;
    SIZE_T ix;

    PAGED_CODE();

    /* Check if there's no imports or if we're a boot driver */
    if (ImportList == MM_SYSLDR_NO_IMPORTS ||
        ImportList == MM_SYSLDR_BOOT_LOADED ||
        !ImportList->Count)
    {
        /* Then there's nothing to do */
        return STATUS_SUCCESS;
    }

    /* Check for single-entry */
    if ((ULONG_PTR)ImportList & MM_SYSLDR_SINGLE_ENTRY)
    {
        /* Set it up */
        SingleEntry.Count = 1;
        SingleEntry.Entry[0] = (PVOID)((ULONG_PTR)ImportList &~ MM_SYSLDR_SINGLE_ENTRY);

        /* Use this as the import list */
        ImportList = &SingleEntry;
    }

    /* Loop the import list */
    for (ix = 0; ix < ImportList->Count && ImportList->Entry[ix]; ix++)
    {
        /* Get the entry */
        LdrEntry = ImportList->Entry[ix];
        DPRINT1("%wZ <%wZ>\n", &LdrEntry->FullDllName, &LdrEntry->BaseDllName);

        /* Skip boot loaded images */
        if (LdrEntry->LoadedImports == MM_SYSLDR_BOOT_LOADED)
            continue;

        /* Dereference the entry */
        ASSERT(LdrEntry->LoadCount >= 1);

        if (--LdrEntry->LoadCount)
            continue;

        /* Save the import data in case unload fails */
        CurrentImports = LdrEntry->LoadedImports;

        /* This is the last entry */
        LdrEntry->LoadedImports = MM_SYSLDR_NO_IMPORTS;

        if (MiCallDllUnloadAndUnloadDll(LdrEntry))
        {
            /* Unloading worked, parse this DLL's imports too */
            MiDereferenceImports(CurrentImports);

            /* Check if we had valid imports */
            if (CurrentImports != MM_SYSLDR_BOOT_LOADED &&
                CurrentImports != MM_SYSLDR_NO_IMPORTS &&
                !((ULONG_PTR)CurrentImports & MM_SYSLDR_SINGLE_ENTRY))
            {
                /* Free them */
                ExFreePoolWithTag(CurrentImports, TAG_LDR_IMPORTS);
            }
        }
        else
        {
            /* Unload failed, restore imports */
            LdrEntry->LoadedImports = CurrentImports;
        }
    }

    /* Done */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MiResolveImageReferences(
    _In_ PVOID ImageBase,
    _In_ PUNICODE_STRING ImageFileDirectory,
    _In_ PUNICODE_STRING NamePrefix OPTIONAL,
    _Out_ PCHAR* MissingApi,
    _Out_ PWCHAR* MissingDriver,
    _Out_ PLOAD_IMPORTS* LoadImports)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MiLoadImageSection(
    _Inout_ PVOID* OutSection,
    _Out_ PVOID* OutBaseAddress,
    _In_ PUNICODE_STRING FileName,
    _In_ BOOLEAN IsSessionLoad,
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PSECTION Section = *OutSection;
    PSEGMENT Segment = Section->Segment;
    PFN_COUNT PteCount = Segment->TotalNumberOfPtes;
    LARGE_INTEGER SectionOffset = {{0, 0}};
    PCONTROL_AREA ControlArea;
    PSUBSECTION Subsection;
    PEPROCESS Process;
    PMMPTE Pte;
    PMMPTE LastPte;
    PMMPTE Proto;
    PMMPTE LastProto;
    PVOID MapBase = NULL;
    PVOID BaseAddress;
    PVOID CurrentVa;
    MMPTE TempPte = ValidKernelPte;
    SIZE_T ViewSize = 0;
    PFN_NUMBER PageFrameIndex;
    ULONG Count = 0;
    KAPC_STATE ApcState;
    BOOLEAN IsLoadSymbols = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("MiLoadImageSection: Section %p, FileName %wZ\n", *OutSection, FileName);

    /* Detect session load */
    if (IsSessionLoad)
    {
        UNIMPLEMENTED_DBGBREAK("Session loading not yet supported!\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    /* Not session load, shouldn't have an entry */
    ASSERT(LdrEntry == NULL);

    DPRINT("MiLoadImageSection: FIXME MiChargeResidentAvailable\n");

    /* Reserve system PTEs needed */
    Pte = MiReserveSystemPtes(PteCount, SystemPteSpace);
    if (!Pte)
    {
        DPRINT1("MiLoadImageSection: MiReserveSystemPtes failed\n");
        ASSERT(FALSE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* New driver base */
    BaseAddress = MiPteToAddress(Pte);

    DPRINT("MiLoadImageSection: FIXME MiChargeCommitment \n");

    InterlockedExchangeAdd((PLONG)&MmDriverCommit, PteCount);

    /* Attach to the system process */
    KeStackAttachProcess(&PsInitialSystemProcess->Pcb, &ApcState);

    /* Check if we need to load symbols */
    if (NtGlobalFlag & FLG_ENABLE_KDEBUG_SYMBOL_LOAD)
    {
        /* Yes we do */
        IsLoadSymbols = TRUE;
        NtGlobalFlag &= ~FLG_ENABLE_KDEBUG_SYMBOL_LOAD;
    }

    /* Map the driver */
    Process = PsGetCurrentProcess();

    Status = MmMapViewOfSection(Section,
                                Process,
                                &MapBase,
                                0,
                                0,
                                &SectionOffset,
                                &ViewSize,
                                ViewUnmap,
                                0,
                                PAGE_EXECUTE);
    /* Re-enable the flag */
    if (IsLoadSymbols)
        NtGlobalFlag |= FLG_ENABLE_KDEBUG_SYMBOL_LOAD;

    /* Check if we failed with distinguished status code */
    if (Status == STATUS_IMAGE_MACHINE_TYPE_MISMATCH)
        /* Change it to something more generic */
        Status = STATUS_INVALID_IMAGE_FORMAT;

    /* Now check if we failed */
    if (!NT_SUCCESS(Status))
    {
        /* Detach and return */
        DPRINT1("MmMapViewOfSection failed with status 0x%X\n", Status);
        KeUnstackDetachProcess(&ApcState);

        ASSERT((SSIZE_T)(PteCount) >= 0);
        MiReleaseSystemPtes(Pte, PteCount, SystemPteSpace);

        return Status;
    }

    ControlArea = Segment->ControlArea;

    if (ControlArea->u.Flags.GlobalOnlyPerSession || ControlArea->u.Flags.Rom)
        Subsection = (PSUBSECTION)((PLARGE_CONTROL_AREA)ControlArea + 1);
    else
        Subsection = (PSUBSECTION)(ControlArea + 1);

    DPRINT("MiLoadImageSection: Segment %p, ControlArea %p\n", Segment, ControlArea);

    ASSERT(Subsection->SubsectionBase != NULL);

    Proto = Subsection->SubsectionBase;
    LastProto = &Subsection->SubsectionBase[Subsection->PtesInSubsection];

    //DPRINT("MiLoadImageSection: Proto %p [%p], LastProto %p\n", Proto, (Proto?Proto->u.Long:0), LastProto);

    /* The driver is here */
    *OutBaseAddress = BaseAddress;
    CurrentVa = MapBase;

    DPRINT1("MiLoadImageSection: %wZ at %p with %X pages\n", FileName, BaseAddress, PteCount);

    LastPte = (Pte + PteCount);

    /* Loop the new driver PTEs */
    for (; Pte < LastPte; Pte++, Proto++)
    {
        //DPRINT("MiLoadImageSection: Pte %p, [%X:%X]\n", Pte, PteCount, Count);
        //DPRINT("MiLoadImageSection: Proto %p, LastProto %p\n", Proto, LastProto);

        if (Proto >= LastProto)
        {
            ASSERT(Subsection->NextSubsection != NULL);

            //DPRINT("MiLoadImageSection: Subsection %p, Subsection->NextSubsection %p\n", Subsection, Subsection->NextSubsection);
            Subsection = Subsection->NextSubsection;

            //DPRINT("MiLoadImageSection: UnusedPtes %X, PtesInSubsection %X\n", Subsection->UnusedPtes, Subsection->PtesInSubsection);
            Proto = Subsection->SubsectionBase;
            LastProto = &Proto[Subsection->PtesInSubsection];
            //DPRINT("MiLoadImageSection: Proto %p, LastProto %p\n", Proto, LastProto);
        }

        if (Proto->u.Hard.Valid || Proto->u.Soft.Protection != 0x18)
        {
            Count++;

            /* Grab a page */
            PageFrameIndex = MiAllocatePfn(Pte, MM_EXECUTE);

            /* Write the PTE */
            TempPte.u.Hard.PageFrameNumber = PageFrameIndex;
            MI_WRITE_VALID_PTE(Pte, TempPte);

            ASSERT(MI_PFN_ELEMENT(PageFrameIndex)->u1.WsIndex == 0);

            _SEH2_TRY
            {
                /* Copy the image */
                RtlCopyMemory(BaseAddress, CurrentVa, PAGE_SIZE);
            }
            _SEH2_EXCEPT (EXCEPTION_EXECUTE_HANDLER)
            {
                DPRINT1("MiLoadImageSection: FIXME\n");
                ASSERT(FALSE);
                return _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        else
        {
            Pte->u.Long = 0;
        }

        /* Move on */
        BaseAddress = (PVOID)((ULONG_PTR)BaseAddress + PAGE_SIZE);
        CurrentVa = (PVOID)((ULONG_PTR)CurrentVa + PAGE_SIZE);
    }

    /* Now unmap the view */
    Status = MiUnmapViewOfSection(Process, MapBase, 0);
    ASSERT(NT_SUCCESS(Status));

    MmPurgeSection(ControlArea->FilePointer->SectionObjectPointer, NULL, 0, FALSE);

    /* Detach and return status */
    KeUnstackDetachProcess(&ApcState);

    if (PteCount != Count)
    {
        ASSERT(PteCount > Count);

        DPRINT1("MiLoadImageSection: PteCount %X, Count %X\n", PteCount, Count);
        ASSERT(FALSE);
    }

    return Status;
}

VOID
NTAPI
MiProcessLoaderEntry(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ BOOLEAN Insert)
{
    UNIMPLEMENTED_DBGBREAK();
}

BOOLEAN
NTAPI
MiUseLargeDriverPage(
    _In_ ULONG NumberOfPtes,
    _Inout_ PVOID* ImageBaseAddress,
    _In_ PUNICODE_STRING BaseImageName,
    _In_ BOOLEAN BootDriver)
{
    PMI_LARGE_PAGE_DRIVER_ENTRY LargePageDriverEntry;
    PLIST_ENTRY NextEntry;
    BOOLEAN DriverFound = FALSE;

    ASSERT(KeGetCurrentIrql () <= APC_LEVEL);
    ASSERT(*ImageBaseAddress >= MmSystemRangeStart);

#ifdef _X86_
    if (!(KeFeatureBits & KF_LARGE_PAGE))
        return FALSE;

    if (!(__readcr4() & CR4_PSE))
        return FALSE;
#endif

    /* Make sure there's enough system PTEs for a large page driver */
    if (MmTotalFreeSystemPtes[SystemPteSpace] < (16 * (PDE_MAPPED_VA / PAGE_SIZE)))
    {
        return FALSE;
    }

    /* This happens if the registry key had a "*" (wildcard) in it */
    if (MiLargePageAllDrivers)
    {
        /* Nothing to do yet */
        DPRINT1("Large pages not supported!\n");
        return FALSE;
    }

    /* It didn't, so scan the list */
    for (NextEntry = MiLargePageDriverList.Flink;
         NextEntry != &MiLargePageDriverList;
         NextEntry = NextEntry->Flink)
    {
        /* Check if the driver name matches */
        LargePageDriverEntry = CONTAINING_RECORD(NextEntry, MI_LARGE_PAGE_DRIVER_ENTRY, Links);

        if (RtlEqualUnicodeString(BaseImageName, &LargePageDriverEntry->BaseName, TRUE))
        {
            /* Enable large pages for this driver */
            DriverFound = TRUE;
            break;
        }
    }

    /* If we didn't find the driver, it doesn't need large pages */
    if (!DriverFound)
        return FALSE;

    /* Nothing to do yet */
    DPRINT1("Large pages not supported!\n");
    return FALSE;
}

VOID
NTAPI
MiEnablePagingOfDriver(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmLoadSystemImage(
    _In_ PUNICODE_STRING FileName,
    _In_ PUNICODE_STRING NamePrefix OPTIONAL,
    _In_ PUNICODE_STRING LoadedName OPTIONAL,
    _In_ ULONG Flags,
    _Out_ PVOID* ModuleObject,
    _Out_ PVOID* ImageBaseAddress)
{
    PLOAD_IMPORTS LoadedImports = MM_SYSLDR_NO_IMPORTS;
    PLDR_DATA_TABLE_ENTRY LdrEntry = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING BaseName;
    UNICODE_STRING BaseDirectory;
    UNICODE_STRING PrefixName;
    UNICODE_STRING UnicodeTemp;
    STRING AnsiTemp;
    IMAGE_INFO ImageInfo;
    PSECTION Section = NULL;
    PIMAGE_NT_HEADERS NtHeader;
    PWCHAR MissingDriverName;
    PCHAR MissingApiName;
    PLIST_ENTRY NextEntry;
    PCHAR Buffer;
    PVOID ModuleLoadBase = NULL;
    HANDLE FileHandle = NULL;
    HANDLE SectionHandle;
    ACCESS_MASK DesiredAccess;
    ULONG TotalNumberOfPtes;
    ULONG EntrySize;
    BOOLEAN NeedToFreeString = FALSE;
    BOOLEAN IsLoadedBefore = FALSE;
    BOOLEAN LockOwned = FALSE;
    BOOLEAN IsLoadOk = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("MmLoadSystemImage: '%wZ', '%wZ', '%wZ', Flags %X\n", FileName, NamePrefix, LoadedName, Flags);

    /* Detect session-load */
    if (Flags)
    {
        /* Sanity checks */
        ASSERT(NamePrefix == NULL);
        ASSERT(LoadedName == NULL);

        /* Make sure the process is in session too */
        if (!PsGetCurrentProcess()->ProcessInSession)
        {
            DPRINT1("MmLoadSystemImage: STATUS_NO_MEMORY\n");
            return STATUS_NO_MEMORY;
        }
    }

    /* Allocate a buffer we'll use for names */
    Buffer = ExAllocatePoolWithTag(NonPagedPool, MAXIMUM_FILENAME_LENGTH, TAG_LDR_WSTR);
    if (!Buffer)
    {
        DPRINT1("MmLoadSystemImage: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Check for a separator */
    if (FileName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        PWCHAR p;
        ULONG BaseLength;

        /* Loop the path until we get to the base name */
        p = &FileName->Buffer[FileName->Length / sizeof(WCHAR)];

        while (*(p - 1) != OBJ_NAME_PATH_SEPARATOR)
            p--;

        /* Get the length */
        BaseLength = (ULONG)(&FileName->Buffer[FileName->Length / sizeof(WCHAR)] - p);
        BaseLength *= sizeof(WCHAR);

        /* Setup the string */
        BaseName.Length = (USHORT)BaseLength;
        BaseName.Buffer = p;
    }
    else
    {
        /* Otherwise, we already have a base name */
        BaseName.Length = FileName->Length;
        BaseName.Buffer = FileName->Buffer;
    }

    /* Setup the maximum length */
    BaseName.MaximumLength = BaseName.Length;

    /* Now compute the base directory */
    BaseDirectory = *FileName;
    BaseDirectory.Length -= BaseName.Length;
    BaseDirectory.MaximumLength = BaseDirectory.Length;

    /* And the prefix, which for now is just the name itself */
    PrefixName = *FileName;

    /* Check if we have a prefix */
    if (NamePrefix)
    {
        DPRINT1("Prefixed images are not yet supported!\n");
    }

    /* Check if we already have a name, use it instead */
    if (LoadedName)
        BaseName = *LoadedName;

    /* Check for loader snap debugging */
    if (NtGlobalFlag & FLG_SHOW_LDR_SNAPS)
    {
        /* Print out standard string */
        DPRINT1("MM:SYSLDR Loading %wZ (%wZ) %s\n", &PrefixName, &BaseName, Flags ? "in session space" : "");
    }

LoaderScan:

    /* Acquire the load lock */
    ASSERT(LockOwned == FALSE);
    LockOwned = TRUE;
    KeEnterCriticalRegion();

    KeWaitForSingleObject(&MmSystemLoadLock, WrVirtualMemory, KernelMode, FALSE, NULL);

    DPRINT("MmLoadSystemImage: PrefixName '%wZ'\n", &PrefixName);

    /* Scan the module list */
    for (NextEntry = PsLoadedModuleList.Flink;
         NextEntry != &PsLoadedModuleList;
         NextEntry = NextEntry->Flink)
    {
        /* Get the entry and compare the names */
        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if (RtlEqualUnicodeString(&PrefixName, &LdrEntry->FullDllName, TRUE))
            /* Found it, break out */
            break;
    }

    /* Check if we found the image */
    if (NextEntry != &PsLoadedModuleList)
    {
        DPRINT("MmLoadSystemImage: Section %p\n", Section);

        DPRINT1("MmLoadSystemImage: FIXME\n");
        ASSERT(FALSE);
    }
    else if (!Section)
    {
        DPRINT("MmLoadSystemImage: Section %p\n", Section);

        /* It wasn't loaded, and we didn't have a previous attempt */
        KeReleaseMutant(&MmSystemLoadLock, 1, FALSE, FALSE);
        KeLeaveCriticalRegion();
        LockOwned = FALSE;

        /* Check if KD is enabled */
        if (KdDebuggerEnabled && !KdDebuggerNotPresent)
        {
            DPRINT1("MmLoadSystemImage: FIXME: Attempt to get image from KD\n");
        }

        /* We don't have a valid entry */
        LdrEntry = NULL;

        /* Setup image attributes */
        InitializeObjectAttributes(&ObjectAttributes,
                                   FileName,
                                   (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
                                   NULL,
                                   NULL);

        DPRINT("MmLoadSystemImage: '%wZ'\n", FileName);

        /* Open the image */
        Status = ZwOpenFile(&FileHandle,
                            FILE_EXECUTE,
                            &ObjectAttributes,
                            &IoStatusBlock,
                            (FILE_SHARE_READ | FILE_SHARE_DELETE),
                            0);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MmLoadSystemImage: '%wZ', %X\n", FileName, Status);
            goto Exit;
        }

        /* Validate it */
        Status = MmCheckSystemImage(FileHandle, FALSE);

        if (Status == STATUS_IMAGE_CHECKSUM_MISMATCH ||
            Status == STATUS_IMAGE_MP_UP_MISMATCH ||
            Status == STATUS_INVALID_IMAGE_PROTECT)
        {
            /* Fail loading */
            DPRINT1("MmLoadSystemImage: Status %X\n", Status);
            goto Exit1;
        }

        /* Check if this is a session-load */
        if (Flags)
            /* Then we only need read and execute */
            DesiredAccess = (SECTION_MAP_READ | SECTION_MAP_EXECUTE);
        else
            /* Otherwise, we can allow write access */
            DesiredAccess = SECTION_ALL_ACCESS;

        /* Initialize the attributes for the section */
        InitializeObjectAttributes(&ObjectAttributes,
                                   NULL,
                                   (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
                                   NULL,
                                   NULL);

        /* Create the section */
        Status = ZwCreateSection(&SectionHandle,
                                 DesiredAccess,
                                 &ObjectAttributes,
                                 NULL,
                                 PAGE_EXECUTE,
                                 SEC_IMAGE,
                                 FileHandle);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MmLoadSystemImage: Status %X\n", Status);
            goto Exit1;
        }

        /* Now get the section pointer */
        Status = ObReferenceObjectByHandle(SectionHandle,
                                           SECTION_MAP_EXECUTE,
                                           MmSectionObjectType,
                                           KernelMode,
                                           (PVOID *)&Section,
                                           NULL);
        ZwClose(SectionHandle);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MmLoadSystemImage: Status %X\n", Status);
            goto Exit1;
        }

        /* Check if this was supposed to be a session-load */
        if (Flags && !Section->Segment->ControlArea->u.Flags.FloppyMedia)
        {
            /* We don't support session loading yet */
            UNIMPLEMENTED_DBGBREAK("Unsupported Session-Load!\n");
        }

        /* Check the loader list again, we should end up in the path below */
        DPRINT("MmLoadSystemImage: goto LoaderScan\n");
        goto LoaderScan;
    }
    else
    {
        /* We don't have a valid entry */
        LdrEntry = NULL;
    }

    /* Load the image */
    Status = MiLoadImageSection((PVOID *)&Section, &ModuleLoadBase, FileName, Flags, NULL);
    ASSERT(Status != STATUS_ALREADY_COMMITTED);

    /* Get the size of the driver */
    TotalNumberOfPtes = Section->Segment->TotalNumberOfPtes;

    /* Make sure we're not being loaded into session space */
    if (!Flags)
    {
        /* Check for success */
        if (NT_SUCCESS(Status))
            /* Support large pages for drivers */
            MiUseLargeDriverPage(TotalNumberOfPtes, &ModuleLoadBase, &BaseName, TRUE);

        /* Dereference the section */
        ObDereferenceObject(Section);
        Section = NULL;
    }

    /* Check for failure of the load earlier */
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmLoadSystemImage: %X\n", Status);

        if (IsLoadedBefore)
        {
            DPRINT1("MmLoadSystemImage: FIXME\n");
            ASSERT(FALSE);
        }

        goto Exit1;
    }

    IsLoadOk = TRUE;

    if (IsLoadedBefore)
        goto Finish;

    if (!Flags || ModuleLoadBase != Section->Segment->BasedAddress)
    {
        /* Relocate the driver */
        Status = LdrRelocateImageWithBias(ModuleLoadBase,
                                          0,
                                          "SYSLDR",
                                          STATUS_SUCCESS,
                                          STATUS_CONFLICTING_ADDRESSES,
                                          STATUS_INVALID_IMAGE_FORMAT);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MmLoadSystemImage: %X\n", Status);
            goto Exit1;
        }
    }

    /* Get the NT Header */
    NtHeader = RtlImageNtHeader(ModuleLoadBase);

    if (Flags)
    {
        DPRINT1("MmLoadSystemImage: FIXME\n");
        ASSERT(FALSE);
    }

    /* Calculate the size we'll need for the entry and allocate it */
    EntrySize = (sizeof(LDR_DATA_TABLE_ENTRY) + BaseName.Length + sizeof(UNICODE_NULL));

    /* Allocate the entry */
    LdrEntry = ExAllocatePoolWithTag(NonPagedPool, EntrySize, TAG_MODULE_OBJECT);
    if (!LdrEntry)
    {
        DPRINT1("MmLoadSystemImage: %X\n", STATUS_INSUFFICIENT_RESOURCES);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit1;
    }

    /* Setup the entry */
    LdrEntry->Flags = LDRP_LOAD_IN_PROGRESS;
    LdrEntry->LoadCount = 1;
    LdrEntry->LoadedImports = LoadedImports;
    LdrEntry->PatchInformation = NULL;

    /* Check the version */
    if (NtHeader->OptionalHeader.MajorOperatingSystemVersion >= 5 &&
        NtHeader->OptionalHeader.MajorImageVersion >= 5)
    {
        /* Mark this image as a native image */
        LdrEntry->Flags |= LDRP_ENTRY_NATIVE;
    }

    if (Flags)
    {
        DPRINT1("MmLoadSystemImage: FIXME\n");
        ASSERT(FALSE);
    }

    /* Setup the rest of the entry */
    LdrEntry->DllBase = ModuleLoadBase;
    LdrEntry->EntryPoint = (PVOID)((ULONG_PTR)ModuleLoadBase + NtHeader->OptionalHeader.AddressOfEntryPoint);
    LdrEntry->SizeOfImage = NtHeader->OptionalHeader.SizeOfImage;
    LdrEntry->CheckSum = NtHeader->OptionalHeader.CheckSum;
    LdrEntry->SectionPointer = Section;

    /* Now write the DLL name */
    LdrEntry->BaseDllName.Buffer = (PVOID)(LdrEntry + 1);
    LdrEntry->BaseDllName.Length = BaseName.Length;
    LdrEntry->BaseDllName.MaximumLength = BaseName.Length;

    /* Copy and null-terminate it */
    RtlCopyMemory(LdrEntry->BaseDllName.Buffer, BaseName.Buffer, BaseName.Length);
    LdrEntry->BaseDllName.Buffer[BaseName.Length / sizeof(WCHAR)] = UNICODE_NULL;

    /* Now allocate the full name */
    LdrEntry->FullDllName.Buffer = ExAllocatePoolWithTag(PagedPool, (PrefixName.Length + sizeof(UNICODE_NULL)), TAG_LDR_WSTR);

    if (!LdrEntry->FullDllName.Buffer)
    {
        /* Don't fail, just set it to zero */
        LdrEntry->FullDllName.Length = 0;
        LdrEntry->FullDllName.MaximumLength = 0;
    }
    else
    {
        /* Set it up */
        LdrEntry->FullDllName.Length = PrefixName.Length;
        LdrEntry->FullDllName.MaximumLength = PrefixName.Length;

        /* Copy and null-terminate */
        RtlCopyMemory(LdrEntry->FullDllName.Buffer, PrefixName.Buffer, PrefixName.Length);
        LdrEntry->FullDllName.Buffer[PrefixName.Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    /* Add the entry */
    MiProcessLoaderEntry(LdrEntry, TRUE);

    /* Resolve imports */
    MissingApiName = Buffer;
    MissingDriverName = NULL;

    Status = MiResolveImageReferences(ModuleLoadBase,
                                      &BaseDirectory,
                                      NULL,
                                      &MissingApiName,
                                      &MissingDriverName,
                                      &LoadedImports);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmLoadSystemImage: '%ls', '%s', %X\n", MissingDriverName, MissingApiName, Status);

        /* If the lowest bit is set to 1, this is a hint that we need to free */
        if (*(ULONG_PTR*)&MissingDriverName & 1)
        {
            NeedToFreeString = TRUE;
            *(ULONG_PTR*)&MissingDriverName &= ~1;
        }

        if (NeedToFreeString)
            ExFreePoolWithTag(MissingDriverName, TAG_LDR_WSTR);

        /* Fail */
        MiProcessLoaderEntry(LdrEntry, FALSE);

        /* Check if we need to free the name */
        if (LdrEntry->FullDllName.Buffer)
            /* Free it */
            ExFreePoolWithTag(LdrEntry->FullDllName.Buffer, TAG_LDR_WSTR);

        /* Free the entry itself */
        ExFreePoolWithTag(LdrEntry, TAG_MODULE_OBJECT);
        LdrEntry = NULL;
        goto Exit1;
    }

    /* Update the loader entry */
    LdrEntry->Flags |= (LDRP_SYSTEM_MAPPED | LDRP_ENTRY_PROCESSED | LDRP_MM_LOADED);
    LdrEntry->Flags &= ~LDRP_LOAD_IN_PROGRESS;
    LdrEntry->LoadedImports = LoadedImports;

    DPRINT("MmLoadSystemImage: FIXME MiApplyDriverVerifier()\n");

    if (Flags)
    {
        DPRINT1("MmLoadSystemImage: FIXME\n");
        ASSERT(FALSE);
    }

    /* Write-protect the system image */
    MiWriteProtectSystemImage(LdrEntry->DllBase);

    if (Flags)
    {
        DPRINT1("MmLoadSystemImage: FIXME\n");
        ASSERT(FALSE);
    }

    /* Check if notifications are enabled */
    if (PsImageNotifyEnabled)
    {
        /* Fill out the notification data */
        ImageInfo.Properties = 0;
        ImageInfo.ImageAddressingMode = IMAGE_ADDRESSING_MODE_32BIT;
        ImageInfo.SystemModeImage = TRUE;
        ImageInfo.ImageSize = LdrEntry->SizeOfImage;
        ImageInfo.ImageBase = LdrEntry->DllBase;
        ImageInfo.ImageSectionNumber = ImageInfo.ImageSelector = 0;

        /* Send the notification */
        PspRunLoadImageNotifyRoutines(FileName, NULL, &ImageInfo);
    }

#if defined(KDBG) || defined(_WINKD_)
    /* MiCacheImageSymbols doesn't detect rossym */
    if (TRUE)
#else
    /* Check if there's symbols */
    if (MiCacheImageSymbols(LdrEntry->DllBase))
#endif
    {
        /* Check if the system root is present */
        if (PrefixName.Length > (11 * sizeof(WCHAR)) &&
            !(_wcsnicmp(PrefixName.Buffer, L"\\SystemRoot", 11)))
        {
            /* Add the system root */
            UnicodeTemp = PrefixName;
            UnicodeTemp.Buffer += 11;
            UnicodeTemp.Length -= (11 * sizeof(WCHAR));

            sprintf_nt(Buffer, "%ws%wZ", &SharedUserData->NtSystemRoot[2], &UnicodeTemp);
        }
        else
        {
            /* Build the name */
            sprintf_nt(Buffer, "%wZ", &BaseName);
        }

        /* Setup the ansi string */
        RtlInitString(&AnsiTemp, Buffer);

        /* Notify the debugger */
        DPRINT("MmLoadSystemImage: FIXME DbgLoadImageSymbols()\n");
        //DbgLoadImageSymbols(&AnsiTemp, LdrEntry->DllBase, (ULONG_PTR)PsGetCurrentProcessId());
        //LdrEntry->Flags |= LDRP_DEBUG_SYMBOLS_LOADED;
    }

Finish:

    /* Page the driver */
    ASSERT(Section == NULL);
    MiEnablePagingOfDriver(LdrEntry);

    /* Return pointers */
    *ModuleObject = LdrEntry;
    *ImageBaseAddress = LdrEntry->DllBase;

Exit1:

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmLoadSystemImage: Status %X\n", Status);

        if (IsLoadOk)
        {
            DPRINT1("MmLoadSystemImage: FIXME\n");
            ASSERT(FALSE);
        }

        if (!IsLoadedBefore && Section)
            ObDereferenceObject(Section);
    }

    if (LockOwned)
    {
        KeReleaseMutant(&MmSystemLoadLock, 1, FALSE, FALSE);
        KeLeaveCriticalRegion();
        LockOwned = FALSE;
    }

    /* If we have a file handle, close it */
    if (FileHandle)
        ZwClose(FileHandle);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmLoadSystemImage: FIXME\n");
        ASSERT(FALSE);
    }

Exit:

    /* Check if we have the lock acquired */
    if (LockOwned)
    {
        /* Release the lock */
        KeReleaseMutant(&MmSystemLoadLock, 1, FALSE, FALSE);
        KeLeaveCriticalRegion();
    }

    /* Check if we had a prefix (not supported yet - PrefixName == *FileName now) */
    /* if (NamePrefix)
           ExFreePool(PrefixName.Buffer); */

    /* Free the name buffer and return status */
    ExFreePoolWithTag(Buffer, TAG_LDR_WSTR);

    return Status;
}

BOOLEAN
NTAPI
MmChangeKernelResourceSectionProtection(
    _In_ ULONG_PTR ProtectionMask)
{
    PMMPTE Pte;
    MMPTE TempPte;

    /* Don't do anything if the resource section is already writable */
    if (!MiKernelResourceStartPte || !MiKernelResourceEndPte)
        return FALSE;

    /* If the resource section is physical, we cannot change its protection */
    if (MI_IS_PHYSICAL_ADDRESS(MiPteToAddress(MiKernelResourceStartPte)))
        return FALSE;

    /* Loop the PTEs */
    for (Pte = MiKernelResourceStartPte; Pte < MiKernelResourceEndPte; Pte++)
    {
        /* Read the PTE */
        TempPte = *Pte;

        /* Update the protection */
        MI_MAKE_HARDWARE_PTE_KERNEL(&TempPte, Pte, ProtectionMask, TempPte.u.Hard.PageFrameNumber);
        MI_UPDATE_VALID_PTE(Pte, TempPte);
    }

    /* Only flush the current processor's TLB */
    KeFlushCurrentTb();
    return TRUE;
}

VOID
NTAPI
MmFreeDriverInitialization(
    _In_ PVOID DriverSection)
{
    UNIMPLEMENTED_DBGBREAK();
}

PVOID
NTAPI
MiLocateExportName(
    _In_ PVOID DllBase,
    _In_ PCHAR ExportName)
{
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PULONG NameTable;
    PULONG ExportTable;
    PUSHORT OrdinalTable;
    PVOID Function;
    LONG Low = 0;
    LONG Mid = 0;
    LONG High;
    LONG Ret;
    ULONG ExportSize;
    USHORT Ordinal;

    PAGED_CODE();

    /* Get the export directory */
    ExportDirectory = RtlImageDirectoryEntryToData(DllBase,
                                                   TRUE,
                                                   IMAGE_DIRECTORY_ENTRY_EXPORT,
                                                   &ExportSize);
    if (!ExportDirectory)
        return NULL;

    /* Setup name tables */
    NameTable = (PULONG)((ULONG_PTR)DllBase + ExportDirectory->AddressOfNames);
    OrdinalTable = (PUSHORT)((ULONG_PTR)DllBase + ExportDirectory->AddressOfNameOrdinals);

    /* Do a binary search */
    High = (ExportDirectory->NumberOfNames - 1);

    while (High >= Low)
    {
        /* Get new middle value */
        Mid = ((Low + High) >> 1);

        /* Compare name */
        Ret = strcmp(ExportName, ((PCHAR)DllBase + NameTable[Mid]));

        if (Ret < 0)
            /* Update high */
            High = (Mid - 1);
        else if (Ret > 0)
            /* Update low */
            Low = (Mid + 1);
        else
            /* We got it */
            break;
    }

    /* Check if we couldn't find it */
    if (High < Low)
        return NULL;

    /* Otherwise, this is the ordinal */
    Ordinal = OrdinalTable[Mid];

    /* Resolve the address and write it */
    ExportTable = (PULONG)((ULONG_PTR)DllBase + ExportDirectory->AddressOfFunctions);
    Function = (PVOID)((ULONG_PTR)DllBase + ExportTable[Ordinal]);

    /* Check if the function is actually a forwarder */
    if ((ULONG_PTR)Function > (ULONG_PTR)ExportDirectory &&
        (ULONG_PTR)Function < ((ULONG_PTR)ExportDirectory + ExportSize))
    {
        /* It is, fail */
        return NULL;
    }

    /* We found it */
    return Function;
}

PVOID
NTAPI
MiFindExportedRoutineByName(
    _In_ PVOID DllBase,
    _In_ PANSI_STRING ExportName)
{
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PULONG NameTable;
    PUSHORT OrdinalTable;
    PULONG ExportTable;
    PVOID Function;
    ULONG ExportSize;
    LONG Low = 0;
    LONG Mid = 0;
    LONG High;
    LONG Ret;
    USHORT Ordinal;

    PAGED_CODE();

    /* Get the export directory */
    ExportDirectory = RtlImageDirectoryEntryToData(DllBase,
                                                   TRUE,
                                                   IMAGE_DIRECTORY_ENTRY_EXPORT,
                                                   &ExportSize);
    if (!ExportDirectory)
        return NULL;

    /* Setup name tables */
    NameTable = (PULONG)((ULONG_PTR)DllBase + ExportDirectory->AddressOfNames);
    OrdinalTable = (PUSHORT)((ULONG_PTR)DllBase + ExportDirectory->AddressOfNameOrdinals);

    /* Do a binary search */
    High = (ExportDirectory->NumberOfNames - 1);

    while (High >= Low)
    {
        /* Get new middle value */
        Mid = ((Low + High) >> 1);

        /* Compare name */
        Ret = strcmp(ExportName->Buffer, ((PCHAR)DllBase + NameTable[Mid]));

        if (Ret < 0)
            /* Update high */
            High = (Mid - 1);
        else if (Ret > 0)
            /* Update low */
            Low = (Mid + 1);
        else
            /* We got it */
            break;
    }

    /* Check if we couldn't find it */
    if (High < Low)
        return NULL;

    /* Otherwise, this is the ordinal */
    Ordinal = OrdinalTable[Mid];

    /* Validate the ordinal */
    if (Ordinal >= ExportDirectory->NumberOfFunctions)
        return NULL;

    /* Resolve the address and write it */
    ExportTable = (PULONG)((ULONG_PTR)DllBase + ExportDirectory->AddressOfFunctions);
    Function = (PVOID)((ULONG_PTR)DllBase + ExportTable[Ordinal]);

    /* We found it! */
    ASSERT((Function < (PVOID)ExportDirectory) ||
           (Function > (PVOID)((ULONG_PTR)ExportDirectory + ExportSize)));

    return Function;
}

NTSTATUS
NTAPI
MmCallDllInitialize(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PLIST_ENTRY ListHead)
{
    UNICODE_STRING ServicesKeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\");
    PMM_DLL_INITIALIZE DllInit;
    UNICODE_STRING RegPath;
    UNICODE_STRING ImportName;
    NTSTATUS Status;

    /* Try to see if the image exports a DllInitialize routine */
    DllInit = (PMM_DLL_INITIALIZE)MiLocateExportName(LdrEntry->DllBase, "DllInitialize");
    if (!DllInit)
        return STATUS_SUCCESS;

    /* Do a temporary copy of BaseDllName called ImportName because we'll alter the length of the string. */
    ImportName.Length = LdrEntry->BaseDllName.Length;
    ImportName.MaximumLength = LdrEntry->BaseDllName.MaximumLength;
    ImportName.Buffer = LdrEntry->BaseDllName.Buffer;

    /* Obtain the path to this dll's service in the registry */
    RegPath.MaximumLength = ServicesKeyName.Length + ImportName.Length + sizeof(UNICODE_NULL);
    RegPath.Buffer = ExAllocatePoolWithTag(NonPagedPool, RegPath.MaximumLength, TAG_LDR_WSTR);

    /* Check if this allocation was unsuccessful */
    if (!RegPath.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Build and append the service name itself */
    RegPath.Length = ServicesKeyName.Length;
    RtlCopyMemory(RegPath.Buffer, ServicesKeyName.Buffer, ServicesKeyName.Length);

    /* Check if there is a dot in the filename */
    if (wcschr(ImportName.Buffer, L'.'))
    {
        /* Remove the extension */
        ImportName.Length = (USHORT)(wcschr(ImportName.Buffer, L'.') - ImportName.Buffer) * sizeof(WCHAR);
    }

    /* Append service name (the basename without extension) */
    RtlAppendUnicodeStringToString(&RegPath, &ImportName);

    /* Now call the DllInit func */
    DPRINT("MmCallDllInitialize: Calling '%wZ'\n", &RegPath);
    Status = DllInit(&RegPath);

    /* Clean up */
    ExFreePoolWithTag(RegPath.Buffer, TAG_LDR_WSTR);

    /* Return status value which DllInitialize returned */
    return Status;
}

VOID
NTAPI
MmMakeKernelResourceSectionWritable(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

BOOLEAN
NTAPI
MmVerifyImageIsOkForMpUse(
    _In_ PVOID BaseAddress)
{
    PIMAGE_NT_HEADERS NtHeader;

    PAGED_CODE();

    /* Get NT Headers */
    NtHeader = RtlImageNtHeader(BaseAddress);
    if (!NtHeader)
        return TRUE;

    /* Check if this image is only safe for UP while we have 2+ CPUs */
    if (KeNumberProcessors > 1)
    {
        if (NtHeader->FileHeader.Characteristics & IMAGE_FILE_UP_SYSTEM_ONLY)
            /* Fail */
            return FALSE;
    }

    /* Otherwise, it's safe */
    return TRUE;
}

NTSTATUS
NTAPI
MmCheckSystemImage(
    _In_ HANDLE ImageHandle,
    _In_ BOOLEAN PurgeSection)
{
    FILE_STANDARD_INFORMATION FileStandardInfo;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID ViewBase = NULL;
    HANDLE SectionHandle;
    SIZE_T ViewSize = 0;
    KAPC_STATE ApcState;
    NTSTATUS Status;

    PAGED_CODE();

    /* Setup the object attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
                               NULL,
                               NULL);

    /* Create a section for the DLL */
    Status = ZwCreateSection(&SectionHandle,
                             SECTION_MAP_EXECUTE,
                             &ObjectAttributes,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             ImageHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmCheckSystemImage: Status %X\n", Status);
        return Status;
    }

    /* Make sure we're in the system process */
    KeStackAttachProcess(&PsInitialSystemProcess->Pcb, &ApcState);

    /* Map it */
    Status = ZwMapViewOfSection(SectionHandle,
                                NtCurrentProcess(),
                                &ViewBase,
                                0,
                                0,
                                NULL,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_EXECUTE);
    if (!NT_SUCCESS(Status))
    {
        /* We failed, close the handle and return */
        DPRINT1("MmCheckSystemImage: Status %X\n", Status);
        KeUnstackDetachProcess(&ApcState);
        ZwClose(SectionHandle);
        return Status;
    }

    /* Now query image information */
    Status = ZwQueryInformationFile(ImageHandle,
                                    &IoStatusBlock,
                                    &FileStandardInfo,
                                    sizeof(FileStandardInfo),
                                    FileStandardInformation);
    if (NT_SUCCESS(Status))
    {
        /* First, verify the checksum */
        if (!LdrVerifyMappedImageMatchesChecksum(ViewBase,
                                                 ViewSize,
                                                 FileStandardInfo.
                                                 EndOfFile.LowPart))
        {
            /* Set checksum failure */
            Status = STATUS_IMAGE_CHECKSUM_MISMATCH;
            goto Fail;
        }

        /* Make sure it's a real image */
        NtHeaders = RtlImageNtHeader(ViewBase);
        if (!NtHeaders)
        {
            /* Set checksum failure */
            Status = STATUS_IMAGE_CHECKSUM_MISMATCH;
            goto Fail;
        }

        /* Make sure it's for the correct architecture */
        if (NtHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_NATIVE ||
            NtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
        {
            /* Set protection failure */
            Status = STATUS_INVALID_IMAGE_PROTECT;
            goto Fail;
        }

        /* Check that it's a valid SMP image if we have more then one CPU */
        if (!MmVerifyImageIsOkForMpUse(ViewBase))
            /* Otherwise it's not the right image */
            Status = STATUS_IMAGE_MP_UP_MISMATCH;
    }

    /* Unmap the section, close the handle, and return status */
Fail:
    ZwUnmapViewOfSection(NtCurrentProcess(), ViewBase);
    KeUnstackDetachProcess(&ApcState);
    ZwClose(SectionHandle);
    return Status;
}

INIT_FUNCTION
VOID
NTAPI
MiUpdateThunks(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PVOID OldBase,
    _In_ PVOID NewBase,
    _In_ ULONG Size)
{
    PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLIST_ENTRY NextEntry;
    PULONG_PTR ImageThunk;
    ULONG_PTR OldBaseTop;
    ULONG_PTR Delta;
    ULONG ImportSize;
    /* FIXME: MINGW-W64 must fix LD to generate drivers that Windows can load,
       since a real version of Windows would fail at this point,
       but they seem busy implementing features such as "HotPatch" support in GCC 4.6 instead,
       a feature which isn't even used by Windows. Priorities, priorities...

       Please note that Microsoft WDK EULA and license prohibits using the information
       contained within it for the generation of "non-Windows" drivers, which is precisely what LD will generate,
       since an LD driver will not load on Windows.
    */
  #ifdef _WORKING_LINKER_
    ULONG ix;
  #endif

    /* Calculate the top and delta */
    OldBaseTop = ((ULONG_PTR)OldBase + Size - 1);
    Delta = ((ULONG_PTR)NewBase - (ULONG_PTR)OldBase);

    /* Loop the loader block */
    for (NextEntry = LoaderBlock->LoadOrderListHead.Flink;
         NextEntry != &LoaderBlock->LoadOrderListHead;
         NextEntry = NextEntry->Flink)
    {
        /* Get the loader entry */
        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

      #ifdef _WORKING_LINKER_

        /* Get the IAT */
        ImageThunk = RtlImageDirectoryEntryToData(LdrEntry->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_IAT, &ImportSize);
        if (!ImageThunk)
            continue;

        /* Make sure we have an IAT */
        DPRINT("[Mm0]: Updating thunks in: %wZ\n", &LdrEntry->BaseDllName);

        for (ix = 0; ix < ImportSize; ix++, ImageThunk++)
        {
            /* Check if it's within this module */
            if ((*ImageThunk < (ULONG_PTR)OldBase) || (*ImageThunk > OldBaseTop))
                continue;

            /* Relocate it */
            DPRINT("[Mm0]: Updating IAT at: %p. Old Entry: %p. New Entry: %p.\n",
                   ImageThunk, *ImageThunk, *ImageThunk + Delta);

            *ImageThunk += Delta;
        }

      #else

        /* Get the import table */
        ImportDescriptor = RtlImageDirectoryEntryToData(LdrEntry->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &ImportSize);
        if (!ImportDescriptor)
            continue;

        /* Make sure we have an IAT */
        //DPRINT("[Mm0]: Updating thunks in: %wZ\n", &LdrEntry->BaseDllName);

        while ((ImportDescriptor->Name) && (ImportDescriptor->OriginalFirstThunk))
        {
            /* Get the image thunk */
            for (ImageThunk = (PVOID)((ULONG_PTR)LdrEntry->DllBase + ImportDescriptor->FirstThunk);
                 *ImageThunk;
                 ImageThunk++)
            {
                /* Check if it's within this module */
                if ((*ImageThunk < (ULONG_PTR)OldBase) || (*ImageThunk > OldBaseTop))
                    continue;

               /* Relocate it */
               DPRINT("[Mm0]: Updating IAT at: %p. Old Entry: %p. New Entry: %p.\n",
                       ImageThunk, *ImageThunk, *ImageThunk + Delta);

               *ImageThunk += Delta;
            }

            /* Go to the next import */
            ImportDescriptor++;
        }

      #endif
    }
}

INIT_FUNCTION
VOID
NTAPI
MiReloadBootLoadedDrivers(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PIMAGE_DATA_DIRECTORY DataDirectory;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PIMAGE_FILE_HEADER FileHeader;
    PIMAGE_NT_HEADERS NtHeader;
    PLIST_ENTRY NextEntry;
    PVOID NewImageAddress;
    PVOID DllBase;
    PMMPTE Pte;
    PMMPTE StartPte;
    PMMPTE LastPte;
    PMMPFN Pfn;
    MMPTE TempPte;
    MMPTE OldPte;
    PFN_COUNT PteCount;
    ULONG ix = 0;
    BOOLEAN ValidRelocs;
    NTSTATUS Status;

    /* Loop driver list */
    for (NextEntry = LoaderBlock->LoadOrderListHead.Flink;
         NextEntry != &LoaderBlock->LoadOrderListHead;
         NextEntry = NextEntry->Flink)
    {
        /* Get the loader entry and NT header */
        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        NtHeader = RtlImageNtHeader(LdrEntry->DllBase);

        /* Debug info */
        DPRINT("[Mm0]: Driver at: %p ending at: %p for module: %wZ\n",
                LdrEntry->DllBase, ((ULONG_PTR)LdrEntry->DllBase + LdrEntry->SizeOfImage), &LdrEntry->FullDllName);

        /* Get the first PTE and the number of PTEs we'll need */
        Pte = StartPte = MiAddressToPte(LdrEntry->DllBase);
        PteCount = (ROUND_TO_PAGES(LdrEntry->SizeOfImage) >> PAGE_SHIFT);
        LastPte = (StartPte + PteCount);

      #if MI_TRACE_PFNS
        /* Loop the PTEs */
        while (Pte < LastPte)
        {
            ULONG len;
            ASSERT(Pte->u.Hard.Valid == 1);
            Pfn = MiGetPfnEntry(PFN_FROM_PTE(Pte));
            len = (wcslen(LdrEntry->BaseDllName.Buffer) * sizeof(WCHAR));
            snprintf(Pfn->ProcessName, min(16, len), "%S", LdrEntry->BaseDllName.Buffer);
            Pte++;
        }
      #endif

        /* Skip kernel and HAL */
        /* ROS HACK: Skip BOOTVID/KDCOM too */
        ix++;
        if (ix <= 4)
            continue;

        /* Skip non-drivers */
        if (!NtHeader)
            continue;

        /* Get the file header and make sure we can relocate */
        FileHeader = &NtHeader->FileHeader;

        if (FileHeader->Characteristics & IMAGE_FILE_RELOCS_STRIPPED)
            continue;

        if (NtHeader->OptionalHeader.NumberOfRvaAndSizes < IMAGE_DIRECTORY_ENTRY_BASERELOC)
            continue;

        /* Everything made sense until now, check the relocation section too */
        DataDirectory = &NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

        if (!DataDirectory->VirtualAddress)
        {
            /* We don't really have relocations */
            ValidRelocs = FALSE;
        }
        else
        {
            /* Make sure the size is valid */
            if ((DataDirectory->VirtualAddress + DataDirectory->Size) > LdrEntry->SizeOfImage)
                /* They're not, skip */
                continue;

            /* We have relocations */
            ValidRelocs = TRUE;
        }

        /* Remember the original address */
        DllBase = LdrEntry->DllBase;

        /* Loop the PTEs */
        for (Pte = StartPte; Pte < LastPte; Pte++)
        {
            /* Mark the page modified in the PFN database */
            ASSERT(Pte->u.Hard.Valid == 1);
            Pfn = MiGetPfnEntry(PFN_FROM_PTE(Pte));
            ASSERT(Pfn->u3.e1.Rom == 0);
            Pfn->u3.e1.Modified = TRUE;
        }

        /* Now reserve system PTEs for the image */
        Pte = MiReserveSystemPtes(PteCount, SystemPteSpace);
        if (!Pte)
        {
            /* Shouldn't happen */
            ERROR_FATAL("[Mm0]: Couldn't allocate driver section!\n");
            return;
        }

        /* This is the new virtual address for the module */
        LastPte = (Pte + PteCount);
        NewImageAddress = MiPteToAddress(Pte);

        /* Sanity check */
        DPRINT("[Mm0]: Copying from: %p to: %p\n", DllBase, NewImageAddress);
        ASSERT(ExpInitializationPhase == 0);

        /* Loop the new driver PTEs */
        TempPte = ValidKernelPte;

        for (; Pte < LastPte; Pte++, StartPte++)
        {
            /* Copy the old data */
            OldPte = *StartPte;
            ASSERT(OldPte.u.Hard.Valid == 1);

            /* Set page number from the loader's memory */
            TempPte.u.Hard.PageFrameNumber = OldPte.u.Hard.PageFrameNumber;

            /* Write it */
            MI_WRITE_VALID_PTE(Pte, TempPte);
        }

        /* Update position */
        Pte -= PteCount;

        /* Sanity check */
        ASSERT(*(PULONG)NewImageAddress == *(PULONG)DllBase);

        /* Set the image base to the address where the loader put it */
        NtHeader->OptionalHeader.ImageBase = (ULONG_PTR)DllBase;

        /* Check if we had relocations */
        if (ValidRelocs)
        {
            /* Relocate the image */
            Status = LdrRelocateImageWithBias(NewImageAddress,
                                              0,
                                              "SYSLDR",
                                              STATUS_SUCCESS,
                                              STATUS_CONFLICTING_ADDRESSES,
                                              STATUS_INVALID_IMAGE_FORMAT);
            if (!NT_SUCCESS(Status))
            {
                /* This shouldn't happen */
                ERROR_FATAL("Relocations failed!\n");
                return;
            }
        }

        /* Update the loader entry */
        LdrEntry->DllBase = NewImageAddress;

        /* Update the thunks */
        DPRINT("[Mm0]: Updating thunks to: %wZ\n", &LdrEntry->BaseDllName);

        MiUpdateThunks(LoaderBlock, DllBase, NewImageAddress, LdrEntry->SizeOfImage);

        /* Update the loader entry */
        LdrEntry->Flags |= LDRP_SYSTEM_MAPPED;
        LdrEntry->EntryPoint = (PVOID)((ULONG_PTR)NewImageAddress + NtHeader->OptionalHeader.AddressOfEntryPoint);
        LdrEntry->SizeOfImage = (PteCount << PAGE_SHIFT);

        /* FIXME: We'll need to fixup the PFN linkage when switching to ARM3 */
    }
}

INIT_FUNCTION
NTSTATUS
NTAPI
MiBuildImportsForBootDrivers(VOID)
{
    UNICODE_STRING KernelName = RTL_CONSTANT_STRING(L"ntoskrnl.exe");
    UNICODE_STRING HalName = RTL_CONSTANT_STRING(L"hal.dll");
    PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLDR_DATA_TABLE_ENTRY KernelEntry = NULL;
    PLDR_DATA_TABLE_ENTRY HalEntry = NULL;
    PLDR_DATA_TABLE_ENTRY LdrEntry2;
    PLDR_DATA_TABLE_ENTRY LastEntry = NULL;
    PLDR_DATA_TABLE_ENTRY* EntryArray;
    PLOAD_IMPORTS LoadedImports;
    PLIST_ENTRY NextEntry;
    PLIST_ENTRY NextEntry2;
    ULONG LoadedImportsSize;
    ULONG ImportSize;
    PULONG_PTR ImageThunk;
    ULONG_PTR DllBase;
    ULONG_PTR DllEnd;
    ULONG Modules = 0;
    ULONG ix;
    ULONG jx = 0;

    /* Loop the loaded module list... we are early enough that no lock is needed */
    for (NextEntry = PsLoadedModuleList.Flink;
         NextEntry != &PsLoadedModuleList;
         NextEntry = NextEntry->Flink)
    {
        /* Get the entry */
        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        /* Check if it's the kernel or HAL */
        if (RtlEqualUnicodeString(&KernelName, &LdrEntry->BaseDllName, TRUE))
            /* Found it */
            KernelEntry = LdrEntry;
        else if (RtlEqualUnicodeString(&HalName, &LdrEntry->BaseDllName, TRUE))
            /* Found it */
            HalEntry = LdrEntry;

        /* Check if this is a driver DLL */
        if (LdrEntry->Flags & LDRP_DRIVER_DEPENDENT_DLL)
        {
            /* Check if this is the HAL or kernel */
            if ((LdrEntry == HalEntry) || (LdrEntry == KernelEntry))
                /* Add a reference */
                LdrEntry->LoadCount = 1;
            else
                /* No referencing needed */
                LdrEntry->LoadCount = 0;
        }
        else
        {
            /* Add a reference for all other modules as well */
            LdrEntry->LoadCount = 1;
        }

        /* Remember this came from the loader */
        LdrEntry->LoadedImports = MM_SYSLDR_BOOT_LOADED;

        /* Keep looping */
        Modules++;
    }

    /* We must have at least found the kernel and HAL */
    if (!(HalEntry) || (!KernelEntry))
        return STATUS_NOT_FOUND;

    /* Allocate the list */
    EntryArray = ExAllocatePoolWithTag(PagedPool, (Modules * sizeof(PVOID)), TAG_LDR_IMPORTS);
    if (!EntryArray)
    {
        DPRINT1("MiBuildImportsForBootDrivers: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Loop the loaded module list again */
    for (NextEntry = PsLoadedModuleList.Flink;
         NextEntry != &PsLoadedModuleList;
         NextEntry = NextEntry->Flink)
    {
        /* Get the entry */
        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        /* Get its imports */
        ImportDescriptor = RtlImageDirectoryEntryToData(LdrEntry->DllBase,
                                                        TRUE,
                                                        IMAGE_DIRECTORY_ENTRY_IMPORT,
                                                        &ImportSize);
        if (!ImportDescriptor)
        {
            /* None present */
            LdrEntry->LoadedImports = MM_SYSLDR_NO_IMPORTS;
            continue;
        }

        /* Clear the list and count the number of IAT thunks */
        RtlZeroMemory(EntryArray, Modules * sizeof(PVOID));
        DllBase = DllEnd = ix = 0;
        while ((ImportDescriptor->Name) && (ImportDescriptor->OriginalFirstThunk))
        {
            /* Get the image thunk */
            ImageThunk = (PVOID)((ULONG_PTR)LdrEntry->DllBase + ImportDescriptor->FirstThunk);
            while (*ImageThunk)
            {
                /* Do we already have an address? */
                if (DllBase)
                {
                    /* Is the thunk in the same address? */
                    if ((*ImageThunk >= DllBase) && (*ImageThunk < DllEnd))
                    {
                        /* Skip it, we already have a reference for it */
                        ASSERT(EntryArray[jx]);
                        ImageThunk++;
                        continue;
                    }
                }

                /* Loop the loaded module list to locate this address owner */
                jx = 0;
                NextEntry2 = PsLoadedModuleList.Flink;
                while (NextEntry2 != &PsLoadedModuleList)
                {
                    /* Get the entry */
                    LdrEntry2 = CONTAINING_RECORD(NextEntry2, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

                    /* Get the address range for this module */
                    DllBase = (ULONG_PTR)LdrEntry2->DllBase;
                    DllEnd = DllBase + LdrEntry2->SizeOfImage;

                    /* Check if this IAT entry matches it */
                    if ((*ImageThunk >= DllBase) && (*ImageThunk < DllEnd))
                    {
                        /* Save it */
                        //DPRINT1("Found imported dll: %wZ\n", &LdrEntry2->BaseDllName);
                        EntryArray[jx] = LdrEntry2;
                        break;
                    }

                    /* Keep searching */
                    NextEntry2 = NextEntry2->Flink;
                    jx++;
                }

                /* Do we have a thunk outside the range? */
                if ((*ImageThunk < DllBase) || (*ImageThunk >= DllEnd))
                {
                    /* Could be 0... */
                    if (*ImageThunk)
                    {
                        /* Should not be happening */
                        ERROR_FATAL("Broken IAT entry for %p at %p (%lx)\n", LdrEntry, ImageThunk, *ImageThunk);
                    }

                    /* Reset if we hit this */
                    DllBase = 0;
                }

                ImageThunk++;
            }

            ix++;
            ImportDescriptor++;
        }

        /* Now scan how many imports we really have */
        for (ix = 0, ImportSize = 0; ix < Modules; ix++)
        {
            /* Skip HAL and kernel */
            if ((EntryArray[ix]) &&
                (EntryArray[ix] != HalEntry) &&
                (EntryArray[ix] != KernelEntry))
            {
                /* A valid reference */
                LastEntry = EntryArray[ix];
                ImportSize++;
            }
        }

        /* Do we have any imports after all? */
        if (!ImportSize)
        {
            /* No */
            LdrEntry->LoadedImports = MM_SYSLDR_NO_IMPORTS;
        }
        else if (ImportSize == 1)
        {
            /* A single entry import */
            LdrEntry->LoadedImports = (PVOID)((ULONG_PTR)LastEntry | MM_SYSLDR_SINGLE_ENTRY);
            LastEntry->LoadCount++;
        }
        else
        {
            /* We need an import table */
            LoadedImportsSize = (ImportSize * sizeof(PVOID) + sizeof(SIZE_T));

            LoadedImports = ExAllocatePoolWithTag(PagedPool, LoadedImportsSize, TAG_LDR_IMPORTS);
            if (!LoadedImports)
            {
                DPRINT1("MiBuildImportsForBootDrivers: Allocate failed\n");
                ASSERT(LoadedImports);
            }

            /* Save the count */
            LoadedImports->Count = ImportSize;

            /* Now copy all imports */
            for (ix = 0, jx = 0; ix < Modules; ix++)
            {
                /* Skip HAL and kernel */
                if ((EntryArray[ix]) &&
                    (EntryArray[ix] != HalEntry) &&
                    (EntryArray[ix] != KernelEntry))
                {
                    /* A valid reference */
                    //DPRINT1("Found valid entry: %p\n", EntryArray[ix]);
                    LoadedImports->Entry[jx] = EntryArray[ix];

                    EntryArray[ix]->LoadCount++;
                    jx++;
                }
            }

            /* Should had as many entries as we expected */
            ASSERT(jx == ImportSize);
            LdrEntry->LoadedImports = LoadedImports;
        }
    }

    /* Free the initial array */
    ExFreePoolWithTag(EntryArray, TAG_LDR_IMPORTS);

    /* FIXME: Might not need to keep the HAL/Kernel imports around */

    /* Kernel and HAL are loaded at boot */
    KernelEntry->LoadedImports = MM_SYSLDR_BOOT_LOADED;
    HalEntry->LoadedImports = MM_SYSLDR_BOOT_LOADED;

    /* All worked well */
    return STATUS_SUCCESS;
}

INIT_FUNCTION
VOID
NTAPI
MiLocateKernelSections(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER SectionHeader;
    ULONG_PTR DllBase;
    ULONG Sections;
    ULONG Size;

    /* Get the kernel section header */
    DllBase = (ULONG_PTR)LdrEntry->DllBase;
    NtHeaders = RtlImageNtHeader((PVOID)DllBase);
    SectionHeader = IMAGE_FIRST_SECTION(NtHeaders);

    /* Loop all the sections */
    for (Sections = NtHeaders->FileHeader.NumberOfSections;
         Sections > 0;
         Sections--, SectionHeader++)
    {
        /* Grab the size of the section */
        Size = max(SectionHeader->SizeOfRawData, SectionHeader->Misc.VirtualSize);

        /* Check for .RSRC section */
        if (*(PULONG)SectionHeader->Name == 'rsr.')
        {
            /* Remember the PTEs so we can modify them later */
            MiKernelResourceStartPte = MiAddressToPte(DllBase + SectionHeader->VirtualAddress);
            MiKernelResourceEndPte = MiAddressToPte(ROUND_TO_PAGES(DllBase + SectionHeader->VirtualAddress + Size));
        }
        else if (*(PULONG)SectionHeader->Name == 'LOOP')
        {
            /* POOLCODE vs. POOLMI */
            if (*(PULONG)&SectionHeader->Name[4] == 'EDOC')
            {
                /* Found Ex* Pool code */
                ExPoolCodeStart = (DllBase + SectionHeader->VirtualAddress);
                ExPoolCodeEnd = (ExPoolCodeStart + Size);
            }
            else if (*(PUSHORT)&SectionHeader->Name[4] == 'MI')
            {
                /* Found Mm* Pool code */
                MmPoolCodeStart = (DllBase + SectionHeader->VirtualAddress);
                MmPoolCodeEnd = (MmPoolCodeStart + Size);
            }
        }
        else if ((*(PULONG)SectionHeader->Name == 'YSIM') &&
                 (*(PULONG)&SectionHeader->Name[4] == 'ETPS'))
        {
            /* Found MISYSPTE (Mm System PTE code) */
            MmPteCodeStart = (DllBase + SectionHeader->VirtualAddress);
            MmPteCodeEnd = (MmPteCodeStart + Size);
        }
    }
}

INIT_FUNCTION
BOOLEAN
NTAPI
MiInitializeLoadedModuleList(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLDR_DATA_TABLE_ENTRY NewEntry;
    PLIST_ENTRY ListHead;
    PLIST_ENTRY NextEntry;
    ULONG EntrySize;
    ULONG Size;

    /* Setup the loaded module list and locks */
    ExInitializeResourceLite(&PsLoadedModuleResource);
    KeInitializeSpinLock(&PsLoadedModuleSpinLock);
    InitializeListHead(&PsLoadedModuleList);

    /* Get loop variables and the kernel entry */
    ListHead = &LoaderBlock->LoadOrderListHead;
    LdrEntry = CONTAINING_RECORD(ListHead->Flink, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
    PsNtosImageBase = (ULONG_PTR)LdrEntry->DllBase;

    /* Locate resource section, pool code, and system pte code */
    MiLocateKernelSections(LdrEntry);

    /* Loop the loader block */
    for (NextEntry = ListHead->Flink;
         NextEntry != ListHead;
         NextEntry = NextEntry->Flink)
    {
        /* Get the loader entry */
        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        /* FIXME: ROS HACK. Make sure this is a driver */
        if (!RtlImageNtHeader(LdrEntry->DllBase))
            /* Skip this entry */
            continue;

        /* Calculate the size we'll need and allocate a copy */
        EntrySize = (sizeof(LDR_DATA_TABLE_ENTRY) + LdrEntry->BaseDllName.MaximumLength + sizeof(UNICODE_NULL));

        NewEntry = ExAllocatePoolWithTag(NonPagedPool, EntrySize, TAG_MODULE_OBJECT);
        if (!NewEntry)
        {
            DPRINT1("MiInitializeLoadedModuleList: Allocate failed\n");
            return FALSE;
        }

        /* Copy the entry over */
        *NewEntry = *LdrEntry;

        /* Allocate the name */
        Size = (LdrEntry->FullDllName.MaximumLength + sizeof(UNICODE_NULL));

        NewEntry->FullDllName.Buffer = ExAllocatePoolWithTag(PagedPool, Size, TAG_LDR_WSTR);
        if (!NewEntry->FullDllName.Buffer)
        {
            DPRINT1("MiInitializeLoadedModuleList: Allocate failed\n");
            ExFreePoolWithTag(NewEntry, TAG_MODULE_OBJECT);
            return FALSE;
        }

        /* Set the base name */
        NewEntry->BaseDllName.Buffer = (PVOID)(NewEntry + 1);

        /* Copy the full and base name */
        RtlCopyMemory(NewEntry->FullDllName.Buffer, LdrEntry->FullDllName.Buffer, LdrEntry->FullDllName.MaximumLength);
        RtlCopyMemory(NewEntry->BaseDllName.Buffer, LdrEntry->BaseDllName.Buffer, LdrEntry->BaseDllName.MaximumLength);

        /* Null-terminate the base name */
        NewEntry->BaseDllName.Buffer[NewEntry->BaseDllName.Length / sizeof(WCHAR)] = UNICODE_NULL;

        /* Insert the entry into the list */
        InsertTailList(&PsLoadedModuleList, &NewEntry->InLoadOrderLinks);
    }

    /* Build the import lists for the boot drivers */
    MiBuildImportsForBootDrivers();

    /* We're done */
    return TRUE;
}

VOID
NTAPI
MiSetSystemCodeProtection(
    _In_ PMMPTE FirstPte,
    _In_ PMMPTE LastPte,
    _In_ ULONG Protection)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MiWriteProtectSystemImage(
    _In_ PVOID ImageBase)
{
    PIMAGE_SECTION_HEADER SectionHeaders;
    PIMAGE_SECTION_HEADER Section;
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID SectionBase;
    PVOID SectionEnd;
    PMMPTE FirstPte;
    PMMPTE LastPte;
    ULONG SectionSize;
    ULONG Protection;
    ULONG ix;

    /* Check if the registry setting is on or not */
    if (!MmEnforceWriteProtection)
    {
        /* Ignore section protection */
        DPRINT("MiWriteProtectSystemImage: Ignore section protection %p. FIXME\n", ImageBase);
        return;
    }

    /* Large page mapped images are not supported */
    NT_ASSERT(!MI_IS_PHYSICAL_ADDRESS(ImageBase));

    /* Session images are not yet supported */
    NT_ASSERT(!MI_IS_SESSION_ADDRESS(ImageBase));

    /* Get the NT headers */
    NtHeaders = RtlImageNtHeader(ImageBase);
    if (!NtHeaders)
    {
        DPRINT1("Failed to get NT headers for image @ %p\n", ImageBase);
        return;
    }

    /* Don't touch NT4 drivers */
    if ((NtHeaders->OptionalHeader.MajorOperatingSystemVersion < 5) ||
        (NtHeaders->OptionalHeader.MajorSubsystemVersion < 5))
    {
        DPRINT1("Skipping NT 4 driver @ %p\n", ImageBase);
        return;
    }

    /* Get the section headers */
    SectionHeaders = IMAGE_FIRST_SECTION(NtHeaders);

    /* Get the base address of the first section */
    SectionBase = Add2Ptr(ImageBase, SectionHeaders[0].VirtualAddress);

    /* Start protecting the image header as R/O */
    FirstPte = MiAddressToPte(ImageBase);
    LastPte = (MiAddressToPte(SectionBase) - 1);
    Protection = IMAGE_SCN_MEM_READ;

    if (LastPte >= FirstPte)
        MiSetSystemCodeProtection(FirstPte, LastPte, IMAGE_SCN_MEM_READ);

    /* Loop the sections */
    for (ix = 0; ix < NtHeaders->FileHeader.NumberOfSections; ix++)
    {
        /* Get the section base address and size */
        Section = &SectionHeaders[ix];
        SectionBase = Add2Ptr(ImageBase, Section->VirtualAddress);
        SectionSize = max(Section->SizeOfRawData, Section->Misc.VirtualSize);

        /* Get the first PTE of this section */
        FirstPte = MiAddressToPte(SectionBase);

        /* Check for overlap with the previous range */
        if (FirstPte == LastPte)
        {
            /* Combine the old and new protection by ORing them */
            Protection |= (Section->Characteristics & IMAGE_SCN_PROTECTION_MASK);

            /* Update the protection for this PTE */
            MiSetSystemCodeProtection(FirstPte, FirstPte, Protection);

            /* Skip this PTE */
            FirstPte++;
        }

        /* There can not be gaps! */
        NT_ASSERT(FirstPte == (LastPte + 1));

        /* Get the end of the section and the last PTE */
        SectionEnd = Add2Ptr(SectionBase, (SectionSize - 1));

        NT_ASSERT(SectionEnd < Add2Ptr(ImageBase, NtHeaders->OptionalHeader.SizeOfImage));

        LastPte = MiAddressToPte(SectionEnd);

        /* If there are no more pages (after an overlap), skip this section */
        if (LastPte < FirstPte)
        {
            NT_ASSERT(FirstPte == (LastPte + 1));
            continue;
        }

        /* Get the section protection */
        Protection = (Section->Characteristics & IMAGE_SCN_PROTECTION_MASK);

        /* Update the protection for this section */
        MiSetSystemCodeProtection(FirstPte, LastPte, Protection);
    }

    /* Image should end with the last section */
    if (ALIGN_UP_POINTER_BY(SectionEnd, PAGE_SIZE) != Add2Ptr(ImageBase, NtHeaders->OptionalHeader.SizeOfImage))
    {
        DPRINT1("ImageBase 0x%p ImageSize 0x%lx Section %u VA 0x%lx Raw 0x%lx virt 0x%lx\n",
                ImageBase, NtHeaders->OptionalHeader.SizeOfImage, ix,
                Section->VirtualAddress, Section->SizeOfRawData, Section->Misc.VirtualSize);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
MmGetSystemRoutineAddress(
    _In_ PUNICODE_STRING SystemRoutineName)
{
    UNICODE_STRING KernelName = RTL_CONSTANT_STRING(L"ntoskrnl.exe");
    UNICODE_STRING HalName = RTL_CONSTANT_STRING(L"hal.dll");
    ANSI_STRING AnsiRoutineName;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PVOID ProcAddress = NULL;
    PLIST_ENTRY NextEntry;
    ULONG Modules = 0;
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    /* Convert routine to ansi name */
    Status = RtlUnicodeStringToAnsiString(&AnsiRoutineName, SystemRoutineName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmGetSystemRoutineAddress: Status %X\n", Status);
        return NULL;
    }

    /* Lock the list */
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);

    /* Loop the loaded module list */
    for (NextEntry = PsLoadedModuleList.Flink;
         NextEntry != &PsLoadedModuleList;
         NextEntry = NextEntry->Flink)
    {
        /* Get the entry */
        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        /* Check if it's the kernel or HAL */
        if (RtlEqualUnicodeString(&KernelName, &LdrEntry->BaseDllName, TRUE))
        {
            /* Found it */
            Found = TRUE;
            Modules++;
        }
        else if (RtlEqualUnicodeString(&HalName, &LdrEntry->BaseDllName, TRUE))
        {
            /* Found it */
            Found = TRUE;
            Modules++;
        }

        /* Check if we found a valid binary */
        if (!Found)
            continue;

        /* Find the procedure name */
        ProcAddress = MiFindExportedRoutineByName(LdrEntry->DllBase, &AnsiRoutineName);

        /* Break out if we found it or if we already tried both modules */
        if (ProcAddress)
            break;

        if (Modules == 2)
            break;
    }

    /* Release the lock */
    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();

    /* Free the string and return */
    RtlFreeAnsiString(&AnsiRoutineName);
    return ProcAddress;
}

PVOID
NTAPI
MmPageEntireDriver(
    _In_ PVOID AddressWithinSection)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmResetDriverPaging(
    _In_ PVOID AddressWithinSection)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
