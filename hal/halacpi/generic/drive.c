
/* INCLUDES ******************************************************************/

#include <hal.h>
#include <ntdddisk.h>

//#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
HalpAssignDriveLetters(IN struct _LOADER_PARAMETER_BLOCK * LoaderBlock,
                       IN PSTRING NtDeviceName,
                       OUT PUCHAR NtSystemPath,
                       OUT PSTRING NtSystemPathString)
{
    /* Call the kernel */
    IoAssignDriveLetters(LoaderBlock,
                         NtDeviceName,
                         NtSystemPath,
                         NtSystemPathString);
}

/* EOF */
