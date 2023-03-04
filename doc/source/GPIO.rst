
gpio API
===========

This API provides access to the GPIO (General Purpose Input/Output) subsystem.

esp32.gpio(pin, mode, cfg)
-----------------------------------

The `gpio.init()` function initializes a GPIO pin and returns a `gpio` object.

:param pin: A valid GPIO pin number.
:param mode: The GPIO mode must be set to one of the following strings:

- ``IN``: input-only mode.
- ``OUT``: output-only mode.
- ``OUTOD``: output-only mode with open-drain.
- ``INOUTOD``: input/output mode with open-drain.
- ``INOUT``: input/output mode.

:param cfg: An optional table with the following options:

- ``pullup``: Enables a GPIO pull-up. Defaults to ``false``.
- ``pulldown``: Enables a GPIO pull-down. Defaults to ``false``.
- ``callback``: A callback function. Enables interrupt mode. The interrupt is controlled by the `type` key.
- ``type``:  The interrupt type can be set to one of the following strings:

  - ``POSEDGE``: interrupt on rising edge.
  - ``NEGEDGE``: interrupt on falling edge.
  - ``ANYEDGE``: interrupt on both rising and falling edges.

  Defaults to ``POSEDGE`` if a callback is provided; interrupts are disabled if no callback is provided.


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

