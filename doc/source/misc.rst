Miscellaneous API
==================

The `esp32` module provides some miscellaneous functions.

esp32.apinfo()
--------------------

Returns a Lua table with WiFi information for the connected WiFi. The table includes several fields with WiFi information, where the most important one is the 'rssi' field, which stands for Received Signal Strength Indicator. The 'rssi' field indicates the strength of the WiFi signal that the ESP32 is receiving.  RSSI is typically expressed in decibels relative to a milliwatt (dBm). The value of 'rssi' is usually negative, as it represents power loss. Here is a general guideline for interpreting 'rssi' values:

- **-30 dBm**: Amazing. You're practically standing next to the router.
- **-50 dBm**: Excellent. Signal strength is really strong.
- **-60 dBm**: Good. Most online activities should work well.
- **-70 dBm**: Fair. You might start experiencing noticeable dips in speed.
- **-80 dBm or worse**: Poor. Connection might be spotty and unstable.

Example using the `serpent <https://github.com/pkulchenko/serpent>`_ module for formatting.

.. code-block:: lua

   local serpent = require("serpent")
   print(serpent.block(esp32.apinfo()))



esp32.loglevel(level)
-----------------------
   :param level: A string representing the desired log level for the ESP32 system log. 
   
   This function allows you to configure the log level for the ESP32 system log.
   The log level determines which log messages are displayed. The valid options for the ``level`` parameter are:

   - **none**: No log messages will be displayed.
   - **error**: Only error messages will be displayed (default setting).
   - **warn**: Error and warning messages will be displayed.
   - **info**: Error, warning, and informational messages will be displayed.
   - **debug**: Error, warning, informational, and debug messages will be displayed.
   - **verbose**: All log messages, including verbose debug messages, will be displayed.

   .. note::
   
      The maximum log level is configured in the menuconfig settings.
      If an invalid log level is provided, or if the requested log level exceeds the
      maximum level set in menuconfig, an error will be raised with a detailed message.


esp32.mac()
--------------------
This function returns the ESP32's 6 byte base MAC address.

Example:

.. code-block:: lua

   local mac=esp32.mac()
    -- Print MAC as hexadecimal numbers
   print(mac:gsub(".",function(x) return string.format("%02X",string.byte(x)) end))


esp32.wscan([print])
--------------------

Scan for Wi-Fi networks. The function returns a list of tables, where each table represents a network. The optional `print` argument can be set to `true` to print the data to :ref:`LuaShell32`.

Example:

.. code-block:: lua

   local fmt = string.format
   for _, net in pairs(esp32.wscan()) do
       print(fmt("SSID: %s\nChannel: %d\nRSSI: %d\nAuthmode: %s\nPChiper: %s\nGCipher: %s\n",
           net.ssid, net.channel, net.rssi, net.authmode, net.pchiper, net.gcipher))
   end

Note that this function takes some time to return.

esp32.netconnect(network, cfg)
--------------------------------

Initiate a WiFi or wired network connection by supplying the necessary configuration parameters. This function initiates the connection process in the background and returns control immediately. The status of the connection is displayed in the :ref:`LuaShell32`. The ``cfg`` parameters are saved in NVRAM on a successful network connection. This enables the ESP32 to reconnect to the network automatically using these parameters upon restart. The device initially operates in Access Point Mode by default, with the SSID set to xedge32. The default SSID password is 12345678.

- ``network``: a string that can be one of:
    * ``wifi``: Connect to a Wi-Fi network by providing the SSID and password
    * ``W5500``:  Connect to an Ethernet network via a W5500 chip.
- ``cfg``: a configuration table that must include:
    * For WiFi networks:
        * ``ssid``: the WiFi network's SSID
        * ``pwd``: the WiFi network's password
    * For wired networks via W5500:
        * ``spi``: The SPI bus number to which the W5500 chip is connected.
        * ``clk``: The GPIO pin number for the clock signal of the SPI bus.
        * ``mosi``: The GPIO pin number for the Master Out Slave In (MOSI) signal of the SPI bus.
        * ``miso``: The GPIO pin number for the Master In Slave Out (MISO) signal of the SPI bus.
        * ``cs``: The GPIO pin number for the chip select signal of the W5500 chip.
        * ``irq``: The GPIO pin number for the interrupt request signal of the W5500 chip.
        * ``freq``: The clock frequency (in Hz) of the SPI bus.

    if ``cfg`` is not provided and device is in WiFi Station Mode, the device reverts back to Access Point mode.

