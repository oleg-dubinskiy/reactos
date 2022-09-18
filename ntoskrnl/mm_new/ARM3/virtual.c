
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

#define MI_MAPPED_COPY_PAGES  0xE // 14
#define MI_POOL_COPY_BYTES    0x200
#define MI_MAX_TRANSFER_SIZE  (0x40 * _1KB) // 64 Kb

extern PVOID MmPagedPoolEnd;
extern KGUARDED_MUTEX MmSectionCommitMutex;
extern SIZE_T MmSharedCommit;
extern MMPTE MmDecommittedPte;

/* FUNCTIONS ******************************************************************/

LONG
MiGetExceptionInfo(
    _In_ PEXCEPTION_POINTERS ExceptionInfo,
    _Out_ PBOOLEAN HaveBadAddress,
    _Out_ PULONG_PTR BadAddress)
{
    PEXCEPTION_RECORD ExceptionRecord;

    PAGED_CODE();

    /* Assume default */
    *HaveBadAddress = FALSE;

    /* Get the exception record */
    ExceptionRecord = ExceptionInfo->ExceptionRecord;

    /* Look at the exception code */
    if (ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION ||
        ExceptionRecord->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION ||
        ExceptionRecord->ExceptionCode == STATUS_IN_PAGE_ERROR)
    {
        /* We can tell the address if we have more than one parameter */
        if (ExceptionRecord->NumberParameters > 1)
        {
            /* Return the address */
            *HaveBadAddress = TRUE;
            *BadAddress = ExceptionRecord->ExceptionInformation[1];
        }
    }

    /* Continue executing the next handler */
    return EXCEPTION_EXECUTE_HANDLER;
}

NTSTATUS
NTAPI
MiDoMappedCopy(
    _In_ PEPROCESS SourceProcess,
    _In_ PVOID SourceAddress,
    _In_ PEPROCESS TargetProcess,
    _Out_ PVOID TargetAddress,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PSIZE_T ReturnSize)
{
    PFN_NUMBER MdlBuffer[(sizeof(MDL) / sizeof(PFN_NUMBER)) + MI_MAPPED_COPY_PAGES + 1];
    PMDL Mdl = (PMDL)MdlBuffer;
    volatile PVOID MdlAddress = NULL;
    PVOID CurrentAddress = SourceAddress;
    PVOID CurrentTargetAddress = TargetAddress;
    ULONG_PTR BadAddress;
    SIZE_T TotalSize;
    SIZE_T CurrentSize;
    SIZE_T RemainingSize;
    KAPC_STATE ApcState;
    BOOLEAN HaveBadAddress;
    volatile BOOLEAN FailedInProbe = FALSE;
    volatile BOOLEAN PagesLocked = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    /* Calculate the maximum amount of data to move */
    TotalSize = (MI_MAPPED_COPY_PAGES * PAGE_SIZE);

    if (BufferSize <= TotalSize)
        TotalSize = BufferSize;

    CurrentSize = TotalSize;
    RemainingSize = BufferSize;

    /* Loop as long as there is still data */
    while (RemainingSize > 0)
    {
        /* Check if this transfer will finish everything off */
        if (RemainingSize < CurrentSize)
            CurrentSize = RemainingSize;

        /* Attach to the source address space */
        KeStackAttachProcess(&SourceProcess->Pcb, &ApcState);

        /* Check state for this pass */
        ASSERT(MdlAddress == NULL);
        ASSERT(PagesLocked == FALSE);
        ASSERT(FailedInProbe == FALSE);

        /* Protect user-mode copy */
        _SEH2_TRY
        {
            /* If this is our first time, probe the buffer */
            if (CurrentAddress == SourceAddress && PreviousMode != KernelMode)
            {
                /* Catch a failure here */
                FailedInProbe = TRUE;

                /* Do the probe */
                ProbeForRead(SourceAddress, BufferSize, sizeof(CHAR));

                /* Passed */
                FailedInProbe = FALSE;
            }

            /* Initialize and probe and lock the MDL */
            MmInitializeMdl(Mdl, CurrentAddress, CurrentSize);
            MmProbeAndLockPages(Mdl, PreviousMode, IoReadAccess);

            PagesLocked = TRUE;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END

        /* Detach from source process */
        KeUnstackDetachProcess(&ApcState);

        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("MiDoMappedCopy: Status %X\n", Status);
            goto Exit;
        }

        /* Now map the pages */
        MdlAddress = MmMapLockedPagesSpecifyCache(Mdl,
                                                  KernelMode,
                                                  MmCached,
                                                  NULL,
                                                  FALSE,
                                                  HighPagePriority);
        if (!MdlAddress)
        {
            DPRINT1("MiDoMappedCopy: STATUS_INSUFFICIENT_RESOURCES\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        /* Grab to the target process */
        KeStackAttachProcess(&TargetProcess->Pcb, &ApcState);

        _SEH2_TRY
        {
            /* Check if this is our first time through */
            if (CurrentTargetAddress == TargetAddress && PreviousMode != KernelMode)
            {
                /* Catch a failure here */
                FailedInProbe = TRUE;

                /* Do the probe */
                ProbeForWrite(TargetAddress, BufferSize, sizeof(CHAR));

                /* Passed */
                FailedInProbe = FALSE;
            }

            /* Now do the actual move */
            RtlCopyMemory(CurrentTargetAddress, MdlAddress, CurrentSize);
        }
        _SEH2_EXCEPT(MiGetExceptionInfo(_SEH2_GetExceptionInformation(),
                                        &HaveBadAddress,
                                        &BadAddress))
        {
            *ReturnSize = BufferSize - RemainingSize;

            /* Check if we failed during the probe */
            if (FailedInProbe)
            {
                /* Exit */
                Status = _SEH2_GetExceptionCode();
            }
            else
            {
                /* Othewise we failed during the move.
                   Check if we know exactly where we stopped copying
                */
                if (HaveBadAddress)
                    /* Return the exact number of bytes copied */
                    *ReturnSize = (BadAddress - (ULONG_PTR)SourceAddress);

                /* Return partial copy */
                Status = STATUS_PARTIAL_COPY;
            }
        }
        _SEH2_END;

        /* Detach from target process */
        KeUnstackDetachProcess(&ApcState);

        /* Check for SEH status */
        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("MiDoMappedCopy: Status %X\n", Status);
            goto Exit;
        }

        /* Unmap and unlock */
        MmUnmapLockedPages(MdlAddress, Mdl);
        MdlAddress = NULL;
        MmUnlockPages(Mdl);
        PagesLocked = FALSE;

        /* Update location and size */
        RemainingSize -= CurrentSize;
        CurrentAddress = (PVOID)((ULONG_PTR)CurrentAddress + CurrentSize);
        CurrentTargetAddress = (PVOID)((ULONG_PTR)CurrentTargetAddress + CurrentSize);
    }

Exit:
    if (MdlAddress)
        MmUnmapLockedPages(MdlAddress, Mdl);

    if (PagesLocked)
        MmUnlockPages(Mdl);

    /* All bytes read */
    if (Status == STATUS_SUCCESS)
        *ReturnSize = BufferSize;

    return Status;
}

NTSTATUS
NTAPI
MiDoPoolCopy(
    _In_ PEPROCESS SourceProcess,
    _In_ PVOID SourceAddress,
    _In_ PEPROCESS TargetProcess,
    _Out_ PVOID TargetAddress,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PSIZE_T ReturnSize)
{
    UCHAR StackBuffer[MI_POOL_COPY_BYTES];
    PVOID CurrentAddress = SourceAddress;
    PVOID CurrentTargetAddress = TargetAddress;
    PVOID PoolAddress;
    ULONG_PTR BadAddress;
    SIZE_T TotalSize;
    SIZE_T CurrentSize;
    SIZE_T RemainingSize;
    volatile BOOLEAN FailedInProbe = FALSE;
    volatile BOOLEAN HavePoolAddress = FALSE;
    BOOLEAN HaveBadAddress;
    KAPC_STATE ApcState;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    DPRINT("MiDoPoolCopy: %X, %p, %p, %p, %p\n",
           BufferSize, SourceProcess, SourceAddress, TargetProcess, TargetAddress);

    /* Calculate the maximum amount of data to move */
    TotalSize = MI_MAX_TRANSFER_SIZE;

    if (BufferSize <= MI_MAX_TRANSFER_SIZE)
        TotalSize = BufferSize;

    CurrentSize = TotalSize;
    RemainingSize = BufferSize;

    /* Check if we can use the stack */
    if (BufferSize <= MI_POOL_COPY_BYTES)
    {
        /* Use it */
        PoolAddress = (PVOID)StackBuffer;
    }
    else
    {
        /* Allocate pool */
        PoolAddress = ExAllocatePoolWithTag(NonPagedPool, TotalSize, 'VmRw');
        if (!PoolAddress)
        {
            DPRINT1("MiDoPoolCopy: FIXME while (TotalSize / 2)\n");
            ASSERT(FALSE);
        }

        HavePoolAddress = TRUE;
    }

    /* Loop as long as there is still data */
    while (RemainingSize > 0)
    {
        /* Check if this transfer will finish everything off */
        if (RemainingSize < CurrentSize)
            CurrentSize = RemainingSize;

        /* Attach to the source address space */
        KeStackAttachProcess(&SourceProcess->Pcb, &ApcState);

        /* Check that state is sane */
        ASSERT(FailedInProbe == FALSE);
        ASSERT(Status == STATUS_SUCCESS);

        /* Protect user-mode copy */
        _SEH2_TRY
        {
            /* If this is our first time, probe the buffer */
            if (CurrentAddress == SourceAddress && PreviousMode != KernelMode)
            {
                /* Catch a failure here */
                FailedInProbe = TRUE;

                /* Do the probe */
                ProbeForRead(SourceAddress, BufferSize, sizeof(CHAR));

                /* Passed */
                FailedInProbe = FALSE;
            }

            /* Do the copy */
            RtlCopyMemory(PoolAddress, CurrentAddress, CurrentSize);
        }
        _SEH2_EXCEPT(MiGetExceptionInfo(_SEH2_GetExceptionInformation(),
                                        &HaveBadAddress,
                                        &BadAddress))
        {
            *ReturnSize = BufferSize - RemainingSize;

            /* Check if we failed during the probe */
            if (FailedInProbe)
            {
                /* Exit */
                Status = _SEH2_GetExceptionCode();
            }
            else
            {
                /* We failed during the move.
                   Check if we know exactly where we stopped copying
                */
                if (HaveBadAddress)
                    /* Return the exact number of bytes copied */
                    *ReturnSize = BadAddress - (ULONG_PTR)SourceAddress;

                /* Return partial copy */
                Status = STATUS_PARTIAL_COPY;
            }
        }
        _SEH2_END

        /* Let go of the source */
        KeUnstackDetachProcess(&ApcState);

        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("MiDoPoolCopy: Status %X\n", Status);
            goto Exit;
        }

        /* Grab the target process */
        KeStackAttachProcess(&TargetProcess->Pcb, &ApcState);

        _SEH2_TRY
        {
            /* Check if this is our first time through */
            if ((CurrentTargetAddress == TargetAddress) && (PreviousMode != KernelMode))
            {
                /* Catch a failure here */
                FailedInProbe = TRUE;

                /* Do the probe */
                ProbeForWrite(TargetAddress, BufferSize, sizeof(CHAR));

                /* Passed */
                FailedInProbe = FALSE;
            }

            /* Now do the actual move */
            RtlCopyMemory(CurrentTargetAddress, PoolAddress, CurrentSize);
        }
        _SEH2_EXCEPT(MiGetExceptionInfo(_SEH2_GetExceptionInformation(),
                                        &HaveBadAddress,
                                        &BadAddress))
        {
            *ReturnSize = BufferSize - RemainingSize;

            /* Check if we failed during the probe */
            if (FailedInProbe)
            {
                /* Exit */
                Status = _SEH2_GetExceptionCode();
            }
            else
            {
                /* Otherwise we failed during the move.
                   Check if we know exactly where we stopped copying
                */
                if (HaveBadAddress)
                    /* Return the exact number of bytes copied */
                    *ReturnSize = BadAddress - (ULONG_PTR)SourceAddress;

                /* Return partial copy */
                Status = STATUS_PARTIAL_COPY;
            }
        }
        _SEH2_END;

        /* Detach from target */
        KeUnstackDetachProcess(&ApcState);

        /* Check for SEH status */
        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("MiDoPoolCopy: Status %X\n", Status);
            goto Exit;
        }

        /* Update location and size */
        RemainingSize -= CurrentSize;
        CurrentAddress = (PVOID)((ULONG_PTR)CurrentAddress + CurrentSize);
        CurrentTargetAddress = (PVOID)((ULONG_PTR)CurrentTargetAddress + CurrentSize);
    }

