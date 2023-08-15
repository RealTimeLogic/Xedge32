Getting Started
================

To start using Xedge, you need to upload the firmware onto an ESP32-S3 or an ESP32 with PSRAM (for example, the ESP32 WROVER). You can do this using a flashing tool or programming software. We suggested purchasing the new improved `ESP32-S3 <https://www.google.com/search?q=esp32-S3>`_ if you do not have a suitable ESP32.

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

.. raw:: html

   <div style="position: relative;width: 100%;height: 0;padding-bottom: 56.25%;"><iframe src="https://www.youtube.com/embed/V-HH0yc74hY" frameborder="0" allowfullscreen style="position: absolute;top: 0;left: 0;width: 100%;height: 100%;"></iframe></div>

**How to flash the Xedge32 IDE**

To upload the Xcode32 firmware to your ESP32 board, begin by downloading the `ESPRESSIF Flash Tool <https://www.espressif.com/en/support/download/other-tools>`_ and one of:

- `ESP32  Xedge Firmware <https://realtimelogic.com/downloads/bas/Xedge32-Firmware.zip>`_
- `ESP32-S3  Xedge Firmware <https://realtimelogic.com/downloads/bas/Xedge32-S3-Firmware.zip>`_

Unzip the `ESPRESSIF Flash Tool` archive and the `Xedge32-Firmware.zip` or `Xedge32-S3-Firmware.zip archive`.


1. Connect your ESP32 board to your computer and find the com port used by using the Device Manager.
2. Start the ESPRESSIF Flash Tool `flash_download_tool_3.9.4.exe` executable.
3. When the tool starts, select ChipType ESP32 or ESP32-S3 and click OK.
4. On the SPIDownload page, `set the COM port to the one used by the ESP32 <https://learn.adafruit.com/adafruit-esp32-s2-feather/advanced-serial-console-on-windows>`_ and set Speed to 115200.
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

**Potential Issues and Solutions:**

   -  On certain boards, you must press the boot button before connecting the USB. Once the USB is plugged in, you can release the button.
   -  If you're using an ESP32-S3 board equipped with both USB-OTG and USB-UART, you will need to perform flashing using the USB-UART connection. Meanwhile, connect Putty to USB-OTG.
   - If you do not see the :ref:`LuaShell32` after connecting Putty, try the following: Configure Putty with the required serial connection parameters so you can quickly click the Open button, then do as follows: Click the ESP32 development board's reset button, followed by quickly clicking the Open button in Putty.


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
   python -m esptool --chip esp32 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size detect --flash_freq 40m 0x1000 Xedge32-Firmware/bootloader.bin 0x8000 Xedge32-Firmware/partition-table.bin 0x10000 Xedge32-Firmware/xedge.bin

Upload the **ESP32-S3** firmware:

.. code-block:: sh

   wget https://realtimelogic.com/downloads/bas/Xedge32-S3-Firmware.zip
   unzip Xedge32-S3-Firmware.zip
   python -m esptool --chip esp32s3 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size detect --flash_freq 80m 0x0 Xedge32-Firmware/bootloader/bootloader.bin 0x8000 Xedge32-Firmware/partition_table/partition-table.bin 0x10000 Xedge32-Firmware/xedge.bin


Firmware Option 2: Compile The Code
-------------------------------------

See the `GitHub Repository <https://github.com/RealTimeLogic/Xedge-ESP32>`_ for details.


Set The Wi-Fi Credentials
--------------------------

.. _wificredentials:

Once the upload is complete, the ESP32 should reboot and display a :ref:`LuaShell32` prompt after printing some information. See the *Potential Issues and Solutions* section above if you do not see the LuaShell32.


1. In the :ref:`LuaShell32` prompt, type the following to connect to your Wi-Fi:

.. code-block:: lua

   esp32.netconnect("wifi", {ssid="your-Wi-Fi-SSID", pwd="password"})

2. After a second or two, the ESP32 should connect to your network.
3. Using your web browser, navigate to the IP address printed in the :ref:`LuaShell32` to access the ESP32.

The next time you turn on your ESP32 device, it will automatically connect to your Wi-Fi network, so there's no need to reconnect the serial console and use the LuaShell32. Here's what happens:

1. IP Address Assignment: The ESP32 gets an IP address from the network's DHCP (Dynamic Host Configuration Protocol). Usually, your router will assign the same IP address each time the ESP32 reconnects to the network.

2. Bookmarking the IP Address: You can bookmark this IP address in your web browser. That way, the next time you power on your ESP32, you can simply click the bookmark to connect to it.

3. Consistent IP Address with DHCP Reservations: If you want to make sure the ESP32 always uses the same IP address, most routers allow you to reserve that IP address specifically for your device using `DHCP reservations <https://portforward.com/dhcp-reservation/>`_. This ensures consistent IP address assignment for the ESP32.

4. Using Let's Encrypt Plugin - SharkTrust: As an alternative, you can also enable a permanent URL for your ESP32 by activating the Let's Encrypt plugin called SharkTrust through the `Xedge's configuration menu <https://realtimelogic.com/ba/doc/?url=Xedge.html#cert>`_.

By following these guidelines, you can create a seamless connection experience with your ESP32 device without using a serial connection to discover the IP address assignment.


Next Step
------------

Once Xedge has successfully connected to your network, continue your journey by exploring our guide on how to use :ref:`Xedge32` .
