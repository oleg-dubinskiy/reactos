
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

SLIST_HEADER MmDeadStackSListHead;
PMMWSL MmWorkingSetList;
ULONG MmMaximumDeadKernelStacks = 5;
ULONG MmProcessColorSeed = 0x12345678;

extern MMPTE DemandZeroPte;
extern ULONG MmVirtualBias;
extern MM_SYSTEMSIZE MmSystemSize;
extern ULONG MmLargeStackSize;
extern ULONG MmSecondaryColorMask;
extern LARGE_INTEGER MmCriticalSectionTimeout;
extern SIZE_T MmMinimumStackCommitInBytes;

/* FUNCTIONS ******************************************************************/

/* HACK!!! This must be cleared during the cleanup of the working set of the process. */
VOID
NTAPI
MiCleanUserSharedData(
    _In_ PEPROCESS Process)
{
    KIRQL OldIrql;
    PMMPTE PointerPde;
    PMMPTE PointerPte;
    PMMPFN Pfn1;
    PMMPFN Pfn2;
    PFN_NUMBER PageFrameIndex;
    PFN_NUMBER PageTableFrameIndex;
    MMPTE DemandZeroWritePte;
    extern RTL_BITMAP MiPfnBitMap;

    MI_MAKE_SOFTWARE_PTE(&DemandZeroWritePte, MM_READWRITE);
    DPRINT("MiCleanUserSharedData: %X\n", DemandZeroWritePte.u.Long);

    OldIrql = MiLockPfnDb(APC_LEVEL);

    PointerPte = MiAddressToPte(USER_SHARED_DATA);
    ASSERT(PointerPte->u.Hard.Valid == 1);
    PageFrameIndex = PFN_FROM_PTE(PointerPte);
    Pfn1 = MI_PFN_ELEMENT(PageFrameIndex);

    DPRINT("MiCleanUserSharedData: %p, %X\n", PointerPte, PointerPte->u.Long);

    PointerPde = MiAddressToPte(PointerPte);
    PageTableFrameIndex = PFN_FROM_PTE(PointerPde);
    Pfn2 = MI_PFN_ELEMENT(PageTableFrameIndex);

    if (!Pfn1->u3.e1.Modified && PointerPte->u.Hard.Dirty)
    {
        ASSERT(Pfn1->u3.e1.Rom == 0);
        Pfn1->u3.e1.Modified = 1;

        if (!Pfn1->OriginalPte.u.Soft.Prototype && !Pfn1->u3.e1.WriteInProgress)
        {
            MiReleasePageFileSpace(Pfn1->OriginalPte);
            Pfn1->OriginalPte.u.Soft.PageFileHigh = 0;
        }
    }

    MiDecrementPfnShare(Pfn2, PageTableFrameIndex);
    MiDecrementShareCount(Pfn1, PageFrameIndex);

    MI_WRITE_INVALID_PTE(PointerPte, DemandZeroWritePte);
    ASSERT(MiPteToAddress(PointerPte) <= MM_HIGHEST_USER_ADDRESS);

    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    DPRINT("MiCleanUserSharedData: %p, %p\n", PointerPte, *PointerPte);
}

VOID
NTAPI
MmCleanProcessAddressSpace(
    _In_ PEPROCESS Process)
{
    PETHREAD Thread = PsGetCurrentThread();
    PMM_AVL_TABLE VadTree;
    PMMVAD Vad;

    DPRINT("MmCleanProcessAddressSpace: %p '%16s'\n", Process, Process->ImageFileName);

    if (Process->VmDeleted)
    {
        DPRINT("MmCleanProcessAddressSpace: VmDeleted\n");
        MiSessionRemoveProcess();
        return;
    }

    /* Only support this */
    ASSERT(Process->AddressSpaceInitialized == 2);

    /* Remove from the session */
    MiSessionRemoveProcess();

    /* Lock the process address space from changes */
    MmLockAddressSpace(&Process->Vm);
    MiLockProcessWorkingSetUnsafe(Process, Thread);

    /* VM is deleted now */
    Process->VmDeleted = TRUE;

    /* HACK!!! This must be cleared during the cleanup of the working set of the process. */
    MiCleanUserSharedData(Process);

    MiUnlockProcessWorkingSetUnsafe(Process, Thread);

    /* Enumerate the VADs */
    VadTree = &Process->VadRoot;

    while (VadTree->NumberGenericTableElements)
    {
        /* Grab the current VAD */
        Vad = (PMMVAD)VadTree->BalancedRoot.RightChild;

        /* Check for old-style memory areas */
        if (Vad->u.VadFlags.Spare == 1)
        {
            /* Let RosMm handle this */
            ASSERT(0);//MiRosCleanupMemoryArea(Process, Vad);
            continue;
        }

        /* Remove VAD charges */
        MiRemoveVadCharges(Vad, Process);

        /* Lock the working set */
        MiLockProcessWorkingSetUnsafe(Process, Thread);

        /* Remove this VAD from the tree */
        ASSERT(VadTree->NumberGenericTableElements >= 1);
        MiRemoveNode((PMMADDRESS_NODE)Vad, VadTree);

        /* Only regular VADs supported for now */
//        ASSERT(Vad->u.VadFlags.VadType == VadNone);

        /* Check if this is a section VAD */
        if (!Vad->u.VadFlags.PrivateMemory && Vad->ControlArea)
        {
            /* Remove the view */
            MiRemoveMappedView(Process, Vad);
        }
        else
        {
            /* Delete the addresses */
            MiDeleteVirtualAddresses((Vad->StartingVpn * PAGE_SIZE),
                                     ((Vad->EndingVpn * PAGE_SIZE) | (PAGE_SIZE - 1)),
                                     Vad);

            /* Release the working set */
            MiUnlockProcessWorkingSetUnsafe(Process, Thread);
        }

         /* Skip ARM3 fake VADs, they'll be freed by MmDeleteProcessAddresSpace */
        if (Vad->u.VadFlags.Spare == 1)
        {
            /* Set a flag so MmDeleteMemoryArea knows to free, but not to remove */
            Vad->u.VadFlags.Spare = 2;
            continue;
        }

        /* Free the VAD memory */
        ExFreePool(Vad);
    }

    /* Lock the working set */
    MiLockProcessWorkingSetUnsafe(Process, Thread);

    ASSERT(Process->CloneRoot == NULL);
    ASSERT(Process->PhysicalVadRoot == NULL);

    /* Delete the shared user data section */
    MiDeleteVirtualAddresses(USER_SHARED_DATA, USER_SHARED_DATA, NULL);

    /* Release the working set */
    MiUnlockProcessWorkingSetUnsafe(Process, Thread);

    /* Release the address space */
    MmUnlockAddressSpace(&Process->Vm);
}

