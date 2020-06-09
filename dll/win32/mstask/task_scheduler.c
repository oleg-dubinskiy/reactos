/*
 * Copyright (C) 2008 Google (Roy Shea)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "corerror.h"
#include "mstask_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mstask);

typedef struct
{
    ITaskScheduler ITaskScheduler_iface;
    LONG ref;
} TaskSchedulerImpl;

typedef struct
{
    IEnumWorkItems IEnumWorkItems_iface;
    LONG ref;
    HANDLE handle;
} EnumWorkItemsImpl;

static inline TaskSchedulerImpl *impl_from_ITaskScheduler(ITaskScheduler *iface)
{
    return CONTAINING_RECORD(iface, TaskSchedulerImpl, ITaskScheduler_iface);
}

static inline EnumWorkItemsImpl *impl_from_IEnumWorkItems(IEnumWorkItems *iface)
{
    return CONTAINING_RECORD(iface, EnumWorkItemsImpl, IEnumWorkItems_iface);
}

static void TaskSchedulerDestructor(TaskSchedulerImpl *This)
{
    TRACE("%p\n", This);
    HeapFree(GetProcessHeap(), 0, This);
    InterlockedDecrement(&dll_ref);
}

static HRESULT WINAPI EnumWorkItems_QueryInterface(IEnumWorkItems *iface, REFIID riid, void **obj)
{
    EnumWorkItemsImpl *This = impl_from_IEnumWorkItems(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualGUID(riid, &IID_IEnumWorkItems) || IsEqualGUID(riid, &IID_IUnknown))
    {
        *obj = &This->IEnumWorkItems_iface;
        IEnumWorkItems_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI EnumWorkItems_AddRef(IEnumWorkItems *iface)
{
    EnumWorkItemsImpl *This = impl_from_IEnumWorkItems(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%u)\n", This, ref);
    return ref;
}

static ULONG WINAPI EnumWorkItems_Release(IEnumWorkItems *iface)
{
    EnumWorkItemsImpl *This = impl_from_IEnumWorkItems(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%u)\n", This, ref);

    if (ref == 0)
    {
        if (This->handle != INVALID_HANDLE_VALUE)
            FindClose(This->handle);
        HeapFree(GetProcessHeap(), 0, This);
        InterlockedDecrement(&dll_ref);
    }

    return ref;
}

static void free_list(LPWSTR *list, LONG count)
{
    LONG i;

    for (i = 0; i < count; i++)
        CoTaskMemFree(list[i]);

    CoTaskMemFree(list);
}

static inline BOOL is_file(const WIN32_FIND_DATAW *data)
{
    return !(data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

static HRESULT WINAPI EnumWorkItems_Next(IEnumWorkItems *iface, ULONG count, LPWSTR **names, ULONG *fetched)
{
    static const WCHAR tasksW[] = { '\\','T','a','s','k','s','\\','*',0 };
    EnumWorkItemsImpl *This = impl_from_IEnumWorkItems(iface);
    WCHAR path[MAX_PATH];
    WIN32_FIND_DATAW data;
    ULONG enumerated, allocated, dummy;
    LPWSTR *list;
    HRESULT hr = S_FALSE;

    TRACE("(%p)->(%u %p %p)\n", This, count, names, fetched);

    if (!count || !names || (!fetched && count > 1)) return E_INVALIDARG;

    if (!fetched) fetched = &dummy;

    *names = NULL;
    *fetched = 0;
    enumerated = 0;
    list = NULL;

    if (This->handle == INVALID_HANDLE_VALUE)
    {
        GetWindowsDirectoryW(path, MAX_PATH);
        lstrcatW(path, tasksW);
        This->handle = FindFirstFileW(path, &data);
        if (This->handle == INVALID_HANDLE_VALUE)
            return S_FALSE;
    }
    else
    {
        if (!FindNextFileW(This->handle, &data))
            return S_FALSE;
    }

    allocated = 64;
    list = CoTaskMemAlloc(allocated * sizeof(list[0]));
    if (!list) return E_OUTOFMEMORY;

    do
    {
        if (is_file(&data))
        {
            if (enumerated >= allocated)
            {
                LPWSTR *new_list;
                allocated *= 2;
                new_list = CoTaskMemRealloc(list, allocated * sizeof(list[0]));
                if (!new_list)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                list = new_list;
            }

            list[enumerated] = CoTaskMemAlloc((lstrlenW(data.cFileName) + 1) * sizeof(WCHAR));
            if (!list[enumerated])
            {
                hr = E_OUTOFMEMORY;
                break;
            }

            lstrcpyW(list[enumerated], data.cFileName);
            enumerated++;

            if (enumerated >= count)
            {
                hr = S_OK;
                break;
            }
        }
    } while (FindNextFileW(This->handle, &data));

    if (FAILED(hr))
        free_list(list, enumerated);
    else
    {
        *fetched = enumerated;
        *names = list;
    }

    return hr;
}

static HRESULT WINAPI EnumWorkItems_Skip(IEnumWorkItems *iface, ULONG count)
{
    EnumWorkItemsImpl *This = impl_from_IEnumWorkItems(iface);
    FIXME("(%p)->(%u): stub\n", This, count);
    return E_NOTIMPL;
}

static HRESULT WINAPI EnumWorkItems_Reset(IEnumWorkItems *iface)
{
    EnumWorkItemsImpl *This = impl_from_IEnumWorkItems(iface);
    FIXME("(%p): stub\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI EnumWorkItems_Clone(IEnumWorkItems *iface, IEnumWorkItems **cloned)
{
    EnumWorkItemsImpl *This = impl_from_IEnumWorkItems(iface);
    FIXME("(%p)->(%p): stub\n", This, cloned);
    return E_NOTIMPL;
}

static const IEnumWorkItemsVtbl EnumWorkItemsVtbl = {
    EnumWorkItems_QueryInterface,
    EnumWorkItems_AddRef,
    EnumWorkItems_Release,
    EnumWorkItems_Next,
    EnumWorkItems_Skip,
    EnumWorkItems_Reset,
    EnumWorkItems_Clone
};

static HRESULT create_task_enum(IEnumWorkItems **ret)
{
    EnumWorkItemsImpl *tasks;

    *ret = NULL;

    tasks = HeapAlloc(GetProcessHeap(), 0, sizeof(*tasks));
    if (!tasks)
        return E_OUTOFMEMORY;

    tasks->IEnumWorkItems_iface.lpVtbl = &EnumWorkItemsVtbl;
    tasks->ref = 1;
    tasks->handle = INVALID_HANDLE_VALUE;

    *ret = &tasks->IEnumWorkItems_iface;
    InterlockedIncrement(&dll_ref);
    return S_OK;
}

static HRESULT WINAPI MSTASK_ITaskScheduler_QueryInterface(
        ITaskScheduler* iface,
        REFIID riid,
        void **ppvObject)
{
    TaskSchedulerImpl * This = impl_from_ITaskScheduler(iface);

    TRACE("IID: %s\n", debugstr_guid(riid));

    if (IsEqualGUID(riid, &IID_IUnknown) ||
            IsEqualGUID(riid, &IID_ITaskScheduler))
    {
        *ppvObject = &This->ITaskScheduler_iface;
        ITaskScheduler_AddRef(iface);
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI MSTASK_ITaskScheduler_AddRef(
        ITaskScheduler* iface)
{
    TaskSchedulerImpl *This = impl_from_ITaskScheduler(iface);
    TRACE("\n");
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI MSTASK_ITaskScheduler_Release(
        ITaskScheduler* iface)
{
    TaskSchedulerImpl * This = impl_from_ITaskScheduler(iface);
    ULONG ref;
    TRACE("\n");
    ref = InterlockedDecrement(&This->ref);
    if (ref == 0)
        TaskSchedulerDestructor(This);
    return ref;
}

static HRESULT WINAPI MSTASK_ITaskScheduler_SetTargetComputer(
        ITaskScheduler* iface,
        LPCWSTR pwszComputer)
{
    TaskSchedulerImpl *This = impl_from_ITaskScheduler(iface);
    WCHAR buffer[MAX_COMPUTERNAME_LENGTH + 3];  /* extra space for two '\' and a zero */
    DWORD len = MAX_COMPUTERNAME_LENGTH + 1;    /* extra space for a zero */

    TRACE("(%p)->(%s)\n", This, debugstr_w(pwszComputer));

    /* NULL is an alias for the local computer */
    if (!pwszComputer)
        return S_OK;

    buffer[0] = '\\';
    buffer[1] = '\\';
    if (GetComputerNameW(buffer + 2, &len))
    {
        if (!lstrcmpiW(buffer, pwszComputer) ||    /* full unc name */
            !lstrcmpiW(buffer + 2, pwszComputer))  /* name without backslash */
            return S_OK;
    }

    FIXME("remote computer %s not supported\n", debugstr_w(pwszComputer));
    return HRESULT_FROM_WIN32(ERROR_BAD_NETPATH);
}

