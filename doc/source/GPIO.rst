
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


gpio Object Methods
--------------------

The `gpio` object has the following methods:

gpio:value([val])
~~~~~~~~~~~~~~~~~

Sets the value of the GPIO to `true` (high) or `false` (low) or returns the GPIO value if no argument is provided.

gpio:close()
~~~~~~~~~~~~

Releases the GPIO, freeing the resources associated with it. Use this method when you are finished using the GPIO.

.. _GpioExamples:

Examples
--------
The following examples are designed to be run as Lua Server Pages (LSP) pages.

Example 1: Setting GPIO 18 High
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example configures GPIO 18 for output mode, sets the GPIO high, waits for 2 seconds, then closes the GPIO. The GPIO is automatically closed since we use Lua's <close> syntax, which automatically closes the object when it goes out of scope.

.. code-block:: lua

   local pin <close> = esp32.gpio(18,"OUT")
   pin:value(true)
   ba.sleep(2000)

Example 2: Turning LED On and Off
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example performs a full garbage-collection cycle, sleeps for 2 seconds, then opens pin 18 for output mode, and sets the GPIO high. If you connect an LED to the pin you will see the LED turning on and stay on. When the page is refreshed, the garbage collector collects the previous instance, thus turning the LED off. We then sleep for two seconds before creating a new object. The effect is that the LED will be on until the page is refreshed, thereby turning off the LED for two seconds.

.. code-block:: lua

   collectgarbage()
   ba.sleep(2000)
   local pin = esp32.gpio(18,"OUT")
   pin:value(true)

Example 3: Reading Button State
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example configures pin 15 as input with pulldown set to true. The code then pulls the pin for a maximum of 30 seconds. If you connect a button to the pin and press the button, the loop exits before it has looped 30 times.

.. code-block:: lua

   local pin <close> = esp32.gpio(15,"IN",{pulldown=true})
   for i= 1,30 do
       local val = pin:value()
       trace(i,val)
       if val then break end
       ba.sleep(1000)
   end

Example 4: Using Interrupt Callback Function
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example builds on the previous example and installs an interrupt callback function that gets called at both rising and falling edges. The callback is called each time you click the button and also when you release the button. The callback is called as long as the GPIO object is not collected by Lua's garbage collector.

.. code-block:: lua

   local cfg={
      pulldown=true,
      type="ANYEDGE",
      callback=function(level)
         trace("level",level)
      end
   }
   trace(esp32.gpio(15,"IN", cfg))
