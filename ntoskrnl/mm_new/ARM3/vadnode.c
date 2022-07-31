
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


/* EOF */
