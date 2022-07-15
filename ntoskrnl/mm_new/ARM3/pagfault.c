
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

extern PMMPTE MiSessionLastPte;
extern PMM_SESSION_SPACE MmSessionSpace;
#if (_MI_PAGING_LEVELS <= 3)
  extern PMMPDE MmSystemPagePtes;
#endif

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

#else
  #error FIXME
#endif

/* EOF */
