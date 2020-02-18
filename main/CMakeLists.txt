
idf_component_register(SRCS "main.c" "LspAppMgr.c" "LspZip.c" "esp_wrap.c"
                    INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PRIVATE -DUSE_DLMALLOC -DBA_FREERTOS -DBA_LWIP -DB_LITTLE_ENDIAN -DLUA_NUMBER_INTEGER=0 -DBA_AMALGAMATED)
add_prebuilt_library(prebuilt  "libbas.a")

target_link_libraries(${COMPONENT_LIB} PRIVATE prebuilt)

 
