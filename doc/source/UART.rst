.. _uart-api:

UART API
========

The UART API provides access to the ESP32's Universal Asynchronous
Receiver/Transmitter hardware. Use it when you need serial communication with
sensors, modems, external MCUs, industrial adapters, or RS-232/RS-485
transceivers.

Xedge32 supports both polling and callback-driven receive handling. In most
cases, the callback-based approach is preferred because it is more responsive
and avoids constant polling from Lua.

The transmit side can also operate efficiently in a semi-asynchronous style as
long as the TX ring buffer has enough free space to accept the outgoing data.

.. important::

   ESP32 UART pins use 3.3 V logic. Do not connect them directly to classic
   RS-232 voltage levels. Use the correct level shifter, RS-232 transceiver, or
   RS-485 transceiver for the device you are connecting.

.. _uart-func:

Creating a UART Object
----------------------

Function signature:

.. code-block:: lua

   uart = esp32.uart(port [, config])

Parameters
~~~~~~~~~~

``port``
   UART port number, for example ``0``.

   Be careful with UART0 on boards where it is also used for flashing or the
   serial console. For external devices, UART1 or UART2 is often easier to
   dedicate to the project.

``config``
   Optional table with the following settings:

- ``callback``: Receive callback function. Setting this enables interrupt-driven
  RX handling.
- ``databits``: Data bits from ``5`` to ``8``. Default is ``8``.
- ``baudrate``: Baud rate. Default is ``9600``.
- ``rxbufsize``: Receive buffer size. Default is ``1024``.
- ``txbufsize``: Transmit buffer size. Default is ``1024``.
- ``txpin``: TX GPIO. Defaults to the port's default pin.
- ``rxpin``: RX GPIO. Defaults to the port's default pin.
- ``rtspin``: RTS GPIO. Disabled by default.
- ``ctspin``: CTS GPIO. Disabled by default.
- ``stopbits``: Stop-bit setting. Use ``1`` or ``2`` explicitly. Values other
  than ``1`` and ``2`` are interpreted as ``1.5``.
- ``parity``: Either ``"EVEN"`` or ``"ODD"``.
- ``flowctrl``: Either ``"RTS"``, ``"CTS"``, or ``"CTSRTS"``.
- ``rs485``: Enables RS-485 half-duplex mode. Requires ``rtspin`` when used
  with a suitable transceiver such as ADM483.
- ``timeout``: Default ``0``. Without ``pattern``, this defines how long to
  wait before triggering the callback, measured in UART character times.
- ``pattern``: Enables pattern detection, for example ``"+++"``. Requires a
  callback.

Additional options when ``pattern`` is enabled:

- ``maxlen``: Maximum number of recorded pattern positions. Default is ``0``.
- ``timeout``: Maximum gap allowed between characters in the same pattern.
- ``postidle``: Minimum idle time required after the pattern.
- ``preidle``: Minimum idle time required before the pattern.

For the lower-level hardware behavior behind pattern detection, see the ESP-IDF
function `uart_enable_pattern_det_baud_intr()
<https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html?highlight=enable_pattern_det_baud_intr#_CPPv433uart_enable_pattern_det_baud_intr11uart_port_tc7uint8_tiii>`_.

Receive Callback Signatures
---------------------------

When using a callback, Xedge32 calls one of the following forms:

``function(data, pattern)``
   ``data`` is a Lua string containing one or more received characters.
   ``pattern`` is a boolean set to ``true`` when the data corresponds to a
   detected pattern.

``function(nil, emsg)``
   Called for receive-side errors or special UART conditions.

Possible ``emsg`` values:

- ``"full"``: RX buffer full; queue reset.
- ``"overflow"``: RX FIFO overflow; queue reset.
- ``"frame"``: Framing error.
- ``"parity"``: Parity error.
- ``"databreak"``: Data-and-break event.
- ``"break"``: Break event.

UART Object Methods
-------------------

``uart:read([timeout])``
~~~~~~~~~~~~~~~~~~~~~~~~

Reads data from the RX queue and optionally waits up to ``timeout``
milliseconds. Do not combine this method with a receive callback on the same
object.

Use ``read`` for quick tests or request/response protocols where your code
knows when a reply should arrive. Use a callback when the device may send data
at any time.

``uart:write(data)``
~~~~~~~~~~~~~~~~~~~~

Writes data to the UART. The call blocks until the data has been pushed into
the TX FIFO or TX ring buffer. If the transmit side is full, the call waits.

``uart:txsize()``
~~~~~~~~~~~~~~~~~

Returns the remaining free space in the TX ring buffer. This is useful when you
want to decide how much data you can queue without blocking.

``uart:close()``
~~~~~~~~~~~~~~~~

Releases the UART object and its associated resources.

UART Echo Example
-----------------

The following example opens UART port 2, configures GPIO 4 and GPIO 5 as TX and
RX, and echoes all received data back to the sender.

.. code-block:: lua

   local uart
   local cfg = {
      baudrate = 115200,
      txpin = 4,
      rxpin = 5,
      callback = function(data, err)
         if data then
            uart:write(data)
         else
            trace("Err", err)
         end
      end
   }

   uart = esp32.uart(2, cfg)

This callback-driven pattern is a good default when you need responsive serial
input handling without writing your own polling loop.

Practical Guidance
------------------

- Prefer callbacks over ``uart:read()`` when the device should react to incoming
  serial data promptly.
- Use ``uart:txsize()`` if you are streaming large bursts and want to avoid
  unexpected blocking.
- Enable ``rs485`` only when your hardware design specifically requires
  half-duplex RS-485 control through RTS.
- Match baud rate, parity, stop bits, and wiring to the external device before
  troubleshooting higher-level protocol code.
