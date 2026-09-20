ADC API
=======

The Analog-to-Digital Converter (ADC) API lets Lua code measure analog voltages
using the ESP32's built-in ADC hardware. This is the API you use when reading
potentiometers, light sensors, simple analog outputs from other circuits, and
similar low-bandwidth analog signals.

Xedge32 exposes two ADC usage models:

- **One-shot mode**, which is easy to use and well suited for occasional
  polling.
- **Continuous mode**, which samples in the background and delivers data through
  a callback.

For most applications, start with one-shot mode. Move to continuous mode only
when you need steady high-rate sampling.

.. important::

   The ESP32 ADC input is not tolerant of arbitrary voltages. Keep the signal
   within the safe input range for your exact board and attenuation setting.
   Use a resistor divider or sensor module output that is designed for 3.3 V
   logic when measuring higher voltages.

ADC readings are useful for sensors and control inputs, but they are not a
precision instrument. Expect noise, board-to-board variation, and some
non-linearity. For stable readings, average multiple samples or use continuous
mode with the ``mean`` filter.

One-Shot Mode
-------------

Function signature:

.. code-block:: lua

   adc, pinOrErr = esp32.adc(unit, channel [, cfg])

Parameters
~~~~~~~~~~

- ``unit``: ADC unit number. ESP32 devices provide ADC unit ``1`` and ``2``.
- ``channel``: ADC channel number within the selected unit.
- ``cfg``: Optional configuration table.

Supported configuration options:

- ``attenuation``: One of ``"0db"``, ``"2.5db"``, ``"6db"``, or ``"11db"``.
  The default is ``"11db"``.
- ``volt``: Boolean. When ``true``, ``adc:read()`` returns both the raw ADC
  value and the value converted to millivolts.
- ``bitwidth``: Number from ``9`` to ``13``. The default is ``13``.

Higher attenuation settings allow a larger input range, but they do not make
the ADC pin safe for voltages above the ESP32's limits. Always check the board
data sheet.

Return Values
~~~~~~~~~~~~~

On success, ``esp32.adc()`` returns:

- an ADC object, and
- the GPIO pin number used by the selected channel.

On failure, the first return value is ``nil`` and the second value contains an
error code or diagnostic string.

ADC Object Methods
~~~~~~~~~~~~~~~~~~

``adc:read()``
   Returns the current sample. If ``volt=true`` was set, the method returns both
   the raw value and the converted millivolt value.

``adc:close()``
   Releases the ADC resources associated with the object.

One-Shot Example
~~~~~~~~~~~~~~~~

The example below opens ADC unit 1, channel 0, and polls it once per second.
The configuration requests both the raw reading and the converted millivolt
value.

.. code-block:: lua

   local adc, pin = esp32.adc(1, 0, {volt = true})
   if adc then
      trace("ADC is using GPIO pin", pin)

      local function pollADC()
         local raw, volt = adc:read()
         trace(string.format("ADC raw data: %d, volt: %d", raw, volt))
         return true
      end

      ba.timer(pollADC):set(1000)
   else
      trace("Failed to open ADC", pin)
   end

Continuous Mode
---------------

.. note::

   Continuous mode is currently experimental.

Continuous mode is designed for applications that need repeated sampling at a
much higher rate than typical timer-based polling. The ESP32 samples in the
background and Lua receives processed results through a callback.

Because Lua cannot handle a callback for every single hardware sample,
continuous mode uses a buffering and filtering layer. Internally, the ADC may
sample at rates as high as ``48 kHz``, but the Lua callback is limited to a much
lower rate that is practical for the runtime.

On ESP32 hardware, the effective sampling rate floor is around ``20,000`` Hz.
Keep that in mind when deciding whether continuous mode is really necessary for
your application.

Function signature:

.. code-block:: lua

   adc, pinOrErr = esp32.adc(unit, channel, cfg)

Required continuous-mode options
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When using continuous mode, set these options in ``cfg``:

- ``callback``: Lua function called when a processed sample block is ready.
- ``fs``: Sampling frequency in Hz. The runtime adjusts values outside the
  supported range.
- ``bl``: Block length. This controls how many samples are collected before the
  callback is invoked. Together with ``fs``, this determines the callback rate.
- ``filter``: One of ``"data"`` or ``"mean"``.

Filters
~~~~~~~

``"data"``
   Returns the sampled block as a Lua string containing 16-bit little-endian
   PCM values.

``"mean"``
   Returns a single averaged value representing the current block of samples.

Callback Signatures
~~~~~~~~~~~~~~~~~~~

When using ``filter = "data"``:

.. code-block:: lua

   function mycallback(data, err)

- ``data``: A PCM-encoded Lua string.
- ``err``: An error string when ``data`` is ``nil``. Occasional ``"overflow"``
  events can happen under load.

When using ``filter = "mean"``:

.. code-block:: lua

   function mycallback(raw, voltOrErr)

- ``raw``: The averaged raw ADC value.
- ``voltOrErr``: The converted millivolt value when ``volt=true`` was enabled.
  If ``raw`` is ``nil``, this argument contains the error.

Continuous Example: ``mean`` Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This example lets the runtime auto-adjust both ``fs`` and ``bl`` by setting
them to zero. On ESP32 hardware, the values typically settle at ``20000`` and
``200`` respectively, which results in a fast but still manageable callback
rate.

.. code-block:: lua

   local cnt = 0

   local function callback(raw, volt)
      if cnt % 100 == 0 then
         trace(raw, volt)
      end
      cnt = cnt + 1
   end

   local adc, pin = esp32.adc(1, 0, {
      volt = true,
      bl = 0,
      callback = callback,
      fs = 0,
      filter = "mean"
   })

Continuous Example: ``data`` Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This example asks for the highest sample rate and lets the block length be
auto-adjusted. The callback prints the size of the returned binary block so you
can verify that data is flowing.

.. code-block:: lua

   local cnt = 0

   local function callback(data, err)
      if data then
         if cnt % 100 == 0 then
            trace(#data)
         end
         cnt = cnt + 1
      else
         trace(err)
      end
   end

   local adc, pin = esp32.adc(1, 0, {
      volt = true,
      bl = 0,
      callback = callback,
      fs = 48000,
      filter = "data"
   })

Practical Guidance
------------------

- Use one-shot mode when you only need occasional readings.
- Use continuous mode when timing matters more than simplicity.
- Prefer the ``mean`` filter when you want a stable interpreted value.
- Prefer the ``data`` filter when you want to process raw sampled blocks in Lua
  or pass them into another decoding step.
- For slowly changing sensors such as light sensors, potentiometers, and soil
  moisture modules, a timer plus one-shot reads is usually easier to maintain
  than continuous mode.
