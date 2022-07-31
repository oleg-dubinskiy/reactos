
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

#define HYDRA_PROCESS (PEPROCESS)1

ULONG MmDataClusterSize;
ULONG MmCodeClusterSize;
ULONG MmInPageSupportMinimum = 4;

extern PMMPTE MiSessionLastPte;
extern PMM_SESSION_SPACE MmSessionSpace;
extern PVOID MmHyperSpaceEnd;
extern PFN_NUMBER MmAvailablePages;
extern PVOID MiSessionImageStart;
extern PVOID MiSessionImageEnd;
extern PVOID MiSessionViewStart;   // 0xBE000000
extern PVOID MiSessionSpaceWs;
extern ULONG MmSecondaryColorMask;
extern BOOLEAN MmProtectFreedNonPagedPool;
extern PVOID MmNonPagedPoolStart;
extern SIZE_T MmSizeOfNonPagedPoolInBytes;
extern PVOID MmNonPagedPoolExpansionStart;

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
MmGetExecuteOptions(
    _In_ PULONG ExecuteOptions)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmSetExecuteOptions(
    _In_ ULONG ExecuteOptions)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
MiSetDirtyBit(
    _In_ PVOID Address,
    _In_ PMMPTE Pte,
    _In_ BOOLEAN Param3)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

PMMPTE
NTAPI
MiCheckVirtualAddress(
    _In_ PVOID VirtualAddress,
    _Out_ PULONG ProtectCode,
    _Out_ PMMVAD* ProtoVad)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

NTSTATUS
NTAPI
MiResolveDemandZeroFault(
    _In_ PVOID Address,
    _In_ PMMPTE Pte,
    _In_ ULONG Protection,
    _In_ PEPROCESS Process,
    _In_ KIRQL OldIrql)
{
    PFN_NUMBER PageFrameNumber;
    MMPTE TempPte;
    PMMPFN Pfn;
    ULONG Color;
    BOOLEAN IsNeedAnyPage = FALSE;
    BOOLEAN HaveLock = FALSE;
    BOOLEAN IsZeroPage = FALSE;

    DPRINT("MiResolveDemandZeroFault: (%p) Pte %p [%I64X], %X, %p, %X\n",
           Address, Pte, MiGetPteContents(Pte), Protection, Process, OldIrql);

    /* Must currently only be called by paging path */
    if (Process > HYDRA_PROCESS && OldIrql == MM_NOIRQL)
    {
        ASSERT(FALSE);
    }
    else
    {
        /* Check if we not need a zero page */
        if (OldIrql == MM_NOIRQL)
        {
            /* Non session-backed image views must be not zeroed */
            if (Process != HYDRA_PROCESS ||
                (!MI_IS_SESSION_IMAGE_ADDRESS(Address) &&
                 (Address < (PVOID)MiSessionViewStart || Address >= (PVOID)MiSessionSpaceWs)))
            {
                IsNeedAnyPage = TRUE;
            }
        }

        /* Hardcode unknown color */
        Color = 0xFFFFFFFF;
    }

    /* Check if the PFN database should be acquired */
    if (OldIrql == MM_NOIRQL)
    {
        /* Acquire it and remember we should release it after */
        OldIrql = MiLockPfnDb(APC_LEVEL);
        HaveLock = TRUE;
    }

    /* We either manually locked the PFN DB, or already came with it locked */
    MI_ASSERT_PFN_LOCK_HELD();
    ASSERT (Pte->u.Hard.Valid == 0);

    /* Assert we have enough pages */
    if (MmAvailablePages < 0x80) // FIXME MiEnsureAvailablePageOrWait()
    {
        if (HaveLock)
            MiUnlockPfnDb(OldIrql, APC_LEVEL);

        return 0xC7303001;
    }

    /* Do we need a zero page? */
    if (Color != 0xFFFFFFFF)
    {
        ASSERT(FALSE);
    }
    else
    {
        /* Get a color, and see if we should grab a zero or non-zero page */
      #if !defined(CONFIG_SMP)
        Color = MI_GET_NEXT_COLOR();
      #else
        //#error FIXME
        Color = MI_GET_NEXT_COLOR();
      #endif

        if (IsNeedAnyPage)
            /* Process or system doesn't want a zero page, grab anything */
            PageFrameNumber = MiRemoveAnyPage(Color);
        else
            /* System wants a zero page, obtain one */
            PageFrameNumber = MiRemoveZeroPage(Color);
    }

    /* Initialize it */
    MiInitializePfn(PageFrameNumber, Pte, TRUE);

    /* Do we have the lock? */
    if (HaveLock)
    {
        /* Release it */
            MiUnlockPfnDb(OldIrql, APC_LEVEL);

        /* Update performance counters */
        if (Process > HYDRA_PROCESS)
            Process->NumberOfPrivatePages++;
    }

    /* Increment demand zero faults */
    InterlockedIncrement(&KeGetCurrentPrcb()->MmDemandZeroCount);

    /* Get the PFN entry */
    Pfn = MI_PFN_ELEMENT(PageFrameNumber);

    /* Zero the page if need be */
    if (IsZeroPage)
    {
        ASSERT(FALSE);
    }

    /* Fault on user PDE, or fault on user PTE? */
    if (Pte <= MiHighestUserPte)
        /* User fault, build a user PTE */
        MI_MAKE_HARDWARE_PTE_USER(&TempPte, Pte, Pte->u.Soft.Protection, PageFrameNumber);
    else
        /* This is a user-mode PDE, create a kernel PTE for it */
        MI_MAKE_HARDWARE_PTE(&TempPte, Pte, Pte->u.Soft.Protection, PageFrameNumber);

    /* Set it dirty if it's a writable page */
    if (MI_IS_PAGE_WRITEABLE(&TempPte))
        MI_MAKE_DIRTY_PAGE(&TempPte);

    /* Write it */
    MI_WRITE_VALID_PTE(Pte, TempPte);

    /* Did we manually acquire the lock */
    if (HaveLock)
    {
        /* Windows does these sanity checks */
        ASSERT(Pfn->u1.Event == 0);
        ASSERT(Pfn->u3.e1.PrototypePte == 0);

        DPRINT("MiResolveDemandZeroFault: FIXME MiAddValidPageToWorkingSet()\n");
    }

    /* It's all good now */
    DPRINT("MiResolveDemandZeroFault: Demand zero page has now been paged in\n");
    return STATUS_PAGE_FAULT_DEMAND_ZERO;
}

