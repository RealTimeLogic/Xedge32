

idf_component_register(
  SRCS "BAS/src/BAS.c" "BAS/src/DiskIo/posix/BaFile.c" "BAS/src/arch/FreeRTOS/ThreadLib.c" "BAS/src/arch/NET/LwIP/SoDisp.c" "BAS/src/dlmalloc.c" "main.c" "smartconfig.c" "BAS/examples/lspappmgr/src/LspAppMgr.c" "BAS/examples/lspappmgr/obj/LspZip.c" "esp_wrap.c"
  INCLUDE_DIRS "."
  "BAS/inc"
  "BAS/inc/arch/NET/LwIP"
  "BAS/inc/arch/FreeRTOS"
)

# Change -DNO_SMARTCONFIG to -DSMARTCONFIG below if you want to enable this feature.
# See: https://github.com/espressif/esp-idf/tree/master/examples/wifi/smart_config
# and: https://play.google.com/store/apps/details?id=com.khoazero123.iot_esptouch_demo

# -fno-inline-small-functions is required. The Xtensa ESP32 compiler
#  generates faulty BAS binary code without this setting.

target_compile_options(${COMPONENT_LIB} PRIVATE -DNO_SMARTCONFIG -DNDEBUG -DBA_ESP32 -DUSE_DLMALLOC -DBA_FREERTOS -DBA_LWIP -DUSE_REVCON=1 -DB_LITTLE_ENDIAN -DLUA_NUMBER_INTEGER=0  -DUSE_DBGMON=1 -DSHARKSSL_ENABLE_ASN1_KEY_CREATION=1 -DSHARKSSL_ENABLE_RSAKEY_CREATE=1 -DSHARKSSL_ENABLE_ECCKEY_CREATE=1 -DSHARKSSL_ENABLE_CSR_CREATION=1 -DSHARKSSL_ENABLE_CSR_SIGNING=1 -fno-inline-small-functions -Dluaopen_LED=luaopen_esp)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-implicit-fallthrough -Wno-attributes)


 
