GPIO API
========

The GPIO API provides access to the ESP32's general-purpose input and output
pins. This is the API you use to read buttons, drive LEDs, control enable pins,
and react to digital edge transitions.

The API supports both simple polling and interrupt-driven operation. For
interactive applications and responsive hardware handling, the callback-based
approach is usually the better choice.

For simple maker projects, GPIO is often the first API to try. Use it for
low-current logic signals, buttons, relays with proper driver circuits, and LED
experiments with a current-limiting resistor. Do not connect motors, LED strips,
or other high-current loads directly to an ESP32 GPIO pin.

Creating a GPIO Object
----------------------

Function signature:

.. code-block:: lua

   gpio = esp32.gpio(pin, mode [, cfg])

Parameters
~~~~~~~~~~

- ``pin``: GPIO pin number.
- ``mode``: One of the following strings:

  - ``"IN"``: Input only.
  - ``"OUT"``: Output only.
  - ``"OUTOD"``: Output only with open-drain behavior.
  - ``"INOUTOD"``: Input/output with open-drain behavior.
  - ``"INOUT"``: Input/output.

- ``cfg``: Optional configuration table.

Configuration options:

- ``pullup``: Enable the internal pull-up resistor. Default is ``false``.
- ``pulldown``: Enable the internal pull-down resistor. Default is ``false``.
- ``callback``: Lua callback function used for interrupt-driven input handling.
- ``type``: Interrupt trigger type when ``callback`` is set.

Supported interrupt types:

- ``"LOW"``: Trigger while the input level is low.
- ``"HIGH"``: Trigger while the input level is high.
- ``"POSEDGE"``: Trigger on rising edge.
- ``"NEGEDGE"``: Trigger on falling edge.
- ``"ANYEDGE"``: Trigger on both rising and falling edges.

If a callback is provided and ``type`` is omitted, the default interrupt type
is ``"POSEDGE"``.

Use edge triggers for buttons and pulse inputs when you want to react to a
transition. Use level triggers only when the application really needs to know
that a pin remains high or low, since a level condition can produce repeated
interrupt activity while the level remains active.

GPIO Object Methods
-------------------

``gpio:value([val])``
~~~~~~~~~~~~~~~~~~~~~

If ``val`` is provided, this method writes the pin level:

- ``true`` for high
- ``false`` for low

If ``val`` is omitted, the method returns the current GPIO level.

On boards where an LED is wired as active-low, ``false`` turns the LED on and
``true`` turns it off. Check the schematic or board documentation if the output
appears inverted.

``gpio:close()``
~~~~~~~~~~~~~~~~

Releases the GPIO object and frees the underlying hardware resources.

.. _GpioExamples:

Examples
--------

The following examples are designed to run well as Lua Server Pages (LSP), but
the same GPIO patterns also work in regular Lua application code.

Example 1: Set GPIO 18 High
~~~~~~~~~~~~~~~~~~~~~~~~~~~

This example opens GPIO 18 as an output, drives it high, waits two seconds, and
then automatically releases the GPIO object when it goes out of scope.

.. code-block:: lua

   local pin <close> = esp32.gpio(18, "OUT")
   pin:value(true)
   ba.sleep(2000)

The ``<close>`` syntax is useful when you want the resource to be released
automatically at the end of the current scope.

Example 2: Toggle an LED Through Garbage Collection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This example demonstrates an Xedge32 pattern that is especially convenient
during hot reloading. When the page is refreshed, the previous GPIO object
becomes unreachable and can be garbage-collected, which releases the pin.

.. code-block:: lua

   collectgarbage()
   ba.sleep(2000)
   local pin = esp32.gpio(18, "OUT")
   pin:value(true)

If an LED is connected to GPIO 18, it turns off briefly while the previous
object is collected, then turns on again when the new object is created.

Example 3: Read a Button State
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This example configures GPIO 15 as an input with a pull-down resistor and polls
the pin once per second for up to 30 seconds.

.. code-block:: lua

   local pin <close> = esp32.gpio(15, "IN", {pulldown = true})
   for i = 1, 30 do
      local val = pin:value()
      trace(i, val)
      if val then
         break
      end
      ba.sleep(1000)
   end

Example 4: React to a Button with an Interrupt Callback
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This example listens for both rising and falling edges on GPIO 15. It is a
better pattern than polling when you want fast response and less idle CPU work.

.. code-block:: lua

   local cfg = {
      pulldown = true,
      type = "ANYEDGE",
      callback = function(level)
         trace("level", level)
      end
   }

   trace(esp32.gpio(15, "IN", cfg))

Practical Guidance
------------------

- Use polling when you only need occasional state checks.
- Use callbacks for buttons, pulse inputs, and other event-driven signals.
- Confirm the board pinout before using a GPIO number. Some ESP32 pins are
  reserved for flash, PSRAM, USB, boot mode selection, camera, or SD card
  hardware.
- Prefer explicit cleanup with ``gpio:close()`` in long-running applications.
- In short-lived LSP pages, Lua's automatic cleanup can simplify iteration.
