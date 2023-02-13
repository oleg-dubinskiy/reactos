//
//    Copyright (C) Microsoft.  All rights reserved.
//
#ifndef _FXFORWARD_HPP_
#define _FXFORWARD_HPP_

typedef struct _FX_DRIVER_GLOBALS *PFX_DRIVER_GLOBALS;

struct FxAutoIrp;
class  FxCallback;
class  FxCallbackLock;
class  FxCallbackMutexLock;
class  FxCallbackSpinLock;
class  FxChildList;
class  FxCmResList;
class  FxCollection;
struct FxCollectionInternal;
class  FxCommonBuffer;
struct FxContextHeader;
class  FxDevice;
class  FxDeviceBase;
struct FxDeviceDescriptionEntry;
class  FxDeviceInterface;
struct FxDeviceText;
struct FxCxDeviceInfo;
class  FxDefaultIrpHandler;
class  FxDisposeList;
class  FxDmaEnabler;
class  FxDmaTransactionBase;
class  FxDmaPacketTransaction;
class  FxDmaScatterGatherTransaction;
class  FxDriver;
class  FxFileObject;
struct FxFileObjectInfo;
struct FxGlobalsStump;
class  FxInterrupt;
struct FxIoQueueNode;
class  FxIoQueue;
class  FxIoResList;
class  FxIoResReqList;
class  FxIoTarget;
class  FxIoTargetSelf;
class  FxIrp;
struct FxIrpPreprocessInfo;
struct FxIrpDynamicDispatchInfo;
class  FxIrpQueue;
class  FxLock;
class  FxLookasideList;
class  FxLookasideListFromPool;
class  FxMemoryBuffer;
class  FxMemoryBufferFromLookaside;
class  FxMemoryBufferFromPool;
class  FxMemoryBufferFromPoolLookaside;
class  FxMemoryBufferPreallocated;
class  FxMemoryObject;
class  FxNonPagedObject;
class  FxNPagedLookasideList;
class  FxNPagedLookasideListFromPool;
class  FxObject;
class  FxPackage;
class  FxPagedLookasideListFromPool;
class  FxPagedObject;
class  FxPkgFdo;
class  FxPkgGeneral;
class  FxPkgIo;
class  FxPkgPdo;
class  FxPkgPnp;
struct FxPnpMachine;
struct FxPnpStateCallback;
struct FxPowerMachine;
struct FxPostProcessInfo;
class  FxPowerIdleMachine;
struct FxPowerPolicyMachine;
struct FxPowerPolicyStateCallback;
struct FxPowerStateCallback;
struct FxQueryInterface;
class  FxRequest;
class  FxRequestBase;
struct FxRequestBuffer;
struct FxRequestContext;
class  FxRequestFromLookaside;
class  FxRequestMemory;
struct FxRequestOutputBuffer;
struct FxRequestSystemBuffer;
class  FxRelatedDevice;
class  FxRelatedDeviceList;
class  FxResourceCm;
class  FxResourceIo;
class  FxSelfManagedIoMachine;
class  FxSpinLock;
class  FxString;
struct FxStump;
class  FxSyncRequest;
class  FxSystemWorkItem;
class  FxSystemThread;
class  FxTagTracker;
class  FxTimer;
struct FxTraceInfo;
class  FxTransactionedList;
struct FxTransactionedEntry;
class  FxUsbDevice;
struct FxUsbIdleInfo;
class  FxUsbInterface;
class  FxUsbPipe;
struct FxUsbPipeContinuousReader;
class  FxVerifierLock;
struct FxWatchdog;
class  FxWaitLock;
class  FxWmiProvider;
class  FxWmiInstance;
class  FxWmiInstanceExternal;
class  FxWmiInstanceInternal;
struct FxWmiInstanceInternalCallbacks;
class  FxWmiIrpHandler;
class  FxWorkItem;

class  IFxHasCallbacks;
class  IFxMemory;

#ifndef __REACTOS__
enum FxObjectType : UINT32;
#else // from fxobject.hpp
//
// type of object being allocated.  An internal object does *NOT*
// 1) have its size rounded up to an alignment value
// 2) extra size and context header appended to the allocation
//
enum FxObjectType { // : UINT32;
    FxObjectTypeInvalid = 0,
    FxObjectTypeInternal,
    FxObjectTypeExternal,
    FxObjectTypeEmbedded
};
#endif

#ifndef __REACTOS__
enum FxWmiInstanceAction : UINT32;
#else // from fxpkgpnp.hpp
enum FxWmiInstanceAction { // : UINT32;
    AddInstance,
    RemoveInstance
};
#endif

#ifndef __REACTOS__
enum FxWakeInterruptEvents : UINT32;
#else // from fxwakeinterruptstatemachine.hpp
enum FxWakeInterruptEvents { // : UINT32
    WakeInterruptEventInvalid                  = 0x00,
    WakeInterruptEventIsr                      = 0x01,
    WakeInterruptEventEnteringD0               = 0x02,
    WakeInterruptEventLeavingD0                = 0x04,
    WakeInterruptEventD0EntryFailed            = 0x08,
    WakeInterruptEventLeavingD0NotArmedForWake = 0x10,
    WakeInterruptEventNull                     = 0xFF,
};
#endif

PVOID
FxObjectHandleAlloc(
    __in        PFX_DRIVER_GLOBALS FxDriverGlobals,
    __in        POOL_TYPE PoolType,
    __in        size_t Size,
    __in        ULONG Tag,
    __in_opt    PWDF_OBJECT_ATTRIBUTES Attributes,
    __in        USHORT ExtraSize,
    __in        FxObjectType ObjectType
    );

#if (FX_CORE_MODE==FX_CORE_USER_MODE)
#include "fxforwardum.hpp"
#endif

#endif //  _FXFORWARD_HPP_
