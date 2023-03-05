UART API
========

This API provides a Lua API to the UART (Universal Asynchronous Receiver/Transmitter) subsystem `provided by esp-idf <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html>`_.

To use the API, begin by creating a UART object through a call to `esp32.uart`, where you specify configuration options such as the UART port number and GPIO pins. It is recommended to use the asynchronous (interrupt-driven) API by providing a callback instead of pulling for received data using `uart:read`.

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
- ``pattern`` - Enable pattern detection. The pattern, which can be of any length, must be of the same type, e.g., "+++".
- ``maxlen`` - Enable recording at most ``maxlen`` pattern positions.
- ``timeout`` - ``chr_tout``. This, along with the following two settings, is sent to the C function `enable_pattern_det_baud_intr() <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html?highlight=enable_pattern_det_baud_intr#_CPPv433uart_enable_pattern_det_baud_intr11uart_port_tc7uint8_tiii>`_ if the pattern is enabled.
- ``preidle`` - ``pre_idle``.
- ``postidle`` - ``post_idle``.

The callback function can receive the following arguments:

- ``function(data)`` - One or several characters packaged into the Lua string 'data'.
- ``function(data, pattern)`` - If the pattern is enabled and a pattern is triggered.
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


This method reads data from the RX queue and optionally waits for the specified ``timeout`` in milliseconds.

uart:write(data)
~~~~~~~~~~~~~~~~~~~~

This method sends data to the UART. It will not return until all the data have been pushed into the TX FIFO, which means that it may block if the FIFO is full.

uart:close()
~~~~~~~~~~~~~~~~

This method releases the UART and frees the resources associated with it. Use this method when you have finished using the UART.

Example
-------

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

