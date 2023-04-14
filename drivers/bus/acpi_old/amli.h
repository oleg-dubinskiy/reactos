/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI Machine Language Interpreter (AMLI) header
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

/* STRUCTURES ***************************************************************/

struct _AMLI_CONTEXT;
typedef NTSTATUS (__cdecl* PAMLI_FN_PARSE)(struct _AMLI_CONTEXT* AmliContext, PVOID Context, NTSTATUS InStatus);

typedef struct _AMLI_FRAME_HEADER
{
    ULONG Signature;
    ULONG Length;
    ULONG Flags;
    PAMLI_FN_PARSE ParseFunction;
} AMLI_FRAME_HEADER, *PAMLI_FRAME_HEADER;

typedef struct _AMLI_CONTEXT
{
    ULONG Signature;

} AMLI_CONTEXT, *PAMLI_CONTEXT;

typedef struct _AMLI_TERM_CONTEXT *PAMLI_TERM_CONTEXT;
typedef NTSTATUS (__cdecl* PAMLI_TERM_HANDLER)(PAMLI_CONTEXT AmliContext, PAMLI_TERM_CONTEXT TermCtx);

typedef struct _AMLI_TERM
{
    PCHAR Name;
    ULONG Opcode;
    PCHAR TypesOfArgs;
    ULONG Flags1;
    ULONG Flags2;
    PVOID CallBack;
    PVOID Context;
    PAMLI_TERM_HANDLER Handler;
} AMLI_TERM, *PAMLI_TERM;

typedef struct _AMLI_TERM_EX
{
    ULONG Opcode;
    PAMLI_TERM AmliTerm;
} AMLI_TERM_EX, *PAMLI_TERM_EX;

typedef struct _AMLI_EVHANDLE
{
    PVOID Handler;
    PVOID Context;
} AMLI_EVHANDLE, *PAMLI_EVHANDLE;

typedef struct _AMLI_TERM_CONTEXT
{
    AMLI_FRAME_HEADER FrameHeader;

} AMLI_TERM_CONTEXT, *PAMLI_TERM_CONTEXT;

/* FUNCTIONS ****************************************************************/

#if 1
NTSTATUS __cdecl Acquire(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Alias(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl BankField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Break(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl BreakPoint(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Buffer(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Concat(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl CondRefOf(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl CreateBitField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl CreateByteField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl CreateDWordField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl CreateField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl CreateWordField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl DerefOf(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Device(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Divide(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Event(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl ExprOp1(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl ExprOp2(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Fatal(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Field(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl IfElse(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl IncDec(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Index(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl IndexField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl LNot(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Load(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl LogOp2(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Match(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Method(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Mutex(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Name(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Notify(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl ObjTypeSizeOf(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl OpRegion(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl OSInterface(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Package(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl PowerRes(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Processor(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl RefOf(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl ReleaseResetSignalUnload(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Return(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Scope(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl SleepStall(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Store(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl ThermalZone(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl Wait(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
NTSTATUS __cdecl While(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext);
#endif

NTSTATUS
NTAPI
ACPIInitializeAMLI(
    VOID
);


/* EOF */