You can also call this function to disconnect from a network by not providing the ``cfg`` configuration table argument. For WiFi, you may call this function with new configuration options to switch to another network.

.. code-block:: lua

   -- Example 1: connecting to Wi-Fi
   esp32.netconnect("wifi",{ssid="My-Wi-Fi", pwd="My-Password"})
   -- Example 2: Configuring Ethernet for EdgeBox-ESP-100
   esp32.netconnect("W5500", {spi=2,clk=13,mosi=12,miso=11,cs=10,freq=40000000,irq=14})
   -- Example 3: Configuring Ethernet for LILYGO T-ETH-Lite
   esp32.netconnect("W5500", {spi=2,clk=13,mosi=12,miso=11,cs=10,freq=40000000,irq=14})
   -- Example 4: Revert a device operating in Station Mode to its original Access Point Mode.
   esp32.netconnect"wifi"

esp32.sdcard(width)
---------------------------

You can register a new disk named 'sd' if your ESP32 board includes an SDMMC interface. The `IO interface <https://realtimelogic.com/ba/doc/?url=lua.html#ba_ioinfo>`_ can then be opened by calling ``ba.openio("sd")``.

The function takes the following hardware dependent arguments:

.. code-block:: lua

   esp32.sdcard(width)
   esp32.sdcard(width, clk, cmd, d0)
   esp32.sdcard(width, clk, cmd, d0, d1, d2, d3)
   esp32.sdcard(width, clk, cmd, d0, d1, d2, d3, d4, d5, d6, d7)

Parameters:
~~~~~~~~~~~~~
- **width**: "bus width", can be 1, 4, or 8.
- **clk, cmd, d0-d7:**  Pin configuration parameters use the defaults for the CPU if not set.

#. ``clk``: GPIO number for the SD card clock pin.
#. ``cmd``: GPIO number for the SD card command pin.
#. ``d0``: GPIO number for the SD card data pin 0.
#. ``d1``: GPIO number for the SD card data pin 1 (when 4-bit wide bus).
#. ``d2``: GPIO number for the SD card data pin 2 (when 4-bit wide bus).
#. ``d3``: GPIO number for the SD card data pin 3 (when 4-bit wide bus).

Default pins:
~~~~~~~~~~~~~~~~~~~~

On ESP32, SDMMC peripheral is connected to specific GPIO pins using the IO MUX. GPIO pins cannot be customized. The following list shows the default settings:

- clk = GPIO14, cmd = GPIO15, d0 = GPIO2, 
- d1 = GPIO4, d2 = GPIO12, d3 = GPIO13, 
- d4 = GPIO33, d5 = GPIO34, d5 = GPIO35, d5 = GPIO36.

On ESP32-S3, SDMMC peripheral is connected to GPIO pins using a GPIO matrix, which enables arbitrary GPIOs to be used to connect an SD card. The following list shows the default settings:

- clk = GPIO34, cmd = GPIO33, d0 = GPIO37, 
- d1 = GPIO38, d2 = GPIO39, d3 = GPIO36,  
- d4 = GPIO35, d5 = GPIO40, d6 = GPIO42, d7 = 41.

Returns:
~~~~~~~~~

The function returns ``nil, error`` if it is unable to detect the SD card. Upon successfully configuring the settings, the function saves the values in the NVRAM and reboots the system. To remove existing settings, call this function without any arguments.

Examples:
~~~~~~~~~

Initialize the SD-CARD driver of a 1-bit wide bus that has clock pin connected to GPIO14, command to GPI15, and data to GPIO2.

.. code-block:: lua

   esp32.sdcard(1)

The following example shows how to set the GPIO pins CLK, CMD, and D0 for a few board.

.. code-block:: lua

   esp32.sdcard(1, 39, 38, 40) -- ESP32-S3-WROOM CAM Board
   esp32.sdcard(1,  7,  6,  5) -- Lilygo's T-ETH-Lite
   esp32.sdcard(1,  7,  9,  8) -- XIAO ESP32S3 Sense

.. _esp32-execute-label:

esp32.execute(command)
-------------------------

This function performs various actions depending on the provided argument.

Commands:

- ``"erase"``: Erases the first FAT partition, which corresponds to the internal SPIFFS FAT file system. Call this function if the FAT file system becomes corrupt. The ESP32 will reboot after erasing the flash, and the FAT file system will be reformatted upon restart.

