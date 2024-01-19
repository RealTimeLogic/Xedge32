RMT API
========

The RMT (Remote Control) Module
--------------------------------

The ESP32's RMT (Remote Control) module is a versatile and powerful feature initially designed for handling remote control signal encoding and decoding. Its primary use was for infrared communication, enabling devices to send and receive IR signals commonly used in remote controls.

However, the RMT module's capabilities extend far beyond its original purpose with its highly flexible design and precision in pulse generation and timing control. With the ability to generate and interpret digital signals with an accuracy as fine-grained as 12.5 nanoseconds, the RMT module has found extensive use in various digital communication scenarios.

Application Examples
-----------------------

The RMT module is not limited to infrared signals; it has a broad range of applications, including:

- **1-Wire Communication:** Often used in temperature sensors and other simple digital sensors, 1-wire communication can be implemented using the RMT module, allowing for precise timing and reliable data transfer.

- **Addressable LED Strips:** The precise timing control offered by the RMT module makes it ideal for controlling addressable LED strips, such as the popular WS2812B. Users can create complex lighting effects with accurate color representation and timing.

.. _rmt-symbol-layout:

RMT-Symbol Layout
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

In many protocols, including 1-wire, a stream of bits is transmitted by defining specific timing for signal pulses. For instance, a protocol may represent a '0' bit as a short high pulse (e.g., 0.4μs) followed by a long low pulse (e.g., 0.85μs). Conversely, a '1' bit might be represented by a long high pulse (e.g., 0.8μs) followed by a short low pulse (e.g., 0.45μs).

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
     {1, 400, 0, 850},
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

TX Example
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

The orchestration of the `play()` function with the RMT TX API's transmit callback creates an accurate rendition of the musical Score. The coroutine yields after sending each note, allowing the hardware to complete the transmission of the RMT-symbol representing the note. Once the transmission is complete and the callback function is invoked, the coroutine resumes, proceeding to the next note in the Score.


RMT RX API
-----------

esp32.rmtrx(cfg)
~~~~~~~~~~~~~~~~~

This function initializes and returns a new RMT RX instance for receiving RMT-symbols. The function requires a configuration table, cfg, with various options for configuring the RMT RX instance.

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

The RMT RX instance provides one method for activating the reception of RMT-symbols.

.. method:: rmtrx:receive(cfg)

   The function initiates a new receive job and then returns.

   - **Parameters:**
     - **cfg (table, required)**: A configuration table that includes required and optional settings.

   **Configuration Options (cfg):**

     - **min (required)**: Specifies the minimum valid pulse duration in nanoseconds for either high or low logic levels. Pulses shorter than this are considered glitches and ignored.
     - **max (required)**: Determines the maximum valid pulse duration for high or low logic levels. Pulses longer than this are treated as a Stop Signal, triggering an immediate receive-complete event.
     - **len (optional, default 512)**: Sets the length of the receive buffer in terms of RMT-symbols.
     - **defer (optional, boolean false)**: This parameter comes into play when an RX and TX instance are linked to the same GPIO pin number. 

           - **false**: RMT-symbol reception is activated immediately, causing transmitted symbols to be included in the received symbols.
           - **true**: RMT-symbol reception is deferred until the TX instance has transmitted all symbols.

.. method:: rmtrx:close()

   Closes and releases the RMT RX channel

RX Example: 1-Wire Reading Temperature
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The example below shows how to implement the 1-wire protocol for reading temperature from a DS18B20 sensor. To understand this example, you must have some understanding of the 1-Wire protocol. The 1-Wire protocol is a communication method designed for minimal wiring, typically involving just a single data wire plus ground. 

Communication begins with a 'bus reset', which entails pulling the data line low for at least 480 microseconds. This signals the connected sensors to initiate communication, and in response, these sensors pull the bus low for a brief period to indicate presence.

During data transmission, timing is critical: to send a 'bit 0', the line is held low for about 60 microseconds, while for a 'bit 1', it is held low for approximately 6 microseconds. These specific durations are crucial as they allow sensors on the bus to differentiate between the two binary states accurately, ensuring precise data communication.

In the decodeBytes function, the application of this timing principle is evident. This function receives an array of RMT-symbols and iterates through them, decoding each received bit. A bit is identified as binary 1 if the data line has been held low for less than 16 microseconds and as binary 0 if it is held low for longer. Each bit is then shifted into a byte using little-endian bit notation. Once a full byte is decoded, it is added to an array, which is then returned by the function.

The core of the implementation resides in the readTemp function, which encompasses the inner function tempThread. This internal function executes as a Lua coroutine, a feature that simplifies coding of the event-based nature of this example by making the code sequential. In this implementation, the coroutine actively manages 1-wire data transmission and then pauses, awaiting the RX event callback to reactivate it.

The coroutine enters a waiting state using coroutine.yield(). It remains in this state until the RX event callback invokes coroutine.resume(). Notice how the RMT-symbol argument from the RX event callback is passed directly to coroutine.resume(). This argument is then conveniently received by the coroutine when coroutine.yield() returns. This mechanism ensures a straightforward handover of event data to the sequential code.


