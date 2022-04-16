
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpMapPhysicalMemory64)
  #pragma alloc_text(INIT, HalpUnmapVirtualAddress)
#endif

/* GLOBALS *******************************************************************/


/* PRIVATE FUNCTIONS *********************************************************/

INIT_FUNCTION
PVOID
NTAPI
HalpMapPhysicalMemory64(IN PHYSICAL_ADDRESS PhysicalAddress,
                        IN PFN_COUNT PageCount)
{
    UNIMPLEMENTED;
    return NULL;
}

INIT_FUNCTION
VOID
NTAPI
HalpUnmapVirtualAddress(IN PVOID VirtualAddress,
                        IN PFN_COUNT PageCount)
{
    UNIMPLEMENTED;
}

/* EOF */
