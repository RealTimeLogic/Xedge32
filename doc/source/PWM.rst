PWM API
========================

The Lua PWM API interfaces with the esp-idf LED control (LEDC). This peripheral is primarily designed to control the intensity of LEDs, although it can also be used to generate PWM signals for other purposes, such as controlling servos. The PWM feature includes 16 channels that have the capability of generating independent waveforms. This feature is useful, for instance, in driving RGB LED devices. These channels are divided into two groups, with each group consisting of 8 channels. One of the groups is designed to operate in high-speed mode, which is implemented in hardware. This mode enables automatic and seamless changes to the PWM duty cycle. The other group of channels operate in low-speed mode. See the `esp-idf documentation <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html>`_ for details.

.. image:: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/_images/ledc-api-settings.jpg
   :alt: PWM Controller

To use the PWM, you need to perform the following steps:

#. Configure Timer by specifying the PWM signal's frequency and duty cycle resolution via ``esp32.pwmtimer()``.
#. Configure Channel by associating it with the timer and GPIO via ``esp32.pwmchannel()`` to output the PWM signal.
#. Change PWM Signal (via ``channel:duty()`` or ``channel:fade()``) that drives the output in order to change PWM duty-cycle. This can be done under the full control of software or with hardware fading functions, using Lua callbacks.

To use the API, begin by configuring a timer with a call to ``esp32.pwmtimer()``, where you can specify options such as the timer resolution in bits, which timer to use, and the frequency. Once a timer has been configured, you can open a channel object by calling the ``esp32.pwmchannel()`` function. For example, the code below configures timer 0, opens channel 0 connected to GPIO 18, and sets the initial duty cycle to 100%, turning the LED on fully. After two seconds, the duty cycle is set to 0%, turning off the LED. Note that calling the ``led:close()`` method also turns off the LED in addition to releasing all used resources. See Examples below for additional examples.

.. code-block:: lua

    local ok, err = esp32.pwmtimer{
        mode = "LOW",   -- low speed mode
        bits = 13,      -- duty resolution (bits)
        timer = 0,      -- timer number
        freq = 5000,    -- 5 kHz freq_hz
    }

    if ok then
        local led, err = esp32.pwmchannel{
            mode = "LOW",
            timer = 0,      -- must match above
            channel = 0,    -- Use channel 0
            gpio = 18,      -- Connect to GPIO 18
            duty = 2^13-1,  -- Set duty to 100%; 2^bits-1
        }
        if ok then
            ba.sleep(2000)  -- 2 seconds
            led:duty(0)     -- Set duty to 0%
            led:close()     -- Release
        end
    end


esp32.pwmtimer(config)
--------------------------

This function is used to configure a timer. It must be called prior to calling ``esp32.pwmchannel()`` for the timer used by the channel. This function should be called only once for a specific timer. It returns ``true`` on success and ``nil,err`` on error.

**Parameters:**
   - ``config``: a Lua table with the following required settings:
      - ``mode``: One of "LOW" or "HIGH". See the ESP-IDF documentation for more details.
      - ``bits``: The duty resolution in bits.
      - ``timer``: The timer number.
      - ``freq``: The frequency in Hertz.

esp32.pwmchannel(config)
----------------------------

This function is used to configure and open a channel object. It returns a channel object on success and ``nil,error`` on failure.

**Parameters:**
   - ``config``: a Lua table with the following settings:
      - Required: ``mode`` - must match the mode set with ``esp32.pwmtimer()``.
      - Required: ``timer`` - must match the mode set with ``esp32.pwmtimer()``.
      - Required: ``channel`` - the channel number to use.
      - Required: ``gpio`` - the GPIO where the PWM signal is output.
      - Optional: ``duty`` - a number between 0 and 2^bits -1, where ``bits`` is the value set with ``esp32.pwmtimer()``. The default value is zero.
      - Optional: ``hpoint`` - see the ESP-IDF documentation for more information on hpoint. The default value is zero.
      - Optional: ``callback`` - set a Lua callback function and enable hardware-supported fading by using the method ``channel:fade()``. See the examples below for how to use this feature.