VOID
NTAPI
MmDeleteTeb(
    _In_ PEPROCESS Process,
    _In_ PTEB Teb)
{
    PETHREAD Thread = PsGetCurrentThread();
    PMM_AVL_TABLE VadTree = &Process->VadRoot;
    PMMADDRESS_NODE PreviousNode;
    PMMADDRESS_NODE NextNode;
    PMMVAD Vad;
    ULONG_PTR TebEnd;

    DPRINT("MmDeleteTeb: %p in %16s\n", Teb, Process->ImageFileName);

    /* TEB is one page */
    TebEnd = ((ULONG_PTR)Teb + ROUND_TO_PAGES(sizeof(TEB)) - 1);

    /* Attach to the process */
    KeAttachProcess(&Process->Pcb);

    /* Lock the process address space */
    KeAcquireGuardedMutex(&Process->AddressCreationLock);

    /* Find the VAD, make sure it's a TEB VAD */
    Vad = MiLocateAddress(Teb);
    ASSERT(Vad != NULL);

    DPRINT("MmDeleteTeb: Removing %X-%X\n", Vad->StartingVpn, Vad->EndingVpn);

    if (Vad->StartingVpn != ((ULONG_PTR)Teb / PAGE_SIZE))
    {
        /* Bug in the AVL code? */
        DPRINT1("Corrupted VAD!\n");
        goto Exit;
    }

    /* Sanity checks for a valid TEB VAD */
    ASSERT((Vad->StartingVpn == ((ULONG_PTR)Teb >> PAGE_SHIFT) &&
           (Vad->EndingVpn == (TebEnd >> PAGE_SHIFT))));

    ASSERT(Vad->u.VadFlags.NoChange == TRUE);
    ASSERT(Vad->u2.VadFlags2.OneSecured == TRUE);
    ASSERT(Vad->u2.VadFlags2.MultipleSecured == FALSE);

    MiRemoveVadCharges(Vad, Process);

    PreviousNode = MiGetPreviousNode((PMMADDRESS_NODE)Vad);
    NextNode = MiGetNextNode((PMMADDRESS_NODE)Vad);

    MiReturnPageTablePageCommitment((ULONG_PTR)Teb, TebEnd, Process, PreviousNode, NextNode);

    /* Lock the working set */
    MiLockProcessWorkingSetUnsafe(Process, Thread);

    /* Remove this VAD from the tree */
    ASSERT(VadTree->NumberGenericTableElements >= 1);
    MiRemoveNode((PMMADDRESS_NODE)Vad, VadTree);

    /* Delete the pages */
    MiDeleteVirtualAddresses((ULONG_PTR)Teb, TebEnd, NULL);

    /* Release the working set */
    MiUnlockProcessWorkingSetUnsafe(Process, Thread);

    /* Remove the VAD */
    ExFreePool(Vad);

Exit:

    /* Release the address space lock */
    KeReleaseGuardedMutex(&Process->AddressCreationLock);

    /* Detach */
    KeDetachProcess();
}