static HRESULT WINAPI MSTASK_ITaskScheduler_GetTargetComputer(
        ITaskScheduler* iface,
        LPWSTR *ppwszComputer)
{
    TaskSchedulerImpl *This = impl_from_ITaskScheduler(iface);
    LPWSTR buffer;
    DWORD len = MAX_COMPUTERNAME_LENGTH + 1; /* extra space for the zero */

    TRACE("(%p)->(%p)\n", This, ppwszComputer);

    if (!ppwszComputer)
        return E_INVALIDARG;

    /* extra space for two '\' and a zero */
    buffer = CoTaskMemAlloc((MAX_COMPUTERNAME_LENGTH + 3) * sizeof(WCHAR));
    if (buffer)
    {
        buffer[0] = '\\';
        buffer[1] = '\\';
        if (GetComputerNameW(buffer + 2, &len))
        {
            *ppwszComputer = buffer;
            return S_OK;
        }
        CoTaskMemFree(buffer);
    }
    *ppwszComputer = NULL;
    return HRESULT_FROM_WIN32(GetLastError());
}

static HRESULT WINAPI MSTASK_ITaskScheduler_Enum(
        ITaskScheduler* iface,
        IEnumWorkItems **tasks)
{
    TaskSchedulerImpl *This = impl_from_ITaskScheduler(iface);

    TRACE("(%p)->(%p)\n", This, tasks);

    if (!tasks)
        return E_INVALIDARG;

    return create_task_enum(tasks);
}

