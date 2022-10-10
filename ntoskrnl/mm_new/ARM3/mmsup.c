
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

extern MM_SYSTEMSIZE MmSystemSize;
extern ULONG MmProductType;

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
MiIsAddressValid(
    _In_ PVOID Address)
{
    PMMPDE Pde;
    PMMPTE Pte;

    Pde = MiAddressToPde(Address);

    if (!Pde->u.Hard.Valid)
    {
        DPRINT("MiIsAddressValid: Address %p not valid\n", Address);
        return FALSE;
    }

    if (Pde->u.Hard.LargePage)
    {
        DPRINT1("MiIsAddressValid: Address %p valid\n", Address);
        return TRUE;
    }

    Pte = MiAddressToPte(Address);

    if (!Pte->u.Hard.Valid)
    {
        DPRINT("MiIsAddressValid: Address %p not valid\n", Address);
        return FALSE;
    }

    if (!Pte->u.Hard.LargePage)
    {
        DPRINT("MiIsAddressValid: Address %p not valid\n", Address);
        return FALSE;
    }

    DPRINT1("MiIsAddressValid: Address %p valid\n", Address);
    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmAdjustWorkingSetSize(
    _In_ SIZE_T WorkingSetMinimumInBytes,
    _In_ SIZE_T WorkingSetMaximumInBytes,
    _In_ ULONG SystemCache,
    _In_ BOOLEAN IncreaseOkay)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmCreateMirror(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;    
}

BOOLEAN
NTAPI
MmIsAddressValid(
    _In_ PVOID VirtualAddress)
{
  #if _MI_PAGING_LEVELS >= 3
    #error FIXME
  #endif

  #if _MI_PAGING_LEVELS >= 2
    /* Check if the PDE is valid */
    if (!MiAddressToPde(VirtualAddress)->u.Hard.Valid)
        return FALSE;
  #endif

    /* Check if the PTE is valid */
    if (!MiAddressToPte(VirtualAddress)->u.Hard.Valid)
        return FALSE;

    /* This address is valid now, but it will only stay so if the caller holds the PFN lock */
    return TRUE;
}

BOOLEAN
NTAPI
MmIsNonPagedSystemAddressValid(
    _In_ PVOID VirtualAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
MmIsRecursiveIoFault(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
MmIsThisAnNtAsSystem(VOID)
{
    /* Return if this is a server system */
    return (MmProductType & 0xFF);
}

NTSTATUS
NTAPI
MmMapUserAddressesToPage(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ PVOID PageAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;    
}

MM_SYSTEMSIZE
NTAPI
MmQuerySystemSize(VOID)
{
    /* Return the low, medium or high memory system type */
    return MmSystemSize;
}

BOOLEAN
NTAPI
MmSetAddressRangeModified(
    _In_ PVOID Address,
    _In_ SIZE_T Length)
{
    PMMPFN Pfn;
    PMMPTE Pte;
    PMMPTE LastPte;
    MMPTE TempPte;
    ULONG Number = 0;
    KIRQL OldIrql;
    BOOLEAN Result = FALSE;

    DPRINT("MmSetAddressRangeModified: Address %p, Length %p\n", Address, Length);

    Pte = MiAddressToPte(Address);
    LastPte = MiAddressToPte(Add2Ptr(Address, (Length - 1)));

    OldIrql = MiLockPfnDb(APC_LEVEL);

    do
    {
        TempPte.u.Long = Pte->u.Long;

        if (!TempPte.u.Hard.Valid)
            goto Next;

        Pfn = MI_PFN_ELEMENT(TempPte.u.Hard.PageFrameNumber);

        ASSERT(Pfn->u3.e1.Rom == 0);
        Pfn->u3.e2.ShortFlags |= 1;

        if (!Pfn->OriginalPte.u.Soft.Prototype &&
            !Pfn->u3.e1.WriteInProgress)
        {
            DPRINT1("MmSetAddressRangeModified: FIXME MiReleasePageFileSpace()\n");
            ASSERT(FALSE);
        }

        if (MI_IS_PAGE_DIRTY(&TempPte))
            Result = TRUE;

        MI_MAKE_CLEAN_PAGE(&TempPte);

        ASSERT(Pte->u.Hard.Valid == 1);
        ASSERT(TempPte.u.Hard.Valid == 1);
        ASSERT(Pte->u.Hard.PageFrameNumber == TempPte.u.Hard.PageFrameNumber);

        Pte->u.Long = TempPte.u.Long;

        if (Number != 0x21)
        {
            Number++;
            //FIXME: FlushArray
        }
Next:
        Address = Add2Ptr(Address, PAGE_SIZE);
        Pte++;
    }
    while (Pte <= LastPte);

    if (Number == 0)
    {
        ;
    }
    else if (Number == 1)
    {
        /* Flush the TLB */
        //FIXME: Use KeFlushSingleTb() instead
        KeFlushEntireTb(TRUE, TRUE);
    }
    else if (Number == 0x21)
    {
        //FIXME: MiFlushType
        //FIXME: KxFlushEntireTb();
        KeFlushEntireTb(TRUE, TRUE);
    }
    else
    {
        //FIXME: KeFlushMultipleTb(Number, FlushArray, 1);
        KeFlushEntireTb(TRUE, TRUE);
    }

    MiUnlockPfnDb(OldIrql, APC_LEVEL);

    return Result;
}

NTSTATUS
NTAPI
MmSetBankedSection(    
    _In_ HANDLE ProcessHandle,
    _In_ PVOID VirtualAddress,
    _In_ ULONG BankLength,
    _In_ BOOLEAN ReadWriteBank,
    _In_ PVOID BankRoutine,
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;    
}

/* EOF */
