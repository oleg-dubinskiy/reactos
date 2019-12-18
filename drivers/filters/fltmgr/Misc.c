/*
 * PROJECT:         Filesystem Filter Manager
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            drivers/filters/fltmgr/Misc.c
 * PURPOSE:         Uncataloged functions
 * PROGRAMMERS:     Ged Murphy (gedmurphy@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "fltmgr.h"
#include "fltmgrint.h"

#define NDEBUG
#include <debug.h>


/* DATA *********************************************************************/




/* EXPORTED FUNCTIONS ******************************************************/

NTSTATUS
FLTAPI
FltBuildDefaultSecurityDescriptor(
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _In_ ACCESS_MASK DesiredAccess
)
{
    UNREFERENCED_PARAMETER(DesiredAccess);
    *SecurityDescriptor = NULL;
    return 0;
}

VOID
FLTAPI
FltFreeSecurityDescriptor(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor
)
{
    UNREFERENCED_PARAMETER(SecurityDescriptor);
}

NTSTATUS
FLTAPI
FltGetDiskDeviceObject(
    _In_ PFLT_VOLUME Volume,
    _Outptr_ PDEVICE_OBJECT *DiskDeviceObject
)
{
    UNREFERENCED_PARAMETER(Volume);
    UNREFERENCED_PARAMETER(DiskDeviceObject);
    return 0;
}

NTSTATUS
FLTAPI
FltGetFileNameInformationUnsafe(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PFLT_INSTANCE Instance,
    _In_ FLT_FILE_NAME_OPTIONS NameOptions,
    _Outptr_ PFLT_FILE_NAME_INFORMATION *FileNameInformation)
{
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(NameOptions);
    *FileNameInformation = NULL;
    return 0;
}

NTSTATUS
FLTAPI
FltGetFileNameInformation(
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _In_ FLT_FILE_NAME_OPTIONS NameOptions,
    _Outptr_ PFLT_FILE_NAME_INFORMATION *FileNameInformation)
{
    UNREFERENCED_PARAMETER(CallbackData);
    UNREFERENCED_PARAMETER(NameOptions);
    *FileNameInformation = NULL;
    return 0;
}

NTSTATUS
FLTAPI
FltGetDestinationFileNameInformation(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ HANDLE RootDirectory,
    _In_reads_bytes_(FileNameLength) PWSTR FileName,
    _In_ ULONG FileNameLength,
    _In_ FLT_FILE_NAME_OPTIONS NameOptions,
    _Outptr_ PFLT_FILE_NAME_INFORMATION *RetFileNameInformation)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(RootDirectory);
    UNREFERENCED_PARAMETER(FileName);
    UNREFERENCED_PARAMETER(FileNameLength);
    UNREFERENCED_PARAMETER(NameOptions);
    *RetFileNameInformation = NULL;
    return 0;
}

NTSTATUS
FLTAPI
FltParseFileNameInformation(
    _Inout_ PFLT_FILE_NAME_INFORMATION FileNameInformation)
{
    UNREFERENCED_PARAMETER(FileNameInformation);
    return 0;
}

VOID
FLTAPI
FltReferenceFileNameInformation(
    _In_ PFLT_FILE_NAME_INFORMATION FileNameInformation)
{
    UNREFERENCED_PARAMETER(FileNameInformation);
}

VOID
FLTAPI
FltReleaseFileNameInformation(
    _In_ PFLT_FILE_NAME_INFORMATION FileNameInformation)
{
    UNREFERENCED_PARAMETER(FileNameInformation);
}

NTSTATUS
FLTAPI
FltQueryInformationFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_to_(Length,*LengthReturned) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _Out_opt_ PULONG LengthReturned)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(FileInformation);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(FileInformationClass);
    LengthReturned = NULL;
    return 0;
}

NTSTATUS
FLTAPI
FltQueueGenericWorkItem(
    _In_ PFLT_GENERIC_WORKITEM FltWorkItem,
    _In_ PVOID FltObject,
    _In_ PFLT_GENERIC_WORKITEM_ROUTINE WorkerRoutine,
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(FltWorkItem);
    UNREFERENCED_PARAMETER(FltObject);
    UNREFERENCED_PARAMETER(WorkerRoutine);
    UNREFERENCED_PARAMETER(QueueType);
    Context = NULL;
    return 0;
}

NTSTATUS
FLTAPI
FltSetInformationFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(FileInformation);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(FileInformationClass);
    return 0;
}

