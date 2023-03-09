
idf_component_register(
  SRCS "BAS/src/BAS.c" "BAS/src/DiskIo/posix/BaFile.c" "BAS/src/arch/FreeRTOS/ThreadLib.c" "BAS/src/arch/NET/LwIP/SoDisp.c" "BAS/src/dlmalloc.c" "main.c" "BAS/examples/xedge/src/xedge.c" "BAS/examples/xedge/obj/LspZip.c" "console.c" "BaESP32.c" "protobuf/pb.c"
  INCLUDE_DIRS "."
  "BAS/inc"
  "BAS/inc/arch/NET/LwIP"
  "BAS/inc/arch/FreeRTOS"
)

# -fno-inline-small-functions is required. The Xtensa ESP32 compiler
#  generates faulty BAS binary code without this setting.

target_compile_options(${COMPONENT_LIB} PRIVATE -DBA_ESP32 -DBA_FREERTOS -DBA_LWIP -DUSE_DLMALLOC -DBP_IO=0 -DUSE_PROTOBUF -DUSE_REVCON=1 -DB_LITTLE_ENDIAN -DLUA_NUMBER_INTEGER=0 -DUSE_DBGMON=1 -DSHARKSSL_ENABLE_ASN1_KEY_CREATION=1 -DSHARKSSL_ENABLE_RSAKEY_CREATE=1 -DSHARKSSL_ENABLE_ECCKEY_CREATE=1 -DSHARKSSL_ENABLE_CSR_CREATION=1 -DSHARKSSL_ENABLE_CSR_SIGNING=1 -fno-inline-small-functions -Wno-char-subscripts)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-implicit-fallthrough -Wno-attributes)


 
