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

Connect to a WiFi or wired network by providing the required configuration parameters. This function starts the connection phase as a background process and returns immediately. The connection status is printed in the :ref:`LuaShell32`. The ``cfg`` parameters are stored persistently in NVRAM if the ESP32 successfully connects to the network and the configuration parameters will be used to automatically connect to the network when the ESP32 restarts.

- ``network``: a string that can be one of:
    * ``wifi``: Connect to a Wi-Fi network by providing the SSID and password
    * ``W5500``:  Connect to an Ethernet network via a W5500 chip.
- ``cfg``: a required configuration table that must include:
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

You can also call this function to disconnect from a network by not providing the ``cfg`` configuration table argument. For WiFi, you may call this function with new configuration options to switch to another network.

.. code-block:: lua

   -- Example 1: connecting to Wi-Fi
   esp32.netconnect("wifi",{ssid="My-Wi-Fi", pwd="My-Password"})
   -- Example 2: Configuring Ethernet for EdgeBox-ESP-100
   esp32.netconnect("W5500", {spi=2,clk=13,mosi=12,miso=11,cs=10,freq=40000000,irq=14})

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

The following example shows how to set the GPIO pins CLK, CMD, and D0 for the Freenove ESP32-S3-WROOM CAM Board.

.. code-block:: lua

   esp32.sdcard(39, 38, 40)


esp32.execute(command)
-------------------------

This function performs various actions depending on the provided argument.

Commands:

- ``"erase"``: Erases the first FAT partition, which corresponds to the internal SPIFFS FAT file system. Call this function if the FAT file system becomes corrupt. The ESP32 will reboot after erasing the flash, and the FAT file system will be reformatted upon restart.

- ``"restart"``: Restarts the ESP32.

- ``"killmain"``: Terminates the main process powering LuaShell32, and reclaims memory. You may choose to terminate the main process and stop LuaShell32 when a network connection is established. Refer to the ``xedge.event()`` function for example code.



xedge.event()
-----------------

Xedge32 extends the `xedge.event() <https://realtimelogic.com/ba/doc/?url=Xedge.html#event>`_ mechanism, allowing you to subscribe and unsubscribe from network events, thus enabling the monitoring of network status changes.

Syntax
~~~~~~~

.. code-block:: lua

   xedge.event(callback [,unsubscribe])

Parameters
~~~~~~~~~~~

- ``callback``: A function that will be called when a network event occurs.
- ``unsubscribe``: An optional boolean parameter. If set to ``true``, the callback function will be unsubscribed from network events.

Callback Function
~~~~~~~~~~~~~~~~~~

The specified ``callback`` function will be called when the network changes state or when an error or warning message is generated. The function takes the following arguments, all represented as Lua strings, including numbers:

- ``"wifi"``: Indicates that the event is related to Wi-Fi connectivity.

  - **Arg1**: ``"up"``: Wi-Fi has transitioned from not connected to connected.
  - **Arg1**: ``"down"``: Wi-Fi has transitioned from connected to not connected.
  - **Arg1**: ``number``: A warning or error number as defined in the ESP-IDF (Espressif IoT Development Framework).



- ``wip`` (WiFi IP address received): Indicates that the device has successfully obtained its IP address, netmask, and gateway from the DHCP server over the WiFi connection.

  - **Arg1**: ``ip-address``: The assigned IP address.
  - **Arg2**: ``netmask``: The assigned network mask.
  - **Arg3**: ``gateway``: The assigned gateway.

- ``eth`` (Ethernet IP address received): Indicates that the device has successfully obtained its IP address, netmask, and gateway from the DHCP server over the Ethernet connection. This event is distributed on devices that has a connected Ethernet port.

  - **Arg1**: ``ip-address``: The assigned IP address.
  - **Arg2**: ``netmask``: The assigned network mask.
  - **Arg3**: ``gateway``: The assigned gateway.

- ``"sntp"``: This event indicates that the ESP32 has synchronized its system time with the time provided by the Network Time Protocol (NTP) server, typically pool.ntp.org. A correct system time is especially crucial when establishing a secure connection to a server using the Transport Layer Security (TLS) protocol. When a client connects to a server over TLS, one of the first steps in the handshake process is the verification of the server's certificate. This certificate includes a validity period - a 'not before' and 'not after' timestamp - and the client will check its current system time against this validity period.  The system time on the client device (in this case, the ESP32) is not set before receiving this event. Therefore, before establishing a secure server connection, any client must subscribe to the ``"sntp"`` event. This subscription ensures that the system time on the ESP32 is synchronized and accurate, thus allowing the TLS handshake process to proceed successfully. Attempting to establish a connection with a server before the system time has been updated will likely result in a failure due to the reasons outlined above. It's therefore crucial to monitor the ``"sntp"`` event and only proceed with the TLS handshake once the system time has been synchronized.


xedge.event usage
~~~~~~~~~~~~~~~~~~~~~

To subscribe to network events, simply pass a callback function to ``xedge.event()``

.. code-block:: lua

   xedge.event(function(event, arg1, arg2, arg3)
      if event == "wifi" then
         if arg1 == "up" then
            trace("Wi-Fi connected")
         elseif arg1 == "down" then
            trace("Wi-Fi disconnected")
         else
            trace("Wi-Fi error:", arg1)
         end
      elseif event == "wip" then
         trace("IP address:", arg1, "network mask", arg2, "gateway", arg3)
         -- We do not need LuaShell32 when we have a network connection
         esp32.execute"killmain"
      elseif event == "eth" then
         -- Received if this device has Ethernet.
         trace("IP address:", arg1, "network mask", arg2, "gateway", arg3)
      elseif event == "sntp" then
         trace("Time synchronized")
      end
   end)


To unsubscribe from network events, pass the same callback function along with the ``unsubscribe`` parameter set to ``true``::

  xedge.event(myCallbackFunction, true)

Note
~~~~

Keep in mind that all arguments in the callback function are represented as Lua strings, including numbers.
