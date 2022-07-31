
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* Include Mm version of AVL support */
#include "miavl.h"
#include <sdk/lib/rtl/avlsupp.c>

/* GLOBALS ********************************************************************/

#define MAXINDEX 0xFFFFFFFF

extern ULONG MiLastVadBit;

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
MiCheckForConflictingVadExistence(
    _In_ PEPROCESS Process,
    _In_ ULONG_PTR StartingAddress,
    _In_ ULONG_PTR EndingAddress)
{
    PMMADDRESS_NODE Node;
    ULONG_PTR StartVpn;
    ULONG_PTR EndVpn;

    StartVpn = (StartingAddress / PAGE_SIZE);
    EndVpn = (EndingAddress / PAGE_SIZE);

    DPRINT("MiCheckForConflictingVadExistence: Process %p, Start %p, End %p, (%X:%X)Vpn\n",
           Process, StartingAddress, EndingAddress, StartVpn, EndVpn);

    /* If the tree is empty, there is no conflict */
    if (!Process->VadRoot.NumberGenericTableElements)
        return FALSE;

    /* Start looping from the root node */
    Node = Process->VadRoot.BalancedRoot.RightChild;
    if (!Node)
    {
        /* If the tree is not empty, this should not be */
        ASSERT(Node != NULL);
        return FALSE;
    }

    while (Node)
    {
        DPRINT("MiCheckForConflictingVadExistence: Node %p, StartingVpn %p, EndingVpn %p\n",
               Node, Node->StartingVpn, Node->EndingVpn);

        /* This address comes after */
        if (StartVpn > Node->EndingVpn)
        {
            Node = Node->RightChild;
        }
        else if (EndVpn < Node->StartingVpn)
        {
            /* This address ends before the node starts, search on the left */
            Node = Node->LeftChild;
        }
        else
        {
            DPRINT("MiCheckForConflictingVadExistence: This address is part of this Node\n");
            return TRUE;
        }
    }

    /* There is no more child */
    return FALSE;
}

PMMADDRESS_NODE
NTAPI
MiGetNextNode(
    _In_ PMMADDRESS_NODE Node)
{
    PMMADDRESS_NODE Parent;

    /* Get the right child */
    if (RtlRightChildAvl(Node))
    {
        /* Get left-most child */
        Node = RtlRightChildAvl(Node);

        while (RtlLeftChildAvl(Node))
            Node = RtlLeftChildAvl(Node);

        return Node;
    }

    Parent = RtlParentAvl(Node);
    ASSERT(Parent != NULL);

    while (Parent != Node)
    {
        /* The parent should be a left child, return the real predecessor */
        if (RtlIsLeftChildAvl(Node))
            /* Return it */
            return Parent;

        /* Keep lopping until we find our parent */
        Node = Parent;
        Parent = RtlParentAvl(Node);
    }

    /* Nothing found */
    return NULL;
}

