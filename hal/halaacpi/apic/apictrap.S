
/* INCLUDES ******************************************************************/

#include <asm.inc>
#include <ks386.inc>
#include <internal/i386/asmmacro.S>

.code

TRAP_ENTRY HalpLocalApicErrorService, KI_PUSH_FAKE_ERROR_CODE

PUBLIC _PicSpuriousService37
_PicSpuriousService37:
    iret

PUBLIC _ApicSpuriousService
_ApicSpuriousService:
    iret

END
