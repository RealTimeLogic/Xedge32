Getting Started
================

To start using Xedge, you need to upload the firmware onto an ESP32-S3 or an ESP32 with PSRAM (for example, the `ESP32 WROVER <https://www.google.com/search?q=esp32+wrover>`_). You can do this using a flashing tool or programming software.

We offer two options for the firmware:

1. Ready-to-use firmware binary files which you can directly upload onto your board.
2. C source code files which you can compile using the Espressif ESP-IDF toolchain.

**Important:** We have provided instructions suitable for both the ESP32 and the newer ESP32-S3 chips. Make sure to follow the correct instructions for the chip that you're using.

.. contents:: Get started as follows:
   :depth: 2
   :local:


Firmware Option 1: Use Pre-Compiled Firmware
---------------------------------------------

The firmware binaries can be uploaded using Windows, Mac, and Linux using the command line tool `esptool`. Refer to the instructions below for details. We will start by showing how to use a graphical installer, which is only compatible with Windows.

Windows Graphical Installer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To upload the Xcode32 firmware to your ESP32 board, begin by downloading the `ESPRESSIF Flash Tool <https://www.espressif.com/en/support/download/other-tools>`_ and one of:

- `ESP32  Xedge Firmware <https://realtimelogic.com/downloads/bas/Xedge32-Firmware.zip>`_
- `ESP32-S3  Xedge Firmware <https://realtimelogic.com/downloads/bas/Xedge32-S3-Firmware.zip>`_

Unzip the `ESPRESSIF Flash Tool` archive and the `Xedge32-Firmware.zip` or `Xedge32-S3-Firmware.zip archive`.


1. Connect your ESP32 board to your computer and find the com port used by using the Device Manager.
2. Start the ESPRESSIF Flash Tool `flash_download_tool_3.9.4.exe` executable.
3. When the tool starts, select ChipType ESP32 or ESP32-S3 and click OK.
4. On the SPIDownload page, set the COM port to the one used by the ESP32 and set Speed to 115200.
5. Click the ERASE button and wait for it to complete.
6. Click the 3 dots (...) to browse to the bin files you downloaded.
7. Select the following binary files and set the address accordingly:

+-----------------------+-------------------+-------------------+
| Binary File           |   ESP32 Address   | ESP32-S3 Address  |
+=======================+===================+===================+
| `bootloader.bin`      | `0x1000`          | `0x0`             |
+-----------------------+-------------------+-------------------+
| `partition_table.bin` | `0x8000`          | `0x8000`          |
+-----------------------+-------------------+-------------------+
| `xedge.bin`           | `0x10000`         | `0x10000`         |
+-----------------------+-------------------+-------------------+

8.  Click the three checkboxes to the left of the three bin files to select them.
9.  Do not change any other values.
10. Click the START button and wait for it to upload the bin files to your ESP32.
11. When completed, open a terminal emulator such as Putty.
12. You should see a terminal window with text being printed, as shown in the screenshot below.
13. When you see `LuaShell32 ready` being printed, proceed by :ref:`setting the Wi-Fi credentials <wificredentials>` as explained below.

.. image:: https://realtimelogic.com/images/Xedg32-Flash-Firmware.png
   :alt: Firmware Upload Tool

In the screenshot above, the firmware tool is shown on the left and a Putty terminal connected to the device is shown on the right.

Linux, Mac, and Windows using the command line tool `esptool`
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following examples show how to use Linux. The commands are similar for Mac and Windows.

Install the required tool:

.. code-block:: sh

   sudo apt install python3-pip
   pip install esptool


Upload the **ESP32** firmware:

.. code-block:: sh

   wget https://realtimelogic.com/downloads/bas/Xedge32-Firmware.zip
   unzip Xedge32-Firmware.zip
   ~/.local/bin/esptool.py -p /dev/ttyUSB0 --before default_reset --after hard_reset --chip esp32  write_flash --flash_mode dio --flash_size detect --flash_freq 40m 0x1000 Xedge32-Firmware/bootloader.bin 0x8000 Xedge32-Firmware/partition-table.bin 0x10000 Xedge32-Firmware/xedge.bin


Upload the **ESP32-S3** firmware:

.. code-block:: sh

   wget https://realtimelogic.com/downloads/bas/Xedge32-S3-Firmware.zip
   unzip Xedge32-S3-Firmware.zip
   ~/.local/bin/esptool.py  -p /dev/ttyUSB0 --chip esp32-s3 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size detect 0x0  Xedge32-Firmware/bootloader.bin 0x8000 Xedge32-Firmware/partition-table.bin 0x10000  Xedge32-Firmware/xedge.bin 


Firmware Option 2: Compile The Code
-------------------------------------

See the `GitHub Repository <https://github.com/RealTimeLogic/Xedge-ESP32>`_ for details.


Set The Wi-Fi Credentials
--------------------------

.. _wificredentials:

Once the upload is complete, the ESP32 should reboot and display a :ref:`LuaShell32` prompt after printing some information.

1. In the :ref:`LuaShell32` prompt, type the following to connect to your Wi-Fi:

.. code-block:: lua

   esp32.netconnect("wifi", {ssid="your-Wi-Fi-SSID", pwd="password"})

2. After a second or two, the ESP32 should connect to your network.
3. Using your web browser, navigate to the IP address printed in the :ref:`LuaShell32` to access the ESP32.

The next time you power on the ESP32 device, it should automatically connect to your Wi-Fi network.


Next Step
------------

Once Xedge has successfully connected to your network, continue your journey by exploring our guide on how to use :ref:`Xedge32` .
