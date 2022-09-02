
#include "winemm.h"

#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>

WINE_DEFAULT_DEBUG_CHANNEL(winmm);

/* Retrieves device interface path for DRVM_INIT and XXXX_GETNUMDEVS messages */
BOOL
GetDeviceInterfacePath(
    DWORD Index,
    LPWSTR* DevicePath)
{
    SP_DEVICE_INTERFACE_DATA interface_data;
    SP_DEVINFO_DATA device_data;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail_data;
    HDEVINFO dev_info;
    DWORD length;

    const GUID category_guid = {STATIC_KSCATEGORY_AUDIO};

    dev_info = SetupDiGetClassDevsW(&category_guid,
                                    NULL,
                                    NULL,
                                    DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

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

    interface_data.cbSize = sizeof(interface_data);
    if (SetupDiEnumDeviceInterfaces(dev_info,
                                    NULL,
                                    &category_guid,
                                    Index,
                                    &interface_data) )
    {
        ZeroMemory(detail_data, length);

        detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        device_data.cbSize = sizeof(device_data);
        SetupDiGetDeviceInterfaceDetailW(dev_info,
                                         &interface_data,
                                         detail_data,
                                         length,
                                         NULL,
                                         &device_data);

        *DevicePath = HeapAlloc(GetProcessHeap(), 0, MAX_PATH * sizeof(WCHAR));
        if (*DevicePath)
        {
            detail_data->DevicePath[1] = L'\\';
            lstrcpyW(*DevicePath, detail_data->DevicePath);
            ERR("DevicePath %S\n", *DevicePath);
        }
    }

    HeapFree(GetProcessHeap(), 0, detail_data);

    SetupDiDestroyDeviceInfoList(dev_info);
    return TRUE;
}
