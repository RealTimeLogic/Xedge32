
PCNT API
==========

This API provides an interface to the ESP32's Pulse Counter (PCNT) functionality.

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
      - Signature: ``function(count)``


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

.. code-block:: lua

    local gpioA = 0
    local gpioB = 2

    pcnt = esp32.pcnt{
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
    pcnt:start()


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

.. code-block::

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
