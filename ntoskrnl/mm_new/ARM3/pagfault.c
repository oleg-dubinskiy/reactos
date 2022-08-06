
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
SIZE_T MmSystemLockPagesCount;

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
extern MMPTE PrototypePte;
extern MMPDE DemandZeroPde;
extern PMMPTE MmSharedUserDataPte;
extern MM_PAGED_POOL_INFO MmPagedPoolInfo;

/* FUNCTIONS ******************************************************************/

FORCEINLINE
BOOLEAN
MiIsAccessAllowed(
    _In_ ULONG ProtectionMask,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN Execute)
{
    #define _BYTE_MASK(Bit0, Bit1, Bit2, Bit3, Bit4, Bit5, Bit6, Bit7) \
        (Bit0)|(Bit1 << 1)|(Bit2 << 2)|(Bit3 << 3)|(Bit4 << 4)|(Bit5 << 5)|(Bit6 << 6)|(Bit7 << 7)

    static const UCHAR AccessAllowedMask[2][2] =
    {
        {   // Protect 0  1  2  3  4  5  6  7
            _BYTE_MASK(0, 1, 1, 1, 1, 1, 1, 1), // READ
            _BYTE_MASK(0, 0, 1, 1, 0, 0, 1, 1), // EXECUTE READ
        },
        {
            _BYTE_MASK(0, 0, 0, 0, 1, 1, 1, 1), // WRITE
            _BYTE_MASK(0, 0, 0, 0, 0, 0, 1, 1), // EXECUTE WRITE
        }
    };

    /* We want only the lower access bits */
    ProtectionMask &= MM_PROTECT_ACCESS;

    /* Look it up in the table */
    return (((AccessAllowedMask[Write != 0][Execute != 0] >> ProtectionMask) & 1) == 1);
}

NTSTATUS
NTAPI
MiAccessCheck(
    _In_ PMMPTE Pte,
    _In_ BOOLEAN StoreInstruction,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ ULONG_PTR ProtectionMask,
    _In_ PVOID TrapFrame,
    _In_ BOOLEAN LockHeld)
{
    MMPTE TempPte;

    /* Check for invalid user-mode access */
    if (PreviousMode == UserMode && Pte > MiHighestUserPte)
    {
        DPRINT1("MiAccessCheck: STATUS_ACCESS_VIOLATION\n");
        return STATUS_ACCESS_VIOLATION;
    }

    /* Capture the PTE -- is it valid? */
    TempPte = *Pte;

    if (TempPte.u.Hard.Valid)
    {
        /* Was someone trying to write to it? */
        if (StoreInstruction)
        {
            /* Is it writable?*/
            if (MI_IS_PAGE_WRITEABLE(&TempPte) ||
                MI_IS_PAGE_COPY_ON_WRITE(&TempPte))
            {
                /* Then there's nothing to worry about */
                return STATUS_SUCCESS;
            }

            /* Oops! This isn't allowed */
            DPRINT1("MiAccessCheck: STATUS_ACCESS_VIOLATION\n");
            return STATUS_ACCESS_VIOLATION;
        }

        /* Someone was trying to read from a valid PTE, that's fine too */
        return STATUS_SUCCESS;
    }

    /* Check if the protection on the page allows what is being attempted */
    if (!MiIsAccessAllowed(ProtectionMask, StoreInstruction, FALSE))
    {
        DPRINT1("MiAccessCheck: STATUS_ACCESS_VIOLATION\n");
        return STATUS_ACCESS_VIOLATION;
    }

    /* Check if this is a guard page */
    if ((ProtectionMask & MM_PROTECT_SPECIAL) != MM_GUARDPAGE)
        /* Nothing to do */
        return STATUS_SUCCESS;

    DPRINT1("MiAccessCheck: FIXME\n");
    ASSERT(FALSE);

    return STATUS_GUARD_PAGE_VIOLATION;
}

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
MiCheckForUserStackOverflow(
    _In_ PVOID Address,
    _In_ PVOID TrapInformation)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PTEB Teb = CurrentThread->Tcb.Teb;
    PVOID StackBase;
    PVOID DeallocationStack;
    PVOID NextStackAddress;
    SIZE_T GuaranteedSize;
    NTSTATUS Status;

    /* Do we own the address space lock? */
    if (CurrentThread->AddressSpaceOwner)
    {
        /* This isn't valid */
        DPRINT1("MiCheckForUserStackOverflow: Process owns address space lock\n");
        ASSERT(KeAreAllApcsDisabled() == TRUE);
        return STATUS_GUARD_PAGE_VIOLATION;
    }

    /* Are we attached? */
    if (KeIsAttachedProcess())
    {
        /* This isn't valid */
        DPRINT1("MiCheckForUserStackOverflow: Process is attached\n");
        return STATUS_GUARD_PAGE_VIOLATION;
    }

    /* Read the current settings */
    StackBase = Teb->NtTib.StackBase;
    DeallocationStack = Teb->DeallocationStack;
    GuaranteedSize = Teb->GuaranteedStackBytes;

    DPRINT("MiCheckForUserStackOverflow: StackBase %p, DeallocationStack %p, GuaranteedSize %X\n",
            StackBase, DeallocationStack, GuaranteedSize);

    /* Guarantees make this code harder, for now, assume there aren't any */
    ASSERT(GuaranteedSize == 0);

    /* So allocate only the minimum guard page size */
    GuaranteedSize = PAGE_SIZE;

    /* Does this faulting stack address actually exist in the stack? */
    if (Address >= StackBase || Address < DeallocationStack)
    {
        /* That's odd... */
        DPRINT1("Faulting address outside of stack bounds. Address %p, StackBase %p, DeallocationStack %p\n",
                Address, StackBase, DeallocationStack);

        return STATUS_GUARD_PAGE_VIOLATION;
    }

    /* This is where the stack will start now */
    NextStackAddress = (PVOID)((ULONG_PTR)PAGE_ALIGN(Address) - GuaranteedSize);

    /* Do we have at least one page between here and the end of the stack? */
    if (((ULONG_PTR)NextStackAddress - PAGE_SIZE) <= (ULONG_PTR)DeallocationStack)
    {
        /* We don't -- Trying to make this guard page valid now */
        DPRINT1("MiCheckForUserStackOverflow: Close to our death...\n");

        /* Calculate the next memory address */
        NextStackAddress = (PVOID)((ULONG_PTR)PAGE_ALIGN(DeallocationStack) + GuaranteedSize);

        /* Allocate the memory */
        Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                         &NextStackAddress,
                                         0,
                                         &GuaranteedSize,
                                         MEM_COMMIT,
                                         PAGE_READWRITE);
        if (NT_SUCCESS(Status))
        {
            /* Success! */
            Teb->NtTib.StackLimit = NextStackAddress;
            return STATUS_STACK_OVERFLOW;
        }

        DPRINT1("MiCheckForUserStackOverflow: Failed to allocate memory\n");
        return STATUS_STACK_OVERFLOW;
    }

    /* Don't handle this flag yet */
    ASSERT((PsGetCurrentProcess()->Peb->NtGlobalFlag & FLG_DISABLE_STACK_EXTENSION) == 0);

    /* Update the stack limit */
    Teb->NtTib.StackLimit = (PVOID)((ULONG_PTR)NextStackAddress + GuaranteedSize);

    /* Now move the guard page to the next page */
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &NextStackAddress,
                                     0,
                                     &GuaranteedSize,
                                     MEM_COMMIT,
                                     (PAGE_READWRITE | PAGE_GUARD));

    if (NT_SUCCESS(Status) || Status == STATUS_ALREADY_COMMITTED)
    {
        /* We did it! */
        DPRINT("MiCheckForUserStackOverflow: Guard page handled successfully for %p\n", Address);
        return STATUS_PAGE_FAULT_GUARD_PAGE;
    }

    /* Fail, we couldn't move the guard page */
    DPRINT1("MiCheckForUserStackOverflow: Guard page failure: %X\n", Status);
    ASSERT(FALSE);

    return STATUS_STACK_OVERFLOW;
}

