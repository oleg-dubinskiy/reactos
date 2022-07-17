
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>

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
