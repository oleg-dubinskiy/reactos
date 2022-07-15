
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

FORCEINLINE
ULONG
MI_GET_CLUSTER_SIZE(
    _In_ PMMPTE Pte)
{
    /* First check for a single PTE */
    if (Pte->u.List.OneEntry)
        return 1;

    /* Then read the size from the trailing PTE */
    Pte++;

    return ((ULONG)Pte->u.List.NextEntry);
}

PMMPTE
NTAPI
MiReserveAlignedSystemPtes(
    _In_ ULONG NumberOfPtes,
    _In_ MMSYSTEM_PTE_POOL_TYPE SystemPtePoolType,
    _In_ ULONG Alignment)
{
    PMMPTE PreviousPte;
    PMMPTE NextPte;
    PMMPTE ReturnPte;
    ULONG ClusterSize;
    KIRQL OldIrql;

    /* Sanity check */
    ASSERT(Alignment <= PAGE_SIZE);

    /* Acquire the System PTE lock */
    OldIrql = KeAcquireQueuedSpinLock(LockQueueSystemSpaceLock);

    /* Find the last cluster in the list that doesn't contain enough PTEs */
    PreviousPte = &MmFirstFreeSystemPte[SystemPtePoolType];

    while (PreviousPte->u.List.NextEntry != MM_EMPTY_PTE_LIST)
    {
        /* Get the next cluster and its size */
        NextPte = (MmSystemPteBase + PreviousPte->u.List.NextEntry);
        ClusterSize = MI_GET_CLUSTER_SIZE(NextPte);

        /* Check if this cluster contains enough PTEs */
        if (NumberOfPtes <= ClusterSize)
            break;

        /* On to the next cluster */
        PreviousPte = NextPte;
    }

    /* Make sure we didn't reach the end of the cluster list */
    if (PreviousPte->u.List.NextEntry == MM_EMPTY_PTE_LIST)
    {
        /* Release the System PTE lock and return failure */
        KeReleaseQueuedSpinLock(LockQueueSystemSpaceLock, OldIrql);
        return NULL;
    }

    /* Unlink the cluster */
    PreviousPte->u.List.NextEntry = NextPte->u.List.NextEntry;

    /* Check if the reservation spans the whole cluster */
    if (ClusterSize == NumberOfPtes)
    {
        /* Return the first PTE of this cluster */
        ReturnPte = NextPte;

        /* Zero the cluster */
        if (NextPte->u.List.OneEntry == 0)
        {
            NextPte->u.Long = 0;
            NextPte++;
        }

        NextPte->u.Long = 0;
    }
    else
    {
        /* Divide the cluster into two parts */
        ClusterSize -= NumberOfPtes;
        ReturnPte = (NextPte + ClusterSize);

        /* Set the size of the first cluster, zero the second if needed */
        if (ClusterSize == 1)
        {
            NextPte->u.List.OneEntry = 1;
            ReturnPte->u.Long = 0;
        }
        else
        {
            NextPte++;
            NextPte->u.List.NextEntry = ClusterSize;
        }

        /* Step through the cluster list to find out where to insert the first */
        PreviousPte = &MmFirstFreeSystemPte[SystemPtePoolType];

        while (PreviousPte->u.List.NextEntry != MM_EMPTY_PTE_LIST)
        {
            /* Get the next cluster */
            NextPte = (MmSystemPteBase + PreviousPte->u.List.NextEntry);

            /* Check if the cluster to insert is smaller or of equal size */
            if (ClusterSize <= MI_GET_CLUSTER_SIZE(NextPte))
                break;

            /* On to the next cluster */
            PreviousPte = NextPte;
        }

        /* Retrieve the first cluster and link it back into the cluster list */
        NextPte = (ReturnPte - ClusterSize);

        NextPte->u.List.NextEntry = PreviousPte->u.List.NextEntry;
        PreviousPte->u.List.NextEntry = (NextPte - MmSystemPteBase);
    }

    /* Decrease availability */
    MmTotalFreeSystemPtes[SystemPtePoolType] -= NumberOfPtes;

    /* Release the System PTE lock */
    KeReleaseQueuedSpinLock(LockQueueSystemSpaceLock, OldIrql);

    /* Flush the TLB */
    KeFlushProcessTb();

    /* Return the reserved PTEs */
    return ReturnPte;
}

PMMPTE
NTAPI
MiReserveSystemPtes(
    _In_ ULONG NumberOfPtes,
    _In_ MMSYSTEM_PTE_POOL_TYPE SystemPtePoolType)
{
    PMMPTE Pte;

    /* Use the extended function */
    Pte = MiReserveAlignedSystemPtes(NumberOfPtes, SystemPtePoolType, 0);
    if (Pte)
        return Pte;

    DPRINT1("MiReserveSystemPtes: Failed to reserve %X PTE(s)!\n", NumberOfPtes);
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
