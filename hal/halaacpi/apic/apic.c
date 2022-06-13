
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include "apic.h"

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DetectAcpiMP)
#endif

/* GLOBALS *******************************************************************/


/* PRIVATE FUNCTIONS *********************************************************/


/* FUNCTIONS *****************************************************************/

INIT_FUNCTION
BOOLEAN
NTAPI 
DetectAcpiMP(
    _Out_ PBOOLEAN OutIsMpSystem,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    HalDisplayString("HAL: DetectAPIC: APIC system found - Returning TRUE\n");
    return TRUE;
}

/* EOF */
