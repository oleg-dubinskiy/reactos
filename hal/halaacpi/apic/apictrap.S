
/* INCLUDES ******************************************************************/

#include <asm.inc>
#include <ks386.inc>
#include <internal/i386/asmmacro.S>

.code

PUBLIC _PicSpuriousService37
_PicSpuriousService37:
    iret

PUBLIC _ApicSpuriousService
_ApicSpuriousService:
    iret

END
