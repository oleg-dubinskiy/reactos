/*++

Copyright (c) Microsoft Corporation

ModuleName:

    MxDrierObjet.h

Abstract:

    Mode agnostic definition of Driver Object

    See MxDriverObjectKm.h and MxDriverObjectUm.h/cpp for mode
    specific implementations

--*/

#pragma once

//
// Forward declare enum
//
#ifndef __REACTOS__
enum FxDriverObjectUmFlags : USHORT;
#else // from fxldrum.h
//
// Valid flags for use in the DRIVER_OBJECT_UM::Flags field.
//
enum FxDriverObjectUmFlags { // : USHORT
    DriverObjectUmFlagsLoggingEnabled = 0x1
};
#endif

class MxDriverObject
{
private:
    //
    // MdDeviceObject is typedef'ed to appropriate type for the mode
    // in the mode specific file
    //
    MdDriverObject m_DriverObject;

public:
    __inline
    MxDriverObject(
        __in MdDriverObject DriverObject
        ) :
        m_DriverObject(DriverObject)
    {
    }

    __inline
    MxDriverObject(
        VOID
        ) :
        m_DriverObject(NULL)
    {
    }

    __inline
    MdDriverObject
    GetObject(
        VOID
        )
    {
        return m_DriverObject;
    }

    __inline
    VOID
    SetObject(
        __in_opt MdDriverObject DriverObject
        )
    {
        m_DriverObject = DriverObject;
    }

    PDRIVER_ADD_DEVICE
    GetDriverExtensionAddDevice(
        VOID
        );

    VOID
    SetDriverExtensionAddDevice(
        _In_ MdDriverAddDevice Value
        );

    MdDriverUnload
    GetDriverUnload(
        VOID
        );

    VOID
    SetDriverUnload(
        _In_ MdDriverUnload Value
        );

    VOID
    SetMajorFunction(
        _In_ UCHAR i,
        _In_ MdDriverDispatch Value
        );

    VOID
    SetDriverObjectFlag(
        _In_ FxDriverObjectUmFlags Flag
        );

    BOOLEAN
    IsDriverObjectFlagSet(
        _In_ FxDriverObjectUmFlags Flag
        );


};

