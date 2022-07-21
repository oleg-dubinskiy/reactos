
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

SLIST_HEADER MmDeadStackSListHead;
PMMWSL MmWorkingSetList;
ULONG MmMaximumDeadKernelStacks = 5;

extern MMPTE DemandZeroPte;
extern ULONG MmVirtualBias;
extern MM_SYSTEMSIZE MmSystemSize;
extern ULONG MmLargeStackSize;
extern ULONG MmSecondaryColorMask;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
MmCleanProcessAddressSpace(
    _In_ PEPROCESS Process)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmDeleteTeb(
    _In_ PEPROCESS Process,
    _In_ PTEB Teb)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmDeleteProcessAddressSpace(
    PEPROCESS Process)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmSetMemoryPriorityProcess(
    _In_ PEPROCESS Process,
    _In_ UCHAR MemoryPriority)
{
    UCHAR OldPriority;

    /* Check if we have less then 16MB of Physical Memory */
    if (MmSystemSize == MmSmallSystem &&
        MmNumberOfPhysicalPages < ((15 * _1MB) / PAGE_SIZE))
    {
        /* Always use background priority */
        MemoryPriority = MEMORY_PRIORITY_BACKGROUND;
    }

    /* Save the old priority and update it */
    OldPriority = (UCHAR)Process->Vm.Flags.MemoryPriority;
    Process->Vm.Flags.MemoryPriority = MemoryPriority;

    /* Return the old priority */
    return OldPriority;
}

BOOLEAN
NTAPI
MmCreateProcessAddressSpace(
    _In_ ULONG MinWs,
    _In_ PEPROCESS Process,
    _Out_ PULONG_PTR DirectoryTableBase)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

INIT_FUNCTION
NTSTATUS
NTAPI
MmInitializeHandBuiltProcess(
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase)
{
    /* Share the directory base with the idle process */
    DirectoryTableBase[0] = PsGetCurrentProcess()->Pcb.DirectoryTableBase[0];
    DirectoryTableBase[1] = PsGetCurrentProcess()->Pcb.DirectoryTableBase[1];

    /* Initialize the Addresss Space */
    KeInitializeGuardedMutex(&Process->AddressCreationLock);
    KeInitializeSpinLock(&Process->HyperSpaceLock);

    Process->Vm.WorkingSetExpansionLinks.Flink = NULL;

    ASSERT(Process->VadRoot.NumberGenericTableElements == 0);
    Process->VadRoot.BalancedRoot.u1.Parent = &Process->VadRoot.BalancedRoot;

    /* Use idle process Working set */
    Process->Vm.VmWorkingSetList = PsGetCurrentProcess()->Vm.VmWorkingSetList;

    /* Done */
    Process->HasAddressSpace = TRUE;//??

    return STATUS_SUCCESS;
}

