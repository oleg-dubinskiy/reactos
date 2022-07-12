
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

PMMPTE MmSystemPteBase;
PMMPTE MmSystemPtesStart[MaximumPtePoolTypes];
PMMPTE MmSystemPtesEnd[MaximumPtePoolTypes];
MMPTE MmFirstFreeSystemPte[MaximumPtePoolTypes];
ULONG MmTotalFreeSystemPtes[MaximumPtePoolTypes];
ULONG MmTotalSystemPtes;

/* FUNCTIONS ******************************************************************/

PMMPTE
NTAPI
MiReserveSystemPtes(
    _In_ ULONG NumberOfPtes,
    _In_ MMSYSTEM_PTE_POOL_TYPE SystemPtePoolType)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

INIT_FUNCTION
VOID
NTAPI
MiInitializeSystemPtes(
    _In_ PMMPTE StartingPte,
    _In_ ULONG NumberOfPtes,
    _In_ MMSYSTEM_PTE_POOL_TYPE PoolType)
{
    /* Sanity checks */
    ASSERT(NumberOfPtes >= 1);

    /* Set the starting and ending PTE addresses for this space */
    MmSystemPteBase = MI_SYSTEM_PTE_BASE;

    MmSystemPtesStart[PoolType] = StartingPte;
    MmSystemPtesEnd[PoolType] = (StartingPte + NumberOfPtes - 1);

    DPRINT("MiInitializeSystemPtes: System PTE space for %d (%p - %p)\n",
           PoolType, MmSystemPtesStart[PoolType], MmSystemPtesEnd[PoolType]);

    /* Clear all the PTEs to start with */
    RtlZeroMemory(StartingPte, NumberOfPtes * sizeof(MMPTE));

    /* Make the first entry free and link it */
    StartingPte->u.List.NextEntry = MM_EMPTY_PTE_LIST;
    MmFirstFreeSystemPte[PoolType].u.Long = 0;
    MmFirstFreeSystemPte[PoolType].u.List.NextEntry = (StartingPte - MmSystemPteBase);

    /* The second entry stores the size of this PTE space */
    StartingPte++;
    StartingPte->u.Long = 0;
    StartingPte->u.List.NextEntry = NumberOfPtes;

    /* We also keep a global for it */
    MmTotalFreeSystemPtes[PoolType] = NumberOfPtes;

    /* Check if this is the system PTE space */
    if (PoolType == SystemPteSpace)
        /* Remember how many PTEs we have */
        MmTotalSystemPtes = NumberOfPtes;
}

/* EOF */
