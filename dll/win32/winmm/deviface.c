
#include "winemm.h"

#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>

WINE_DEFAULT_DEBUG_CHANNEL(winmm);

/* Retrieves device interface path for DRVM_INIT and XXXX_GETNUMDEVS messages */
BOOL GetDeviceInterfacePath(LPWSTR* DevicePath)
{
    SP_DEVICE_INTERFACE_DATA interface_data;
    SP_DEVINFO_DATA device_data;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail_data;
    WCHAR device_path[MAX_PATH];
    HDEVINFO dev_info;
    DWORD length;
    int index = 0;

    const GUID category_guid = {STATIC_KSCATEGORY_AUDIO};

    dev_info = SetupDiGetClassDevsW(&category_guid,
                                    NULL,
                                    NULL,
                                    DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

    interface_data.cbSize = sizeof(interface_data);
    interface_data.Reserved = 0;

    length = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA)
                + (MAX_PATH * sizeof(WCHAR));

    detail_data =
        (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(GetProcessHeap(),
                                                      0,
                                                      length);

    if (!detail_data)
    {
        ERR("Failed to allocate detail_data\n");
        return FALSE;
    }

    while (
    SetupDiEnumDeviceInterfaces(dev_info,
                                NULL,
                                &category_guid,
                                index,
                                &interface_data) )
    {
        ZeroMemory(detail_data, length);

        detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        device_data.cbSize = sizeof(device_data);
        device_data.Reserved = 0;
        SetupDiGetDeviceInterfaceDetailW(dev_info,
                                         &interface_data,
                                         detail_data,
                                         length,
                                         NULL,
                                         &device_data);

        wcscpy(device_path, detail_data->DevicePath);
        device_path[0] = L'\\';
        device_path[1] = L'?';
        *DevicePath = device_path;
        ERR("device_path %S\n", device_path);
        index++;
    }

    HeapFree(GetProcessHeap(), 0, detail_data);

    SetupDiDestroyDeviceInfoList(dev_info);
    return TRUE;
}
