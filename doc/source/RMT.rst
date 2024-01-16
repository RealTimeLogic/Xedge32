RMT API
========

The RMT (Remote Control) Module
--------------------------------

The ESP32's RMT (Remote Control) module is a versatile and powerful feature initially designed for handling remote control signal encoding and decoding. Its primary use was for infrared communication, enabling devices to send and receive IR signals commonly used in remote controls.

However, the RMT module's capabilities extend far beyond its original purpose with its highly flexible design and precision in pulse generation and timing control. With the ability to generate and interpret digital signals with an accuracy as fine-grained as 12.5 nanoseconds, the RMT module has found extensive use in various digital communication scenarios.

Application Examples
-----------------------

The RMT module is not limited to infrared signals; it has a broad range of applications, including:

- **One-Wire Communication:** Often used in temperature sensors and other simple digital sensors, one-wire communication can be implemented using the RMT module, allowing for precise timing and reliable data transfer.

- **Addressable LED Strips:** The precise timing control offered by the RMT module makes it ideal for controlling addressable LED strips, such as the popular WS2812B. Users can create complex lighting effects with accurate color representation and timing.

.. _rmt-symbol-layout:

RMT Symbol Layout
------------------

The RMT hardware defines data according to its unique RMT-symbol pattern. An RMT-symbol encodes the duration and level of a digital signal pulse, which are critical for accurate signal generation and reception.

.. figure:: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/_images/packetdiag-3577851dc99f87d0ace9a8ff06179282fd6f9c71.png
   :align: center
   :alt: RMT-symbol layout

   The bit fields of an RMT-symbol as defined by the hardware.

As illustrated in the figure above, each RMT-symbol consists of the following components:

- **Duration Value (15 bits):** Represents the length of time the signal maintains a specific logic level. This duration is measured in RMT clock ticks.

- **Logic Level Value (1 bit):** Indicates the logic level of the signal, either high (`1`) or low (`0`) for the corresponding duration.

Each RMT-symbol is divided into two pairs of these values, allowing a single RMT-symbol to represent a sequence of high and low signals in a compact form.

Lua RMT-Symbol
----------------

An *RMT-symbol* encapsulates the timing information for a digital signal. It is a composite of four values: two for the signal's logic level and two for the duration of these levels. The frequency at which the RMT module is configured determines a tick's duration. For a frequency of 1MHz, each tick translates to one microsecond because there are 1 million microseconds (μs) in a second. As an example, a waveform that remains high for 350μs followed by a low period of 800μs can be represented by the following RMT-symbol:

.. code-block:: lua

    {
      1, 350, 0, 800  -- High for 350μs, then Low for 800μs
    }

The above represents a Lua table consisting of four elements in a sequence. This table is a simple array where each element is indexed numerically, starting with 1.

Lua RMT Byte Encoding
-----------------------

In many one-wire protocols, a stream of bits is transmitted by defining specific timing for signal pulses. For instance, a protocol may represent a '0' bit as a short high pulse (e.g., 0.4μs) followed by a long low pulse (e.g., 0.85μs). Conversely, a '1' bit might be represented by a long high pulse (e.g., 0.8μs) followed by a short low pulse (e.g., 0.45μs).

To implement this in Lua, create the following structure:

.. code-block:: lua

   {
     boolean msb, -- Determines the order of bit transmission;
                  -- if true, sends the most significant bit first.
     RMT-symbol for bit 0, -- Defines timing for transmitting a '0' bit.
     RMT-symbol for bit 1, -- Defines timing for transmitting a '1' bit.
     { array of byte values } -- Array of data to be transmitted.
   }

For example, to send data using a frequency of 100,000,000 Hz, the structure would look like this:

.. code-block:: lua

   {
     -- Send the most significant bit first.
     true,
     -- RMT-symbol for bit 0: high for 0.4μs, low for 0.85μs.
     {0, 400, 1, 850},
     -- RMT-symbol for bit 1: high for 0.8μs, low for 0.45μs.
     {1, 800, 0, 450},
     -- Data to send: three bytes (0x00, 0x55, 0xFF).
     { 0x00, 0x55, 0xFF }
   }


RMT TX API
-----------

