
remove_definitions(-D_WIN32_WINNT=0x502)
add_definitions(-D_WIN32_WINNT=0x600)

spec2def(mmdevapi.dll mmdevapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    audiovolume.c
    devenum.c
    main.c
    precomp.h
    ${CMAKE_CURRENT_BINARY_DIR}/mmdevapi_stubs.c)

add_library(mmdevapi MODULE
    ${SOURCE}
    guid.c
    mmdevapi.rc
    ${CMAKE_CURRENT_BINARY_DIR}/mmdevapi.def)

set_module_type(mmdevapi win32dll)
target_link_libraries(mmdevapi uuid wine)
add_importlibs(mmdevapi ole32 oleaut32 user32 advapi32 msvcrt kernel32 ntdll)
add_pch(mmdevapi precomp.h SOURCE)
add_dependencies(mmdevapi dxsdk)
add_cd_file(TARGET mmdevapi DESTINATION reactos/system32 FOR all)
set_wine_module(mmdevapi)
