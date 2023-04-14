/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI Machine Language Interpreter (AMLI)
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "acpi.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

PAMLI_NAME_SPACE_OBJECT gpnsNameSpaceRoot;
PAMLI_RS_ACCESS_HANDLER gpRSAccessHead;
PAMLI_HEAP gpheapGlobal;

AMLI_EVHANDLE ghNotify;
AMLI_EVHANDLE ghFatal;
AMLI_EVHANDLE ghValidateTable;
AMLI_EVHANDLE ghGlobalLock;
AMLI_EVHANDLE ghCreate;
AMLI_EVHANDLE ghDestroyObj;
KSPIN_LOCK gdwGHeapSpinLock;
KSPIN_LOCK gdwGContextSpinLock;
NPAGED_LOOKASIDE_LIST AMLIContextLookAsideList;
AMLI_CONTEXT_QUEUE gReadyQueue;
AMLI_MUTEX gmutCtxtList;
AMLI_MUTEX gmutOwnerList;
AMLI_MUTEX gmutHeap;
AMLI_MUTEX gmutSleep;

LONG giIndent;
LONG gdwcCTObjs;
ULONG AMLIMaxCTObjs;
ULONG gdwCtxtBlkSize = 0x2000;
ULONG gdwGlobalHeapBlkSize = 0x10000;
ULONG gdwfAMLIInit;
ULONG gdwfHacks;
ULONG gdwcCTObjsMax;
ULONG gdwcHPObjs;
ULONG gdwcMemObjs;
ULONG gdwcNSObjs;
ULONG gdwGlobalHeapSize;
ULONG gdwLocalHeapMax;
ULONG gdwcSDObjs;
ULONG gdwcMTObjs;
ULONG gdwGHeapSnapshot;
ULONG gdwfAMLI;
ULONG gdwcOOObjs;
ULONG gdwcODObjs;
ULONG gdwLocalStackMax;
ULONG gdwcCRObjs;
ULONG gdwcORObjs;
ULONG gdwcFObjs;
ULONG gdwcIFObjs;
ULONG gdwcKFObjs;
ULONG gdwcBDObjs;
ULONG gdwcPKObjs;
ULONG gdwcFUObjs;
ULONG gdwcEVObjs;
ULONG gdwcMEObjs;
ULONG gdwcPRObjs;
ULONG gdwcPCObjs;
ULONG gdwcBFObjs;
ULONG gdwcRSObjs;
BOOLEAN g_AmliHookEnabled;

