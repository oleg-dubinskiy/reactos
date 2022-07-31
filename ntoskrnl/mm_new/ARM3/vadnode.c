
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* Include Mm version of AVL support */
#include "miavl.h"
#include <sdk/lib/rtl/avlsupp.c>

/* GLOBALS ********************************************************************/


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

/* EOF */
