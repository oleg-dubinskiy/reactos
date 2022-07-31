
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

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


/* EOF */