VOID
NTAPI
MmDeleteProcessAddressSpace2(
    _In_ PEPROCESS Process)
{
    PEPROCESS CurrentProcess = PsGetCurrentProcess();
    PMMPFN Pfn1;
    PMMPFN Pfn2;
    PMMPTE Pte;
    PFN_NUMBER PdeFrameIndex;
    PFN_NUMBER HyperFrameIndex;
    PFN_NUMBER VadBitmapIndex;
    KIRQL OldIrql;

    DPRINT("MmDeleteProcessAddressSpace2: Process %p\n", Process);

    /* Acquire the PFN lock */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* Check for fully initialized process */
    if (Process->AddressSpaceInitialized == 2)
    {
        /* Revert MmCreateProcessAddressSpace() */

        /* Map the working set page and its page table */
        Pfn1 = MiGetPfnEntry(Process->WorkingSetPage);
        Pfn2 = MiGetPfnEntry(Pfn1->u4.PteFrame);

        /* Nuke it */
        MI_SET_PFN_DELETED(Pfn1);
        MiDecrementShareCount(Pfn2, Pfn1->u4.PteFrame);
        MiDecrementShareCount(Pfn1, Process->WorkingSetPage);
        ASSERT((Pfn1->u3.e2.ReferenceCount == 0) || (Pfn1->u3.e1.WriteInProgress));

        /* hyperspace and vad bitmap */
        HyperFrameIndex = (Process->Pcb.DirectoryTableBase[1] / PAGE_SIZE);
        Pte = MiMapPageInHyperSpaceAtDpc(CurrentProcess, HyperFrameIndex);
        VadBitmapIndex = (Pte + MiAddressToPteOffset(MI_VAD_BITMAP))->u.Hard.PageFrameNumber;
        MiUnmapPageInHyperSpaceFromDpc(CurrentProcess, Pte);

        /* Now map vad bitmap and its page table */
        Pfn1 = MiGetPfnEntry(VadBitmapIndex);
        Pfn2 = MiGetPfnEntry(Pfn1->u4.PteFrame);

        /* Nuke it */
        MI_SET_PFN_DELETED(Pfn1);
        MiDecrementShareCount(Pfn2, Pfn1->u4.PteFrame);
        MiDecrementShareCount(Pfn1, VadBitmapIndex);
        ASSERT((Pfn1->u3.e2.ReferenceCount == 0) || (Pfn1->u3.e1.WriteInProgress));

        /* Now map hyperspace and its page table */
        Pfn1 = MiGetPfnEntry(HyperFrameIndex);
        Pfn2 = MiGetPfnEntry(Pfn1->u4.PteFrame);

        /* Nuke it */
        MI_SET_PFN_DELETED(Pfn1);
        MiDecrementShareCount(Pfn2, Pfn1->u4.PteFrame);
        MiDecrementShareCount(Pfn1, HyperFrameIndex);

        ASSERT((Pfn1->u3.e2.ReferenceCount == 0) || (Pfn1->u3.e1.WriteInProgress));

        /* Finally, nuke the PDE itself */
        PdeFrameIndex = (Process->Pcb.DirectoryTableBase[0] / PAGE_SIZE);
        Pfn1 = MiGetPfnEntry(PdeFrameIndex);
        MI_SET_PFN_DELETED(Pfn1);
        MiDecrementShareCount(Pfn1, PdeFrameIndex);
        MiDecrementShareCount(Pfn1, PdeFrameIndex);

        /* Page table is now dead. Bye bye... */
        ASSERT((Pfn1->u3.e2.ReferenceCount == 0) || (Pfn1->u3.e1.WriteInProgress));
    }
    else
    {
        /* A partly-initialized process should never exit through here */
        ASSERT(FALSE);
    }

    /* Release the PFN lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Drop a reference on the session */
    if (Process->Session)
        MiReleaseProcessReferenceToSessionDataPage(Process->Session);

    /* Clear out the PDE pages */
    Process->Pcb.DirectoryTableBase[0] = 0;
    Process->Pcb.DirectoryTableBase[1] = 0;
}

