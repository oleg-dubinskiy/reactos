
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalInitializeProcessor)
  #pragma alloc_text(INIT, HalInitSystem)
#endif

/* GLOBALS *******************************************************************/


/* PRIVATE FUNCTIONS *********************************************************/


/* FUNCTIONS *****************************************************************/

INIT_FUNCTION
VOID
NTAPI
HalInitializeProcessor(
    IN ULONG ProcessorNumber,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNIMPLEMENTED;
}

INIT_FUNCTION
BOOLEAN
NTAPI
HalInitSystem(IN ULONG BootPhase,
              IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNIMPLEMENTED;
    return TRUE;
}

/* EOF */
