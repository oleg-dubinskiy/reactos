
/* INCLUDES ******************************************************************/

/* This file is compiled twice. Once for UP and once for MP */

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#include <internal/spinlock.h>

#undef KeAcquireSpinLock
#undef KeReleaseSpinLock

/* GLOBALS *******************************************************************/

ULONG_PTR HalpSystemHardwareFlags;
KSPIN_LOCK HalpSystemHardwareLock;

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
HalpAcquireCmosSpinLock(VOID)
{
    ULONG_PTR Flags;

    /* Get flags and disable interrupts */
    Flags = __readeflags();
    _disable();

    /* Acquire the lock */
    KxAcquireSpinLock(&HalpSystemHardwareLock);

    /* We have the lock, save the flags now */
    HalpSystemHardwareFlags = Flags;
}

VOID
NTAPI
HalpReleaseCmosSpinLock(VOID)
{
    ULONG_PTR Flags;

    /* Get the flags */
    Flags = HalpSystemHardwareFlags;

    /* Release the lock */
    KxReleaseSpinLock(&HalpSystemHardwareLock);

    /* Restore the flags */
    __writeeflags(Flags);
}

KIRQL
FASTCALL
KfAcquireSpinLock(
    _In_ PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    /* Raise to dispatch and acquire the lock */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

/* PUBLIC FUNCTIONS **********************************************************/

VOID
FASTCALL
KeAcquireInStackQueuedSpinLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    /* Set up the lock */
    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;

    /* Raise to dispatch */
    KeRaiseIrql(DISPATCH_LEVEL, &LockHandle->OldIrql);

    /* Acquire the lock */
    KxAcquireSpinLock(LockHandle->LockQueue.Lock); // HACK
}

VOID
FASTCALL
KeAcquireInStackQueuedSpinLockRaiseToSynch(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    /* Set up the lock */
    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;

    /* Raise to synch */
    KeRaiseIrql(SYNCH_LEVEL, &LockHandle->OldIrql);

    /* Acquire the lock */
    KxAcquireSpinLock(LockHandle->LockQueue.Lock); // HACK
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    KIRQL OldIrql;

    /* Raise to dispatch */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    /* Acquire the lock */
    KxAcquireSpinLock(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock); // HACK
    return OldIrql;
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLockRaiseToSynch(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    KIRQL OldIrql;

    /* Raise to synch */
    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);

    /* Acquire the lock */
    KxAcquireSpinLock(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock); // HACK
    return OldIrql;
}

VOID
NTAPI
KeAcquireSpinLock(
    _In_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
    /* Call the fastcall function */
    *OldIrql = KfAcquireSpinLock(SpinLock);
}

KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch(
    _In_ PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    /* Raise to sync */
    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);

    /* Acquire the lock and return */
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

/* EOF */
