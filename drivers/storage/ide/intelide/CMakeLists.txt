
add_library(intelide MODULE intelide.c intelide.rc)
set_module_type(intelide kernelmodedriver)
add_importlibs(intelide pciidex ntoskrnl)
add_cd_file(TARGET intelide DESTINATION reactos/system32/drivers NO_CAB FOR all)
