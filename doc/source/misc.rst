Miscellaneous API
==================

The `esp32` module provides some miscellaneous functions.

esp32.mac()
~~~~~~~~~~~~~~~~~~~~
This function returns the ESP32's 6 byte base MAC address.

Example:

.. code-block:: lua

   local mac=esp32.mac():gsub(".",function(x) return string.format("%02X",string.byte(x)) end)
   print(mac) -- Print MAC as hexadecimal numbers


esp32.wscan([print])
~~~~~~~~~~~~~~~~~~~~

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
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Connect to a Wi-Fi network by providing the SSID and password. This function starts the connection phase as a background process and returns immediately. The connection status is printed in the :ref:`LuaShell32`.

You can also call this function to disconnect from a network by calling it without arguments, or to connect to a new network.
