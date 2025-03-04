
idf_component_register(
  SRCS "BAS/src/BAS.c" "BAS/src/DiskIo/posix/BaFile.c" "BAS/src/arch/FreeRTOS/ThreadLib.c" "BAS/src/arch/NET/LwIP/SoDisp.c" "BAS/src/dlmalloc.c" "main.c" "NetESP32.c" "CfgESP32.c" "BAS/examples/xedge/src/xedge.c" "BAS/examples/xedge/obj/XedgeZip.c" "console.c" "BaESP32.c" "LPeg/lpcset.c" "LPeg/lpcap.c" "LPeg/lpcode.c" "LPeg/lpprint.c" "LPeg/lptree.c" "LPeg/lpvm.c" "CBOR/cbor_c.c" "CBOR/dnf.c" "lua-protobuf/pb.c" "BaCam.c"
  INCLUDE_DIRS "."
  "BAS/inc"
  "BAS/inc/arch/NET/LwIP"
  "BAS/inc/arch/FreeRTOS"
  "BAS/examples/xedge/src"
)

# -fno-inline-small-functions is required. The Xtensa ESP32 compiler
#  generates faulty BAS binary code without this setting.

target_compile_options(${COMPONENT_LIB} PRIVATE -DBA_ESP32 -DBA_FREERTOS -DBA_LWIP -DUSE_DLMALLOC -DBP_IO=0 -DUSE_LPEG=1 -DUSE_CBOR=1 -DUSE_PROTOBUF=1 -DUSE_REVCON=1 -DB_LITTLE_ENDIAN -DLUA_NUMBER_INTEGER=0 -DUSE_DBGMON=1)


# -fno-inline-small-functions is required. The Xtensa ESP32 compiler
#  generates faulty BAS binary code without this setting.
target_compile_options(${COMPONENT_LIB} PRIVATE  -fno-inline-small-functions -Wno-char-subscripts -Wno-implicit-fallthrough -Wno-attributes)

# Create a FATFS image from the contents of the 'partitions/storage' directory
# that fits the partition named 'storage'. FLASH_IN_PROJECT indicates that
# the generated image should be flashed when the entire project is flashed to
# the target with 'idf.py -p PORT flash'.
fatfs_create_spiflash_image(storage ../partitions/storage FLASH_IN_PROJECT PRESERVE_TIME)

# Generate NVS partition binary from 'nvs_xedge_values.cvs' file that contains 
# Key-value pairs. FLASH_IN_PROJECT indicates that the generated image should be 
# flashed when the entire project is flashed to the target with 'idf.py -p PORT flash'.
nvs_create_partition_image(nvs ../partitions/nvs_xedge_values.csv FLASH_IN_PROJECT)