- ``"restart"``: Restarts the ESP32.

- ``"killmain"``: Terminates the main process powering LuaShell32, and reclaims memory. You may choose to terminate the main process and stop LuaShell32 when a network connection is established. Refer to the ``xedge.event()`` function for example code.

- ``"mdns"``: Change the mDNS name (see example below). The default name is Xedge32.

.. code-block:: lua

   esp32.execute("mdns", "myesp") -- Change mDNS name to 'myesp'
   esp32.execute"restart" -- Navigate to http://myesp.local after restarting



xedge.event()
-----------------

Xedge32 extends the Xedge xedge.event() mechanism, allowing you to subscribe and unsubscribe from network events, thus enabling the monitoring of network status changes. The following shows the xedge.event() function in the `Xedge xedge.event() <https://realtimelogic.com/ba/doc/?url=Xedge.html#event>`_ documentation.

.. code-block:: lua

   xedge.event(event, callback [,unsubscribe])

All Xedge32 events carry a 'retained' flag, ensuring subscribers receive these events even if they subscribe after the event's generation.

The specified ``callback`` function will be called when the network changes state or when an error or warning message is generated. The function takes the following arguments, all represented as Lua strings, including numbers:

- ``"wifi"``: Indicates that the event is related to Wi-Fi connectivity.

  - **Arg1**: ``"up"``: Wi-Fi has transitioned from not connected to connected.
  - **Arg1**: ``"down"``: Wi-Fi has transitioned from connected to not connected.
  - **Arg1**: ``number``: A warning or error number as defined in the ESP-IDF (Espressif IoT Development Framework).
  - **Arg2**: ``"ap" | "sta"``: Tells you Wi-Fi mode, which is Access Point Mode or Station Mode.


- ``wip`` (WiFi IP address received): Indicates that the device has successfully obtained its IP address, netmask, and gateway from the DHCP server over the WiFi connection.

  - **Arg1**: ``ip-address``: The assigned IP address.
  - **Arg2**: ``netmask``: The assigned network mask.
  - **Arg3**: ``gateway``: The assigned gateway.

- ``eth`` (Ethernet IP address received): Indicates that the device has successfully obtained its IP address, netmask, and gateway from the DHCP server over the Ethernet connection. This event is distributed on devices that has a connected Ethernet port.

  - **Arg1**: ``ip-address``: The assigned IP address.
  - **Arg2**: ``netmask``: The assigned network mask.
  - **Arg3**: ``gateway``: The assigned gateway.

- ``"sntp"``: This event indicates that the ESP32 has synchronized its system time with the time provided by the Network Time Protocol (NTP) server, typically pool.ntp.org. This event is generated  when the device receives the time from the network. A correct system time is especially crucial when establishing a secure connection to a server using the Transport Layer Security (TLS) protocol. When a client connects to a server over TLS, one of the first steps in the handshake process is the verification of the server's certificate. This certificate includes a validity period - a 'not before' and 'not after' timestamp - and the client will check its current system time against this validity period.  The system time on the client device (in this case, the ESP32) is not set before receiving this event. Therefore, before establishing a secure server connection, any client must subscribe to the ``"sntp"`` event. This subscription ensures that the system time on the ESP32 is synchronized and accurate, thus allowing the TLS handshake process to proceed successfully. Attempting to establish a connection with a server before the system time has been updated will likely result in a failure due to the reasons outlined above. It's therefore crucial to monitor the ``"sntp"`` event and only proceed with the TLS handshake once the system time has been synchronized.


Example code
~~~~~~~~~~~~~~~~

.. code-block:: lua

   xedge.event("wifi",function(status)
      if status == "up" then
         trace("Wi-Fi connected")
      elseif status == "down" then
         trace("Wi-Fi disconnected")
      else
         trace("Wi-Fi error:", status)
      end
   end)

   xedge.event("wip",function(ip,mask,gw)
      trace("IP address:", ip, "network mask", mask, "gateway", gw)
      -- We do not need LuaShell32 when we have a network connection
      esp32.execute"killmain"
   end)

   xedge.event("eth",function(ip,mask,gw)
      -- Received if this device has Ethernet and Ethernet connected during runtime.
      trace("IP address:", ip, "network mask", mask, "gateway", gw)
   end)

   xedge.event("sntp",function()
      trace("Time synchronized")
   end)


Note
~~~~

All arguments provided by C-code-generated-events are represented as Lua strings, including numbers.

