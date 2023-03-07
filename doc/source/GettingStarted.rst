Getting Started
================

To use the Barracuda App Server with the ESP32 peripheral interface, you must first compile the source code and upload the firmware to an ESP32 with PSRAM, such as the ESP32 WROVER development board.

The ESP32 interface is currently available on `GitHub under the V5 branch <https://github.com/RealTimeLogic/LspAppMgr-ESP32/tree/V5>`_.

To compile the source code, you must use the latest ESP-IDF, which can be found on `GitHub <https://github.com/espressif/esp-idf>`_.

The following Linux commands show all steps required for installing the development tools, downloading the ESP32 interface, the Barracuda App Server, and compiling the code:

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

   # Download the V5 LspAppMgr branch and latest BAS
   git clone --branch V5 https://github.com/RealTimeLogic/LspAppMgr-ESP32.git LspAppMgr
   cd LspAppMgr/main
   git clone https://github.com/RealTimeLogic/BAS.git BAS
   cd ..

   # Build the code
   idf.py build

Note that the code can be compiled in a Linux console, including the Windows Subsystem for Linux (WSL). If you use WSL, it's recommended to use generation one (WSL1), as it can be difficult to get the USB serial working in WSL2. For more information, see `the WSL documentation <https://docs.microsoft.com/en-us/windows/wsl/about>`_.

To upload the firmware to your ESP32, follow these steps:

1. Wait for the build process to complete.
2. Upload the code using ``idf.py flash monitor`` (WSL ``idf.py -p /dev/ttyS4 -b 115200 flash monitor``). If you are uncertain how to upload the firmware, refer to the `original build steps <https://realtimelogic.com/downloads/bas/ESP32/>`_.
3. Once the upload is complete, the ESP32 should reboot and display a :ref:`LuaShell32` prompt after printing some information.
4. In the :ref:`LuaShell32` prompt, type the following to connect to your Wi-Fi: ``esp32.connect("your-Wi-Fi-SSID","password")``.
5. After a second or two, the ESP32 should connect to your network.
6. Using your web browser, navigate to the IP address printed in :ref:`LuaShell32` to access the ESP32.


To have the ESP32 automatically connect at startup, create a file named ``.boot`` and insert the code used for connecting to your Wi-Fi e.g., ``esp32.connect("your-Wi-Fi-SSID","password")`` into .boot.

1. Navigate to the web file manager at ``http://esp-32-ip-address/fs/`` using your browser.
2. Drag and drop the ``.boot`` file onto the web file manager to upload it.
3. Verify that the ESP32 automatically connects to your Wi-Fi by rebooting it.
