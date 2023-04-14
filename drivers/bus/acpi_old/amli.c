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

AMLI_EVHANDLE ghNotify;
AMLI_EVHANDLE ghFatal;
AMLI_EVHANDLE ghValidateTable;
AMLI_EVHANDLE ghGlobalLock;
AMLI_EVHANDLE ghCreate;
AMLI_EVHANDLE ghDestroyObj;

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
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
RegEventHandler(
    _In_ PAMLI_EVHANDLE EventHandler,
    _In_ PVOID Handler,
    _In_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
__cdecl
RegRSAccess(
    _In_ ULONG RegionSpace,
    _In_ PVOID FnHandler,
    _In_ PVOID Param,
    _In_ BOOLEAN IsRaw)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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

NTSTATUS
NTAPI
RegisterOperationRegionHandler(
    _In_ PVOID Param1,
    _In_ ULONG EventType,
    _In_ ULONG EventData,
    _In_ PVOID CallBack,
    _In_ PVOID CallBackContext,
    _In_ PVOID *OutRegionData)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
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
