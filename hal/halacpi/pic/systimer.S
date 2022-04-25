
/* INCLUDES ******************************************************************/

#include <asm.inc>
#include <ks386.inc>

/* GLOBALS *******************************************************************/

.data
ASSUME CS:NOTHING, DS:NOTHING, ES:NOTHING, FS:NOTHING, GS:NOTHING

/* FUNCTIONS *****************************************************************/

.code

PUBLIC _KeStallExecutionProcessor@4
_KeStallExecutionProcessor@4:

    /* Get the number of microseconds required */
    mov ecx, [esp+4]
    jecxz Done

    /* Multiply by the stall factor */
    mov eax, fs:[KPCR_STALL_SCALE_FACTOR]
    mul ecx

    /* Jump to subtraction loop */
    jmp SubtractLoop

    /* Align to 16 bytes */
    .align 16

    /* Subtract one count */
SubtractLoop:
    sub eax, 1
    jnz SubtractLoop

Done:
    /* Return */
    ret 4

END
