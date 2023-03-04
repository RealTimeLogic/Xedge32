I2C API
========================

To use the I2C API, begin by creating an I2C object through a call to ``esp32.i2cmaster``,, where you specify the I2C port, SDA GPIO, SCL GPIO, and the desired I2C speed. Next, start the I2C communication sequence by calling ``i2cm:start``, followed by ``i2cm:address`` to specify the address of the I2C device you want to communicate with. You can then proceed to call ``i2cm:write`` and/or ``i2c:read`` as needed. Finally, end the sequence by calling ``i2cm:commit`` to commit the I2C transaction.


Creating an I2C object
----------------------

.. code-block:: lua

    i2cm=esp32.i2cmaster(port, pinSDA, pinSCL, speed)

Create an I2C object.

**Parameters:**

- ``port`` (``int``): I2C port number - e.g. 0
- ``pinSDA`` (``int``): the GPIO number used for the I2C Serial Data 
- ``pinSCL`` (``int``): the GPIO number used for the I2C Serial Clock
- ``speed`` (``int``): the clock speed

I2C object methods
------------------

``i2cm:start()``
    Start a new command sequence (new job).

``i2cm:address(addr, direction, [,ack])``
    Add an address message to the I2C job.

    **Parameters:**

    - ``addr`` (``int``): the slave’s I2C address
    - ``direction`` (``const``): ``i2cm.READ`` or ``i2cm.WRITE``
    - ``ack`` (``bool``, optional): the I2C ack, defaults to ``true``

``i2cm:write(data [,ack])``
    Add a write message to the I2C job.

    **Parameters:**

    - ``data`` (``string``): the data to write
    - ``ack`` (``bool``, optional): the I2C ack, defaults to ``true``

``i2cm:read(len [, acktype])``
    Read data from the I2C job.

    **Parameters:**

    - ``len`` (``int``): how much data to read
    - ``acktype`` (``const``, optional): is one of ``i2cm.ACK`` | ``i2cm.NACK`` | ``i2cm.LAST_NACK``, and defaults to ``i2cm.NACK``

``i2cm:commit([timeout])``
    Commit the job and wait for a response. The default time is 500ms. Method commit returns ``x,err``, where x is true for a successful write operation and the response data for a read operation.

Example
------------------

The following example shows the read and write functions in the `BME280 Lua Module <https://github.com/RealTimeLogic/LspAppMgr-ESP32/blob/master/Lua-Examples/bme280.lua>`_. Variable regAddr is the register to read in the BME280 chip.


.. code-block:: lua

    local function read(i2cm,regAddr,len)
       i2cm:start()
       i2cm:address(self.address, i2cm.WRITE)
       i2cm:write(regAddr)
       i2cm:start() -- Repeated Start Condition
       i2cm:address(self.address, i2cm.READ)
       i2cm:read(len)
       local x,err=i2cm:commit()
       if not x then trace("read failed",err) end
       return x,err
    end
    
    local function write(i2cm,regAddr,data)
       i2cm:start()
       i2cm:address(self.address, i2cm.WRITE)
       i2cm:write(regAddr)
       i2cm:write(data)
       local x,err=i2cm:commit()
       if not x then trace("write failed",err) end
       return x,err
    end
    
    local function bme280(port, address, sda, scl, settings)
       i2cm = esp32.i2cmaster(port, sda, scl, settings.speed or 100000)
       ......
    end
