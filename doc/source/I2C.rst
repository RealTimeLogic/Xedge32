I2C API
========================

The I2C API enables communication with I2C-enabled devices on your ESP32.

To use the I2C API, begin by creating an I2C master object with ``esp32.i2cmaster``, specifying the I2C port, SDA GPIO, SCL GPIO, and desired communication speed. This bus-based API allows for direct interactions with I2C devices using simplified methods.

**I2C Interactions:**

- **Device Check:** Use ``i2cm:probe`` to verify if an I2C device is connected at a specific address.

- **Simple Read:** For a simpler read operation that does not require a specific register, use  ``i2cm:read`` to retrieve data.

- **Read Data From Register:** Use  ``i2cm:readfrom`` to read from a specified register of the device without issuing a stop condition between the write and read operations.

- **Write Data:** Use  ``i2cm:write`` to send data directly to the I2C device.

- **Close Connection:** When finished, release the I2C bus by calling  ``i2cm:close``.


esp32.i2cmaster
----------------

Create an I2C master object.

.. code-block:: lua

   i2cm=esp32.i2cmaster(port, pinSDA, pinSCL, speed)


**Parameters:**

- **port** (``int``): I2C port number - e.g. 0
- **pinSDA** (``int``): the GPIO number used for the I2C Serial Data 
- **pinSCL** (``int``): the GPIO number used for the I2C Serial Clock
- **speed** (``int``): the clock speed

I2C Master Object Methods
--------------------------

**Note:** Some methods may block for up to the specified timeout. In real-time applications where responsiveness is critical, consider running these methods in a separate thread or on a dedicated LSP page, as described in the `Lua Thread Library documentation <https://realtimelogic.com/ba/doc/en/lua/auxlua.html#thread_lib>`_.


i2cm:probe(address, [timeout])
------------------------------

Probes a device at the specified address to verify if an I2C device is connected at this address.

**Parameters:**

- **address** (``int``): The I2C device address
- **timeout** (``int``, optional): Timeout duration in ms, defaults to 500ms

**Returns**:
``true`` if the device responds, otherwise ``nil``, ``error code``.

i2cm:read(address, len, [timeout])
----------------------------------
Read from Device: For a simpler read operation that does not require a specific register, use i2cm:read to retrieve data.

**Parameters:**

- **address** (``int``): I2C device address
- **len** (``int``): Number of bytes to read 
- **timeout** (``int``, optional): Timeout duration in ms, defaults to 500ms

**Returns**:
``x, err``: The data read as a Lua string if successful, or ``nil``, ``error code`` if the operation fails.


i2cm:readfrom(address, register, len, [timeout])
------------------------------------------------
Reads data from a specified register on the I2C device. This method does not send a stop condition between the write and read operations, ensuring a repeated start.

**Parameters:**

- **address** (``int``): I2C device address
- **register** (``int``): Register address to read from
- **len** (``int``): Number of bytes to read
- **timeout** (``int``, optional): Timeout duration in ms, defaults to 500ms

**Returns**:
``x, err``: The data read as a Lua string if successful, or ``nil``, ``error code`` if the operation fails.

i2cm:write(address, data, [timeout])
------------------------------------
Writes data to the specified I2C device.

**Parameters:**

- **address** (``int``): I2C device address
- **data** (``string`` or ``int``): Data to write, as a single byte or string
- **timeout** (``int``, optional): Timeout duration in ms, defaults to 500ms

**Returns**:
``true`` on success; otherwise, ``nil``, ``error code`` is returned.

i2cm:close()
------------
Closes the I2C connection and releases allocated resources for the device.

**Returns**:
``true`` on success; otherwise, ``nil``, ``error code`` is returned.


I2C Example
------------------

The following example shows the read and write functions in the :ref:`BME280 Lua Module <BME280 Module>`. Variable regAddr is the register to read in the BME280 chip.

.. code-block:: lua

	-- Initialize the I2C master
	local i2cm = esp32.i2cmaster(0, 21, 22, 400000)  -- Port 0, SDA on GPIO 21, SCL on GPIO 22, Speed 400kHz

	-- Probe the device at address 0x76
	local found = i2cm:probe(0x76)
	if found then
	    print("Device found at address 0x76")
	else
	    print("Device not found")
	end

	-- Write a value to a register
	i2cm:write(0x76, "\xF4\x27")  -- Write 0x27 to register 0xF4 at address 0x76

	-- Read multiple bytes from a specific register
	local data = i2cm:readfrom(0x76, 0xF7, 8)  -- Read 8 bytes from register 0xF7 at address 0x76
	print("Data read from register:", data)

	-- Perform a simple read from the device without specifying a register
	local simple_data = i2cm:read(0x76, 4)  -- Read 4 bytes directly from address 0x76
	print("Simple data read:", simple_data)

	-- Close the I2C connection when done
	i2cm:close()

