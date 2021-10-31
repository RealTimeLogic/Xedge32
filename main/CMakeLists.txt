
idf_component_register(SRCS "main.c" "smartconfig.c" "LspAppMgr.c" "LspZip.c" "esp_wrap.c"
                    INCLUDE_DIRS ".")

# Add -DSMARTCONFIG below if you want to enable this feature.
# See: https://github.com/espressif/esp-idf/tree/master/examples/wifi/smart_config
# and: https://play.google.com/store/apps/details?id=com.khoazero123.iot_esptouch_demo

target_compile_options(${COMPONENT_LIB} PRIVATE -DUSE_REVCON -DUSE_DLMALLOC -DBA_FREERTOS -DBA_LWIP -DB_LITTLE_ENDIAN -DLUA_NUMBER_INTEGER=0 -DBA_AMALGAMATED -DUSE_DBGMON=1)

add_prebuilt_library(prebuilt "libbas.a" PRIV_REQUIRES xtensa)

target_link_libraries(${COMPONENT_LIB} PRIVATE prebuilt)

 