NTSTATUS
NTAPI
MiDispatchFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ PMMPTE Pte,
    _In_ PMMPTE SectionProto,
    _In_ BOOLEAN Recursive,
    _In_ PEPROCESS Process,
    _In_ PVOID TrapInformation,
    _In_ PMMVAD Vad)
{
    PMMPFN LockedProtoPfn = NULL;
    PMMSUPPORT SessionWs = NULL;
    MMPTE TempPte;
    KIRQL OldIrql;
    //KIRQL LockIrql = MM_NOIRQL;
    NTSTATUS Status;

    DPRINT("MiDispatchFault: %X, %p, Pte %p [%p], Proto %p [%I64X], %X, %p, %p, %p\n",
           FaultCode, Address, Pte, Pte->u.Long, SectionProto, MiGetPteContents(SectionProto),
           Recursive, Process, TrapInformation, Vad);

    /* Make sure APCs are off and we're not at dispatch */
    OldIrql = KeGetCurrentIrql();
    ASSERT(OldIrql <= APC_LEVEL);
    ASSERT(KeAreAllApcsDisabled() == TRUE);

    /* Do we have a prototype PTE? */
    if (SectionProto)
    {
        DPRINT1("MiDispatchFault: FIXME! SectionProto %X\n", SectionProto);
        ASSERT(FALSE);
    }

    TempPte = *Pte;

    ASSERT(TempPte.u.Hard.Valid == 0);
    ASSERT(TempPte.u.Soft.Prototype == 0);
    ASSERT(TempPte.u.Long != 0);

    if (TempPte.u.Soft.Transition)
    {
        DPRINT1("MiDispatchFault: FIXME! TempPte.u.Soft.Transition %X\n", TempPte.u.Soft.Transition);
        ASSERT(FALSE);Status = STATUS_NOT_IMPLEMENTED;
    }
    else if (TempPte.u.Soft.PageFileHigh)
    {
        DPRINT1("MiDispatchFault: FIXME! TempPte.u.Soft.PageFileHigh %X\n", TempPte.u.Soft.PageFileHigh);
        ASSERT(FALSE);Status = STATUS_NOT_IMPLEMENTED;
    }
    else
    {
        Status = MiResolveDemandZeroFault(Address, Pte, Pte->u.Soft.Protection, Process, MM_NOIRQL);
    }

    ASSERT(KeAreAllApcsDisabled() == TRUE);

    if (NT_SUCCESS(Status))
    {
        if (LockedProtoPfn)
        {
            DPRINT1("MiDispatchFault: FIXME! LockedProtoPfn %X\n", LockedProtoPfn);
            ASSERT(FALSE);
        }

        if (SessionWs)
        {
            DPRINT1("MiDispatchFault: FIXME! SessionWs %X\n", SessionWs);
            ASSERT(FALSE);
        }

        ASSERT(OldIrql == KeGetCurrentIrql());
        ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

        return Status;
    }

    if (Status == 0xC0033333)
    {
        DPRINT1("MiDispatchFault: FIXME! Status == 0xC0033333\n");
        ASSERT(FALSE);
    }

    if (Status == 0xC7303001 || Status == 0x87303000)
        Status = STATUS_SUCCESS;

    ASSERT(KeAreAllApcsDisabled() == TRUE);

    if (SessionWs)
    {
        DPRINT1("MiDispatchFault: FIXME! SessionWs %X\n", SessionWs);
        ASSERT(FALSE);
    }

    if (LockedProtoPfn)
    {
        DPRINT1("MiDispatchFault: FIXME! LockedProtoPfn %X\n", LockedProtoPfn);
        ASSERT(FALSE);
    }

    ASSERT(OldIrql == KeGetCurrentIrql());
    ASSERT(KeAreAllApcsDisabled() == TRUE);

    DPRINT("MiDispatchFault: return Status %X\n", Status);
    return Status;
}

