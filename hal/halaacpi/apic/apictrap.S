
/* INCLUDES ******************************************************************/

#include <asm.inc>
#include <ks386.inc>
#include <internal/i386/asmmacro.S>

.code

//TRAP_ENTRY HalpClockInterrupt, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY HalpClockInterruptStub, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY HalpProfileInterrupt, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY HalpApicRebootService, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY HalpBroadcastCallService, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY HalpPerfInterrupt, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY HalpIpiHandler, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY HalpLocalApicErrorService, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY HalpTrap0D, 0

PUBLIC _PicSpuriousService37
_PicSpuriousService37:
    iret

PUBLIC _ApicSpuriousService
_ApicSpuriousService:
    iret

END
