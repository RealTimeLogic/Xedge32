ADC API
=============


The Analog to Digital Converter (ADC) API allows you to interface with the ESP32's on-chip ADC sensor, which can measure analog signals from dedicated analog IO pads. The Lua API internally uses the OneShot Mode and Continuous Mode provided by the ESP-IDF.

OneShot Mode is a simpler approach, where you can directly poll for data. Continuous Mode, on the other hand, employs interrupts and allows Lua to sample data at a rate of up to 48 kHz.

OneShot Mode
-------------

To use the OneShot Mode, you can call the following function:

::

    esp32.adc(unit, channel [, cfg])

- ``unit``: The ESP32 has two ADC units, represented by numbers 1 and 2.
- ``channel``: The specific ADC channel you want to use.
- ``cfg``: (Optional) A configuration table with the following options:
    * ``attenuation``: (String) One of "0db" | "2.5db" | "6db" | "11db". The default value is "11db".
    * ``volt``: (Boolean) If set to true, the API will return the raw ADC data and the data converted to millivolts.
    * ``bitwidth``: (Number) A value between 9 and 13, where the default is 13.

The ``esp32.adc()`` function returns an ADC object (``adc``) and the GPIO pin number used by the specified channel. The ADC object has the following methods:

- ``adc:read()``: Returns the raw ADC data and, if the 'volt' option is set in the configuration table, the raw data converted to millivolts.
- ``adc:close()``: Closes the ADC object and releases the resources.

**Example**

This example demonstrates how to open ADC unit 1, channel 0, and poll for data every second using a timer. The ADC object is configured to return both raw data and millivolts.

::

    local adc, pin = esp32.adc(1, 0, {volt = true})
    if adc then
       -- Prints 36, the VP pin
       trace("ADC using GPIO pin", pin)
       local function pollADC()
          local raw, volt = adc:read()
          trace(string.format("ADC raw data: %d, volt: %d", raw, volt))
          return true -- Continue timer
       end
       ba.timer(pollADC):set(1000)
    else
       trace("Failed", pin) -- Pin now represents the error code
    end

Continuous Mode
----------------

.. note:: This is an experimental API.

The Continuous Mode uses interrupts to sample the ADC at a high frequency. This data is then provided to Lua via a callback. However, Lua cannot accept a callback being called 48,000 times a second, which is the maximum supported sample rate. Instead, filters provide the data in a way Lua can handle it. The Lua callback can be called up to a hundred times per second. Note that the minimum sampling rate for an ESP32 is 20,000; thus, the Continuous Mode puts some strain on your ESP32.

To use the Continuous Mode, call the following function:

::

    esp32.adc(unit, channel, cfg)

When using Continuous Mode, the following additional options must be set in the configuration table 'cfg':

- ``callback``: (Function) A Lua callback that is called each time a sample block 'bl' is ready. This function will at most be called 100 times per second.
- ``fs``: (Number) The frequency sample rate is how fast the ADC samples in Hz. For example, setting this value to 20000 provides 20,000 samples per second. This value will be auto-adjusted if outside the acceptable range.
- ``bl``: (Number) Sample block length is how many samples should be collected before calling the Lua callback. This value, together with 'fs', controls how many times per second the Lua callback is called and can be calculated as fs/bl. Note that bl will be auto-adjusted such that the following is true: fs/bl >= 100.
- ``filter``: (String) One of "data" or "mean". These are the two filters currently provided.

The 'data' filter provides all data collected during a sample 'bl' as a Lua string with 16-bit values in little-endian format. This format is referred to as Pulse Code Modulation (PCM).

The 'mean' filter provides a mean value of the sampled values.

**Callback with 'data' filter**

::

    function mycallback(data, err)
    
- ``data``: A PCM-encoded binary object encoded as a Lua string.
- ``err``: Set if data is nil. The error "overflow" should be expected once in a while.

**Callback with 'mean' filter**

::

    function mycallback(raw, volt)
    
- ``raw``: The raw ADC value.
- ``volt``: Set if the configuration table included the 'volt' option. Note that the second argument is the error code if 'raw' is nil.

**Example: Continuous Mode with 'mean' filter**

This example shows how to use the continuous mode with the 'mean' filter. We set fs and bl to zero, letting them be auto-adjusted. On an ESP32, fs will be set to 20000, and bl will be set to 200, which causes the callback to be called at the highest rate. We only print every 100th value in the callback to limit the amount of data being printed. You should see a printout every second.

::

    local cnt = 0

    local function callback(raw, volt)
        if cnt % 100 == 0 then
            trace(raw, volt) -- This can print nil,emsg
        end
        cnt = cnt + 1
    end

    local adc, pin = esp32.adc(1, 0, {
                                volt = true,
                                bl = 0, -- auto-adjust
                                callback = callback,
                                fs = 0,  -- auto-adjust
                                filter = "mean"
                            })

**Example: Continuous Mode with 'data' filter**

This example shows how to use the continuous mode with the 'data' filter. We set fs to the highest possible value and let bl be auto-adjusted. The callback prints the length of the binary data, which in this case is 960. If you do the math, you will find that bl will be auto-adjusted to 480. The data is twice this size since the data contains 16-bit PCM-encoded data.

::

    local cnt = 0

    local function callback(data, err)
        if data then
            if cnt % 100 == 0 then
                trace(#data)
            end
            cnt = cnt + 1
        else
            print(err)
        end
    end

    local adc, pin = esp32.adc(1, 0, {
                                volt = true,
                                bl = 0, -- auto-adjust
                                callback = callback,
                                fs = 48000,
                                filter = "data"
                            })
