/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel Streaming
 * FILE:            drivers/ksfilter/ks/misc.c
 * PURPOSE:         KS Allocator functions
 * PROGRAMMER:      Johannes Anderwald
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

#define TAG_KS 'ssKK'

VOID
CompleteRequest(
    PIRP Irp,
    CCHAR PriorityBoost)
{
    DPRINT("Completing IRP %p Status %x\n", Irp, Irp->IoStatus.Status);

    ASSERT(Irp->IoStatus.Status != STATUS_PENDING);

    IoCompleteRequest(Irp, PriorityBoost);
}

PVOID
AllocateItem(
    IN POOL_TYPE PoolType,
    IN SIZE_T NumberOfBytes)
{
    return ExAllocatePoolZero(PoolType, NumberOfBytes, TAG_KS);
}

VOID
FreeItem(
    IN PVOID Item)
{
    ExFreePoolWithTag(Item, TAG_KS);
}

NTSTATUS
KspCopyCreateRequest(
    IN PIRP Irp,
    IN LPWSTR ObjectClass,
    IN OUT PULONG Size,
    OUT PVOID * Result)
{
    PIO_STACK_LOCATION IoStack;
    PKSOBJECT_CREATE_ITEM CreateItem;
    SIZE_T ObjectLength, ParametersLength;
    PWCHAR Buffer;

    /* get current irp stack */
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    /* get create item */
    CreateItem = KSCREATE_ITEM_IRP_STORAGE(Irp);

    /* get object class length */
    ObjectLength = CreateItem->ObjectClass.Length + sizeof(WCHAR);

    /* check for minium length requirement */
    if (ObjectLength + *Size > IoStack->FileObject->FileName.Length)
        return STATUS_UNSUCCESSFUL;

    /* extract parameters length */
    ParametersLength = IoStack->FileObject->FileName.Length - ObjectLength;
    ASSERT(ParametersLength);

    /* get file name buffer */
    Buffer = IoStack->FileObject->FileName.Buffer;

    /* move to the parameter at the end */
    Buffer += ObjectLength;

    /* allocate buffer */
    Irp->AssociatedIrp.SystemBuffer = AllocateItem(NonPagedPool, ParametersLength);
    if (!Irp->AssociatedIrp.SystemBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* copy parameters */
    RtlMoveMemory(Irp->AssociatedIrp.SystemBuffer, Buffer, ParametersLength);

    /* store result */
    *Result = Irp->AssociatedIrp.SystemBuffer;
    *Size = (ULONG)ParametersLength;

    return STATUS_SUCCESS;
}

/*
    @implemented
*/
KSDDKAPI
PVOID
NTAPI
KsGetObjectFromFileObject(
    IN PFILE_OBJECT FileObject)
{
    PKSIOBJECT_HEADER ObjectHeader;

    /* get object header */
    ObjectHeader = (PKSIOBJECT_HEADER)FileObject->FsContext2;

    /* return associated object */
    return ObjectHeader->ObjectType;
}

/*
    @implemented
*/
KSDDKAPI
KSOBJECTTYPE
NTAPI
KsGetObjectTypeFromFileObject(
    IN PFILE_OBJECT FileObject)
{
    PKSIOBJECT_HEADER ObjectHeader;

    /* get object header */
    ObjectHeader = (PKSIOBJECT_HEADER)FileObject->FsContext2;
    /* return type */
    return ObjectHeader->Type;
}

/*
    @implemented
*/
KSOBJECTTYPE
NTAPI
KsGetObjectTypeFromIrp(
    IN PIRP  Irp)
{
    PKSIOBJECT_HEADER ObjectHeader;
    PIO_STACK_LOCATION IoStack;

    /* get current irp stack */
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    /* get object header */
    ObjectHeader = (PKSIOBJECT_HEADER)IoStack->FileObject->FsContext2;
    /* return type */
    return ObjectHeader->Type;
}

/*
    @implemented
*/
PUNKNOWN
NTAPI
KsGetOuterUnknown(
    IN PVOID  Object)
{
    PKSBASIC_HEADER BasicHeader = (PKSBASIC_HEADER)((ULONG_PTR)Object - sizeof(KSBASIC_HEADER));

    /* sanity check */
    ASSERT(BasicHeader->Type == KsObjectTypeDevice || BasicHeader->Type == KsObjectTypeFilterFactory ||
           BasicHeader->Type == KsObjectTypeFilter || BasicHeader->Type == KsObjectTypePin);

    /* return objects outer unknown */
    return BasicHeader->OuterUnknown;
}

/*
    @implemented
*/
KSDDKAPI
PVOID
NTAPI
KsGetParent(
    IN PVOID Object)
{
    PKSBASIC_HEADER BasicHeader = (PKSBASIC_HEADER)((ULONG_PTR)Object - sizeof(KSBASIC_HEADER));
    /* sanity check */
    ASSERT(BasicHeader->Parent.KsDevice != NULL);
    /* return object type */
    return (PVOID)BasicHeader->Parent.KsDevice;
}
