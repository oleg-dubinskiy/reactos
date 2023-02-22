
list(APPEND SOURCE
    partmgr.c
    partmgr.h)

add_library(partmgr MODULE ${SOURCE} partmgr.rc)
target_link_libraries(partmgr wdmguid memcmp)
set_module_type(partmgr kernelmodedriver)
add_importlibs(partmgr ntoskrnl hal)
#add_pch(partmgr partmgr.h SOURCE)
add_cd_file(TARGET partmgr DESTINATION reactos/system32/drivers NO_CAB FOR all)