.. code-block:: lua
  :linenos:

    local tInsert=table.insert
    local cResume,cYield=coroutine.resume,coroutine.yield
    
    local function decodeBytes(symbols)
       local mask,byte,t=1,0,{}
       for i,sym in ipairs(symbols) do
          -- sym[2] is the duration low level
          if sym[2] <= 15 then byte = byte | mask end
          mask = mask << 1
          if 256 == mask then
             tInsert(t,byte)
             mask,byte = 1,0
          end
       end
       return t
    end
    
    local function readTemp(gpio,callback)
       local coro
       local function tempThread()
          local txCfg={eot=1}
          local rx <close> = esp32.rmtrx{
             gpio=gpio,
             resolution=1000000,
             callback=function(symbols) cResume(coro,symbols) end
          }
          local tx <close> = esp32.rmttx({
             gpio=gpio,
             opendrain=true,
             resolution=1000000,
          },rx) -- Second arg. Link RX and TX
    
          local function busReset()
             rx:receive{min=2000,max=480*2*1000}
             tx:transmit(txCfg, { {0,480,1,70} })
             local symbols=cYield()
             if #symbols < 2 then
                callback(nil,"No sensors connected")
             end
             return #symbols < 2 -- true means failed
          end
    
          local function sendCommand(cmd)
             rx:receive{min=900,max=70*1000}
             tx:transmit(txCfg, {
                            {
                               false, -- least significant first
                               {0,60,1,2}, -- binary 0
                               {0,6,1,56}, -- binary 1
                               cmd
                            }
                         })
             return cYield()
          end
    
          local function readBytes(len)
             local t={}
             for i=1,len do tInsert(t,0xFF) end
             return sendCommand(t)
          end
    
          tx:enable()
          -- release the HW by sending a special RMT-symbol
          tx:transmit(txCfg, { {1,1,1,0} })
          if busReset() then return end -- failed
          -- Skip rom, Start temp measurement
          sendCommand{0xCC,0x44}
          ba.timer(function() cResume(coro) end):set(1000,true)
          cYield() -- wait for timer (temperature conversion to finish)
          if busReset() then return end -- failed
          -- Skip rom, read scratchpad
          sendCommand{0xCC,0xBE}
          local data=decodeBytes(readBytes(2))
          local raw = (data[2] << 8) + data[1]
          callback(raw * 0.0625)
       end
       coro = coroutine.create(tempThread)
       cResume(coro) -- Start
    end
    
    readTemp(1, function(temp,err) trace(temp,err) end)

The tempThread function initiates its process by creating RX and TX instances on the same GPIO port, which is required for 1-wire communication. When using 1-wire, a special HW reset is necessary to make the Esp32's RMT HW counters work correctly. This command is sent on line 64.

Within the tempThread coroutine, two 1-wire commands are transmitted: 0x44 to start the temperature reading and 0xBE to read the scratchpad - a register where the temperature sensor stores its measurements. Notice the one-second delay, managed by the timer, between these two commands, a required pause allowing the sensor to complete the temperature measurement. A long duration is required when powering the sensor using parasitic power mode.

Both commands commence with 0xCC, informing the sensor to bypass sensor addressing. This approach implies that only one 1-wire sensor can be connected to the bus.

When the code initiates TX, the RX callback is activated when the data has been transmitted. See method rmtrx:receive() and the "defer "option for details. The coroutine uses this event to resume from coroutine.yield(). However, the received data is generally not used, with the exception of the code on line 73. 

After issuing the 0x44 command to read the temperature, the sensor responds by sending data over the bus. However, each bit transfer must be initiated by the master, in this case, the ESP32. If you look at the readBytes function at line 56, you'll notice that it takes the number of bytes to receive, creates an array of 0xFF with this length, and calls sendCommand. Remember, sending a one bit involves pulling the bus low for a brief period, which in turn prompts the sensor to transmit each bit. If the sensor intends to send a zero bit, it maintains the bus in a low state for an extended duration. Consequently, the RX callback receives the symbols transmitted by the ESP32, but these are formatted by the sensor, resulting in the accurate reception of the temperature as a 16-bit value.

If you are familiar with Lua and its efficient, incremental garbage collector (GC), you might have identified a potential issue in the above code. The concern lies with the absence of explicit references, or 'anchors', for the coroutine. While an internal network of references exists, there's no persistent reference to the main object - the coroutine - during its execution. This omission leaves the coroutine vulnerable to being garbage-collected. In such a case, the callback reporting the temperature would never be triggered. While it's unlikely for the coroutine to be collected during testing, real-world applications require a more robust approach. Ensuring a reference to the coroutine is necessary to prevent its premature collection. The presented code serves as a base for designing a 1-wire protocol. In a fully developed implementation, the coroutine should be encapsulated within an instance of the 1-wire library. This encapsulation guarantees that the coroutine remains active and is not garbage-collected while in use, thereby ensuring the reliability and stability of the application.