VOID
NTAPI
MiZeroPfn(
    _In_ PFN_NUMBER PageFrameNumber)
{
    PMMPTE ZeroPte;
    MMPTE TempPte;
    PMMPFN Pfn;
    PVOID ZeroAddress;

    /* Get the PFN for this page */
    Pfn = MiGetPfnEntry(PageFrameNumber);
    ASSERT(Pfn);

    /* Grab a system PTE we can use to zero the page */
    ZeroPte = MiReserveSystemPtes(1, SystemPteSpace);
    ASSERT(ZeroPte);

    /* Initialize the PTE for it */
    TempPte = ValidKernelPte;
    TempPte.u.Hard.PageFrameNumber = PageFrameNumber;

    /* Setup caching */
    if (Pfn->u3.e1.CacheAttribute == MiWriteCombined)
    {
        /* Write combining, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_COMBINED(&TempPte);
    }
    else if (Pfn->u3.e1.CacheAttribute == MiNonCached)
    {
        /* Write through, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_THROUGH(&TempPte);
    }

    /* Make the system PTE valid with our PFN */
    MI_WRITE_VALID_PTE(ZeroPte, TempPte);

    /* Get the address it maps to, and zero it out */
    ZeroAddress = MiPteToAddress(ZeroPte);
    KeZeroPages(ZeroAddress, PAGE_SIZE);

    /* Now get rid of it */
    MiReleaseSystemPtes(ZeroPte, 1, SystemPteSpace);
}

PMMPTE
NTAPI
MiCheckVirtualAddress(
    _In_ PVOID VirtualAddress,
    _Out_ PULONG ProtectCode,
    _Out_ PMMVAD* ProtoVad)
{
    PMMVAD Vad;
    PMMVAD_LONG VadLong;
    PMMPTE SectionProto;
    UINT64 CommittedSize;
    ULONG_PTR Vpn;
    ULONG_PTR Count;

    /* No prototype/section support for now */
    *ProtoVad = NULL;

    /* User or kernel fault? */
    if (VirtualAddress <= MM_HIGHEST_USER_ADDRESS)
    {
        /* Special case for shared data */
        if (PAGE_ALIGN(VirtualAddress) == (PVOID)MM_SHARED_USER_DATA_VA)
        {
            /* It's a read-only page */
            *ProtectCode = MM_READONLY;
            return MmSharedUserDataPte;
        }

        /* Find the VAD, it might not exist if the address is bogus */
        Vad = MiLocateAddress(VirtualAddress);
        if (!Vad)
        {
            /* Bogus virtual address */
            *ProtectCode = MM_NOACCESS;
            return NULL;
        }

        /* ReactOS does not handle physical memory VADs yet */
        ASSERT(Vad->u.VadFlags.VadType != VadDevicePhysicalMemory);

        /* Check if it's a section, or just an allocation */
        if (Vad->u.VadFlags.PrivateMemory)
        {
            /* ReactOS does not handle AWE VADs yet */
            ASSERT(Vad->u.VadFlags.VadType != VadAwe);

            /* This must be a TEB/PEB VAD */
            if (Vad->u.VadFlags.MemCommit)
                /* It's committed, so return the VAD protection */
                *ProtectCode = (ULONG)Vad->u.VadFlags.Protection;
            else
                /* It has not yet been committed, so return no access */
                *ProtectCode = MM_NOACCESS;

            /* In both cases, return no PTE */
            return NULL;
        }

        Vpn = ((ULONG_PTR)VirtualAddress / PAGE_SIZE);

        if (Vad->u.VadFlags.VadType == VadImageMap)
        {
            *ProtectCode = 0x100;
        }
        else
        {
            /* Return the Prototype PTE and the protection for the page mapping */
            *ProtectCode = Vad->u.VadFlags.Protection;

            if (!Vad->u2.VadFlags2.ExtendableFile)
                /* Return the proto VAD */
                *ProtoVad = Vad;
        }

        /* Get the section proto for this page */
        SectionProto = MI_GET_PROTOTYPE_PTE_FOR_VPN(Vad, Vpn);
        if (!SectionProto)
            *ProtectCode = MM_NOACCESS;

        if (!Vad->u2.VadFlags2.ExtendableFile)
            return SectionProto;

        Count = (Vpn - Vad->StartingVpn);
        VadLong = (PMMVAD_LONG)Vad;
        CommittedSize = (VadLong->u4.ExtendedInfo->CommittedSize - 1);

        if (Count > (ULONG_PTR)(CommittedSize / PAGE_SIZE))
            *ProtectCode = MM_NOACCESS;

        return SectionProto;
    }

    if (MI_IS_PAGE_TABLE_ADDRESS(VirtualAddress))
    {
        /* This should never happen, as these addresses are handled by the double-maping */
        if ((PMMPTE)VirtualAddress >= MiAddressToPte(MmPagedPoolStart) &&
            (PMMPTE)VirtualAddress <= MmPagedPoolInfo.LastPteForPagedPool)
        {
            /* Fail such access */
            *ProtectCode = MM_NOACCESS;
            return NULL;
        }

        /* Return full access rights */
        *ProtectCode = MM_READWRITE;
        return NULL;
    }

    if (MI_IS_SESSION_ADDRESS(VirtualAddress))
        /* ReactOS does not have an image list yet, so bail out to failure case */
        ASSERT(IsListEmpty(&MmSessionSpace->ImageList));

    /* Default case -- failure */
    *ProtectCode = MM_NOACCESS;

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
    ASSERT(Pte->u.Hard.Valid == 0);

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
MiCompleteProtoPteFault(
    _In_ BOOLEAN StoreInstruction,
    _In_ PVOID Address,
    _In_ PMMPTE Pte,
    _In_ PMMPTE SectionProto,
    _In_ KIRQL OldIrql,
    _In_ PMMPFN* LockedProtoPfn)
{
    PMMPTE OriginalPte;
    PMMPDE Pde;
    PMMPFN Pfn;
    MMPTE TempPte;
    ULONG_PTR Protection;
    PFN_NUMBER PageFrameIndex;
    BOOLEAN OriginalProtection;
    BOOLEAN DirtyPage;

    DPRINT("MiCompleteProtoPteFault: %X, %p, %p [%I64X], %p [%I64X], %X\n",
           StoreInstruction, Address, Pte, MiGetPteContents(Pte), SectionProto, MiGetPteContents(SectionProto), OldIrql);

    /* Must be called with an valid prototype PTE, with the PFN lock held */
    MI_ASSERT_PFN_LOCK_HELD();
    ASSERT(SectionProto->u.Hard.Valid == 1);

    /* Increment the share count for the page table */
    Pde = MiAddressToPte(Pte);
    Pfn = MiGetPfnEntry(Pde->u.Hard.PageFrameNumber);
    Pfn->u2.ShareCount++;

    /* Get the page */
    PageFrameIndex = PFN_FROM_PTE(SectionProto);

    /* Get the PFN entry and set it as a prototype PTE */
    Pfn = MiGetPfnEntry(PageFrameIndex);
    Pfn->u3.e1.PrototypePte = 1;

    OriginalPte = &Pfn->OriginalPte;

    /* Check where we should be getting the protection information from */
    if (Pte->u.Soft.PageFileHigh == MI_PTE_LOOKUP_NEEDED)
    {
        /* Get the protection from the PTE, there's no real Proto PTE data */
        Protection = Pte->u.Soft.Protection;

        /* Remember that we did not use the proto protection */
        OriginalProtection = FALSE;
    }
    else
    {
        /* Get the protection from the original PTE link */
        Protection = OriginalPte->u.Soft.Protection;

        /* Remember that we used the original protection */
        OriginalProtection = TRUE;

        /* Check if this was a write on a read only proto */
        if (StoreInstruction && !(Protection & MM_READWRITE))
            /* Clear the flag */
            StoreInstruction = 0;
    }

    /* Check if this was a write on a non-COW page */
    DirtyPage = FALSE;

    if (StoreInstruction && ((Protection & MM_WRITECOPY) != MM_WRITECOPY))
    {
        /* Then the page should be marked dirty */
        DirtyPage = TRUE;

        ASSERT(Pfn->u3.e1.Rom == 0);
        Pfn->u3.e1.Modified = 1;

        DPRINT("MiCompleteProtoPteFault: OriginalPte %X\n", OriginalPte->u.Long);

        if (!OriginalPte->u.Soft.Prototype && !Pfn->u3.e1.WriteInProgress)
        {
            ULONG PageFileHigh = OriginalPte->u.Soft.PageFileHigh;

            if (PageFileHigh && PageFileHigh != MI_PTE_LOOKUP_NEEDED)
            {
                DPRINT1("MiCompleteProtoPteFault: FIXME. PageFileHigh %X\n", PageFileHigh);
                ASSERT(FALSE);
            }

            OriginalPte->u.Soft.PageFileHigh = 0;
        }
    }

    /* Did we get a locked incoming PFN? */
    if (*LockedProtoPfn)
    {
        /* Drop a reference */
        ASSERT((*LockedProtoPfn)->u3.e2.ReferenceCount >= 1);
        MiDereferencePfnAndDropLockCount(*LockedProtoPfn);
        *LockedProtoPfn = NULL;
    }

    /* Release the PFN lock */
    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    /* Remove special/caching bits */
    Protection &= ~MM_PROTECT_SPECIAL;

    /* Setup caching */
    if (Pfn->u3.e1.CacheAttribute == MiWriteCombined)
    {
        /* Write combining, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_COMBINED(&TempPte);
    }
    else if (Pfn->u3.e1.CacheAttribute == MiNonCached)
    {
        /* Write through, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_THROUGH(&TempPte);
    }

    /* Check if this is a kernel or user address */
    if (Address < MmSystemRangeStart)
        /* Build the user PTE */
        MI_MAKE_HARDWARE_PTE_USER(&TempPte, Pte, Protection, PageFrameIndex);
    else
        /* Build the kernel PTE */
        MI_MAKE_HARDWARE_PTE(&TempPte, Pte, Protection, PageFrameIndex);

    /* Set the dirty flag if needed */
    if (DirtyPage)
        MI_MAKE_DIRTY_PAGE(&TempPte);

    /* Write the PTE */
    MI_WRITE_VALID_PTE(Pte, TempPte);

    /* Reset the protection if needed */
    if (OriginalProtection)
        Protection = MM_ZERO_ACCESS;

    /* Return success */
    ASSERT(Pte == MiAddressToPte(Address));

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MiResolveProtoPteFault(
    _In_ BOOLEAN StoreInstruction,
    _In_ PVOID Address,
    _In_ PMMPTE Pte,
    _In_ PMMPTE SectionProto,
    _Inout_ PMMPFN* LockedProtoPfn,
    _Out_ PMI_PAGE_SUPPORT_BLOCK* OutPageBlock,
    _Out_ PMMPTE PteValue,
    _In_ PEPROCESS Process,
    _In_ KIRQL OldIrql,
    _In_ PVOID TrapInformation)
{
    PMI_PAGE_SUPPORT_BLOCK PageBlock;
    PLIST_ENTRY Entry;
    PMMPFN Pfn;
    MMPTE TempProto;
    MMPTE TempPte;
    PFN_NUMBER PageFrameIndex;
    ULONG Protection;
    BOOLEAN IsLocked = TRUE;
    NTSTATUS Status;

    DPRINT("MiResolveProtoPteFault: %p, %p, %p [%p], %X, %X\n",
           Address, Pte, SectionProto, SectionProto->u.Long, Process, OldIrql);

    /* Must be called with an invalid, prototype PTE, with the PFN lock held */
    MI_ASSERT_PFN_LOCK_HELD();
    ASSERT(Pte->u.Hard.Valid == 0);
    ASSERT(Pte->u.Soft.Prototype == 1);

    /* Read the prototype PTE and check if it's valid */
    TempProto = *SectionProto;

    if (TempProto.u.Hard.Valid)
    {
        /* One more user of this mapped page */
        PageFrameIndex = PFN_FROM_PTE(&TempProto);
        Pfn = MiGetPfnEntry(PageFrameIndex);
        Pfn->u2.ShareCount++;

        /* Call it a transition */
        InterlockedIncrement(&KeGetCurrentPrcb()->MmTransitionCount);

        DPRINT1("MiResolveProtoPteFault: FIXME\n");
        ASSERT(FALSE);

        /* Complete the prototype PTE fault -- this will release the PFN lock */
        Status = MiCompleteProtoPteFault(StoreInstruction, Address, Pte, SectionProto, OldIrql, LockedProtoPfn);
        return Status;
    }

    /* Make sure there's some protection mask */
    if (!TempProto.u.Long)
    {
        /* Release the lock */
        DPRINT1("MiResolveProtoPteFault: Access on reserved section?\n");
        MiUnlockPfnDb(OldIrql, APC_LEVEL);
        return STATUS_ACCESS_VIOLATION;
    }

    PageBlock = NULL;

    /* Check for access rights on the PTE proper */
    TempPte = *Pte;

    if (TempPte.u.Soft.PageFileHigh != MI_PTE_LOOKUP_NEEDED)
    {
        if (TempPte.u.Proto.ReadOnly)
        {
            Protection = MM_READONLY;
        }
        else
        {
            Status = MiAccessCheck(SectionProto,
                                   StoreInstruction,
                                   KernelMode,
                                   TempProto.u.Soft.Protection,
                                   TrapInformation,
                                   TRUE);

            if (Status != STATUS_SUCCESS)
            {
                DPRINT("MiResolveProtoPteFault: Status %X\n", Status);

                if (StoreInstruction &&
                    Address >= MmSessionBase && Address < MiSessionSpaceEnd &&
                    MmSessionSpace->ImageLoadingCount)
                {
                    for (Entry = MmSessionSpace->ImageList.Flink;
                         Entry != &MmSessionSpace->ImageList;
                         Entry = Entry->Flink)
                    {
                        DPRINT1("MiResolveProtoPteFault: FIXME\n");
                        ASSERT(FALSE);
                    }
                }

                MiUnlockPfnDb(OldIrql, APC_LEVEL);
                return Status;
            }

            DPRINT("MiResolveProtoPteFault: Status %X\n", Status);

            Protection = TempProto.u.Soft.Protection;
        }
    }
    else
    {
        Protection = TempPte.u.Soft.Protection;
    }

    if (Pte <= MiHighestUserPte &&
        Process > (PEPROCESS)2 &&
        Process->CloneRoot)
    {
        DPRINT1("MiResolveProtoPteFault: FIXME\n");
        ASSERT(FALSE);
        Protection = MM_WRITECOPY;
    }

    /* Check for writing copy on write page */
    if (!MI_IS_MAPPED_PTE(&TempProto) && ((Protection & MM_WRITECOPY) == MM_WRITECOPY))
    {
        DPRINT1("MiResolveProtoPteFault: DemandZero page with Protection protection\n");

        ASSERT(Process != NULL);
        MiUnlockPfnDb(OldIrql, APC_LEVEL);

        DPRINT1("MiResolveProtoPteFault: FIXME\n");
        ASSERT(FALSE);

        //DPRINT("MiResolveProtoPteFault: Status %X\n", Status);
        return Status;
    }

    if (TempProto.u.Soft.Prototype)
    {
        DPRINT1("MiResolveProtoPteFault: FIXME\n");
        ASSERT(FALSE);

        /* This is mapped file fault */
        Status = STATUS_NOT_IMPLEMENTED;//MiResolveMappedFileFault(SectionProto, OutPageBlock, Process, OldIrql);
        if (Status == 0xC0033333)
        {
            *PteValue = *SectionProto;

            ASSERT(PteValue->u.Hard.Valid == 0);
            ASSERT(PteValue->u.Soft.Prototype == 0);
            ASSERT(PteValue->u.Soft.Transition == 1);
        }
    }
    else if (TempProto.u.Soft.Transition)
    {
        DPRINT1("MiResolveProtoPteFault: FIXME\n");
        ASSERT(FALSE);

        ASSERT(OldIrql != MM_NOIRQL);

        /* Resolve the transition fault */
        Status = STATUS_NOT_IMPLEMENTED;//MiResolveTransitionFault(Address, SectionProto, Process, OldIrql, &PageBlock);
    }
    else if (TempProto.u.Soft.PageFileHigh)
    {
        /* We don't support paged out pages */
        DPRINT1("MiResolveProtoPteFault: FIXME\n");
        ASSERT(FALSE);

        Status = STATUS_NOT_IMPLEMENTED;//MiResolvePageFileFault (Address, SectionProto, PteValue, OutPageBlock, Process, OldIrql);

        if (Status == 0xC0033333)
        {
            ASSERT(PteValue->u.Hard.Valid == 0);
            ASSERT(PteValue->u.Soft.Prototype == 0);
            ASSERT(PteValue->u.Soft.Transition == 1);
        }

        ASSERT(KeAreAllApcsDisabled() == TRUE);
        IsLocked = FALSE;
    }
    else
    {
        ASSERT(OldIrql != MM_NOIRQL);

        /* Resolve the demand zero fault */
        Status = MiResolveDemandZeroFault(Address, SectionProto, (ULONG)TempProto.u.Soft.Protection, Process, OldIrql);
    }

    if (NT_SUCCESS(Status))
    {
        ASSERT(Pte->u.Hard.Valid == 0);

        /* Complete the prototype PTE fault -- this will release the PFN lock */
        Status = MiCompleteProtoPteFault(StoreInstruction, Address, Pte, SectionProto, OldIrql, LockedProtoPfn);
    }
    else
    {
        if (IsLocked)
            MiUnlockPfnDb(OldIrql, APC_LEVEL);

        ASSERT(KeAreAllApcsDisabled() == TRUE);
    }

    if (PageBlock)
    {
        DPRINT1("MiResolveProtoPteFault: FIXME\n");
        ASSERT(FALSE);
        //MiFreeInPageSupportBlock(PageBlock);
    }

    return Status;
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
    PMI_PAGE_SUPPORT_BLOCK PageBlock;
    PMMPFN LockedProtoPfn = NULL;
    PMMPFN PfnForPde;
    PMMPFN Pfn;
    PMMSUPPORT SessionWs = NULL;
    PMMPTE SectionProtoPte;
    MMPTE TempPte;
    MMPTE TempProto;
    MMPTE OriginalPte;
    MMPTE PteContents;
    MMPTE NewPteContents;
    MMWSLE ProtoProtect;
    PFN_NUMBER PageFrameIndex;
    PFN_COUNT ProcessedPtes;
    PFN_COUNT PteCount;
    ULONG Flags = 0;
    ULONG Index;
    ULONG ix;
    ULONG jx;
    KIRQL OldIrql;
    KIRQL LockIrql = MM_NOIRQL;
    NTSTATUS Status;

    DPRINT("MiDispatchFault: %X, %p, Pte %p [%p], Proto %p [%I64X], %X, %p, %p, %p\n",
           FaultCode, Address, Pte, Pte->u.Long, SectionProto, MiGetPteContents(SectionProto),
           Recursive, Process, TrapInformation, Vad);

    /* Make sure APCs are off and we're not at dispatch */
    OldIrql = KeGetCurrentIrql();
    ASSERT(OldIrql <= APC_LEVEL);
    ASSERT(KeAreAllApcsDisabled() == TRUE);

    OriginalPte.u.Long = -1;
    ProtoProtect.u1.Long = 0;

    /* Do we have a prototype PTE? */
    if (SectionProto)
    {
        /* This should never happen */
        ASSERT(!MI_IS_PHYSICAL_ADDRESS(SectionProto));

        /* Check if this is a kernel-mode address */
        SectionProtoPte = MiAddressToPte(SectionProto);

        if (Address >= MmSystemRangeStart)
        {
            /* Lock the PFN database */
            LockIrql = MiLockPfnDb(APC_LEVEL);

            /* Has the PTE been made valid yet? */
            if (!SectionProtoPte->u.Hard.Valid)
            {
                ASSERT((Process == NULL) || (Process == HYDRA_PROCESS));

                /* Unlock the PFN database */
                MiUnlockPfnDb(LockIrql, APC_LEVEL);

                Address = SectionProto;
                Pte = SectionProtoPte;
                SectionProto = NULL;

                if (Process == HYDRA_PROCESS)
                {
                    DPRINT1("MiDispatchFault: FIXME! Process == HYDRA_PROCESS. SectionProto %X\n", SectionProto);
                    ASSERT(FALSE);
                }

                goto OtherPteTypes;
            }
            else
            {
                if (Pte->u.Hard.Valid)
                {
                    /* Release the lock and leave*/
                    MiUnlockPfnDb(LockIrql, APC_LEVEL);
                    return STATUS_SUCCESS;
                }
            }
        }
        else
        {
            /* This is a user fault */

            PteCount = 1;

            if (Pte->u.Soft.PageFileHigh == MI_PTE_LOOKUP_NEEDED || Pte->u.Proto.ReadOnly)
            {
                /* Is there a non-image VAD? */
                if (Vad &&
                    Vad->u.VadFlags.VadType != VadImageMap &&
                    !Vad->u2.VadFlags2.ExtendableFile)
                {
                    PteCount = 8;

                    if (SectionProto >= Vad->FirstPrototypePte &&
                        SectionProto <= Vad->LastContiguousPte)
                    {
                        ix = (Vad->LastContiguousPte - SectionProto + 1);
                    }
                    else
                    {
                        DPRINT1("MiDispatchFault: FIXME! (%X:%X)\n", Vad->FirstPrototypePte, Vad->LastContiguousPte);
                        ASSERT(FALSE);
                        ix = 0;//FIXME
                    }

                    if (ix < 8)
                        PteCount = ix;

                    if (PteCount > (PAGE_SIZE - ((ULONG_PTR)Pte & (PAGE_SIZE - 1))) / 4)
                        PteCount = (PAGE_SIZE - ((ULONG_PTR)Pte & (PAGE_SIZE - 1))) / 4;

                    if (PteCount > (PAGE_SIZE - ((ULONG_PTR)SectionProto & (PAGE_SIZE - 1))) / 4)
                        PteCount = (PAGE_SIZE - ((ULONG_PTR)SectionProto & (PAGE_SIZE - 1))) / 4;

                    if (PteCount > (Vad->EndingVpn - ((ULONG_PTR)Address / PAGE_SIZE) + 1))
                        PteCount = (Vad->EndingVpn - ((ULONG_PTR)Address / PAGE_SIZE) + 1);

                    ix = 1;
                    Index = MmWorkingSetList->FirstFree;

                    if (PteCount > 1 && Index != 0x0FFFFFFF)
                    {
                        PMMWSLE MmWsle = (PMMWSLE)(MmWorkingSetList + 1);

                        do
                        {
                            if (MmWsle[Index].u1.Long == 0xFFFFFFF0)
                                break;

                            Index = (MmWsle[Index].u1.Long >> 4);

                            ix++;
                        }
                        while (ix < PteCount);
                    }

                    if (PteCount > ix)
                        PteCount = ix;

                    ASSERT(Address <= MM_HIGHEST_USER_ADDRESS);

                    for (jx = 1; jx < PteCount && !Pte[jx].u.Long; jx++)
                        MI_WRITE_INVALID_PTE(&Pte[jx], *Pte);

                    PteCount = jx;

                    if (PteCount > 1)
                    {
                        MiAddPageTableReferences(Address, (PteCount - 1));
                        ProtoProtect.u1.e1.Protection = Pte->u.Soft.Protection;
    
                        MI_MAKE_HARDWARE_PTE_USER(&PteContents, Pte, ProtoProtect.u1.e1.Protection, 0);

                        if (MI_IS_WRITE_ACCESS(FaultCode) && ((ProtoProtect.u1.e1.Protection & 5) != 5))
                        {
                            MI_MAKE_DIRTY_PAGE(&PteContents);
                            Flags |= 0x01;
                        }

                        PfnForPde = MI_PFN_ELEMENT((MiAddressToPte(Pte))->u.Hard.PageFrameNumber);
                    }
                }
            }
            else
            {
                Flags = 0x04; // FIXME
            }

            ProcessedPtes = 0;
            Flags |= 0x02;

            /* Lock the PFN database */
            LockIrql = MiLockPfnDb(APC_LEVEL);

            /* We only handle the valid path */
            if (!SectionProtoPte->u.Hard.Valid)
            {
                DPRINT1("MiDispatchFault: FIXME! SectionProtoPte %X\n", SectionProtoPte);
                ASSERT(FALSE);
            }

            /* Capture the PTE */
            TempProto = *SectionProto;

            if (Recursive)
            {
                DPRINT1("MiDispatchFault: FIXME!\n");
                ASSERT(FALSE);
            }

            if (!(Flags & 0x04))
            {
                while (TRUE)
                {
                    ULONG Protection;

                    if (TempProto.u.Hard.Valid == 1)
                    {
                        /* Bump the share count on the PTE */
                        PageFrameIndex = PFN_FROM_PTE(&TempProto);
                        Pfn = MI_PFN_ELEMENT(PageFrameIndex);
                        Pfn->u2.ShareCount++;
                    }
                    else if (!TempProto.u.Soft.Prototype && TempProto.u.Soft.Transition)
                    {
                        DPRINT1("MiDispatchFault: FIXME!\n");
                        ASSERT(FALSE);
                    }
                    else
                    {
                        DPRINT1("MiDispatchFault: FIXME!\n");
                        ASSERT(FALSE);
                    }

                    /* One more done, was it the last? */
                    ProcessedPtes++;
                    if (ProcessedPtes == PteCount)
                    {
                        /* Complete the fault */
                        MiCompleteProtoPteFault(MI_IS_WRITE_ACCESS(FaultCode),
                                                Address,
                                                Pte,
                                                SectionProto,
                                                LockIrql,
                                                &LockedProtoPfn);
                        Flags &= ~0x02;
                        break;
                    }

                    ASSERT(SectionProto->u.Hard.Valid == 1);

                    Pfn->u3.e1.PrototypePte = 1;
                    PfnForPde->u2.ShareCount++;

                    NewPteContents.u.Long = PteContents.u.Long;
                    ASSERT(NewPteContents.u.Long != 0);

                    if (Pfn->u3.e1.CacheAttribute == MiNonCached)
                    {
                        Protection = ProtoProtect.u1.e1.Protection;
                        Protection &= ~(MM_NOCACHE | MM_WRITECOMBINE);
                        Protection |= MM_NOCACHE;
                        NewPteContents.u.Long = 0;
                    }
                    else if (Pfn->u3.e1.CacheAttribute == MiWriteCombined)
                    {
                        Protection = ProtoProtect.u1.e1.Protection;
                        Protection &= ~(MM_NOCACHE | MM_WRITECOMBINE);
                        Protection |= MM_WRITECOMBINE;
                        NewPteContents.u.Long = 0;
                    }

                    if (!NewPteContents.u.Long)
                    {
                        MI_MAKE_HARDWARE_PTE_USER(&NewPteContents, Pte, Protection, 0);//u.Hard.Accessed = 1

                        if (MI_IS_WRITE_ACCESS(FaultCode) && ((Protection & 5) != 5))
                            MI_MAKE_DIRTY_PAGE(&NewPteContents);
                    }

                    NewPteContents.u.Hard.PageFrameNumber = PageFrameIndex;

                    if (Flags & 0x01)
                    {
                        OriginalPte = Pfn->OriginalPte;

                        ASSERT(Pfn->u3.e1.Rom == 0);
                        Pfn->u3.e1.Modified = 1;

                        if (!OriginalPte.u.Soft.Prototype && !Pfn->u3.e1.WriteInProgress)
                        {
                            ULONG PageFileHigh = OriginalPte.u.Soft.PageFileHigh;

                            if (PageFileHigh && PageFileHigh != MI_PTE_LOOKUP_NEEDED)
                            {
                                DPRINT1("MiCompleteProtoPteFault: FIXME. PageFileHigh %X\n", PageFileHigh);
                                ASSERT(FALSE);
                            }

                            Pfn->OriginalPte.u.Soft.PageFileHigh = 0;
                        }
                    }

                    ASSERT(Pte == MiAddressToPte(Address));

                    MI_WRITE_VALID_PTE(Pte, NewPteContents);

                    SectionProto++;
                    TempProto = *SectionProto;

                    Pte++;
                    Address = (PVOID)((ULONG_PTR)Address + PAGE_SIZE);
                }
            }

            /* Did we resolve the fault? */
            if (ProcessedPtes)
            {
                if (Flags & 0x02)
                {
                    MiUnlockPfnDb(LockIrql, APC_LEVEL);
                    InterlockedExchangeAddSizeT(&KeGetCurrentPrcb()->MmTransitionCount, ProcessedPtes);
                }
                else
                {
                    /* Bump the transition count */
                    InterlockedExchangeAddSizeT(&KeGetCurrentPrcb()->MmTransitionCount, ProcessedPtes);
                    ProcessedPtes--;

                }

                while (ProcessedPtes)
                {
                    SectionProto--;
                    Pte--;
                    Address = (PVOID)((ULONG_PTR)Address - PAGE_SIZE);
                    ProcessedPtes--;

                    PageFrameIndex = PFN_FROM_PTE(Pte);
                    Pfn = MI_PFN_ELEMENT(PageFrameIndex);

                    ASSERT(ProtoProtect.u1.e1.Protection != MM_ZERO_ACCESS);
                    ASSERT(MI_IS_PAGE_TABLE_ADDRESS(Pte));
                    ASSERT(Pte->u.Hard.Valid == 1);

                    DPRINT("MiDispatchFault: FIXME MiAllocateWsle(). ProcessedPtes %X\n", ProcessedPtes);

                    if (Pfn->OriginalPte.u.Soft.Prototype)
                    {
                        DPRINT("MiDispatchFault: FIXME CcPfNumActiveTraces\n");
                        //ASSERT(FALSE);
                    }
                }

                ASSERT(OldIrql == KeGetCurrentIrql());
                ASSERT(OldIrql <= APC_LEVEL);
                ASSERT(KeAreAllApcsDisabled() == TRUE);

                DPRINT("MiDispatchFault: return STATUS_PAGE_FAULT_TRANSITION\n");
                return STATUS_PAGE_FAULT_TRANSITION;
            }

            ASSERT(Flags & 0x02);

            /* We did not -- PFN lock is still held, prepare to resolve prototype PTE fault */
            LockedProtoPfn = MI_PFN_ELEMENT(SectionProtoPte->u.Hard.PageFrameNumber);
            MiReferenceUsedPageAndBumpLockCount(LockedProtoPfn);

            ASSERT(LockedProtoPfn->u3.e2.ReferenceCount > 1);
            ASSERT(Pte->u.Hard.Valid == 0);
        }

        /* Resolve the fault -- this will release the PFN lock */
        Status = MiResolveProtoPteFault(MI_IS_WRITE_ACCESS(FaultCode),
                                        Address,
                                        Pte,
                                        SectionProto,
                                        &LockedProtoPfn,
                                        &PageBlock,
                                        &OriginalPte,
                                        Process,
                                        LockIrql,
                                        TrapInformation);

        ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
        goto Finish;
    }

OtherPteTypes:

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

Finish:

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
    PEPROCESS CurrentProcess;
    PEPROCESS Process;
    PMMSUPPORT WorkingSet;
    PMMVAD Vad = NULL;
    PMMPFN Pfn;
    MMPTE TempPte;
    PFN_NUMBER PageFrameIndex;
    ULONG ProtectionCode;
    ULONG PagesCount;
    ULONG Color;
    BOOLEAN IsSessionAddress;
    BOOLEAN IsForceIrpComplete = FALSE;
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

    CurrentThread = PsGetCurrentThread();
    CurrentProcess = (PEPROCESS)CurrentThread->Tcb.ApcState.Process;

    if (MmAvailablePages < 0x100)
    {
        DPRINT1("MmAccessFault: FIXME. MmAvailablePages %X\n", MmAvailablePages);
        ASSERT(FALSE);
    }

    /* Lock the working set */
    MiLockProcessWorkingSet(CurrentProcess, CurrentThread);

    /* Check if the PDE is invalid */
    if (!Pde->u.Hard.Valid)
    {
        /* Right now, we only handle scenarios where the PDE is totally empty */
        if (!Pde->u.Long)
        {
            ULONG protectionCode;

            MiCheckVirtualAddress(Address, &protectionCode, &Vad);

            if (protectionCode == MM_NOACCESS)
            {
                DPRINT1("MmAccessFault: FIXME\n");
                ASSERT(FALSE);
                goto Exit2;
            }

            MI_WRITE_INVALID_PTE(Pde, DemandZeroPde);
        }

        /* Dispatch the fault */
        Status = MiDispatchFault(1,
                                 Pte,
                                 Pde,
                                 NULL,
                                 FALSE,
                                 CurrentProcess,
                                 TrapInformation,
                                 NULL);

        ASSERT(KeAreAllApcsDisabled() == TRUE);

        if (!Pde->u.Hard.Valid)
            goto Exit3;

        if (Pde->u.Hard.Dirty)
            MiSetDirtyBit(Pte, Pde, FALSE);
    }
    else if (MI_IS_PAGE_LARGE(Pde))
    {
        DPRINT1("MmAccessFault: FIXME\n");
        ASSERT(FALSE);
        Status = STATUS_SUCCESS;
        goto Exit3;
    }

    TempPte = *Pte;

    if (TempPte.u.Hard.Valid)
    {
        DPRINT1("MmAccessFault: FIXME\n");
        ASSERT(FALSE);
        goto Exit2;
    }

    if (TempPte.u.Long == (MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS))
    {
        DPRINT1("MmAccessFault: FIXME\n");
        ASSERT(FALSE);
        goto Exit3;
    }

    if (!TempPte.u.Long)
    {
        /* Check if this address range belongs to a valid allocation (VAD) */
        SectionProto = MiCheckVirtualAddress(Address, &ProtectionCode, &Vad);

        if (ProtectionCode == MM_NOACCESS)
        {
            Status = STATUS_ACCESS_VIOLATION;

            /* Could be a page table for paged pool */
            MiCheckPdeForPagedPool(Address);

            /* Has the code above changed anything -- is this now a valid PTE? */
            if (Pte->u.Hard.Valid)
                Status = STATUS_SUCCESS;

            if (Status == STATUS_ACCESS_VIOLATION)
            {
                DPRINT1("MmAccessFault: STATUS_ACCESS_VIOLATION\n");
            }

            goto Exit2;
        }

        if ((ProtectionCode & MM_PROTECT_SPECIAL) == MM_GUARDPAGE)
        {
            if (KeInvalidAccessAllowed(TrapInformation))
            {
                DPRINT("MmAccessFault: STATUS_ACCESS_VIOLATION\n");
                Status = STATUS_ACCESS_VIOLATION;
                goto Exit2;
            }
        }

        /* Check if this is a real user-mode address or actually a kernel-mode page table for a user mode address */
        if (Address <= MM_HIGHEST_USER_ADDRESS)
            /* Add an additional page table reference */
            MiIncrementPageTableReferences(Address);

        /* Is this a guard page? */
        if ((ProtectionCode & MM_PROTECT_SPECIAL) == MM_GUARDPAGE)
        {
            /* Remove the bit */
            Pte->u.Soft.Protection = (ProtectionCode & ~0x10);

            if (SectionProto)
            {
                Pte->u.Soft.PageFileHigh = MI_PTE_LOOKUP_NEEDED;
                Pte->u.Soft.Prototype = 1;
            }

            if (CurrentThread->ApcNeeded && !CurrentThread->ActiveFaultCount)
            {
                CurrentThread->ApcNeeded = 0;
                IsForceIrpComplete = TRUE;
            }

            /* Drop the working set lock */
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            ASSERT(KeGetCurrentIrql() == OldIrql);

            if (IsForceIrpComplete)
            {
                ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
                DPRINT1("MmAccessFault: FIXME IoRetryIrpCompletions()\n");
                ASSERT(FALSE);
            }

            /* Handle stack expansion */
            Status = MiCheckForUserStackOverflow(Address, TrapInformation);
            return Status;
        }

        /* Did we get a prototype PTE back? */
        if (!SectionProto)
        {
            /* Is this PTE actually part of the PDE-PTE self-mapping directory? */
            if (Pde == MiAddressToPde(PTE_BASE))
            {
                /* Then it's really a demand-zero PDE (on behalf of user-mode) */
                MI_WRITE_INVALID_PTE(Pte, DemandZeroPde);
                DPRINT1("MmAccessFault: Pte->u.Long %X\n", Pte->u.Long);
            }
            else
            {
                /* No, create a new PTE. First, write the protection */
                TempPte.u.Soft.Protection = ProtectionCode;
                MI_WRITE_INVALID_PTE(Pte, TempPte);
                DPRINT1("MmAccessFault: Pte->u.Long %X\n", Pte->u.Long);
            }

            /* Lock the PFN database since we're going to grab a page */
            PfnLockIrql = MiLockPfnDb(APC_LEVEL);

            if (MmAvailablePages < 0x80)
            {
                DPRINT1("MmAccessFault: FIXME MiEnsureAvailablePageOrWait()\n");
                ASSERT(FALSE);
            }

            /* Make sure we have enough pages */
            if (MmAvailablePages >= 0x80)
            {
                /* Try to get a zero page */
                MI_SET_USAGE(MI_USAGE_PEB_TEB);
                MI_SET_PROCESS2(CurrentProcess->ImageFileName);

                Color = MI_GET_NEXT_PROCESS_COLOR(CurrentProcess);

                PageFrameIndex = MiRemoveZeroPageSafe(Color);
                if (!PageFrameIndex)
                {
                    /* Grab a page out of there. Later we should grab a colored zero page */
                    PageFrameIndex = MiRemoveAnyPage(Color);
                    ASSERT(PageFrameIndex);

                    /* Release the lock since we need to do some zeroing */
                    MiUnlockPfnDb(PfnLockIrql, APC_LEVEL);

                    /* Zero out the page, since it's for user-mode */
                    MiZeroPfn(PageFrameIndex);

                    /* Grab the lock again so we can initialize the PFN entry */
                    PfnLockIrql = MiLockPfnDb(APC_LEVEL);
                }

                /* Initialize the PFN entry now */
                MiInitializePfn(PageFrameIndex, Pte, 1);

                /* Increment the count of pages in the process */
                CurrentProcess->NumberOfPrivatePages++;

                /* One more demand-zero fault */
                InterlockedIncrement(&KeGetCurrentPrcb()->MmDemandZeroCount);

                /* And we're done with the lock */
                MiUnlockPfnDb(PfnLockIrql, APC_LEVEL);

                /* Fault on user PDE, or fault on user PTE? */
                if (Pte <= MiHighestUserPte)
                    /* User fault, build a user PTE */
                    MI_MAKE_HARDWARE_PTE_USER(&TempPte, Pte, Pte->u.Soft.Protection, PageFrameIndex);
                else
                    /* This is a user-mode PDE, create a kernel PTE for it */
                    MI_MAKE_HARDWARE_PTE(&TempPte, Pte, Pte->u.Soft.Protection, PageFrameIndex);

                /* Write the dirty bit for writeable pages */
                if (MI_IS_PAGE_WRITEABLE(&TempPte))
                    MI_MAKE_DIRTY_PAGE(&TempPte);

                /* And now write down the PTE, making the address valid */
                MI_WRITE_VALID_PTE(Pte, TempPte);
                Pfn = MI_PFN_ELEMENT(PageFrameIndex);
                ASSERT(Pfn->u1.Event == NULL);

                DPRINT1("MmAccessFault: FIXME MiAllocateWsle()\n");
            }
            else
            {
                MiUnlockPfnDb(PfnLockIrql, APC_LEVEL);
            }

            DPRINT("MmAccessFault: return STATUS_PAGE_FAULT_DEMAND_ZERO\n");
            Status = STATUS_PAGE_FAULT_DEMAND_ZERO;
            goto Exit3;
        }

        if (ProtectionCode == 0x100)
        {
            MI_MAKE_PROTOTYPE_PTE(&TempPte, SectionProto);
        }
        else
        {
            /* Write the prototype PTE */
            TempPte = PrototypePte;
            TempPte.u.Soft.Protection = ProtectionCode;
        }

        /* Write the prototype PTE */
        ASSERT(TempPte.u.Long != 0);
        MI_WRITE_INVALID_PTE(Pte, TempPte);
    }
    else
    {
        DPRINT1("MmAccessFault: FIXME\n");
        ASSERT(FALSE);
    }

    /* Do we have a valid protection code? */
    if (ProtectionCode != 0x100)
    {
        /* Run a software access check first, including to detect guard pages */
        Status = MiAccessCheck(Pte,
                               MI_IS_WRITE_ACCESS(FaultCode),
                               Mode,
                               ProtectionCode,
                               TrapInformation,
                               FALSE);

        if (Status != STATUS_SUCCESS)
        {
            if (Status == STATUS_ACCESS_VIOLATION)
            {
                DPRINT1("MmAccessFault: STATUS_ACCESS_VIOLATION\n");
                ASSERT(FALSE);
            }

            /* Not supported */
            if (CurrentThread->ApcNeeded && !CurrentThread->ActiveFaultCount)
            {
                DPRINT1("MmAccessFault: FIXME\n");
                ASSERT(FALSE);
            }

            /* Drop the working set lock */
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            ASSERT(KeGetCurrentIrql() == OldIrql);

            /* Did we hit a guard page? */
            if (Status == STATUS_GUARD_PAGE_VIOLATION)
            {
                /* Handle stack expansion */
                Status = MiCheckForUserStackOverflow(Address, TrapInformation);
                DPRINT("MmAccessFault: return %X\n", Status);
                return Status;
            }

            /* Otherwise, fail back to the caller directly */
            DPRINT("MmAccessFault: return %X\n", Status);
            return Status;
        }
    }

    Status = MiDispatchFault(FaultCode,
                             Address,
                             Pte,
                             SectionProto,
                             FALSE,
                             CurrentProcess,
                             TrapInformation,
                             Vad);
Exit3:

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    if (CurrentProcess->Vm.Flags.GrowWsleHash)
    {
        DPRINT1("MmAccessFault: FIXME\n");
        ASSERT(FALSE);
    }

Exit2:

    PagesCount = (CurrentProcess->Vm.WorkingSetSize - CurrentProcess->Vm.MinimumWorkingSetSize);

    if (CurrentThread->ApcNeeded)
    {
        ASSERT(CurrentThread->ApcNeeded == 0);
    }

    MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);

    ASSERT(KeGetCurrentIrql() == OldIrql);

    if (MmAvailablePages < 0x400 &&
        PagesCount > 0x64 &&
        KeGetCurrentThread()->Priority >= 0x10)
    {
        DPRINT1("MmAccessFault: FIXME\n");
        ASSERT(FALSE);
    }

Exit1:

    if (Status != STATUS_SUCCESS)
    {
        DPRINT("MmAccessFault: Status %X\n", Status);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MmAccessFault: FIXME MmIsRetryIoStatus(). Status %X\n", Status);
            ASSERT(FALSE);
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
