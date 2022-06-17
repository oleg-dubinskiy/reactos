
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpRegisterVector)
#endif

/* GLOBALS ********************************************************************/

IDTUsageFlags HalpIDTUsageFlags[MAXIMUM_IDTVECTOR + 1];
IDTUsage HalpIDTUsage[MAXIMUM_IDTVECTOR + 1];

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
HalpRegisterVector(
    _In_ UCHAR Flags,
    _In_ ULONG BusVector,
    _In_ ULONG SystemVector,
    _In_ KIRQL Irql)
{
    DPRINT("HalpRegisterVector: %X, %X, %X, %X\n", Flags, BusVector, SystemVector, Irql);

    /* Save the vector flags */
    HalpIDTUsageFlags[SystemVector].Flags = Flags;

    /* Save the vector data */
    HalpIDTUsage[SystemVector].Irql  = Irql;
    HalpIDTUsage[SystemVector].BusReleativeVector = (UCHAR)BusVector;
}


/* EOF */

