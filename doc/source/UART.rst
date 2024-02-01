.. _uart-api:

UART API
========

This API provides a Lua API to the UART (Universal Asynchronous Receiver/Transmitter) subsystem `provided by esp-idf <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html>`_.

To use the API, begin by creating a UART object through a call to `esp32.uart`, where you specify configuration options such as the UART port number and GPIO pins. It is recommended to use the asynchronous (interrupt-driven) API by providing a callback instead of pulling for received data using `uart:read`.

The UART instance can operate in a semi-asynchronous mode. This mode enhances data handling efficiency by utilizing a callback for receiving RX data asynchronously and non-blocking mechanisms for sending data (TX) as long as the TX ring buffer can buffer the data being transmitted.

.. _uart-func:

esp32.uart()
----------------------------

.. code-block:: lua

  uart = esp32.uart(port [,config])

This function creates, configures, and returns a UART object.

``port`` - UART port number, e.g., 0.

``config`` - An optional table with the following options:

- ``callback`` - A callback function that enables interrupt mode.
- ``databits`` - Data bits, ranging from 5 to 8. The default is 8.
- ``baudrate`` - Default baud rate is 9600.
- ``rxbufsize`` - The default receive buffer size is 1024.
- ``txbufsize`` - The default transmit buffer size is 1024.
- ``txpin`` - UART TX pin GPIO number, which defaults to the default pin used by the UART port.
- ``rxpin`` - UART RX pin GPIO number, which defaults to the default pin used by the UART port.
- ``rtspin`` - UART RTS pin GPIO number, which is disabled by default.
- ``ctspin`` - UART CTS pin GPIO number, which is disabled by default.
- ``stopbits`` - Stop bits, which can be 1, 2, 1.5, or 2. You cannot set the number to 1.5, but any value other than 1 and 2 sets the stop bits to 1.5. The default is 1.
- ``parity`` - Parity, which can be either "EVEN" or "ODD". Disabled if not set.
- ``flowctrl`` - Flow control, which can be either "RTS", "CTS", or "CTSRTS". Disabled if not set.
- ``rs485`` - Enable RS-485 half duplex mode for use with an RS485 chip such as ADM483, where the RTS pin is used for driving the RS485 chip's collition detect. You need to set the ``rtspin`` for this to work.
- ``timeout`` - Default 0 (not used). When used without setting the ``pattern`` option, specifies how long to wait before triggering the callback. The option is defined in terms of UART character times. One character time is the time it takes to transmit one character (bit) on the UART bus, which is determined by the baud rate of the UART. For example, at 9600 baud, one character time is approximately 104 microseconds.
- ``pattern`` - Enable pattern detection. The pattern, which can be of any length, must be of the same type, e.g., "+++". A callback is required when setting this option.
    The following additional flags may be set when pattern is enabled. For details, see the C code function `enable_pattern_det_baud_intr() <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html?highlight=enable_pattern_det_baud_intr#_CPPv433uart_enable_pattern_det_baud_intr11uart_port_tc7uint8_tiii>`_.

    - ``maxlen`` - Default 0 (not used). Enable recording at most ``maxlen`` pattern positions.
    - ``timeout`` -  Default 0. Used to determine if a series of received characters can be considered as part of the same pattern. If the gap between two characters exceeds this timeout, the ongoing pattern detection is reset. This prevents false positives from scattered similar characters over a long duration.
    - ``postidle`` - Default 0. Defines the minimum duration of silence (no data received) after a pattern is detected. It's the idle period that must be observed following the last character of the pattern.
    - ``preidle`` - Default 0. Sets the minimum idle period before a pattern starts. It's the duration of silence that must be observed before the first character of the pattern is received.

The callback triggers when characters are received according to the pattern and timeout options. The callback function can receive the following arguments:

- ``function(data,pattern)`` - One or several characters packaged into the Lua string 'data'. The pattern argument is a boolean set to true if this is a pattern match.
- ``function(nil, emsg)`` - An error or special character received; ``emsg`` can be one of:

  - ``full`` - UART RX buffer full event; queue was reset (UART_BUFFER_FULL).
  - ``overflow`` - UART FIFO overflow event; queue was reset (UART_FIFO_OVF).
  - ``frame`` - UART RX frame error event (UART_FRAME_ERR).
  - ``parity`` - UART RX parity event (UART_PARITY_ERR).
  - ``databreak`` - UART TX data and break event (UART_DATA_BREAK).
  - ``break`` - UART break event (UART_BREAK).

UART Object Methods
-------------------

The UART object has the following methods:

uart:read([timeout])
~~~~~~~~~~~~~~~~~~~~~~~~

This method reads data from the RX queue and optionally waits for the specified ``timeout`` in milliseconds. Do not call this method if you have installed the RX callback.

uart:write(data)
~~~~~~~~~~~~~~~~~~~~

This method sends data to the UART. It will not return until all the data have been pushed into the TX FIFO, which means that it blocks if the FIFO is full. Use method uart:txsize() to find the free space size.

uart:txsize()
~~~~~~~~~~~~~~~~~~~~

Returns the TX ring buffer's free space.

uart:close()
~~~~~~~~~~~~~~~~

This method releases the UART and frees the resources associated with it. Use this method when you have finished using the UART.

UART Example
------------

The following UART echo example demonstrates how to open a UART object and configure it for asynchronous receive mode:

.. code-block:: lua

   local uart
   local cfg = {
       baudrate = 115200,
       txpin = 4,
       rxpin = 5,
       callback = function(data, err)
           if data then
               uart:write(data) -- Echo
           else
               trace("Err", err)
           end
       end
   }
   uart = esp32.uart(2, cfg)

In the example, a UART object is created with a baud rate of 115200 and GPIO pins 4 and 5 for the TX and RX pins, respectively. The object is also set to operate in asynchronous receive mode using a callback function. When data is received, the callback function sends the same data back to the UART, effectively creating an echo effect.

