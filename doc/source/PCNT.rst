
PCNT API
==========

The Lua PCNT module interfaces with the `ESP-IDF PCNT <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html>`_ (Pulse Counter) functionality, designed for counting the rising and falling edges of an input signal on the ESP32.

Key Features
------------

- **16-bit Signed Counter**
  
  - Each pulse counter unit features a 16-bit signed counter register. The maximum frequency the counters can manage is 40 Mhz.

- **Two Configurable Channels**
  
  - There are two channels per unit, each configurable to increment or decrement the counter based on the input signal.

- **Signal and Control Inputs**
  
  - *Signal Input*: Accepts signal edges (rising or falling) for detection.
  - *Control Input*: Enables or disables the signal input dynamically.

- **Input Filters**
  
  - Optional filters are available for count and control inputs to eliminate unwanted signal glitches.

- **Watchpoints and Interrupts**
  
  - The pulse counters offer five watchpoints sharing a single interrupt.  The watchpoints include:

    - *Maximum/Minimum Count Value*: Triggers an interrupt when the counter reaches the set upper (positive number) or lower (negative number) limit, resetting the counter to 0.
    - *Two Threshold Values (Threshold 0 and 1)*: Triggers an interrupt when these preset values are reached while counting continues.
    - *Zero*: Triggers an interrupt when the counter value is zero, with counting continuing.

For a comprehensive understanding of the pulse counter hardware, refer to Chapter 17 of the `ESP32 Technical Reference Manual <https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf>`_.



esp32.pcnt
~~~~~~~~~~~~~~~~~~~~~~~~

Create a PCNT object.

.. code-block:: lua

   pcntm=esp32.pcnt(cfg)



Argument cfg is a table with the following fields:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- ``high``
  
  - The upper limit for the pulse counter.
  - Type: number

- ``low``
  
  - The lower limit for the pulse counter.
  - Type: number

- ``accumulator`` (optional)
  
  - Determines whether the counter should accumulate values or reset on reaching limits.
  - Type: boolean
  - Default: false

- ``glitch`` (optional) glitch filter
  
  - Sets a filter to ignore glitches (spurious pulses) on the input signal. Value is in nano seconds.
  - Type: number
  - Default: 0 (no filter)

- ``watch`` (optional)
  
  - Configuration for watch points and associated callback.
  - Type: table
  - Fields:

    - ``points``
      
      - A list of numbers where the counter will trigger a callback.
      - Type: array of numbers

    - ``callback``
      
      - The function to call when a watch point is reached. Receives the current count as an argument.
      - Type: function
      - Signature: ``function(count, crossmode)``
      - Arguments:

        - ``count`` Watch point value that triggered the event
        - ``crossmode`` Indicates how the PCNT unit crossed the last zero point. The possible zero cross modes are:
          
          - 0 Start from positive value, end to zero, i.e., +N -> 0
          - 1 Start from negative value, end to zero, i.e., -N -> 0
          - 2 Start from negative value, end to positive value, i.e., -N -> +M
          - 3 Start from positive value, end to negative value, i.e., +N -> -M

- ``channels``
  
  - An array of channel configurations.
  - Type: array of tables
  - Structure:

    - ``gpio``
      
      - GPIO configuration for the channel.
      - Type: table
      - Fields:

        - ``edge``
          
          - GPIO number for edge detection.
          - Type: number

        - ``level``
          
          - GPIO number for level detection.
          - Type: number

    - ``action``
      
      - Action configuration for the channel.
      - Type: table
      - Fields:

        - ``edge``
          
          - Actions for positive and negative edges.
          - Type: table
          - Fields:

            - ``positive``
              
              - Action on positive edge.
              - Type: string
              - Options: "HOLD", "INCREASE", "DECREASE"

            - ``negative``
              
              - Action on negative edge.
              - Type: string
              - Options: "HOLD", "INCREASE", "DECREASE"

        - ``level``
          
          - Actions for high and low levels.
          - Type: table
          - Fields:

            - ``high``
              
              - Action when the level is high.
              - Type: string
              - Options: "KEEP", "INVERSE", "HOLD"

            - ``low``
              
              - Action when the level is low.
              - Type: string
              - Options: "KEEP", "INVERSE", "HOLD"

Return Object
~~~~~~~~~~~~~~~~~~~~~~~~

The `esp32.pcnt` function returns an object with the following methods:

- ``start()``
  
  - Starts the pulse counter.

- ``stop()``
  
  - Stops the pulse counter.

- ``count()``
  
  - Returns the current count value.

- ``clear()``
  
  - Clears the counter.


Usage Example
~~~~~~~~~~~~~~~~~~~~~~~~

This Lua example is designed to mirror the functionality of the `Rotary Encoder C Code Example <https://github.com/espressif/esp-idf/tree/master/examples/peripherals/pcnt/rotary_encoder>`_.

.. code-block:: lua

    local gpioA = 0
    local gpioB = 2

    pcnt,err = esp32.pcnt{
        high = 100,
        low = -100,
        glitch=1000,
        watch = {
            points = {-100, -50, 0, 50, 100},
            callback = function(count)
                trace("Watch point reached with count: ", count)
            end
        },
        channels = {
            { -- Channel 1
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
            {  -- Channel 2
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
       timer=ba.timer(function() trace("Pulse count:",pcnt:count()) return true end)
       timer:set(1000)
    else
       trace(err)
    end


How to Use the Example
~~~~~~~~~~~~~~~~~~~~~~~~

This section provides an example of how to use the ESP32's Pulse Counter (PCNT) functionality with a rotary encoder.

Hardware Required
~~~~~~~~~~~~~~~~~~~~~~~~

- An ESP development board.
- An EC11 rotary encoder, or other encoders capable of producing quadrature waveforms.

Connection
~~~~~~~~~~~~~~~~~~~~~~~~

Connect the ESP development board and the rotary encoder as follows:

.. code-block:: text

    +--------+              +---------------------------------+
    |        |              |                                 |
    |      A +--------------+ GPIO 0 (internal pull-up)       |
    |        |              |                                 |
    +-------+|              |                                 |
    |     | |  GND +--------------+ GND                       |
    +-------+|              |                                 |
    |        |              |                                 |
    |      B +--------------+ GPIO 2 (internal pull-up)       |
    |        |              |                                 |
    +--------+              +---------------------------------+

In this setup:

- Connect pin A (CLK) of the rotary encoder to GPIO 0 on the ESP development board.
- Connect pin B (DT) of the rotary encoder to GPIO 2 on the ESP development board.
- Connect the GND pin of the rotary encoder to the GND pin on the ESP development board.

This configuration allows the ESP32 to read the quadrature waveforms generated by the rotary encoder through GPIO 0 and GPIO 2. In this example, each complete rotary step will result in PCNT counter increasing or decreasing by 4.
