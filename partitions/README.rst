Partitions Generator Utility
============================

Introduction
------------
The Partitions Generator Utility is used during manufacturing to write various resources such as LUA applications, configurations, certificates, and network connection parameters to flash memory. It is based on two tools provided by Espressif: [fatfsgen.py](https://github.com/espressif/esp-idf/blob/v5.2.1/components/fatfs/fatfsgen.py) for generating the FAT image and [nvs_partition_gen.py](https://github.com/espressif/esp-idf/blob/v5.2.1/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py) for generating the NVS partition binary file.

Prerequisites
-------------

Ensure that the files for applications/configurations/certificates are copied to the 'partitions/storage' folder, and the connection parameters are set in the 'partitions/nvs_xedge_values.csv' file.

Usage
~~~~~

This tool provides the opportunity to use the stock xedge32 firmware and generate/write the binary images of the FAT and NVS by console commands. Additionally, it can generate/flash the FAT and NVS images with the firmware and bootloader when executing the "idf.py build flash" commands.

1. **Integrated Usage with CMakeLists.txt:**
   - Modify the main `CMakeLists.txt` to use the macros `fatfs_create_spiflash_image` and `nvs_create_partition_image` with `FLASH_IN_PROJECT` to automate the generation of binary files and include them in the `idf.py flash` command.
   
2. **Manual Usage:**
   - Generate the files separately and flash them to the ESP32 using the `esp_tool.py` utility provided by Espressif.

Running the Utility for NVS Manually
------------------------------------
Before running the utility manually, ensure that you have modified the connection parameters in the `nvs_xedge_values.csv` file.

For example, for Wi-Fi, you need to modify the parameter like this:

netAdapter,data,string,"wifi"
netSsid,data,string,"provider"
netPassword,data,string,"password"


**Usage**::

    python ~/esp/esp-idf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate nvs_xedge_values.csv nvs.bin 0x6000

    python ~/esp/esp-idf/components/esptool_py/esptool/esptool.py write_flash -z 0x9000 nvs.bin 

Running the Utility to Generate the FAT Image Manually
-------------------------------------------------------
Before generating the FAT image manually, copy the required files to the `storage` folder.

**Usage**::

    python ~/esp/esp-idf/components/fatfs/wl_fatfsgen.py --output_file storage.bin --sector_size 4096 --partition_size 0x1B5000 --long_name_support ./storage

    python ~/esp/esp-idf/components/esptool_py/esptool/esptool.py write_flash -z 0x640000 storage.bin 