NTSTATUS
NTAPI
MmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation)
{
    PMMPDE Pde = MiAddressToPde(Address);
    PMMPTE Pte = MiAddressToPte(Address);
    PMMPTE SectionProto = NULL;
    PETHREAD CurrentThread;
    PEPROCESS Process;
    PMMSUPPORT WorkingSet;
    PMMVAD Vad = NULL;
    PMMPFN Pfn;
    MMPTE TempPte;
    ULONG ProtectionCode;
    BOOLEAN IsSessionAddress;
    KIRQL OldIrql;
    KIRQL PfnLockIrql;
    KIRQL WsLockIrql = MM_NOIRQL;
    NTSTATUS Status;

    DPRINT1("MmAccessFault: %X, %p, Pde %p [%p], Pte %p [%p], %X, %p\n",
            FaultCode, Address, Pde, Pde->u.Long, Pte, Pte->u.Long, Mode, TrapInformation);

    OldIrql = KeGetCurrentIrql();

    /* Check for page fault on high IRQL */
    if (OldIrql > APC_LEVEL)
    {
      #if (_MI_PAGING_LEVELS < 3)
        /* Could be a page table for paged pool, which we'll allow */
        if (MI_IS_SYSTEM_PAGE_TABLE_ADDRESS(Address))
            MiSynchronizeSystemPde((PMMPDE)Pte);

        DPRINT("MiCheckPdeForPagedPool(Address %p)\n", Address);
        MiCheckPdeForPagedPool(Address);
      #endif

        /* Check if any of the top-level pages are invalid */
        if (!Pde->u.Hard.Valid || !Pte->u.Hard.Valid)
        {
            /* This fault is not valid, print out some debugging help */
            DbgPrint("MM:***PAGE FAULT AT IRQL > 1  Va %p, IRQL %lx\n", Address, OldIrql);

            if (TrapInformation)
            {
                PKTRAP_FRAME TrapFrame = TrapInformation;
                DbgPrint("MM:***EIP %p, EFL %p\n", TrapFrame->Eip, TrapFrame->EFlags);
                DbgPrint("MM:***EAX %p, ECX %p EDX %p\n", TrapFrame->Eax, TrapFrame->Ecx, TrapFrame->Edx);
                DbgPrint("MM:***EBX %p, ESI %p EDI %p\n", TrapFrame->Ebx, TrapFrame->Esi, TrapFrame->Edi);
            }

            /* Tell the trap handler to fail */
            DPRINT1("MmAccessFault: return %X\n", (STATUS_IN_PAGE_ERROR | 0x10000000));
            ASSERT(FALSE);//DbgBreakPoint();
            return (STATUS_IN_PAGE_ERROR | 0x10000000);
        }

        /* Not yet implemented in ReactOS */
        ASSERT(MI_IS_PAGE_LARGE(Pde) == FALSE);
        ASSERT((!MI_IS_NOT_PRESENT_FAULT(FaultCode) && MI_IS_PAGE_COPY_ON_WRITE(Pte)) == FALSE);

        /* Check if this was a write */
        if (MI_IS_WRITE_ACCESS(FaultCode))
        {
            /* Was it to a read-only page? */
            Pfn = MI_PFN_ELEMENT(Pte->u.Hard.PageFrameNumber);

            if (!(Pte->u.Long & PTE_READWRITE) &&
                !(Pfn->OriginalPte.u.Soft.Protection & MM_READWRITE))
            {
                /* Crash with distinguished bugcheck code */
                KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                             (ULONG_PTR)Address,
                             Pte->u.Long,
                             (ULONG_PTR)TrapInformation,
                             10);
            }
        }

        /* Nothing is actually wrong */
        DPRINT("Fault at IRQL %u is ok (%p)\n", OldIrql, Address);
        return STATUS_SUCCESS;
    }

    /* Check for kernel fault address */
    if (Address >= MmSystemRangeStart)
    {
        /* Bail out, if the fault came from user mode */
        if (Mode == UserMode)
        {
            DPRINT1("MmAccessFault: return STATUS_ACCESS_VIOLATION\n");
            return STATUS_ACCESS_VIOLATION;
        }

        /* Check if the higher page table entries are invalid */
        if (!Pde->u.Hard.Valid)
        {
            DPRINT("MiCheckPdeForPagedPool(Address %p)\n", Address);
            MiCheckPdeForPagedPool(Address);

            if (!Pde->u.Hard.Valid)
            {
                if (KeInvalidAccessAllowed(TrapInformation))
                {
                    DPRINT1("MmAccessFault: return STATUS_ACCESS_VIOLATION\n");
                    return STATUS_ACCESS_VIOLATION;
                }

                /* PDE (still) not valid, kill the system */
                KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                             (ULONG_PTR)Address,
                             FaultCode,
                             (ULONG_PTR)TrapInformation,
                             2);
            }
        }
        else
        {
            if (MI_IS_PAGE_LARGE(Pde))
            {
                DPRINT1("MmAccessFault: FIXME! MI_IS_PAGE_LARGE(Pde) %p\n", Pde);
                ASSERT(FALSE);//DbgBreakPoint();
                return STATUS_SUCCESS;
            }
        }

        /* Session faults? */
        IsSessionAddress = MI_IS_SESSION_ADDRESS(Address);

        /* The PDE is valid, so read the PTE */
        TempPte = *Pte;
        if (TempPte.u.Hard.Valid)
        {
            /* Check if this was system space or session space */
            if (!IsSessionAddress)
            {
                /* Check if the PTE is still valid under PFN lock */
                PfnLockIrql = MiLockPfnDb(APC_LEVEL);
                TempPte = *Pte;

                if (TempPte.u.Hard.Valid)
                {
                    /* Check if this was a write */
                    if (MI_IS_WRITE_ACCESS(FaultCode))
                    {
                        /* Was it to a read-only page? */
                        Pfn = MI_PFN_ELEMENT(TempPte.u.Hard.PageFrameNumber);

                        if (!(TempPte.u.Long & PTE_READWRITE) &&
                            !(Pfn->OriginalPte.u.Soft.Protection & MM_READWRITE))
                        {
                            /* Crash with distinguished bugcheck code */
                            KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                                         (ULONG_PTR)Address,
                                         (ULONG_PTR)TempPte.u.Long,
                                         (ULONG_PTR)TrapInformation,
                                         11);
                        }
                    }

                    if (MI_IS_WRITE_ACCESS(FaultCode) && !Pte->u.Hard.Dirty)
                        MiSetDirtyBit(Address, Pte, TRUE);
                }

                /* Release PFN lock and return all good */
                MiUnlockPfnDb(PfnLockIrql, APC_LEVEL);
                DPRINT("MmAccessFault: return STATUS_SUCCESS\n");
                return STATUS_SUCCESS;
            }
        }

        /* Check if this was a session PTE that needs to remap the session PDE */
        if (MI_IS_SESSION_PTE(Address))
        {
            /* Do the remapping */
            DPRINT1("MmAccessFault: FIXME! MI_IS_SESSION_PTE(%p) is TRUE\n", Address);
            ASSERT(FALSE);//DbgBreakPoint(); 
            Status = STATUS_NOT_IMPLEMENTED;
        }

        /* Check for a fault on the page table or hyperspace */
        if (MI_IS_PAGE_TABLE_OR_HYPER_ADDRESS(Address))
        {
            DPRINT("MmAccessFault: call MiCheckPdeForPagedPool(%p)\n", Address);

            if (MiCheckPdeForPagedPool(Address) == STATUS_WAIT_1)
            {
                DPRINT("MmAccessFault: return STATUS_SUCCESS\n");
                return STATUS_SUCCESS;
            }

            goto UserFault;
        }

        /* Get the current thread */
        CurrentThread = PsGetCurrentThread();

        /* What kind of address is this */
        if (!IsSessionAddress)
        {
            /* Use the system working set */
            WorkingSet = &MmSystemCacheWs;
            Process = NULL;
        }
        else
        {
            /* Use the session process and working set */
            WorkingSet = &MmSessionSpace->GlobalVirtualAddress->Vm;
            Process = HYDRA_PROCESS;
        }

        /* Acquire the working set lock */
        KeRaiseIrql(APC_LEVEL, &WsLockIrql);
        MiLockWorkingSet(CurrentThread, WorkingSet);

        /* Re-read PTE now that we own the lock */
        TempPte = *Pte;
        if (TempPte.u.Hard.Valid)
        {
            DPRINT1("MmAccessFault: FIXME! TempPte.u.Hard.Valid\n");
            ASSERT(FALSE);//DbgBreakPoint();
        }

        /* Check one kind of prototype PTE */
        if (TempPte.u.Soft.Prototype)
        {
            /* Make sure protected pool is on, and that this is a pool address */
            if (MmProtectFreedNonPagedPool &&
                ((Address >= MmNonPagedPoolStart && Address < (PVOID)((ULONG_PTR)MmNonPagedPoolStart + MmSizeOfNonPagedPoolInBytes)) ||
                 (Address >= MmNonPagedPoolExpansionStart && Address < MmNonPagedPoolEnd)))
            {
                if (KeInvalidAccessAllowed(TrapInformation))
                {
                    DPRINT1("MmAccessFault: return STATUS_ACCESS_VIOLATION\n");
                    ASSERT(FALSE); // DbgBreakPoint();
                    return STATUS_ACCESS_VIOLATION;
                }

                /* Bad boy, bad boy, whatcha gonna do, whatcha gonna do when ARM3 comes for you! */
                DPRINT1("KeBugCheckEx()\n");ASSERT(FALSE);//DbgBreakPoint();
                KeBugCheckEx(DRIVER_CAUGHT_MODIFYING_FREED_POOL, (ULONG_PTR) Address, FaultCode, Mode, 4);
            }

            /* Get the prototype PTE! */
            SectionProto = MiGetProtoPtr(&TempPte);

            /* Do we need to locate the prototype PTE in session space? */
            if (IsSessionAddress &&
                TempPte.u.Soft.PageFileHigh == MI_PTE_LOOKUP_NEEDED)
            {
                /* Yep, go find it as well as the VAD for it */
                SectionProto = MiCheckVirtualAddress(Address, &ProtectionCode, &Vad);
                if (!SectionProto)
                {
                    DPRINT1("MmAccessFault: FIXME!\n"); ASSERT(FALSE);//DbgBreakPoint();
                    return (STATUS_IN_PAGE_ERROR | 0x10000000);
                }
            }
        }
        else if (!TempPte.u.Soft.Transition && !TempPte.u.Soft.Protection)
        {
            DPRINT1("MmAccessFault: FIXME! !Transition and !Protection\n");
            ASSERT(FALSE);//DbgBreakPoint();
        }
        else if (TempPte.u.Soft.Protection == MM_NOACCESS)
        {
            DPRINT1("MmAccessFault: FIXME! Protection == MM_NOACCESS\n");
            ASSERT(FALSE);//DbgBreakPoint();
        }

        if (MI_IS_WRITE_ACCESS(FaultCode) &&
            !SectionProto &&
            !IsSessionAddress &&
            !TempPte.u.Hard.Valid)
        {
            ULONG protectionCode;

            /* Get the protection code */
            if (TempPte.u.Soft.Transition)
                protectionCode = (ULONG)TempPte.u.Trans.Protection;
            else
                protectionCode = (ULONG)TempPte.u.Soft.Protection;
                
            if (!(protectionCode & MM_READWRITE))
            {
                /* Bugcheck the system! */
                KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                             (ULONG_PTR)Address,
                             TempPte.u.Long,
                             (ULONG_PTR)TrapInformation,
                             14);
            }
        }

        /* Now do the real fault handling */
        Status = MiDispatchFault(FaultCode,
                                 Address,
                                 Pte,
                                 SectionProto,
                                 FALSE,
                                 Process,
                                 TrapInformation,
                                 NULL);

        /* Release the working set */
        ASSERT(KeAreAllApcsDisabled() == TRUE);

        if (WorkingSet->Flags.GrowWsleHash)
        {
            DPRINT1("MmAccessFault: FIXME! WorkingSet->Flags.GrowWsleHash\n");
            ASSERT(FALSE);
        }

        MiUnlockWorkingSet(CurrentThread, WorkingSet);
        ASSERT(WsLockIrql != MM_NOIRQL);
        KeLowerIrql(WsLockIrql);

        if (!(WorkingSet->PageFaultCount & 0xFFF) && (MmAvailablePages < 1024))
        {
            DPRINT1("MmAccessFault: FIXME! MmAvailablePages %X\n", MmAvailablePages);
            ASSERT(FALSE);//DbgBreakPoint();
        }

        /* We are done! */
        goto Exit1;
    }

    /* This is a user fault */
