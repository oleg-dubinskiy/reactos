
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

LIST_ENTRY PsLoadedModuleList;
LIST_ENTRY MmLoadedUserImageList;
ERESOURCE PsLoadedModuleResource;
ULONG_PTR PsNtosImageBase;
KMUTANT MmSystemLoadLock;

PVOID MmUnloadedDrivers;
PVOID MmLastUnloadedDrivers;

BOOLEAN MmMakeLowMemory;
BOOLEAN MmEnforceWriteProtection = FALSE; // FIXME: should be TRUE, but that would cause CORE-16387 & CORE-16449

/* FUNCTIONS ******************************************************************/

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
MmLoadSystemImage(
    _In_ PUNICODE_STRING FileName,
    _In_ PUNICODE_STRING NamePrefix OPTIONAL,
    _In_ PUNICODE_STRING LoadedName OPTIONAL,
    _In_ ULONG Flags,
    _Out_ PVOID* ModuleObject,
    _Out_ PVOID* ImageBaseAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
MmChangeKernelResourceSectionProtection(
    _In_ ULONG_PTR ProtectionMask)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

VOID
NTAPI
MmFreeDriverInitialization(
    _In_ PVOID DriverSection)
{
    UNIMPLEMENTED_DBGBREAK();
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
MmCallDllInitialize(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PLIST_ENTRY ListHead)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
MmMakeKernelResourceSectionWritable(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmCheckSystemImage(
    _In_ HANDLE ImageHandle,
    _In_ BOOLEAN PurgeSection)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
MmGetSystemRoutineAddress(
    _In_ PUNICODE_STRING SystemRoutineName)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
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
