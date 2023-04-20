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
PAMLI_LIST gplistCtxtHead;
PAMLI_LIST gplistObjOwners;

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
BOOLEAN gInitTime;

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
CHAR TmpSegString[8];

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
    ULONG ix;

    //DPRINT("StrLen: str '%s', nx %X)\n", psz, nx);

    giIndent++;

    ASSERT(psz != NULL);

    ix = 0;
    if (nx)
    {
        do
        {
            if (!*psz)
                break;

            ix++;
            psz++;
        }
        while (ix < nx);
    }

    giIndent--;

    return ix;
}

int
__cdecl
StrCmp(
    _In_ PCHAR psz1,
    _In_ PCHAR psz2,
    _In_ ULONG nx,
    _In_ BOOLEAN fMatchCase)
{
    ULONG ix;
    ULONG jx;
    PCHAR pChar2;
    char char1;
    char char2;
    ULONG Len1;
    ULONG Len2;

    //DPRINT("StrCmp: psz1 '%s', psz2 '%s', nx %X, MatchCase %X\n", psz1, psz2, nx, MatchCase);

    giIndent++;

    ASSERT(psz1 != NULL);
    ASSERT(psz2 != NULL);

    Len1 = StrLen(psz1, nx);
    Len2 = StrLen(psz2, nx);

    if (nx == 0xFFFFFFFF)
    {
        nx = Len1;

        if (Len1 <= Len2)
            nx = Len2;
    }

    ix = 0;
    jx = 0;

    if (fMatchCase)
    {
        do
        {
            if (ix >= nx)
                break;

            if (ix >= Len1)
                break;

            if (ix >= Len2)
                break;

            jx = (LONG)(psz1[ix] - psz2[ix]);

            ix++;
        }
        while (!jx);
    }
    else
    {
        pChar2 = psz2;
        do
        {
            if (ix >= nx || ix >= Len1 || ix >= Len2)
                break;

            char1 = pChar2[psz1 - psz2];
            if (char1 >= 'a' && char1 <= 'z')
                char1 &= ~0x20; // to capital letter

            char2 = *pChar2;
            if (*pChar2 >= 'a' && char2 <= 'z')
                char2 &= ~0x20; // to capital letter

            jx = (char1 - char2);

            ix++;
            pChar2++;
        }
        while (char1 == char2);
    }

    if (!jx && ix < nx)
    {
        if (ix >= Len1)
        {
            if (ix < Len2)
                jx = -psz2[ix];
        }
        else
        {
            jx = psz1[ix];
        }
    }

    giIndent--;

    return jx;
}

PCHAR
__cdecl
StrChr(
    _In_ PCHAR pszStr,
    _In_ CHAR Char)
{
    //DPRINT("StrChr: pszStr - '%s', Char -'%c'\n", pszStr, Char);

    giIndent++;

    ASSERT(pszStr != NULL);

    while (*pszStr != Char && *pszStr)
        pszStr++;

    if (*pszStr != Char)
        pszStr = NULL;

    giIndent--;

    return pszStr;
}

PCHAR
__cdecl
StrRChr(
    _In_ PCHAR pszStr,
    _In_ CHAR Char)
{
    PCHAR pChar;

    //DPRINT("StrRChr: pszStr - '%s', Char -'%c'\n", pszStr, Char);

    giIndent++;

    ASSERT(pszStr != NULL);

    pChar = &pszStr[StrLen(pszStr, 0xFFFFFFFF)];

    while (*pChar != Char)
    {
        if (pChar <= pszStr)
            break;

        pChar--;
    }

    if (*pChar != Char)
        pChar = NULL;

    giIndent--;

    return pChar;
}

PCHAR
__cdecl
StrCpy(
    _In_ PCHAR Dst,
    _In_ PCHAR Src,
    _In_ ULONG Len)
{
    ULONG SrcLen;

    giIndent++;

    ASSERT(Dst != NULL);
    ASSERT(Src != NULL);

    if (Len == 0xFFFFFFFF)
    {
        SrcLen = StrLen(Src, 0xFFFFFFFF);

        if (SrcLen < 0xFFFFFFFF)
            Len = SrcLen;
    }

    RtlCopyMemory(Dst, Src, Len);
    Dst[Len] = 0;

    giIndent--;

    return Dst;
}

PCHAR
__cdecl
StrCat(
    _In_ PCHAR Dst,
    _In_ PCHAR Src,
    _In_ ULONG nx)
{
    ULONG Len;
    ULONG SrcLen;
    PCHAR DstEnd;

    //DPRINT("StrCat: Dst '%s', Src '%s', nx %X\n", Dst, Src, nx);

    giIndent++;

    ASSERT(Dst != NULL);
    ASSERT(Src != NULL);

    SrcLen = StrLen(Src, nx);

    if (nx == -1 || nx > SrcLen)
        Len = SrcLen;
    else
        Len = nx;

    DstEnd = &Dst[StrLen(Dst, 0xFFFFFFFF)];

    RtlCopyMemory(DstEnd, Src, Len);
    DstEnd[Len] = 0;

    giIndent--;

    return Dst;
}