UserFault:
    DPRINT1("MmAccessFault: FIXME! UserFault\n");
    ASSERT(FALSE);//DbgBreakPoint();
    return STATUS_NOT_IMPLEMENTED;

Exit1:

    if (Status != STATUS_SUCCESS)
    {
        DPRINT("MmAccessFault: Status %X\n", Status);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MmAccessFault: FIXME MmIsRetryIoStatus(). Status %X\n", Status);
            ASSERT(FALSE);//DbgBreakPoint();
        }

        if (Status != STATUS_SUCCESS)
        {
            DPRINT("MmAccessFault: fixeme NotifyRoutine. Status %X\n", Status);
        }
    }

    DPRINT("MmAccessFault: return Status %X\n", Status);
    return Status;
}

#if (_MI_PAGING_LEVELS == 2)
static
NTSTATUS
FASTCALL
MiCheckPdeForSessionSpace(
    _In_ PVOID Address)
{
    MMPTE TempPde;
    PMMPDE Pde;
    PVOID SessionAddress;
    ULONG Index;

    /* Is this a session PTE? */
    if (MI_IS_SESSION_PTE(Address))
    {
        /* Make sure the PDE for session space is valid */
        Pde = MiAddressToPde(MmSessionSpace);
        if (!Pde->u.Hard.Valid)
        {
            /* This means there's no valid session, bail out */
            DbgPrint("MiCheckPdeForSessionSpace: No current session for PTE %p\n", Address);
            ASSERT(FALSE); // MiDbgBreakPointEx(); 
            return STATUS_ACCESS_VIOLATION;
        }

        /* Now get the session-specific page table for this address */
        SessionAddress = MiPteToAddress(Address);
        Pde = MiAddressToPte(Address);
        if (Pde->u.Hard.Valid)
            return STATUS_WAIT_1;

        /* It's not valid, so find it in the page table array */
        Index = ((ULONG_PTR)SessionAddress - (ULONG_PTR)MmSessionBase) >> 22;

        TempPde.u.Long = MmSessionSpace->PageTables[Index].u.Long;
        if (TempPde.u.Hard.Valid)
        {
            /* The copy is valid, so swap it in */
            InterlockedExchange((PLONG)Pde, TempPde.u.Long);
            return STATUS_WAIT_1;
        }

        /* We don't seem to have allocated a page table for this address yet? */
        DbgPrint("MiCheckPdeForSessionSpace: No Session PDE for PTE %p, %p\n", Pde->u.Long, SessionAddress);
        ASSERT(FALSE); // MiDbgBreakPointEx(); 
        return STATUS_ACCESS_VIOLATION;
    }

    /* Is the address also a session address? If not, we're done */
    if (!MI_IS_SESSION_ADDRESS(Address))
        return STATUS_SUCCESS;

    /* It is, so again get the PDE for session space */
    Pde = MiAddressToPde(MmSessionSpace);
    if (!Pde->u.Hard.Valid)
    {
        /* This means there's no valid session, bail out */
        DbgPrint("MiCheckPdeForSessionSpace: No current session for VA %p\n", Address);
        ASSERT(FALSE); // MiDbgBreakPointEx(); 
        return STATUS_ACCESS_VIOLATION;
    }

    /* Now get the PDE for the address itself */
    Pde = MiAddressToPde(Address);
    if (!Pde->u.Hard.Valid)
    {
        /* Do the swap, we should be good to go */
        Index = ((ULONG_PTR)Address - (ULONG_PTR)MmSessionBase) >> 22;
        Pde->u.Long = MmSessionSpace->PageTables[Index].u.Long;
        if (Pde->u.Hard.Valid)
            return STATUS_WAIT_1;

        /* We had not allocated a page table for this session address yet, fail! */
        DbgPrint("MiCheckPdeForSessionSpace: No Session PDE for VA %p, %p\n", Pde->u.Long, Address);
        ASSERT(FALSE); // MiDbgBreakPointEx(); 
        return STATUS_ACCESS_VIOLATION;
    }

    /* It's valid, so there's nothing to do */
    return STATUS_SUCCESS;
}

