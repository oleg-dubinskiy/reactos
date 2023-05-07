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

AMLI_OBJECT_TYPE_NAME ObjTypesNames[26] =
{
    { 0x00, "Unknown"       },
    { 0x01, "Integer"       },
    { 0x02, "String"        },
    { 0x03, "Buffer"        },
    { 0x04, "Package"       },
    { 0x05, "FieldUnit"     },
    { 0x06, "Device"        },
    { 0x07, "Event"         },
    { 0x08, "Method"        },
    { 0x09, "Mutex"         },
    { 0x0A, "OpRegion"      }, // 10
    { 0x0B, "PowerResource" }, // 11
    { 0x0C, "Processor"     }, // 12
    { 0x0D, "ThermalZone"   }, // 13
    { 0x0E, "BuffField"     }, // 14
    { 0x0F, "DDBHandle"     }, // 15
    { 0x10, "Debug"         }, // 16
    { 0x80, "ObjAlias"      }, // 128
    { 0x81, "DataAlias"     }, // 129
    { 0x82, "BankField"     }, // 130
    { 0x83, "Field"         }, // 131
    { 0x84, "IndexField"    }, // 132
    { 0x85, "Data"          }, // 133
    { 0x86, "DataField"     }, // 134
    { 0x87, "DataObject"    }, // 135
    { 0, NULL               }
};

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
    ASSERT(OutListHead != NULL);

    //DPRINT("ListInsertHead: ListEntry %X, OutListHead %X\n", ListEntry, *OutListHead);

    giIndent++;

    ASSERT(ListEntry != NULL);

    ListInsertTail(ListEntry, OutListHead);
    *OutListHead = ListEntry;

    giIndent--;
}