PCHAR
__cdecl
NameSegString(
    _In_ ULONG NameSeg)
{
    giIndent++;
    RtlZeroMemory(TmpSegString, sizeof(*TmpSegString));
    StrCpy(TmpSegString, (PCHAR)&NameSeg, 4);
    giIndent--;

    return TmpSegString;
}

/* LIST FUNCTIONS ***********************************************************/

VOID
__cdecl
ListInsertTail(
    _In_ PAMLI_LIST ListEntry,
    _Out_ PAMLI_LIST* OutListHead)
{
    ASSERT(OutListHead != NULL);

    giIndent++;

    ASSERT(ListEntry != NULL);

    if (*OutListHead)
    {
        ListEntry->Next = *OutListHead;
        ListEntry->Prev = (*OutListHead)->Prev;

        (*OutListHead)->Prev->Next = ListEntry;
        (*OutListHead)->Prev = ListEntry;
    }
    else
    {
        *OutListHead = ListEntry;

        ListEntry->Next = ListEntry;
        ListEntry->Prev = ListEntry;
    }

    giIndent--;
}

VOID
__cdecl
ListInsertHead(
    _In_ PAMLI_LIST ListEntry,
    _Inout_ PAMLI_LIST* OutListHead)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
__cdecl
ListRemoveEntry(
    _In_ PAMLI_LIST ListEntry,
    _Inout_ PAMLI_LIST* OutListHead)
{
    UNIMPLEMENTED_DBGBREAK();
}

PAMLI_LIST
__cdecl
ListRemoveHead(
    _Inout_ PAMLI_LIST* OutListHead)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

/* FUNCTIONS ****************************************************************/

VOID
__cdecl
InitializeMutex(
    _In_ PAMLI_MUTEX AmliMutex)
{
    giIndent++;

    KeInitializeSpinLock(&AmliMutex->SpinLock);
    AmliMutex->OldIrql = 0;

    giIndent--;
}

VOID
__cdecl
AcquireMutex(
    _In_ PAMLI_MUTEX AmliMutex)
{
    giIndent++;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeAcquireSpinLock(&AmliMutex->SpinLock, &AmliMutex->OldIrql);

    giIndent--;
}

VOID
__cdecl
ReleaseMutex(
    _In_ PAMLI_MUTEX AmliMutex)
{
    giIndent++;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    KeReleaseSpinLock(&AmliMutex->SpinLock, AmliMutex->OldIrql);

    giIndent--;
}

PCHAR
__cdecl
GetObjectPath(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject)
{
    static CHAR TmpPath[0x100] = {0};
    int ix;

    giIndent++;

    if (!NsObject)
    {
        TmpPath[0] = 0;
        goto Exit;
    }

    if (NsObject->Parent)
    {
        GetObjectPath(NsObject->Parent);

        if (NsObject->Parent->Parent)
            StrCat(TmpPath, ".", 0xFFFFFFFF);

        StrCat(TmpPath, (PCHAR)&NsObject->NameSeg, 4);
    }
    else
    {
        StrCpy(TmpPath, "\\", 0xFFFFFFFF);
    }

    for (ix = StrLen(TmpPath, 0xFFFFFFFF); ; TmpPath[ix] = 0)
    {
        ix--;
        if (ix < 0)
            break;

        if (TmpPath[ix] != '_')
            break;
    }

Exit:

    giIndent--;

    return TmpPath;
}