Channel Object Methods
-----------------------

channel:duty(pwm-duty-cycle [, hpoint])
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This method is used to set the duty cycle.

Arguments:
   - ``pwm-duty-cycle``: Any number between 0 and 2^bits-1.
   - ``hpoint`` (optional): Change the hpoint value configured with ``esp32.pwmtimer()``.

channel:fade(pwm-duty-cycle, time)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


This method is used to use the hardware to fade from the current value to the target duty cycle. You must have installed the callback for this to work. See the two examples below for how to use the fade function.

**Parameters:**
   - ``pwm-duty-cycle``: The target duty cycle can be any number between 0 and 2^bits-1.
   - ``time``: The maximum time of the fading in milliseconds.

channel:close()
~~~~~~~~~~~~~~~~


This method is used to release the channel and free the resources associated with it. Use this method when you have finished using the channel.


Examples
--------

The following example demonstrates how to use interrupt mode by providing a callback and using the ``led:fade()`` method. After creating a channel object, the code calls the callback to activate the channel instance ``led``. The code is triggered when ``led:fade()`` is called. Upon completion of the fading, the callback is called via an interrupt, and the fading direction is inverted, repeating the cycle.


.. code-block:: lua

    -- Define constants
    local bits = 13                   -- duty resolution (bits)
    local maxPwmDuty = 2^bits - 1     -- maximum duty cycle value

    -- Configure timer for high-speed mode
    local ok, err = esp32.pwmtimer{
        mode = "HIGH",                 -- high speed mode
        bits = bits,                   -- duty resolution
        timer = 0,                     -- timer number
        freq = 5000,                   -- frequency of 5 kHz
    }

    if ok then
        local duty,led = 0,0
        local function callback()
           -- Callback function to toggle LED duty cycle
           trace("led callback triggered", duty)
           duty = duty == 0 and maxPwmDuty or 0
           led:fade(duty, 1000)   -- Fade LED to new duty cycle over 1 second
        end
        led, err = esp32.pwmchannel{
            callback = callback,
            mode = "HIGH",              -- high speed mode
            channel = 0,                -- use channel 0
            timer = 0,                  -- must match above
            gpio = 18,                  -- connect to GPIO 18
        }
        if led then
             callback()  -- Activate by calling the callback
        end
    end


The following example is similar to the above, but is designed to slowly turn a servo from 0 to 180 degrees and then back again. This cycle continues as long as the channel object is not garbage collected. If you put this example in an LSP page, no references to the channel object remain after running the page, so it may be collected and released. The two variables, ``minServoDuty`` and ``maxServoDuty``, were calculated by the ``calculatePwmDutyCycle()`` function in the `servo.lsp source code example <https://github.com/RealTimeLogic/LspAppMgr-ESP32/blob/master/Lua-Examples/servo.lsp>`_.

.. code-block:: lua

    -- Configure timer for 50 Hz frequency (20 ms used by servos)
    local ok, err = esp32.pwmtimer{
        mode = "LOW",                  -- low speed mode
        bits = 13,                     -- duty resolution (bits)
        timer = 0,                     -- timer number
        freq = 50,                     -- frequency of 50 Hz
    }

    if ok then
        local minServoDuty, maxServoDuty = 409, 819 -- duty cycle values for 0 and 180 degrees
        local duty = maxServoDuty                  -- set initial duty cycle to 180 degrees
        local servo
        local function callback()
            -- Callback function to toggle servo duty cycle between 0 and 180 degrees
            trace("servo callback triggered", duty)
            duty = duty == minServoDuty and maxServoDuty or minServoDuty
            servo:fade(duty, 3000)                  -- Fade servo to new duty cycle over 3 seconds
        end
        servo, err = esp32.pwmchannel{
            callback = callback,
            mode = "LOW",                          -- low speed mode
            channel = 0,                           -- use channel 0
            timer = 0,                             -- must match above
            gpio = 14,                             -- connect to GPIO 14 (servo control pin)
        }
        if servo then
            callback()                             -- initial callback to set servo to 180 degrees
        end
    end