#if 1
AMLI_TERM atAlias        = { "Alias",            0x06,       "NN",     1, 0,    NULL, NULL, Alias };
AMLI_TERM atName         = { "Name",             0x08,       "NO",     1, 0,    NULL, NULL, Name };
AMLI_TERM atScope        = { "Scope",            0x10,       "N",      1, 1,    NULL, NULL, Scope };
AMLI_TERM atBankField    = { "BankField",        0x875B,     "NNCB",   2, 1,    NULL, NULL, BankField };
AMLI_TERM atBitField     = { "CreateBitField",   0x8D,       "CCN",    2, 0,    NULL, NULL, CreateBitField };
AMLI_TERM atByteField    = { "CreateByteField",  0x8C,       "CCN",    2, 0,    NULL, NULL, CreateByteField };
AMLI_TERM atDWordField   = { "CreateDWordField", 0x8A,       "CCN",    2, 0,    NULL, NULL, CreateDWordField };
AMLI_TERM atCreateField  = { "CreateField",      0x135B,     "CCCN",   2, 0,    NULL, NULL, CreateField };
AMLI_TERM atWordField    = { "CreateWordField",  0x8B,       "CCN",    2, 0,    NULL, NULL, CreateWordField };
AMLI_TERM atDevice       = { "Device",           0x825B,     "N",      2, 1,    NULL, NULL, Device };
AMLI_TERM atEvent        = { "Event",            0x025B,     "N",      2, 0,    NULL, NULL, Event };
AMLI_TERM atField        = { "Field",            0x815B,     "NB",     2, 1,    NULL, NULL, Field };
AMLI_TERM atIndexField   = { "IndexField",       0x865B,     "NNB",    2, 1,    NULL, NULL, IndexField };
AMLI_TERM atMethod       = { "Method",           0x14,       "NB",     2, 1,    NULL, NULL, Method };
AMLI_TERM atMutex        = { "Mutex",            0x015B,     "NB",     2, 0,    NULL, NULL, Mutex };
AMLI_TERM atOpRegion     = { "OperationRegion",  0x805B,     "NBCC",   2, 0,    NULL, NULL, OpRegion };
AMLI_TERM atPowerRes     = { "PowerResource"  ,  0x845B,     "NBW",    2, 1,    NULL, NULL, PowerRes };
AMLI_TERM atProcessor    = { "Processor",        0x835B,     "NBDB",   2, 1,    NULL, NULL, Processor };
AMLI_TERM atThermalZone  = { "ThermalZone",      0x855B,     "N",      2, 1,    NULL, NULL, ThermalZone };
AMLI_TERM atBreak        = { "Break",            0xA5,       NULL,     3, 0,    NULL, NULL, Break };
AMLI_TERM atBreakPoint   = { "BreakPoint",       0xCC,       NULL,     3, 0,    NULL, NULL, BreakPoint };
AMLI_TERM atElse         = { "Else",             0xA1,       NULL,     3, 1,    NULL, NULL, IfElse };
AMLI_TERM atFatal        = { "Fatal",            0x325B,     "BDC",    3, 0,    NULL, NULL, Fatal };
AMLI_TERM atIf           = { "If",               0xA0,       "C",      3, 1,    NULL, NULL, IfElse };
AMLI_TERM atLoad         = { "Load",             0x205B,     "NS",     3, 0,    NULL, NULL, Load };
AMLI_TERM atNOP          = { "NoOp",             0xA3,       NULL,     3, 0,    NULL, NULL, NULL };
AMLI_TERM atNotify       = { "Notify",           0x86,       "SC",     3, 0,    NULL, NULL, Notify };
AMLI_TERM atRelease      = { "Release",          0x275B,     "S",      3, 0,    NULL, NULL, ReleaseResetSignalUnload };
AMLI_TERM atReset        = { "Reset",            0x265B,     "S",      3, 0,    NULL, NULL, ReleaseResetSignalUnload };
AMLI_TERM atReturn       = { "Return",           0xA4,       "C",      3, 0,    NULL, NULL, Return };
AMLI_TERM atSignal       = { "Signal",           0x245B,     "S",      3, 0,    NULL, NULL, ReleaseResetSignalUnload };
AMLI_TERM atSleep        = { "Sleep",            0x225B,     "C",      3, 0,    NULL, NULL, SleepStall };
AMLI_TERM atStall        = { "Stall",            0x215B,     "C",      3, 0,    NULL, NULL, SleepStall };
AMLI_TERM atUnload       = { "Unload",           0x2A5B,     "S",      3, 0,    NULL, NULL, ReleaseResetSignalUnload };
AMLI_TERM atWhile        = { "While",            0xA2,       "C",      3, 1,    NULL, NULL, While };
AMLI_TERM atAcquire      = { "Acquire",          0x235B,     "SW",     4, 0,    NULL, NULL, Acquire };
AMLI_TERM atAdd          = { "Add",              0x72,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atAnd          = { "And",              0x7B,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atBuffer       = { "Buffer",           0x11,       "C",      4, 1,    NULL, NULL, Buffer };
AMLI_TERM atConcat       = { "Concatenate",      0x73,       "CCS",    4, 0,    NULL, NULL, Concat };
AMLI_TERM atCondRefOf    = { "CondRefOf",        0x125B,     "sS",     3, 0,    NULL, NULL, CondRefOf };
AMLI_TERM atDecrement    = { "Decrement",        0x76,       "S",      4, 0,    NULL, NULL, IncDec };
AMLI_TERM atDerefOf      = { "DerefOf",          0x83,       "C",      4, 0,    NULL, NULL, DerefOf };
AMLI_TERM atDivide       = { "Divide",           0x78,       "CCSS",   4, 0,    NULL, NULL, Divide };
AMLI_TERM atFindSetLBit  = { "FindSetLeftBit",   0x81,       "CS",     4, 0,    NULL, NULL, ExprOp1 };
AMLI_TERM atFindSetRBit  = { "FindSetRigtBit",   0x82,       "CS",     4, 0,    NULL, NULL, ExprOp1 };
AMLI_TERM atFromBCD      = { "FromBCD",          0x285B,     "CS",     4, 0,    NULL, NULL, ExprOp1 };
AMLI_TERM atIncrement    = { "Increment",        0x75,       "S",      4, 0,    NULL, NULL, IncDec };
AMLI_TERM atIndex        = { "Index",            0x88,       "CCS",    4, 0x80, NULL, NULL, Index };
AMLI_TERM atLAnd         = { "Land",             0x90,       "CC",     4, 0,    NULL, NULL, LogOp2 };
AMLI_TERM atLEq          = { "LEqual",           0x93,       "CC",     4, 0,    NULL, NULL, LogOp2 };
AMLI_TERM atLG           = { "LGreater",         0x94,       "CC",     4, 0,    NULL, NULL, LogOp2 };
AMLI_TERM atLL           = { "LLess",            0x95,       "CC",     4, 0,    NULL, NULL, LogOp2 };
AMLI_TERM atLNot         = { "LNot",             0x92,       "C",      4, 0,    NULL, NULL, LNot };
AMLI_TERM atLOr          = { "LOr",              0x91,       "CC",     4, 0,    NULL, NULL, LogOp2 };
AMLI_TERM atMatch        = { "Match",            0x89,       "CBCBCC", 4, 0,    NULL, NULL, Match };
AMLI_TERM atMultiply     = { "Multiply",         0x77,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atNAnd         = { "NAnd",             0x7C,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atNOr          = { "NOr",              0x7E,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atNot          = { "Not",              0x80,       "CS",     4, 0,    NULL, NULL, ExprOp1 };
AMLI_TERM atObjType      = { "ObjectType",       0x8E,       "S",      4, 0,    NULL, NULL, ObjTypeSizeOf };
AMLI_TERM atOr           = { "Or",               0x7D,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atOSI          = { "OSI",              0xCA,       "S",      4, 0,    NULL, NULL, OSInterface };
AMLI_TERM atPackage      = { "Package",          0x12,       "B",      4, 1,    NULL, NULL, Package };
AMLI_TERM atRefOf        = { "RefOf",            0x71,       "S",      4, 0,    NULL, NULL, RefOf };
AMLI_TERM atShiftLeft    = { "ShiftLeft",        0x79,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atShiftRight   = { "ShiftRight",       0x7A,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atSizeOf       = { "SizeOf",           0x87,       "S",      4, 0,    NULL, NULL, ObjTypeSizeOf };
AMLI_TERM atStore        = { "Store",            0x70,       "CS",     4, 0,    NULL, NULL, Store };
AMLI_TERM atSubtract     = { "Subtract",         0x74,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atToBCD        = { "ToBCD",            0x295B,     "CS",     4, 0,    NULL, NULL, ExprOp1 };
AMLI_TERM atWait         = { "Wait",             0x255B,     "SC",     4, 0,    NULL, NULL, Wait };
AMLI_TERM atXOr          = { "XOr",              0x7F,       "CCS",    4, 0,    NULL, NULL, ExprOp2 };
AMLI_TERM atNameObj      = { NULL,               0xFFFFFFFF, NULL,     5, 0x20, NULL, NULL, NULL };
AMLI_TERM atDataObj      = { NULL,               0xFFFFFFFF, NULL,     5, 8,    NULL, NULL, NULL };
AMLI_TERM atString       = { NULL,               0x0D,       NULL,     5, 0x10, NULL, NULL, NULL };
AMLI_TERM atArgObj       = { NULL,               0xFFFFFFFF, NULL,     5, 2,    NULL, NULL, NULL };
AMLI_TERM atLocalObj     = { NULL,               0xFFFFFFFF, NULL,     5, 4,    NULL, NULL, NULL };
AMLI_TERM atDebugObj     = { "Debug",            0x315B,     NULL,     5, 0x40, NULL, NULL, NULL };
#endif

#if 1
PAMLI_TERM OpcodeTable[0x100] =
{
    &atDataObj,
    &atDataObj,
    NULL,
    NULL,
    NULL,
    NULL,
    &atAlias,
    NULL,
    &atName,
    NULL,
    &atDataObj,
    &atDataObj,
    &atDataObj,
    &atString,
    NULL,
    NULL,
    &atScope,
    &atBuffer,
    &atPackage,
    NULL,
    &atMethod,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &atNameObj,
    &atNameObj,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    &atNameObj,
    NULL,
    &atNameObj,
    NULL,
    &atNameObj,
    &atNameObj,
    &atLocalObj,
    &atLocalObj,
    &atLocalObj,
    &atLocalObj,
    &atLocalObj,
    &atLocalObj,
    &atLocalObj,
    &atLocalObj,
    &atArgObj,
    &atArgObj,
    &atArgObj,
    &atArgObj,
    &atArgObj,
    &atArgObj,
    &atArgObj,
    NULL,
    &atStore,
    &atRefOf,
    &atAdd,
    &atConcat,
    &atSubtract,
    &atIncrement,
    &atDecrement,
    &atMultiply,
    &atDivide,
    &atShiftLeft,
    &atShiftRight,
    &atAnd,
    &atNAnd,
    &atOr,
    &atNOr,
    &atXOr,
    &atNot,
    &atFindSetLBit,
    &atFindSetRBit,
    &atDerefOf,
    NULL,
    NULL,
    &atNotify,
    &atSizeOf,
    &atIndex,
    &atMatch,
    &atDWordField,
    &atWordField,
    &atByteField,
    &atBitField,
    &atObjType,
    NULL,
    &atLAnd,
    &atLOr,
    &atLNot,
    &atLEq,
    &atLG,
    &atLL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &atIf,
    &atElse,
    &atWhile,
    &atNOP,
    &atReturn,
    &atBreak,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &atOSI,
    NULL,
    &atBreakPoint,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &atDataObj
};
#endif

#if 1
AMLI_TERM_EX ExOpcodeTable[] =
{
    { 0x01, &atMutex       }, // (1)
    { 0x02, &atEvent       }, // (2)
    { 0x12, &atCondRefOf   }, // (18)
    { 0x13, &atCreateField }, // (19)
    { 0x20, &atLoad        }, // (32)
    { 0x21, &atStall       }, // (33)
    { 0x22, &atSleep       }, // (34)
    { 0x23, &atAcquire     }, // (35)
    { 0x24, &atSignal      }, // (36)
    { 0x25, &atWait        }, // (37)
    { 0x26, &atReset       }, // (38)
    { 0x27, &atRelease     }, // (39)
    { 0x28, &atFromBCD     }, // (40)
    { 0x29, &atToBCD       }, // (41)
    { 0x2A, &atUnload      }, // (42)
    { 0x30, &atDataObj     }, // (48)
    { 0x31, &atDebugObj    }, // (49)
    { 0x32, &atFatal       }, // (50)
    { 0x80, &atOpRegion    }, // (128)
    { 0x81, &atField       }, // (129)
    { 0x82, &atDevice      }, // (130)
    { 0x83, &atProcessor   }, // (131)
    { 0x84, &atPowerRes    }, // (132)
    { 0x85, &atThermalZone }, // (133)
    { 0x86, &atIndexField  }, // (134)
    { 0x87, &atBankField   }, // (135)
    { 0x00, NULL           }
};
#endif

UCHAR OSIAML[] = { 0xA4, 0xCA, 0x68 };
PCHAR gpszOSName = "Microsoft Windows NT";

/* 5.3.1 Predefined (under) Root Namespaces (Table 5-40) */
PCHAR ObjTags[5] = {
    "_GPE", // General purpose events (GP_STS)
    "_PR",  // Processor Tree
    "_SB",  // System bus tree
    "_SI",  // System Indicators
    "_TZ"   // Thermal Zone
};

/* STRING FUNCTIONS *********************************************************/

ULONG
__cdecl
StrLen(
    _In_ PCHAR psz,
    _In_ ULONG nx)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

/* FUNCTIONS ****************************************************************/

VOID
__cdecl
InitializeMutex(
    _In_ PAMLI_MUTEX AmliSpinLock)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* CALLBACKS TERM HANDLERS **************************************************/

PAMLI_RS_ACCESS_HANDLER
__cdecl
FindRSAccess(
    _In_ ULONG RegionSpace)
{
    PAMLI_RS_ACCESS_HANDLER RsAccess;

    giIndent++;

    for (RsAccess = gpRSAccessHead;
         RsAccess && RsAccess->RegionSpace != RegionSpace;
         RsAccess = RsAccess->Next)
    {
        ;
    }

    giIndent--;

    return RsAccess;
}

/* TERM HANDLERS ************************************************************/

#if 1
NTSTATUS __cdecl Acquire(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Alias(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl BankField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Break(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl BreakPoint(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Buffer(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Concat(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl CondRefOf(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl CreateBitField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl CreateByteField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl CreateDWordField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl CreateField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl CreateWordField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl DerefOf(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Device(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Divide(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Event(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl ExprOp1(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl ExprOp2(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Fatal(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Field(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl IfElse(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl IncDec(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Index(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl IndexField(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl LNot(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Load(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl LogOp2(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Match(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Method(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Mutex(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Name(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Notify(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl ObjTypeSizeOf(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl OpRegion(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl OSInterface(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Package(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl PowerRes(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Processor(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl RefOf(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl ReleaseResetSignalUnload(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Return(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Scope(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl SleepStall(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Store(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl ThermalZone(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Wait(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl While(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
#endif

/* PCI HANDLER FUNCTIONS ****************************************************/

NTSTATUS
 __cdecl
 PciConfigSpaceHandler(
    _In_ int Param1,
    _In_ int Param2,
    _In_ int Param3,
    _In_ int Param4,
    _In_ int Param5,
    _In_ int Param6,
    _In_ int Param7,
    _In_ int Param8)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* FUNCTIONS ****************************************************************/

PAMLI_TERM
__cdecl
FindOpcodeTerm(
    _In_ ULONG Opcode,
    _In_ PAMLI_TERM_EX OpcodeTable)
{
    PAMLI_TERM Term = 0;

    DPRINT("FindOpcodeTerm: %X, %X\n", Opcode, OpcodeTable);

    giIndent++;

    while (OpcodeTable->AmliTerm)
    {
        if (Opcode == OpcodeTable->Opcode)
        {
            Term = OpcodeTable->AmliTerm;
            DPRINT("FindOpcodeTerm: Found Term %X\n", Term);
            break;
        }

        OpcodeTable++;
    }

    giIndent--;

    return Term;
}

NTSTATUS
__cdecl
RegOpcodeHandler(
    _In_ ULONG Opcode,
    _In_ PVOID Handler,
    _In_ PVOID Context,
    _In_ ULONG FlagsOpcode)
{
    PAMLI_TERM Term;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("RegOpcodeHandler: %X, %X, %X, %X\n", Opcode, Handler, Context, FlagsOpcode);

    giIndent++;

    if ((UCHAR)Opcode == 0x5B) // (91)
    {
        Term = FindOpcodeTerm((Opcode >> 8), ExOpcodeTable);
    }
    else
    {
        Term = OpcodeTable[Opcode];
    }

    if (Term)
    {
        if (Term->CallBack && Handler)
        {
            DPRINT1("AMLIRegEventHandler: opcode or opcode class already has a handler\n");
            ASSERT(FALSE);
            Status = STATUS_ACPI_HANDLER_COLLISION;
        }
        else
        {
            Term->Context = Context;
            Term->Flags2 |= FlagsOpcode;
            Term->CallBack = Handler;
        }
    }
    else
    {
        DPRINT1("AMLIRegEventHandler: either invalid opcode or opcode does not allow callback\n");
        ASSERT(FALSE);
        Status = STATUS_ACPI_REG_HANDLER_FAILED;
    }

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
RegEventHandler(
    _In_ PAMLI_EVHANDLE EventHandler,
    _In_ PVOID Handler,
    _In_ PVOID Context)
{
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("RegEventHandler: %X, %X, %X\n", EventHandler, Handler, Context);

    giIndent++;

    if (EventHandler->Handler && Handler)
    {
        DPRINT1("RegEventHandler: RegEventHandler: event handler already exist");
        ASSERT(FALSE);
        Status = STATUS_ACPI_HANDLER_COLLISION;
    }
    else
    {
        EventHandler->Handler = Handler;
        EventHandler->Context = Context;
    }

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
RegRSAccess(
    _In_ ULONG RegionSpace,
    _In_ PVOID FnHandler,
    _In_ PVOID Param,
    _In_ BOOLEAN IsRaw)
{
    PAMLI_RS_ACCESS_HANDLER RsAccess;
    NTSTATUS Status = 0;

    DPRINT("RegRSAccess: %X, %X, %X, %X\n", RegionSpace, FnHandler, Param, IsRaw);

    giIndent++;

    if (RegionSpace == 0 || RegionSpace == 1)
    {
        DPRINT1("RegRSAccess: illegal region space %X\n", RegionSpace);
        ASSERT(FALSE);
        Status = STATUS_ACPI_INVALID_REGION;
        goto Exit;
    }

    RsAccess = FindRSAccess(RegionSpace);
    if (!RsAccess)
    {
        gdwcRSObjs++;
        gdwcMemObjs++;

        RsAccess = ExAllocatePoolWithTag(NonPagedPool, sizeof(*RsAccess), 'RlmA');
        if (!RsAccess)
        {
            DPRINT1("RegRSAccess: failed to allocate handler structure\n");
            ASSERT(FALSE);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        RtlZeroMemory(RsAccess, sizeof(*RsAccess));

        RsAccess->RegionSpace = RegionSpace;
        RsAccess->Next = gpRSAccessHead;
        gpRSAccessHead = RsAccess;
    }

    if (IsRaw)
    {
        if (RsAccess->RawAccessHandler && FnHandler)
        {
            DPRINT1("RegRSAccess: RawAccess for RegionSpace %X already have a handler\n", RegionSpace);
            ASSERT(FALSE);
            Status = STATUS_ACPI_HANDLER_COLLISION;
        }
        else
        {
            RsAccess->RawAccessHandler = FnHandler;
            RsAccess->RawAccessParam = Param;
        }
    }
    else if (RsAccess->CookAccessHandler && FnHandler)
    {
        DPRINT1("RegRSAccess: CookAccess for RegionSpace %x already have a handler\n", RegionSpace);
        ASSERT(FALSE);
        Status = STATUS_ACPI_HANDLER_COLLISION;
    }
    else
    {
        RsAccess->CookAccessHandler = FnHandler;
        RsAccess->CookAccessParam = Param;
    }

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
AMLIRegEventHandler(
    _In_ ULONG EventType,
    _In_ ULONG EventData,
    _In_ PVOID Handler,
    _In_ PVOID Context)
{
    NTSTATUS Status;

    DPRINT("AMLIRegEventHandler: %X, %X, %X, %X\n", EventType, EventData, Handler, Context);

    giIndent++;

    if (g_AmliHookEnabled)
    {
        DPRINT1("AMLIRegEventHandler: FIXME\n");
        ASSERT(FALSE);
    }

    switch (EventType)
    {
        case 1:
            Status = RegOpcodeHandler(EventData, Handler, Context, 0);
            break;

        case 0xC:
            Status = RegOpcodeHandler(EventData, Handler, Context, 0x80000000);
            break;

        case 2:
            Status = RegEventHandler(&ghNotify, Handler, Context);
            break;

        case 3:
            Status = RegEventHandler(&ghFatal, Handler, Context);
            break;

        case 4:
            Status = RegEventHandler(&ghValidateTable, Handler, Context);
            break;

        case 5:
            Status = RegEventHandler(&ghGlobalLock, Handler, Context);
            break;

        case 6:
            Status = RegRSAccess(EventData, Handler, Context, 0);
            break;

        case 7:
            Status = RegRSAccess(EventData, Handler, Context, 1);
            break;

        case 0xA:
            Status = RegEventHandler(&ghCreate, Handler, Context);
            break;

        case 0xB:
            Status = RegEventHandler(&ghDestroyObj, Handler, Context);
            break;

        case 8:
            DPRINT1("AMLIRegEventHandler: FIXME\n");
            ASSERT(FALSE);
            Status = STATUS_NOT_IMPLEMENTED;
            break;

        case 9:
            DPRINT1("AMLIRegEventHandler: FIXME\n");
            ASSERT(FALSE);
            Status = STATUS_NOT_IMPLEMENTED;
            break;

        default:
            DPRINT1("AMLIRegEventHandler: FIXME\n");
            ASSERT(FALSE);
            Status = STATUS_ACPI_INVALID_EVENTTYPE;
            break;
    }

    if (Status == 0x8004)
        Status = STATUS_PENDING;

    if (g_AmliHookEnabled)
    {
        DPRINT1("AMLIRegEventHandler: FIXME\n");
        ASSERT(FALSE);
        Status = STATUS_NOT_IMPLEMENTED;
    }

    giIndent--;

   return Status;
}

VOID
__cdecl
InitHeap(
    _In_ PAMLI_HEAP Heap,
    _In_ ULONG NumberOfBytes)
{
    DPRINT("InitHeap: %X, %X\n", Heap, NumberOfBytes);

    giIndent++;

    RtlZeroMemory(Heap, NumberOfBytes);

    Heap->Signature = 'PAEH';
    Heap->HeapTop = &Heap->Heap;
    Heap->HeapEnd = Add2Ptr(Heap, NumberOfBytes);

    giIndent--;
}

NTSTATUS
__cdecl
NewHeap(
    _In_ SIZE_T NumberOfBytes,
    _In_ PAMLI_HEAP* OutHeap)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
InternalOpRegionHandler(
    _In_ ULONG AccType,
    _In_ PAMLI_NAME_SPACE_OBJECT BaseObj,
    _In_ PVOID Addr,
    _In_ ULONG AccSize,
    _In_ ULONG* OutValue,
    _In_ PAMLI_REGION_HANDLER Handler,
    _In_ PVOID Callback,
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
InternalRawAccessOpRegionHandler(
    _In_ ULONG AccType,
    _In_ PAMLI_FIELD_UNIT_OBJECT FieldUnitObj,
    _In_ PAMLI_OBJECT_DATA ObjData,
    _In_ PAMLI_REGION_HANDLER Handler,
    _In_ PVOID Callback,
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
RegisterOperationRegionHandler(
    _In_ PAMLI_NAME_SPACE_OBJECT ScopeObject,
    _In_ ULONG EventType,
    _In_ ULONG EventData,
    _In_ PVOID CallBack,
    _In_ PVOID CallBackContext,
    _In_ PVOID* OutRegionData)
{
    PAMLI_REGION_HANDLER RegionData;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("RegisterOperationRegionHandler: EventType %X\n", EventType);

    *OutRegionData = 0;

    RegionData = ExAllocatePoolWithTag(NonPagedPool, sizeof(*RegionData), 'ScpA');
    if (!RegionData)
    {
        DPRINT1("RegisterOperationRegionHandler: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RegionData->EventType = EventType;
    RegionData->EventData = EventData;

    RegionData->CallBack = CallBack;
    RegionData->CallBackContext = CallBackContext;

    if (EventType == 6)
    {
        Status = AMLIRegEventHandler(6, EventData, InternalOpRegionHandler, RegionData);
    }
    else
    {
        if (EventType != 7)
        {
            DPRINT1("RegisterOperationRegionHandler: STATUS_INVALID_PARAMETER\n");
            ExFreePoolWithTag(RegionData, 'ScpA');
            return STATUS_INVALID_PARAMETER;
        }

        Status = AMLIRegEventHandler(7, EventData, InternalRawAccessOpRegionHandler, RegionData);
    }

    if (Status)
    {
        DPRINT1("RegisterOperationRegionHandler: Status %X\n", Status);
        ExFreePoolWithTag(RegionData, 'ScpA');
        return STATUS_UNSUCCESSFUL;
    }

    *OutRegionData = RegionData;

    if (!ScopeObject)
        return STATUS_SUCCESS;

    DPRINT1("RegisterOperationRegionHandler: ScopeObject %p\n", ScopeObject);
    ASSERT(FALSE);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
OSInitializeCallbacks(VOID)
{
    PVOID RegionData;
    NTSTATUS Status;

    DPRINT("OSInitializeCallbacks()\n");

    Status = AMLIRegEventHandler(0xC, 0x205B, ACPICallBackLoad, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSInitializeCallbacks: Status %X\n", Status);
        DbgBreakPoint();
    }

    Status = AMLIRegEventHandler(0xC, 0x2A5B, ACPICallBackUnload, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSInitializeCallbacks: Status %X\n", Status);
        DbgBreakPoint();
    }

    Status = AMLIRegEventHandler(0xB, 0, ACPITableNotifyFreeObject, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSInitializeCallbacks: Status %X\n", Status);
        DbgBreakPoint();
    }

    Status = AMLIRegEventHandler(2, 0, NotifyHandler, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSInitializeCallbacks: Status %X\n", Status);
        DbgBreakPoint();
    }

    Status = AMLIRegEventHandler(5, 0, GlobalLockEventHandler, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSInitializeCallbacks: Status %X\n", Status);
        DbgBreakPoint();
    }

    Status = AMLIRegEventHandler(0xA, 0, OSNotifyCreate, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSInitializeCallbacks: Status %X\n", Status);
        DbgBreakPoint();
    }

    Status = AMLIRegEventHandler(3, 0, OSNotifyFatalError, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OSInitializeCallbacks: Status %X\n", Status);
        DbgBreakPoint();
    }

    return RegisterOperationRegionHandler(NULL, 6, 2, PciConfigSpaceHandler, 0, &RegionData);
}

PVOID
__cdecl
HeapAlloc(
    _In_ PAMLI_HEAP InHeap,
    _In_ ULONG NameSeg,
    _In_ ULONG Length)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

NTSTATUS
__cdecl
CreateNameSpaceObject(
    _In_ PAMLI_HEAP Heap,
    _In_ PCHAR Name,
    _In_ PAMLI_NAME_SPACE_OBJECT NsScope,
    _In_ PAMLI_OBJECT_OWNER Owner,
    _In_ PAMLI_NAME_SPACE_OBJECT* OutObject,
    _In_ ULONG Flags)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
InitMutex(
    _In_ PAMLI_HEAP Heap,
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ ULONG Level)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
StartTimeSlicePassive(
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
__cdecl
AMLIInitialize(
    _In_ ULONG CtxtBlkSize,
    _In_ ULONG GlobalHeapBlkSize,
    _In_ ULONG InitFlags,
    _In_ ULONG TimeSliceLen,
    _In_ ULONG TimeSliceInterval,
    _In_ ULONG MaxContextsDepth)
{
    PAMLI_NAME_SPACE_OBJECT NsObject;
    PAMLI_METHOD_OBJECT MethodObject;
    ULONG ix;
    NTSTATUS Status;

    DPRINT("AMLIInitialize: %X, %X, %X, %X, %X\n", InitFlags, CtxtBlkSize, GlobalHeapBlkSize, TimeSliceLen, TimeSliceInterval);

    giIndent++;

    if (gpnsNameSpaceRoot)
    {
        DPRINT1("AMLIInitialize: interpreter already initialized\n");
        ASSERT(FALSE);
        Status = STATUS_ACPI_ALREADY_INITIALIZED;
        goto Exit;
    }

    if (!CtxtBlkSize)
        gdwCtxtBlkSize = 0x2000;
    else
        gdwCtxtBlkSize = CtxtBlkSize;

    if (!GlobalHeapBlkSize)
        gdwGlobalHeapBlkSize = 0x10000;
    else
        gdwGlobalHeapBlkSize = GlobalHeapBlkSize;

    gdwfAMLIInit = InitFlags;

    DPRINT("AMLIInitialize: FIXME GetHackFlags()\n");

    if (MaxContextsDepth <= 0x10)
        gdwcCTObjsMax = 0x10;
    else if (MaxContextsDepth > 0x400)
        gdwcCTObjsMax = 0x400;
    else
        gdwcCTObjsMax = MaxContextsDepth;

    KeInitializeSpinLock(&gdwGHeapSpinLock);
    KeInitializeSpinLock(&gdwGContextSpinLock);

    ExInitializeNPagedLookasideList(&AMLIContextLookAsideList, NULL, NULL, 0, gdwCtxtBlkSize, 'ClmA', gdwcCTObjsMax);

    Status = NewHeap(gdwGlobalHeapBlkSize, &gpheapGlobal);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("AMLIInitialize: Status %X\n", Status);
        goto Exit1;
    }

    gpheapGlobal->HeapHead = gpheapGlobal;

    InitializeMutex(&gmutHeap);

    /* The root of the namespace */
    Status = CreateNameSpaceObject(gpheapGlobal, "\\", NULL, NULL, NULL, 0);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("AMLIInitialize: Status %X\n", Status);
        goto Exit1;
    }

    ix = 0;

    while (TRUE)
    {
        Status = CreateNameSpaceObject(gpheapGlobal, ObjTags[ix], NULL, NULL, NULL, 0);
        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("AMLIInitialize: Status %X\n", Status);
            break;
        }

        ix++;
        if (ix < 5)
            continue;

        /* Revision of the ACPI specification that OSPM implements */
        Status = CreateNameSpaceObject(gpheapGlobal, "_REV", NULL, NULL, &NsObject, 0);
        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("AMLIInitialize: Status %X\n", Status);
            break;
        }

        NsObject->ObjData.DataType = 1;
        NsObject->ObjData.DataValue = ULongToPtr(1);

        /* Name of the operating system */
        Status = CreateNameSpaceObject(gpheapGlobal, "_OS", NULL, NULL, &NsObject, 0);
        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("AMLIInitialize: Status %X\n", Status);
            break;
        }

        NsObject->ObjData.DataType = 2;
        NsObject->ObjData.DataLen = StrLen(gpszOSName, 0xFFFFFFFF) + 1;

        gdwcSDObjs++;

        NsObject->ObjData.DataBuff = HeapAlloc(gpheapGlobal, 'RTSH', NsObject->ObjData.DataLen);
        if (!NsObject->ObjData.DataBuff)
        {
            DPRINT1("AMLIInitialize: failed to allocate \\_OS name object\n");
            ASSERT(FALSE);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        RtlCopyMemory(NsObject->ObjData.DataBuff, gpszOSName, NsObject->ObjData.DataLen);

        /* Operating System Interface support */
        Status = CreateNameSpaceObject(gpheapGlobal, "_OSI", NULL, NULL, &NsObject, 0);
        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("AMLIInitialize: Status %X\n", Status);
            break;
        }

        NsObject->ObjData.DataType = 8;
        NsObject->ObjData.DataLen = 0x16;

        gdwcSDObjs++;

        NsObject->ObjData.DataBuff = MethodObject = HeapAlloc(gpheapGlobal, 'RTSH', NsObject->ObjData.DataLen);
        if (!MethodObject)
        {
            DPRINT1("AMLIInitialize: failed to allocate \\_OSI name object\n");
            ASSERT(FALSE);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        RtlZeroMemory(MethodObject, NsObject->ObjData.DataLen);

        MethodObject->MethodFlags = 1;
        RtlCopyMemory(MethodObject->CodeBuff, OSIAML, sizeof(OSIAML));

        /* Global Lock */
        Status = CreateNameSpaceObject(gpheapGlobal, "_GL", NULL, NULL, &NsObject, 0);
        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("AMLIInitialize: Status %X\n", Status);
            break;
        }

        NsObject->ObjData.Flags = 2;

        Status = InitMutex(gpheapGlobal, NsObject, 0);
        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("AMLIInitialize: Status %X\n", Status);
            break;
        }

        gReadyQueue.TimeSliceLength = TimeSliceLen;
        if (!TimeSliceLen)
            gReadyQueue.TimeSliceLength = 100;

        gReadyQueue.TimeSliceInterval = TimeSliceInterval;
        if (!TimeSliceInterval)
            gReadyQueue.TimeSliceInterval = 100;

        KeInitializeTimer(&gReadyQueue.Timer);
        InitializeMutex(&gReadyQueue.Mutex);

        gReadyQueue.WorkItem.WorkerRoutine = StartTimeSlicePassive;
        gReadyQueue.WorkItem.Parameter = &gReadyQueue;
        gReadyQueue.WorkItem.List.Flink = 0;

        InitializeMutex(&gmutCtxtList);
        InitializeMutex(&gmutOwnerList);
        InitializeMutex(&gmutSleep);

        DPRINT("AMLIInitialize: FIXME\n");
        ASSERT(FALSE);

        break;
    }

Exit1:

    if (Status == 0x8004)
        Status = STATUS_PENDING;

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
NTAPI
ACPIInitializeAMLI(VOID)
{
    ULONG CtxtBlkSize;
    ULONG GlobalHeapBlkSize;
    ULONG InitFlags;
    ULONG TimeSliceLength;
    ULONG TimeSliceInterval;
    ULONG Length;

    PAGED_CODE();
    DPRINT("ACPIInitializeAMLI()\n");

    Length = 4;
    if (OSReadRegValue("AMLIInitFlags", 0, &InitFlags, &Length) < 0)
        InitFlags = 0;

    Length = 4;
    if (OSReadRegValue("AMLICtxtBlkSize", 0, &CtxtBlkSize, &Length) < 0)
        CtxtBlkSize = 0;

    Length = 4;
    if (OSReadRegValue("AMLIGlobalHeapBlkSize", 0, &GlobalHeapBlkSize, &Length) < 0)
        GlobalHeapBlkSize = 0;

    Length = 4;
    if (OSReadRegValue("AMLITimeSliceLength", 0, &TimeSliceLength, &Length) < 0)
        TimeSliceLength = 0;

    Length = 4;
    if (OSReadRegValue("AMLITimeSliceInterval", 0, &TimeSliceInterval, &Length) < 0)
        TimeSliceInterval = 0;

    Length = 4;
    if (OSReadRegValue("AMLIMaxCTObjs", 0, &AMLIMaxCTObjs, &Length) < 0)
        AMLIMaxCTObjs = 0;

    OSInitializeCallbacks();

    return AMLIInitialize(CtxtBlkSize, GlobalHeapBlkSize, InitFlags, TimeSliceLength, TimeSliceInterval, AMLIMaxCTObjs);
}

/* EOF */
