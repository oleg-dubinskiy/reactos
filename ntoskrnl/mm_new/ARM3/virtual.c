
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

extern PVOID MmPagedPoolEnd;

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
MmCopyVirtualMemory(
    _In_ PEPROCESS SourceProcess,
    _In_ PVOID SourceAddress,
    _In_ PEPROCESS TargetProcess,
    _Out_ PVOID TargetAddress,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PSIZE_T ReturnSize)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PFN_COUNT
NTAPI
MiDeleteSystemPageableVm(
    _In_ PMMPTE Pte,
    _In_ PFN_NUMBER PageCount,
    _In_ ULONG Flags,
    _Out_ PPFN_NUMBER ValidPages)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PFN_COUNT ActualPages = 0;
    PMMPFN Pfn1;
    PMMPFN Pfn2;
    PFN_NUMBER PageFrameIndex;
    PFN_NUMBER PageTableIndex;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    /* Lock the system working set */
    MiLockWorkingSet(CurrentThread, &MmSystemCacheWs);

    /* Loop all pages */
    for (; PageCount; Pte++, PageCount--)
    {
        /* Make sure there's some data about the page */
        if (!Pte->u.Long)
            continue;

        /* As always, only handle current ARM3 scenarios */
        ASSERT(Pte->u.Soft.Prototype == 0);
        ASSERT(Pte->u.Soft.Transition == 0);

        /* Normally this is one possibility -- freeing a valid page */
        if (!Pte->u.Hard.Valid)
        {
            /* The only other ARM3 possibility is a demand zero page,
               which would mean freeing some of the paged pool pages that haven't even been touched yet,
               as part of a larger allocation.

               Right now, we shouldn't expect any page file information in the PTE
            */
            ASSERT(Pte->u.Soft.PageFileHigh == 0);

            /* Destroy the PTE */
            MI_ERASE_PTE(Pte);

            /* Actual legitimate pages */
            ActualPages++;

            continue;
        }

        /* Get the page PFN */
        PageFrameIndex = PFN_FROM_PTE(Pte);
        Pfn1 = MiGetPfnEntry(PageFrameIndex);

        /* Should not have any working set data yet */
        ASSERT(Pfn1->u1.WsIndex == 0);

        /* Actual valid, legitimate, pages */
        if (ValidPages)
            (*ValidPages)++;

        /* Get the page table entry */
        PageTableIndex = Pfn1->u4.PteFrame;
        Pfn2 = MiGetPfnEntry(PageTableIndex);

        /* Lock the PFN database */
        OldIrql = MiLockPfnDb(APC_LEVEL);

        /* Delete it the page */
        MI_SET_PFN_DELETED(Pfn1);
        MiDecrementShareCount(Pfn1, PageFrameIndex);

        /* Decrement the page table too */
        MiDecrementShareCount(Pfn2, PageTableIndex);

        /* Release the PFN database */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);

        /* Destroy the PTE */
        MI_ERASE_PTE(Pte);

        /* Actual legitimate pages */
        ActualPages++;
    }

    /* Release the working set */
    MiUnlockWorkingSet(CurrentThread, &MmSystemCacheWs);

    /* Flush the entire TLB */
    KeFlushEntireTb(TRUE, TRUE);

    /* Done */
    return ActualPages;
}

ULONG
NTAPI
MiMakeSystemAddressValid(
    _In_ PVOID PageTableVirtualAddress,
    _In_ PEPROCESS CurrentProcess)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    BOOLEAN WsShared = FALSE;
    BOOLEAN WsSafe = FALSE;
    BOOLEAN LockChange = FALSE;
    NTSTATUS Status;

    /* Must be a non-pool page table, since those are double-mapped already */
    ASSERT(PageTableVirtualAddress > MM_HIGHEST_USER_ADDRESS);
    ASSERT(PageTableVirtualAddress < MmPagedPoolStart || PageTableVirtualAddress > MmPagedPoolEnd);

    /* Working set lock or PFN lock should be held */
    ASSERT(KeAreAllApcsDisabled() == TRUE);

    /* Check if the page table is valid */
    while (!MmIsAddressValid(PageTableVirtualAddress))
    {
        /* Release the working set lock */
        MiUnlockProcessWorkingSetForFault(CurrentProcess, CurrentThread, &WsSafe, &WsShared);

        /* Fault it in */
        Status = MmAccessFault(FALSE, PageTableVirtualAddress, KernelMode, NULL);
        if (!NT_SUCCESS(Status))
        {
            /* This should not fail */
            ASSERT(FALSE);
            KeBugCheckEx(KERNEL_DATA_INPAGE_ERROR,
                         1,
                         Status,
                         (ULONG_PTR)CurrentProcess,
                         (ULONG_PTR)PageTableVirtualAddress);
        }

        /* Lock the working set again */
        MiLockProcessWorkingSetForFault(CurrentProcess, CurrentThread, WsSafe, WsShared);

        /* This flag will be useful later when we do better locking */
        LockChange = TRUE;
    }

    /* Let caller know what the lock state is */
    return LockChange;
}

