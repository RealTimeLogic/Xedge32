Miscellaneous API
=================

The ``esp32`` module also includes a set of utility functions that do not fit
under a single peripheral category. These functions cover Wi-Fi information,
network connection management, SD card setup, device control actions, event
subscriptions, and OTA firmware updates.

This page groups those helpers into one place so you can quickly find common
system-level operations.

``esp32.apinfo()``
------------------

Returns a Lua table describing the currently connected Wi-Fi network.

One of the most useful fields is ``rssi``, the Received Signal Strength
Indicator. RSSI is reported in dBm and is usually a negative number.

General RSSI guidance:

- ``-30 dBm``: Outstanding signal strength.
- ``-50 dBm``: Excellent signal strength.
- ``-60 dBm``: Good for normal use.
- ``-70 dBm``: Fair, but performance may start to degrade.
- ``-80 dBm`` or lower: Weak and potentially unstable.

Example using the `serpent <https://github.com/pkulchenko/serpent>`_ module for
formatting:

.. code-block:: lua

   local serpent = require("serpent")
   print(serpent.block(esp32.apinfo()))

``esp32.loglevel(level)``
-------------------------

Controls the ESP32 system log verbosity.

Parameter:

- ``level``: One of ``"none"``, ``"error"``, ``"warn"``, ``"info"``,
  ``"debug"``, or ``"verbose"``.

Meaning of each level:

- ``"none"``: Disable log output.
- ``"error"``: Show only error messages. This is the default.
- ``"warn"``: Show errors and warnings.
- ``"info"``: Show errors, warnings, and informational messages.
- ``"debug"``: Add debug messages.
- ``"verbose"``: Show the most detailed available output.

.. note::

   The maximum available log level depends on the firmware build settings in
   ``menuconfig``. If you request a level that exceeds the compiled maximum, the
   function raises an error.

``esp32.mac()``
---------------

Returns the ESP32 base MAC address as a 6-byte Lua string.

Example:

.. code-block:: lua

   local mac = esp32.mac()
   print(mac:gsub(".", function(x)
      return string.format("%02X", string.byte(x))
   end))

``esp32.wscan([print])``
------------------------

Scans for visible Wi-Fi networks and returns a list of tables, one table per
network.

If the optional argument is ``true``, the function also prints the result to
:ref:`LuaShell32`.

Example:

.. code-block:: lua

   local fmt = string.format
   for _, net in pairs(esp32.wscan()) do
      print(fmt(
         "SSID: %s\nChannel: %d\nRSSI: %d\nAuthmode: %s\nPChiper: %s\nGCipher: %s\n",
         net.ssid, net.channel, net.rssi, net.authmode, net.pchiper, net.gcipher
      ))
   end

.. note::

   Wi-Fi scanning takes time. Plan for a noticeable delay before the function
   returns.

``esp32.netconnect(network, cfg)``
----------------------------------

Starts a Wi-Fi or wired network connection in the background and returns
immediately. Connection progress and status messages are shown in
:ref:`LuaShell32`.

If the connection succeeds, the configuration is stored in NVRAM so the device
can reconnect automatically after a restart.

By default, an unconfigured system starts in Access Point Mode with SSID
``xedge32`` and password ``12345678``.

Network Parameters
~~~~~~~~~~~~~~~~~~

``network``
   One of:

- ``"wifi"``: Connect through Wi-Fi.
- ``"W5500"`` or ``"DM9051"``: Connect through SPI Ethernet when the firmware
  includes support for that controller.
- ``"IP101"``, ``"RTL8201"``, ``"LAN87XX"``, ``"DP83848"``, or ``"KSZ80XX"``:
  Connect through RMII Ethernet when the firmware and board support that PHY.

``cfg``
   Configuration table. Required fields depend on the selected network type.

Wi-Fi configuration fields:

- ``ssid``: Wi-Fi network name.
- ``pwd``: Wi-Fi password.

SPI Ethernet configuration fields:

- ``spi``: SPI bus number.
- ``clk``: SPI clock GPIO.
- ``mosi``: MOSI GPIO.
- ``miso``: MISO GPIO.
- ``cs``: Chip-select GPIO.
- ``irq``: Interrupt GPIO.
- ``freq``: SPI clock frequency in Hertz.
- ``rst``: Optional PHY reset GPIO.

RMII Ethernet configuration fields:

- ``rst``: PHY reset GPIO.
- ``mdio``: MDIO GPIO.
- ``mdc``: MDC GPIO.

If ``cfg`` is omitted while the device is already running in Wi-Fi Station
Mode, the device switches back to Access Point Mode.

Examples:

.. code-block:: lua

   -- Connect to Wi-Fi
   esp32.netconnect("wifi", {ssid = "My-Wi-Fi", pwd = "My-Password"})

   -- Configure Ethernet for EdgeBox-ESP-100
   esp32.netconnect("W5500", {spi = 2, clk = 13, mosi = 12, miso = 11, cs = 10, freq = 40000000, irq = 14})

   -- Configure Ethernet for LILYGO T-ETH-Lite
   esp32.netconnect("W5500", {spi = 2, clk = 13, mosi = 12, miso = 11, cs = 10, freq = 40000000, irq = 14})

   -- Configure RMII Ethernet with a DP83848 PHY
   esp32.netconnect("DP83848", {rst = 5, mdio = 18, mdc = 23})

   -- Return from Station Mode to Access Point Mode
   esp32.netconnect"wifi"

.. note::

   Ethernet support is build-dependent. If an adapter name is rejected or the
   driver fails to start, verify that the firmware was built with the matching
   Ethernet PHY support and that the board wiring matches the selected adapter.

``esp32.sdcard(width)``
-----------------------

Registers a new disk named ``sd`` when the board provides an SDMMC interface.
After configuration, you can open the device through the `IO interface
<https://realtimelogic.com/ba/doc/?url=lua.html#ba_ioinfo>`_ with
``ba.openio("sd")``.

Supported call forms:

.. code-block:: lua

   esp32.sdcard(width)
   esp32.sdcard(width, clk, cmd, d0)
   esp32.sdcard(width, clk, cmd, d0, d1, d2, d3)
   esp32.sdcard(width, clk, cmd, d0, d1, d2, d3, d4, d5, d6, d7)

SD Card Parameters
~~~~~~~~~~~~~~~~~~

- ``width``: Bus width. Valid values are ``1``, ``4``, or ``8``.
- ``clk``: Clock pin.
- ``cmd``: Command pin.
- ``d0`` to ``d7``: Data pins.

Default Pins
~~~~~~~~~~~~

On **ESP32**, the SDMMC peripheral uses fixed IO-MUX-connected pins:

- ``clk`` = GPIO14
- ``cmd`` = GPIO15
- ``d0`` = GPIO2
- ``d1`` = GPIO4
- ``d2`` = GPIO12
- ``d3`` = GPIO13
- ``d4`` = GPIO33
- ``d5`` = GPIO34
- ``d6`` = GPIO35
- ``d7`` = GPIO36

On **ESP32-S3** and other targets with SDMMC GPIO matrix routing, the pins are
more flexible. The default ESP32-S3 mapping is:

- ``clk`` = GPIO34
- ``cmd`` = GPIO33
- ``d0`` = GPIO37
- ``d1`` = GPIO38
- ``d2`` = GPIO39
- ``d3`` = GPIO36
- ``d4`` = GPIO35
- ``d5`` = GPIO40
- ``d6`` = GPIO42
- ``d7`` = GPIO41

Return Behavior
~~~~~~~~~~~~~~~

- Returns ``nil, error`` if the SD card cannot be detected.
- On success, stores the configuration in NVRAM and reboots the system.
- Calling the function without arguments removes an existing SD card
  configuration.

Custom SD card pins are only available on targets where the SDMMC peripheral
can be routed through the GPIO matrix. On original ESP32 targets, use the
fixed IO-MUX-connected SDMMC pins.

Examples:

.. code-block:: lua

   -- Initialize a 1-bit SD card bus using the default pin mapping
   esp32.sdcard(1)

   -- Example custom mappings for specific boards
   esp32.sdcard(1, 39, 38, 40) -- ESP32-S3-WROOM CAM Board
   esp32.sdcard(1,  7,  6,  5) -- LilyGO T-ETH-Lite
   esp32.sdcard(1,  7,  9,  8) -- XIAO ESP32S3 Sense

.. _esp32-execute-label:

``esp32.execute(command)``
--------------------------

Performs device-level control actions.

Supported commands:

- ``"erase"``: Erase the first FAT partition, reformat it on reboot, and
  restart the device. Use this if the internal FAT filesystem becomes corrupt.