NTSTATUS
FLTAPI
FltReadFile(
    _In_ PFLT_INSTANCE InitiatingInstance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _Out_writes_bytes_to_(Length,*BytesRead) PVOID Buffer,
    _In_ FLT_IO_OPERATION_FLAGS Flags,
    _Out_opt_ PULONG BytesRead,
    _In_opt_ PFLT_COMPLETED_ASYNC_IO_CALLBACK CallbackRoutine,
    _In_opt_ PVOID CallbackContext)
{
    UNREFERENCED_PARAMETER(InitiatingInstance);
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(ByteOffset);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Flags);
    BytesRead = NULL;
    CallbackRoutine = NULL;
    CallbackContext = NULL;
    return 0;
}

NTSTATUS
FLTAPI
FltWriteFile(
    _In_ PFLT_INSTANCE InitiatingInstance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ FLT_IO_OPERATION_FLAGS Flags,
    _Out_opt_ PULONG BytesWritten,
    _In_opt_ PFLT_COMPLETED_ASYNC_IO_CALLBACK CallbackRoutine,
    _In_opt_ PVOID CallbackContext)
{
    UNREFERENCED_PARAMETER(InitiatingInstance);
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(ByteOffset);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Buffer);
    BytesWritten = NULL;
    CallbackRoutine = NULL;
    CallbackContext = NULL;
    return 0;
}

NTSTATUS
FLTAPI
FltFsControlFile(
    _In_ PFLT_INSTANCE Instance,
    _In_  PFILE_OBJECT FileObject,
    _In_ ULONG FsControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength,*LengthReturned) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_opt_ PULONG LengthReturned)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(FsControlCode);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    InputBuffer = NULL;
    OutputBuffer = NULL;
    LengthReturned = NULL;
    return 0;
}

VOID
FLTAPI
FltCancelFileOpen(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(FileObject);
}

PVOID
FLTAPI
FltGetRoutineAddress(
    _In_ PCSTR FltMgrRoutineName)
{
    UNREFERENCED_PARAMETER(FltMgrRoutineName);
    return NULL;
}

NTSTATUS
FLTAPI
FltGetTopInstance(
    _In_ PFLT_VOLUME Volume,
    _Outptr_ PFLT_INSTANCE *Instance)
{
    UNREFERENCED_PARAMETER(Volume);
    UNREFERENCED_PARAMETER(*Instance);
    return 0;
}

PFLT_GENERIC_WORKITEM
FLTAPI
FltAllocateGenericWorkItem(VOID)
{
    return NULL;
}

NTSTATUS
FLTAPI
FltAllocateCallbackData(
    _In_ PFLT_INSTANCE Instance,
    _In_opt_ PFILE_OBJECT FileObject,
    _Outptr_ PFLT_CALLBACK_DATA *RetNewCallbackData)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(*RetNewCallbackData);
    FileObject = NULL;
    return 0;
}

BOOLEAN
FLTAPI
FltIsCallbackDataDirty(
    _In_ PFLT_CALLBACK_DATA Data)
{
    UNREFERENCED_PARAMETER(Data);
    return TRUE;
}

VOID
FLTAPI
FltClearCallbackDataDirty(
    _Inout_ PFLT_CALLBACK_DATA Data)
{
    UNREFERENCED_PARAMETER(Data);
}

VOID
FLTAPI
FltSetCallbackDataDirty(
    _Inout_ PFLT_CALLBACK_DATA Data)
{
    UNREFERENCED_PARAMETER(Data);
}

VOID
FLTAPI
FltInitializePushLock(
    _Out_ PEX_PUSH_LOCK PushLock)
{
    UNREFERENCED_PARAMETER(PushLock);
}

NTSTATUS
FLTAPI
FltLockUserBuffer(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    UNREFERENCED_PARAMETER(CallbackData);
    return 0;
}

VOID
FLTAPI
FltPerformSynchronousIo(
    _Inout_ PFLT_CALLBACK_DATA CallbackData)
{
    UNREFERENCED_PARAMETER(CallbackData);
}

PEPROCESS
FLTAPI
FltGetRequestorProcess(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    UNREFERENCED_PARAMETER(CallbackData);
    return NULL;
}

ULONG
FLTAPI
FltGetRequestorProcessId(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    UNREFERENCED_PARAMETER(CallbackData);
    return 0;
}

