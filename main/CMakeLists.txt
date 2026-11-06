include("${CMAKE_CURRENT_LIST_DIR}/Resources.cmake")

# 1. Define base private requirements for all targets
set(main_priv_reqs
    esp_driver_i2c
    esp_driver_uart
    esp_driver_pcnt
    esp_driver_rmt
    esp_driver_usb_serial_jtag
    esp_driver_ledc    
    esp_wifi
    esp_event
    esp_netif
    nvs_flash
    fatfs           # <-- for esp_vfs_fat.h
    esp_eth         # <-- for esp_eth_mac.h / esp_eth_phy.h
    efuse           # for esp_efuse.h
    esp_adc         # for esp_adc/adc_oneshot.h (IDF v5+)
    app_update      # <-- for esp_ota_ops.h
    esp_timer
    bootloader_support # Early native RNG entropy, before peripheral startup.
)

# Use ESP-IDF's provider, never the bundled Windows PSA provider.
list(APPEND main_priv_reqs mbedtls)

# 1.5 Define base source files for all targets
set(main_srcs
    "BAS/src/BAS.c" "BAS/src/DiskIo/posix/BaFile.c" "BAS/src/arch/FreeRTOS/ThreadLib.c" 
    "BAS/src/arch/NET/LwIP/SoDisp.c" "BAS/src/dlmalloc.c" "main.c" "NetESP32.c" "CfgESP32.c" 
    "BAS/examples/xedge/src/xedge.c" ${ZIP_FILE_NAME} "console.c"
    "BaESP32.c" "LPeg/lpcset.c" "LPeg/lpcap.c" "LPeg/lpcode.c" "LPeg/lpprint.c" "LPeg/lptree.c" 
    "LPeg/lpvm.c" "CBOR/cbor_c.c" "CBOR/dnf.c" "lua-protobuf/pb.c"
)

# 2. Conditionally append the camera driver AND the correct C file
if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32s3")
    list(APPEND main_priv_reqs "esp32-camera")
    list(APPEND main_srcs "BaCam.c")
elseif(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND main_priv_reqs "esp_video" "esp_cam_sensor" "esp_driver_jpeg" "esp_driver_ppa")
    list(APPEND main_srcs "BaCamV4L2.c")
endif()

# 3. Register the component using the dynamic lists
idf_component_register(
    SRCS ${main_srcs}
    INCLUDE_DIRS "."
        "BAS/inc"
        "BAS/inc/arch/NET/LwIP"
        "BAS/inc/arch/FreeRTOS"
        "BAS/examples/xedge/src"
    PRIV_REQUIRES "${main_priv_reqs}"
)

# -fno-inline-small-functions is required. The Xtensa ESP32 compiler
#  generates faulty BAS binary code without this setting.

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BA_ESP32 BA_FREERTOS BA_LWIP BA_ENABLE_TPM=1 BA_ENABLE_EMBEDDED_CA=1
    BA_ENABLE_IPV6=$<BOOL:${CONFIG_LWIP_IPV6}>
    XEDGE_THREAD_POOL_MODE=XEDGE_THREAD_POOL_SHARED_FIXED XEDGE_THREAD_COUNT=2
    BA_ALLOCATOR=BA_ALLOCATOR_DLMALLOC BA_ENABLE_LUA_STDIO=0
    BA_ENABLE_LUA_LPEG=1 BA_ENABLE_LUA_CBOR=1 BA_ENABLE_LUA_PROTOBUF=1
    BA_ENABLE_REVERSE_CONNECTION=1 BA_BYTE_ORDER=BA_BYTE_ORDER_LITTLE_ENDIAN
    BA_ENABLE_LUA_DEBUGGER=1 BA_ENABLE_LUA_OPCUA=$<BOOL:${CONFIG_OPCUA_ENABLED}>
    SHARKSSL_PSA_CONSUMER=$<BOOL:${CONFIG_XEDGE_PSA}>)
if(CONFIG_XEDGE_PSA)
    # Provider headers must precede BAS/inc/psa. Roots are device-specific.
    target_include_directories(${COMPONENT_LIB} BEFORE PRIVATE
        "$ENV{IDF_PATH}/components/mbedtls/mbedtls/tf-psa-crypto/include")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        BA_PSA_GLOBALKEY_USE_DEVICE_ROOT=1 SHARKSSL_USE_CHACHA20=0 SHARKSSL_USE_POLY1305=0
        SHARKSSL_AES_SOFTWARE_CT=0 SHARKSSL_AES_GCM_SOFTWARE_CT=0)
endif()


# -fno-inline-small-functions is required. The Xtensa ESP32 compiler
#  generates faulty BAS binary code without this setting.
target_compile_options(${COMPONENT_LIB} PRIVATE  -fno-inline-small-functions -Wno-char-subscripts -Wno-implicit-fallthrough -Wno-attributes)

# Factory images are generated, but ordinary flash must preserve keys/files.
# Factory provisioning is explicit in the release packager or *-flash targets.
fatfs_create_spiflash_image(storage ../partitions/storage PRESERVE_TIME)
nvs_create_partition_image(nvs ../partitions/nvs_xedge_values.csv)
if(IDF_TARGET STREQUAL "esp32s3" OR IDF_TARGET STREQUAL "esp32p4")
    # Empty key store for explicit factory provisioning only.
    nvs_create_partition_image(psa ../partitions/nvs_psa.csv)
endif()

# Keep protobuf's allocator and file-I/O settings local to this third-party source.
# Suppress GCC's 'maybe-uninitialized' just for pb.c
set_source_files_properties(
  lua-protobuf/pb.c
  PROPERTIES
    COMPILE_FLAGS "-Wno-maybe-uninitialized -Wno-error=maybe-uninitialized"
    COMPILE_DEFINITIONS "BP_IO=0;PB_MALLOC=dlmalloc;PB_REALLOC=dlrealloc;PB_FREE=dlfree"
)

include("${CMAKE_CURRENT_LIST_DIR}/PatchLinenoise.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/PatchMdns.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/PatchRmt.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/PatchPsaNvs.cmake")
