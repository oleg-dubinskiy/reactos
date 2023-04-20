/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI Machine Language Interpreter (AMLI) header
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

/* STRUCTURES ***************************************************************/

typedef struct _AMLI_LIST
{
    struct _AMLI_LIST* Prev;
    struct _AMLI_LIST* Next;
} AMLI_LIST, *PAMLI_LIST;

typedef struct _AMLI_OBJECT_DATA
{
    USHORT Flags;
    USHORT DataType;
    union
    {
        ULONG RefCount;
        struct _AMLI_OBJECT_DATA* DataBase;
    };
    union
    {
        PVOID DataValue;
        struct _AMLI_NAME_SPACE_OBJECT* Alias;
        struct _AMLI_OBJECT_DATA* DataAlias;
        PVOID Owner;
    };
    ULONG DataLen;
    PVOID DataBuff;
} AMLI_OBJECT_DATA, *PAMLI_OBJECT_DATA;

typedef struct _AMLI_NAME_SPACE_OBJECT
{
    AMLI_LIST List;
    struct _AMLI_NAME_SPACE_OBJECT* Parent;
    struct _AMLI_NAME_SPACE_OBJECT* FirstChild;
    ULONG NameSeg;
    PVOID Owner;
    struct _AMLI_NAME_SPACE_OBJECT* OwnedNext;
    AMLI_OBJECT_DATA ObjData;
    PVOID Context;
    ULONG RefCount;
} AMLI_NAME_SPACE_OBJECT, *PAMLI_NAME_SPACE_OBJECT;

typedef struct _AMLI_OBJECT_OWNER
{
    AMLI_LIST List;
    ULONG Signature;
    PAMLI_NAME_SPACE_OBJECT ObjList;
} AMLI_OBJECT_OWNER, *PAMLI_OBJECT_OWNER;

struct _AMLI_CONTEXT;
typedef NTSTATUS (__cdecl* PAMLI_FN_PARSE)(struct _AMLI_CONTEXT* AmliContext, PVOID Context, NTSTATUS InStatus);

typedef struct _AMLI_FRAME_HEADER
{
    ULONG Signature;
    ULONG Length;
    ULONG Flags;
    PAMLI_FN_PARSE ParseFunction;
} AMLI_FRAME_HEADER, *PAMLI_FRAME_HEADER;

typedef struct _AMLI_CALL
{
    AMLI_FRAME_HEADER FrameHdr;
    struct _AMLI_CALL* CallPrev;
    PAMLI_OBJECT_OWNER OwnerPrev;
    PAMLI_NAME_SPACE_OBJECT NsMethod;
    ULONG ArgIndex;
    ULONG NumberOfArgs;
    PAMLI_OBJECT_DATA DataArgs;
    AMLI_OBJECT_DATA Locals[8];
    PAMLI_OBJECT_DATA DataResult;
} AMLI_CALL, *PAMLI_CALL;

typedef struct _AMLI_HEAP_HEADER
{
    ULONG Signature;
    ULONG Length;
    struct _AMLI_HEAP* Heap;
    AMLI_LIST List;
} AMLI_HEAP_HEADER, *PAMLI_HEAP_HEADER;

typedef struct _AMLI_HEAP
{
    ULONG Signature;
    PVOID HeapEnd;
    struct _AMLI_HEAP* HeapHead;
    struct _AMLI_HEAP* HeapNext;
    PVOID HeapTop;
    PAMLI_LIST ListFreeHeap;
    AMLI_HEAP_HEADER Heap;
} AMLI_HEAP, *PAMLI_HEAP;

typedef struct _AMLI_CONTEXT
{
    ULONG Signature;
    PUCHAR End;
    AMLI_LIST List;
    AMLI_LIST QueueList;
    PAMLI_LIST* QueueLists;
    PAMLI_LIST ResourcesList;
    ULONG Flags;
    PAMLI_NAME_SPACE_OBJECT NsObject;
    PAMLI_NAME_SPACE_OBJECT Scope;
    PAMLI_OBJECT_OWNER Owner;
    PAMLI_CALL Call;
    //PAMLI_NESTED_CONTEXT NestedContext;
    ULONG SyncLevel;
    PUCHAR Op;
    AMLI_OBJECT_DATA Result;
    //PAMLI_FN_ASYNC_CALLBACK AsyncCallBack;
    PAMLI_OBJECT_DATA DataCallBack;
    PVOID CallBackContext;
    KTIMER Timer;
    KDPC Dpc;
    PAMLI_HEAP HeapCurrent;
    //AMLI_CONTEXT_DATA ContextData;
    AMLI_HEAP LocalHeap;
} AMLI_CONTEXT, *PAMLI_CONTEXT;

typedef struct _AMLI_TERM_CONTEXT *PAMLI_TERM_CONTEXT;
typedef NTSTATUS (__cdecl* PAMLI_TERM_HANDLER)(PAMLI_CONTEXT AmliContext, PAMLI_TERM_CONTEXT TermCtx);
typedef NTSTATUS (__cdecl* PAMLI_TERM_CALLBACK_1)(ULONG, ULONG, ULONG, PVOID, PVOID);

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

