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

/* PTE Data */
ULONG HalpSavedPfn;
HARDWARE_PTE HalpSavedPte;

/* IDT Data */
PVOID HalpGpfHandler;
PVOID HalpBopHandler;

/* TSS Data */
ULONG HalpSavedEsp0;
USHORT HalpSavedTss;

/* IOPM Data */
USHORT HalpSavedIopmBase;
PUSHORT HalpSavedIoMap;
USHORT HalpSavedIoMapData[IOPM_SIZE / sizeof(USHORT)][2];
ULONG HalpSavedIoMapEntries;

/* Where the protected mode stack is */
//ULONG_PTR HalpSavedEsp;

/* Where the real mode code ends */
extern PVOID HalpRealModeEnd;

/* Context saved for return from v86 mode */
jmp_buf HalpSavedContext;

BOOLEAN HalpNMIInProgress;

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

VOID
DECLSPEC_NORETURN
HalpTrap06(VOID)
{
    /* Restore ES/DS to known good values first */
    Ke386SetEs(KGDT_R3_DATA | RPL_MASK);
    Ke386SetDs(KGDT_R3_DATA | RPL_MASK);
    Ke386SetFs(KGDT_R0_PCR);

    /* Restore the stack */ 
    KeGetPcr()->TSS->Esp0 = HalpSavedEsp0;

    /* Return back to where we left */
    longjmp(HalpSavedContext, 1);
    UNREACHABLE;
}

/* V8086 ENTER ****************************************************************/

VOID
NTAPI
HalpBiosCall(VOID)
{
    /* Must be volatile so it doesn't get optimized away! */
    volatile KTRAP_FRAME V86TrapFrame;
    ULONG_PTR StackOffset, CodeOffset;
    
    /* Save the context, check for return */
    if (_setjmp(HalpSavedContext))
        /* Returned from v86 */
        return;
    
    /* Kill alignment faults */
    __writecr0(__readcr0() & ~CR0_AM);
    
    /* Set new stack address */
    KeGetPcr()->TSS->Esp0 = ((ULONG)&V86TrapFrame - 0x20 - sizeof(FX_SAVE_AREA));

    /* Compute segmented IP and SP offsets */
    StackOffset = ((ULONG_PTR)&HalpRealModeEnd - 4 - (ULONG_PTR)HalpRealModeStart);
    CodeOffset = ((ULONG_PTR)HalpRealModeStart & 0xFFF);
    
    /* Now build the V86 trap frame */
    V86TrapFrame.V86Es = 0;
    V86TrapFrame.V86Ds = 0;
    V86TrapFrame.V86Gs = 0;
    V86TrapFrame.V86Fs = 0;
    V86TrapFrame.HardwareSegSs = 0x2000;
    V86TrapFrame.HardwareEsp = (StackOffset + CodeOffset);
    V86TrapFrame.EFlags = (__readeflags() | EFLAGS_V86_MASK | EFLAGS_IOPL);
    V86TrapFrame.SegCs = 0x2000;
    V86TrapFrame.Eip = CodeOffset;
    
    /* Exit to V86 mode */
    HalpExitToV86((PKTRAP_FRAME)&V86TrapFrame);
}

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
HalpBorrowTss(VOID)
{
    USHORT Tss;
    PKGDTENTRY TssGdt;
    ULONG_PTR TssLimit;
    PKTSS TssBase;

    /* Get the current TSS and its GDT entry */
    Tss = Ke386GetTr();
    TssGdt = &((PKIPCR)KeGetPcr())->GDT[Tss / sizeof(KGDTENTRY)];

    /* Get the KTSS limit and check if it has IOPM space */
    TssLimit = (TssGdt->LimitLow | TssGdt->HighWord.Bits.LimitHi << 16);

    /* If the KTSS doesn't have enough space this is probably an NMI or DF */
    if (TssLimit > IOPM_SIZE)
    {
        /* We are good to go */
        HalpSavedTss = 0;
        return;
    }

    /* Get the "real" TSS */
    TssGdt = &((PKIPCR)KeGetPcr())->GDT[KGDT_TSS / sizeof(KGDTENTRY)];
    TssBase = (PKTSS)(ULONG_PTR)(TssGdt->BaseLow |
                                 TssGdt->HighWord.Bytes.BaseMid << 16 |
                                 TssGdt->HighWord.Bytes.BaseHi << 24);

    /* Switch to it */
    KeGetPcr()->TSS = TssBase;

    /* Set it up */
    TssGdt->HighWord.Bits.Type = I386_TSS;
    TssGdt->HighWord.Bits.Pres = 1;
    TssGdt->HighWord.Bits.Dpl = 0;
    
    /* Load new TSS and return old one */
    Ke386SetTr(KGDT_TSS);
    HalpSavedTss = Tss;
}

