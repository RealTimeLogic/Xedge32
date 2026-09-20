PCNT API
========

The PCNT API exposes the ESP32 pulse counter hardware to Lua. Pulse counters
are useful when you need to count edges, track direction, or monitor quadrature
signals from devices such as rotary encoders, tachometers, flow sensors, and
other pulse-producing hardware.

Xedge32 mirrors the underlying ESP-IDF PCNT model closely enough that the
official `ESP-IDF PCNT documentation
<https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html>`_
is still a useful reference when you need hardware-level background.

Why Use PCNT
------------

The PCNT peripheral is much better suited than ordinary GPIO polling when you
need reliable counting at higher edge rates. It moves the edge handling into
hardware and lets Lua read the accumulated result or respond to watchpoints.

Key Features
------------

- **16-bit signed counter**
  Each PCNT unit contains a signed 16-bit counter register. The hardware can
  handle input frequencies up to 40 MHz.

- **Two configurable channels per unit**
  Each channel can be programmed to increment, decrement, or hold the counter
  based on edge and level conditions.

- **Separate signal and control inputs**
  The signal input is used for edge detection, while the control input modifies
  how those edges affect the counter.

- **Optional glitch filtering**
  A hardware filter can ignore very short pulses that are likely to be noise.

- **Watchpoints with callbacks**
  You can trigger Lua callbacks when the counter reaches selected values such as
  zero, thresholds, or high and low limits.

For a deeper hardware description, see Chapter 17 of the `ESP32 Technical
Reference Manual
<https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf>`_.

Creating a PCNT Object
----------------------

Function signature:

.. code-block:: lua

   pcnt, err = esp32.pcnt(cfg)

The ``cfg`` argument is a Lua table that describes the counter limits, optional
filters, optional watchpoints, and one or two channel definitions.

Top-Level Configuration Fields
------------------------------

``high``
   Upper limit for the pulse counter.

``low``
   Lower limit for the pulse counter.

``accumulator``
   Optional boolean. When ``true``, the driver accumulates values instead of
   resetting at the limits. Default is ``false``.

``glitch``
   Optional glitch-filter duration in nanoseconds. Default is ``0`` which means
   no filter.

``watch``
   Optional watchpoint configuration table.

``channels``
   Required array of channel definitions.

Watchpoint Configuration
------------------------

If ``watch`` is provided, it must contain:

``points``
   Array of counter values that should trigger a callback.

``callback``
   Function called when one of the watchpoints is reached.

Callback signature:

.. code-block:: lua

   function(count, crossmode)

Arguments:

- ``count``: The watchpoint value that triggered the callback.
- ``crossmode``: How the counter crossed zero most recently.

Possible zero-cross modes:

- ``0``: ``+N -> 0``
- ``1``: ``-N -> 0``
- ``2``: ``-N -> +M``
- ``3``: ``+N -> -M``

Channel Configuration
---------------------

Each entry in ``channels`` is a table with ``gpio`` and ``action`` sub-tables.

``gpio`` fields:

- ``edge``: GPIO used for edge detection.
- ``level``: GPIO used for control/level detection.

``action.edge`` fields:

- ``positive``: Action on positive edge.
- ``negative``: Action on negative edge.

Valid edge actions:

- ``"HOLD"``
- ``"INCREASE"``
- ``"DECREASE"``

``action.level`` fields:

- ``high``: Behavior when the control level is high.
- ``low``: Behavior when the control level is low.

Valid level actions:

- ``"KEEP"``
- ``"INVERSE"``
- ``"HOLD"``

PCNT Object Methods
-------------------

The object returned by ``esp32.pcnt()`` provides the following methods:

``pcnt:start()``
   Starts counting.

``pcnt:stop()``
   Stops counting.

``pcnt:count()``
   Returns the current counter value.

``pcnt:clear()``
   Clears the current counter value.

Rotary Encoder Example
----------------------

The following example mirrors the functionality of Espressif's `rotary encoder
PCNT example
<https://github.com/espressif/esp-idf/tree/master/examples/peripherals/pcnt/rotary_encoder>`_.

The example assumes the encoder signals are already pulled to a valid logic
level. Many rotary encoder modules include pull-up resistors. If you use a bare
mechanical encoder, add suitable pull-ups or pull-downs for your board.

.. code-block:: lua

   local gpioA = 0
   local gpioB = 2

   local pcnt, err = esp32.pcnt{
      high = 100,
      low = -100,
      glitch = 1000,
      watch = {
         points = {-100, -50, 0, 50, 100},
         callback = function(count)
            trace("Watch point reached with count:", count)
         end
      },
      channels = {
         {
            gpio = {
               edge = gpioA,
               level = gpioB
            },
            action = {
               edge = {
                  positive = "DECREASE",
                  negative = "INCREASE"
               },
               level = {
                  high = "KEEP",
                  low = "INVERSE"
               }
            }
         },
         {
            gpio = {
               edge = gpioB,
               level = gpioA
            },
            action = {
               edge = {
                  positive = "INCREASE",
                  negative = "DECREASE"
               },
               level = {
                  high = "KEEP",
                  low = "INVERSE"
               }
            }
         }
      }
   }

   if pcnt then
      pcnt:start()
      local timer = ba.timer(function()
         trace("Pulse count:", pcnt:count())
         return true
      end)
      timer:set(1000)
   else
      trace(err)
   end

How the Example Works
---------------------

This configuration is designed for a quadrature rotary encoder:

- Channel 1 looks at ``gpioA`` as the edge source and ``gpioB`` as the control
  level.
- Channel 2 swaps the two signals.
- The level configuration inverts counting direction depending on the state of
  the other encoder line.

That is what allows the counter to move up or down as the encoder is rotated in
different directions.

Hardware Setup
--------------

The example assumes:

- an ESP development board, and
- an EC11 rotary encoder or another encoder that outputs quadrature waveforms.

Wiring Example
~~~~~~~~~~~~~~

Connect the encoder as follows:

.. code-block:: text

   +--------+              +---------------------------------+
   |        |              |                                 |
   |      A +--------------+ GPIO 0                         |
   |        |              |                                 |
   +-------+|              |                                 |
   |     | |  GND +--------------+ GND                       |
   +-------+|              |                                 |
   |        |              |                                 |
   |      B +--------------+ GPIO 2                         |
   |        |              |                                 |
   +--------+              +---------------------------------+

In this setup:

- encoder pin **A (CLK)** connects to GPIO 0,
- encoder pin **B (DT)** connects to GPIO 2, and
- encoder **GND** connects to board **GND**.

With the example configuration above, one full rotary detent typically changes
the PCNT counter by four counts because the hardware sees four signal edges per
quadrature cycle.

The PCNT API configures the pulse counter peripheral. It does not, by itself,
turn on GPIO pull-up or pull-down resistors. Treat signal conditioning as part
of the hardware setup.

Practical Guidance
------------------

- Use the glitch filter when mechanical switches or noisy signals cause false
  counts.
- Use watchpoints when you want low-overhead notifications at important count
  thresholds.
- For rotary encoders, expect to do some board-specific tuning of pull-ups,
  filter values, and direction mapping.