esp32.rmttx(cfg [,rx])
~~~~~~~~~~~~~~~~~~~~~~~
   This function initializes and returns a new RMT TX (Remote Control Module Transmission) instance for transmitting signals. It requires a configuration table, `cfg`, with various options that configure the RMT instance.

   :param table cfg: Configuration options for the RMT transmitter.
   :param RMT-RX rx: An RX instance can be provided to create a bi-directional bus (e.g., 1-wire). For this to work, the GPIO pin must be the same for the RX and TX instances.
   :return: RMT TX instance. The instance is in a disabled state and must be enabled before being used.

   **Configuration Options (cfg)**

   - ``gpio`` (*required*): The GPIO pin number used for transmission.
   - ``resolution`` (*required*): Sets the resolution of the internal tick counter. The timing parameter of the RMT signal is calculated based on this tick.
   - ``mem`` (optional, default 64): Has a different meaning based on whether DMA is enabled or not. If DMA is enabled, this field controls the size of the internal DMA buffer. If DMA is not used, it controls the size of the dedicated memory block owned by the channel.
   - ``queue`` (optional, default 4): Sets the depth of the internal transaction queue. A deeper queue allows more transactions to be prepared in the backlog.
   - ``invert`` (optional, default false): Decides whether to invert the RMT signal before sending it to the GPIO pad.
   - ``DMA`` (optional, default false): Enables the DMA backend for the channel, offloading a significant workload from the CPU.
   - ``opendrain`` (optional, default false): Configures the GPIO pad in open-drain mode.
   - ``callback`` (optional): The function to be called when the transmission completes.

   **Optional Carrier Modulation Options**

   For applications requiring carrier modulation, the following additional parameters can be set:

   - ``dutycycle`` : Sets the carrier duty cycle.
   - ``frequency`` : Sets the carrier frequency in Hertz (Hz).
   - ``polaritylow`` : Determines the carrier polarity, i.e., on which level the carrier is applied.

TX Object Methods
~~~~~~~~~~~~~~~~~

The RMT TX instance provides several methods for managing the transmission channel and sending data.

.. method:: rmttx:enable()

   Prepares the channel for data transmission. This method must be called before any transmission occurs. It enables a specific interrupt and readies the hardware to dispatch transactions.

.. method:: rmttx:disable()

   Disables the RMT channel by turning off the associated interrupt and clearing any pending interrupts. This method should be called to stop any ongoing transmission, especially if the transmission is set to loop indefinitely.

.. method:: rmttx:transmit(cfg, symbols)

   Initiates the transmission of signals defined by RMT-symbols.

   :param table cfg: Configuration options for the transmission process.
   :param table symbols: An array of RMT-symbols or bytes to transmit.

   The `cfg` table may include the following options:

   - **loop** (optional, default 0): Sets the number of transmission loops. A value of -1 indicates an infinite loop, which will require `rmttx:disable()` to be called to stop the transmission.
   - **eot** (optional, default 0): Determines the output level when transmission is complete or stopped.

.. method:: rmttx:close()

   Closes and releases the RMT TX channel

   Example:

Example
~~~~~~~~~~~~~~~~~

The following Lua script shows how to use the RMT TX API to play a musical score, specifically Beethoven's "Ode to Joy". Each note in the score is represented by a frequency (in Hertz) and duration (in milliseconds), forming a simple melody. The score table below has been copied from the C code example `Musical Buzzer <https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/musical_buzzer>`_.

.. code-block:: lua


    local score = { -- Beethoven's Ode to joy
        {740, 400}, {740, 600}, {784, 400}, {880, 400},
        {880, 400}, {784, 400}, {740, 400}, {659, 400},
        {587, 400}, {587, 400}, {659, 400}, {740, 400},
        {740, 400}, {740, 200}, {659, 200}, {659, 800},
    
        {740, 400}, {740, 600}, {784, 400}, {880, 400},
        {880, 400}, {784, 400}, {740, 400}, {659, 400},
        {587, 400}, {587, 400}, {659, 400}, {740, 400},
        {659, 400}, {659, 200}, {587, 200}, {587, 800},
    
        {659, 400}, {659, 400}, {740, 400}, {587, 400},
        {659, 400}, {740, 200}, {784, 200}, {740, 400}, {587, 400},
        {659, 400}, {740, 200}, {784, 200}, {740, 400}, {659, 400},
        {587, 400}, {659, 400}, {440, 400}, {440, 400},
    
        {740, 400}, {740, 600}, {784, 400}, {880, 400},
        {880, 400}, {784, 400}, {740, 400}, {659, 400},
        {587, 400}, {587, 400}, {659, 400}, {740, 400},
        {659, 400}, {659, 200}, {587, 200}, {587, 800},
    }
    
    local resolution=1000000
    
    local function play(rmt)
       for _,note in ipairs(score) do
          local freq,duration = note[1],note[2]
          local symbolDuration=resolution/freq/2
          rmt:transmit({loop=duration*freq/1000},
                       {
                          {0,symbolDuration,1,symbolDuration}
                       })
          coroutine.yield()
       end
       rmt:close()
    end
    
    local coro=coroutine.create(play)
    
    local rmt,err=esp32.rmttx{
       gpio=0,
       resolution=resolution,
       callback=function() coroutine.resume(coro) end
    }
    if rmt then
       rmt:enable()
       coroutine.resume(coro,rmt)
    end
    
    function onunload()
       rmt:close()
    end

