
gpio Module
===========

This module provides access to the GPIO (General Purpose Input/Output) subsystem.

gpio.MODE
---------

The `gpio.MODE` table defines the available modes for a GPIO pin.

**Keys:**

- ``IN``: input-only mode.
- ``OUT``: output-only mode.
- ``OUTOD``: output-only mode with open-drain.
- ``INOUTOD``: input/output mode with open-drain.
- ``INOUT``: input/output mode.

Use these keys to set the mode of a GPIO pin using the `gpio.init()` function.

gpio.TYPE
---------

The `gpio.TYPE` table specifies the interrupt type.

**Keys:**

- ``POSEDGE``: interrupt on rising edge.
- ``NEGEDGE``: interrupt on falling edge.
- ``ANYEDGE``: interrupt on both rising and falling edges.

Use these keys to set the interrupt type in the `gpio.init()` function.

gpio.init(pin, mode, cfg)
-----------------------------------

The `gpio.init()` function initializes a GPIO pin and returns a `gpio` object.

:param pin: A valid GPIO pin number.
:param mode: A key from the `gpio.MODE` table.
:param cfg: An optional table with the following options:

- ``type``: A key from the `gpio.TYPE` table. Defaults to ``POSEDGE`` if a callback is provided and is disabled if no callback is provided.
- ``pullup``: Enables a GPIO pull-up. Defaults to ``false``.
- ``pulldown``: Enables a GPIO pull-down. Defaults to ``false``.
- ``callback``: A callback function. Enables interrupt mode. The interrupt is controlled by the `type` key.


gpio object Methods
--------------------

The `gpio` object has the following methods:

gpio:set(value)
~~~~~~~~~~~~~~~

Sets the value of the GPIO to `true` (high) or `false` (low).

gpio:read()
~~~~~~~~~~~

Reads the current value of the GPIO and returns the value as a boolean.

gpio:close()
~~~~~~~~~~~~

Releases the GPIO, freeing the resources associated with it. Use this method when you are finished using the GPIO.

