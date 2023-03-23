Miscellaneous API
==================

The `esp32` module provides some miscellaneous functions.

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

esp32.wconnect([ssid, password])
--------------------------------

Connect to a Wi-Fi network by providing the SSID and password. This function starts the connection phase as a background process and returns immediately. The connection status is printed in the :ref:`LuaShell32`. The credentials are stored persistently in the NVRAM if the ESP32 successfully connects to the network.

You can also call this function to disconnect from a network by calling it without arguments, or to connect to a new network.

esp32.sdcard(clk, cmd, d0)
---------------------------

You can register a new disk named 'sd' if your ESP32 board includes an SDMMC interface. The `IO interface <https://realtimelogic.com/ba/doc/?url=lua.html#ba_ioinfo>`_ can then be opened by calling ``ba.openio("sd")``.

This function returns ``nil, error`` if it is unable to detect the SD card. Upon successfully configuring the settings, the function saves the values in the NVRAM and reboots the system. To remove existing settings, call this function without any arguments.

The example below shows how to set the GPIO pins CLK, CMD, and D0 for the Freenove ESP32-S3-WROOM CAM Board.

.. code-block:: lua

   esp32.sdcard(39, 38, 40)



esp32.execute(command)
-------------------------

This function performs various actions depending on the provided argument.

Commands:

- ``"erase"``: Erases the first FAT partition, which corresponds to the internal SPIFFS FAT file system. Call this function if the FAT file system becomes corrupt. The ESP32 will reboot after erasing the flash, and the FAT file system will be reformatted upon restart.

- ``"restart"``: Restarts the ESP32.

- ``"killmain"``: Terminates the main process powering LuaShell32, and reclaims memory. You may choose to terminate the main process and stop LuaShell32 when a network connection is established. Refer to the ``xedge.netevent()`` function for example code.



xedge.netevent()
-----------------

The ``xedge.netevent()`` function allows you to subscribe and unsubscribe from network events, enabling you to monitor changes to the network status.

Syntax
~~~~~~~

.. code-block:: lua

   xedge.netevent(callback [,unsubscribe])

Parameters
~~~~~~~~~~~

- ``callback``: A function that will be called when a network event occurs.
- ``unsubscribe``: An optional boolean parameter. If set to ``true``, the callback function will be unsubscribed from network events.

Callback Function
~~~~~~~~~~~~~~~~~~

The specified ``callback`` function will be called when the network changes state or when an error or warning message is generated. The function takes the following arguments, all represented as Lua strings, including numbers:

- ``"wifi"``: Indicates that the event is related to Wi-Fi connectivity.

  - ``"up"``: Wi-Fi has transitioned from not connected to connected.

- ``"wifi"``: Indicates that the event is related to Wi-Fi connectivity.

  - ``"down"``: Wi-Fi has transitioned from connected to not connected.

- ``"wifi"``: Indicates that the event is related to Wi-Fi connectivity.

  - ``number``: A warning or error number as defined in the ESP-IDF (Espressif IoT Development Framework).

- ``"wip"``: Indicates that the device has received its IP address, netmask, and gateway from the DHCP server.

  - ``ip-address``: The assigned IP address.
  - ``netmask``: The assigned network mask.
  - ``gateway``: The assigned gateway.

- ``"sntp"``: Indicates that the ESP32 has synchronized its system time with the time provided by pool.ntp.org.

xedge.netevent usage
~~~~~~~~~~~~~~~~~~~~~

To subscribe to network events, simply pass a callback function to ``xedge.netevent()``

.. code-block:: lua

   xedge.netevent(function(event, valA, valB, valC)
      if event == "wifi" then
         if valA == "up" then
            trace("Wi-Fi connected")
         elseif valA == "down" then
            trace("Wi-Fi disconnected")
         else
            trace("Wi-Fi error:", valA)
         end
      elseif event == "wip" then
         trace("IP address:", valA, "network mask", valB, "gateway", valC)
         -- We do not need LuaShell32 when we have a network connection
         esp32.execute"killmain"
      elseif event == "sntp" then
         trace("Time synchronized")
      end
   end)


To unsubscribe from network events, pass the same callback function along with the ``unsubscribe`` parameter set to ``true``::

  xedge.netevent(myCallbackFunction, true)

Note
~~~~

Keep in mind that all arguments in the callback function are represented as Lua strings, including numbers.
