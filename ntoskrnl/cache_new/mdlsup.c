
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/

VOID
NTAPI
CcMdlReadComplete2(IN PFILE_OBJECT FileObject,
                   IN PMDL MdlChain)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcMdlWriteComplete2(IN PFILE_OBJECT FileObject,
                    IN PLARGE_INTEGER FileOffset,
                    IN PMDL MdlChain)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
CcMdlRead(IN PFILE_OBJECT FileObject,
          IN PLARGE_INTEGER FileOffset,
          IN ULONG Length,
          OUT PMDL *MdlChain,
          OUT PIO_STATUS_BLOCK IoStatus)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcMdlReadComplete(IN PFILE_OBJECT FileObject,
                  IN PMDL MdlChain)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcMdlWriteAbort(IN PFILE_OBJECT FileObject,
                IN PMDL MdlChain)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcMdlWriteComplete(IN PFILE_OBJECT FileObject,
                   IN PLARGE_INTEGER FileOffset,
                   IN PMDL MdlChain)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
CcPrepareMdlWrite(IN PFILE_OBJECT FileObject,
                  IN PLARGE_INTEGER FileOffset,
                  IN ULONG Length,
                  OUT PMDL *MdlChain,
                  OUT PIO_STATUS_BLOCK IoStatus)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
