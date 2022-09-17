
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

CHAR MmReadWrite[32] =
{
    MM_NO_ACCESS_ALLOWED, MM_READ_ONLY_ALLOWED, MM_READ_ONLY_ALLOWED, MM_READ_ONLY_ALLOWED,
    MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED,

    MM_NO_ACCESS_ALLOWED, MM_READ_ONLY_ALLOWED, MM_READ_ONLY_ALLOWED, MM_READ_ONLY_ALLOWED,
    MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED,

    MM_NO_ACCESS_ALLOWED, MM_READ_ONLY_ALLOWED, MM_READ_ONLY_ALLOWED, MM_READ_ONLY_ALLOWED,
    MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED,

    MM_NO_ACCESS_ALLOWED, MM_READ_ONLY_ALLOWED, MM_READ_ONLY_ALLOWED, MM_READ_ONLY_ALLOWED,
    MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED, MM_READ_WRITE_ALLOWED,
};

extern ULONG MiLastVadBit;
extern SIZE_T MmTotalCommittedPages;

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

PMMADDRESS_NODE
NTAPI
MiGetPreviousNode(
    _In_ PMMADDRESS_NODE Node)
{
    PMMADDRESS_NODE Parent;

    /* Get the left child */
    if (RtlLeftChildAvl(Node))
    {
        /* Get right-most child */
        Node = RtlLeftChildAvl(Node);

        while (RtlRightChildAvl(Node))
            Node = RtlRightChildAvl(Node);

        return Node;
    }

    Parent = RtlParentAvl(Node);
    ASSERT(Parent != NULL);

    while (Parent != Node)
    {
        /* The parent should be a right child, return the real predecessor */
        if (RtlIsRightChildAvl(Node))
        {
            /* Return it unless it's the root */
            if (Parent == RtlParentAvl(Parent))
                Parent = NULL;

            return Parent;
        }

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
    _Out_ PULONG_PTR Base)
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

VOID
NTAPI
MiInsertNode(
    _In_ PMM_AVL_TABLE Table,
    _In_ PMMADDRESS_NODE NewNode,
    _In_ PMMADDRESS_NODE Parent,
    _In_ TABLE_SEARCH_RESULT Result)
{
    DPRINT1("MiInsertNode: NewNode %p, Table %p, StartingVpn %p, EndingVpn %p\n",
            NewNode, Table, NewNode->StartingVpn, NewNode->EndingVpn);

    /* Insert it into the tree */
    RtlpInsertAvlTreeNode(Table, NewNode, Parent, Result);

    DPRINT1("MiInsertNode: Result %X\n", Result);
}

VOID
NTAPI
MiInsertVad(
    _In_ PMMVAD Vad,
    _In_ PMM_AVL_TABLE VadRoot)
{
    TABLE_SEARCH_RESULT Result;
    PMMADDRESS_NODE Parent = NULL;

    DPRINT("MiInsertVad: Vad %p, VadRoot %p\n", Vad, VadRoot);

    /* Validate the VAD and set it as the current hint */
    ASSERT(Vad->EndingVpn >= Vad->StartingVpn);

    VadRoot->NodeHint = Vad;

    /* Find the parent VAD and where this child should be inserted */
    Result = RtlpFindAvlTableNodeOrParent(VadRoot, (PVOID)Vad->StartingVpn, &Parent);

    ASSERT(Result != TableFoundNode);
    ASSERT((Parent != NULL) || (Result == TableEmptyTree));

    /* Do the actual insert operation */
    MiInsertNode(VadRoot, (PVOID)Vad, Parent, Result);
}

PMMVAD
NTAPI
MiLocateAddress(
    _In_ PVOID VirtualAddress)
{
    PMM_AVL_TABLE Table = &PsGetCurrentProcess()->VadRoot;
    PMMVAD FoundVad;
    TABLE_SEARCH_RESULT SearchResult;
    ULONG_PTR Vpn;

    /* Start with the the hint */
    FoundVad = (PMMVAD)Table->NodeHint;
    if (!FoundVad)
        return NULL;

    /* Check if this VPN is in the hint, if so, use it */
    Vpn = ((ULONG_PTR)VirtualAddress / PAGE_SIZE);

    if (Vpn >= FoundVad->StartingVpn && Vpn <= FoundVad->EndingVpn)
        return FoundVad;

    /* VAD hint didn't work, go look for it */
    SearchResult = RtlpFindAvlTableNodeOrParent(Table, (PVOID)Vpn, (PMMADDRESS_NODE *)&FoundVad);
    if (SearchResult != TableFoundNode)
        return NULL;

    /* We found it, update the hint */
    ASSERT(FoundVad != NULL);
    ASSERT((Vpn >= FoundVad->StartingVpn) && (Vpn <= FoundVad->EndingVpn));

    Table->NodeHint = FoundVad;

    return FoundVad;
}

PMM_AVL_TABLE
NTAPI
MiCreatePhysicalVadRoot(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN IsLocked)
{
    PMM_AVL_TABLE PhysicalVadRoot;
    PETHREAD Thread;

    DPRINT("MiCreatePhysicalVadRoot: Process %p, IsLocked %X\n", Process, IsLocked);

    if (Process->PhysicalVadRoot)
    {
        DPRINT1("MiCreatePhysicalVadRoot: Process->PhysicalVadRoot %p\n", Process->PhysicalVadRoot);
        return Process->PhysicalVadRoot;
    }

    PhysicalVadRoot = ExAllocatePoolWithTag(NonPagedPool, sizeof(MM_AVL_TABLE), 'rpmM');
    if (!PhysicalVadRoot)
    {
        DPRINT1("MiCreatePhysicalVadRoot: allocate failed\n");
        return NULL;
    }

    RtlZeroMemory(PhysicalVadRoot, sizeof(MM_AVL_TABLE));

    ASSERT(PhysicalVadRoot->NumberGenericTableElements == 0);

    Thread = PsGetCurrentThread();

    PhysicalVadRoot->BalancedRoot.u1.Parent = &PhysicalVadRoot->BalancedRoot;

    if (!IsLocked)
        MiLockProcessWorkingSet(Process, Thread);

    if (Process->PhysicalVadRoot)
    {
        if (!IsLocked)
            MiUnlockProcessWorkingSet(Process, Thread);

        ExFreePoolWithTag(PhysicalVadRoot, 'rpmM');
    }
    else
    {
        Process->PhysicalVadRoot = PhysicalVadRoot;

        if (!IsLocked)
            MiUnlockProcessWorkingSet(Process, Thread);
    }

    return Process->PhysicalVadRoot;
}

NTSTATUS
NTAPI
MiCheckSecuredVad(
    _In_ PMMVAD Vad,
    _In_ PVOID Base,
    _In_ SIZE_T Size,
    _In_ ULONG ProtectionMask)
{
    ULONG_PTR StartAddress, EndAddress;

    /* Compute start and end address */
    StartAddress = (ULONG_PTR)Base;
    EndAddress = (StartAddress + Size - 1);

    /* Are we deleting/unmapping, or changing? */
    if (ProtectionMask < MM_DELETE_CHECK)
    {
        /* Changing... are we allowed to do so? */
        if (Vad->u.VadFlags.NoChange &&
            Vad->u2.VadFlags2.SecNoChange &&
            Vad->u.VadFlags.Protection != ProtectionMask)
        {
            /* Nope, bail out */
            DPRINT1("Trying to mess with a no-change VAD!\n");
            return STATUS_INVALID_PAGE_PROTECTION;
        }
    }
    else
    {
        /* This is allowed */
        ProtectionMask = 0;
    }

    /* ARM3 doesn't support this yet */
    ASSERT(Vad->u2.VadFlags2.MultipleSecured == 0);

    /* Is this a one-secured VAD, like a TEB or PEB? */
    if (!Vad->u2.VadFlags2.OneSecured)
        return STATUS_SUCCESS;

    /* Is this allocation being described by the VAD? */
    if (StartAddress <= ((PMMVAD_LONG)Vad)->u3.Secured.EndVpn &&
        EndAddress >= ((PMMVAD_LONG)Vad)->u3.Secured.StartVpn)
    {
        /* Guard page? */
        if (ProtectionMask & MM_DECOMMIT)
        {
            DPRINT1("Not allowed to change protection on guard page!\n");
            return STATUS_INVALID_PAGE_PROTECTION;
        }

        /* ARM3 doesn't have read-only VADs yet */
        ASSERT(Vad->u2.VadFlags2.ReadOnly == 0);

        /* Check if read-write protections are allowed */
        if (MmReadWrite[ProtectionMask] < MM_READ_WRITE_ALLOWED)
        {
            DPRINT1("Invalid protection mask for RW access!\n");
            return STATUS_INVALID_PAGE_PROTECTION;
        }
    }

    /* All good, allow the change */
    return STATUS_SUCCESS;
}

VOID
NTAPI
MiRemoveVadCharges(
    _In_ PMMVAD Vad,
    _In_ PEPROCESS Process)
{
    ULONG_PTR SizeVpn;
    ULONG RealCharge;

    DPRINT("MiRemoveVadCharges: Vad %p, Process %p\n", Vad, Process);

    ASSERT(!MM_ANY_WS_LOCK_HELD(PsGetCurrentThread()));
    ASSERT(Process == PsGetCurrentProcess());

    RealCharge = Vad->u.VadFlags.CommitCharge;

    if (RealCharge != 0x7FFFF)
    {
        PsReturnProcessNonPagedPoolQuota(Process, sizeof(MMVAD));

        if (!Vad->u.VadFlags.PrivateMemory && Vad->ControlArea)
        {
            SizeVpn = (Vad->EndingVpn - Vad->StartingVpn + 1);
            PsReturnProcessPagedPoolQuota(Process, (SizeVpn * sizeof(MMPTE)));
        }

        if (RealCharge)
        {
            PsReturnProcessPageFileQuota(Process, RealCharge);

            ASSERT((SSIZE_T)(RealCharge) >= 0);
            ASSERT(MmTotalCommittedPages >= RealCharge);

            InterlockedExchangeAdd((volatile PLONG)&MmTotalCommittedPages, -RealCharge);

            if (Process->JobStatus & 0x10) // ?
            {
                DPRINT1("MiRemoveVadCharges: FIXME PsChangeJobMemoryUsage()\n");
                ASSERT(FALSE);
            }

            Process->CommitCharge -= RealCharge;
        }
    }

    if (Vad->u.VadFlags.NoChange &&
        Vad->u2.VadFlags2.MultipleSecured)
    {
        DPRINT1("MiRemoveVadCharges: FIXME MultipleSecured\n");
        ASSERT(FALSE);
    }

    if (Process->VadFreeHint)
    {
        if (Vad == Process->VadFreeHint ||
            Vad->StartingVpn < ((PMMVAD)(Process->VadFreeHint))->StartingVpn)
        {
            Process->VadFreeHint = MiGetPreviousNode((PMMADDRESS_NODE)Vad);
        }
    }
}

VOID
NTAPI
MiRemoveNode(
    _In_ PMMADDRESS_NODE Node,
    _In_ PMM_AVL_TABLE Table)
{
    DPRINT("MiRemoveNode: Node %p, Table %p\n", Node, Table);

    /* Call the AVL code */
    RtlpDeleteAvlTreeNode(Table, Node);

    /* Decrease element count */
    Table->NumberGenericTableElements--;

    /* Check if this node was the hint */
    if (Table->NodeHint != Node)
        return;

    /* Get a new hint, unless we're empty now, in which case nothing */
    if (!Table->NumberGenericTableElements)
        Table->NodeHint = NULL;
    else
        Table->NodeHint = Table->BalancedRoot.RightChild;
}

TABLE_SEARCH_RESULT
NTAPI
MiFindNodeOrParent(
    _In_ PMM_AVL_TABLE Table,
    _In_ ULONG_PTR StartingVpn,
    _Out_ PMMADDRESS_NODE* OutNodeOrParent)
{
    PMMADDRESS_NODE Node;
    PMMADDRESS_NODE ChildNode;
    TABLE_SEARCH_RESULT SearchResult;
    ULONG NumberCompares = 0;

    if (!Table->NumberGenericTableElements)
        return TableEmptyTree;

    for (Node = Table->BalancedRoot.RightChild;
         ;
         Node = ChildNode)
    {
        NumberCompares++;
        ASSERT(NumberCompares <= Table->DepthOfTree);

        if (StartingVpn >= Node->StartingVpn)
        {
            if (StartingVpn <= Node->EndingVpn)
            {
                SearchResult = TableFoundNode;
                break;
            }

            ChildNode = Node->RightChild;
            if (ChildNode)
                continue;

            SearchResult = TableInsertAsRight;
            break;
        }

        ChildNode = Node->LeftChild;

        if (!ChildNode)
        {
            SearchResult = TableInsertAsLeft;
            break;
        }
    }

    *OutNodeOrParent = Node;

    return SearchResult;
}

VOID
NTAPI
MiPhysicalViewRemover(
    _In_ PEPROCESS Process,
    _In_ PMMVAD Vad)
{
    PMM_PHYSICAL_VIEW PhysicalView;
    TABLE_SEARCH_RESULT SearchResult;

    DPRINT("MiPhysicalViewRemover: Process %p, Vad %p\n", Process, Vad);

    ASSERT(Process->PhysicalVadRoot != NULL);

    SearchResult = MiFindNodeOrParent(Process->PhysicalVadRoot,
                                      Vad->StartingVpn,
                                      (PMMADDRESS_NODE *)&PhysicalView);

    ASSERT(SearchResult == TableFoundNode);
    ASSERT(PhysicalView->Vad == Vad);

    MiRemoveNode((PMMADDRESS_NODE)PhysicalView, Process->PhysicalVadRoot);

    if (Vad->u.VadFlags.VadType == VadWriteWatch)
    {
        DPRINT1("MiPhysicalViewRemover: VadType - VadWriteWatch\n");
        ASSERT(FALSE);
    }

    if (Vad->u.VadFlags.VadType == VadRotatePhysical)
    {
        DPRINT1("MiPhysicalViewRemover: VadType - VadRotatePhysical\n");
        ASSERT(FALSE);
    }

    ExFreePoolWithTag(PhysicalView, 'vpmM');
}

/* EOF */
