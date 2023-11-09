
idf_component_register(
  SRCS "BAS/src/BAS.c" "BAS/src/DiskIo/posix/BaFile.c" "BAS/src/arch/FreeRTOS/ThreadLib.c" "BAS/src/arch/NET/LwIP/SoDisp.c" "BAS/src/dlmalloc.c" "main.c" "NetESP32.c" "CfgESP32.c" "BAS/examples/xedge/src/xedge.c" "BAS/examples/xedge/obj/XedgeZip.c" "console.c" "BaESP32.c" "lua-protobuf/pb.c" "BaCam.c"
  INCLUDE_DIRS "."
  "BAS/inc"
  "BAS/inc/arch/NET/LwIP"
  "BAS/inc/arch/FreeRTOS"
  "BAS/examples/xedge/src"
)

# -fno-inline-small-functions is required. The Xtensa ESP32 compiler
#  generates faulty BAS binary code without this setting.

target_compile_options(${COMPONENT_LIB} PRIVATE -DBA_ESP32 -DBA_FREERTOS -DBA_LWIP -DUSE_DLMALLOC -DBP_IO=0 -DUSE_PROTOBUF -DUSE_REVCON=1 -DB_LITTLE_ENDIAN -DLUA_NUMBER_INTEGER=0 -DUSE_DBGMON=1)

# -fno-inline-small-functions is required. The Xtensa ESP32 compiler
#  generates faulty BAS binary code without this setting.
target_compile_options(${COMPONENT_LIB} PRIVATE  -fno-inline-small-functions -Wno-char-subscripts -Wno-implicit-fallthrough -Wno-attributes)