The `play()` function executes as a Lua coroutine. It plays through Beethoven's "Ode to Joy" by iterating over the `score` table. Each entry in this table is a tuple, comprising a frequency and a duration, which together define a musical note.

Key elements of the play() Function:

- **Note Representation:** Each tuple in the `score` table encapsulates two key aspects of a musical note:

  - The **frequency** determines the pitch of the note.
  - The **duration** specifies the length of time the note is played.

- **Coroutine Behavior:** The function operates as a coroutine, enabling it to pause (yield) its execution after transmitting each note. 

- **Synchronization with Transmit Callback:** After transmitting a note, the coroutine yields (temporarily halts its execution). It resumes only when the transmit callback function is triggered, signaling the completion of the note's playback. This mechanism ensures that each note is played for its full duration before moving on to the next one.

The orchestration of the `play()` function with the RMT TX API's transmit callback creates an accurate rendition of the musical Score. The coroutine yields after sending each note, allowing the hardware to complete the transmission of the RMT symbol representing the note. Once the transmission is complete and the callback function is invoked, the coroutine resumes, proceeding to the next note in the Score.


RMT RX API
-----------

esp32.rmtrx(cfg)
~~~~~~~~~~~~~~~~~

**Function Overview:**
``esp32.rmtrx(cfg)`` This function initializes and returns a new RMT RX instance for receiving RMT-symbols. The function requires a configuration table, cfg, with various options for configuring the RMT RX instance.

**Parameters:**
   :param table cfg: This parameter is a configuration table comprising various required and optional options.
   :return: RMT TX instance. 

**Configuration Options (cfg):**

1. **gpio (required)**: 
   - Specifies the GPIO pin number used for signal reception.

2. **resolution (required)**: 
   - Determines the resolution of the internal tick counter. The RMT signal's timing parameter is calculated based on this resolution.

3. **mem (optional, default: 64)**: 
   - Has a different meaning based on whether DMA is enabled or not. If DMA is enabled, this field controls the size of the internal DMA buffer. If DMA is not used, it controls the size of the dedicated memory block owned by the channel.

4. **invert (optional, default: false)**: 
   - When set to true, inverts the input signals prior to processing by the RMT receiver.

5. **DMA (optional, default: false)**: 
   - Activates the DMA backend for the channel, significantly reducing CPU workload.

6. **callback (required)**: 
   - Designates a function to be called upon the completion of reception.
   - **Function Structure**: 
   ``function callback(symbols, overflow)``

      - **symbols**: A list of RMT-symbols.
      - **overflow**: Indicates whether the receive buffer overflowed. Refer to the ``rmtrx:receive`` method for details on setting the receive buffer size.

RX Object Methods
~~~~~~~~~~~~~~~~~

The RMT RX instance provides one method for activating the reception of RMT symbols.

.. method:: rmtrx:receive(cfg [,defer])

   The function initiates a new receive job and then returns.

   - **Parameters:**
     - **cfg (table, required)**: A configuration table that includes required and optional settings.
     - **defer (boolean, optional)**: This parameter comes into play when an RX and TX instance are linked to the same GPIO pin number. 

           - **Default (true)**: RMT-symbol reception is deferred until the TX instance has transmitted all symbols.
           - **False**: RMT-symbol reception is activated immediately, causing transmitted symbols to be included in the received symbols.

   **Configuration Options (cfg):**

     - **min (required)**: Specifies the minimum valid pulse duration in nanoseconds for either high or low logic levels. Pulses shorter than this are considered glitches and ignored.
     - **max (required)**: Determines the maximum valid pulse duration for high or low logic levels. Pulses longer than this are treated as a Stop Signal, triggering an immediate receive-complete event.
     - **len (optional, default 512)**: Sets the length of the receive buffers in terms of RMT-symbols.

.. method:: rmtrx:close()

   Closes and releases the RMT RX channel