
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
    if (MmAvailablePages < 0x80) // FIMXE MiEnsureAvailablePageOrWait()
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
MmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