INIT_FUNCTION
NTSTATUS
NTAPI
MmInitializeHandBuiltProcess2(
    _In_ PEPROCESS Process)
{
    // ? MmInitializeProcessAddressSpace()

    if (MmVirtualBias)
    {
        /* Lock the VAD, ARM3-owned ranges away */
        UNIMPLEMENTED_DBGBREAK();
        return STATUS_NOT_IMPLEMENTED;
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
MiInitializeWorkingSetList(
    _In_ PEPROCESS CurrentProcess)
{
    PMMPFN Pfn1;
    PMMPTE sysPte;
    MMPTE tempPte;

    /* Setup some bogus list data */
    MmWorkingSetList->LastEntry = CurrentProcess->Vm.MinimumWorkingSetSize;
    MmWorkingSetList->HashTable = NULL;
    MmWorkingSetList->HashTableSize = 0;
    MmWorkingSetList->NumberOfImageWaiters = 0;
    MmWorkingSetList->Wsle = (PVOID)(ULONG_PTR)0xDEADBABEDEADBABEULL;
    MmWorkingSetList->VadBitMapHint = 1;
    MmWorkingSetList->HashTableStart = (PVOID)(ULONG_PTR)0xBADAB00BBADAB00BULL;
    MmWorkingSetList->HighestPermittedHashAddress = (PVOID)(ULONG_PTR)0xCAFEBABECAFEBABEULL;
    MmWorkingSetList->FirstFree = 1;
    MmWorkingSetList->FirstDynamic = 2;
    MmWorkingSetList->NextSlot = 3;
    MmWorkingSetList->LastInitializedWsle = 4;

    /* The rule is that the owner process is always in the FLINK of the PDE's PFN entry */
    Pfn1 = MiGetPfnEntry(CurrentProcess->Pcb.DirectoryTableBase[0] >> PAGE_SHIFT);
    ASSERT(Pfn1->u4.PteFrame == MiGetPfnEntryIndex(Pfn1));
    Pfn1->u1.Event = (PKEVENT)CurrentProcess;

    /* Map the process working set in kernel space */
    sysPte = MiReserveSystemPtes(1, SystemPteSpace);
    MI_MAKE_HARDWARE_PTE_KERNEL(&tempPte, sysPte, MM_READWRITE, CurrentProcess->WorkingSetPage);
    MI_WRITE_VALID_PTE(sysPte, tempPte);
    CurrentProcess->Vm.VmWorkingSetList = MiPteToAddress(sysPte);
}

NTSTATUS
NTAPI
MmInitializeProcessAddressSpace(
    _In_ PEPROCESS Process,
    _In_ PEPROCESS ProcessClone OPTIONAL,
    _In_ PVOID SectionObject OPTIONAL,
    _Inout_ PULONG Flags,
    _In_ POBJECT_NAME_INFORMATION* AuditName OPTIONAL)
{
    PSECTION_IMAGE_INFORMATION ImageInformation;
    PSECTION Section = SectionObject;
    LARGE_INTEGER SectionOffset;
    UNICODE_STRING FileName;
    PFN_NUMBER PageFrameNumber;
    PFILE_OBJECT FileObject;
    PVOID ImageBase = NULL;
    PSEGMENT Segment;
    PWCHAR Source;
    PCHAR Destination;
    PMMPDE Pde;
    PMMPTE Pte;
    MMPTE TempPte;
    SIZE_T ViewSize = 0;
    ULONG AllocationType = 0;
    USHORT Length = 0;
    KIRQL OldIrql;
    NTSTATUS status;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("MmInitializeProcessAddressSpace: Process %p, Clone %p, Section %p, Flags %X\n",
           Process, ProcessClone, SectionObject, (Flags?*Flags:0));

    /* We should have a PDE */
    ASSERT(Process->Pcb.DirectoryTableBase[0] != 0);
    ASSERT(Process->PdeUpdateNeeded == FALSE);

    /* Attach to the process */
    KeAttachProcess(&Process->Pcb);

    /* The address space should now been in phase 1 or 0 */
    ASSERT(Process->AddressSpaceInitialized <= 1);
    Process->AddressSpaceInitialized = 2;

    /* Initialize the Addresss Space lock */
    KeInitializeGuardedMutex(&Process->AddressCreationLock);
    Process->Vm.WorkingSetExpansionLinks.Flink = NULL;

    /* Initialize AVL tree */
    ASSERT(Process->VadRoot.NumberGenericTableElements == 0);
    Process->VadRoot.BalancedRoot.u1.Parent = &Process->VadRoot.BalancedRoot;

    /* Lock PFN database */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* Setup the PFN for the page directory of this process */
#ifdef _M_AMD64
    #error FIXME
#else
    Pde = MiAddressToPte(PDE_BASE);
#endif

    PageFrameNumber = PFN_FROM_PTE(Pde);
    MiInitializePfn(PageFrameNumber, Pde, TRUE);
    DPRINT("MmInitializeProcessAddressSpace: PDE_BASE %p, Pde %p [%I64X]\n",
           PDE_BASE, Pde, MiGetPteContents(Pde));

    ASSERT(Process->Pcb.DirectoryTableBase[0] == (PageFrameNumber * PAGE_SIZE));

    /* Do the same for hyperspace page */
#ifdef _M_AMD64
    #error FIXME
#else
    Pde = MiAddressToPde(HYPER_SPACE);
#endif

    PageFrameNumber = PFN_FROM_PTE(Pde);
    //ASSERT(Process->Pcb.DirectoryTableBase[0] == PageFrameNumber * PAGE_SIZE); // we're not lucky
    MiInitializePfn(PageFrameNumber, (PMMPTE)Pde, TRUE);
    DPRINT("MmInitializeProcessAddressSpace: PDE_BASE %p, Pde %p [%I64X]\n",
           PDE_BASE, Pde, MiGetPteContents(Pde));

    /* Setup the TempPte */
    Pte = MiAddressToPte(MI_VAD_BITMAP);
    DPRINT("MmInitializeProcessAddressSpace: MI_VAD_BITMAP %p, Pte %p [%p]\n",
           MI_VAD_BITMAP, Pte, MiGetPteContents(Pte));
    MI_MAKE_HARDWARE_PTE(&TempPte, Pte, MM_READWRITE, 0);
    MI_MAKE_DIRTY_PAGE(&TempPte);
    DPRINT("MmInitializeProcessAddressSpace: TempPte %p\n", TempPte.u.Long);

    /* Setup for the vad bitmap page */
    ASSERT(Pte->u.Long != 0);
    PageFrameNumber = PFN_FROM_PTE(Pte);
    MI_WRITE_INVALID_PTE(Pte, DemandZeroPte);
    MiInitializePfn(PageFrameNumber, Pte, TRUE);
    DPRINT("MmInitializeProcessAddressSpace: MI_VAD_BITMAP %p, Pte %p [%I64X]\n",
           MI_VAD_BITMAP, Pte, MiGetPteContents(Pte));

    TempPte.u.Hard.PageFrameNumber = PageFrameNumber;
    MI_WRITE_VALID_PTE(Pte, TempPte);
    DPRINT("MmInitializeProcessAddressSpace: MI_VAD_BITMAP %p, Pte %p [%I64X]\n",
           MI_VAD_BITMAP, Pte, MiGetPteContents(Pte));

    /* Setup for the working set page */
    Pte = MiAddressToPte(MI_WORKING_SET_LIST);
    ASSERT(Pte->u.Long != 0);
    PageFrameNumber = PFN_FROM_PTE(Pte);
    MI_WRITE_INVALID_PTE(Pte, DemandZeroPte);
    MiInitializePfn(PageFrameNumber, Pte, TRUE);
    DPRINT("MmInitializeProcessAddressSpace: MI_WORKING_SET_LIST %p, Pte %p [%I64X]\n",
           MI_WORKING_SET_LIST, Pte, MiGetPteContents(Pte));

    TempPte.u.Hard.PageFrameNumber = PageFrameNumber;
    MI_WRITE_VALID_PTE(Pte, TempPte);
    DPRINT("MmInitializeProcessAddressSpace: MI_WORKING_SET_LIST %p, Pte %p [%I64X]\n",
           MI_WORKING_SET_LIST, Pte, MiGetPteContents(Pte));

    /* Now initialize the working set list */
    MiInitializeWorkingSetList(Process);

    /* Sanity check */
    ASSERT(Process->PhysicalVadRoot == NULL);

    /* Release PFN lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Check if there's a Section Object */
    if (!Section)
    {
        if (ProcessClone)
        {
            DPRINT1("MmInitializeProcessAddressSpace: ProcessClone %p\n", ProcessClone);
            ASSERT(FALSE);
        }
        else
        {
            /* Be nice and detach */
            KeDetachProcess();
            Status = STATUS_SUCCESS;
        }

        if (NT_SUCCESS(Status))
            *Flags &= ~0x10; // FIXME

        goto Exit;
    }

    if (!Section->u.Flags.Image)
    {
        DPRINT1("MmInitializeProcessAddressSpace: STATUS_SECTION_NOT_IMAGE\n");
        Status = STATUS_SECTION_NOT_IMAGE;
        goto Exit1;
    }

    Segment = Section->Segment;
    FileObject = Segment->ControlArea->FilePointer;
    ImageInformation = Segment->u2.ImageInformation;

    /* Determine the image file name and save it to EPROCESS */
    FileName = FileObject->FileName;
    Source = (PWCHAR)((PCHAR)FileName.Buffer + FileName.Length);

    if (FileName.Buffer)
    {
        /* Loop the file name*/
        while (Source > FileName.Buffer)
        {
            /* Make sure this isn't a backslash */
            if (*--Source == OBJ_NAME_PATH_SEPARATOR)
            {
                /* If so, stop it here */
                Source++;
                break;
            }
            else
            {
                /* Otherwise, keep going */
                Length++;
            }
        }
    }

    /* Copy the to the process and truncate it to 15 characters if necessary */
    Destination = Process->ImageFileName;
    Length = min(Length, sizeof(Process->ImageFileName) - 1);

    while (Length--)
        *Destination++ = (UCHAR)*Source++;

    *Destination = ANSI_NULL;

    /* Check if caller wants an audit name */
    if (AuditName)
    {
        /* Setup the audit name */
        Status = SeInitializeProcessAuditName(FileObject, FALSE, AuditName);
        if (!NT_SUCCESS(Status))
        {
            /* Fail */
            KeDetachProcess();
            return Status;
        }
    }

    Process->SubSystemMajorVersion = (UCHAR)ImageInformation->SubSystemMajorVersion;
    Process->SubSystemMinorVersion = (UCHAR)ImageInformation->SubSystemMinorVersion;

    if ((*Flags & 0x10) == 0x10)
        AllocationType = 0x20000000;

    /* Map the section */
    SectionOffset.QuadPart = 0;
    Status = MmMapViewOfSection(SectionObject,
                                Process,
                                &ImageBase,
                                0,
                                0,
                                &SectionOffset,
                                &ViewSize,
                                ViewShare,
                                AllocationType,
                                PAGE_READWRITE);
    /* Save the pointer */
    Process->SectionBaseAddress = ImageBase;

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmInitializeProcessAddressSpace: Status %X\n", Status);
        goto Exit1;
    }


    if (*Flags & 0x10)
    {
        DPRINT1("MmInitializeProcessAddressSpace: *Flags %X\n", *Flags);
        ASSERT(FALSE);
    }

    status = PspMapSystemDll(Process, NULL, FALSE);
    if (!NT_SUCCESS(status))
    {
        DPRINT1("MmInitializeProcessAddressSpace: status %X\n", status);
        Status = status;
    }

Exit1:

    DPRINT1("MmInitializeProcessAddressSpace: FIXME MiAllowWorkingSetExpansion\n");
    KeDetachProcess();

Exit:
    /* Return status to caller */
    DPRINT1("MmInitializeProcessAddressSpace: return %X\n", Status);
    return Status;
}

NTSTATUS
NTAPI
MmCreatePeb(
    _In_ PEPROCESS Process,
    _In_ PINITIAL_PEB InitialPeb,
    _Out_ PPEB* BasePeb)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

ULONG
NTAPI
MmGetSessionIdEx(
    _In_ PEPROCESS Process)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

NTSTATUS
NTAPI
MmCreateTeb(
    _In_ PEPROCESS Process,
    _In_ PCLIENT_ID ClientId,
    _In_ PINITIAL_TEB InitialTeb,
    _Out_ PTEB* BaseTeb)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmGrowKernelStack(
    _In_ PVOID StackPointer)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PVOID
NTAPI
MmCreateKernelStack(
    _In_ BOOLEAN GuiStack,
    _In_ UCHAR Node)
{
    PSLIST_ENTRY SListEntry;
    PVOID BaseAddress;
    PMMPTE Pte;
    PMMPTE StackPte;
    MMPTE TempPte;
    MMPTE InvalidPte;
    PFN_NUMBER PageFrameIndex;
    PFN_COUNT StackPtes;
    PFN_COUNT StackPages;
    ULONG ix;
    KIRQL OldIrql;

    /* Calculate pages needed */
    if (GuiStack)
    {
        /* We'll allocate 64KB stack, but only commit 12K */
        StackPtes = BYTES_TO_PAGES(MmLargeStackSize);
        StackPages = BYTES_TO_PAGES(KERNEL_LARGE_STACK_COMMIT);
    }
    else
    {
        /* If the dead stack S-LIST has a stack on it, use it instead of allocating new system PTEs for this stack */
        if (ExQueryDepthSList(&MmDeadStackSListHead))
        {
            SListEntry = InterlockedPopEntrySList(&MmDeadStackSListHead);
            if (SListEntry)
            {
                BaseAddress = (SListEntry + 1);
                return BaseAddress;
            }
        }

        /* We'll allocate 12K and that's it */
        StackPtes = BYTES_TO_PAGES(KERNEL_STACK_SIZE);
        StackPages = StackPtes;
    }

    /* Reserve stack pages, plus a guard page */
    StackPte = MiReserveSystemPtes((StackPtes + 1), SystemPteSpace);
    if (!StackPte)
        return NULL;

    /* Get the stack address */
    BaseAddress = MiPteToAddress(StackPte + StackPtes + 1);

    /* Select the right PTE address where we actually start committing pages */
    Pte = StackPte;
    if (GuiStack)
        Pte += BYTES_TO_PAGES(MmLargeStackSize - KERNEL_LARGE_STACK_COMMIT);


    /* Setup the temporary invalid PTE */
    MI_MAKE_SOFTWARE_PTE(&InvalidPte, MM_NOACCESS);

    /* Setup the template stack PTE */
    MI_MAKE_HARDWARE_PTE_KERNEL(&TempPte, (Pte + 1), MM_READWRITE, 0);

    /* Acquire the PFN DB lock */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* Loop each stack page */
    for (ix = 0; ix < StackPages; ix++)
    {
        Pte++;

        /* Get a page and write the current invalid PTE */
        MI_SET_USAGE(MI_USAGE_KERNEL_STACK);
        MI_SET_PROCESS2(PsGetCurrentProcess()->ImageFileName);
        PageFrameIndex = MiRemoveAnyPage(MI_GET_NEXT_COLOR());
        MI_WRITE_INVALID_PTE(Pte, InvalidPte);

        /* Initialize the PFN entry for this page */
        MiInitializePfn(PageFrameIndex, Pte, 1);

        /* Write the valid PTE */
        TempPte.u.Hard.PageFrameNumber = PageFrameIndex;
        MI_WRITE_VALID_PTE(Pte, TempPte);
    }

    /* Release the PFN lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Return the stack address */
    return BaseAddress;
}

VOID
NTAPI
MmDeleteKernelStack(
    _In_ PVOID StackBase,
    _In_ BOOLEAN GuiStack)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtAllocateUserPhysicalPages(
    _In_ HANDLE ProcessHandle,
    _Inout_ PULONG_PTR NumberOfPages,
    _Inout_ PULONG_PTR UserPfnArray)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtMapUserPhysicalPages(
    _In_ PVOID VirtualAddresses,
    _In_ ULONG_PTR NumberOfPages,
    _Inout_ PULONG_PTR UserPfnArray)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtMapUserPhysicalPagesScatter(
    _In_ PVOID* VirtualAddresses,
    _In_ ULONG_PTR NumberOfPages,
    _Inout_ PULONG_PTR UserPfnArray)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtFreeUserPhysicalPages(
    _In_ HANDLE ProcessHandle,
    _Inout_ PULONG_PTR NumberOfPages,
    _Inout_ PULONG_PTR UserPfnArray)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