NTSTATUS
NTAPI
MmDeleteProcessAddressSpace(
    _In_ PEPROCESS Process)
{
    KIRQL OldIrql;
    PVOID Address;

    DPRINT("MmDeleteProcessAddressSpace: %p, '%s'\n", Process, Process->ImageFileName);

    OldIrql = MiAcquireExpansionLock();
    RemoveEntryList(&Process->MmProcessLinks);
    MiReleaseExpansionLock(OldIrql);

    MmLockAddressSpace(&Process->Vm);

    /* There should not be any memory areas left! */
    ASSERT(Process->Vm.WorkingSetExpansionLinks.Flink == NULL);

    #if (_MI_PAGING_LEVELS == 2)
    {
        PMMPDE Pde;

        /* Attach to Process */
        KeAttachProcess(&Process->Pcb);

        /* Acquire PFN lock */
        OldIrql = MiLockPfnDb(APC_LEVEL);

        for (Address = MI_LOWEST_VAD_ADDRESS;
             Address < MM_HIGHEST_VAD_ADDRESS;
             Address = (PVOID)((ULONG_PTR)Address + (PAGE_SIZE * PTE_PER_PAGE)))
        {
            /* At this point all references should be dead */
            if (MiQueryPageTableReferences(Address))
            {
                DPRINT1("MmDeleteProcessAddressSpace: %p, %p, %X\n", Process, Address, MiQueryPageTableReferences(Address));
                ASSERT(MiQueryPageTableReferences(Address) == 0);
            }

            Pde = MiAddressToPde(Address);

            /* Unlike in ARM3, we don't necesarrily free the PDE page as soon as reference reaches 0,
               so we must clean up a bit when process closes
            */
            if (Pde->u.Hard.Valid)
                MiDeletePte(Pde, MiPdeToPte(Pde), Process, NULL, NULL, OldIrql);

            ASSERT(Pde->u.Hard.Valid == 0);
        }

        /* Release lock */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);

        /* Detach */
        KeDetachProcess();
    }
    #endif

    MmUnlockAddressSpace(&Process->Vm);

    DPRINT("Finished MmDeleteProcessAddressSpace()\n");
    MmDeleteProcessAddressSpace2(Process);

    return(STATUS_SUCCESS);
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

INIT_FUNCTION
NTSTATUS
NTAPI
MmInitializeHandBuiltProcess(
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase)
{
    DPRINT1("MmInitializeHandBuiltProcess: Process %p\n", Process);

#if defined(ONE_CPU)

    /* Share the directory base with the idle process */
    DirectoryTableBase[0] = PsGetCurrentProcess()->Pcb.DirectoryTableBase[0];
    DirectoryTableBase[1] = PsGetCurrentProcess()->Pcb.DirectoryTableBase[1];

    /* Initialize the Addresss Space */
    KeInitializeGuardedMutex(&Process->AddressCreationLock);
    KeInitializeSpinLock(&Process->HyperSpaceLock);

    ASSERT(Process->VadRoot.NumberGenericTableElements == 0);
    Process->VadRoot.BalancedRoot.u1.Parent = &Process->VadRoot.BalancedRoot;

    ExInitializePushLock(&Process->Vm.WorkingSetMutex);

    /* Use idle process Working set */
    Process->Vm.VmWorkingSetList = MmWorkingSetList;
    Process->Vm.WorkingSetSize = PsGetCurrentProcess()->Vm.WorkingSetSize;
    KeQuerySystemTime(&Process->Vm.LastTrimTime);

    MiInsertHandBuiltProcessIntoList(Process);
    MiAllowWorkingSetExpansion(&Process->Vm);

    /* Done */
    return STATUS_SUCCESS;

#else
    return MmCreateProcessAddressSpace(0, Process, DirectoryTableBase);
#endif
}

INIT_FUNCTION
NTSTATUS
NTAPI
MmInitializeHandBuiltProcess2(
    _In_ PEPROCESS Process)
#if defined(ONE_CPU)
{
    DPRINT1("MmInitializeHandBuiltProcess2: Process %p\n", Process);

    if ((ULONG_PTR)MmHighestUserAddress <= (MM_SHARED_USER_DATA_VA + 0x10000)) // FIXME
        return STATUS_SUCCESS;

    DPRINT1("MmInitializeHandBuiltProcess2: FIXME\n");
    ASSERT(FALSE):

    return STATUS_SUCCESS;
}
#else
{
    ULONG Flags = 0;
    NTSTATUS Status;

    DPRINT1("MmInitializeHandBuiltProcess2: Process %p\n", Process);

    Status = MmInitializeProcessAddressSpace(Process, NULL, NULL, &Flags, NULL);

    if (MmVirtualBias && !PsInitialSystemProcess && NT_SUCCESS(Status))
    {
        UNIMPLEMENTED_DBGBREAK();
        return STATUS_NOT_IMPLEMENTED;
    }

    return Status;
}
#endif

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

    DPRINT("MmInitializeProcessAddressSpace: FIXME MiAllowWorkingSetExpansion\n");
    KeDetachProcess();

Exit:
    /* Return status to caller */
    DPRINT("MmInitializeProcessAddressSpace: return %X\n", Status);
    return Status;
}