typedef struct _AMLI_MUTEX_OBJECT
{
    ULONG SyncLevel;
    ULONG OwnedCounter;
    PVOID Owner;
    PAMLI_LIST ListWaiters;
} AMLI_MUTEX_OBJECT, *PAMLI_MUTEX_OBJECT;

C_ASSERT(sizeof(AMLI_MUTEX_OBJECT) == 0x10);

typedef struct _AMLI_METHOD_OBJECT
{
    AMLI_MUTEX_OBJECT Mutex;
    UCHAR MethodFlags;
    UCHAR CodeBuff[1];
    UCHAR Pad[2];
} AMLI_METHOD_OBJECT, *PAMLI_METHOD_OBJECT;

typedef struct _AMLI_MUTEX
{
    KSPIN_LOCK SpinLock;
    KIRQL OldIrql;
    UCHAR Pad[3];
} AMLI_MUTEX, *PAMLI_MUTEX;

typedef VOID (__cdecl* PAMLI_FN_PAUSE_CALLBACK)(PVOID Context);

typedef struct _AMLI_CONTEXT_QUEUE
{
    ULONG Flags;
    PKTHREAD Thread;
    PAMLI_CONTEXT CurrentContext;
    PAMLI_LIST List;
    ULONG TimeSliceLength;
    ULONG TimeSliceInterval;
    PAMLI_FN_PAUSE_CALLBACK PauseCallback;
    PVOID CallbackContext;
    AMLI_MUTEX Mutex;
    KTIMER Timer;
    KDPC DpcStartTimeSlice;
    KDPC DpcExpireTimeSlice;
    WORK_QUEUE_ITEM WorkItem;
} AMLI_CONTEXT_QUEUE, *PAMLI_CONTEXT_QUEUE;

C_ASSERT(sizeof(AMLI_CONTEXT_QUEUE) == 0xA0);

typedef struct _AMLI_SCOPE
{
    AMLI_FRAME_HEADER FrameHeader;
    PUCHAR OpEnd;
    PUCHAR OpcodeRet;
    PAMLI_NAME_SPACE_OBJECT OldScope;
    PAMLI_OBJECT_OWNER OldOwner;
    PAMLI_HEAP HeapCurrent;
    PAMLI_OBJECT_DATA DataResult;
} AMLI_SCOPE, *PAMLI_SCOPE;
typedef struct _AMLI_TERM_CONTEXT
{
    AMLI_FRAME_HEADER FrameHeader;

} AMLI_TERM_CONTEXT, *PAMLI_TERM_CONTEXT;

typedef struct _AMLI_FIELD_DESCRIPTOR
{
    ULONG ByteOffset;
    ULONG StartBitPos;
    ULONG NumBits;
    ULONG FieldFlags;
} AMLI_FIELD_DESCRIPTOR, *PAMLI_FIELD_DESCRIPTOR;

typedef struct _AMLI_FIELD_UNIT_OBJECT
{
    AMLI_FIELD_DESCRIPTOR FieldDesc;
    PAMLI_NAME_SPACE_OBJECT NsFieldParent;
} AMLI_FIELD_UNIT_OBJECT, *PAMLI_FIELD_UNIT_OBJECT;

typedef struct _AMLI_RS_ACCESS_HANDLER
{
    struct _AMLI_RS_ACCESS_HANDLER* Next;
    ULONG RegionSpace;
    PVOID CookAccessHandler;
    PVOID CookAccessParam;
    PVOID RawAccessHandler;
    PVOID RawAccessParam;
} AMLI_RS_ACCESS_HANDLER, *PAMLI_RS_ACCESS_HANDLER;

typedef struct _AMLI_REGION_HANDLER
{
    PVOID CallBack;
    PVOID CallBackContext;
    ULONG EventType;
    ULONG EventData;
} AMLI_REGION_HANDLER, *PAMLI_REGION_HANDLER;

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

NTSTATUS
__cdecl 
AMLILoadDDB(
    _In_ PDSDT Dsdt,
    _Out_ HANDLE* OutHandle
);

NTSTATUS
__cdecl
AMLIGetNameSpaceObject(
    _In_ PCHAR ObjPath,
    _In_ PAMLI_NAME_SPACE_OBJECT ScopeObject,
    _Out_ PAMLI_NAME_SPACE_OBJECT* OutNsObject,
    _In_ ULONG Flags
);

PAMLI_NAME_SPACE_OBJECT
NTAPI
ACPIAmliGetNamedChild(
    _In_ PAMLI_NAME_SPACE_OBJECT AcpiObject,
    _In_ ULONG NameSeg
);

NTSTATUS
__cdecl
AMLIAsyncEvalObject(
    _In_ PAMLI_NAME_SPACE_OBJECT AcpiObject,
    _In_ PAMLI_OBJECT_DATA DataResult,
    _In_ ULONG ArgsCount,
    _In_ PAMLI_OBJECT_DATA DataArgs,
    _In_ PVOID CallBack,
    _In_ PVOID CallBackContext
);

/* EOF */
