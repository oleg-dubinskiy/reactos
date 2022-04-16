
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalAcpiGetTable)
  #pragma alloc_text(INIT, HalpCheckPowerButton)
#endif

/* GLOBALS ********************************************************************/


/* PRIVATE FUNCTIONS **********************************************************/

INIT_FUNCTION
PVOID
NTAPI
HalAcpiGetTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                IN ULONG Signature)
{
    UNIMPLEMENTED;
    return NULL;
}

INIT_FUNCTION
VOID
NTAPI
HalpCheckPowerButton(VOID)
{
    UNIMPLEMENTED;
}

/* EOF */