VOID
NTAPI
MiMakePdeExistAndMakeValid(
    _In_ PMMPDE Pde,
    _In_ PEPROCESS TargetProcess,
    _In_ KIRQL OldIrql)
{
   PMMPTE Pte;
   PMMPTE Ppe;
   PMMPTE Pxe;

   /* Sanity checks.
      The latter is because we only use this function with the PFN lock not held, so it may go away in the future.
   */
   ASSERT(KeAreAllApcsDisabled() == TRUE);
   ASSERT(OldIrql == MM_NOIRQL);

   /* Also get the PPE and PXE.
     This is okay not to #ifdef because they will return the same address as the PDE on 2-level page table systems.

      If everything is already valid, there is nothing to do.
   */
   Ppe = MiAddressToPte(Pde);
   Pxe = MiAddressToPde(Pde);

   if (Pxe->u.Hard.Valid &&
       Ppe->u.Hard.Valid &&
       Pde->u.Hard.Valid)
   {
       return;
   }

   /* At least something is invalid, so begin by getting the PTE for the PDE itself and then lookup each additional level.
      We must do it in this precise order because the pagfault.c code (as well as in Windows)
      depends that the next level up (higher) must be valid when faulting a lower level
   */
   Pte = MiPteToAddress(Pde);

   do
   {
       /* Make sure APCs continued to be disabled */
       ASSERT(KeAreAllApcsDisabled() == TRUE);

       /* First, make the PXE valid if needed */
       if (!Pxe->u.Hard.Valid)
       {
           MiMakeSystemAddressValid(Ppe, TargetProcess);
           ASSERT(Pxe->u.Hard.Valid == 1);
       }

       /* Next, the PPE */
       if (!Ppe->u.Hard.Valid)
       {
           MiMakeSystemAddressValid(Pde, TargetProcess);
           ASSERT(Ppe->u.Hard.Valid == 1);
       }

       /* And finally, make the PDE itself valid. */
       MiMakeSystemAddressValid(Pte, TargetProcess);

       /* This should've worked the first time so the loop is really just for show -- ASSERT
          that we're actually NOT going to be looping.
       */
       ASSERT(Pxe->u.Hard.Valid == 1);
       ASSERT(Ppe->u.Hard.Valid == 1);
       ASSERT(Pde->u.Hard.Valid == 1);
   }
   while (!Pxe->u.Hard.Valid || !Ppe->u.Hard.Valid || !Pde->u.Hard.Valid);
}

ULONG
NTAPI
MiDeletePte(IN PMMPTE PointerPte,
            IN PVOID VirtualAddress,
            IN PEPROCESS CurrentProcess,
            IN PMMPTE PrototypePte,
            IN PMMPTE_FLUSH_LIST PteFlushList,
            IN KIRQL OldIrql)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