VOID
NTAPI
HalpStoreAndClearIopm(VOID)
{
    USHORT ix, jx;
    PUSHORT Entry = HalpSavedIoMap;

    /* Loop the I/O Map */
    for (ix = jx = 0; ix < (IOPM_SIZE / sizeof(USHORT)); ix++)
    {
        /* Check for non-FFFF entry */
        if (*Entry != 0xFFFF)
        {
            /* Save it */
            ASSERT(jx < 32);
            HalpSavedIoMapData[jx][0] = ix;
            HalpSavedIoMapData[jx][1] = *Entry;
            jx++;
        }

        /* Clear it */
        *Entry++ = 0;
    }

    /* Terminate it */
    while (ix++ < (IOPM_FULL_SIZE / sizeof(USHORT)))
    {
        *Entry++ = 0xFFFF;
    }

    /* Return the entries we saved */
    HalpSavedIoMapEntries = jx;
}

VOID
NTAPI
HalpMapRealModeMemory(VOID)
{
    PHARDWARE_PTE Pte, V86Pte;
    ULONG ix;

    /* Get the page table directory for the lowest meg of memory */
    Pte = HalAddressToPde(0);
    HalpSavedPfn = Pte->PageFrameNumber;
    HalpSavedPte = *Pte;

    /* Map it to the HAL reserved region and make it valid */
    Pte->Valid = 1;
    Pte->Write = 1;
    Pte->Owner = 1;
    Pte->PageFrameNumber = (HalAddressToPde(0xFFC00000))->PageFrameNumber;

    /* Flush the TLB */
    HalpFlushTLB();

    /* Now loop the first meg of memory */
    for (ix = 0; ix < 0x100000; ix += PAGE_SIZE)
    {
        /* Identity map it */
        Pte = HalAddressToPte(ix);
        Pte->PageFrameNumber = ix >> PAGE_SHIFT;
        Pte->Valid = 1;
        Pte->Write = 1;
        Pte->Owner = 1;
    }

    /* Now get the entry for our real mode V86 code and the target */
    Pte = HalAddressToPte(0x20000);
    V86Pte = HalAddressToPte(&HalpRealModeStart);

    do
    {
        /* Map the physical address into our real-mode region */
        Pte->PageFrameNumber = V86Pte->PageFrameNumber;
        
        /* Keep going until we've reached the end of our region */
        Pte++;
        V86Pte++;
    }
    while (V86Pte <= HalAddressToPte(&HalpRealModeEnd));

    /* Flush the TLB */
    HalpFlushTLB();
}

VOID
NTAPI
HalpSwitchToRealModeTrapHandlers(VOID)
{
    /* Save the current Invalid Opcode and General Protection Fault Handlers */
    HalpGpfHandler = KeQueryInterruptHandler(13);
    HalpBopHandler = KeQueryInterruptHandler(6);

    /* Now set our own GPF handler to handle exceptions while in real mode */
    KeRegisterInterruptHandler(13, HalpTrap0D);

    /* And our own invalid opcode handler to detect the BOP to get us out */
    KeRegisterInterruptHandler(6, HalpTrap06);
}

VOID
NTAPI
HalpSetupRealModeIoPermissionsAndTask(VOID)
{
    /* Switch to valid TSS */
    HalpBorrowTss();

    /* Save a copy of the I/O Map and delete it */
    HalpSavedIoMap = (PUSHORT)KeGetPcr()->TSS->IoMaps[0].IoMap;
    HalpStoreAndClearIopm();

    /* Save the IOPM and switch to the real-mode one */
    HalpSavedIopmBase = KeGetPcr()->TSS->IoMapBase;
    KeGetPcr()->TSS->IoMapBase = KiComputeIopmOffset(1);

    /* Save our stack pointer */
    HalpSavedEsp0 = KeGetPcr()->TSS->Esp0; 
}