Exit:

    /* Check if we had allocated pool */
    if (HavePoolAddress)
        ExFreePoolWithTag(PoolAddress, 'VmRw');

    /* All bytes read */
    if (Status == STATUS_SUCCESS)
        *ReturnSize = BufferSize;

    return Status;
}

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
    NTSTATUS Status;
    PEPROCESS Process = SourceProcess;

    /* Don't accept zero-sized buffers */
    if (!BufferSize)
        return STATUS_SUCCESS;

    /* If we are copying from ourselves, lock the target instead */
    if (SourceProcess == PsGetCurrentProcess())
        Process = TargetProcess;

    /* Acquire rundown protection */
    if (!ExAcquireRundownProtection(&Process->RundownProtect))
    {
        /* Fail */
        DPRINT1("MmCopyVirtualMemory: STATUS_PROCESS_IS_TERMINATING\n");
        return STATUS_PROCESS_IS_TERMINATING;
    }

    /* See if we should use the pool copy */
    if (BufferSize > MI_POOL_COPY_BYTES)
    {
        /* Use MDL-copy */
        Status = MiDoMappedCopy(SourceProcess,
                                SourceAddress,
                                TargetProcess,
                                TargetAddress,
                                BufferSize,
                                PreviousMode,
                                ReturnSize);
    }
    else
    {
        *ReturnSize = 0;

        /* Do pool copy */
        Status = MiDoPoolCopy(SourceProcess,
                              SourceAddress,
                              TargetProcess,
                              TargetAddress,
                              BufferSize,
                              PreviousMode,
                              ReturnSize);
    }

    /* Release the lock */
    ExReleaseRundownProtection(&Process->RundownProtect);

    return Status;
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
MiDeletePte(
    _In_ PMMPTE Pte,
    _In_ PVOID Va,
    _In_ PEPROCESS CurrentProcess,
    _In_ PMMPTE Proto,
    _In_ PMMPTE_FLUSH_LIST FlushList,
    _In_ KIRQL OldIrql)
{
    PMMPFN Pfn;
    PMMPDE Pde = NULL;
    MMPTE TempPte;
    PFN_NUMBER PageFrameIndex;
    PFN_NUMBER PageNumber;

    DPRINT("MiDeletePte: Pte %p [%p], Va %p, Proto %p [%p], OldIrql %X\n",
           Pte, (Pte ? Pte->u.Long : 0), Va, Proto, (Proto ? Proto->u.Long : 0), OldIrql);

    /* PFN lock must be held */
    MI_ASSERT_PFN_LOCK_HELD();

    /* Capture the PTE */
    TempPte = *Pte;

    /* See if the PTE is valid */
    if (TempPte.u.Hard.Valid)
    {
        /* Get the PFN entry */
        PageFrameIndex = PFN_FROM_PTE(&TempPte);
        Pfn = MiGetPfnEntry(PageFrameIndex);

        /* Check if this is a valid, prototype PTE */
        if (Pfn->u3.e1.PrototypePte)
        {
            ASSERT(KeGetCurrentIrql() > APC_LEVEL);

            if (!Pfn->u3.e1.Modified && Pte->u.Hard.Dirty)
            {
                ASSERT(Pfn->u3.e1.Rom == 0);
                Pfn->u3.e1.Modified = 1;

                if (!Pfn->OriginalPte.u.Soft.Prototype && !Pfn->u3.e1.WriteInProgress)
                {
                    DPRINT1("MiDeletePte: FIXME\n");
                    ASSERT(FALSE);
                }
            }

            /* Get the PDE and make sure it's faulted in */
            Pde = MiPteToPde(Pte);

            /* Could be paged pool access from a new process -- synchronize the page directories */
            if (!Pde->u.Hard.Valid && !NT_SUCCESS(MiCheckPdeForPagedPool(Va)))
            {
                /* The PDE must be valid at this point */
                KeBugCheckEx(MEMORY_MANAGEMENT, 0x61940, (ULONG_PTR)Pte, Pte->u.Long, (ULONG_PTR)Va);
            }

            /* Drop the share count on the page table */
            PageNumber = Pde->u.Hard.PageFrameNumber;
            DPRINT("MiDeletePte: Pde %p, PageNumber %X\n", Pde, PageNumber);

            MiDecrementShareCount(MiGetPfnEntry(PageNumber), PageNumber);

            /* Drop the share count */
            MiDecrementShareCount(Pfn, PageFrameIndex);

            /* Either a fork, or this is the shared user data page */
            if (Pte <= MiHighestUserPte && Proto != Pfn->PteAddress)
            {
                /* If it's not the shared user page, then crash, since there's no fork() yet */
                if (PAGE_ALIGN(Va) != (PVOID)USER_SHARED_DATA ||
                    MmHighestUserAddress <= (PVOID)USER_SHARED_DATA)
                {
                    /* Must be some sort of memory corruption */
                    KeBugCheckEx(MEMORY_MANAGEMENT, 0x400, (ULONG_PTR)Pte, (ULONG_PTR)Proto, (ULONG_PTR)Pfn->PteAddress);
                }
            }
        }
        else
        {
            /* Make sure the saved PTE address is valid */
            if ((PMMPTE)((ULONG_PTR)Pfn->PteAddress & ~0x1) != Pte)
            {
                /* The PFN entry is illegal, or invalid */
                KeBugCheckEx(MEMORY_MANAGEMENT, 0x401, (ULONG_PTR)Pte, Pte->u.Long, (ULONG_PTR)Pfn->PteAddress);
            }

            /* There should only be 1 shared reference count */
            ASSERT(Pfn->u2.ShareCount == 1);
    
            /* Drop the reference on the page table. */
            DPRINT("MiDeletePte: Pfn->u4.PteFrame %X\n", Pfn->u4.PteFrame);
            MiDecrementShareCount(MiGetPfnEntry(Pfn->u4.PteFrame), Pfn->u4.PteFrame);
    
            /* Mark the PFN for deletion and dereference what should be the last ref */
            MI_SET_PFN_DELETED(Pfn);
            MiDecrementShareCount(Pfn, PageFrameIndex);

            /* We should eventually do this */
            //Process->NumberOfPrivatePages--;
        }

        /* Erase it */
        MI_ERASE_PTE(Pte);

        if (!FlushList)
        {
            /* Flush the TLB */
            //FIXME: Use KeFlushSingleTb(Va, 0) instead
            KeFlushCurrentTb();
        }
        else
        {
            DPRINT1("MiDeletePte: FIXME\n");
            ASSERT(FALSE);
        }
    }
    else if (TempPte.u.Soft.Prototype)
    {
        DPRINT1("MiDeletePte: FIXME\n");
        ASSERT(FALSE);
    }
    else if (TempPte.u.Soft.Transition)
    {
        DPRINT1("MiDeletePte: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        if (TempPte.u.Soft.PageFileHigh != 0xFFFFF)
        {
            DPRINT1("MiDeletePte: FIXME\n");
            ASSERT(FALSE);
        }

        MI_ERASE_PTE(Pte);
    }

    DPRINT("MiDeletePte: return 0\n");
    return 0; // FIXME
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

ULONG
NTAPI
MiMakeSystemAddressValidPfn(
    _In_ PVOID VirtualAddress,
    _In_ KIRQL OldIrql)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

ULONG
NTAPI
MiGetPageProtection(
    _In_ PMMPTE Pte)
{
    PEPROCESS CurrentProcess;
    PETHREAD CurrentThread;
    PMMPTE Pde;
    PMMPFN Pfn;
    PMMPTE Proto;
    MMPTE PteContents;
    ULONG Protect;
    BOOLEAN WsShared = 0;
    BOOLEAN WsSafe = 0;
    KIRQL OldIrql;

    ASSERT(Pte < (PMMPTE)PTE_TOP);

    PteContents.u.Long = Pte->u.Long;
    ASSERT(PteContents.u.Long != 0);

    DPRINT("MiGetPageProtection: Pte %p [%X]\n", Pte, PteContents.u.Long);

    if (PteContents.u.Soft.Valid || !PteContents.u.Soft.Prototype)
    {
        if (!PteContents.u.Hard.Valid)
            return MmProtectToValue[PteContents.u.Soft.Protection];

        Pfn = MI_PFN_ELEMENT(PteContents.u.Hard.PageFrameNumber);

        if (Pfn->u3.e1.PrototypePte)
        {
            DPRINT("MiGetPageProtection: FIXME MiLocateWsle()\n");
            return MmProtectToValue[Pfn->OriginalPte.u.Soft.Protection];
        }

        if (!Pfn->u4.AweAllocation)
            return MmProtectToValue[Pfn->OriginalPte.u.Soft.Protection];

        if (!PteContents.u.Hard.Owner)
            return PAGE_NOACCESS;

        if (PteContents.u.Hard.Write)
            return PAGE_READWRITE;

        return PAGE_READONLY;
    }

    if (PteContents.u.Soft.PageFileHigh == MI_PTE_LOOKUP_NEEDED)
        return MmProtectToValue[PteContents.u.Soft.Protection];

    CurrentThread = PsGetCurrentThread();
    CurrentProcess = PsGetCurrentProcess();

    MiUnlockProcessWorkingSetForFault(CurrentProcess, CurrentThread, &WsSafe, &WsShared);

    Proto = MiGetProtoPtr(&PteContents);
    PteContents.u.Long = Proto->u.Long;

    if (PteContents.u.Hard.Valid)
    {
        Pde = MiAddressToPde(Proto);

        OldIrql = MiLockPfnDb(APC_LEVEL);

        if (!Pde->u.Hard.Valid)
            MiMakeSystemAddressValidPfn(Proto, OldIrql);

        PteContents.u.Long = Proto->u.Long;
        ASSERT(PteContents.u.Long != 0);

        if (PteContents.u.Hard.Valid)
        {
            Pfn = MI_PFN_ELEMENT(PteContents.u.Hard.PageFrameNumber);
            Protect = MmProtectToValue[Pfn->OriginalPte.u.Soft.Protection];
        }
        else
        {
            Protect = MmProtectToValue[PteContents.u.Soft.Protection];
        }

        MiUnlockPfnDb(OldIrql, APC_LEVEL);
    }
    else
    {
        Protect = MmProtectToValue[PteContents.u.Soft.Protection];
    }

    MiLockProcessWorkingSetForFault(CurrentProcess, CurrentThread, WsSafe, WsShared);

    return Protect;
}

NTSTATUS
NTAPI
MiSetProtectionOnSection(
    _In_ PEPROCESS Process,
    _In_ PMMVAD FoundVad,
    _In_ ULONG_PTR StartingAddress,
    _In_ ULONG_PTR EndingAddress,
    _In_ ULONG NewProtect,
    _Out_ ULONG* OutProtection,
    _In_ ULONG DontCharge,
    _Out_ BOOLEAN* OutLocked)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    SIZE_T BytesWritten = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    /* Check if we came from user mode */
    if (PreviousMode != KernelMode)
    {
        /* Validate the read addresses */
        if ((((ULONG_PTR)BaseAddress + NumberOfBytesToWrite) < (ULONG_PTR)BaseAddress) ||
            (((ULONG_PTR)Buffer + NumberOfBytesToWrite) < (ULONG_PTR)Buffer) ||
            (((ULONG_PTR)BaseAddress + NumberOfBytesToWrite) > MmUserProbeAddress) ||
            (((ULONG_PTR)Buffer + NumberOfBytesToWrite) > MmUserProbeAddress))
        {
            /* Don't allow to write into kernel space */
            return STATUS_ACCESS_VIOLATION;
        }

        /* Enter SEH for probe */
        _SEH2_TRY
        {
            /* Probe the output value */
            if (NumberOfBytesWritten)
                ProbeForWriteSize_t(NumberOfBytesWritten);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Get exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* Don't do zero-byte transfers */
    if (NumberOfBytesToWrite)
    {
        /* Reference the process */
        Status = ObReferenceObjectByHandle(ProcessHandle,
                                           PROCESS_VM_WRITE,
                                           PsProcessType,
                                           PreviousMode,
                                           (PVOID *)&Process,
                                           NULL);
        if (NT_SUCCESS(Status))
        {
            /* Do the copy */
            Status = MmCopyVirtualMemory(PsGetCurrentProcess(),
                                         Buffer,
                                         Process,
                                         BaseAddress,
                                         NumberOfBytesToWrite,
                                         PreviousMode,
                                         &BytesWritten);

            /* Dereference the process */
            ObDereferenceObject(Process);
        }
    }

    /* Check if the caller sent this parameter */
    if (NumberOfBytesWritten)
    {
        /* Enter SEH to guard write */
        _SEH2_TRY
        {
            /* Return the number of bytes written */
            *NumberOfBytesWritten = BytesWritten;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    return Status;
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
    PETHREAD CurrentThread = PsGetCurrentThread();
    PEPROCESS CurrentProcess = PsGetCurrentProcess();
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PMMSUPPORT AddressSpace;
    PEPROCESS Process;
    ULONG ProtectionMask;
    PVOID PBaseAddress;
    PMMVAD Vad;
    PMMVAD FoundVad;
    PMMPDE Pde;
    PMMPTE Pte;
    PMMPTE StartPte;
    PMMPTE LastPte;
    PMMPTE EndVpnPte;
    MMPTE TempPte;
    ULONG_PTR PRegionSize;
    ULONG_PTR StartingAddress;
    ULONG_PTR EndingAddress;
    ULONG_PTR HighestAddress = (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;
    ULONG_PTR Alignment;
    SIZE_T CommitCharge = 0;
    SIZE_T QuotaCharge = 0;
    SIZE_T QuotaFree;
    SIZE_T QuotaCopyOnWrite;
    SIZE_T ExcessCharge = 0;
    ULONG OldProtect;
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;
    BOOLEAN IsReturnQuota = FALSE;
    BOOLEAN IsChangeProtection = FALSE;
    BOOLEAN Locked;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("NtAllocateVirtualMemory: Handle %p, Base %X, ZeroBits %X, Size %X, AllocType %X, Protect %X\n",
           ProcessHandle, (UBaseAddress ? *UBaseAddress : 0), ZeroBits, (URegionSize ? *URegionSize : 0), AllocationType, Protect);

    /* Check for valid Zero bits */
    if (ZeroBits > MI_MAX_ZERO_BITS)
    {
        DPRINT1("NtAllocateVirtualMemory: Too many zero bits\n");
        return STATUS_INVALID_PARAMETER_3;
    }

    /* Check for valid Allocation Types */
    if ((AllocationType & !(MEM_LARGE_PAGES | MEM_PHYSICAL | MEM_WRITE_WATCH | MEM_TOP_DOWN | MEM_RESET | MEM_RESERVE | MEM_COMMIT)))
    {
        DPRINT1("NtAllocateVirtualMemory: Invalid Allocation Type\n");
        return STATUS_INVALID_PARAMETER_5;
    }

    /* Check for at least one of these Allocation Types to be set */
    if (!(AllocationType & (MEM_COMMIT | MEM_RESERVE | MEM_RESET)))
    {
        DPRINT1("NtAllocateVirtualMemory: No memory allocation base type\n");
        return STATUS_INVALID_PARAMETER_5;
    }

    /* MEM_RESET is an exclusive flag, make sure that is valid too */
    if ((AllocationType & MEM_RESET) && (AllocationType != MEM_RESET))
    {
        DPRINT1("NtAllocateVirtualMemory: Invalid use of MEM_RESET\n");
        return STATUS_INVALID_PARAMETER_5;
    }

    if (AllocationType & (MEM_LARGE_PAGES | MEM_PHYSICAL | MEM_WRITE_WATCH))
    {
        /* Check if large pages are being used */
        if (AllocationType & MEM_LARGE_PAGES)
        {
            /* Large page allocations MUST be committed */
            if (!(AllocationType & MEM_COMMIT))
            {
                DPRINT1("NtAllocateVirtualMemory: Must supply MEM_COMMIT with MEM_LARGE_PAGES\n");
                return STATUS_INVALID_PARAMETER_5;
            }

            /* These flags are not allowed with large page allocations */
            if (AllocationType & (MEM_PHYSICAL | MEM_RESET | MEM_WRITE_WATCH))
            {
                DPRINT1("NtAllocateVirtualMemory: Using illegal flags with MEM_LARGE_PAGES\n");
                return STATUS_INVALID_PARAMETER_5;
            }
        }
        /* Check for valid MEM_WRITE_WATCH usage */
        else if (AllocationType & MEM_WRITE_WATCH)
        {
            if (AllocationType & MEM_PHYSICAL)
            {
                return STATUS_INVALID_PARAMETER_5;
            }

            if (!(AllocationType & MEM_RESERVE))
            {
                DPRINT1("NtAllocateVirtualMemory: MEM_WRITE_WATCH used without MEM_RESERVE\n");
                return STATUS_INVALID_PARAMETER_5;
            }
        }
        /* Check for valid MEM_PHYSICAL usage */
        else if (AllocationType & MEM_PHYSICAL)
        {
            ASSERT((AllocationType & (MEM_LARGE_PAGES | MEM_WRITE_WATCH)) == 0);

            /* MEM_PHYSICAL can only be used if MEM_RESERVE is also used */
            if (!(AllocationType & MEM_RESERVE))
            {
                DPRINT1("NtAllocateVirtualMemory: MEM_PHYSICAL used without MEM_RESERVE\n");
                return STATUS_INVALID_PARAMETER_5;
            }

            /* Only these flags are allowed with MEM_PHYSIAL */
            if (AllocationType & ~(MEM_RESERVE | MEM_TOP_DOWN | MEM_PHYSICAL))
            {
                DPRINT1("NtAllocateVirtualMemory: Using illegal flags with MEM_PHYSICAL\n");
                return STATUS_INVALID_PARAMETER_5;
            }

            /* Then make sure PAGE_READWRITE is used */
            if (Protect != PAGE_READWRITE)
            {
                DPRINT1("NtAllocateVirtualMemory: MEM_PHYSICAL used without PAGE_READWRITE\n");
                return STATUS_INVALID_PARAMETER_6;
            }
        }
    }

    /* Calculate the protection mask and make sure it's valid */
    ProtectionMask = MiMakeProtectionMask(Protect);
    if (ProtectionMask == MM_INVALID_PROTECTION)
    {
        DPRINT1("NtAllocateVirtualMemory: Invalid protection mask\n");
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    /* Enter SEH */
    _SEH2_TRY
    {
        /* Check for user-mode parameters */
        if (PreviousMode != KernelMode)
        {
            /* Make sure they are writable */
            ProbeForWritePointer(UBaseAddress);
            ProbeForWriteSize_t(URegionSize);
        }

        /* Capture their values */
        PBaseAddress = *UBaseAddress;
        PRegionSize = *URegionSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Return the exception code */
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* Make sure the allocation isn't past the VAD area */
    if (PBaseAddress > MM_HIGHEST_VAD_ADDRESS)
    {
        DPRINT1("NtAllocateVirtualMemory: Virtual allocation base above User Space\n");
        return STATUS_INVALID_PARAMETER_2;
    }

    /* Make sure the allocation wouldn't overflow past the VAD area */
    if ((((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1) - (ULONG_PTR)PBaseAddress) < PRegionSize)
    {
        DPRINT1("NtAllocateVirtualMemory: Region size would overflow into kernel-memory\n");
        return STATUS_INVALID_PARAMETER_4;
    }

    /* Make sure there's a size specified */
    if (!PRegionSize)
    {
        DPRINT1("NtAllocateVirtualMemory: Region size is invalid (zero)\n");
        return STATUS_INVALID_PARAMETER_4;
    }

    /* If this is for the current process, just use PsGetCurrentProcess */
    if (ProcessHandle == NtCurrentProcess())
    {
        Process = CurrentProcess;
    }
    else
    {
        /* Otherwise, reference the process with VM rights and attach to it if this isn't the current process.
           We must attach because we'll be touching PTEs and PDEs that belong to user-mode memory,
           and also touching the Working Set which is stored in Hyperspace.
        */
        Status = ObReferenceObjectByHandle(ProcessHandle,
                                           PROCESS_VM_OPERATION,
                                           PsProcessType,
                                           PreviousMode,
                                           (PVOID *)&Process,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NtAllocateVirtualMemory: Status %X\n", Status);
            return Status;
        }

    }

    DPRINT("NtAllocateVirtualMemory: %p, %p, %X, %X, %X, %X\n",
           Process, PBaseAddress, ZeroBits, PRegionSize, AllocationType, Protect);

    /* Check for large page allocations and make sure that the required privilege is being held,
       before attempting to handle them.
    */
    if ((AllocationType & MEM_LARGE_PAGES) && !SeSinglePrivilegeCheck(SeLockMemoryPrivilege, PreviousMode))
    {
        /* Fail without it */
        DPRINT1("NtAllocateVirtualMemory: Privilege not held for MEM_LARGE_PAGES\n");
        Status = STATUS_PRIVILEGE_NOT_HELD;
        goto ErrorExit;
    }

    if (CurrentProcess != Process)
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    /* Check if the caller is reserving memory, or committing memory and letting us pick the base address */
    if (!PBaseAddress || (AllocationType & MEM_RESERVE))
    {
        /*  Do not allow COPY_ON_WRITE through this API */
        if (Protect & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
        {
            DPRINT1("NtAllocateVirtualMemory: Copy on write not allowed through this path\n");
            Status = STATUS_INVALID_PAGE_PROTECTION;
            goto ErrorExit;
        }

        Alignment = 0x10000;

        /* Does the caller have an address in mind, or is this a blind commit? */
        if (!PBaseAddress)
        {
            /* This is a blind commit, all we need is the region size */
            PRegionSize = ROUND_TO_PAGES(PRegionSize);

            /* Check if ZeroBits were specified */
            if (ZeroBits)
            {
                /* Calculate the highest address and check if it's valid */
                HighestAddress = (MAXULONG_PTR >> ZeroBits);

                if (HighestAddress > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
                {
                    Status = STATUS_INVALID_PARAMETER_3;
                    goto ErrorExit;
                }
            }

            if (Process->VmTopDown)
                AllocationType |= MEM_TOP_DOWN;

            StartingAddress = 0;
            EndingAddress = 0;

            CommitCharge = BYTES_TO_PAGES(PRegionSize);

            if (AllocationType & MEM_LARGE_PAGES)
            {
                DPRINT1("NtAllocateVirtualMemory: FIXME\n");
                ASSERT(FALSE);
                Alignment = 0x400000;
            }
        }
        else
        {
            /* This is a reservation, so compute the starting address on the expected 64KB granularity,
               and see where the ending address will fall based on the aligned address and the passed in region size
            */
            EndingAddress = (((ULONG_PTR)PBaseAddress + PRegionSize - 1) | (PAGE_SIZE - 1));

            if (AllocationType & MEM_LARGE_PAGES)
            {
                DPRINT1("NtAllocateVirtualMemory: FIXME\n");
                ASSERT(FALSE);
            }
            else
            {
                StartingAddress = ((ULONG_PTR)PBaseAddress & ~(0x10000 - 1));
            }

            CommitCharge = BYTES_TO_PAGES(EndingAddress - StartingAddress);
        }

        /* Allocate and initialize the VAD */
        Vad = ExAllocatePoolWithTag (NonPagedPool, sizeof(MMVAD_SHORT), 'SdaV');
        if (!Vad)
        {
            DPRINT1("NtAllocateVirtualMemory: Failed to allocate a VAD!\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto ErrorExit;
        }

        Vad->u.LongFlags = 0;

        if (AllocationType & MEM_COMMIT)
            Vad->u.VadFlags.MemCommit = 1;

        Vad->u.VadFlags.CommitCharge = CommitCharge;
        Vad->u.VadFlags.Protection = ProtectionMask;
        Vad->u.VadFlags.PrivateMemory = 1;

        if (AllocationType & (MEM_PHYSICAL | MEM_LARGE_PAGES))
        {
            ASSERT((AllocationType & MEM_WRITE_WATCH) == 0);

            DPRINT1("NtAllocateVirtualMemory: FIXME\n");
            ASSERT(FALSE);
        }
        else if (AllocationType & MEM_WRITE_WATCH)
        {
            ASSERT(AllocationType & MEM_RESERVE);

            DPRINT1("NtAllocateVirtualMemory: FIXME\n");
            ASSERT(FALSE);
        }

        /* Lock the address space and make sure the process isn't already dead */
        AddressSpace = MmGetCurrentAddressSpace();
        MmLockAddressSpace(AddressSpace);

        if (Process->VmDeleted)
        {
            DPRINT1("NtAllocateVirtualMemory: STATUS_PROCESS_IS_TERMINATING. FIXME MiFreeAweInfo()\n");
            Status = STATUS_PROCESS_IS_TERMINATING;
            goto ErrorVadExit;
        }

        // FIXME ? MiInsertAweInfo()

        if (!PBaseAddress)
        {
            if (AllocationType & 0x00100000)
                Status = MiFindEmptyAddressRangeDownTree(PRegionSize, HighestAddress, Alignment, &Process->VadRoot, &StartingAddress);
            else
                Status = MiFindEmptyAddressRange(PRegionSize, Alignment, ZeroBits, &StartingAddress);

            if (!NT_SUCCESS(Status))
            {
                DPRINT1("NtAllocateVirtualMemory: Status %X\n", Status);
                goto ErrorVadExit;
            }

            EndingAddress = (StartingAddress + PRegionSize - 1) | (PAGE_SIZE - 1);

            if (EndingAddress > HighestAddress)
            {
                DPRINT1("NtAllocateVirtualMemory: STATUS_NO_MEMORY. EndingAddress > HighestAddress!\n");
                Status = STATUS_NO_MEMORY;
                goto ErrorVadExit;
            }
        }
        else
        {
            if (MiCheckForConflictingVadExistence(Process, StartingAddress, EndingAddress)) 
            {
                DPRINT1("NtAllocateVirtualMemory: STATUS_CONFLICTING_ADDRESSES\n");
                Status = STATUS_CONFLICTING_ADDRESSES;
                goto ErrorVadExit;
            }
        }

        Vad->StartingVpn = (StartingAddress / PAGE_SIZE);
        Vad->EndingVpn = (EndingAddress / PAGE_SIZE);

        Status = MiInsertVadCharges(Vad, Process);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NtAllocateVirtualMemory: Status %X\n", Status);
            goto ErrorVadExit;
        }

        MiLockProcessWorkingSetUnsafe(Process, CurrentThread);
        MiInsertVad(Vad, &Process->VadRoot);

        if (AllocationType & (MEM_PHYSICAL | MEM_LARGE_PAGES))
        {
            DPRINT1("NtAllocateVirtualMemory: FIXME\n");
            ASSERT(FALSE);
        }

        MiUnlockProcessWorkingSetUnsafe(Process, CurrentThread);

        PRegionSize = (EndingAddress - StartingAddress + 1);
        Process->VirtualSize += PRegionSize;

        if (Process->VirtualSize > Process->PeakVirtualSize)
            Process->PeakVirtualSize = Process->VirtualSize;

        /* Unlock the address space */
        MmUnlockAddressSpace(AddressSpace);

        /* Detach and dereference the target process if it was different from the current process */
        if (Attached)
            KeUnstackDetachProcess(&ApcState);

        if (ProcessHandle != NtCurrentProcess())
            ObDereferenceObject(Process);

        /* Use SEH to write back the base address and the region size.
           In the case of an exception, we do not return back the exception code, as the memory *has* been allocated.
           The caller would now have to call VirtualQuery or
           do some other similar trick to actually find out where its memory allocation ended up
        */
        _SEH2_TRY
        {
            *URegionSize = PRegionSize;
            *UBaseAddress = (PVOID)StartingAddress;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Ignore exception! */
            DPRINT1("NtAllocateVirtualMemory: Ignore exception!\n");
            //ASSERT(FALSE);
        }
        _SEH2_END;

        DPRINT("NtAllocateVirtualMemory: Reserved %X bytes at %p\n", PRegionSize, StartingAddress);
        return STATUS_SUCCESS;

ErrorVadExit:

        DPRINT1("NtAllocateVirtualMemory: ErrorVadExit\n");

        MmUnlockAddressSpace(AddressSpace);
        ExFreePool(Vad);

        if (AllocationType & (MEM_PHYSICAL | MEM_LARGE_PAGES))
        {
            DPRINT1("NtAllocateVirtualMemory: FIXME\n");
            ASSERT(FALSE);
        }
        else if (AllocationType & MEM_WRITE_WATCH)
        {
            DPRINT1("NtAllocateVirtualMemory: FIXME\n");
            ASSERT(FALSE);
        }

        goto ErrorExit;
    }

    /* This is a MEM_COMMIT on top of an existing address which must have been MEM_RESERVED already.
       Compute the start and ending base addresses based on the user input,
       and then compute the actual region size once all the alignments have been done.
    */
    if (AllocationType == MEM_RESET)
    {
        DPRINT1("NtAllocateVirtualMemory: FIXME\n");
        ASSERT(FALSE);
    }
    else
    {
        StartingAddress = (ULONG_PTR)PAGE_ALIGN(PBaseAddress);
        EndingAddress = (((ULONG_PTR)PBaseAddress + PRegionSize - 1) | (PAGE_SIZE - 1));
    }

    PRegionSize = (EndingAddress - StartingAddress + 1);

    /* Lock the address space and make sure the process isn't already dead */
    AddressSpace = MmGetCurrentAddressSpace();
    MmLockAddressSpace(AddressSpace);

    if (Process->VmDeleted)
    {
        DPRINT1("NtAllocateVirtualMemory: STATUS_PROCESS_IS_TERMINATING\n");
        Status = STATUS_PROCESS_IS_TERMINATING;
        goto ErrorExit1;
    }

    /* Get the VAD for this address range, and make sure it exists */
    FoundVad = (PMMVAD)MiCheckForConflictingNode((StartingAddress / PAGE_SIZE),
                                                 (EndingAddress / PAGE_SIZE),
                                                 &Process->VadRoot);
    if (!FoundVad)
    {
        DPRINT1("NtAllocateVirtualMemory: STATUS_CONFLICTING_ADDRESSES\n");
        Status = STATUS_CONFLICTING_ADDRESSES;
        goto ErrorExit1;
    }

    DPRINT("NtAllocateVirtualMemory: FoundVad %p, Start %X, End %X\n", FoundVad, StartingAddress, EndingAddress);

    /* These kinds of VADs are illegal for this Windows function when trying to commit an existing range */
    if (FoundVad->u.VadFlags.VadType == VadAwe ||
        FoundVad->u.VadFlags.VadType == VadDevicePhysicalMemory ||
        FoundVad->u.VadFlags.VadType == VadLargePages)
    {
        DPRINT1("NtAllocateVirtualMemory: STATUS_CONFLICTING_ADDRESSES\n");
        Status = STATUS_CONFLICTING_ADDRESSES;
        goto ErrorExit1;
    }

    /* Make sure that this address range actually fits within the VAD for it */
    if ((StartingAddress / PAGE_SIZE) < FoundVad->StartingVpn ||
        (EndingAddress / PAGE_SIZE) > FoundVad->EndingVpn)
    {
        DPRINT1("NtAllocateVirtualMemory: STATUS_CONFLICTING_ADDRESSES\n");
        Status = STATUS_CONFLICTING_ADDRESSES;
        goto ErrorExit1;
    }

    if (FoundVad->u.VadFlags.CommitCharge == 0x7FFFF)
    {
        DPRINT("NtAllocateVirtualMemory: STATUS_CONFLICTING_ADDRESSES\n");
        Status = STATUS_CONFLICTING_ADDRESSES;
        goto ErrorExit1;
    }

    if (AllocationType == MEM_RESET)
    {
        DPRINT1("NtAllocateVirtualMemory: FIXME\n");
        ASSERT(FALSE);
        goto Exit;
    }

    /* Is this a previously reserved section being committed? If so, enter the special section path */
    if (!FoundVad->u.VadFlags.PrivateMemory)
    {
        /* You cannot commit large page sections through this API */
        if (FoundVad->u.VadFlags.VadType == VadLargePageSection)
        {
            DPRINT1("NtAllocateVirtualMemory: Large page sections cannot be VirtualAlloc'd\n");
            Status = STATUS_INVALID_PAGE_PROTECTION;
            goto ErrorExit1;
        }

        /* You can only use caching flags on a rotate VAD */
        if ((Protect & (PAGE_NOCACHE | PAGE_WRITECOMBINE)) &&
            FoundVad->u.VadFlags.VadType != VadRotatePhysical)
        {
            DPRINT1("NtAllocateVirtualMemory: Cannot use caching flags with anything but rotate VADs\n");
            Status = STATUS_INVALID_PAGE_PROTECTION;
            goto ErrorExit1;
        }

        /* We should make sure that the section's permissions aren't being messed with */
        if (FoundVad->u.VadFlags.NoChange)
        {
            /* Make sure it's okay to touch it
               Note: The Windows 2003 kernel has a bug here,
               passing the unaligned base address together with the aligned size,
               potentially covering a region larger than the actual allocation.
               Might be exposed through NtGdiCreateDIBSection w/ section handle.
               For now we keep this behavior.
               TODO: analyze possible implications, create test case
            */
            Status = MiCheckSecuredVad(FoundVad, PBaseAddress, PRegionSize, ProtectionMask);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("NtAllocateVirtualMemory: Secured VAD being messed around with. Status %X\n", Status);
                goto ErrorExit1;
            }
        }

        if (FoundVad->ControlArea->FilePointer)
        {
            /* ARM3 does not support file-backed sections, only shared memory */
            ASSERT(FoundVad->ControlArea->FilePointer == NULL);

            DPRINT1("NtAllocateVirtualMemory: FIXME\n");
            ASSERT(FALSE);
        }

        /* Compute PTE addresses and the quota charge, then grab the commit lock */
        StartPte = MI_GET_PROTOTYPE_PTE_FOR_VPN(FoundVad, (StartingAddress / PAGE_SIZE));
        LastPte = MI_GET_PROTOTYPE_PTE_FOR_VPN(FoundVad, (EndingAddress / PAGE_SIZE));

        QuotaCharge = (ULONG)(LastPte - StartPte + 1);
        Pte = StartPte;

        DPRINT("NtAllocateVirtualMemory: StartingAddress %p, EndingAddress %p, StartPte %p, LastPte %p\n", StartingAddress, EndingAddress, StartPte, LastPte);

        QuotaCopyOnWrite = 0;

        /* Rotate VADs cannot be guard pages or inaccessible, nor copy on write */
        if (FoundVad->u.VadFlags.VadType == VadRotatePhysical)
        {
            if ((Protect & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY | PAGE_NOACCESS | PAGE_GUARD)))
            {
                DPRINT1("NtAllocateVirtualMemory: Invalid page protection for rotate VAD\n");
                Status = STATUS_INVALID_PAGE_PROTECTION;
                goto ErrorExit1;
            }
        }
        else if ((ProtectionMask & 5) == 5)
        {
            QuotaCopyOnWrite = QuotaCharge;
        }

        if (QuotaCopyOnWrite);
        {
            Status = PsChargeProcessPageFileQuota(Process, QuotaCopyOnWrite);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("NtAllocateVirtualMemory: FIXME\n");
                ASSERT(FALSE);
                goto ErrorExit;
            }

            if (Process->CommitChargeLimit)
            {
                if ((Process->CommitCharge + QuotaCopyOnWrite) > Process->CommitChargeLimit)
                {
                    if (Process->Job)
                    {
                        DPRINT1("NtAllocateVirtualMemory: FIXME\n");
                        ASSERT(FALSE);
                    }

                    DPRINT1("NtAllocateVirtualMemory: FIXME\n");
                    ASSERT(FALSE);

                    Status = STATUS_COMMITMENT_LIMIT;
                    goto ErrorExit;
                }
            }

            if (Process->JobStatus & 0x10)
            {
                DPRINT1("NtAllocateVirtualMemory: FIXME\n");
                ASSERT(FALSE);
            }
        }

        KeAcquireGuardedMutexUnsafe(&MmSectionCommitMutex);

        DPRINT("NtAllocateVirtualMemory: FIXME MiChargeCommitment \n");

        /* Get the segment template PTE and start looping each page */
        TempPte = FoundVad->ControlArea->Segment->SegmentPteTemplate;
        ASSERT(TempPte.u.Long != 0);

        QuotaFree = 0;

        while (Pte <= LastPte)
        {
            /* For each non-already-committed page, write the invalid template PTE */
            if (!Pte->u.Long)
                MI_WRITE_INVALID_PTE(Pte, TempPte);
            else
                QuotaFree++;

            Pte++;
        }

        /* Now do the commit accounting and release the lock */
        if (!IsReturnQuota)
        {
            ASSERT(QuotaCharge >= QuotaFree);
            QuotaCharge -= QuotaFree;
        }
        else
        {
            QuotaFree = 0;
        }

        if (QuotaCharge)
        {
            FoundVad->ControlArea->Segment->NumberOfCommittedPages += QuotaCharge;
            InterlockedExchangeAdd((PLONG)&MmSharedCommit, QuotaCharge);
        }

        KeReleaseGuardedMutexUnsafe(&MmSectionCommitMutex);

        if (QuotaCopyOnWrite)
        {
            FoundVad->u.VadFlags.CommitCharge += QuotaCopyOnWrite;
            Process->CommitCharge += QuotaCopyOnWrite;

            if (Process->CommitCharge > Process->CommitChargePeak)
                Process->CommitChargePeak = Process->CommitCharge;
        }

        MiSetProtectionOnSection(Process,
                                 FoundVad,
                                 StartingAddress,
                                 EndingAddress,
                                 Protect,
                                 &OldProtect,
                                 TRUE,
                                 &Locked);
    
        /* Unlock the address space */
        MmUnlockAddressSpace(AddressSpace);

        if (QuotaFree)
        {
            DPRINT1("NtAllocateVirtualMemory: FIXME\n");
            ASSERT(FALSE);
        }

        if (Attached)
            KeUnstackDetachProcess(&ApcState);

        if (ProcessHandle != NtCurrentProcess())
            ObDereferenceObject(Process);

        /* Use SEH to write back the base address and the region size.
           In the case of an exception, we strangely do return back the exception code,
           even though the memory *has* been allocated.
           This mimics Windows behavior and there is not much we can do about it.
        */
        _SEH2_TRY
        {
            *URegionSize = PRegionSize;
            *UBaseAddress = (PVOID)StartingAddress;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return STATUS_SUCCESS;
    }

    // FoundVad->u.VadFlags.PrivateMemory

    /* While this is an actual Windows check */
    ASSERT(FoundVad->u.VadFlags.VadType != VadRotatePhysical);

    /* Throw out attempts to use copy-on-write through this API path */
    if ((Protect & PAGE_WRITECOPY) || (Protect & PAGE_EXECUTE_WRITECOPY))
    {
        DPRINT1("NtAllocateVirtualMemory: STATUS_INVALID_PAGE_PROTECTION\n");
        Status = STATUS_INVALID_PAGE_PROTECTION;
        goto ErrorExit1;
    }

    /* Initialize a demand-zero PTE */
    TempPte.u.Long = 0;
    TempPte.u.Soft.Protection = ProtectionMask;
    ASSERT(TempPte.u.Long != 0);

    /* Get the PTE, PDE and the last PTE for this address range */
    Pde = MiAddressToPde(StartingAddress);
    Pte = MiAddressToPte(StartingAddress);
    LastPte = MiAddressToPte(EndingAddress);

    QuotaCharge = (SIZE_T)(LastPte - Pte + 1);

    if (FoundVad->u.VadFlags.MemCommit)
        EndVpnPte = MiAddressToPte(FoundVad->EndingVpn * PAGE_SIZE);
    else
        EndVpnPte = NULL;

    if (Process->CommitChargeLimit &&
        Process->CommitChargeLimit < (Process->CommitCharge + QuotaCharge))
    {
        DPRINT1("NtAllocateVirtualMemory: FIXME\n");
        ASSERT(FALSE);
        goto ErrorExit1;
    }

    if (Process->JobStatus & 0x10)
    {
        DPRINT1("NtAllocateVirtualMemory: FIXME\n");
        ASSERT(FALSE);
    }

    Status = PsChargeProcessPageFileQuota(Process, QuotaCharge);
    if (!NT_SUCCESS (Status))
    {
        DPRINT1("NtAllocateVirtualMemory: FIXME\n");
        ASSERT(FALSE);
        goto ErrorExit1;
    }

    DPRINT("NtAllocateVirtualMemory: FIXME MiChargeCommitment()\n");

    /* Update the commit charge in the VAD as well as in the process,
       and check if this commit charge was now higher than the last recorded peak,
       in which case we also update the peak
    */
    FoundVad->u.VadFlags.CommitCharge += QuotaCharge;
    Process->CommitCharge += QuotaCharge;

    if (Process->CommitChargePeak < Process->CommitCharge)
        Process->CommitChargePeak = Process->CommitCharge;

    QuotaFree = 0;

    if (!IsReturnQuota)
        /* Lock the working set while we play with user pages and page tables */
        MiLockProcessWorkingSetUnsafe(Process, CurrentThread);

    /* Make the current page table valid, and then loop each page within it */
    MiMakePdeExistAndMakeValid(Pde, Process, MM_NOIRQL);

    while (Pte <= LastPte)
    {
        /* Have we crossed into a new page table? */
        if (MiIsPteOnPdeBoundary(Pte))
        {
            /* Get the PDE and now make it valid too */
            Pde = MiPteToPde(Pte);
            MiMakePdeExistAndMakeValid(Pde, Process, MM_NOIRQL);
        }

        /* Is this a zero PTE as expected? */
        if (!Pte->u.Long)
        {
            if (Pte <= EndVpnPte)
                QuotaFree++;

            /* First increment the count of pages in the page table for this process */
            MiIncrementPageTableReferences(MiPteToAddress(Pte));

            /* And now write the invalid demand-zero PTE as requested */
            MI_WRITE_INVALID_PTE(Pte, TempPte);
        }
        else if (Pte->u.Long == MmDecommittedPte.u.Long)
        {
            MI_WRITE_INVALID_PTE(Pte, TempPte);
        }
        else
        {
            /* We don't handle these scenarios yet */
            if (!IsChangeProtection)
            {
                if (!Pte->u.Soft.Valid &&
                    Pte->u.Soft.Prototype &&
                    Pte->u.Soft.PageFileHigh != MI_PTE_LOOKUP_NEEDED)
                {
                    if (Protect != MiGetPageProtection(Pte))
                        IsChangeProtection = TRUE;

                    MiMakePdeExistAndMakeValid (Pde, Process, MM_NOIRQL);
                }
                else
                {
                    if (Protect != MiGetPageProtection(Pte))
                        IsChangeProtection = TRUE;
                }
            }

            QuotaFree++;
        }

        /* Move to the next PTE */
        Pte++;
    }

    /* Release the working set lock */
    MiUnlockProcessWorkingSetUnsafe(Process, CurrentThread);

    if (!IsReturnQuota && QuotaFree)
        ExcessCharge = QuotaFree;

    if (ExcessCharge)
    {
        if (!IsReturnQuota)
        {
            FoundVad->u.VadFlags.CommitCharge -= ExcessCharge;
            ASSERT((LONG_PTR)FoundVad->u.VadFlags.CommitCharge >= 0);
            Process->CommitCharge -= ExcessCharge;
        }

        /* Unlock the address space */
        MmUnlockAddressSpace(AddressSpace);

        if (!IsReturnQuota)
        {
            DPRINT1("NtAllocateVirtualMemory: FIXME\n");
            ASSERT(FALSE);
        }

        PsReturnProcessPageFileQuota(Process, ExcessCharge);

        if (Process->JobStatus & 0x10)
        {
            DPRINT1("NtAllocateVirtualMemory: FIXME\n");
            ASSERT(FALSE);
        }
    }
    else
    {
        /* Unlock the address space */
        MmUnlockAddressSpace(AddressSpace);
    }

Exit:

    /* Check if we need to update the protection */
    if (IsChangeProtection)
    {
        DPRINT1("NtAllocateVirtualMemory: FIXME\n");
        ASSERT(FALSE);
    }

    if (Attached)
        KeUnstackDetachProcess(&ApcState);

    if (ProcessHandle != NtCurrentProcess())
        ObDereferenceObject(Process);

    /* Use SEH to write back the base address and the region size.
       In the case of an exception, we strangely do return back the exception code,
       even though the memory *has* been allocated.
       This mimics Windows behavior and there is not much we can do about it.
    */
    _SEH2_TRY
    {
        *URegionSize = PRegionSize;
        *UBaseAddress = (PVOID)StartingAddress;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;

ErrorExit1:

    /* Unlock the address space */
    MmUnlockAddressSpace(AddressSpace);

ErrorExit:

    if (Attached)
        KeUnstackDetachProcess (&ApcState);

    if (ProcessHandle != NtCurrentProcess())
        ObDereferenceObject (Process);

    return Status;
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