NTSTATUS
NTAPI
MiCreatePebOrTeb(
    _In_ PEPROCESS Process,
    _In_ ULONG Size,
    _Out_ PULONG_PTR BaseAddress)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PMMVAD_LONG Vad;
    LARGE_INTEGER CurrentTime;
    ULONG_PTR StartingAddress;
    ULONG_PTR HighestAddress;
    ULONG_PTR RandomBase;
    BOOLEAN Result;
    NTSTATUS Status;

    DPRINT("MiCreatePebOrTeb: Process %p, Size %X\n", Process, Size);

    /* Allocate a VAD */
    Vad = ExAllocatePoolWithTag(NonPagedPool, sizeof(MMVAD_LONG), 'ldaV');
    if (!Vad)
    {
        DPRINT1("MiCreatePebOrTeb: STATUS_NO_MEMORY\n");
        return STATUS_NO_MEMORY;
    }

    /* Setup the primary flags with the size, and make it commited, private, RW */
    Vad->u.LongFlags = 0;
    Vad->u.VadFlags.CommitCharge = BYTES_TO_PAGES(Size);
    Vad->u.VadFlags.MemCommit = TRUE;
    Vad->u.VadFlags.PrivateMemory = TRUE;
    Vad->u.VadFlags.Protection = MM_READWRITE;
    Vad->u.VadFlags.NoChange = TRUE;
    Vad->u1.Parent = NULL;

    /* Setup the secondary flags to make it a secured, writable, long VAD */
    Vad->u2.LongFlags2 = 0;
    Vad->u2.VadFlags2.OneSecured = TRUE;
    Vad->u2.VadFlags2.LongVad = TRUE;
    Vad->u2.VadFlags2.ReadOnly = FALSE;

    Vad->ControlArea = NULL; // For Memory-Area hack
    Vad->FirstPrototypePte = NULL;

    /* Check if this is a PEB creation */
    ASSERT(sizeof(TEB) != sizeof(PEB));

    /* Acquire the address creation lock and make sure the process is alive */
    KeAcquireGuardedMutex(&Process->AddressCreationLock);

    if (Size == sizeof(PEB))
    {
        /* Create a random value to select one page in a 64k region */
        KeQueryTickCount(&CurrentTime);

        /* Calculate a random base address */
        RandomBase = (CurrentTime.LowPart & 0xF);

        if (RandomBase <= 1)
            RandomBase = 2;

        CurrentTime.LowPart = RandomBase;

        /* Calculate the highest allowed address */
        HighestAddress = ((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1);
        StartingAddress = (HighestAddress - (RandomBase * PAGE_SIZE));

        Result = MiCheckForConflictingVadExistence(Process, StartingAddress, (StartingAddress + (PAGE_SIZE - 1)));
    }
    else
    {
        Result = TRUE;
    }

    *BaseAddress = 0;

    if (Result)
    {
        Status = MiFindEmptyAddressRangeDownTree(ROUND_TO_PAGES(Size),
                                                 ((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1),
                                                 PAGE_SIZE,
                                                 &Process->VadRoot,
                                                 BaseAddress);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MiCreatePebOrTeb: Status %X\n", Status);
            KeReleaseGuardedMutex(&Process->AddressCreationLock);
            ExFreePoolWithTag(Vad, 'ldaV');
            return Status;
        }
    }
    else
    {
        *BaseAddress = StartingAddress;
    }

    Vad->StartingVpn = (*BaseAddress / PAGE_SIZE);
    Vad->EndingVpn = ((*BaseAddress + Size - 1) / PAGE_SIZE);

    Vad->u3.Secured.StartVpn = *BaseAddress;
    Vad->u3.Secured.EndVpn = ((Vad->EndingVpn * PAGE_SIZE) | (PAGE_SIZE - 1));

    Status = MiInsertVadCharges((PMMVAD)Vad, Process);

    if (NT_SUCCESS(Status))
    {
        MiLockProcessWorkingSetUnsafe(Process, CurrentThread);

        Process->VadRoot.NodeHint = Vad;
        MiInsertVad((PMMVAD)Vad, &Process->VadRoot);

        DPRINT("MiCreatePebOrTeb: MiInsertVad return %X\n", Result);

        MiUnlockProcessWorkingSetUnsafe(Process, CurrentThread);
        KeReleaseGuardedMutex(&Process->AddressCreationLock);

        /* Success */
        return Status;
    }

    DPRINT1("MiCreatePebOrTeb: Status %X\n", Status);

    KeReleaseGuardedMutex(&Process->AddressCreationLock);
    ExFreePoolWithTag(Vad, 'ldaV');

    /* Fail */
    return Status;
}