VOID
NTAPI
MiDeleteVirtualAddresses(
    _In_ ULONG_PTR Va,
    _In_ ULONG_PTR EndingAddress,
    _In_ PMMVAD Vad)
{
    PMMPDE Pde;
    PMMPTE Pte;
    PMMPTE Proto;
    PMMPTE LastProto;
    MMPTE TempPte;
    PEPROCESS CurrentProcess;
    KIRQL OldIrql;
    BOOLEAN AddressGap = FALSE;
    PSUBSECTION Subsection;

    DPRINT("MiDeleteVirtualAddresses: Va %p, EndingAddress %p, Vad %p\n", Va, EndingAddress, Vad);

    /* Get out if this is a fake VAD, RosMm will free the marea pages */
    if (Vad && Vad->u.VadFlags.Spare)
        return;

    /* Grab the process and PTE/PDE for the address being deleted */
    CurrentProcess = PsGetCurrentProcess();

    Pde = MiAddressToPde(Va);
    Pte = MiAddressToPte(Va);

    DPRINT("MiDeleteVirtualAddresses: Pde %p, Pte %p\n", Pde, Pte);

    /* Check if this is a section VAD or a VM VAD */
    if (!Vad || Vad->u.VadFlags.PrivateMemory || !Vad->FirstPrototypePte)
    {
        /* Don't worry about prototypes */
        Proto = LastProto = NULL;
    }
    else
    {
        /* Get the prototype PTE */
        Proto = Vad->FirstPrototypePte;
        LastProto = (Vad->FirstPrototypePte + 1);
    }

    /* In all cases, we don't support fork() yet */
    ASSERT(CurrentProcess->CloneRoot == NULL);

    /* Loop the PTE for each VA */
    while (TRUE)
    {
        /* First keep going until we find a valid PDE */
        while (!Pde->u.Long)
        {
            /* There are gaps in the address space */
            AddressGap = TRUE;

            /* Still no valid PDE, try the next 4MB (or whatever) */
            Pde++;

            /* Update the PTE on this new boundary */
            Pte = MiPteToAddress(Pde);

            /* Check if all the PDEs are invalid, so there's nothing to free */
            Va = (ULONG_PTR)MiPteToAddress(Pte);
            if (Va > EndingAddress)
            {
                DPRINT1("MiDeleteVirtualAddresses: Va %p, EndingAddress %p, Vad %p\n", Va, EndingAddress, Vad);
                return;
            }
        }

        /* Now check if the PDE is mapped in */
        if (!Pde->u.Hard.Valid)
        {
            /* It isn't, so map it in */
            Pte = MiPteToAddress(Pde);
            MiMakeSystemAddressValid(Pte, CurrentProcess);
        }

        /* Now we should have a valid PDE, mapped in, and still have some VA */
        ASSERT(Pde->u.Hard.Valid == 1);
        ASSERT(Va <= EndingAddress);

        /* Check if this is a section VAD with gaps in it */
        if (AddressGap && LastProto)
        {
            /* We need to skip to the next correct prototype PTE */
            Proto = MI_GET_PROTOTYPE_PTE_FOR_VPN(Vad, (Va / PAGE_SIZE));

            /* And we need the subsection to skip to the next last prototype PTE */
            Subsection = MiLocateSubsection(Vad, (Va / PAGE_SIZE));

            if (Subsection)
                /* Found it! */
                LastProto = &Subsection->SubsectionBase[Subsection->PtesInSubsection];
            else
                /* No more subsections, we are done with prototype PTEs */
                Proto = NULL;
        }

        /* Lock the PFN Database while we delete the PTEs */
        OldIrql = MiLockPfnDb(APC_LEVEL);
        do
        {
            /* Capture the PDE and make sure it exists */
            TempPte = *Pte;

            if (TempPte.u.Long)
            {
                MiDecrementPageTableReferences((PVOID)Va);

                /* Check if the PTE is actually mapped in */
                if (MI_IS_MAPPED_PTE(&TempPte))
                {
                    /* Are we dealing with section VAD? */
                    if (LastProto && Proto > LastProto)
                    {
                        /* We need to skip to the next correct prototype PTE */
                        Proto = MI_GET_PROTOTYPE_PTE_FOR_VPN(Vad, (Va / PAGE_SIZE));

                        /* And we need the subsection to skip to the next last prototype PTE */
                        Subsection = MiLocateSubsection(Vad, (Va / PAGE_SIZE));

                        if (Subsection)
                        {
                            /* Found it! */
                            LastProto = &Subsection->SubsectionBase[Subsection->PtesInSubsection];
                        }
                        else
                        {
                            /* No more subsections, we are done with prototype PTEs */
                            Proto = NULL;
                        }
                    }

                    /* Check for prototype PTE */
                    if (!TempPte.u.Hard.Valid && TempPte.u.Soft.Prototype)
                        /* Just nuke it */
                        MI_ERASE_PTE(Pte);
                    else
                        /* Delete the PTE proper */
                        MiDeletePte(Pte, (PVOID)Va, CurrentProcess, Proto, NULL, OldIrql);
                }
                else
                {
                    /* The PTE was never mapped, just nuke it here */
                    MI_ERASE_PTE(Pte);
                }
            }

            /* Update the address and PTE for it */
            Va += PAGE_SIZE;
            Pte++;
            Proto++;

            /* Making sure the PDE is still valid */
            ASSERT(Pde->u.Hard.Valid == 1);
        }
        while ((Va & (PDE_MAPPED_VA - 1)) && Va <= EndingAddress);

        /* The PDE should still be valid at this point */
        ASSERT(Pde->u.Hard.Valid == 1);

        /* Check remaining PTE count (go back 1 page due to above loop) */
        if (!MiQueryPageTableReferences((PVOID)(Va - PAGE_SIZE)))
        {
            if (Pde->u.Long)
                 /* Delete the PTE proper */
                MiDeletePte(Pde, MiPteToAddress(Pde), CurrentProcess, NULL, NULL, OldIrql);
        }

        /* Release the lock and get out if we're done */
        MiUnlockPfnDb(OldIrql, APC_LEVEL);

        if (Va > EndingAddress)
            return;

        /* Otherwise, we exited because we hit a new PDE boundary, so start over */
        Pde = MiAddressToPde(Va);
        AddressGap = FALSE;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

PHYSICAL_ADDRESS
NTAPI
MmGetPhysicalAddress(
    PVOID Address)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    MMPDE TempPde;
    MMPTE TempPte;

    TempPde = *MiAddressToPde(Address);

    /* Check if the PXE/PPE/PDE is valid */
    if (!TempPde.u.Hard.Valid)
    {
        //KeRosDumpStackFrames(NULL, 20);
        DPRINT1("MM:MmGetPhysicalAddress: Failed base address was %p\n", Address);
        ASSERT(FALSE);

        PhysicalAddress.QuadPart = 0;
    }
    /* Check for large pages */
    else if (TempPde.u.Hard.LargePage)
    {
        /* Physical address is base page + large page offset */
        PhysicalAddress.QuadPart = ((ULONG64)TempPde.u.Hard.PageFrameNumber << PAGE_SHIFT);
        PhysicalAddress.QuadPart += ((ULONG_PTR)Address & (PAGE_SIZE * PTE_PER_PAGE - 1));
    }
    else
    {
        /* Check if the PTE is valid */
        TempPte = *MiAddressToPte(Address);
        if (TempPte.u.Hard.Valid)
        {
            /* Physical address is base page + page offset */
            PhysicalAddress.QuadPart = ((ULONG64)TempPte.u.Hard.PageFrameNumber << PAGE_SHIFT);
            PhysicalAddress.QuadPart += ((ULONG_PTR)Address & (PAGE_SIZE - 1));
        }
    }

    return PhysicalAddress;
}

PVOID
NTAPI
MmGetVirtualForPhysical(
    _In_ PHYSICAL_ADDRESS PhysicalAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
MmSecureVirtualMemory(
    _In_ PVOID Address,
    _In_ SIZE_T Length,
    _In_ ULONG Mode)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmUnsecureVirtualMemory(
    _In_ PVOID SecureMem)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtReadVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _Out_ PVOID Buffer,
    _In_ SIZE_T NumberOfBytesToRead,
    _Out_ PSIZE_T NumberOfBytesRead OPTIONAL)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtWriteVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _In_ PVOID Buffer,
    _In_ SIZE_T NumberOfBytesToWrite,
    _Out_ PSIZE_T NumberOfBytesWritten OPTIONAL)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtFlushInstructionCache(
    _In_ HANDLE ProcessHandle,
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtProtectVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* UnsafeBaseAddress,
    _Inout_ SIZE_T* UnsafeNumberOfBytesToProtect,
    _In_ ULONG NewAccessProtection,
    _Out_ PULONG UnsafeOldAccessProtection)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtLockVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* BaseAddress,
    _Inout_ PSIZE_T NumberOfBytesToLock,
    _In_ ULONG MapType)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtUnlockVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* BaseAddress,
    _Inout_ PSIZE_T NumberOfBytesToUnlock,
    _In_ ULONG MapType)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtFlushVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* BaseAddress,
    _Inout_ PSIZE_T NumberOfBytesToFlush,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtGetWriteWatch(
    _In_ HANDLE ProcessHandle,
    _In_ ULONG Flags,
    _In_ PVOID BaseAddress,
    _In_ SIZE_T RegionSize,
    _In_ PVOID* UserAddressArray,
    _Out_ PULONG_PTR EntriesInUserAddressArray,
    _Out_ PULONG Granularity)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtResetWriteWatch(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _In_ SIZE_T RegionSize)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtQueryVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _In_ MEMORY_INFORMATION_CLASS MemoryInformationClass,
    _Out_ PVOID MemoryInformation,
    _In_ SIZE_T MemoryInformationLength,
    _Out_ PSIZE_T ReturnLength)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtAllocateVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* UBaseAddress,
    _In_ ULONG_PTR ZeroBits,
    _Inout_ PSIZE_T URegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG Protect)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtFreeVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID* UBaseAddress,
    _In_ PSIZE_T URegionSize,
    _In_ ULONG FreeType)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
