/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

typedef struct _HAL_BIOS_FRAME
{
    ULONG SegSs;
    ULONG Esp;
    ULONG EFlags;
    ULONG SegCs;
    ULONG Eip;
    PKTRAP_FRAME TrapFrame;
    ULONG CsLimit;
    ULONG CsBase;
    ULONG CsFlags;
    ULONG SsLimit;
    ULONG SsBase;
    ULONG SsFlags;
    ULONG Prefix;
} HAL_BIOS_FRAME, *PHAL_BIOS_FRAME;

/* GLOBALS ********************************************************************/


/* V86 OPCODE HANDLERS ********************************************************/

BOOLEAN
FASTCALL
HalpOpcodeInvalid(IN PHAL_BIOS_FRAME BiosFrame)
{
    PUCHAR Inst = (PUCHAR)(BiosFrame->CsBase + BiosFrame->Eip);

    /* Print error message */
    DPRINT1("HAL: An invalid V86 opcode was encountered at address %X:%X\n"
            "Opcode: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
            BiosFrame->SegCs, BiosFrame->Eip,
            Inst[0], Inst[1], Inst[2], Inst[3], Inst[4],
            Inst[5], Inst[6], Inst[7], Inst[8], Inst[9]);

    /* Break */
    DbgBreakPoint();//HalpDbgBreakPointEx();
    return FALSE;
}

BOOLEAN
FASTCALL
HalpPushInt(IN PHAL_BIOS_FRAME BiosFrame,
            IN ULONG Interrupt)
{
    PUSHORT Stack;
    ULONG Eip;
    
    /* Calculate stack address (SP) */
    Stack = (PUSHORT)(BiosFrame->SsBase + (BiosFrame->Esp & 0xFFFF));
    
    /* Push EFlags */
    Stack--;
    *Stack = BiosFrame->EFlags & 0xFFFF;
    
    /* Push CS */
    Stack--;
    *Stack = BiosFrame->SegCs & 0xFFFF;
        
    /* Push IP */
    Stack--;
    *Stack = BiosFrame->Eip & 0xFFFF;
    
    /* Compute new CS:IP from the IVT address for this interrupt entry */
    Eip = *(PULONG)(Interrupt * 4);
    BiosFrame->Eip = Eip & 0xFFFF;
    BiosFrame->SegCs = Eip >> 16;
    
    /* Update stack address */
    BiosFrame->Esp = (ULONG_PTR)Stack & 0xFFFF;
    
    /* Update CS to linear */
    BiosFrame->CsBase = BiosFrame->SegCs << 4;
    BiosFrame->CsLimit = 0xFFFF;
    BiosFrame->CsFlags = 0;
    
    /* We're done */
    return TRUE;
}

BOOLEAN
FASTCALL
HalpOpcodeINTnn(IN PHAL_BIOS_FRAME BiosFrame)
{
    UCHAR Interrupt;
    PKTRAP_FRAME TrapFrame;
    
    /* Convert SS to linear */
    BiosFrame->SsBase = BiosFrame->SegSs << 4;
    BiosFrame->SsLimit = 0xFFFF;
    BiosFrame->SsFlags = 0;
    
    /* Increase EIP and validate */
    BiosFrame->Eip++;
    if (BiosFrame->Eip > BiosFrame->CsLimit) return FALSE;
    
    /* Read interrupt number */
    Interrupt = *(PUCHAR)(BiosFrame->CsBase + BiosFrame->Eip);
    
    /* Increase EIP and push the interrupt */
    BiosFrame->Eip++;
    if (HalpPushInt(BiosFrame, Interrupt))
    {
        /* Update the trap frame */
        TrapFrame = BiosFrame->TrapFrame;
        TrapFrame->HardwareSegSs = BiosFrame->SegSs;
        TrapFrame->HardwareEsp = BiosFrame->Esp;
        TrapFrame->SegCs = BiosFrame->SegCs;
        TrapFrame->EFlags = BiosFrame->EFlags;
        
        /* Success */
        return TRUE;
    }
    
    /* Failure */
    return FALSE;
}

BOOLEAN
FASTCALL
HalpDispatchV86Opcode(IN PKTRAP_FRAME TrapFrame)
{
    UCHAR Instruction;
    HAL_BIOS_FRAME BiosFrame;
    
    /* Fill out the BIOS frame */
    BiosFrame.TrapFrame = TrapFrame;
    BiosFrame.SegSs = TrapFrame->HardwareSegSs;
    BiosFrame.Esp = TrapFrame->HardwareEsp;
    BiosFrame.EFlags = TrapFrame->EFlags;
    BiosFrame.SegCs = TrapFrame->SegCs;
    BiosFrame.Eip = TrapFrame->Eip;
    BiosFrame.Prefix = 0;
    
    /* Convert CS to linear */
    BiosFrame.CsBase = BiosFrame.SegCs << 4;
    BiosFrame.CsLimit = 0xFFFF;
    BiosFrame.CsFlags = 0;
    
    /* Validate IP */
    if (BiosFrame.Eip > BiosFrame.CsLimit) return FALSE;
    
    /* Read IP */
    Instruction = *(PUCHAR)(BiosFrame.CsBase + BiosFrame.Eip);
    if (Instruction != 0xCD)
    {
        /* We only support INT */
        HalpOpcodeInvalid(&BiosFrame);
        return FALSE;
    }
    
    /* Handle the interrupt */
    if (HalpOpcodeINTnn(&BiosFrame))
    {
        /* Update EIP */
        TrapFrame->Eip = BiosFrame.Eip;
        
        /* We're done */
        return TRUE;
    }
    
    /* Failure */
    return FALSE;
}

/* V86 TRAP HANDLERS **********************************************************/

DECLSPEC_NORETURN
VOID
FASTCALL
HalpTrap0DHandler(IN PKTRAP_FRAME TrapFrame)
{
    /* Enter the trap */
    KiEnterTrap(TrapFrame);
    
    /* Check if this is a V86 trap */
    if (TrapFrame->EFlags & EFLAGS_V86_MASK)
    {
        /* Dispatch the opcode and exit the trap */
        HalpDispatchV86Opcode(TrapFrame);
        KiEoiHelper(TrapFrame);
    }
    
    /* Strange, it isn't! This can happen during NMI */
    DPRINT1("HAL: Trap0D while not in V86 mode\n");
    KiDumpTrapFrame(TrapFrame);

    ERROR_FATAL();
    while (TRUE); /* 'noreturn' function */
}

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
HalpBiosDisplayReset(VOID)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return FALSE;
}

/* EOF */