NTSTATUS
FASTCALL
MiCheckPdeForPagedPool(
    _In_ PVOID Address)
{
    PMMPDE Pde;
    NTSTATUS Status = STATUS_SUCCESS;

    /* Check session PDE */
    if (MI_IS_SESSION_ADDRESS(Address))
        return MiCheckPdeForSessionSpace(Address);

    if (MI_IS_SESSION_PTE(Address))
        return MiCheckPdeForSessionSpace(Address);

    /* Check if this is a fault while trying to access the page table itself */
    if (MI_IS_SYSTEM_PAGE_TABLE_ADDRESS(Address))
    {
        /* Send a hint to the page fault handler that this is only a valid fault
           if we already detected this was access within the page table range.
        */
        Pde = (PMMPDE)MiAddressToPte(Address);
        Status = STATUS_WAIT_1;
    }
    else if (Address < MmSystemRangeStart)
    {
        /* This is totally illegal */
        DPRINT1("MiCheckPdeForPagedPool: STATUS_ACCESS_VIOLATION. Address %p\n", Address);
        //ASSERT(FALSE); // MiDbgBreakPointEx(); 
        return STATUS_ACCESS_VIOLATION;
    }
    else
    {
        /* Get the PDE for the address */
        Pde = MiAddressToPde(Address);
    }

    /* Check if it's not valid */
    if (!Pde->u.Hard.Valid)
        /* Copy it from our double-mapped system page directory */
        InterlockedExchangePte(Pde, MmSystemPagePtes[MiGetPdeOffset(Pde)].u.Long);

    return Status;
}
#else
  #error FIXME
#endif

/* EOF */