- ``"restart"``: Restart the ESP32.
- ``"killmain"``: Stop the main process that powers LuaShell32 and reclaim its
  memory.
- ``"mdns"``: Change the mDNS name.

Example:

.. code-block:: lua

   esp32.execute("mdns", "myesp")
   esp32.execute"restart"

After restarting, the device can be reached at ``http://myesp.local`` if mDNS
is available on your network.

``xedge.event()``
-----------------

Xedge32 extends the generic Xedge event mechanism so Lua code can subscribe to
network-related runtime events.

Function signature:

.. code-block:: lua

   xedge.event(event, callback [, unsubscribe])

All Xedge32-generated events are retained. That means a late subscriber can
still receive the most recent event state when appropriate.

Event Types
~~~~~~~~~~~

``"wifi"``
   Reports Wi-Fi state changes and Wi-Fi-related warnings or errors.

   Callback arguments:

   - ``"up"`` when Wi-Fi transitions to connected.
   - ``"down"`` when Wi-Fi transitions to disconnected.
   - an ESP-IDF warning or error number as a string.
   - a second argument, ``"ap"`` or ``"sta"``, identifying Access Point or
     Station mode.

``"wip"``
   Reports that Wi-Fi has received IP configuration from DHCP.

   Callback arguments:

   - IP address
   - netmask
   - gateway

``"eth"``
   Reports that Ethernet has received IP configuration from DHCP.

   Callback arguments:

   - IP address
   - netmask
   - gateway

``"sntp"``
   Reports that the system time has been synchronized over SNTP.

This event is especially important before opening TLS connections, because
certificate validation depends on the device clock being correct.

Event Examples
~~~~~~~~~~~~~~

.. code-block:: lua

   xedge.event("wifi", function(status)
      if status == "up" then
         trace("Wi-Fi connected")
      elseif status == "down" then
         trace("Wi-Fi disconnected")
      else
         trace("Wi-Fi error:", status)
      end
   end)

   xedge.event("wip", function(ip, mask, gw)
      trace("IP address:", ip, "network mask", mask, "gateway", gw)
      esp32.execute"killmain"
   end)

   xedge.event("eth", function(ip, mask, gw)
      trace("IP address:", ip, "network mask", mask, "gateway", gw)
   end)

   xedge.event("sntp", function()
      trace("Time synchronized")
   end)

One-Time SNTP Subscription Example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example subscribes to the ``"sntp"`` event, records the first
time synchronization, and then unregisters itself.

.. code-block:: lua

   local sntp

   function onunload()
      xedge.event("sntp", sntp, true)
   end

   sntp = function()
      onunload()
      startTime = ba.datetime"NOW"
      xedge.elog({ts = true}, "Boot time=%s", startTime:tostring())
      xedge.eflush({subject = "App starting"})
   end

   xedge.event("sntp", sntp)

.. note::

   All arguments supplied by C-generated Xedge32 events are represented as Lua
   strings, including values that conceptually represent numbers.

Xedge32 OTA
-----------

Xedge32 provides core OTA support through ``esp32.ota``.

Call forms:

- ``esp32.ota()`` returns the current firmware version table.
- ``esp32.ota"begin"`` starts an upgrade session and returns an OTA object.

Examples
~~~~~~~~

Retrieve the current firmware version:

.. code-block:: lua

   local ver = esp32.ota()
   for k, v in pairs(ver) do
      trace(k, v)
   end

Begin an OTA upgrade:

.. code-block:: lua

   local ota, err = esp32.ota"begin"

OTA Object Methods
~~~~~~~~~~~~~~~~~~

``ota:write(data)``
   Writes a chunk of firmware data. Keep calling this method until the full
   image has been written.

   .. code-block:: lua

      local ok, err = ota:write(data)

``ota:commit()``
   Finalizes the upgrade. Returns ``true`` on success, otherwise ``nil, error``.

   .. code-block:: lua

      local ok, err = ota:commit()

``ota:abort()``
   Cancels the current OTA session.

   .. code-block:: lua

      ota:abort()

Practical Guidance
------------------

- Use ``apinfo`` and ``wscan`` during network bring-up and troubleshooting.
- Use ``netconnect`` for both first-time network configuration and automated
  reconnect logic.
- Use ``xedge.event("sntp", ...)`` before TLS-dependent application startup if
  accurate time matters.
- Use OTA only after verifying image integrity and transport reliability in your
  application workflow.