PAMLI_NAME_SPACE_OBJECT
NTAPI
ACPIAmliGetNamedChild(
    _In_ PAMLI_NAME_SPACE_OBJECT AcpiObject,
    _In_ ULONG NameSeg)
{
    PAMLI_NAME_SPACE_OBJECT Child;
    PAMLI_NAME_SPACE_OBJECT Parent;

    Child = AcpiObject->FirstChild;

    while (Child && NameSeg != Child->NameSeg)
    {
        Parent = Child->Parent;

        if (Parent)
        {
            Child = (PAMLI_NAME_SPACE_OBJECT)Child->List.Next;

            if (Parent->FirstChild != Child)
                continue;
        }

        Child = NULL;
    }

    return Child;
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
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("NewHeap: NumberOfBytes %X\n", NumberOfBytes);

    giIndent++;

    gdwcHPObjs++;
    gdwcMemObjs++;

    *OutHeap = ExAllocatePoolWithTag(NonPagedPool, NumberOfBytes, 'HlmA');
    if (*OutHeap)
    {
        InitHeap(*OutHeap, NumberOfBytes);
    }
    else
    {
        DPRINT1("NewHeap: failed to allocate new heap block\n");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    giIndent--;

    return Status;
}

PAMLI_HEAP_HEADER
__cdecl
HeapFindFirstFit(
    _In_ PAMLI_HEAP Heap,
    _In_ ULONG Length)
{
    PAMLI_HEAP_HEADER RetHeader = NULL;
    PAMLI_LIST HeadList;
    PAMLI_LIST EntryList;
  
    //DPRINT("HeapFindFirstFit: Heap %X, Length %X\n", Heap, Length);

    giIndent++;

    HeadList = Heap->ListFreeHeap;
    if (HeadList)
    {
        EntryList = Heap->ListFreeHeap;

        while (TRUE)
        {
            RetHeader = CONTAINING_RECORD(EntryList, AMLI_HEAP_HEADER, List);

            if (Length <= RetHeader->Length)
                break;

            EntryList = EntryList->Next;

            if (EntryList == HeadList)
            {
                if (Length > RetHeader->Length)
                    RetHeader = NULL;

                break;
            }
        }
    }

    giIndent--;

    return RetHeader;
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

VOID
__cdecl
HeapInsertFreeList(
    _In_ PAMLI_HEAP Heap,
    _In_ PAMLI_HEAP_HEADER HeapHeader)
{
    UNIMPLEMENTED_DBGBREAK();
}

PVOID
__cdecl
HeapAlloc(
    _In_ PAMLI_HEAP InHeap,
    _In_ ULONG NameSeg,
    _In_ ULONG Length)
{
    PAMLI_HEAP_HEADER Header = NULL;
    PAMLI_HEAP_HEADER NextHeader;
    PAMLI_HEAP currentHeap;
    PAMLI_HEAP HeapPrev = NULL;
    PAMLI_HEAP heap = NULL;
    PCHAR NameSegStr;
    ULONG heapMax;
    KIRQL OldIrql;

    NameSegStr = NameSegString(NameSeg);

    DPRINT("HeapAlloc: %X, '%s', %X\n", InHeap, NameSegStr, Length);

    giIndent++;

    ASSERT(InHeap != NULL);
    ASSERT(InHeap->Signature == 'PAEH'); // SIG_HEAP
    ASSERT(InHeap->HeapHead != NULL);
    ASSERT(InHeap == InHeap->HeapHead);

    Length += FIELD_OFFSET(AMLI_HEAP_HEADER, List);

    if (Length < sizeof(AMLI_HEAP_HEADER))
        Length = sizeof(AMLI_HEAP_HEADER);

    Length = (Length + 3) & ~3;

    AcquireMutex(&gmutHeap);

    if (((ULONG_PTR)InHeap->HeapEnd - (ULONG_PTR)&InHeap->Heap) < Length)
        goto Exit;

    heap = InHeap;

    while (heap)
    {
        Header = HeapFindFirstFit(heap, Length);
        if (Header)
        {
            ASSERT(Header->Signature == 0);

            ListRemoveEntry(&Header->List, &heap->ListFreeHeap);

            if (Header->Length >= Length + sizeof(AMLI_HEAP_HEADER))
            {
                NextHeader = Add2Ptr(Header, Length);
                NextHeader->Signature = 0;
                NextHeader->Length = (Header->Length - Length);
                NextHeader->Heap = heap;

                Header->Length = Length;

                HeapInsertFreeList(heap, NextHeader);
            }

            break;
        }

        if (Length <= ((ULONG_PTR)heap->HeapEnd - (ULONG_PTR)heap->HeapTop))
        {
            Header = (PAMLI_HEAP_HEADER)heap->HeapTop;
            heap->HeapTop = Add2Ptr(Header, Length);
            Header->Length = Length;

            break;
        }

        HeapPrev = heap = heap->HeapNext;
    }

    if (!Header)
    {
        if (InHeap != gpheapGlobal || NewHeap(gdwGlobalHeapBlkSize, &heap))
            goto Exit;

        heap->HeapHead = InHeap;

        ASSERT(HeapPrev != NULL);
        HeapPrev->HeapNext = heap;

        ASSERT(Length <= ((ULONG_PTR)heap->HeapEnd - (ULONG_PTR)&heap->Heap));

        Header = (PAMLI_HEAP_HEADER)heap->HeapTop;
        heap->HeapTop = Add2Ptr(Header, Length);
        Header->Length = Length;
    }

    currentHeap = InHeap;

    if (InHeap == gpheapGlobal)
    {
        KeAcquireSpinLock(&gdwGHeapSpinLock, &OldIrql);
        gdwGlobalHeapSize += Header->Length;
        KeReleaseSpinLock(&gdwGHeapSpinLock, OldIrql);
    }
    else
    {
        heapMax = 0;
        do
        {
            heapMax += ((ULONG_PTR)currentHeap->HeapTop - (ULONG_PTR)&currentHeap->Heap);
            currentHeap = currentHeap->HeapNext;
        }
        while (currentHeap);

        if (heapMax > gdwLocalHeapMax)
            gdwLocalHeapMax = heapMax;
    }

    Header->Signature = NameSeg;
    Header->Heap = heap;

    RtlZeroMemory(&Header->List, (Length - FIELD_OFFSET(AMLI_HEAP_HEADER, List)));

Exit:

    ReleaseMutex(&gmutHeap);

    giIndent--;

    return (PVOID)(Header ? &Header->List : NULL);
}

VOID
__cdecl
InsertOwnerObjList(
    _In_ PAMLI_OBJECT_OWNER Owner,
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject)
{
    giIndent++;

    NsObject->Owner = Owner;
    if (Owner)
    {
        NsObject->OwnedNext = Owner->ObjList;
        Owner->ObjList = NsObject;
    }

    giIndent--;
}

NTSTATUS
__cdecl
GetNameSpaceObject(
    _In_ PCHAR ObjPath,
    _In_ PAMLI_NAME_SPACE_OBJECT ScopeObject,
    _In_ PAMLI_NAME_SPACE_OBJECT* OutObject,
    _In_ ULONG Flags)
{
    PAMLI_NAME_SPACE_OBJECT scopeObject;
    PAMLI_NAME_SPACE_OBJECT object;
    PCHAR EndString;
    PCHAR StartString;
    int NameSeg;
    ULONG NameSegSize;
    BOOLEAN IsTrueBoolean; // FIXME
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("GetNameSpaceObject: '%s', '%s', %X, %X\n", ObjPath, GetObjectPath(ScopeObject), OutObject, Flags);

    giIndent++;

    if (!ScopeObject)
        scopeObject = gpnsNameSpaceRoot;
    else
        scopeObject = ScopeObject;

    if (*ObjPath == '\\')
    {
        StartString = (ObjPath + 1);
        scopeObject = gpnsNameSpaceRoot;
    }
    else
    {
        StartString = ObjPath;

        while (*StartString == '^')
        {
            if (!scopeObject)
                break;

            StartString++;

            scopeObject = scopeObject->Parent;
        }
    }
  
    *OutObject = scopeObject;

    if (!scopeObject)
    {
        DPRINT1("GetNameSpaceObject: STATUS_OBJECT_NAME_NOT_FOUND\n");
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto Exit;
    }

    if (*StartString == 0)
    {
        goto Exit;
    }

    if ((Flags & 1) || *ObjPath == '\\' || *ObjPath == '^' || StrLen(ObjPath, 0xFFFFFFFF) > 4)
        IsTrueBoolean = FALSE;
    else
        IsTrueBoolean = TRUE;

    while (TRUE)
    {
        while (scopeObject->FirstChild)
        {
            EndString = StrChr(StartString, '.');

            NameSegSize = EndString ? EndString - StartString : StrLen(StartString, 0xFFFFFFFF);
            if (NameSegSize > 4)
            {
                DPRINT1("GetNameSpaceObject: invalid name '%s' (%X)\n", ObjPath, NameSegSize);
                ASSERT(FALSE);
                Status = STATUS_OBJECT_NAME_INVALID;
                goto Exit;
            }

            NameSeg = '____';
            RtlCopyMemory(&NameSeg, StartString, NameSegSize);

            object = scopeObject->FirstChild;

            while (TRUE)
            {
                if (object->NameSeg == NameSeg)
                {
                    StartString += NameSegSize;
                    scopeObject = object;

                    if (*StartString == '.')
                    {
                        StartString++;
                    }
                    else if (*StartString == 0)
                    {
                        *OutObject = object;
                        goto Exit;
                    }

                    break;
                }

                object = (PAMLI_NAME_SPACE_OBJECT)object->List.Next;
                if (object == object->Parent->FirstChild)
                {
                    goto GetParent;
                }
            }
        }

GetParent:

        if (!IsTrueBoolean || !scopeObject || !scopeObject->Parent)
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            goto Exit;
        }
        else
        {
            scopeObject = scopeObject->Parent;
            Status = 0;
        }
    }

Exit:

    if ((Flags & 0x80000000) && Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DPRINT1("GetNameSpaceObject: '%s' not found. Flags %X, Status %X\n", ObjPath, Flags, Status);
        ASSERT(FALSE);
    }

    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("GetNameSpaceObject: Status %X\n", Status);
        *OutObject = NULL;
    }

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
AMLIGetNameSpaceObject(
    _In_ PCHAR ObjPath,
    _In_ PAMLI_NAME_SPACE_OBJECT ScopeObject,
    _Out_ PAMLI_NAME_SPACE_OBJECT* OutNsObject,
    _In_ ULONG Flags)
{
    NTSTATUS Status;

    DPRINT("AMLIGetNameSpaceObject: '%s', '%s', %X,  %X\n", ObjPath, GetObjectPath(ScopeObject), ScopeObject, Flags);

    giIndent++;

    ASSERT(ObjPath != NULL);
    ASSERT(*ObjPath != '\\0');
    ASSERT(OutNsObject != NULL);

    if (g_AmliHookEnabled)
    {
        DPRINT1("AMLIGetNameSpaceObject: FIXME");
        ASSERT(FALSE);
    }

    if (ScopeObject && (ScopeObject->ObjData.Flags & 4))
    {
        DPRINT1("AMLIGetNameSpaceObject: ScopeObject is no longer valid");
        ASSERT(FALSE);
        Status = STATUS_NO_SUCH_DEVICE;
    }
    else
    {
        Status = GetNameSpaceObject(ObjPath, ScopeObject, OutNsObject, Flags);
        if (Status == 0x8004)
            Status = STATUS_PENDING;
    }

    if (g_AmliHookEnabled)
    {
        DPRINT1("AMLIGetNameSpaceObject: FIXME");
        ASSERT(FALSE);
    }

    giIndent--;

    return Status;
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
    PAMLI_NAME_SPACE_OBJECT NsObject = NULL;
    PAMLI_NAME_SPACE_OBJECT ParentObject;
    ULONG nameSegSize;
    PCHAR nameSeg;
    PCHAR name;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Name)
        name = "<null>";
    else
        name = Name;

    DPRINT("CreateNameSpaceObject: %X, '%s', %X, %X, %X, %X\n", Heap, name, NsScope, Owner, OutObject, Flags);

    giIndent++;

    if (!NsScope)
        NsScope = gpnsNameSpaceRoot;

    if (!Name)
    {
        gdwcNSObjs++;

        NsObject = HeapAlloc(Heap, 'OSNH', sizeof(AMLI_NAME_SPACE_OBJECT));
        if (!NsObject)
        {
            DPRINT1("CreateNameSpaceObject: fail to allocate name space object");
            ASSERT(FALSE);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        ASSERT(gpnsNameSpaceRoot != NULL);

        RtlZeroMemory(NsObject, sizeof(AMLI_NAME_SPACE_OBJECT));

        NsObject->Parent = NsScope;
        InsertOwnerObjList(Owner, NsObject);

        ListInsertTail(&NsObject->List, &NsScope->FirstChild->List.Prev);

        if (OutObject)
            *OutObject = NsObject;

        goto Exit;
    }

    if (*Name)
    {
        Status = GetNameSpaceObject(Name, NsScope, &NsObject, 1);
        if (Status == STATUS_SUCCESS)
        {
            if (!(Flags & 0x10000))
            {
                DPRINT1("CreateNameSpaceObject: object already exist - %s", Name);
                ASSERT(FALSE);
                Status = STATUS_OBJECT_NAME_COLLISION;
            }

            if (OutObject)
                *OutObject = NsObject;

            goto Exit;
        }

        if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
            goto Exit;
    }

    Status = STATUS_SUCCESS;

    if (!StrCmp(Name, "\\", 0xFFFFFFFF, TRUE))
    {
        ASSERT(gpnsNameSpaceRoot == NULL);
        ASSERT(Owner == NULL);

        gdwcNSObjs++;

        NsObject = HeapAlloc(Heap, 'OSNH', sizeof(AMLI_NAME_SPACE_OBJECT));
        if (!NsObject)
        {
            DPRINT1("CreateNameSpaceObject: fail to allocate name space object");
            ASSERT(FALSE);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        RtlZeroMemory(NsObject, sizeof(AMLI_NAME_SPACE_OBJECT));

        NsObject->NameSeg = '___\\';
        gpnsNameSpaceRoot = NsObject;

        DPRINT("CreateNameSpaceObject: gpnsNameSpaceRoot %X, NameSeg %X\n", gpnsNameSpaceRoot, gpnsNameSpaceRoot->NameSeg);

        InsertOwnerObjList(Owner, NsObject);

        if (OutObject)
            *OutObject = NsObject;

        goto Exit;
    }

    nameSeg = StrRChr(Name, '.');

    if (nameSeg)
    {
        *nameSeg = 0;
        nameSeg = (nameSeg + 1);

        Status = GetNameSpaceObject(Name, NsScope, &ParentObject, 0x80000001);
        if (Status)
            goto Exit;
    }
    else if (*Name == '\\')
    {
        nameSeg = (Name + 1);

        ASSERT(gpnsNameSpaceRoot != NULL);
        ParentObject = gpnsNameSpaceRoot;
    }
    else if (*Name == '^')
    {
        nameSeg = Name;
        ParentObject = NsScope;

        while (ParentObject)
        {
            ParentObject = ParentObject->Parent;

            nameSeg++;
            if (*nameSeg != '^')
                break;
        }
    }
    else
    {
        ASSERT(NsScope != NULL);
        ParentObject = NsScope;

        nameSeg = Name;
        if (Status)
            goto Exit;
    }

    nameSegSize = StrLen(nameSeg, 0xFFFFFFFF);

    if (*nameSeg != 0 && nameSegSize > 4)
    {
        DPRINT1("CreateNameSpaceObject: invalid name - %s", nameSeg);
        ASSERT(FALSE);
        Status = STATUS_OBJECT_NAME_INVALID;
        goto Exit;
    }

    gdwcNSObjs++;

    NsObject = HeapAlloc(Heap, 'OSNH', sizeof(AMLI_NAME_SPACE_OBJECT));
    if (!NsObject)
    {
        DPRINT1("CreateNameSpaceObject: fail to allocate name space object");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(NsObject, sizeof(AMLI_NAME_SPACE_OBJECT));

    if (*Name)
    {
        NsObject->NameSeg = '____';
        RtlCopyMemory(&NsObject->NameSeg, nameSeg, nameSegSize);
    }
    else
    {
        NsObject->NameSeg = 0;
    }

    NsObject->Parent = ParentObject;
    InsertOwnerObjList(Owner, NsObject);

    ListInsertTail(&NsObject->List, (PAMLI_LIST *)&ParentObject->FirstChild);

    if (Status == STATUS_SUCCESS && OutObject)
        *OutObject = NsObject;

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
InitMutex(
    _In_ PAMLI_HEAP Heap,
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject,
    _In_ ULONG Level)
{
    PAMLI_MUTEX_OBJECT AmliMutex;
    NTSTATUS Status = STATUS_SUCCESS;

    giIndent++;

    NsObject->ObjData.DataLen = 0x10;
    NsObject->ObjData.DataType = 9;

    gdwcMTObjs++;

    NsObject->ObjData.DataBuff = AmliMutex = HeapAlloc(Heap, 'TUMH', NsObject->ObjData.DataLen);

    if (AmliMutex)
    {
        RtlZeroMemory(AmliMutex, NsObject->ObjData.DataLen);
        AmliMutex->SyncLevel = Level;
    }
    else
    {
        DPRINT1("CreateNameSpaceObject: failed to allocate Mutex object");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    giIndent--;

    return Status;
}

VOID
NTAPI
StartTimeSlicePassive(
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
TimeoutCallback(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
__cdecl
InitContext(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ ULONG Size)
{
    //DPRINT("InitContext: AmliContext %X, Size %X\n", AmliContext, Size);

    giIndent++;

    RtlZeroMemory(AmliContext, offsetof(AMLI_CONTEXT, LocalHeap));

    AmliContext->Signature = 'TXTC';
    AmliContext->End = (PUCHAR)((ULONG_PTR)AmliContext + Size);
    AmliContext->HeapCurrent = &AmliContext->LocalHeap;

    KeInitializeDpc(&AmliContext->Dpc, TimeoutCallback, AmliContext);
    KeInitializeTimer(&AmliContext->Timer);

    InitHeap(&AmliContext->LocalHeap, ((ULONG_PTR)AmliContext->End - (ULONG_PTR)&AmliContext->LocalHeap));

    AmliContext->LocalHeap.HeapHead = &AmliContext->LocalHeap;

    giIndent--;
}

NTSTATUS
__cdecl
NewContext(
    _Out_ PAMLI_CONTEXT* OutContext)
{
    PAMLI_CONTEXT AmliContext;
    KIRQL OldIrql;
    LONG ObjsMax;
    NTSTATUS Status = STATUS_SUCCESS;
  
    //DPRINT("NewContext: OutContext %X\n", OutContext);

    giIndent++;

    *OutContext = AmliContext = ExAllocateFromNPagedLookasideList(&AMLIContextLookAsideList);

    if (AmliContext)
    {
        KeAcquireSpinLock(&gdwGContextSpinLock, &OldIrql);

        ObjsMax = (gdwcCTObjs++ + 1);

        if (gdwcCTObjs > 0 && ObjsMax > gdwcCTObjsMax)
            gdwcCTObjsMax = ObjsMax;

        KeReleaseSpinLock(&gdwGContextSpinLock, OldIrql);

        InitContext(*OutContext, gdwCtxtBlkSize);

        AcquireMutex(&gmutCtxtList);
        ListInsertTail(&(*OutContext)->List, &gplistCtxtHead);
        ReleaseMutex(&gmutCtxtList);
    }
    else
    {
        DPRINT1("NewContext: Could not Allocate New Context\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    giIndent--;

    return Status;
}

VOID
__cdecl
FreeContext(
    _In_ PAMLI_CONTEXT AmliContext)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
__cdecl
NewObjOwner(
    _In_ PAMLI_HEAP Heap,
    _Out_ PAMLI_OBJECT_OWNER* OutOwner)
{
    PAMLI_OBJECT_OWNER Owner;
    NTSTATUS Status = STATUS_SUCCESS;
  
    DPRINT("NewObjOwner: %X, %X\n", Heap, OutOwner);

    giIndent++;
    gdwcOOObjs++;

    *OutOwner = Owner = HeapAlloc(Heap, 'NWOH', sizeof(*Owner));

    if (Owner)
    {
        Owner->List.Prev = NULL;
        Owner->List.Next = NULL;
        Owner->Signature = 'RNWO';
        Owner->ObjList = NULL;

        AcquireMutex(&gmutOwnerList);
        ListInsertTail(&Owner->List, &gplistObjOwners);
        ReleaseMutex(&gmutOwnerList);
    }
    else
    {
        DPRINT1("NewObjOwner: failed to allocate object owner\n");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
PushFrame(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ ULONG Signature,
    _In_ ULONG Length,
    _In_ PVOID ParseFunction,
    _Out_ PVOID* OutFrame)
{
    PCHAR SignatureStr;
    PAMLI_FRAME_HEADER Frame;
    NTSTATUS Status = 0;

    SignatureStr = NameSegString(Signature);
    DPRINT("PushFrame: %p, '%s', %X, %p\n", AmliContext, SignatureStr, Length, ParseFunction);

    giIndent++;

    Frame = (PAMLI_FRAME_HEADER)((ULONG_PTR)AmliContext->LocalHeap.HeapEnd - Length);

    if ((ULONG_PTR)Frame < (ULONG_PTR)AmliContext->LocalHeap.HeapTop)
    {
        DPRINT1("PushFrame: stack ran out of space\n");
        ASSERT(FALSE);
        Status = STATUS_ACPI_STACK_OVERFLOW;
        goto Exit;
    }

    AmliContext->LocalHeap.HeapEnd = Frame;

    RtlZeroMemory(Frame, Length);

    Frame->Signature = Signature;
    Frame->Length = Length;
    Frame->ParseFunction = ParseFunction;

    if (OutFrame)
        *OutFrame = Frame;

    if (((ULONG_PTR)AmliContext->End - (ULONG_PTR)AmliContext->LocalHeap.HeapEnd) > gdwLocalStackMax)
        gdwLocalStackMax = ((ULONG_PTR)AmliContext->End - (ULONG_PTR)AmliContext->LocalHeap.HeapEnd);

Exit:

    giIndent--;

    return Status;
}

VOID
__cdecl
EvalMethodComplete(
    _In_ PAMLI_NAME_SPACE_OBJECT Object,
    _In_ NTSTATUS InStatus,
    _In_ PAMLI_OBJECT_DATA Data,
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
__cdecl
ParseCall(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_CALL AmliCall,
    _In_ NTSTATUS InStatus)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
PushCall(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_NAME_SPACE_OBJECT NsMethod,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    PAMLI_METHOD_OBJECT MethodObject;
    PAMLI_CALL AmliCall;
    ULONG Size;
    NTSTATUS Status;

    DPRINT("PushCall: %X, '%s', %X\n", AmliContext, GetObjectPath(NsMethod), DataResult);

    giIndent++;

    ASSERT((NsMethod == NULL) || (NsMethod->ObjData.DataType == 8));//OBJTYPE_METHOD

    Status = PushFrame(AmliContext, 'LLAC', 0xCC, ParseCall, (PVOID *)&AmliCall);
    if (Status != STATUS_SUCCESS)
    {
        goto Exit;
    }

    if (!NsMethod)
    {
        ASSERT(AmliContext->Call == NULL);
        AmliContext->Call = AmliCall;

        AmliCall->FrameHdr.Flags = 4;
        AmliCall->DataResult = DataResult;

        goto Exit;
    }

    MethodObject = NsMethod->ObjData.DataBuff;
    AmliCall->NsMethod = NsMethod;

    if (MethodObject->MethodFlags & 8)
        AmliCall->FrameHdr.Flags |= 0x10000;

    AmliCall->NumberOfArgs = (MethodObject->MethodFlags & 7);

    if (AmliCall->NumberOfArgs > 0)
    {
        gdwcODObjs++;

        Size = (AmliCall->NumberOfArgs * sizeof(AMLI_OBJECT_DATA));
        AmliCall->DataArgs = HeapAlloc(AmliContext->HeapCurrent, 'TADH', Size);

        if (AmliCall->DataArgs)
        {
            RtlZeroMemory(AmliCall->DataArgs, Size);
        }
        else
        {
            DPRINT1("PushCall: failed to allocate argument objects\n");
            ASSERT(FALSE);
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    AmliCall->DataResult = DataResult;

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
ParseScope(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_SCOPE AmliScope,
    _In_ NTSTATUS InStatus)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
PushScope(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PUCHAR OpcodeBegin,
    _In_ PUCHAR OpEnd,
    _In_ PUCHAR OpcodeRet,
    _In_ PAMLI_NAME_SPACE_OBJECT NsScope,
    _In_ PAMLI_OBJECT_OWNER Owner,
    _In_ PAMLI_HEAP Heap,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    NTSTATUS Status;
    PAMLI_SCOPE AmliScope;

    DPRINT("PushScope: %X, %X, %X, %X, %X, %X, %X\n", AmliContext, OpcodeBegin, OpEnd, OpcodeRet, NsScope, Heap, DataResult);

    giIndent++;

    Status = PushFrame(AmliContext, 'POCS', sizeof(AMLI_SCOPE), ParseScope, (PVOID *)&AmliScope);
    if (Status == STATUS_SUCCESS)
    {
        AmliContext->Op = OpcodeBegin;
        AmliScope->OpEnd = OpEnd;
        AmliScope->OpcodeRet = OpcodeRet;
        AmliScope->OldScope = AmliContext->Scope;
        AmliContext->Scope = NsScope;
        AmliScope->OldOwner = AmliContext->Owner;
        AmliContext->Owner = Owner;
        AmliScope->HeapCurrent = AmliContext->HeapCurrent;
        AmliContext->HeapCurrent = Heap;
        AmliScope->DataResult = DataResult;
    }

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
LoadDDB(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PDSDT Dsdt,
    _In_ PAMLI_NAME_SPACE_OBJECT NsScope,
    _Out_ PAMLI_OBJECT_OWNER* OutOwner)
{
    //PCHAR SegString;
    NTSTATUS Status;

    DPRINT("LoadDDB: Dsdt %X\n", Dsdt);

    DPRINT("LoadDDB: FIXME ValidateTable()\n");

    Status = NewObjOwner(gpheapGlobal, OutOwner);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("LoadDDB: Status %X\n", Status);
        ASSERT(FALSE);
        AmliContext->Owner = NULL;
        FreeContext(AmliContext);
        return Status;
    }

    if (!AmliContext->Call)
    {
        Status = PushCall(AmliContext, NULL, &AmliContext->Result);
        if (Status != STATUS_SUCCESS)
        {
            DPRINT1("LoadDDB: Status %X\n", Status);
            ASSERT(FALSE);
            return Status;
        }
    }

    Status = PushScope(AmliContext,
                       (PUCHAR)Dsdt->DiffDefBlock,
                       ((PUCHAR)Dsdt + Dsdt->Header.Length),
                       AmliContext->Op,
                       NsScope,
                       *OutOwner,
                       gpheapGlobal,
                       &AmliContext->Result);

    DPRINT("LoadDDB: ret Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
RestartContext(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ BOOLEAN IsDelayExecute)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
SyncLoadDDB(
    _In_ PAMLI_CONTEXT AmliContext)
{
    AMLI_ASYNC_CONTEXT AsyncContext;
    NTSTATUS Status;
  
    DPRINT("SyncLoadDDB: %X\n", AmliContext);

    giIndent++;

    if (KeGetCurrentThread() == gReadyQueue.Thread)
    {
        DPRINT("SyncLoadDDB: cannot nest a SyncLoadDDB\n");
        ASSERT(FALSE);
        Status = STATUS_ACPI_FATAL;
        AmliContext->Owner = NULL;
        FreeContext(AmliContext);
        goto Exit;
    }

    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        DPRINT("SyncLoadDDB: cannot SyncLoadDDB at IRQL >= DISPATCH_LEVEL\n");
        ASSERT(FALSE);
        Status = STATUS_ACPI_FATAL;
        AmliContext->Owner = NULL;
        FreeContext(AmliContext);
        goto Exit;
    }

    KeInitializeEvent(&AsyncContext.Event, SynchronizationEvent, FALSE);

    AmliContext->AsyncCallBack = EvalMethodComplete;
    AmliContext->CallBackContext = &AsyncContext;

    Status = RestartContext(AmliContext, FALSE);

    while (Status == 0x8004)
    {
        Status = KeWaitForSingleObject(&AsyncContext.Event, Executive, KernelMode, FALSE, NULL);

        if (Status)
        {
            DPRINT("SyncLoadDDB: object synchronization failed (%X)\n", Status);
            ASSERT(FALSE);
            Status = STATUS_ACPI_FATAL;
        }
        else
        {
            if (AsyncContext.Status == 0x8003)
                Status = RestartContext(AsyncContext.AmliContext, FALSE);
            else
                Status = AsyncContext.Status;
        }
    }

  Exit:

    giIndent--;

    DPRINT("SyncLoadDDB: Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl 
AMLILoadDDB(
    _In_ PDSDT Dsdt,
    _Out_ HANDLE* OutHandle)
{
    PAMLI_OBJECT_OWNER Owner = NULL;
    PAMLI_CONTEXT AmliContext = NULL;
    PAMLI_TERM_CALLBACK_1 CallBack;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT("AMLILoadDDB: Dsdt %X\n", Dsdt);

    giIndent++;

    ASSERT(Dsdt != NULL);

    gInitTime = TRUE;

    Status = NewContext(&AmliContext);
    if (Status == STATUS_SUCCESS)
    {
        ASSERT(gpheapGlobal != NULL);
        AmliContext->HeapCurrent = gpheapGlobal;

        gdwfAMLI |= 0x80000000;

        if (atLoad.CallBack && atLoad.Flags2 & 0x80000000)
        {
            CallBack = atLoad.CallBack;
            CallBack(12, 1, atLoad.Opcode, NULL, atLoad.Context);
        }

        Status = LoadDDB(AmliContext, Dsdt, gpnsNameSpaceRoot, &Owner);
        if (Status == STATUS_SUCCESS)
            Status = SyncLoadDDB(AmliContext);

        gdwfAMLI &= 0x80000000;

        KeAcquireSpinLock(&gdwGHeapSpinLock, &OldIrql);
        gdwGHeapSnapshot = gdwGlobalHeapSize;
        KeReleaseSpinLock(&gdwGHeapSpinLock, OldIrql);
    }

    if (OutHandle)
        *OutHandle = Owner;

    if (Owner && atLoad.CallBack)
    {
        if (atLoad.Flags2 & 0x80000000)
        {
            CallBack = atLoad.CallBack;
            CallBack(0xC, 2, atLoad.Opcode, 0, atLoad.Context);
        }
        else
        {
            DPRINT("AMLILoadDDB: FIXME\n");
            ASSERT(FALSE);
        }
    }

    if (gdwfAMLIInit & 2)
    {
        DPRINT("AMLILoadDDB: Break at Load Definition Block Completion.\n");
        ASSERT(FALSE);
        //AMLIDebugger(0);
    }

    if (Status == 0x8004)
        Status = STATUS_PENDING;

    giIndent--;

    gInitTime = FALSE;

    DPRINT("AMLILoadDDB: Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
AMLIAsyncEvalObject(
    _In_ PAMLI_NAME_SPACE_OBJECT AcpiObject,
    _In_ PAMLI_OBJECT_DATA DataResult,
    _In_ ULONG ArgsCount,
    _In_ PAMLI_OBJECT_DATA DataArgs,
    _In_ PVOID CallBack,
    _In_ PVOID CallBackContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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

        DPRINT("AMLIInitialize: FIXME - initialize DPC and timer\n");
        //ASSERT(FALSE);

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