VOID
__cdecl
ListRemoveEntry(
    _In_ PAMLI_LIST ListEntry,
    _Inout_ PAMLI_LIST* OutListHead)
{
    ASSERT(OutListHead);

    //DPRINT("ListRemoveEntry: ListEntry %X, OutListHead %X\n", ListEntry, *OutListHead);

    giIndent++;

    ASSERT(ListEntry != NULL);

    if (ListEntry->Next == ListEntry)
    {
        ASSERT(ListEntry == *OutListHead);
        *OutListHead = NULL;
    }
    else
    {
        if (ListEntry == *OutListHead)
            *OutListHead = (*OutListHead)->Next;

        ListEntry->Next->Prev = ListEntry->Prev;
        ListEntry->Prev->Next = ListEntry->Next;
    }

    giIndent--;
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
__cdecl
GetBaseObject(
    _In_ PAMLI_NAME_SPACE_OBJECT NsObject)
{
    DPRINT("GetBaseObject: %X, '%s'\n", NsObject, GetObjectPath(NsObject));

    giIndent++;

    while (NsObject->ObjData.DataType == 0x80)
        NsObject = NsObject->ObjData.Alias;

    giIndent--;

    return NsObject;
}

PAMLI_OBJECT_DATA
__cdecl
GetBaseData(
    _In_ PAMLI_OBJECT_DATA DataObj)
{
    USHORT DataType;

    DPRINT("GetBaseData: DataObj %p\n", DataObj);

    giIndent++;

    ASSERT(DataObj);

    while (TRUE)
    {
        while (TRUE)
        {
            DataType = DataObj->DataType;

            if (DataType != 0x80)
                break;

            DataObj = &DataObj->Alias->ObjData;
        }

        if (DataType != 0x81)
            break;

        DataObj = DataObj->DataAlias;
    }

    giIndent--;

    return DataObj;
}

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

VOID
__cdecl
MoveObjData(
    _In_ PAMLI_OBJECT_DATA DataDst,
    _In_ PAMLI_OBJECT_DATA DataSrc)
{
    DPRINT("MoveObjData: DataDst %X, DataSrc %X\n", DataDst, DataSrc);

    giIndent++;

    ASSERT(DataDst != NULL);
    ASSERT(DataSrc != NULL);

    if (DataDst != DataSrc)
    {
        ASSERT((DataSrc->Flags & 1) || (DataSrc->DataBuff == NULL) || (DataSrc->RefCount == 0));//DATAF_BUFF_ALIAS

        RtlCopyMemory(DataDst, DataSrc, sizeof(*DataDst));
        RtlZeroMemory(DataSrc, sizeof(*DataSrc));
    }

    giIndent--;
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

PCHAR
__cdecl
GetObjectTypeName(
    _In_ ULONG Type)
{
    PCHAR TypeName = NULL;
    ULONG ix;

    TypeName;

    giIndent++;

    ix = 0;
    if (ObjTypesNames[0].Name)
    {
        while (Type != ObjTypesNames[ix].Type)
        {
            ix++;
            if (!ObjTypesNames[ix].Name)
                goto Exit;
        }

        TypeName = ObjTypesNames[ix].Name;
    }

Exit:

    giIndent--;

    return TypeName;
}

NTSTATUS
__cdecl
ValidateArgTypes(
    _In_ PAMLI_OBJECT_DATA ObjData,
    _In_ PCHAR ExpectedTypes)
{
    ULONG ExpectedType;
    ULONG DataLen;
    ULONG ix;
    USHORT DataType;
    NTSTATUS Status = STATUS_SUCCESS;

    giIndent++;

    ASSERT(ExpectedTypes != NULL);

    DataLen = StrLen(ExpectedTypes, 0xFFFFFFFF);

    for (ix = 0; ix < DataLen; ix++)
    {
        DataType = ObjData[ix].DataType;
        ExpectedType = ExpectedTypes[ix];

        switch (ExpectedType)
        {
            case 0x41:
            {
                if (DataType != 0x81)
                {
                    DPRINT1("ValidateArgTypes: expected Arg %X to be type DataAlias (Type '%s')\n", ix, GetObjectTypeName(DataType));
                    ASSERT(FALSE);
                    Status = STATUS_ACPI_INVALID_OBJTYPE;
                }
                break;
            }
            case 0x42:
            {
                if (DataType != 3)
                {
                    DPRINT1("ValidateArgTypes: expected Arg %X to be type Buffer (Type '%s')\n", ix, GetObjectTypeName(DataType));
                    ASSERT(FALSE);
                    Status = STATUS_ACPI_INVALID_ARGTYPE;
                }
                break;
            }
            case 0x43:
            {
                if (DataType == 3 || DataType == 4)
                    break;

                DPRINT1("ValidateArgTypes: expected Arg %X to be type buff/pkg (Type '%s')\n", ix, GetObjectTypeName(DataType));
                ASSERT(FALSE);
                Status = STATUS_ACPI_INVALID_OBJTYPE;
                break;
            }
            case 0x44:
            {
                if (DataType == 1 || DataType == 2 || DataType == 3)
                    break;

                DPRINT1("ValidateArgTypes: expected Arg %X to be type int/str/buff (Type '%s')\n", ix, GetObjectTypeName(DataType));
                ASSERT(FALSE);
                Status = STATUS_ACPI_INVALID_OBJTYPE;
                break;
            }
            case 0x46:
            {
                if (DataType != 5)
                {
                    DPRINT1("ValidateArgTypes: expected Arg %X to be type FieldUnit (Type '%s')\n", ix, GetObjectTypeName(DataType));
                    ASSERT(FALSE);
                    Status = STATUS_ACPI_INVALID_ARGTYPE;
                }
                break;
            }
            case 0x49:
            {
                if (DataType != 1)
                {
                    DPRINT1("ValidateArgTypes: expected Arg %X to be type Integer (Type '%s')\n", ix, GetObjectTypeName(DataType));
                    ASSERT(FALSE);
                    Status = STATUS_ACPI_INVALID_ARGTYPE;
                }
                break;
            }
            case 0x4F:
            {
                if (DataType != 0x80)
                {
                    DPRINT1("ValidateArgTypes: expected Arg %X to be type ObjAlias (Type '%s')\n", ix, GetObjectTypeName(DataType));
                    ASSERT(FALSE);
                    Status = STATUS_ACPI_INVALID_OBJTYPE;
                }
                break;
            }
            case 0x50:
            {
                if (DataType != 4)
                {
                    DPRINT1("ValidateArgTypes: expected Arg %X to be type Package (Type '%s')\n", ix, GetObjectTypeName(DataType));
                    ASSERT(FALSE);
                    Status = STATUS_ACPI_INVALID_ARGTYPE;
                }
                break;
            }
            case 0x52:
            {
                if (DataType == 0x80 || DataType == 0x81 || DataType == 0xE)
                    break;

                ASSERT(ExpectedTypes != NULL);

                DPRINT1("ValidateArgTypes: expected Arg %X to be type reference (Type '%s')\n", ix, GetObjectTypeName(DataType));
                ASSERT(FALSE);
                Status = STATUS_ACPI_INVALID_ARGTYPE;
                break;
            }
            case 0x55:
            {
                break;
            }
            case 0x5A:
            {
                if (DataType != 2)
                {
                    DPRINT1("ValidateArgTypes: expected Arg %X to be type String (Type '%s')\n", ix, GetObjectTypeName(DataType));
                    ASSERT(FALSE);
                    Status = STATUS_ACPI_INVALID_ARGTYPE;
                }
                break;
            }
            default:
            {
                DPRINT1("ValidateArgTypes: internal error (invalid type - '%c'), ix %X\n", ix, ExpectedTypes[ix]);
                ASSERT(FALSE);
                Status = STATUS_ACPI_ASSERT_FAILED;
                break;
            }
        }

        if (Status != STATUS_SUCCESS)
            break;
    }

    giIndent--;

    return Status;
}

/* CALLBACKS TERM HANDLERS **************************************************/

NTSTATUS
__cdecl
ParsePackage(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_PACKAGE_CONTEXT PackageContext,
    _In_ NTSTATUS InStatus)
{
   ULONG Stage;
   UCHAR Opcode;
   ULONG idx;

    if (InStatus != STATUS_SUCCESS)
        Stage = 2;
    else
        Stage = (PackageContext->FrameHeader.Flags & 0xF);

    DPRINT("ParsePackage: %X, %X, %X, %X, %X\n", Stage, AmliContext, AmliContext->Op, PackageContext, InStatus);

    giIndent++;

   ASSERT(PackageContext->FrameHeader.Signature == 'FGKP');//SIG_PACKAGE

    if (Stage == 0)
    {
        PackageContext->FrameHeader.Flags++;
    }
    else if (Stage == 1)
    {
        ;
    }
    else if (Stage == 2)
    {
        PopFrame(AmliContext);
        goto Exit;
    }
    else
    {
        goto Exit;
    }

    while (TRUE)
    {
        if (AmliContext->Op < PackageContext->OpEnd)
        {
            while ((PackageContext->ElementCount < PackageContext->PackageObject->Elements))
            {
                idx = PackageContext->ElementCount;
                PackageContext->ElementCount++;

                Opcode = *AmliContext->Op;

                if (Opcode == 0x11 || Opcode == 0x12)
                {
                    InStatus = ParseOpcode(AmliContext, NULL, &PackageContext->PackageObject->Data[idx]);
                    if (InStatus != STATUS_SUCCESS)
                        break;

                    if (PackageContext != AmliContext->LocalHeap.HeapEnd)
                        goto Next;
                }
                else
                {
                    InStatus = ParseIntObj(&AmliContext->Op, &PackageContext->PackageObject->Data[idx], TRUE);
                    if (InStatus == STATUS_ACPI_INVALID_OPCODE)
                    {
                        InStatus = ParseString(&AmliContext->Op, &PackageContext->PackageObject->Data[idx], TRUE);
                        if (InStatus == STATUS_ACPI_INVALID_OPCODE)
                        {
                            InStatus = ParseObjName(&AmliContext->Op, &PackageContext->PackageObject->Data[idx], TRUE);
                            if (InStatus == STATUS_ACPI_INVALID_OPCODE)
                            {
                                DPRINT("ParsePackage: invalid opcode %X at %X", *AmliContext->Op, AmliContext->Op);
                                ASSERT(FALSE);
                                break;
                            }
                        }
                    }

                    if (InStatus != STATUS_SUCCESS)
                        break;
                }

                if (AmliContext->Op >= PackageContext->OpEnd)
                    goto Next;
            }
        }

        if (InStatus == 0x8004)
            break;
Next:
        if (PackageContext != AmliContext->LocalHeap.HeapEnd)
            break;

        if (InStatus != STATUS_SUCCESS ||
            AmliContext->Op >= PackageContext->OpEnd ||
            PackageContext->ElementCount >= PackageContext->PackageObject->Elements)
        {
            PackageContext->FrameHeader.Flags++;
            PopFrame(AmliContext);
            goto Exit;
        }
    }

Exit:

    giIndent--;

    //DPRINT("ParsePackage: ret Status %X\n", InStatus);
    return InStatus;
}

NTSTATUS
__cdecl
CreateXField(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_TERM_CONTEXT TermiContext,
    _In_ PAMLI_OBJECT_DATA DataTarget,
    _Out_ PAMLI_BUFF_FIELD_OBJECT* OutBufferField)
{
    PAMLI_OBJECT_DATA Data = NULL;
    NTSTATUS Status;

    DPRINT("CreateXField: %X, %X, %X, %X, %X\n", AmliContext, AmliContext->Op, TermiContext, DataTarget, OutBufferField);

    giIndent++;

    ASSERT(DataTarget != NULL);
    ASSERT(DataTarget->DataType == 2);//OBJTYPE_STRDATA

    Status = ValidateArgTypes(TermiContext->DataArgs, "BI");
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("CreateXField: Status %X\n", Status);
        goto Exit;
    }

    Status = CreateNameSpaceObject(AmliContext->HeapCurrent, 
                                   DataTarget->DataBuff,
                                   AmliContext->Scope,
                                   AmliContext->Owner,
                                   &TermiContext->NsObject,
                                   0);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("CreateXField: Status %X\n", Status);
        goto Exit;
    }

    Data = &TermiContext->NsObject->ObjData;
    Data->DataLen = 0x18;
    Data->DataType = 0xE;

    gdwcBFObjs++;

    Data->DataBuff = HeapAlloc(AmliContext->HeapCurrent, 'DFBH', Data->DataLen);
    if (!Data->DataBuff)
    {
        DPRINT1("CreateXField: failed to allocate BuffField object\n");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(Data->DataBuff, Data->DataLen);

    *OutBufferField = Data->DataBuff;

    (*OutBufferField)->DataBuff = TermiContext->DataArgs->DataBuff;
    (*OutBufferField)->BuffLen = TermiContext->DataArgs->DataLen;

Exit:

    giIndent--;

    //DPRINT("CreateXField: ret Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
ValidateTarget(
    _In_ PAMLI_OBJECT_DATA DataTarget,
    _In_ ULONG ExpectedType,
    _Out_ PAMLI_OBJECT_DATA* OutData)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
__cdecl
NeedGlobalLock(
    _In_ PAMLI_FIELD_UNIT_OBJECT FieldUnitObj)
{
    PAMLI_FIELD_UNIT_OBJECT ParentFieldUnitObj;
    PAMLI_INDEX_FIELD_OBJECT IndexFieldObj;
    BOOLEAN Result = FALSE;

    DPRINT("NeedGlobalLock: %X\n", FieldUnitObj);

    giIndent++;

    if ((FieldUnitObj->FieldDesc.FieldFlags & 0x80000000) ||
        (FieldUnitObj->FieldDesc.FieldFlags & 0x10))
    {
        FieldUnitObj->FieldDesc.FieldFlags |= 0x80000000;
        Result = TRUE;
        goto Exit;
    }

    IndexFieldObj = FieldUnitObj->NsFieldParent->ObjData.DataBuff;

    if (FieldUnitObj->NsFieldParent->ObjData.DataType == 0x82)
    {
        DPRINT1("NeedGlobalLock: FIXME\n");
        ASSERT(FALSE);

        goto Exit;
    }

    if (FieldUnitObj->NsFieldParent->ObjData.DataType == 0x84)
    {
        ParentFieldUnitObj = IndexFieldObj->IndexObj->ObjData.DataBuff;

        if (ParentFieldUnitObj->FieldDesc.FieldFlags & 0x10)
        {
            FieldUnitObj->FieldDesc.FieldFlags |= 0x80000000;
            Result = TRUE;
        }
        else
        {
            ParentFieldUnitObj = IndexFieldObj->DataObj->ObjData.DataBuff;

            if (ParentFieldUnitObj->FieldDesc.FieldFlags & 0x10)
            {
                FieldUnitObj->FieldDesc.FieldFlags |= 0x80000000;
                Result = TRUE;
            }
        }
    }

Exit:

    giIndent--;

    return Result;
}

ULONG
__cdecl
ReadSystemMem(
    _In_ PVOID Addr,
    _In_ ULONG Size,
    _In_ ULONG Mask)
{
    UCHAR buffer[4] = {0};
    ULONG Result;

    giIndent++;

    ASSERT((Size == sizeof(UCHAR)) || (Size == sizeof(USHORT)) || (Size == sizeof(ULONG)));

    RtlCopyMemory(buffer, Addr, Size);

    Result = (*(ULONG *)buffer & Mask);

    giIndent--;

    return Result;
}

VOID
__cdecl
WriteSystemMem(
    _In_ PVOID Addr,
    _In_ ULONG Size,
    _In_ ULONG Data,
    _In_ ULONG Mask)
{
    ULONG mask[5];
    ULONG buff = 0;
    BOOLEAN IsNotBuffering = FALSE;

    mask[0] = 0;
    mask[1] = 0xFF;
    mask[2] = 0xFFFF;
    mask[3] = 0;
    mask[4] = 0xFFFFFFFF;

    if (Mask == mask[Size])
        IsNotBuffering = TRUE;

    giIndent++;

    ASSERT((Size == sizeof(UCHAR)) || (Size == sizeof(USHORT)) || (Size == sizeof(ULONG)));

    if (!IsNotBuffering)
        RtlCopyMemory(&buff, Addr, Size);

    buff = (buff & ~Mask) | Data;
    RtlCopyMemory(Addr, &buff, Size);

    giIndent--;
}

ULONG
__cdecl
ReadSystemIO(
    _In_ PVOID Addr,
    _In_ ULONG Size,
    _In_ ULONG Mask)
{
    ULONG Value;
    ULONG OutValue = 0;

    DPRINT("ReadSystemIO: %X, %X, %X\n", Addr, Size, Mask);

    giIndent++;

    ASSERT((Size == sizeof(UCHAR)) || (Size == sizeof(USHORT)) || (Size == sizeof(ULONG)));

    //FIXME
    //if (CheckSystemIOAddressValidity(1, Addr, Size, &OutValue))
    if (TRUE)
    {
        switch (Size)
        {
            case 1:
                Value = READ_PORT_UCHAR((PUCHAR)Addr);
                break;

            case 2:
                Value = READ_PORT_USHORT((PUSHORT)Addr);
                break;

            case 4:
                Value = READ_PORT_ULONG((PULONG)Addr);
                break;
        }
    }
    else
    {
        Value = OutValue;
    }

    giIndent--;

    OutValue = (Value & Mask);

    return OutValue;
}

VOID
__cdecl
WriteSystemIO(
    _In_ PVOID Port,
    _In_ ULONG Size,
    _In_ ULONG Value)
{
    giIndent++;

    ASSERT((Size == sizeof(UCHAR)) || (Size == sizeof(USHORT)) || (Size == sizeof(ULONG)));

    //FIXME
    //if (CheckSystemIOAddressValidity(0, Port, Size, &Value))
    if (TRUE)
    {
        switch (Size)
        {
            case 1:
                WRITE_PORT_UCHAR((PUCHAR)Port, (UCHAR)Value);
                break;

            case 2:
                WRITE_PORT_USHORT((PUSHORT)Port, (USHORT)Value);
                break;

            case 4:
                WRITE_PORT_ULONG((PULONG)Port, Value);
                break;
        }
    }

    giIndent--;
}

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

VOID
__cdecl
RestartCtxtCallback(
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
__cdecl
AccessBaseField(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_NAME_SPACE_OBJECT BaseObj,
    _In_ PAMLI_FIELD_DESCRIPTOR FieldDesc,
    _Out_ ULONG* OutData,
    _In_ BOOLEAN IsRead)
{
    PINTERNAL_OP_REGION_HANDLER IntRegionHandler;
    PAMLI_OP_REGION_OBJECT OpRegionObj;
    PAMLI_RS_ACCESS_HANDLER RsAccess;
    PVOID Addr;
    ULONG DataSize;
    ULONG DataMask;
    ULONG NumBits;
    ULONG AccMask;
    ULONG AccSize;
    ULONG Value;
    ULONG Stage;
    BOOLEAN IsReadBeforeWrite;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("AccessBaseField: %X, %X, %X, %X, %X\n", AmliContext, BaseObj, FieldDesc, OutData, IsRead);

    giIndent++;

    ASSERT(BaseObj->ObjData.DataType == 0xA);//OBJTYPE_OPREGION

    Stage = (FieldDesc->FieldFlags & 0xF);

    if (Stage >= 1 || Stage <= 3)
        AccSize = (1 << (Stage - 1));
    else
        AccSize = 1;

    NumBits = FieldDesc->NumBits;

    DataSize = ((NumBits < 0x20) ?  (1 << NumBits) : 0);
    DataMask = ((DataSize - 1) << FieldDesc->StartBitPos);
    AccMask = ((8 * AccSize) < 0x20 ? ((1 << (8 * AccSize)) + 1) : 0xFFFFFFFF);

    if ((FieldDesc->FieldFlags & 0x60) || !(~DataMask & AccMask))
        IsReadBeforeWrite = FALSE;
    else
        IsReadBeforeWrite = TRUE;

    *OutData &= DataMask;

    if (!IsRead && (FieldDesc->FieldFlags & 0x60) == 0x20)
        *OutData |= ~DataMask;

    Addr = Add2Ptr(BaseObj->ObjData.DataBuff, FieldDesc->ByteOffset);
    OpRegionObj = BaseObj->ObjData.DataBuff;

    if (OpRegionObj->RegionSpace == 0)
    {
        if (IsRead)
        {
            DPRINT1("AccessBaseField: FIXME\n");
            ASSERT(FALSE);
            goto Exit;
        }

        if (IsReadBeforeWrite)
        {
            DPRINT1("AccessBaseField: FIXME\n");
            ASSERT(FALSE);
        }

        WriteSystemMem(Addr, AccSize, *OutData, AccMask);

        goto Exit;
    }

    if (OpRegionObj->RegionSpace == 1)
    {
        if (IsRead)
        {
            Value = ReadSystemIO(Addr, AccSize, DataMask);
            *OutData = Value;
        }
        else
        {
            if (IsReadBeforeWrite)
                *OutData |= ReadSystemIO(Addr, AccSize, ~DataMask);

            WriteSystemIO(Addr, AccSize, *OutData);
        }
        goto Exit;
    }

    RsAccess = FindRSAccess(OpRegionObj->RegionSpace);

    if (!RsAccess || !RsAccess->CookAccessHandler)
    {
        DPRINT1("AccessBaseField: AccessBaseField: no handler for RegionSpace %x\n", OpRegionObj->RegionSpace);
        Status = STATUS_ACPI_INVALID_REGION;
        goto Exit;
    }

    if (IsRead)
    {
        ASSERT(!(AmliContext->Flags & 8));//CTXTF_READY

        IntRegionHandler = RsAccess->CookAccessHandler;

        Status = IntRegionHandler(0,
                                  BaseObj,
                                  (ULONG)Addr,
                                  AccSize,
                                  OutData,
                                  RsAccess->CookAccessParam,
                                  RestartCtxtCallback,
                                  &AmliContext->ContextData);

        if (Status == STATUS_PENDING)
        {
            Status = 0x8004;
        }
        else if (Status)
        {
            DPRINT1("AccessBaseField: RegionSpace %X read handler returned error %X\n", OpRegionObj->RegionSpace, Status);
            ASSERT(FALSE);
            Status = STATUS_ACPI_RS_ACCESS;
        }
    }
    else
    {
        ASSERT(FALSE);
    }

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
ReadBuffField(
    _In_ PAMLI_BUFF_FIELD_OBJECT BufferFieldObj,
    _In_ PAMLI_FIELD_DESCRIPTOR FieldDesc,
    _Out_ ULONG* OutData)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
GetFieldUnitRegionObj(
    _In_ PAMLI_FIELD_UNIT_OBJECT FieldUnitObj,
    _Out_ PAMLI_NAME_SPACE_OBJECT* OutRegionObj)
{
    PAMLI_NAME_SPACE_OBJECT NsParent;
    PAMLI_NAME_SPACE_OBJECT NsObject;
    PAMLI_INDEX_FIELD_OBJECT IndexFieldObj;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("GetFieldUnitRegionObj: %X, %X\n", FieldUnitObj, OutRegionObj);

    giIndent++;

    NsParent = FieldUnitObj->NsFieldParent;

    if (NsParent->ObjData.DataType == 0x82) // BankField
    {
        DPRINT1("GetFieldUnitRegionObj: FIXME\n");
        ASSERT(FALSE);
    }
    else if (NsParent->ObjData.DataType == 0x83) // Field
    {
        *OutRegionObj = *(PAMLI_NAME_SPACE_OBJECT *)NsParent->ObjData.DataBuff;
    }
    else if (NsParent->ObjData.DataType == 0x84) // IndexField
    {
        IndexFieldObj = NsParent->ObjData.DataBuff;
        NsObject = IndexFieldObj->DataObj;

        ASSERT(NsObject->ObjData.DataType == 5);//OBJTYPE_FIELDUNIT

        Status = GetFieldUnitRegionObj(NsObject->ObjData.DataBuff, OutRegionObj);
    }
    else
    {
        DPRINT1("GetFieldUnitRegionObj: unknown field unit parent object type %X\n", (*OutRegionObj)->ObjData.DataType);
        ASSERT(FALSE);
        Status = STATUS_ACPI_ASSERT_FAILED;
    }

    if (*OutRegionObj && (*OutRegionObj)->ObjData.DataType != 0xA)
    {
        DPRINT1("GetFieldUnitRegionObj: base object of field unit is not OperationRegion (BaseObj '%s', Type %X)\n",
                GetObjectPath(*OutRegionObj), (*OutRegionObj)->ObjData.DataType);

        ASSERT(FALSE);
        Status = STATUS_ACPI_ASSERT_FAILED;
    }

    giIndent--;

    return Status;
}
    PAMLI_NAME_SPACE_OBJECT IndexObj;
    PAMLI_NAME_SPACE_OBJECT DataObj;

NTSTATUS
__cdecl
AccessFieldData(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_OBJECT_DATA DataObj,
    _In_ PAMLI_FIELD_DESCRIPTOR FieldDesc,
    _Out_ ULONG* OutData,
    _In_ BOOLEAN IsRead)
{
    PAMLI_FIELD_UNIT_OBJECT FieldUnitObj;
    PAMLI_NAME_SPACE_OBJECT FieldUnitRegionObj;
    PAMLI_INDEX_FIELD_OBJECT IndexFieldObj;
    PAMLI_OBJECT_DATA ParentObjData;
    PAMLI_OBJECT_DATA IndexObjData;
    ULONG PreserveMaskData;
    ULONG PreserveMask;
    ULONG AccSize;
    ULONG AccMask;
    ULONG Stage;
    NTSTATUS Status;

    DPRINT("AccessFieldData: %p, %p, %p, %p, %X\n", AmliContext, DataObj, FieldDesc, OutData, IsRead);

    giIndent++;

    if (DataObj->DataType == 0xE)
    {
        if (IsRead)
        {
            Status = ReadBuffField(DataObj->DataBuff, FieldDesc, OutData);
        }
        else
        {
            DPRINT1("AccessFieldData: FIXME\n");
            ASSERT(FALSE);
        }

        goto Exit;
    }

    FieldUnitObj = DataObj->DataBuff;
    PreserveMaskData = 0;
    DataObj = NULL;

    if (FieldUnitObj->NsFieldParent->ObjData.DataType != 0x84)
    {
        Status = GetFieldUnitRegionObj(FieldUnitObj, &FieldUnitRegionObj);
        if (Status == STATUS_SUCCESS && FieldUnitRegionObj)
        {
            Status = AccessBaseField(AmliContext, FieldUnitRegionObj, FieldDesc, OutData, IsRead);
        }

        goto Exit;
    }

    ParentObjData = &FieldUnitObj->NsFieldParent->ObjData;
    IndexFieldObj = ParentObjData->DataBuff;

    if (IsRead)
    {
        IndexObjData = &IndexFieldObj->DataObj->ObjData;

        Status = PushAccFieldObj(AmliContext, ReadFieldObj, IndexObjData, IndexObjData->DataBuff, (PUCHAR)OutData, 4);
        goto Exit;
    }

    if (FieldDesc->NumBits < 0x20)
        PreserveMaskData = (1 << FieldDesc->NumBits);

    Stage = (FieldDesc->FieldFlags & 0xF);

    PreserveMask = ~((PreserveMaskData - 1) << FieldDesc->StartBitPos);

    if (Stage >= 1 && Stage <= 3)
        AccSize = (1 << (Stage - 1));
    else
        AccSize = 1;

    if ((8 * AccSize) < 0x20)
        AccMask = ((1 << (8 * AccSize)) - 1);
    else
        AccMask = 0xFFFFFFFF;

    if (!(FieldDesc->FieldFlags & 0x60) && (AccMask & PreserveMask))
    {
        DPRINT1("AccessFieldData: FIXME\n");
        ASSERT(FALSE);
        goto Exit;
    }

    DPRINT1("AccessFieldData: FIXME\n");
    ASSERT(FALSE);

Exit:

    giIndent--;

    //DPRINT("AccessFieldData: ret Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
WriteFieldObj(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_ACCESS_FIELD_OBJECT Afo,
    _In_ NTSTATUS InStatus)
{
    PAMLI_FIELD_UNIT_OBJECT FieldUnitObj;
    PAMLI_FIELD_UNIT_OBJECT ParentFieldUnitObj;
    PAMLI_OBJECT_DATA ParentObjData;
    PAMLI_INDEX_FIELD_OBJECT IndexFieldObj;
    ULONG Stage;
    ULONG data;

    if (InStatus != STATUS_SUCCESS)
        Stage = 3;
    else
        Stage = (Afo->FrameHeader.Flags & 0xF);

    DPRINT("WriteFieldObj: %X, %X, %X, %X\n", Stage, AmliContext, Afo, InStatus);

    giIndent++;

    ASSERT(Afo->FrameHeader.Signature == 'OFCA');//SIG_ACCFIELDOBJ

    if (Stage == 0)
        goto Stage0;
    else if (Stage == 1)
        goto Stage1;
    else if (Stage == 2)
        goto Stage2;
    else if (Stage == 3)
        PopFrame(AmliContext);

    goto Exit;

Stage0:

    while (TRUE)
    {
        if (Afo->CurrentNum >= Afo->AccCount)
        {
            Afo->FrameHeader.Flags += 3;
            PopFrame(AmliContext);
            goto Exit;
        }

        Afo->FrameHeader.Flags++;

        if (Afo->DataObj->DataType == 5)
        {
            FieldUnitObj = Afo->DataObj->DataBuff;
            ParentObjData = &FieldUnitObj->NsFieldParent->ObjData;

            if (ParentObjData->DataType == 0x84)
            {
                IndexFieldObj = ParentObjData->DataBuff;
                ParentFieldUnitObj = IndexFieldObj->IndexObj->ObjData.DataBuff;

                InStatus = PushAccFieldObj(AmliContext,
                                           WriteFieldObj,
                                           &IndexFieldObj->IndexObj->ObjData,
                                           &ParentFieldUnitObj->FieldDesc,
                                           (PUCHAR)&Afo->FieldDesc.ByteOffset,
                                           4);
                goto Exit;
            }
        }

Stage1:

        Afo->FrameHeader.Flags++;

        data = ReadSystemMem(Afo->BufferStart, Afo->AccSize, Afo->Mask);

        if (Afo->CurrentNum > 0)
        {
            Afo->Data = data >> Afo->BitPos1;
            Afo->BufferStart += Afo->AccSize;

            if (Afo->BufferStart < Afo->BufferEnd)
                data = ReadSystemMem(Afo->BufferStart, Afo->AccSize, Afo->Mask);
            else
                data = 0;
        }
        else
        {
            Afo->Data = 0;
        }

        Afo->Data |= (Afo->Mask & (data << Afo->BitPos2));

        InStatus = AccessFieldData(AmliContext, Afo->DataObj, &Afo->FieldDesc, &Afo->Data, FALSE);
        if (InStatus == 0x8004)
            break;

        if (Afo != AmliContext->LocalHeap.HeapEnd)
            break;

Stage2:

        Afo->FieldDesc.ByteOffset += Afo->AccSize;
        Afo->FieldDesc.StartBitPos = 0;
        Afo->FieldDesc.NumBits += (Afo->FieldDesc.StartBitPos - 8 * Afo->AccSize);

        Afo->CurrentNum++;

        if (Afo->CurrentNum >= Afo->AccCount)
        {
            Afo->FrameHeader.Flags++;
            PopFrame(AmliContext);
            goto Exit;
        }

        Afo->FrameHeader.Flags -= 2;
    }

Exit:

    giIndent--;

    //DPRINT("WriteFieldObj: ret InStatus %X\n", InStatus);
    return InStatus;
}

NTSTATUS
__cdecl
ReadFieldObj(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_ACCESS_FIELD_OBJECT Afo,
    _In_ NTSTATUS InStatus)
{
    PAMLI_FIELD_UNIT_OBJECT FieldUnitObj;
    PAMLI_FIELD_UNIT_OBJECT ParentFieldUnitObj;
    PAMLI_OBJECT_DATA ParentObjData;
    PAMLI_INDEX_FIELD_OBJECT IndexFieldObj;
    ULONG Stage;
    ULONG Data;
    ULONG mask;

    if (InStatus != STATUS_SUCCESS)
        Stage = 3;
    else
        Stage = (Afo->FrameHeader.Flags & 0xF);

    DPRINT("ReadFieldObj: %X, %X, %X, %X\n", Stage, AmliContext, Afo, InStatus);

    giIndent++;

    ASSERT(Afo->FrameHeader.Signature == 'OFCA');

    if (Stage == 0)
        goto Stage0;
    else if (Stage == 1)
        goto Stage1;
    else if (Stage == 2)
        goto Stage2;
    else if (Stage == 3)
        PopFrame(AmliContext);

    goto Exit;

Stage0:

    while (TRUE)
    {
        if (Afo->CurrentNum >= Afo->AccCount)
        {
            Afo->FrameHeader.Flags += 3;
            PopFrame(AmliContext);
            break;
        }

        Afo->FrameHeader.Flags++;

        if (Afo->DataObj->DataType == 5)
        {
            FieldUnitObj = Afo->DataObj->DataBuff;
            ParentObjData = &FieldUnitObj->NsFieldParent->ObjData;

            if (ParentObjData->DataType == 0x84)
            {
                IndexFieldObj = ParentObjData->DataBuff;
                ParentFieldUnitObj = IndexFieldObj->IndexObj->ObjData.DataBuff;

                InStatus = PushAccFieldObj(AmliContext,
                                           WriteFieldObj,
                                           &IndexFieldObj->IndexObj->ObjData,
                                           &ParentFieldUnitObj->FieldDesc,
                                           (PUCHAR)&Afo->FieldDesc.ByteOffset,
                                           4);
                break;
            }
        }

  Stage1:

        Afo->FrameHeader.Flags++;

        InStatus = AccessFieldData(AmliContext, Afo->DataObj, &Afo->FieldDesc, &Afo->Data, TRUE);
        if (InStatus != STATUS_SUCCESS)
            break;

        if (Afo != AmliContext->LocalHeap.HeapEnd)
            break;

  Stage2:

        if (Afo->CurrentNum > 0)
        {
            mask = ((Afo->BitPos2 < 0x20) ? (1 << Afo->BitPos2) : 0);
            Data = ((Afo->BitPos1 < 0x20) ? (Afo->Data << Afo->BitPos1) : 0);

            WriteSystemMem(Afo->BufferStart, Afo->AccSize, (Afo->Mask & Data), ((mask - 1) << Afo->BitPos1));

            Afo->BufferStart += Afo->AccSize;
            if (Afo->BufferStart >= Afo->BufferEnd)
            {
                Afo->FrameHeader.Flags++;
                PopFrame(AmliContext);
            }
        }

        Afo->Data >>= Afo->BitPos2;

        if (Afo->FieldDesc.NumBits < Afo->BitPos1)
        {
            mask = ((Afo->FieldDesc.NumBits < 0x20) ? (1 << Afo->FieldDesc.NumBits) : 0);
            Afo->Data &= (mask - 1);
        }

        mask = ((Afo->BitPos1 < 0x20) ? (1 << Afo->BitPos1) : 0);
        WriteSystemMem(Afo->BufferStart, Afo->AccSize, Afo->Data, ((mask - 1) >> Afo->BitPos2));

        Afo->FieldDesc.ByteOffset += Afo->AccSize;
        Afo->FieldDesc.NumBits += (Afo->FieldDesc.StartBitPos - 8 * Afo->AccSize);
        Afo->FieldDesc.StartBitPos = 0;

        Afo->CurrentNum++;

        if (Afo->CurrentNum >= Afo->AccCount)
        {
            Afo->FrameHeader.Flags++;
            PopFrame(AmliContext);
        }

        Afo->FrameHeader.Flags -= 2;
    }

Exit:

    giIndent--;

    return InStatus;
}

NTSTATUS
__cdecl
PushAccFieldObj(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PVOID AccCallBack,
    _In_ PAMLI_OBJECT_DATA DataObj,
    _In_ PAMLI_FIELD_DESCRIPTOR FieldDesc,
    _In_ PUCHAR Buffer,
    _In_ ULONG ByteCount)
{
    PAMLI_ACCESS_FIELD_OBJECT Afo;
    ULONG AccSize;
    ULONG Shift;
    ULONG Bits;
    ULONG Size;
    NTSTATUS Status;

    DPRINT("PushAccFieldObj: %p, %p, %p, %p, %p, %X\n", AmliContext, AccCallBack, DataObj, FieldDesc, Buffer, ByteCount);

    giIndent++;

    Status = PushFrame(AmliContext, 'OFCA', sizeof(*Afo), AccCallBack, (PVOID *)&Afo);
    if (Status == STATUS_SUCCESS)
    {
        Afo->DataObj = DataObj;
        Afo->BufferStart = Buffer;
        Afo->BufferEnd = &Buffer[ByteCount];

        Shift = (FieldDesc->FieldFlags & 0xF);

        if (Shift >= 1 && Shift <= 3)
            AccSize = (1 << (Shift - 1));
        else
            AccSize = 1;

        Afo->AccSize = AccSize;
        ASSERT((AccSize == sizeof(UCHAR)) || (AccSize == sizeof(USHORT)) || (AccSize == sizeof(ULONG)));

        Afo->AccCount = (FieldDesc->StartBitPos + FieldDesc->NumBits + (8 * Afo->AccSize - 1)) / (8 * Afo->AccSize);

        Bits = (8 * Afo->AccSize);

        if (Bits < 0x20)
            Size = (1 << Bits);
        else
            Size = 0;

        Afo->Mask = (Size - 1);

        Afo->BitPos1 = (8 * Afo->AccSize - FieldDesc->StartBitPos);
        Afo->BitPos2 = FieldDesc->StartBitPos;

        Afo->FieldDesc = *FieldDesc;
    }

    giIndent--;

    DPRINT("PushAccFieldObj: Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
WriteField(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_OBJECT_DATA DataObj,
    _In_ PAMLI_FIELD_DESCRIPTOR FieldDesc,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
ReadField(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_OBJECT_DATA DataObj,
    _In_ PAMLI_FIELD_DESCRIPTOR FieldDesc,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    PVOID DataBuff;
    PVOID TargetBuffer;
    ULONG Size;
    NTSTATUS Status;

    DPRINT("ReadField: %X, %X, %X, %X\n", AmliContext, DataObj, FieldDesc, DataResult);

    giIndent++;

    if ((FieldDesc->FieldFlags & 0xF) > 3)
    {
        if (DataObj->DataType == 5)
        {
            DPRINT1("ReadField: failed to allocate target buffer (size %X)\n", DataResult->DataLen);
            ASSERT(FALSE);
        }
        else
        {
            DPRINT1("ReadField: invalid access size for buffer field (FieldFlags %X)\n", FieldDesc->FieldFlags);
            ASSERT(FALSE);
            Status = STATUS_ACPI_INVALID_ACCESS_SIZE;
        }

        goto Exit;
    }

    if (DataResult->DataType == 0)
    {
        if (FieldDesc->FieldFlags & 0x10000 || FieldDesc->NumBits > 0x20)
        {
            DataResult->DataType = 3;
            DataResult->DataLen = (FieldDesc->NumBits + 7) >> 3;

            gdwcBDObjs++;

            DataResult->DataBuff = TargetBuffer = HeapAlloc(gpheapGlobal, 'FUBH', DataResult->DataLen);
            if (!TargetBuffer)
            {
                DPRINT1("ReadField: failed to allocate target buffer (size %X)\n", DataResult->DataLen);
                ASSERT(FALSE);
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Exit;
            }

            RtlZeroMemory(TargetBuffer, DataResult->DataLen);

            DataBuff = DataResult->DataBuff;
            Size = DataResult->DataLen;
        }
        else
        {
            DataResult->DataType = 1;
            DataBuff = &DataResult->DataValue;
            Size = 4;
        }
    }
    else if (DataResult->DataType == 1)
    {
        DataBuff = &DataResult->DataValue;
        Size = 4;
    }
    else if (DataResult->DataType == 2)
    {
        DataBuff = DataResult->DataBuff;
        Size = (DataResult->DataLen - 1);
    }
    else if (DataResult->DataType == 3)
    {
        DataBuff = DataResult->DataBuff;
        Size = DataResult->DataLen;
    }
    else
    {
        DPRINT1("ReadField: invalid target data type (type '%s')\n", GetObjectTypeName(DataResult->DataType));
        ASSERT(FALSE);
        Status = STATUS_ACPI_INVALID_OBJTYPE;
        goto Exit;
    }

    Status = PushAccFieldObj(AmliContext, ReadFieldObj, DataObj, FieldDesc, DataBuff, Size);

Exit:

    giIndent--;

    //DPRINT1("ReadField: ret Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
AccFieldUnit(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_AFU_CONTEXT AfuContext,
    _In_ NTSTATUS InStatus)
{
    PAMLI_FIELD_UNIT_OBJECT FieldUnitObj;
    PAMLI_OBJECT_DATA DataResult;
    PAMLI_OBJECT_DATA DataObj;
    ULONG Stage;

    if (InStatus != STATUS_SUCCESS)
        Stage = 3;
    else
        Stage = (AfuContext->FrameHeader.Flags & 0xF);

    DPRINT("AccFieldUnit: %X, %p, %p, %X\n", Stage, AmliContext, AfuContext, InStatus);

    giIndent++;

    ASSERT(AfuContext->FrameHeader.Signature == 'UFCA');//SIG_ACCFIELDUNIT

    FieldUnitObj = AfuContext->DataObj->DataBuff;

    if (Stage == 0)
    {
        AfuContext->FrameHeader.Flags++;

        if (FieldUnitObj->NsFieldParent->ObjData.DataType == 0x82)
        {
            DPRINT1("AccFieldUnit: FIXME\n");
            ASSERT(FALSE);
        }

        AfuContext->FrameHeader.Flags++;

        if (NeedGlobalLock(FieldUnitObj))
        {
            DPRINT1("AccFieldUnit: FIXME\n");
            ASSERT(FALSE);
        }
    }
    else if (Stage == 1)
    {
        AfuContext->FrameHeader.Flags++;

        if (NeedGlobalLock(FieldUnitObj))
        {
            DPRINT1("AccFieldUnit: FIXME\n");
            ASSERT(FALSE);
        }
    }
    else if (Stage == 2)
    {
        ;
    }
    else if (Stage == 3)
    {
        goto Exit1;
    }
    else
    {
        goto Exit;
    }

    AfuContext->FrameHeader.Flags++;

    if (FieldUnitObj->FieldDesc.FieldFlags & 0x80000000)
        AfuContext->FrameHeader.Flags |= 0x20000;

    DataResult = AfuContext->DataResult;
    DataObj = AfuContext->DataObj;

    if (AfuContext->FrameHeader.Flags & 0x10000)
    {
        InStatus = ReadField(AmliContext, DataObj, &FieldUnitObj->FieldDesc, DataResult);
    }
    else
    {
        InStatus = WriteField(AmliContext, DataObj, &FieldUnitObj->FieldDesc, DataResult);
    }

    if (InStatus == 0x8004 || AfuContext != AmliContext->LocalHeap.HeapEnd)
        goto Exit;

Exit1:

    if (AfuContext->FrameHeader.Flags & 0x20000)
    {
        DPRINT1("AccFieldUnit: FIXME\n");
        ASSERT(FALSE);
    }

    PopFrame(AmliContext);

Exit:

    giIndent--;

    return InStatus;
}

NTSTATUS
__cdecl
DupObjData(
    _In_ PAMLI_HEAP Heap,
    _In_ PAMLI_OBJECT_DATA Dest,
    _In_ PAMLI_OBJECT_DATA Src)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
WriteObject(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_OBJECT_DATA DataObj,
    _In_ PAMLI_OBJECT_DATA DataSrc)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
    ULONG InitSize;
    ULONG Buffersize;
    NTSTATUS Status;

    InitSize = TermContext->OpEnd - AmliContext->Op;

    DPRINT("Buffer: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    Status = ValidateArgTypes(TermContext->DataArgs, "I");
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("Buffer: Status %X\n", Status);
        ASSERT(FALSE);
        goto Exit;
    }

    Buffersize = (ULONG)TermContext->DataArgs->DataValue;
    if (Buffersize < InitSize)
    {
        DPRINT1("Buffer: too many initializers (buffsize %X, InitSize %X)\n", TermContext->DataArgs->DataValue, InitSize);
        ASSERT(FALSE);
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (!Buffersize)
    {
        DPRINT1("Buffer: invalid buffer size %X\n", TermContext->DataArgs->DataValue);
        ASSERT(FALSE);
        Status = STATUS_INVALID_BUFFER_SIZE;
        goto Exit;
    }

    gdwcBDObjs++;

    TermContext->DataResult->DataBuff = HeapAlloc(gpheapGlobal, 'FUBH', (ULONG)TermContext->DataArgs->DataValue);
    if (!TermContext->DataResult->DataBuff)
    {
        DPRINT1("Buffer: failed to allocate data buffer (size %X)\n", TermContext->DataArgs->DataValue);
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    TermContext->DataResult->DataType = 3;
    TermContext->DataResult->DataLen = (ULONG)TermContext->DataArgs->DataValue;

    RtlZeroMemory(TermContext->DataResult->DataBuff, TermContext->DataResult->DataLen);
    RtlCopyMemory(TermContext->DataResult->DataBuff, AmliContext->Op, InitSize);

    AmliContext->Op = TermContext->OpEnd;

Exit:

    giIndent--;

    //DPRINT("Buffer: ret Status %X\n", Status);
    return Status;
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
    PAMLI_BUFF_FIELD_OBJECT BufferField;
    NTSTATUS Status;

    DPRINT("CreateWordField: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    Status = CreateXField(AmliContext, TermContext, (TermContext->DataArgs + 2), &BufferField);
    if (Status == STATUS_SUCCESS)
    {
        BufferField->FieldDesc.ByteOffset = (ULONG)TermContext->DataArgs[1].DataValue;
        BufferField->FieldDesc.StartBitPos = 0;
        BufferField->FieldDesc.NumBits = 0x10;
        BufferField->FieldDesc.FieldFlags = 2;
    }

    giIndent--;

    //DPRINT("CreateWordField: Status %X\n", Status);
    return Status;
}
NTSTATUS __cdecl DerefOf(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Device(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    PAMLI_NAME_SPACE_OBJECT* OutNsObject;
    PAMLI_FN_HANDLER FnHandler;
    NTSTATUS Status;

    DPRINT("Device: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    OutNsObject = &TermContext->NsObject;

    Status = CreateNameSpaceObject(AmliContext->HeapCurrent,
                                   TermContext->DataArgs->DataBuff,
                                   AmliContext->Scope,
                                   AmliContext->Owner,
                                   &TermContext->NsObject,
                                   0);
    if (Status == STATUS_SUCCESS)
    {
        (*OutNsObject)->ObjData.DataType = 6;

        if (ghCreate.Handler)
        {
            FnHandler = ghCreate.Handler;
            FnHandler(6, *OutNsObject);
        }

        Status = PushScope(AmliContext,
                           AmliContext->Op,
                           TermContext->OpEnd,
                           NULL,
                           *OutNsObject,
                           AmliContext->Owner,
                           AmliContext->HeapCurrent,
                           TermContext->DataResult);
    }

    giIndent--;

    //DPRINT("Device: Status %X\n", Status);
    return Status;
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
    PAMLI_OBJECT_DATA DataObj;
    ULONG Opcode;
    NTSTATUS Status;

    DPRINT("ExprOp2: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    Status = ValidateArgTypes(TermContext->DataArgs, "II");
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("ExprOp2: Status %X\n", Status);
        goto Exit;
    }

    Status = ValidateTarget((TermContext->DataArgs + 2), 0x87, &DataObj);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("ExprOp2: Status %X\n", Status);
        goto Exit;
    }

    TermContext->DataResult->DataType = 1;
    Opcode = TermContext->AmliTerm->Opcode;

    if (Opcode == 0x72)
    {
        giIndent++;
        TermContext->DataResult->DataValue = (PVOID)((ULONG)TermContext->DataArgs[0].DataValue + (ULONG)TermContext->DataArgs[1].DataValue);
        giIndent--;
    }
    else if (Opcode == 0x74)
    {
        giIndent++;
        TermContext->DataResult->DataValue = (PVOID)((ULONG)TermContext->DataArgs[0].DataValue - (ULONG)TermContext->DataArgs[1].DataValue);
        giIndent--;
    }
    else if (Opcode == 0x77)
    {
        giIndent++;
        TermContext->DataResult->DataValue = (PVOID)((ULONG)TermContext->DataArgs[0].DataValue * (ULONG)TermContext->DataArgs[1].DataValue);
        giIndent--;
    }
    else if (Opcode == 0x79)
    {
        giIndent++;
        if ((ULONG)TermContext->DataArgs[1].DataValue < 0x20)
           TermContext->DataResult->DataValue = (PVOID)((ULONG)TermContext->DataArgs[0].DataValue << (CHAR)(ULONG)TermContext->DataArgs[1].DataValue);
        else
           TermContext->DataResult->DataValue = 0;
        giIndent--;
    }
    else if (Opcode == 0x7A)
    {
        giIndent++;
        if ((ULONG)TermContext->DataArgs[1].DataValue < 0x20)
           TermContext->DataResult->DataValue = (PVOID)((ULONG)TermContext->DataArgs[0].DataValue >> (CHAR)(ULONG)TermContext->DataArgs[1].DataValue);
        else
           TermContext->DataResult->DataValue = 0;
        giIndent--;
    }
    else if (Opcode == 0x7B)
    {
        giIndent++;
        TermContext->DataResult->DataValue = (PVOID)((ULONG)TermContext->DataArgs[0].DataValue & (ULONG)TermContext->DataArgs[1].DataValue);
        giIndent--;
    }
    else if (Opcode == 0x7C)
    {
        giIndent++;
        TermContext->DataResult->DataValue = (PVOID)~((ULONG)TermContext->DataArgs[0].DataValue & (ULONG)TermContext->DataArgs[1].DataValue);
        giIndent--;
    }
    else if (Opcode == 0x7D)
    {
        giIndent++;
        TermContext->DataResult->DataValue = (PVOID)((ULONG)TermContext->DataArgs[0].DataValue | (ULONG)TermContext->DataArgs[1].DataValue);
        giIndent--;
    }
    else if (Opcode == 0x7E)
    {
        giIndent++;
        TermContext->DataResult->DataValue = (PVOID)~((ULONG)TermContext->DataArgs[0].DataValue | (ULONG)TermContext->DataArgs[1].DataValue);
        giIndent--;
    }
    else if (Opcode == 0x7F)
    {
        giIndent++;
        TermContext->DataResult->DataValue = (PVOID)((ULONG)TermContext->DataArgs[0].DataValue ^ (ULONG)TermContext->DataArgs[1].DataValue);
        giIndent--;
    }

    Status = WriteObject(AmliContext, DataObj, TermContext->DataResult);

Exit:

    giIndent--;

    return Status;
}
NTSTATUS __cdecl Fatal(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Field(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    PAMLI_NAME_SPACE_OBJECT* OutNsObject;
    PAMLI_NAME_SPACE_OBJECT NsObject;
    PAMLI_OP_REGION_OBJECT OpRegionObj;
    NTSTATUS Status;

    DPRINT("Field: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    Status = GetNameSpaceObject((PCHAR)TermContext->DataArgs->DataBuff, AmliContext->Scope, &NsObject, 0x80000000);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("Field: ret Status %X\n", Status);
        ASSERT(FALSE);
        goto Exit;
    }

    if (NsObject->ObjData.DataType != 0xA)
    {
        DPRINT1("Field: %s is not an operation region\n", TermContext->DataArgs->DataBuff);
        ASSERT(FALSE);
        Status = STATUS_ACPI_INVALID_OBJTYPE;
    }

    OutNsObject = &TermContext->NsObject;

    Status = CreateNameSpaceObject(AmliContext->HeapCurrent, 0, AmliContext->Scope, AmliContext->Owner, &TermContext->NsObject, 0);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("Field: ret Status %X\n", Status);
        ASSERT(FALSE);
        goto Exit;
    }

    (*OutNsObject)->ObjData.DataType = 0x83;
    (*OutNsObject)->ObjData.DataLen = 4;

    gdwcFObjs++;

    (*OutNsObject)->ObjData.DataBuff = HeapAlloc(AmliContext->HeapCurrent, 'ODFH', (*OutNsObject)->ObjData.DataLen);

    if ((*OutNsObject)->ObjData.DataBuff == NULL)
    {
        DPRINT1("Field: failed to allocate Field object\n");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory((*OutNsObject)->ObjData.DataBuff, (*OutNsObject)->ObjData.DataLen);

    *(PAMLI_NAME_SPACE_OBJECT *)(*OutNsObject)->ObjData.DataBuff = NsObject;

    OpRegionObj = NsObject->ObjData.DataBuff;
    Status = ParseFieldList(AmliContext, TermContext->OpEnd, *OutNsObject, (ULONG)TermContext->DataArgs[1].DataValue, OpRegionObj->Len);

Exit:

    giIndent--;

    //DPRINT("Field: ret Status %X\n", Status);
    return Status;
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
    PAMLI_NAME_SPACE_OBJECT* OutNsObject;
    PAMLI_NAME_SPACE_OBJECT* NsObjects;
    PAMLI_NAME_SPACE_OBJECT NsObject1;
    PAMLI_NAME_SPACE_OBJECT NsObject0;
    NTSTATUS Status;

    DPRINT("IndexField: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    Status = GetNameSpaceObject(TermContext->DataArgs->DataBuff, AmliContext->Scope, &NsObject0, 0x80000000);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("IndexField: Status %X\n", Status);
        ASSERT(FALSE);
        goto Exit;
    }

    Status = GetNameSpaceObject(TermContext->DataArgs[1].DataBuff, AmliContext->Scope, &NsObject1, 0x80000000);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("IndexField: Status %X\n", Status);
        ASSERT(FALSE);
        goto Exit;
    }

    if (NsObject0->ObjData.DataType != 5)
    {
        DPRINT1("IndexField: Index '%s' is not a field unit\n", TermContext->DataArgs->DataBuff);
        ASSERT(FALSE);
        Status = STATUS_ACPI_INVALID_OBJTYPE;
    }

    if (NsObject1->ObjData.DataType != 5)
    {
        DPRINT1("IndexField: Data '%s' is not a field unit\n", TermContext->DataArgs[1].DataBuff);
        ASSERT(FALSE);
        Status = STATUS_ACPI_INVALID_OBJTYPE;
    }

    OutNsObject = &TermContext->NsObject;

    Status = CreateNameSpaceObject(AmliContext->HeapCurrent,
                                   NULL,
                                   AmliContext->Scope,
                                   AmliContext->Owner,
                                   &TermContext->NsObject,
                                   0);
    if (Status != STATUS_SUCCESS)
    {
        goto Exit;
    }

    (*OutNsObject)->ObjData.DataType = 0x84;
    (*OutNsObject)->ObjData.DataLen = 8;

    gdwcIFObjs++;

    (*OutNsObject)->ObjData.DataBuff = HeapAlloc(AmliContext->HeapCurrent, 'FXIH', (*OutNsObject)->ObjData.DataLen);
    if ((*OutNsObject)->ObjData.DataBuff == NULL)
    {
        DPRINT1("IndexField: failed to allocate IndexField object\n");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory((*OutNsObject)->ObjData.DataBuff, (*OutNsObject)->ObjData.DataLen);

    NsObjects = (PAMLI_NAME_SPACE_OBJECT *)(*OutNsObject)->ObjData.DataBuff;
    NsObjects[0] = NsObject0;
    NsObjects[1] = NsObject1;

    Status = ParseFieldList(AmliContext, TermContext->OpEnd, *OutNsObject, (ULONG)TermContext->DataArgs[2].DataValue, 0xFFFFFFFF);

Exit:

    giIndent--;

    //DPRINT("IndexField: ret Status %X\n", Status);
    return Status;
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
    PAMLI_NAME_SPACE_OBJECT* OutNsObject;
    PUCHAR Op;
    NTSTATUS Status;

    DPRINT("Method: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    OutNsObject = &TermContext->NsObject;

    Status = CreateNameSpaceObject(AmliContext->HeapCurrent,
                                   (PCHAR)TermContext->DataArgs->DataBuff,
                                   AmliContext->Scope,
                                   AmliContext->Owner,
                                   &TermContext->NsObject,
                                   0);

    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("Method: Status %X\n", Status);
        ASSERT(FALSE);
        goto Exit;
    }

    (*OutNsObject)->ObjData.DataType = 8;
    (*OutNsObject)->ObjData.DataLen = (TermContext->OpEnd - AmliContext->Op + 0x11);

    gdwcMEObjs++;

    (*OutNsObject)->ObjData.DataBuff = HeapAlloc(AmliContext->HeapCurrent, 'TEMH', (*OutNsObject)->ObjData.DataLen);
    if ((*OutNsObject)->ObjData.DataBuff == NULL)
    {
        DPRINT1("Method: failed to allocate method buffer\n");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Op = (*OutNsObject)->ObjData.DataBuff;

    DPRINT("Method: FIXME AddObjSymbol()\n");

    RtlZeroMemory(Op, (*OutNsObject)->ObjData.DataLen);

    Op[0x10] = *(AmliContext->Op - 1);
    RtlCopyMemory((Op + 0x11), AmliContext->Op, (TermContext->OpEnd - AmliContext->Op));

    OutNsObject = &TermContext->NsObject;
    AmliContext->Op = TermContext->OpEnd;

Exit:

    giIndent--;

    //DPRINT("Method: ret Status %X\n", Status);
    return Status;
}
NTSTATUS __cdecl Mutex(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Name(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    PAMLI_NAME_SPACE_OBJECT *OutNsObject;
    NTSTATUS Status;

    DPRINT("Method: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    ASSERT(TermContext->DataArgs[0].DataType == 2); // OBJTYPE_STRDATA

    OutNsObject = &TermContext->NsObject;

    Status = CreateNameSpaceObject(AmliContext->HeapCurrent,
                                   (PCHAR)TermContext->DataArgs->DataBuff,
                                   AmliContext->Scope,
                                   AmliContext->Owner,
                                   &TermContext->NsObject,
                                   0);
    if (Status == STATUS_SUCCESS)
        MoveObjData(&((*OutNsObject)->ObjData), (TermContext->DataArgs + 1));

    giIndent--;

    //DPRINT("Name: ret Status %X\n", Status);
    return Status;
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
    PAMLI_NAME_SPACE_OBJECT* OutNsObject;
    PAMLI_OP_REGION_OBJECT OpRegionObject;
    NTSTATUS Status;

    DPRINT("OpRegion: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    OutNsObject = &TermContext->NsObject;

    Status = CreateNameSpaceObject(AmliContext->HeapCurrent,
                                   TermContext->DataArgs->DataBuff,
                                   AmliContext->Scope,
                                   AmliContext->Owner,
                                   &TermContext->NsObject,
                                   0);
    if (Status != STATUS_SUCCESS)
    {
        goto Exit;
    }

    (*OutNsObject)->ObjData.DataType = 10;
    (*OutNsObject)->ObjData.DataLen = sizeof(*OpRegionObject);

    gdwcORObjs++;

    (*OutNsObject)->ObjData.DataBuff = OpRegionObject = HeapAlloc(AmliContext->HeapCurrent, 'GROH', (*OutNsObject)->ObjData.DataLen);

    if (!OpRegionObject)
    {
        DPRINT1("OpRegion: failed to allocate OpRegion object\n");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(OpRegionObject, (*OutNsObject)->ObjData.DataLen);

    OpRegionObject->RegionSpace = (UCHAR)((ULONG_PTR)TermContext->DataArgs[1].DataValue & 0xFF);
    OpRegionObject->Offset = (ULONG)TermContext->DataArgs[2].DataValue;
    OpRegionObject->Len = (ULONG)TermContext->DataArgs[3].DataValue;

    KeInitializeSpinLock(&OpRegionObject->ListLock);

    if (OpRegionObject->RegionSpace == 0)
    {
        DPRINT1("OpRegion: FIXME\n");
        ASSERT(FALSE);
    }
    else if (OpRegionObject->RegionSpace == 1)
    {
        OpRegionObject->Offset = OpRegionObject->Offset;
    }
    else if (OpRegionObject->RegionSpace == 6 && ghCreate.Handler)
    {
        DPRINT1("OpRegion: FIXME\n");
        ASSERT(FALSE);
    }

Exit:

    giIndent--;

    //DPRINT("OpRegion: Status %X\n", Status);
    return Status;
}
NTSTATUS __cdecl OSInterface(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS __cdecl Package(_In_ PAMLI_CONTEXT AmliContext, _In_ PAMLI_TERM_CONTEXT TermContext)
{
    PAMLI_PACKAGE_OBJECT PackageObject;
    PAMLI_PACKAGE_CONTEXT PackageContext;
    NTSTATUS Status;

    DPRINT("Package: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    Status = ValidateArgTypes(TermContext->DataArgs, "I");
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("Package: Status %X\n", Status);
        ASSERT(FALSE);
        goto Exit;
    }

    TermContext->DataResult->DataLen = (sizeof(ULONG) + (ULONG)TermContext->DataArgs[0].DataValue * sizeof(AMLI_OBJECT_DATA));

    gdwcPKObjs++;

    PackageObject = HeapAlloc(gpheapGlobal, 'GKPH', TermContext->DataResult->DataLen);
    if (!PackageObject)
    {
        DPRINT1("Package: failed to allocate package object (size %X)\n", TermContext->DataResult->DataLen);
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    TermContext->DataResult->DataType = 4;

    memset(PackageObject, 0, TermContext->DataResult->DataLen);

    TermContext->DataResult->DataBuff = PackageObject;
    PackageObject->Elements = (UCHAR)(ULONG)TermContext->DataArgs[0].DataValue;

    Status = PushFrame(AmliContext, 'FGKP', sizeof(*PackageContext), ParsePackage, (PVOID *)&PackageContext);
    if (Status == STATUS_SUCCESS)
    {
        PackageContext->PackageObject = PackageObject;
        PackageContext->OpEnd = TermContext->OpEnd;
    }

Exit:

    giIndent--;

    //DPRINT("Package: ret Status %X\n", Status);
    return Status;
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
    NTSTATUS Status;

    DPRINT("Scope: %X, %X, %X\n", AmliContext, AmliContext->Op, TermContext);

    giIndent++;

    ASSERT(TermContext->DataArgs[0].DataType == 2);//OBJTYPE_STRDATA

    Status = GetNameSpaceObject(TermContext->DataArgs->DataBuff,
                                AmliContext->Scope,
                                &TermContext->NsObject,
                                0x80000000);

    if (Status == STATUS_SUCCESS)
    {
        Status = PushScope(AmliContext,
                           AmliContext->Op,
                           TermContext->OpEnd,
                           NULL,
                           TermContext->NsObject,
                           AmliContext->Owner,
                           AmliContext->HeapCurrent,
                           TermContext->DataResult);
    }

    giIndent--;

    //DPRINT("Scope: ret Status %X\n", Status);
    return Status;
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
    PAMLI_HEAP_HEADER NextHeader;
    PAMLI_LIST Entry;

    //DPRINT("HeapInsertFreeList: Heap %X, HeapHeader %X\n", Heap, HeapHeader);

    giIndent++;

    ASSERT(HeapHeader->Length >= sizeof(AMLI_HEAP_HEADER));

    Entry = Heap->ListFreeHeap;
    if (!Entry)
    {
        ListInsertHead(&HeapHeader->List, &Heap->ListFreeHeap);
    }
    else
    {
        Entry = Heap->ListFreeHeap;
        while (&HeapHeader->List >= Entry)
        {
            Entry = Entry->Next;
            if (Entry == Heap->ListFreeHeap)
                break;
        }

        if (&HeapHeader->List >= Entry)
        {
            ListInsertTail(&HeapHeader->List, &Heap->ListFreeHeap);
        }
        else
        {
            HeapHeader->List.Next = Entry;
            HeapHeader->List.Prev = Entry->Prev;
            HeapHeader->List.Prev->Next = &HeapHeader->List;
            HeapHeader->List.Next->Prev = &HeapHeader->List;

            if (Heap->ListFreeHeap == Entry)
                Heap->ListFreeHeap = &HeapHeader->List;
        }
    }

    NextHeader = Add2Ptr(HeapHeader, HeapHeader->Length);

    if (HeapHeader->List.Next == &NextHeader->List)
    {
        ASSERT(NextHeader->Signature == 0);
        HeapHeader->Length += NextHeader->Length;
        ListRemoveEntry(&NextHeader->List, &Heap->ListFreeHeap);
    }

    NextHeader = CONTAINING_RECORD(HeapHeader->List.Prev, AMLI_HEAP_HEADER, List);

    if (Add2Ptr(NextHeader, NextHeader->Length) == HeapHeader)
    {
        ASSERT(NextHeader->Signature == 0);
        NextHeader->Length += HeapHeader->Length;
        ListRemoveEntry(&HeapHeader->List, &Heap->ListFreeHeap);
        HeapHeader = NextHeader;
    }

    if (Add2Ptr(HeapHeader, HeapHeader->Length) >= Heap->HeapTop)
    {
        Heap->HeapTop = HeapHeader;
        ListRemoveEntry(&HeapHeader->List, &Heap->ListFreeHeap);
    }

    giIndent--;
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
    _Out_ PAMLI_NAME_SPACE_OBJECT* OutObject,
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
HeapFree(
    _In_ PVOID Entry)
{
    PAMLI_HEAP_HEADER HeapHeader;
    PAMLI_HEAP heap;
    KIRQL OldIrql;

    ASSERT(Entry != NULL);

    HeapHeader = CONTAINING_RECORD(Entry, AMLI_HEAP_HEADER, List);

    DPRINT("HeapFree: %X, %X, '%s', %X\n", HeapHeader->Heap, Entry, NameSegString(HeapHeader->Signature), HeapHeader->Length);

    giIndent++;

    heap = HeapHeader->Heap;

    ASSERT((ULONG_PTR)HeapHeader >= (ULONG_PTR)&heap->Heap &&
           ((ULONG_PTR)HeapHeader + HeapHeader->Length) <= (ULONG_PTR)heap->HeapEnd);

    ASSERT(HeapHeader->Signature != 0);

    if (Entry && HeapHeader->Signature)
    {
        if (HeapHeader->Heap->HeapHead == gpheapGlobal)
        {
            KeAcquireSpinLock(&gdwGHeapSpinLock, &OldIrql);
            gdwGlobalHeapSize -= HeapHeader->Length;
            KeReleaseSpinLock(&gdwGHeapSpinLock, OldIrql);
        }

        HeapHeader->Signature = 0;

        AcquireMutex(&gmutHeap);
        HeapInsertFreeList(HeapHeader->Heap, HeapHeader);
        ReleaseMutex(&gmutHeap);
    }

    giIndent--;
}

VOID
__cdecl
FreeContext(
    _In_ PAMLI_CONTEXT AmliContext)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
__cdecl
FreeObjData(
    _In_ PAMLI_OBJECT_DATA AmliData)
{
    ULONG DataType;

    DataType = AmliData->DataType;

    DPRINT("FreeObjData: %X, %X\n", AmliData, DataType);

    switch (DataType)
    {
        case 2:
            HeapFree(AmliData->DataBuff);
            gdwcSDObjs--;
            break;

        case 3:
            HeapFree(AmliData->DataBuff);
            gdwcBDObjs--;
            break;

        case 4:
            HeapFree(AmliData->DataBuff);
            gdwcPKObjs--;
            break;

        case 5:
            HeapFree(AmliData->DataBuff);
            gdwcFUObjs--;
            break;

        case 7:
            HeapFree(AmliData->DataBuff);
            gdwcEVObjs--;
            break;

        case 8:
            HeapFree(AmliData->DataBuff);
            gdwcMEObjs--;
            break;

        case 9:
            HeapFree(AmliData->DataBuff);
            gdwcMTObjs--;
            break;

        case 10:
            HeapFree(AmliData->DataBuff);
            gdwcORObjs--;
            break;

        case 11:
            HeapFree(AmliData->DataBuff);
            gdwcPRObjs--;
            break;

        case 12:
            HeapFree(AmliData->DataBuff);
            gdwcPCObjs--;
            break;

        case 14:
            HeapFree(AmliData->DataBuff);
            gdwcBFObjs--;
            break;

        case 0x83:
            HeapFree(AmliData->DataBuff);
            gdwcFObjs--;
            break;

        case 0x84:
            HeapFree(AmliData->DataBuff);
            gdwcIFObjs--;
            break;

        default:
            DPRINT1("FreeObjData: invalid object type '%s'\n", GetObjectTypeName(AmliData->DataType));
            ASSERT(FALSE);
    }
}

VOID
__cdecl
FreeDataBuffs(
    _In_ PAMLI_OBJECT_DATA AmliData,
    _In_ LONG DataCount)
{
    ULONG ix;

    DPRINT("FreeDataBuffs: %X, %X\n", AmliData, DataCount);

    giIndent++;

    for (ix = 0; ix < DataCount; ix++)
    {
        if (AmliData[ix].DataBuff)
        {
            if (AmliData[ix].Flags & 1)
            {
                AmliData[ix].DataBase->RefCount--;
            }
            else
            {
                ASSERT(AmliData[ix].RefCount == 0);

                if (AmliData[ix].DataType == 4)
                {
                    FreeDataBuffs(Add2Ptr(AmliData[ix].DataBuff, 4), *(LONG *)AmliData[ix].DataBuff);
                }

                giIndent++;
                FreeObjData(&AmliData[ix]);
                giIndent--;
            }
        }

        RtlZeroMemory(&AmliData[ix], sizeof(*AmliData));
    }

    giIndent--;
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

BOOLEAN
__cdecl
IsStackEmpty(
    _In_ PAMLI_CONTEXT AmliContext)
{
    BOOLEAN RetIsEmpty;

    giIndent++;
    RetIsEmpty = (AmliContext->LocalHeap.HeapEnd == AmliContext->End);
    giIndent--;

    return RetIsEmpty;
}

VOID
__cdecl
PopFrame(
    _In_ PAMLI_CONTEXT AmliContext)
{
    PAMLI_FRAME_HEADER Header;

    DPRINT("PopFrame: %X\n", AmliContext);

    giIndent++;

    ASSERT(!IsStackEmpty(AmliContext));

    Header = AmliContext->LocalHeap.HeapEnd;
    ASSERT(Header->Signature != 0);

    AmliContext->LocalHeap.HeapEnd = Add2Ptr(AmliContext->LocalHeap.HeapEnd, Header->Length);

    giIndent--;
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
ParseInteger(
    _Inout_ PUCHAR* OutOp,
    _In_ PAMLI_OBJECT_DATA Data,
    _In_ ULONG DataLen)
{
    DPRINT("ParseInteger: %p, %p, %X\n", *OutOp, Data, DataLen);

    giIndent++;

    ASSERT(Data != NULL);

    Data->DataValue = 0;
    Data->DataType = 1;

    RtlCopyMemory(&Data->DataValue, *OutOp, DataLen);

    *OutOp += DataLen;

    giIndent--;

    return STATUS_SUCCESS;
}


NTSTATUS
__cdecl
ParseIntObj(
    _Inout_ PUCHAR* OutOp,
    _In_ PAMLI_OBJECT_DATA DataResult,
    _In_ BOOLEAN ErrOk)
{
    UCHAR Opcode;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ParseIntObj: %X, %X, %X\n", *OutOp, DataResult, ErrOk);

    giIndent++;

    ASSERT(DataResult != NULL);

    Opcode = *(*OutOp)++;

    DataResult->DataType = 1;
    DataResult->DataValue = 0;

    if (Opcode == 0)
    {
        DataResult->DataValue = 0;
    }
    else if (Opcode == 1)
    {
        DataResult->DataValue = ULongToPtr(1);
    }
    else if (Opcode == 0xA)
    {
        DataResult->DataValue = (PVOID)(((ULONG_PTR)DataResult->DataValue & 0xFFFFFF00) + *(PUCHAR)*OutOp);
        *OutOp += 1;
    }
    else if (Opcode == 0xB)
    {
        DataResult->DataValue = (PVOID)(((ULONG_PTR)DataResult->DataValue & 0xFFFF0000) + *(PUSHORT)*OutOp);
        *OutOp += 2;
    }
    else if (Opcode == 0xC)
    {
        DataResult->DataValue = (PVOID)(*(PULONG)*OutOp);
        *OutOp += 4;
    }
    else if (Opcode == 0xFF)
    {
        DataResult->DataValue = (PVOID)0xFFFFFFFF;
    }
    else if (Opcode == 0x305B)
    {
        DataResult->DataValue = ULongToPtr(1);
    }
    else
    {
        --*OutOp;

        DPRINT1("ParseIntObj: invalid opcode %X at %X\n", **OutOp, *OutOp);

        if (!ErrOk)
        {
            ASSERT(FALSE);
        }

        Status = STATUS_ACPI_INVALID_OPCODE;
    }

    giIndent--;

    //DPRINT("ParseIntObj: Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
ParseString(
    _Inout_ PUCHAR* OutOp,
    _In_ PAMLI_OBJECT_DATA DataResult,
    _In_ BOOLEAN ErrOk)
{
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ParseString: %X, %X, %X\n", *OutOp, DataResult, ErrOk);

    giIndent++;

    ASSERT(DataResult != NULL);

    if (**OutOp == 0xD)
    {
        ++*OutOp;

        DataResult->DataType = 2;
        DataResult->DataLen = (StrLen((PCHAR)*OutOp, 0xFFFFFFFF) + 1);

        gdwcSDObjs++;

        DataResult->DataBuff = HeapAlloc(gpheapGlobal, 'RTSH', DataResult->DataLen);

        if (DataResult->DataBuff)
        {
            RtlCopyMemory(DataResult->DataBuff, *OutOp, DataResult->DataLen);
        }
        else
        {
            DPRINT("ParseString: failed to allocate string buffer\n");
            ASSERT(FALSE);
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }

        *OutOp += DataResult->DataLen;
    }
    else
    {
        DPRINT1("ParseString: invalid opcode %X at %X\n", **OutOp, *OutOp);

        if (!ErrOk)
        {
            ASSERT(FALSE);
        }

        Status = STATUS_ACPI_INVALID_OPCODE;
    }

    giIndent--;

    //DPRINT("ParseString: Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
ParseSuperName(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_OBJECT_DATA Data,
    _In_ BOOLEAN AbsentOk)
{
    PAMLI_NAME_SPACE_OBJECT NsObject = NULL;
    PAMLI_TERM OpcodeTerm;
    PUCHAR* PointToOp;
    PUCHAR Op;
    LONG nx;
    UCHAR Opcode;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ParseSuperName: %x, %x, %x, %X\n", AmliContext, AmliContext->Op, Data, AbsentOk);

    giIndent++;

    ASSERT(Data != NULL);

    PointToOp = &AmliContext->Op;
    Op = AmliContext->Op;
    Opcode = *Op;

    if (Opcode == 0)
    {
        ASSERT(Data->DataType == 0);//OBJTYPE_UNKNOWN
        (*PointToOp)++;
        goto Exit;
    }

    if (Opcode == 0x5B && *(Op + 1) == 0x31)
    {
        *PointToOp = (Op + 2);
        Data->DataType = 0x10;
        goto Exit;
    }

    OpcodeTerm = OpcodeTable[Opcode];
    if (!OpcodeTerm)
    {
        DPRINT1("ParseSuperName: invalid SuperName %X at %X\n", **PointToOp, *PointToOp);
        ASSERT(FALSE);
        Status = STATUS_ACPI_INVALID_SUPERNAME;
        goto Exit;
    }

    if (OpcodeTerm->Flags2 & 0x20)
    {
        Status = ParseAndGetNameSpaceObject(&AmliContext->Op, AmliContext->Scope, &NsObject, AbsentOk);
        if (Status == STATUS_SUCCESS)
        {
            if (!NsObject)
            {
                ASSERT(Data->DataType == 0);//OBJTYPE_UNKNOWN
            }
            else
            {
                Data->DataType = 0x80;
                Data->Alias = GetBaseObject(NsObject);
            }
        }

        goto Exit;
    }

    if (OpcodeTerm->Flags2 & 2)
    {
        *PointToOp = (Op + 1);

        nx = (Opcode - 0x68);

        if (nx >= AmliContext->Call->NumberOfArgs)
        {
            DPRINT1("ParseSuperName: Arg %X does not exist\n", nx);
            ASSERT(FALSE);
            Status = STATUS_ACPI_INVALID_ARGUMENT;
        }
        else
        {
            Data->DataType = 0x81;
            Data->DataValue = GetBaseData(&AmliContext->Call->DataArgs[nx]);
        }

        goto Exit;
    }

    if (OpcodeTerm->Flags2 & 4)
    {
        nx = (Opcode - 0x60);

        *PointToOp = (Op + 1);

        Data->DataType = 0x81;
        Data->DataAlias = &AmliContext->Call->Locals[nx];

        goto Exit;
    }

    if (OpcodeTerm->Flags2 & 0x80)
    {
        Status = PushTerm(AmliContext, Op, NULL, OpcodeTable[Opcode], Data);
        (*PointToOp)++;
        goto Exit;
    }

    DPRINT1("ParseSuperName: invalid SuperName %X at %X\n", **PointToOp, *PointToOp);
    ASSERT(FALSE);
    Status = STATUS_ACPI_INVALID_SUPERNAME;

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
ParseNameTail(
    _Inout_ PUCHAR* OutOp,
    _Inout_ PCHAR NameBuff,
    _In_ ULONG BuffLen)
{
    ULONG NameLen;
    PUCHAR Op;
    PCHAR Buffer;
    ULONG BufferSize;
    LONG SegCount;
    UCHAR Opcode;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ParseNameTail: %X, '%s', %X\n", *OutOp, NameBuff, BuffLen);

    giIndent++;

    NameLen = StrLen(NameBuff, 0xFFFFFFFF);

    Op = *OutOp;
    Opcode = *Op;

    if (Opcode == 0)
    {
        *OutOp = (Op + 1);
        goto Exit;
    }
    else if (Opcode == '/')
    {
        *OutOp = (Op + 1);
        SegCount = *(Op + 1);
        *OutOp = (Op + 2);

        if (SegCount <= 0)
            goto Exit;
    }
    else if (Opcode != '.')
    {
        SegCount = 1;
    }
    else
    {
        *OutOp = (Op + 1);
        SegCount = 2;
    }

    Buffer = &NameBuff[NameLen];
    BufferSize = (NameLen + 1);
    NameLen += 4;

    while (NameLen < BuffLen)
    {
        StrCpy(Buffer, (PCHAR)*OutOp, 4);

        Buffer += 4;
        BufferSize += 4;
        NameLen += 4;
        *OutOp += 4;

        SegCount--;
        if (SegCount <= 0)
            goto Exit;

        if (BufferSize < BuffLen)
        {
            StrCpy(Buffer, ".", 0xFFFFFFFF);

            Buffer++;
            BufferSize++;
            NameLen++;
        }
    }

    if (SegCount > 0)
    {
        DPRINT1("ParseNameTail: name too long - '%s'\n", NameBuff);
        ASSERT(FALSE);
        Status = STATUS_NAME_TOO_LONG;
    }

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
ParseName(
    _Inout_ PUCHAR* OutOp,
    _Inout_ PCHAR NameBuff,
    _In_ ULONG BuffLen)
{
    UCHAR Opcode;
    PUCHAR Op;
    ULONG ix;
    NTSTATUS Status;

    giIndent++;

    Opcode = **OutOp;

    if (Opcode == '\\')
    {
        if (BuffLen > 1)
        {
            StrCpy(NameBuff, "\\", 0xFFFFFFFF);
            ++*OutOp;
            Status = ParseNameTail(OutOp, NameBuff, BuffLen);
            goto Exit;
        }

        DPRINT1("ParseName: name too long - '%s'\n", NameBuff);
        ASSERT(FALSE);
        Status = STATUS_NAME_TOO_LONG;
        goto Exit;
    }

    if (Opcode != '^')
    {
        if (BuffLen)
        {
            *NameBuff = 0;
            Status = ParseNameTail(OutOp, NameBuff, BuffLen);
            goto Exit;
        }

        DPRINT1("ParseName: name too long - '%s'\n", NameBuff);
        ASSERT(FALSE);
        Status = STATUS_NAME_TOO_LONG;
        goto Exit;
    }

    if (BuffLen <= 1)
    {
        DPRINT1("ParseName: name too long - '%s'\n", NameBuff);
        ASSERT(FALSE);
        Status = STATUS_NAME_TOO_LONG;
        goto Exit;
    }

    StrCpy(NameBuff, "^", 0xFFFFFFFF);

    ++*OutOp;
    Op = *OutOp;

    for (ix = 1; ix < BuffLen; ix++)
    {
        if (*Op != '^')
            break;

        NameBuff[ix] = '^';

        ++*OutOp;
        Op = *OutOp;
    }

    NameBuff[ix] = 0;

    if (**OutOp == '^')
    {
        DPRINT1("ParseName: name too long - '%s'\n", NameBuff);
        ASSERT(FALSE);
        Status = STATUS_NAME_TOO_LONG;
        goto Exit;
    }

    Status = ParseNameTail(OutOp, NameBuff, BuffLen);

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
ParseObjName(
    _Inout_ PUCHAR* OutOp,
    _In_ PAMLI_OBJECT_DATA Data,
    _In_ BOOLEAN ErrOk)
{
    PAMLI_TERM AmliTerm;
    CHAR Name[256];
    NTSTATUS Status;

    AmliTerm = OpcodeTable[**OutOp];

    DPRINT("ParseObjName: %X, %X, %X\n", *OutOp, Data, ErrOk);

    giIndent++;

    ASSERT(Data != NULL);

    if (AmliTerm && (AmliTerm->Flags2 & 0x20))
    {
        Status = ParseName(OutOp, Name, 0x100);
        if (Status)
            goto Exit;

        Data->DataType = 2;
        Data->DataLen = (StrLen(Name, 0xFFFFFFFF) + 1);

        gdwcSDObjs++;

        Data->DataBuff = HeapAlloc(gpheapGlobal, 'RTSH', Data->DataLen);
        if (Data->DataBuff)
        {
            RtlCopyMemory(Data->DataBuff, Name, Data->DataLen);
            goto Exit;
        }

        DPRINT1("ParseObjName: failed to allocate name buffer '%s'\n", Name);
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        if (ErrOk)
        {
            Status = STATUS_ACPI_INVALID_OPCODE;
            goto Exit;
        }

        DPRINT1("ParseObjName: invalid opcode %X at %X\n", **OutOp, *OutOp);
        ASSERT(FALSE);
        Status = STATUS_ACPI_INVALID_OPCODE;
    }

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
ParseArg(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ CHAR ArgType,
    _In_ PAMLI_OBJECT_DATA DataArg)
{
    PUCHAR* OutOp;
    UCHAR idx;
    NTSTATUS Status;

    DPRINT("ParseArg: %X, %X, %c, %X\n", AmliContext, AmliContext->Op, ArgType, DataArg);

    giIndent++;

    ASSERT(DataArg != NULL);

    if (ArgType == 'S')
    {
        Status = ParseSuperName(AmliContext, DataArg, 0);
    }
    else if (ArgType == 'W')
    {
        Status = ParseInteger(&AmliContext->Op, DataArg, 2);
    }
    else if (ArgType == 's')
    {
        Status = ParseSuperName(AmliContext, DataArg, 1);
    }
    else if (ArgType == 'O')
    {
        OutOp = &AmliContext->Op;

        Status = ParseIntObj(&AmliContext->Op, DataArg, 1);

        if (Status == STATUS_ACPI_INVALID_OPCODE)
        {
            Status = ParseString(&AmliContext->Op, DataArg, TRUE);
            if (Status == STATUS_ACPI_INVALID_OPCODE)
            {
                idx = **OutOp;
                if (idx == 0x11 || idx == 0x12)
                {
                    Status = PushTerm(AmliContext, (*OutOp)++, 0, OpcodeTable[idx], DataArg);
                }
            }
        }
    }
    else if (ArgType == 'B')
    {
        Status = ParseInteger(&AmliContext->Op, DataArg, 1);
    }
    else if (ArgType == 'C')
    {
        Status = ParseOpcode(AmliContext, 0, DataArg);
    }
    else if (ArgType == 'D')
    {
        Status = ParseInteger(&AmliContext->Op, DataArg, 4);
    }
    else if (ArgType == 'N')
    {
        Status = ParseObjName(&AmliContext->Op, DataArg, 0);
    }
    else
    {
        DPRINT1("ParseArg: unexpected arguemnt type (%c)\n", ArgType);
        ASSERT(FALSE);
        Status = STATUS_ACPI_ASSERT_FAILED;
    }

    giIndent--;

    //DPRINT("ParseArg: Status %X\n", Status);
    return Status;
}

ULONG
__cdecl
ParsePackageLen(
    _Inout_ PUCHAR* OutOp,
    _Out_ PUCHAR* OutOpEnd)
{
    PUCHAR Op;
    ULONG MaxSize;
    ULONG ix;
    ULONG Result;

    DPRINT("ParsePackageLen: %X, %X\n", *OutOp, OutOpEnd);

    giIndent++;

    if (OutOpEnd)
        *OutOpEnd = *OutOp;

    Result = **OutOp;
    MaxSize = ((**OutOp >> 6) & 3);

    (*OutOp)++;

    Op = *OutOp;

    if (MaxSize)
    {
        Result &= 0xF;

        for (ix = 0; ix < MaxSize; ix++)
        {
            Result |= (*Op++ << (4 + (ix * 8)));
            *OutOp = Op;
        }
    }

    if (OutOpEnd)
        *OutOpEnd += Result;

    giIndent--;

    return Result;
}

NTSTATUS
__cdecl
ParseField(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_NAME_SPACE_OBJECT NsParentObject,
    _Out_ ULONG* OutFieldFlags,
    _Out_ ULONG* OutBitPos)
{
    PAMLI_FIELD_UNIT_OBJECT FieldUnitObject;
    PAMLI_NAME_SPACE_OBJECT NsObject;
    PUCHAR Op;
    ULONG AccessFlags;
    ULONG ByteOffset;
    ULONG Access;
    ULONG NumBits;
    CHAR Name[8];
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ParseField: %X, %X, %X, %X, %X\n", AmliContext, AmliContext->Op, NsParentObject, *OutFieldFlags, *OutBitPos);

    giIndent++;

    Op = AmliContext->Op;

    if (*AmliContext->Op == 1)
    {
        AmliContext->Op++;

        *OutFieldFlags &= ~0xF;
        *OutFieldFlags |= (*AmliContext->Op & 0xF);

        AmliContext->Op++;

        *OutFieldFlags &= ~0xFF00;
        *OutFieldFlags |= (*AmliContext->Op * 0x100);

        AmliContext->Op++;

        goto Exit;
    }

    AccessFlags = (*OutFieldFlags & 0xF);

    if (AccessFlags >= 1 && AccessFlags <= 3)
        Access = (1 << (AccessFlags - 1));
    else
        Access = 1;

    if (*Op)
    {
        StrCpy(Name, (PCHAR)Op, 4);
        AmliContext->Op += 4;
    }
    else
    {
        Name[0] = 0;
        AmliContext->Op++;
    }

    NumBits = ParsePackageLen(&AmliContext->Op, 0);

    Status = CreateNameSpaceObject(AmliContext->HeapCurrent, Name, AmliContext->Scope, AmliContext->Owner, &NsObject, 0);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT("ParseField: Status %X\n", Status);
        goto Exit;
    }

    NsObject->ObjData.DataType = 5;
    NsObject->ObjData.DataLen = sizeof(AMLI_FIELD_UNIT_OBJECT);

    gdwcFUObjs++;

    NsObject->ObjData.DataBuff = FieldUnitObject = HeapAlloc(AmliContext->HeapCurrent, 'UDFH', NsObject->ObjData.DataLen);
    if (!NsObject->ObjData.DataBuff)
    {
        DPRINT1("ParseField: failed to allocate FieldUnit object\n");
        ASSERT(FALSE);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(FieldUnitObject, NsObject->ObjData.DataLen);

    FieldUnitObject = NsObject->ObjData.DataBuff;
    FieldUnitObject->NsFieldParent = NsParentObject;
    FieldUnitObject->FieldDesc.FieldFlags = *OutFieldFlags;

    ByteOffset = (Access * (*OutBitPos / (8 * Access)));

    FieldUnitObject->FieldDesc.ByteOffset = ByteOffset;
    FieldUnitObject->FieldDesc.StartBitPos = (*OutBitPos - (8 * ByteOffset));
    FieldUnitObject->FieldDesc.NumBits = NumBits;

    *OutBitPos += NumBits;

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
ParseFieldList(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PUCHAR OpEnd,
    _In_ PAMLI_NAME_SPACE_OBJECT NsParentObject,
    _In_ ULONG FieldFlags,
    _In_ ULONG RegionLen)
{
    ULONG Offset;
    ULONG BitPos = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ParseFieldList: %X, %X, %X, %X, %X\n", AmliContext, AmliContext->Op, NsParentObject, FieldFlags, RegionLen);

    giIndent++;

    do
    {
        if (AmliContext->Op >= OpEnd)
            break;

        Status = ParseField(AmliContext, NsParentObject, &FieldFlags, &BitPos);
        if (Status)
            break;

        if (RegionLen != -1)
        {
            Offset = ((BitPos + 7) >> 3);

            if (Offset > RegionLen)
            {
                DPRINT1("ParseFieldList: offset exceeds OpRegion range (Offset %X, RegionLen %X\n", Offset, RegionLen);
                ASSERT(FALSE);
                Status = STATUS_ACPI_INVALID_INDEX;
            }
        }
    }
    while (Status == STATUS_SUCCESS);

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
ParseTerm(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_TERM_CONTEXT TermContext,
    _In_ NTSTATUS InStatus)
{
    PAMLI_TERM_HANDLER Handler;
    PAMLI_TERM AmliTerm;
    ULONG ArgIndex;
    ULONG Stage;

    if (InStatus != STATUS_SUCCESS)
        Stage = 4;
    else
        Stage = (TermContext->FrameHeader.Flags & 0xF);

    DPRINT("ParseTerm: '%s', %X, %X, %X, %X, %X\n", TermContext->AmliTerm->Name, Stage, AmliContext, AmliContext->Op, TermContext, InStatus);

    giIndent++;

    ASSERT(TermContext->FrameHeader.Signature == 'MRET');//SIG_TERM

    if (Stage == 0)
    {
        TermContext->FrameHeader.Flags++; // next stage

        if (TermContext->AmliTerm->Flags2 & 1)
            ParsePackageLen(&AmliContext->Op, &TermContext->OpEnd);

        goto Stage1;
    }
    else if (Stage == 1)
    {
Stage1:
        while (TermContext->ArgIndex < TermContext->NumberOfArgs)
        {
            while (TermContext->ArgIndex < TermContext->NumberOfArgs)
            {
                ArgIndex = TermContext->ArgIndex;
                TermContext->ArgIndex++;

                InStatus = ParseArg(AmliContext, TermContext->AmliTerm->TypesOfArgs[ArgIndex], &TermContext->DataArgs[ArgIndex]);

                if (InStatus != STATUS_SUCCESS || TermContext != AmliContext->LocalHeap.HeapEnd)
                    goto Exit;
            }

            if (InStatus != STATUS_SUCCESS || TermContext != AmliContext->LocalHeap.HeapEnd)
                goto Exit;
        }

        TermContext->FrameHeader.Flags++; // next stage
        goto Stage2;
    }
    else if (Stage == 2)
    {
Stage2:
        TermContext->FrameHeader.Flags++; // next stage
        AmliTerm = TermContext->AmliTerm;

        if (AmliTerm->Flags2 & 0x80000000)
        {
            if (AmliTerm->CallBack)
            {
                DPRINT1("ParseTerm: FIXME\n");
                ASSERT(FALSE);
            }
        }

        Handler = TermContext->AmliTerm->Handler;
        if (!Handler)
        {
            goto Stage3;
        }

        InStatus = Handler(AmliContext, TermContext);
        if (InStatus == STATUS_SUCCESS && TermContext == AmliContext->LocalHeap.HeapEnd)
        {
            goto Stage3;
        }
    }
    else if (Stage == 3)
    {
Stage3:
        TermContext->FrameHeader.Flags++; // next stage
        AmliTerm = TermContext->AmliTerm;

        if (AmliTerm->CallBack)
        {
            if (AmliTerm->Flags2 & 0x80000000)
            {
                DPRINT1("ParseTerm: FIXME\n");
                ASSERT(FALSE);
            }
            else
            {
                DPRINT1("ParseTerm: FIXME\n");
                ASSERT(FALSE);
            }
        }

        goto Stage4;
    }
    else if (Stage == 4)
    {
Stage4:
        if (TermContext->DataArgs)
        {
            FreeDataBuffs(TermContext->DataArgs, TermContext->NumberOfArgs);
            HeapFree(TermContext->DataArgs);
            gdwcODObjs--;
        }

        PopFrame(AmliContext);
    }

Exit:

    giIndent--;

    //DPRINT("ParseTerm: Status %X\n", InStatus);
    return InStatus;
}

NTSTATUS
__cdecl
PushTerm(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PUCHAR OpTerm,
    _In_ PUCHAR ScopeEnd,
    _In_ PAMLI_TERM AmliTerm,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    PAMLI_TERM_CONTEXT TermContext;
    ULONG Size;
    NTSTATUS Status;

    DPRINT("PushTerm: %X, %X, %X, %X, %X\n", AmliContext, OpTerm, ScopeEnd, AmliTerm, DataResult);

    giIndent++;

    Status = PushFrame(AmliContext, 'MRET', sizeof(*TermContext), ParseTerm, (PVOID *)&TermContext);
    if (Status == STATUS_SUCCESS)
    {
        TermContext->Op = OpTerm;
        TermContext->ScopeEnd = ScopeEnd;
        TermContext->AmliTerm = AmliTerm;
        TermContext->DataResult = DataResult;

        TermContext->NumberOfArgs = (AmliTerm->TypesOfArgs ? StrLen(AmliTerm->TypesOfArgs, 0xFFFFFFFF) : 0);

        if (TermContext->NumberOfArgs > 0)
        {
            gdwcODObjs++;

            Size = (TermContext->NumberOfArgs * sizeof(AMLI_OBJECT_DATA));
            TermContext->DataArgs = HeapAlloc(AmliContext->HeapCurrent, 'TADH', Size);

            if (TermContext->DataArgs)
            {
                RtlZeroMemory(TermContext->DataArgs, Size);
            }
            else
            {
                DPRINT1("PushTerm: failed to allocate argument objects\n");
                ASSERT(FALSE);
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
ParseAndGetNameSpaceObject(
    _Out_ PUCHAR* OutOp,
    _In_ PAMLI_NAME_SPACE_OBJECT ScopeObject,
    _Out_ PAMLI_NAME_SPACE_OBJECT* OutNsObject,
    _In_ BOOLEAN AbsentOk)
{
    CHAR Name[256];
    NTSTATUS Status;

    DPRINT("ParseAndGetNameSpaceObject: %X, '%s', %X, %X\n", *OutOp, GetObjectPath(ScopeObject), OutNsObject, AbsentOk);

    giIndent++;

    Status = ParseName(OutOp, Name, 0x100);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("ParseAndGetNameSpaceObject: Status %X\n", Status);
        goto Exit;
    }

    Status = GetNameSpaceObject(Name, ScopeObject, OutNsObject, 0);
    if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DPRINT1("ParseAndGetNameSpaceObject: Status %X\n", Status);
        goto Exit;
    }

    if (AbsentOk)
    {
        Status = STATUS_SUCCESS;
        *OutNsObject = NULL;
    }
    else
    {
        DPRINT1("ParseAndGetNameSpaceObject: object '%s' not found", Name);
    }

Exit:

    giIndent--;

    //DPRINT("ParseAndGetNameSpaceObject: ret Status %X\n", Status);
    return Status;
}

VOID
__cdecl
CopyObjData(
    _In_ PAMLI_OBJECT_DATA DataDest,
    _In_ PAMLI_OBJECT_DATA DataSrc)
{
    DPRINT("CopyObjData: %p, %p\n", DataDest, DataSrc);

    giIndent++;

    ASSERT(DataDest != NULL);
    ASSERT(DataSrc != NULL);

    if (DataDest != DataSrc)
    {
        RtlCopyMemory(DataDest, DataSrc, sizeof(*DataDest));

        if (DataSrc->Flags & 1)
        {
            ASSERT(DataSrc->DataBase != NULL);
            DataSrc->DataBase->RefCount++;
        }
        else if (DataSrc->DataBuff)
        {
            DataSrc->RefCount++;

            DataDest->Flags |= 1;
            DataDest->DataBase = DataSrc;
        }
    }

    giIndent--;
}

NTSTATUS
__cdecl
ReadObject(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_OBJECT_DATA DataObj,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    PAMLI_OBJECT_DATA BaseData;
    PAMLI_AFU_CONTEXT AcfuContext;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("ReadObject: %X, %X, %X\n", AmliContext, DataObj, DataResult);

    giIndent++;

    BaseData = GetBaseData(DataObj);

    if (BaseData->DataType == 5)
    {
        Status = PushFrame(AmliContext, 'UFCA', sizeof(*AcfuContext), AccFieldUnit, (PVOID *)&AcfuContext);
        if (Status == STATUS_SUCCESS)
        {
            AcfuContext->FrameHeader.Flags = 0x10000;
            AcfuContext->DataObj = BaseData;
            AcfuContext->DataResult = DataResult;
        }
    }
    else if (BaseData->DataType == 0xE)
    {
        Status = ReadField(AmliContext, BaseData, BaseData->DataBuff, DataResult);
    }
    else
    {
        ASSERT(DataResult->DataType == 0);//OBJTYPE_UNKNOWN
        CopyObjData(DataResult, BaseData);
    }

    giIndent--;

    //DPRINT("ReadObject: ret Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
ParseNameObj(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    PAMLI_NAME_SPACE_OBJECT AcpiObject = NULL;
    NTSTATUS Status;

    DPRINT("ParseNameObj: %X, %X, %X\n", AmliContext, AmliContext->Op, DataResult);

    giIndent++;

    ASSERT(DataResult != NULL);

    Status = ParseAndGetNameSpaceObject(&AmliContext->Op, AmliContext->Scope, &AcpiObject, FALSE);
    if (Status == STATUS_SUCCESS)
    {
        AcpiObject = GetBaseObject(AcpiObject);

        if (AcpiObject->ObjData.DataType == 8)
        {
            Status = PushCall(AmliContext, AcpiObject, DataResult);
        }
        else
        {
            Status = ReadObject(AmliContext, &AcpiObject->ObjData, DataResult);
        }
    }

    giIndent--;

    DPRINT("ParseNameObj: Status %X\n", Status);
    return Status;
}

NTSTATUS
__cdecl
ParseArgObj(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
ParseLocalObj(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
ParseOpcode(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PUCHAR ScopeEnd,
    _In_ PAMLI_OBJECT_DATA DataResult)
{
    PUCHAR* OpArray;
    PUCHAR Op;
    UCHAR Opcode;
    NTSTATUS Status;
    ULONG Flags2;
    PAMLI_TERM AmliTerm;

    DPRINT("ParseOpcode: %X, %X, %X, %X\n", AmliContext, AmliContext->Op, ScopeEnd, DataResult);

    giIndent++;

    ASSERT(DataResult != NULL);

    OpArray = &AmliContext->Op;

    DPRINT("ParseOpcode: FIXME CheckBP()\n");

    Op = *OpArray;
    Opcode = **OpArray;

    if (Opcode == 0x5B)
    {
        *OpArray = (Op + 1);

        AmliTerm = FindOpcodeTerm(Op[1], ExOpcodeTable);
    }
    else
    {
        AmliTerm = OpcodeTable[Opcode];
    }

    if (AmliTerm)
    {
        Flags2 = AmliTerm->Flags2;

        if (Flags2 & 8)
        {
            Status = ParseIntObj(&AmliContext->Op, DataResult, FALSE);
        }
        else if (Flags2 & 0x10)
        {
            Status = ParseString(&AmliContext->Op, DataResult, FALSE);
        }
        else if (Flags2 & 2)
        {
            Status = ParseArgObj(AmliContext, DataResult);
        }
        else if (Flags2 & 4)
        {
            Status = ParseLocalObj(AmliContext, DataResult);
        }
        else if (Flags2 & 0x20)
        {
            Status = ParseNameObj(AmliContext, DataResult);
        }
        else if (Flags2 & 0x40)
        {
            DPRINT1("ParseOpcode: debug object cannot be evaluated\n");
            ASSERT(FALSE);
            Status = STATUS_ACPI_FATAL;
        }
        else
        {
            ++*OpArray;

            Status = PushTerm(AmliContext, Op, ScopeEnd, AmliTerm, DataResult);
        }
    }
    else
    {
        DPRINT1("ParseOpcode: invalid opcode %X at %X\n", **OpArray, *OpArray);
        ASSERT(FALSE);
        Status = STATUS_ACPI_INVALID_OPCODE;
    }

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
    ULONG Stage;

    if (InStatus && InStatus != 0x8001)
        Stage = 2;
    else
        Stage = AmliScope->FrameHeader.Flags & 0xF;

    DPRINT("ParseScope: %X, %p, %X, %p, %X\n", Stage, AmliContext, AmliContext->Op, AmliScope, InStatus);

    giIndent++;

    ASSERT(AmliScope->FrameHeader.Signature == 'POCS');//SIG_SCOPE

    if (Stage == 0)
    {
        AmliScope->FrameHeader.Flags++;
        Stage++;
    }

    if (Stage == 1)
    {
        while (InStatus != 0x8001)
        {
            while (AmliContext->Op < AmliScope->OpEnd)
            {
                FreeDataBuffs(AmliScope->DataResult, 1);

                InStatus = ParseOpcode(AmliContext, AmliScope->OpEnd, AmliScope->DataResult);
                if (InStatus)
                    break;

                if (AmliScope != AmliContext->LocalHeap.HeapEnd)
                    goto Exit;
            }

            if (InStatus == 0x8001)
            {
                AmliContext->Op = AmliScope->OpEnd;
                AmliScope->OpcodeRet = AmliScope->OpEnd;

                if ((AmliScope->FrameHeader.Flags & 0x20000))
                    InStatus = STATUS_SUCCESS;

                break;
            }

            if (InStatus == 0x8004 || AmliScope != AmliContext->LocalHeap.HeapEnd)
                goto Exit;

            if (InStatus || AmliContext->Op >= AmliScope->OpEnd)
                break;
        }

        AmliScope->FrameHeader.Flags++;
    }
    else if (Stage != 2)
    {
        goto Exit;
    }

    AmliContext->Scope = AmliScope->OldScope;
    AmliContext->Owner = AmliScope->OldOwner;
    AmliContext->HeapCurrent = AmliScope->HeapCurrent;

    if (AmliScope->OpcodeRet)
        AmliContext->Op = AmliScope->OpcodeRet;

    PopFrame(AmliContext);

Exit:

    giIndent--;

    return InStatus;
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

VOID
NTAPI
RestartCtxtPassive(
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
__cdecl
ReleaseASLMutex(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ PAMLI_MUTEX_OBJECT AmliMutex)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
__cdecl
AsyncCallBack(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ NTSTATUS InStatus)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
__cdecl
RunContext(
    _In_ PAMLI_CONTEXT AmliContext)
{
    PAMLI_CONTEXT OldAmliContext;
    PAMLI_RESOURCE AmliResourse;
    PAMLI_FRAME_HEADER Frame;
    PAMLI_LIST Entry;
    PKTHREAD Thread;
    ULONG Flags;
    NTSTATUS Status;

    Thread = gReadyQueue.Thread;
    OldAmliContext = gReadyQueue.CurrentContext;

    DPRINT("RunContext: AmliContext %X\n", AmliContext);

    giIndent++;

    ASSERT(AmliContext->Signature == 'TXTC'); // SIG_CTXT
    ASSERT(AmliContext->Flags & 8); // CTXTF_READY

    gReadyQueue.CurrentContext = AmliContext;
    gReadyQueue.Thread = KeGetCurrentThread();

    while (TRUE)
    {
        Status = STATUS_SUCCESS;

        AmliContext->Flags = ((AmliContext->Flags & ~8) | 0x10);

        ReleaseMutex(&gReadyQueue.Mutex);

        while (!IsStackEmpty(AmliContext))
        {
            Frame = AmliContext->LocalHeap.HeapEnd;
            ASSERT(Frame->ParseFunction != NULL);

            Status = Frame->ParseFunction(AmliContext, Frame, Status);

            if (Status == 0x8004 || Status == 0x8000)
                break;
        }

        AcquireMutex(&gReadyQueue.Mutex);
        Flags = AmliContext->Flags;

        if ((Flags & 0x80) == 0 || Status != 0x8000)
            AmliContext->Flags = (Flags & ~0x10);

        if (!(AmliContext->Flags & 8))
            break;

        ASSERT(Status == 0x8004);//AMLISTA_PENDING
    }

    if (Status == 0x8004)
    {
        AmliContext->Flags |= 0x20;
    }
    else if (Status == 0x8000)
    {
        if (!AmliContext->NestedContext)
            AmliContext->Flags &= ~0x80;

        Status = STATUS_SUCCESS;
    }
    else
    {
        ReleaseMutex(&gReadyQueue.Mutex);

        if (Status == STATUS_SUCCESS)
        {
            if (AmliContext->DataCallBack)
            {
                Status = DupObjData(gpheapGlobal, AmliContext->DataCallBack, &AmliContext->Result);
            }
        }

        if (AmliContext->Flags & 0x20)
        {
            AsyncCallBack(AmliContext, Status);

            if (AmliContext->Flags & 0x100)
                Status = 0x8004;
        }

        while (AmliContext->ResourcesList)
        {
            Entry = AmliContext->ResourcesList;
            AmliResourse = CONTAINING_RECORD(Entry, AMLI_RESOURCE, List);

            ASSERT(AmliResourse->ContextOwner == AmliContext);

            if (AmliResourse->ResType == 1)
            {
                ReleaseASLMutex(AmliContext, AmliResourse->ResObject);
            }
            else
            {
                DPRINT1("RunContext: FIXME\n");
                ASSERT(FALSE);
            }
        }

        FreeContext(AmliContext);
        AcquireMutex(&gReadyQueue.Mutex);
    }

    gReadyQueue.Thread = Thread;
    gReadyQueue.CurrentContext = OldAmliContext;

    if (gReadyQueue.Flags & 4)
    {
        if (!gplistCtxtHead)
        {
            gReadyQueue.Flags = ((gReadyQueue.Flags & ~4) | 8);

            if (gReadyQueue.PauseCallback)
            {
                gReadyQueue.PauseCallback(gReadyQueue.CallbackContext);
            }
        }
    }

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
InsertReadyQueue(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ BOOLEAN IsDelayExecute)
{
    ULONG Flags;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("InsertReadyQueue: %X, %X\n", AmliContext, IsDelayExecute);

    giIndent++;

    Flags = AmliContext->Flags;
    if (Flags & 1)
    {
        AmliContext->Flags = (Flags & ~1);

        if (!KeCancelTimer(&AmliContext->Timer))
            AmliContext->Flags |= 2;
    }

    AmliContext->Flags |= 8;
    Flags = AmliContext->Flags;

    if ((Flags & 2) || ((Flags & 0x10) && (Flags & 0x80) == 0))
    {
        goto Exit;
    }

    if (IsDelayExecute)
    {
        DPRINT("InsertReadyQueue: FIXME AsyncCallBack()\n");
        ASSERT(FALSE);
        goto Exit;
    }

    if ((Flags & 0x80) && gReadyQueue.Thread == KeGetCurrentThread())
    {
        Status = RunContext(AmliContext);
        goto Exit;
    }

    if (!gReadyQueue.Thread && !(gReadyQueue.Flags & 8))
    {
        Status = RunContext(AmliContext);

        if (gReadyQueue.List && !(gReadyQueue.Flags & 2))
        {
            OSQueueWorkItem(&gReadyQueue.WorkItem);
            gReadyQueue.Flags |= 2;
        }

        goto Exit;
    }

    ASSERT(!(AmliContext->Flags & 0x50)); // (CTXTF_IN_READYQ | CTXTF_RUNNING)

    Flags = AmliContext->Flags;

    if (!(Flags & 0x40))
    {
        AmliContext->Flags = (Flags | 0x40);

        ListInsertTail(&AmliContext->QueueList, &gReadyQueue.List);
        AmliContext->QueueLists = &gReadyQueue.List;
    }

    AmliContext->Flags |= 0x20;
    Status = 0x8004;

Exit:

    giIndent--;

    return Status;
}

NTSTATUS
__cdecl
RestartContext(
    _In_ PAMLI_CONTEXT AmliContext,
    _In_ BOOLEAN IsDelayExecute)
{
    PAMLI_RESTART_CONTEXT RestartCtxt;
    NTSTATUS Status;

    DPRINT("RestartContext: %X, %X\n", AmliContext, IsDelayExecute);

    giIndent++;

    ASSERT(!(AmliContext->Flags & 1)); // CTXTF_TIMER_PENDING
    ASSERT((IsDelayExecute == FALSE) || !(AmliContext->Flags & 0x100)); // CTXTF_ASYNC_EVAL

    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        gdwcMemObjs++;

        RestartCtxt = ExAllocatePoolWithTag(NonPagedPool, sizeof(*RestartCtxt), 'TlmA');
        if (RestartCtxt)
        {
            AmliContext->Flags |= 0x20;

            RestartCtxt->AmliContext = AmliContext;

            RestartCtxt->WorkQueueItem.WorkerRoutine = RestartCtxtPassive;
            RestartCtxt->WorkQueueItem.Parameter = RestartCtxt;
            RestartCtxt->WorkQueueItem.List.Flink = NULL;

            OSQueueWorkItem(&RestartCtxt->WorkQueueItem);
            Status = 0x8004;
        }
        else
        {
            DPRINT("RestartContext: failed to allocate restart context item\n");
            ASSERT(FALSE);
            Status = STATUS_ACPI_FATAL;
        }
    }
    else
    {
        AcquireMutex(&gReadyQueue.Mutex);
        Status = InsertReadyQueue(AmliContext, IsDelayExecute);
        ReleaseMutex(&gReadyQueue.Mutex);
    }

    giIndent--;

    return Status;
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