TABLE_SEARCH_RESULT
NTAPI
MiFindEmptyAddressRangeInTree(
    _In_ SIZE_T Length,
    _In_ ULONG_PTR Alignment,
    _In_ PMM_AVL_TABLE Table,
    _Out_ PMMADDRESS_NODE* PreviousVad,
    _Out_ PULONG_PTR Base)
{
    PMMADDRESS_NODE Node;
    PMMADDRESS_NODE PreviousNode;
    ULONG_PTR PageCount;
    ULONG_PTR AlignmentVpn;
    ULONG_PTR LowVpn;
    ULONG_PTR HighestVpn;

    ASSERT(Length != 0);
    ASSERT(FALSE);

    /* Calculate page numbers for the length, alignment, and starting address */
    PageCount = BYTES_TO_PAGES(Length);
    AlignmentVpn = (Alignment / PAGE_SIZE);
    LowVpn = ALIGN_UP_BY(((ULONG_PTR)MM_LOWEST_USER_ADDRESS / PAGE_SIZE), AlignmentVpn);

    /* Check for kernel mode table (memory areas) */
    if (Table->Unused)
        LowVpn = ALIGN_UP_BY(((ULONG_PTR)MmSystemRangeStart / PAGE_SIZE), AlignmentVpn);

    /* Check if the table is empty */
    if (!Table->NumberGenericTableElements)
    {
        /* Tree is empty, the candidate address is already the best one */
        *Base = (LowVpn * PAGE_SIZE);
        return TableEmptyTree;
    }

    /* Otherwise, follow the leftmost child of the right root node's child */
    Node = RtlRightChildAvl(&Table->BalancedRoot);

    while (RtlLeftChildAvl(Node))
        Node = RtlLeftChildAvl(Node);

    /* Start a search to find a gap */
    PreviousNode = NULL;

    while (Node)
    {
        /* Check if the gap below the current node is suitable */
        if (Node->StartingVpn >= (LowVpn + PageCount))
        {
            /* There is enough space to add our node */
            *Base = (LowVpn * PAGE_SIZE);

            /* Can we use the current node as parent? */
            if (!RtlLeftChildAvl(Node))
            {
                /* Node has no left child, so use it as parent */
                *PreviousVad = Node;
                return TableInsertAsLeft;
            }

            /* Node has a left child, this means that the previous node is the right-most child
               of it's left child and can be used as the parent.
               In case we use the space before the left-most node, it's left child must be NULL.
            */
            ASSERT(PreviousNode != NULL);
            ASSERT(RtlRightChildAvl(PreviousNode) == NULL);

            *PreviousVad = PreviousNode;
            return TableInsertAsRight;
        }

        /* The next candidate is above the current node */
        if (Node->EndingVpn >= LowVpn)
            LowVpn = ALIGN_UP_BY((Node->EndingVpn + 1), AlignmentVpn);

        /* Remember the current node and go to the next node */
        PreviousNode = Node;
        Node = MiGetNextNode(Node);
    }

    /* We're up to the highest VAD, will this allocation fit above it? */
    HighestVpn = (((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1) / PAGE_SIZE);

    /* Check for kernel mode table (memory areas) */
    if (Table->Unused)
        HighestVpn = ALIGN_UP_BY(((ULONG_PTR)(LONG_PTR)-1 / PAGE_SIZE), AlignmentVpn);

    if (HighestVpn >= (LowVpn + PageCount))
    {
        /* Yes! Use this VAD to store the allocation */
        *PreviousVad = PreviousNode;
        *Base = (LowVpn * PAGE_SIZE);

        return TableInsertAsRight;
    }

    /* Nyet, there's no free address space for this allocation, so we'll fail */
    return TableFoundNode;
}

NTSTATUS
NTAPI
MiFindEmptyAddressRange(
    _In_ SIZE_T Size,
    _In_ ULONG_PTR Alignment,
    _In_ ULONG ZeroBits,
    _Out_ PULONG_PTR OutBaseAddress)
{
    PEPROCESS Process = PsGetCurrentProcess();
    ULONG StartVadBitmapValue;
    ULONG StartBitIndex;
    RTL_BITMAP BitMapHeader;

    DPRINT("MiFindEmptyAddressRange: Size %X, Alignment %X, ZeroBits %X\n", Size, Alignment, ZeroBits);

    if (ZeroBits || Alignment != MM_ALLOCATION_GRANULARITY)
    {
        DPRINT1("MiFindEmptyAddressRange: goto FindInTree\n");
        goto FindInTree;
    }

    BitMapHeader.SizeOfBitMap = (MiLastVadBit + 1);
    BitMapHeader.Buffer = MI_VAD_BITMAP;

    /* Mask null bit */
    StartVadBitmapValue = *MI_VAD_BITMAP;
    *(PULONG)MI_VAD_BITMAP |= 1;

    /* Get starting bit index for a clear bit range of at least the requested size */
    StartBitIndex = RtlFindClearBits(&BitMapHeader,
                                     ((Size + (MM_ALLOCATION_GRANULARITY - 1)) / MM_ALLOCATION_GRANULARITY),
                                     MmWorkingSetList->VadBitMapHint);
    /* Unmask null bit */
    *(PULONG)MI_VAD_BITMAP &= (StartVadBitmapValue | (~1));

    if (StartBitIndex != MAXINDEX)
    {
        *OutBaseAddress = ((ULONG_PTR)StartBitIndex * MM_ALLOCATION_GRANULARITY);
        DPRINT("MiFindEmptyAddressRange: StartBitIndex %X, BaseAddress %X\n", StartBitIndex, *OutBaseAddress);
        return STATUS_SUCCESS;
    }

    /* Cannot find a range within the MI_VAD_BITMAP */

    if (!Process->VadFreeHint)
    {
        DPRINT1("MiFindEmptyAddressRange: VadFreeHint == NULL\n");
        goto FindInTree;
    }

    DPRINT1("MiFindEmptyAddressRange: FIXME!\n");
    ASSERT(FALSE);

FindInTree:

    return MiFindEmptyAddressRangeInTree(Size,
                                         Alignment,
                                         &Process->VadRoot,
                                         (PMMADDRESS_NODE *)&Process->VadFreeHint,
                                         OutBaseAddress);
}

TABLE_SEARCH_RESULT
NTAPI
MiFindEmptyAddressRangeDownTree(
    _In_ SIZE_T Length,
    _In_ ULONG_PTR BoundaryAddress,
    _In_ ULONG_PTR Alignment,
    _In_ PMM_AVL_TABLE Table,
    _Out_ PULONG_PTR Base,
    _Out_ PMMADDRESS_NODE* Parent)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

NTSTATUS
NTAPI
MiFindEmptyAddressRangeDownBasedTree(
    _In_ SIZE_T Length,
    _In_ ULONG_PTR BoundaryAddress,
    _In_ ULONG_PTR Alignment,
    _In_ PMM_AVL_TABLE Table,
    _Out_ PULONG_PTR Base)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MiInsertVadCharges(
    _In_ PMMVAD Vad,
    _In_ PEPROCESS Process)
{
    RTL_BITMAP BitMapHeader;
    PMMVAD VadFreeHint;
    ULONG StartingIndex;
    ULONG EndingIndex;
    ULONG StartPdeIndex;
    ULONG EndPdeIndex;
    ULONG PageCharge;
    ULONG PagesReallyCharged;
    ULONG RealCharge = 0;
    ULONG BitNumber;
    ULONG NumberToSet;
    BOOLEAN IsChangeJobMemoryUsage;
    NTSTATUS Status;

    DPRINT("MiInsertVadCharges: Vad %p, Process %p, StartingVpn %X, EndingVpn %X\n",
           Vad, Process, Vad->StartingVpn, Vad->EndingVpn);

    ASSERT(Vad->EndingVpn >= Vad->StartingVpn);
    ASSERT(Process == PsGetCurrentProcess());

    if (Vad->u.VadFlags.CommitCharge != 0x7FFFF) // ?
    {
        PageCharge = 0;
        PagesReallyCharged = 0;
        IsChangeJobMemoryUsage = FALSE;

        Status = PsChargeProcessNonPagedPoolQuota(Process, sizeof(MMVAD));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("MiInsertVadCharges: STATUS_COMMITMENT_LIMIT\n");
            return STATUS_COMMITMENT_LIMIT;
        }

        if (!Vad->u.VadFlags.PrivateMemory && Vad->ControlArea)
        {
            PagesReallyCharged = ((Vad->EndingVpn - Vad->StartingVpn + 1) * sizeof(MMPTE));

            Status = PsChargeProcessPagedPoolQuota(Process, PagesReallyCharged);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("MiInsertVadCharges: Status %X\n", Status);
                PagesReallyCharged = 0;
                goto ErrorExit;
            }
        }

        StartPdeIndex = MiAddressToPdeOffset(Vad->StartingVpn * PAGE_SIZE);
        EndPdeIndex = MiAddressToPdeOffset(Vad->EndingVpn * PAGE_SIZE);

        DPRINT("MiInsertVadCharges: StartPdeIndex %X, EndPdeIndex %X\n", StartPdeIndex, EndPdeIndex);

      #if (_MI_PAGING_LEVELS == 2)

        RtlInitializeBitMap(&BitMapHeader,
                            MmWorkingSetList->CommittedPageTables,
                            (MI_USED_PAGE_TABLES_MAX / (8 * sizeof(ULONG)))); 

        for (BitNumber = StartPdeIndex; BitNumber <= EndPdeIndex; BitNumber++)
        {
            if (!RtlCheckBit(&BitMapHeader, BitNumber))
                PageCharge++;
        }

      #else
        #error FIXME
        ASSERT(FALSE);
      #endif

        RealCharge = (PageCharge + Vad->u.VadFlags.CommitCharge);
        if (RealCharge)
        {
            Status = PsChargeProcessPageFileQuota(Process, RealCharge);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("MiInsertVadCharges: Status %X\n", Status);
                RealCharge = 0;
                goto ErrorExit;
            }

            if (Process->CommitChargeLimit &&
                Process->CommitChargeLimit < (RealCharge + Process->CommitCharge))
            {
                DPRINT1("MiInsertVadCharges: error\n");
                if (Process->Job)
                {
                    ASSERT(FALSE);//PsReportProcessMemoryLimitViolation();
                }

                goto ErrorExit;
            }

            if (Process->JobStatus & 0x10)
            {
                DPRINT1("MiInsertVadCharges: FIXME\n");
                ASSERT(FALSE);
                IsChangeJobMemoryUsage = TRUE;
            }

            DPRINT("MiInsertVadCharges: FIXME MiChargeCommitment \n");

            Process->CommitCharge += RealCharge;

            if (Process->CommitCharge > Process->CommitChargePeak)
                Process->CommitChargePeak = Process->CommitCharge;

            ASSERT(RealCharge == (Vad->u.VadFlags.CommitCharge + PageCharge));
        }

        if (PageCharge)
        {
            PagesReallyCharged = 0;

          #if (_MI_PAGING_LEVELS == 2)

            for (BitNumber = StartPdeIndex; BitNumber <= EndPdeIndex; BitNumber++)
            {
                if (!RtlCheckBit(&BitMapHeader, BitNumber))
                {
                    RtlSetBit(&BitMapHeader, BitNumber);

                    MmWorkingSetList->NumberOfCommittedPageTables++;
                    ASSERT(MmWorkingSetList->NumberOfCommittedPageTables < (PD_COUNT * PDE_PER_PAGE));

                    PagesReallyCharged++;
                }
            }

          #else
            #error FIXME
            ASSERT(FALSE);
          #endif

            ASSERT(PageCharge == PagesReallyCharged);
        }
    }

    StartingIndex = ((Vad->StartingVpn * PAGE_SIZE) / MM_ALLOCATION_GRANULARITY);
    EndingIndex = ((Vad->EndingVpn * PAGE_SIZE) / MM_ALLOCATION_GRANULARITY);

    NumberToSet = (EndingIndex - StartingIndex + 1);
    DPRINT("MiInsertVadCharges: StartingIndex %X NumberToSet %X\n", StartingIndex, NumberToSet);

    RtlInitializeBitMap(&BitMapHeader, MI_VAD_BITMAP, (MiLastVadBit + 1)); 
    RtlSetBits(&BitMapHeader, StartingIndex, NumberToSet);

    DPRINT("MiInsertVadCharges: MI_VAD_BITMAP %X, *(PULONG)MI_VAD_BITMAP %X\n", MI_VAD_BITMAP, *(PULONG)MI_VAD_BITMAP);

    if (MmWorkingSetList->VadBitMapHint == StartingIndex)
        MmWorkingSetList->VadBitMapHint = (EndingIndex + 1);

    VadFreeHint = Process->VadFreeHint;

    if (VadFreeHint && (VadFreeHint->EndingVpn + (MM_ALLOCATION_GRANULARITY / PAGE_SIZE)) >= Vad->StartingVpn)
        Process->VadFreeHint = Vad;

    return STATUS_SUCCESS;

ErrorExit:

    PsReturnProcessNonPagedPoolQuota(Process, sizeof(MMVAD));

    if (PagesReallyCharged)
        PsReturnProcessPagedPoolQuota(Process, PagesReallyCharged);

    if (RealCharge)
        PsReturnProcessPageFileQuota(Process, RealCharge);

    if (IsChangeJobMemoryUsage)
    {
        DPRINT1("MiInsertVadCharges: FIXME PsChangeJobMemoryUsage()\n");
        ASSERT(FALSE);
    }

    DPRINT1("MiInsertVadCharges: STATUS_COMMITMENT_LIMIT\n");
    return STATUS_COMMITMENT_LIMIT;
}

/* EOF */
