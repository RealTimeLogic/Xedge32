Getting Started
================

To get started, you must upload the firmware to an ESP32 with PSRAM, such as the `ESP32 WROVER <https://www.google.com/search?q=esp32+wrover>`_ board, using a flashing tool or programming software. You can either use our ready-to-use firmware binary files or compile the C source code using the Espressif ESP-IDF.


Option 1: Use Binaries
------------------------

The binaries can be installed using Windows, Mac, and Linux using the command line tool `esptool`. Refer to the instructions below for details. We will start by showing how to use a graphical installer, which is only compatible with Windows.

Windows
~~~~~~~~

To upload the Xcode32 firmware to your ESP32 board, begin by downloading the `ESPRESSIF Flash Tool <https://www.espressif.com/en/support/download/other-tools>`_ and `Xedge32-Firmware.zip <https://realtimelogic.com/downloads/bas/Xedge32-Firmware.zip>`_. Unzip both archives.


1. Connect your ESP32 board to your computer and find the com port used by using the Device Manager.
2. Start the ESPRESSIF Flash Tool `flash_download_tool_3.9.4.exe` executable.
3. When the tool starts, select ChipType ESP32 and click OK.
4. On the SPIDownload page, set the COM port to the one used by the ESP32 and set Speed to 115200.
5. Click the ERASE button and wait for it to complete.
6. Click the 3 dots (...) to browse to the bin files you downloaded.
7. Select the following binary files and set the address accordingly:

+-----------------------+-------------------+
| Binary File           | Address           |
+=======================+===================+
| `bootloader.bin`      | `0x1000`          |
+-----------------------+-------------------+
| `partition_table.bin` | `0x8000`          |
+-----------------------+-------------------+
| `xedge.bin`           | `0x10000`         |
+-----------------------+-------------------+

8.  Click the three checkboxes to the left of the three bin files to select them.
9.  Do not change any other values.
10. Click the START button and wait for it to upload the bin files to your ESP32.
11. When completed, open a terminal emulator such as Putty.
12. You should see a terminal window with text being printed, as shown in the screenshot below.
13. When you see `LuaShell32 ready` being printed, proceed by :ref:`setting the Wi-Fi credentials <wificredentials>` as explained below.

.. image:: https://realtimelogic.com/images/Xedg32-Flash-Firmware.png
   :alt: Firmware Upload Tool

In the screenshot above, the firmware tool is shown on the left and a Putty terminal connected to the device is shown on the right.

How To Install The Binaries Using `esptool`
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example shows how to use Linux. The commands are similar for Mac and Windows.

.. code-block:: shell

   sudo apt install python3-pip
   pip install esptool
   wget https://realtimelogic.com/downloads/bas/Xedge32-Firmware.zip
   unzip Xedge32-Firmware.zip
   ~/.local/bin/esptool.py -p /dev/ttyUSB0 --before default_reset --after hard_reset --chip esp32  write_flash --flash_mode dio --flash_size detect --flash_freq 40m 0x1000 Xedge32-Firmware/bootloader.bin 0x8000 Xedge32-Firmware/partition-table.bin 0x10000 Xedge32-Firmware/xedge.bin




Option 2: Compile The Code
--------------------------

See the `GitHub Repository <https://github.com/RealTimeLogic/Xedge-ESP32>`_ for details.


Set The Wi-Fi Credentials
--------------------------

.. _wificredentials:

Once the upload is complete, the ESP32 should reboot and display a :ref:`LuaShell32` prompt after printing some information.

1. In the :ref:`LuaShell32` prompt, type the following to connect to your Wi-Fi:

.. code-block:: lua

   esp32.wconnect("your-Wi-Fi-SSID","password")

2. After a second or two, the ESP32 should connect to your network.
3. Using your web browser, navigate to the IP address printed in the :ref:`LuaShell32` to access the ESP32.

The next time you power on the ESP32 device, it should automatically connect to your Wi-Fi network.

.. 1. Navigate to the web file manager at ``http://esp-32-ip-address/fs/`` using your browser. 2. Drag and drop the ``.boot`` file onto the web file manager to upload it. 3. Verify that the ESP32 automatically connects to your Wi-Fi by rebooting it.