static HRESULT WINAPI MSTASK_ITaskScheduler_Activate(ITaskScheduler *iface,
        LPCWSTR task_name, REFIID riid, IUnknown **unknown)
{
    ITask *task;
    IPersistFile *pfile;
    HRESULT hr;

    TRACE("%p, %s, %s, %p\n", iface, debugstr_w(task_name), debugstr_guid(riid), unknown);

    hr = ITaskScheduler_NewWorkItem(iface, task_name, &CLSID_CTask, riid, (IUnknown **)&task);
    if (hr != S_OK) return hr;

    hr = ITask_QueryInterface(task, &IID_IPersistFile, (void **)&pfile);
    if (hr == S_OK)
    {
        WCHAR *curfile;

        hr = IPersistFile_GetCurFile(pfile, &curfile);
        if (hr == S_OK)
        {
            hr = IPersistFile_Load(pfile, curfile, STGM_READ | STGM_SHARE_DENY_WRITE);
            CoTaskMemFree(curfile);
        }

        IPersistFile_Release(pfile);
    }

    if (hr == S_OK)
        *unknown = (IUnknown *)task;
    else
        ITask_Release(task);
    return hr;
}

static HRESULT WINAPI MSTASK_ITaskScheduler_Delete(
        ITaskScheduler* iface,
        LPCWSTR pwszName)
{
    FIXME("%p, %s: stub\n", iface, debugstr_w(pwszName));
    return E_NOTIMPL;
}

static HRESULT WINAPI MSTASK_ITaskScheduler_NewWorkItem(
        ITaskScheduler* iface,
        LPCWSTR pwszTaskName,
        REFCLSID rclsid,
        REFIID riid,
        IUnknown **ppunk)
{
    HRESULT hr;
    TRACE("(%p, %s, %s, %s, %p)\n", iface, debugstr_w(pwszTaskName),
            debugstr_guid(rclsid) ,debugstr_guid(riid),  ppunk);

    if (!IsEqualGUID(rclsid, &CLSID_CTask))
        return CLASS_E_CLASSNOTAVAILABLE;

    if (!IsEqualGUID(riid, &IID_ITask))
        return E_NOINTERFACE;

    hr = TaskConstructor(pwszTaskName, (LPVOID *)ppunk);
    return hr;
}

static HRESULT WINAPI MSTASK_ITaskScheduler_AddWorkItem(
        ITaskScheduler* iface,
        LPCWSTR pwszTaskName,
        IScheduledWorkItem *pWorkItem)
{
    FIXME("%p, %s, %p: stub\n", iface, debugstr_w(pwszTaskName), pWorkItem);
    return E_NOTIMPL;
}

static HRESULT WINAPI MSTASK_ITaskScheduler_IsOfType(
        ITaskScheduler* iface,
        LPCWSTR pwszName,
        REFIID riid)
{
    FIXME("%p, %s, %s: stub\n", iface, debugstr_w(pwszName),
            debugstr_guid(riid));
    return E_NOTIMPL;
}

static const ITaskSchedulerVtbl MSTASK_ITaskSchedulerVtbl =
{
    MSTASK_ITaskScheduler_QueryInterface,
    MSTASK_ITaskScheduler_AddRef,
    MSTASK_ITaskScheduler_Release,
    MSTASK_ITaskScheduler_SetTargetComputer,
    MSTASK_ITaskScheduler_GetTargetComputer,
    MSTASK_ITaskScheduler_Enum,
    MSTASK_ITaskScheduler_Activate,
    MSTASK_ITaskScheduler_Delete,
    MSTASK_ITaskScheduler_NewWorkItem,
    MSTASK_ITaskScheduler_AddWorkItem,
    MSTASK_ITaskScheduler_IsOfType
};

HRESULT TaskSchedulerConstructor(LPVOID *ppObj)
{
    TaskSchedulerImpl *This;
    TRACE("(%p)\n", ppObj);

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;

    This->ITaskScheduler_iface.lpVtbl = &MSTASK_ITaskSchedulerVtbl;
    This->ref = 1;

    *ppObj = &This->ITaskScheduler_iface;
    InterlockedIncrement(&dll_ref);
    return S_OK;
}