VOID
NTAPI
HalpRestoreTrapHandlers(VOID)
{
    /* Keep dummy GPF handler in case we get an NMI during V8086 */
    if (!HalpNMIInProgress)
    {
        /* Not an NMI -- put back the original handler */
        KeRegisterInterruptHandler(13, HalpGpfHandler);
    }

    /* Restore invalid opcode handler */
    KeRegisterInterruptHandler(6, HalpBopHandler);
}

VOID
NTAPI
HalpRestoreIopm(VOID)
{
    ULONG ix = HalpSavedIoMapEntries;

    /* Set default state */
    RtlFillMemory(HalpSavedIoMap, IOPM_FULL_SIZE, 0xFF);

    /* Restore the backed up copy, and initialize it */
    while (ix--)
        HalpSavedIoMap[HalpSavedIoMapData[ix][0]] = HalpSavedIoMapData[ix][1];
}

VOID
NTAPI
HalpReturnTss(VOID)
{
    PKGDTENTRY TssGdt;
    PKTSS TssBase;

    /* Get the original TSS */
    TssGdt = &((PKIPCR)KeGetPcr())->GDT[HalpSavedTss / sizeof(KGDTENTRY)];
    TssBase = (PKTSS)(ULONG_PTR)(TssGdt->BaseLow |
                                 TssGdt->HighWord.Bytes.BaseMid << 16 |
                                 TssGdt->HighWord.Bytes.BaseHi << 24);
    /* Switch to it */
    KeGetPcr()->TSS = TssBase;

    /* Set it up */
    TssGdt->HighWord.Bits.Type = I386_TSS;
    TssGdt->HighWord.Bits.Pres = 1;
    TssGdt->HighWord.Bits.Dpl = 0;

    /* Load old TSS */
    Ke386SetTr(HalpSavedTss);
}

VOID
NTAPI
HalpRestoreIoPermissionsAndTask(VOID)
{
    /* Restore the stack pointer */
    KeGetPcr()->TSS->Esp0 = HalpSavedEsp0;

    /* Restore the I/O Map */
    HalpRestoreIopm();

    /* Restore the IOPM */
    KeGetPcr()->TSS->IoMapBase = HalpSavedIopmBase;

    /* Restore the TSS */
    if (HalpSavedTss)
        HalpReturnTss();
}

VOID
NTAPI
HalpUnmapRealModeMemory(VOID)
{
    ULONG ix;
    PHARDWARE_PTE Pte;

    /* Loop the first meg of memory */
    for (ix = 0; ix < 0x100000; ix += PAGE_SIZE)
    {
        /* Invalidate each PTE */
        Pte = HalAddressToPte(ix);
        Pte->Valid = 0;
        Pte->Write = 0;
        Pte->Owner = 0;
        Pte->PageFrameNumber = 0;
    }

    /* Restore the PDE for the lowest megabyte of memory */
    Pte = HalAddressToPde(0);
    *Pte = HalpSavedPte;
    Pte->PageFrameNumber = HalpSavedPfn;

    /* Flush the TLB */
    HalpFlushTLB();
}

BOOLEAN
NTAPI
HalpBiosDisplayReset(VOID)
{
    ULONG Flags;
    PHARDWARE_PTE IdtPte;
    BOOLEAN RestoreWriteProtection = FALSE;

    /* Disable interrupts */
    Flags = __readeflags();
    _disable();

    /* Map memory available to the V8086 real-mode code */
    HalpMapRealModeMemory();

    /* On P5, the first 7 entries of the IDT are write protected to work around
       the cmpxchg8b lock errata. Unprotect them here so we can set our custom
       invalid op-code handler.
    */
    IdtPte = HalAddressToPte(((PKIPCR)KeGetPcr())->IDT);
    RestoreWriteProtection = (IdtPte->Write != 0);
    IdtPte->Write = 1;

    /* Use special invalid opcode and GPF trap handlers */
    HalpSwitchToRealModeTrapHandlers();

    /* Configure the IOPM and TSS */
    HalpSetupRealModeIoPermissionsAndTask();

    /* Now jump to real mode */
    HalpBiosCall();

    /* Restore kernel trap handlers */
    HalpRestoreTrapHandlers();

    /* Restore write permission */
    IdtPte->Write = RestoreWriteProtection;

    /* Restore TSS and IOPM */
    HalpRestoreIoPermissionsAndTask();
    
    /* Restore low memory mapping */
    HalpUnmapRealModeMemory();

    /* Restore interrupts if they were previously enabled */
    __writeeflags(Flags);
    return TRUE;
}

/* EOF */
