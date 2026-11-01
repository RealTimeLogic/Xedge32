Partitions Generator Utility
============================

Introduction
------------
The Partitions Generator Utility is used during manufacturing to write various resources such as LUA applications, configurations, certificates, and network connection parameters to flash memory. It is based on two tools provided by Espressif: [fatfsgen.py](https://github.com/espressif/esp-idf/blob/master/components/fatfs/fatfsgen.py) for generating the FAT image and [nvs_partition_gen.py](https://github.com/espressif/esp-idf/blob/master/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py) for generating the NVS partition binary file.

Prerequisites
-------------
Ensure that the files for applications/configurations/certificates are copied to the **partitions/storage** folder, and the connection parameters are set in the **partitions/nvs_xedge_values.csv** file.

Usage
-----

The build generates factory filesystem and NVS images. Flash them explicitly using the commands below; ordinary `idf.py flash` preserves these data partitions.

1. **Factory provisioning:**
   - The build generates NVS and storage images, but ordinary `idf.py flash` preserves those partitions. Use `idf.py nvs-flash storage-flash` explicitly to install the factory images. This replaces saved network settings and application files. On S3/P4, explicitly add `psa-flash` to reset PSA keys as well. Use these commands only for a new board or an intentional reset. Release factory images include all three partitions on S3/P4; classic ESP32 has no PSA partition.
   
2. **Manual Usage:**
   - Generate the files separately and flash them to the ESP32 using the **esp_tool.py** utility provided by Espressif.

Running the Utility for NVS Manually
------------------------------------
Before running the utility manually, ensure that you have modified the connection parameters in the **nvs_xedge_values.csv** file.

For example, for Wi-Fi, you need to modify the parameter like this:

```
netAdapter,data,string,"wifi"
netSsid,data,string,"provider"
netPassword,data,string,"password"
```

**Usage**::

    python ~/esp/esp-idf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate nvs_xedge_values.csv nvs.bin 0x6000

    python ~/esp/esp-idf/components/esptool_py/esptool/esptool.py write_flash -z 0x9000 nvs.bin 

Running the Utility to Generate the FAT Image Manually
-------------------------------------------------------
Before generating the FAT image manually, copy the required files to the `storage` folder.

**Usage**::

    python ~/esp/esp-idf/components/fatfs/wl_fatfsgen.py --output_file storage.bin --sector_size 4096 --partition_size 0x1B5000 --long_name_support ./storage

    python ~/esp/esp-idf/components/esptool_py/esptool/esptool.py write_flash -z 0x640000 storage.bin 



PSA key storage (S3/P4)
----------------------
The `psa` partition occupies 24 KiB at `0x12000`, in the former alignment gap.
Other partition offsets and sizes are unchanged. It is reserved in PSA and
softTPM builds. `nvs_psa.csv` creates an empty key store for factory provisioning;
it contains no device secrets and is excluded from ordinary `idf.py flash`.

The `nvs` partition holds network and hardware settings. If its initialization
reports no free pages or an unsupported newer format, Xedge32 erases `nvs` and
retries; network settings must then be configured again. A PSA partition
initialization error stops startup. Xedge32 never automatically erases `psa`
or falls back to configuration NVS. This separation does not provide
hardware-isolated key storage.

Ordinary `idf.py flash` and application OTA preserve the data partitions.
Explicit `idf.py nvs-flash storage-flash` replaces configuration and application
files. On S3/P4, `idf.py psa-flash` separately replaces the key store with an
empty image and permanently loses existing PSA keys. Factory release images reset
all three partitions; do not use them to preserve an installed identity.

Upgrading from firmware that stored PSA keys in `nvs` requires installing the
new partition table and deliberately reprovisioning keys, certificates, and
encrypted data. There is no key migration. Application-only OTA is insufficient
for this first upgrade; use a factory installation. Subsequent application OTA
updates require retaining the matching crypto backend.
