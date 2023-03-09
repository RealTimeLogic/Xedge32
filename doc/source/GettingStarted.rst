Getting Started
================

To get started, you must upload the firmware to the ESP32 using a flashing tool or programming software. You can either use ready-to-use firmware binary files or compile the C source code using the ESP-IDF (Espressif IoT Development Framework).

Option 1: Use Binary
---------------------

To upload code to your ESP32 board, begin by downloading the `ESP32 firmware archive file <https://realtimelogic.com/downloads/bas/ESP32-firmware.zip>`_, which includes the ESP32 Flash Tool. Once you have downloaded the file, unzip the archive to any directory, and follow the steps below.

1. Connect your ESP32 board to your computer and find the com port used by using the Device Manager.
2. Start the `flash_download_tool_3.9.4.exe` executable.
3. When the tool starts, select ChipType ESP32 and click OK.
4. On the SPIDownload page, click the 3 dots (...) to browse to the bin files you downloaded.
5. Select the following binary files and set the address accordingly:


+-----------------------+-------------------+
| Binary File           | Address           |
+=======================+===================+
| `bootloader.bin`      | `0x1000`          |
+-----------------------+-------------------+
| `partition_table.bin` | `0x8000`          |
+-----------------------+-------------------+
| `xedge.bin`           | `0x10000`         |
+-----------------------+-------------------+

6. Click the three checkboxes to the left of the three bin files to select them.
7. Set the COM port to the one used by your ESP32. Do not change any other values.
8. Click the ERASE button and wait for it to complete.
9. Click the START button and wait for it to upload the bin files to your ESP32.
10. When completed, open a terminal emulator such as Putty.
11. Set the COM port to the one used by the ESP32 and set Speed to 115200.
12. You should see a terminal window with text being printed, as shown in the screenshot below.
13. When you see `LuaShell32 ready` being printed, proceed by :ref:`setting the Wi-Fi credentials <wificredentials>` as explained below.

.. image:: https://realtimelogic.com/images/ESP32-Upload-Tool.png
   :alt: Firmware Upload Tool

In the screenshot above, the firmware tool is shown on the left and a Putty terminal connected to the device is shown on the right.

Note that the included `flash_download_tool_3.9.4.exe` can also be downloaded from:
https://www.espressif.com/en/support/download/other-tools

Option 2: Compile The Code
--------------------------

The ESP32 source code is currently available on `GitHub under the V5 branch <https://github.com/RealTimeLogic/xedge-ESP32/tree/V5>`_.

To compile the source code, you must use the latest ESP-IDF, which can be found on `GitHub <https://github.com/espressif/esp-idf>`_.

The following Linux commands show all steps required for installing the development tools, downloading the required source code, and compiling the code:

.. code-block:: shell

   sudo apt -y update
   sudo apt -y install git wget flex bison gperf python3 python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

   cd
   # remove old installation, if any
   rm -rf .espressif esp/esp-idf

   # Install the latest esp-idf
   mkdir esp
   cd esp
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh
   cd ..
   source esp-idf/export.sh

   # Download the V5 xedge branch and latest BAS
   git clone --branch V5 https://github.com/RealTimeLogic/xedge-ESP32.git xedge
   cd xedge/main
   git clone https://github.com/RealTimeLogic/BAS.git BAS
   cd ..

   # Build the code
   idf.py build

Note that the code can be compiled in a Linux console, including the Windows Subsystem for Linux (WSL). If you use WSL, it's recommended to use generation one (WSL1), as it can be difficult to get the USB serial working in WSL2. For more information, see `the WSL documentation <https://docs.microsoft.com/en-us/windows/wsl/about>`_.

To upload the firmware to your ESP32, follow these steps:

1. Wait for the build process to complete.
2. Upload the code using ``idf.py flash monitor`` (WSL ``idf.py -p /dev/ttyS4 -b 115200 flash monitor``). If you are uncertain how to upload the firmware, refer to the `original build steps <https://realtimelogic.com/downloads/bas/ESP32/>`_.

Set The Wi-Fi Credentials
--------------------------

.. _wificredentials:

Once the upload is complete, the ESP32 should reboot and display a :ref:`LuaShell32` prompt after printing some information.

1. In the :ref:`LuaShell32` prompt, type the following to connect to your Wi-Fi:

.. code-block:: lua

   esp32.wconnect("your-Wi-Fi-SSID","password")

2. After a second or two, the ESP32 should connect to your network.
3. Using your web browser, navigate to the IP address printed in the :ref:`LuaShell32` to access the ESP32.


To have the ESP32 automatically connect at startup, create a file named ``.boot`` and insert the code used for connecting to your Wi-Fi e.g., ``esp32.wconnect("your-Wi-Fi-SSID","password")`` into .boot.

1. Navigate to the web file manager at ``http://esp-32-ip-address/fs/`` using your browser.
2. Drag and drop the ``.boot`` file onto the web file manager to upload it.
3. Verify that the ESP32 automatically connects to your Wi-Fi by rebooting it.