NTSTATUS
NTAPI
MmCreatePeb(
    _In_ PEPROCESS Process,
    _In_ PINITIAL_PEB InitialPeb,
    _Out_ PPEB* BasePeb)
{
    PIMAGE_LOAD_CONFIG_DIRECTORY ImageConfigData;
    KAFFINITY ProcessAffinityMask = 0;
    PIMAGE_NT_HEADERS NtHeaders;
    LARGE_INTEGER SectionOffset;
    PPEB Peb = NULL;
    PVOID TableBase = NULL;
    SIZE_T ViewSize = 0;
    USHORT Characteristics;
    NTSTATUS Status;

    SectionOffset.QuadPart = 0;
    *BasePeb = NULL;

    /* Attach to Process */
    KeAttachProcess(&Process->Pcb);

    /* Map NLS Tables */
    Status = MmMapViewOfSection(ExpNlsSectionPointer,
                                (PEPROCESS)Process,
                                &TableBase,
                                0,
                                0,
                                &SectionOffset,
                                &ViewSize,
                                ViewShare,
                                MEM_TOP_DOWN,
                                PAGE_READONLY);

    DPRINT("NLS Tables at: %p\n", TableBase);

    if (!NT_SUCCESS(Status))
    {
        /* Cleanup and exit */
        KeDetachProcess();

        DPRINT1("MmCreatePeb: Status %X\n", Status);
        return Status;
    }

    /* Allocate the PEB */
    Status = MiCreatePebOrTeb(Process, sizeof(PEB), (PULONG_PTR)&Peb);
    DPRINT("PEB at: %p\n", Peb);
    if (!NT_SUCCESS(Status))
    {
        /* Cleanup and exit */
        KeDetachProcess();

        DPRINT1("MmCreatePeb: Status %X\n", Status);
        return Status;
    }

    /* Use SEH in case we can't load the PEB */
    _SEH2_TRY
    {
        /* Initialize the PEB */
        RtlZeroMemory(Peb, sizeof(PEB));

        /* Set up data */
        Peb->ImageBaseAddress = Process->SectionBaseAddress;
        Peb->InheritedAddressSpace = InitialPeb->InheritedAddressSpace;
        Peb->Mutant = InitialPeb->Mutant;
        Peb->ImageUsesLargePages = InitialPeb->ImageUsesLargePages;

        /* NLS */
        Peb->AnsiCodePageData = ((PCHAR)TableBase + ExpAnsiCodePageDataOffset);
        Peb->OemCodePageData = ((PCHAR)TableBase + ExpOemCodePageDataOffset);
        Peb->UnicodeCaseTableData = ((PCHAR)TableBase + ExpUnicodeCaseTableDataOffset);

        /* Default Version Data (could get changed below) */
        Peb->OSMajorVersion = NtMajorVersion;
        Peb->OSMinorVersion = NtMinorVersion;
        Peb->OSBuildNumber = (USHORT)(NtBuildNumber & 0x3FFF);
        Peb->OSPlatformId = VER_PLATFORM_WIN32_NT;
        Peb->OSCSDVersion = (USHORT)CmNtCSDVersion;

        /* Heap and Debug Data */
        Peb->NumberOfProcessors = KeNumberProcessors;
        Peb->BeingDebugged = (BOOLEAN)(Process->DebugPort != NULL);
        Peb->NtGlobalFlag = NtGlobalFlag;
        Peb->HeapSegmentReserve = MmHeapSegmentReserve;
        Peb->HeapSegmentCommit = MmHeapSegmentCommit;
        Peb->HeapDeCommitTotalFreeThreshold = MmHeapDeCommitTotalFreeThreshold;
        Peb->HeapDeCommitFreeBlockThreshold = MmHeapDeCommitFreeBlockThreshold;
        Peb->CriticalSectionTimeout = MmCriticalSectionTimeout;
        Peb->MinimumStackCommit = MmMinimumStackCommitInBytes;
        Peb->MaximumNumberOfHeaps = ((PAGE_SIZE - sizeof(PEB)) / sizeof(PVOID));
        Peb->ProcessHeaps = (PVOID *)(Peb + 1);

        /* Session ID */
        if (Process->Session)
            Peb->SessionId = MmGetSessionId(Process);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Fail */
        KeDetachProcess();
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* Use SEH in case we can't load the image */
    _SEH2_TRY
    {
        /* Get NT Headers */
        NtHeaders = RtlImageNtHeader(Peb->ImageBaseAddress);
        Characteristics = NtHeaders->FileHeader.Characteristics;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Fail */
        KeDetachProcess();
        _SEH2_YIELD(return STATUS_INVALID_IMAGE_PROTECT);
    }
    _SEH2_END;

    /* Parse the headers */
    if (NtHeaders)
    {
        /* Use SEH in case we can't load the headers */
        _SEH2_TRY
        {
            /* Get the Image Config Data too */
            ImageConfigData = RtlImageDirectoryEntryToData(Peb->ImageBaseAddress,
                                                           TRUE,
                                                           IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG,
                                                           (PULONG)&ViewSize);
            if (ImageConfigData)
                /* Probe it */
                ProbeForRead(ImageConfigData, sizeof(IMAGE_LOAD_CONFIG_DIRECTORY), sizeof(ULONG));

            /* Write subsystem data */
            Peb->ImageSubsystem = NtHeaders->OptionalHeader.Subsystem;
            Peb->ImageSubsystemMajorVersion = NtHeaders->OptionalHeader.MajorSubsystemVersion;
            Peb->ImageSubsystemMinorVersion = NtHeaders->OptionalHeader.MinorSubsystemVersion;

            /* Check for version data */
            if (NtHeaders->OptionalHeader.Win32VersionValue)
            {
                /* Extract values and write them */
                Peb->OSMajorVersion = (NtHeaders->OptionalHeader.Win32VersionValue & (0x100 - 1));
                Peb->OSMinorVersion = ((NtHeaders->OptionalHeader.Win32VersionValue >> 8) & (0x100 - 1));
                Peb->OSBuildNumber = ((NtHeaders->OptionalHeader.Win32VersionValue >> 16) & (0x4000 - 1));
                Peb->OSPlatformId = ((NtHeaders->OptionalHeader.Win32VersionValue >> 30) ^ 2);

                /* Process CSD version override */
                if (ImageConfigData && ImageConfigData->CSDVersion)
                    /* Take the value from the image configuration directory */
                    Peb->OSCSDVersion = ImageConfigData->CSDVersion;
            }

            /* Process optional process affinity mask override */
            if (ImageConfigData && ImageConfigData->ProcessAffinityMask)
                /* Take the value from the image configuration directory */
                ProcessAffinityMask = ImageConfigData->ProcessAffinityMask;

            /* Check if this is a UP image */
            if (Characteristics & IMAGE_FILE_UP_SYSTEM_ONLY)
                /* Force it to use CPU 0 */
                /* FIXME: this should use the MmRotatingUniprocessorNumber */
                Peb->ImageProcessAffinityMask = 0;
            else
                /* Whatever was configured */
                Peb->ImageProcessAffinityMask = ProcessAffinityMask;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Fail */
            KeDetachProcess();
            _SEH2_YIELD(return STATUS_INVALID_IMAGE_PROTECT);
        }
        _SEH2_END;
    }

    /* Detach from the Process */
    KeDetachProcess();

    *BasePeb = Peb;

    return STATUS_SUCCESS;
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
    PTEB Teb;
    NTSTATUS Status = STATUS_SUCCESS;

    *BaseTeb = NULL;

    /* Attach to Target */
    KeAttachProcess(&Process->Pcb);

    /* Allocate the TEB */
    Status = MiCreatePebOrTeb(Process, sizeof(TEB), (PULONG_PTR)&Teb);
    if (!NT_SUCCESS(Status))
    {
        /* Cleanup and exit */
        KeDetachProcess();

        DPRINT1("MmCreateTeb: Status %X\n", Status);
        return Status;
    }

    /* Use SEH in case we can't load the TEB */
    _SEH2_TRY
    {
        /* Initialize the PEB */
        RtlZeroMemory(Teb, sizeof(TEB));

        /* Set TIB Data */
        Teb->NtTib.Self = (PNT_TIB)Teb;

      #ifdef _M_AMD64
        Teb->NtTib.ExceptionList = NULL;
      #else
        Teb->NtTib.ExceptionList = EXCEPTION_CHAIN_END;
      #endif

        /* Identify this as an OS/2 V3.0 ("Cruiser") TIB */
        Teb->NtTib.Version = (30 << 8);

        /* Set TEB Data */
        Teb->ClientId = *ClientId;
        Teb->RealClientId = *ClientId;
        Teb->ProcessEnvironmentBlock = Process->Peb;
        Teb->CurrentLocale = PsDefaultThreadLocaleId;

        /* Check if we have a grandparent TEB */
        if (!InitialTeb->PreviousStackBase &&
            !InitialTeb->PreviousStackLimit)
        {
            /* Use initial TEB values */
            Teb->NtTib.StackBase = InitialTeb->StackBase;
            Teb->NtTib.StackLimit = InitialTeb->StackLimit;
            Teb->DeallocationStack = InitialTeb->AllocatedStackBase;
        }
        else
        {
            /* Use grandparent TEB values */
            Teb->NtTib.StackBase = InitialTeb->PreviousStackBase;
            Teb->NtTib.StackLimit = InitialTeb->PreviousStackLimit;
        }

        /* Initialize the static unicode string */
        Teb->StaticUnicodeString.MaximumLength = sizeof(Teb->StaticUnicodeBuffer);
        Teb->StaticUnicodeString.Buffer = Teb->StaticUnicodeBuffer;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Get error code */
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    KeDetachProcess();

    *BaseTeb = Teb;

    return Status;
}

NTSTATUS
NTAPI
MmGrowKernelStackEx(
    _In_ PVOID StackPointer,
    _In_ ULONG GrowSize)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PMMPTE LimitPte;
    PMMPTE NewLimitPte;
    PMMPTE LastPte;
    MMPTE TempPte;
    MMPTE InvalidPte;
    PFN_NUMBER PageFrameIndex;
    PFN_NUMBER PagesCount;
    KIRQL OldIrql;

    /* Make sure the stack did not overflow */
    ASSERT(((ULONG_PTR)Thread->StackBase - (ULONG_PTR)Thread->StackLimit) <= (MmLargeStackSize + PAGE_SIZE));

    /* Get the current stack limit */
    LimitPte = MiAddressToPte(Thread->StackLimit);
    ASSERT(LimitPte->u.Hard.Valid == 1);

    /* Get the new one and make sure this isn't a retarded request */
    NewLimitPte = MiAddressToPte((PVOID)((ULONG_PTR)StackPointer - GrowSize));
    if (NewLimitPte == LimitPte)
        return STATUS_SUCCESS;

    /* Now make sure you're not going past the reserved space */
    LastPte = MiAddressToPte((PVOID)((ULONG_PTR)Thread->StackBase - MmLargeStackSize));
    if (NewLimitPte < LastPte)
    {
        /* Sorry! */
        DPRINT1("MmGrowKernelStackEx: STATUS_STACK_OVERFLOW\n");
        return STATUS_STACK_OVERFLOW;
    }

    /* Calculate the number of new pages */
    LimitPte--;

    /* Setup the temporary invalid PTE */
    MI_MAKE_SOFTWARE_PTE(&InvalidPte, MM_NOACCESS);

    PagesCount = (PFN_NUMBER)(LimitPte - NewLimitPte + 1);
    DPRINT("MmGrowKernelStackEx: PagesCount %X\n", PagesCount);

    /* Acquire the PFN DB lock */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    if (PagesCount >= (MmResidentAvailablePages - MmSystemLockPagesCount))
    {
        DPRINT1("MmGrowKernelStackEx: %IX, %IX\n", MmResidentAvailablePages, MmSystemLockPagesCount);
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        return STATUS_NO_MEMORY;
    }

    InterlockedExchangeAddSizeT(&MmResidentAvailablePages, -PagesCount);

    /* Loop each stack page */
    while (LimitPte >= NewLimitPte)
    {
        if (MmAvailablePages < 0x80)
            MiEnsureAvailablePageOrWait(NULL, OldIrql);

        /* Get a page and write the current invalid PTE */
        MI_SET_USAGE(MI_USAGE_KERNEL_STACK_EXPANSION);
        MI_SET_PROCESS2(PsGetCurrentProcess()->ImageFileName);
        PageFrameIndex = MiRemoveAnyPage(MI_GET_NEXT_COLOR());
        MI_WRITE_INVALID_PTE(LimitPte, InvalidPte);

        /* Initialize the PFN entry for this page */
        MiInitializePfn(PageFrameIndex, LimitPte, 1);

        /* Setup the template stack PTE */
        MI_MAKE_HARDWARE_PTE_KERNEL(&TempPte, LimitPte, MM_READWRITE, PageFrameIndex);

        /* Set it dirty */
        MI_MAKE_DIRTY_PAGE(&TempPte);

        /* Write the valid PTE */
        MI_WRITE_VALID_PTE(LimitPte--, TempPte);
    }

    /* Release the PFN lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Set the new limit */
    Thread->StackLimit = (ULONG_PTR)MiPteToAddress(NewLimitPte);

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmGrowKernelStack(
    _In_ PVOID StackPointer)
{
    /* Call the extended version */
    return MmGrowKernelStackEx(StackPointer, KERNEL_LARGE_STACK_COMMIT);
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

    /* Set it dirty */
    MI_MAKE_DIRTY_PAGE(&TempPte);

    /* Acquire the PFN DB lock */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* Loop each stack page */
    for (ix = 0; ix < StackPages; ix++)
    {
        Pte++;

        if (MmAvailablePages < 0x80)
            MiEnsureAvailablePageOrWait(NULL, OldIrql);

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
    PSLIST_ENTRY SListEntry;
    PMMPTE Pte;
    PMMPFN Pfn;
    PFN_NUMBER PageFrameNumber;
    PFN_COUNT StackPages;
    ULONG ix;
    KIRQL OldIrql;

    /* This should be the guard page, so decrement by one */
    Pte = MiAddressToPte(StackBase);
    Pte--;

    /* If this is a small stack, just push the stack onto the dead stack S-LIST */
    if (!GuiStack &&
        ExQueryDepthSList(&MmDeadStackSListHead) < MmMaximumDeadKernelStacks)
    {
        SListEntry = (((PSLIST_ENTRY)StackBase) - 1);
        InterlockedPushEntrySList(&MmDeadStackSListHead, SListEntry);
        return;
    }

    /* Calculate pages used */
    StackPages = BYTES_TO_PAGES(GuiStack ? MmLargeStackSize : KERNEL_STACK_SIZE);

    /* Acquire the PFN lock */
    OldIrql = MiLockPfnDb(APC_LEVEL);

    /* Loop them */
    for (ix = 0; ix < StackPages; ix++, Pte--)
    {
        /* Check if this is a valid PTE */
        if (!Pte->u.Hard.Valid)
            continue;

        /* Get the PTE's page */
        PageFrameNumber = PFN_FROM_PTE(Pte);
        Pfn = MiGetPfnEntry(PageFrameNumber);

        /* Now get the page of the page table mapping it.
           Remove a shared reference, since the page is going away
        */
        MiDecrementShareCount(MiGetPfnEntry(Pfn->u4.PteFrame), Pfn->u4.PteFrame);

        /* Set the special pending delete marker */
        MI_SET_PFN_DELETED(Pfn);

        /* And now delete the actual stack page */
        MiDecrementShareCount(Pfn, PageFrameNumber);
    }

    /* We should be at the guard page now */
    ASSERT(Pte->u.Hard.Valid == 0);

    /* Release the PFN lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Release the PTEs */
    MiReleaseSystemPtes(Pte, (StackPages + 1), SystemPteSpace);
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
