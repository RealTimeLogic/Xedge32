.. _I2C API:

I2C API
=======

The I2C API lets Xedge32 communicate with sensors, displays, EEPROMs, and other
peripherals that use the I2C bus. It exposes a straightforward master-side API
that is well suited for both direct register access and small driver modules
written in Lua.

The typical workflow is:

1. Create an I2C master object.
2. Probe the device address if needed.
3. Read from or write to the target device.
4. Close the bus object when you are done.

I2C is a shared two-wire bus. Many sensors can use the same SDA and SCL pins as
long as each device has a unique address. Most small breakout boards already
include pull-up resistors, but bare sensors often do not. If the bus behaves
unreliably, check power, ground, address selection, and pull-ups before changing
the Lua code.

Creating an I2C Master
----------------------

Function signature:

.. code-block:: lua

   i2cm = esp32.i2cmaster(port, pinSDA, pinSCL, speed)

Parameters
~~~~~~~~~~

- ``port``: I2C controller number, for example ``0``.
- ``pinSDA``: GPIO pin used for the SDA line.
- ``pinSCL``: GPIO pin used for the SCL line.
- ``speed``: Bus speed in Hertz.

Use the 7-bit I2C address shown in the device data sheet, for example ``0x76``.
Do not left-shift the address; Xedge32 passes the address to the ESP-IDF driver
in normal 7-bit form.

Return Value
~~~~~~~~~~~~

The function returns an I2C master object.

I2C Master Object Methods
-------------------------

.. note::

   Some operations may block for up to the specified timeout. If predictable
   responsiveness matters, consider running longer I2C transactions in a
   separate thread or on a dedicated LSP page. See the `Lua thread library
   documentation
   <https://realtimelogic.com/ba/doc/en/lua/auxlua.html#thread_lib>`_.

``i2cm:probe(address [, timeout])``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Checks whether a device responds at the given address.

Parameters:

- ``address``: I2C device address.
- ``timeout``: Optional timeout in milliseconds. Default is ``500``.

Returns ``true`` if the device responds. Otherwise returns ``nil, error``.

``i2cm:read(address, len [, timeout])``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Reads raw data directly from the device without first selecting a register.

Parameters:

- ``address``: I2C device address.
- ``len``: Number of bytes to read.
- ``timeout``: Optional timeout in milliseconds. Default is ``500``.

Returns the data as a Lua string on success, otherwise ``nil, error``.

``i2cm:readfrom(address, registerOrBytes, len [, timeout])``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Reads data from a specific device register. This method performs the common
write-then-read transaction without issuing a stop condition between the two
phases, which results in a repeated start.

Parameters:

- ``address``: I2C device address.
- ``registerOrBytes``: Register selector to write before reading. This can be a
  single numeric byte such as ``0xF7`` or a Lua string when the device needs a
  multi-byte register address.
- ``len``: Number of bytes to read.
- ``timeout``: Optional timeout in milliseconds. Default is ``500``.

Returns the data as a Lua string on success, otherwise ``nil, error``.

``i2cm:write(address, data [, timeout])``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Writes data to the target device.

Parameters:

- ``address``: I2C device address.
- ``data``: Either a Lua string or a single byte value.
- ``timeout``: Optional timeout in milliseconds. Default is ``500``.

Returns ``true`` on success, otherwise ``nil, error``.

``i2cm:close()``
~~~~~~~~~~~~~~~~

Closes the I2C master object and releases the associated resources.

Returns ``true`` on success, otherwise ``nil, error``.

Example
-------

The example below demonstrates the same style of operations typically used in a
:ref:`BME280 Module` driver: probing the device, writing a configuration
register, reading from a specific register, and then performing a plain read.

.. code-block:: lua

   -- Initialize the I2C master
   local i2cm = esp32.i2cmaster(0, 21, 22, 400000)

   -- Probe the device at address 0x76
   local found = i2cm:probe(0x76)
   if found then
      print("Device found at address 0x76")
   else
      print("Device not found")
   end

   -- Write a value to a register
   i2cm:write(0x76, "\xF4\x27")

   -- Read multiple bytes from a specific register
   local data = i2cm:readfrom(0x76, 0xF7, 8)
   print("Data read from register:", data)

   -- Perform a simple read without specifying a register
   local simple_data = i2cm:read(0x76, 4)
   print("Simple data read:", simple_data)

   -- Close the I2C connection when done
   i2cm:close()

Simple Address Scanner
----------------------

If you do not know which address a sensor uses, scan the common 7-bit address
range:

.. code-block:: lua

   local i2cm <close> = esp32.i2cmaster(0, 21, 22, 100000)

   for address = 0x03, 0x77 do
      if i2cm:probe(address, 50) then
         trace(string.format("I2C device found at 0x%02X", address))
      end
   end

Practical Guidance
------------------

- Use ``probe`` during bring-up when you are not yet sure which address a
  device is using.
- Use ``readfrom`` for most register-based sensors.
- Use ``read`` for devices or protocols that stream data without a register
  pointer phase.
- If every address fails, verify that SDA and SCL are not swapped and that the
  sensor shares ground with the ESP32.
- If communication works at ``100000`` but not ``400000``, the bus wiring,
  pull-ups, or sensor may not support the faster speed reliably.
- Close the bus object when a transaction sequence is complete, especially in
  longer-running applications.
