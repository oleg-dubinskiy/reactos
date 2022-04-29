#pragma once

/* INCLUDES ******************************************************************/

typedef struct _PCI_REGISTRY_INFO_INTERNAL
{
    UCHAR MajorRevision;
    UCHAR MinorRevision;
    UCHAR NoBuses; // Number Of Buses
    UCHAR HardwareMechanism;
    ULONG ElementCount;
    PCI_CARD_DESCRIPTOR CardList[ANYSIZE_ARRAY];
} PCI_REGISTRY_INFO_INTERNAL, *PPCI_REGISTRY_INFO_INTERNAL;


/* FUNCTIONS *****************************************************************/


/* EOF */
