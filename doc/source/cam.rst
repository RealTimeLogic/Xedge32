Camera API
==========

Xedge32 includes the ESP32 camera driver together with a Lua interface for
capturing images from supported camera modules. This API is typically used on
boards such as ESP32-CAM and ESP32-S3 camera variants where the camera wiring
is fixed by the board design.

The basic workflow is:

1. Create a camera object with ``esp32.cam(cfg)``.
2. Call ``cam:read()`` whenever you want a frame.
3. Call ``cam:close()`` when you are finished.

.. note::

   The camera API is still evolving, so this page focuses on the currently
   documented setup and configuration model.

There are two camera paths in the firmware:

- classic ESP32/ESP32-S3 camera support using the ESP-IDF camera driver and a
  parallel camera pin map, and
- ESP32-P4 camera support using the V4L2/PPA pipeline.

Use the section that matches your target board.

Creating a Camera Object
------------------------

Function signature:

.. code-block:: lua

   cam, err = esp32.cam(cfg)

Parameters
~~~~~~~~~~

``cfg``
   Configuration table describing the camera pin mapping and runtime options.

Return Values
~~~~~~~~~~~~~

- On success, returns a camera object.
- On failure, returns ``nil, error message``.

Camera Object Methods
---------------------

``cam:read()``
~~~~~~~~~~~~~~

Captures a new image. On success, it returns one Lua string containing the
frame. On a recoverable capture or processing failure, it returns ``nil`` and
an error string. The string encoding depends on the configured ``format``.

``cam:close()``
~~~~~~~~~~~~~~~

Closes the camera object and releases the associated hardware resources. An
explicit close returns ``true``. Do not call ``read`` again after closing the
object.

Configuration Table
-------------------

The configuration table maps the camera's parallel data bus and control signals
to GPIO pins on the ESP32. In most cases, you copy the mapping used by your
specific board design and only adjust optional settings when needed.

Required fields
~~~~~~~~~~~~~~~

- ``d0`` to ``d7``: Pixel data pins.
- ``xclk``: Camera master clock output.
- ``pclk``: Pixel clock input.
- ``vsync``: Vertical synchronization signal.
- ``href``: Horizontal reference signal.
- ``sda``: SCCB data line.
- ``scl``: SCCB clock line.
- ``reset``: Camera reset pin.
- ``freq``: XCLK frequency, typically between 10 MHz and 20 MHz.

Optional fields
~~~~~~~~~~~~~~~

- ``pwdn``: Camera power-down pin.
- ``frame``: Frame size, for example ``"HD"``.
- ``format``: Pixel format such as ``"RGB565"``, ``"YUV422"``,
  ``"GRAYSCALE"``, or ``"JPEG"``.
- ``vflip``: Boolean that flips the image vertically.
- ``hmirror``: Boolean that mirrors the image horizontally.

Configuration Examples
----------------------

.. code-block:: lua

   -- Seeed Studio XIAO ESP32S3 Sense
   local cfg = {
      d0 = 15, d1 = 17, d2 = 18, d3 = 16, d4 = 14, d5 = 12, d6 = 11, d7 = 48,
      xclk = 10, pclk = 13, vsync = 38, href = 47, sda = 40, scl = 39, pwdn = -1,
      reset = -1, freq = 20000000, frame = "HD"
   }

   -- Aideepen ESP32-CAM
   local cfg = {
      d0 = 5, d1 = 18, d2 = 19, d3 = 21, d4 = 36, d5 = 39, d6 = 34, d7 = 35,
      xclk = 0, pclk = 22, vsync = 25, href = 23, sda = 26, scl = 27, pwdn = 32,
      reset = -1, freq = 20000000, frame = "HD"
   }

   -- FREENOVE ESP32-S3 WROOM
   local cfg = {
      d0 = 11, d1 = 9, d2 = 8, d3 = 10, d4 = 12, d5 = 18, d6 = 17, d7 = 16,
      xclk = 15, pclk = 13, vsync = 6, href = 7, sda = 4, scl = 5, pwdn = -1,
      reset = -1, freq = 20000000, frame = "HD"
   }

ESP32-P4 Configuration
----------------------

On ESP32-P4 builds, ``esp32.cam(cfg)`` uses the ESP Video V4L2
capture path, the Pixel Processing Accelerator (PPA), and the hardware JPEG
encoder. The checked-in defaults target the OV5647 camera on the ESP32-P4
Function EV Board. Only one P4 camera object can own the camera hardware at a
time.

Common ESP32-P4 fields:

- ``scl``: Optional integer SCCB/I2C clock GPIO. Default is ``8``.
- ``sda``: Optional integer SCCB/I2C data GPIO. Default is ``7``.
- ``xclk``: Optional integer camera master-clock GPIO. Default is ``22``.
- ``format``: Optional string. Accepted values are ``"JPEG"``, ``"YUV422"``,
  and ``"RGB565"``. Default is ``"JPEG"``.
- ``width``: Optional even integer output width from ``2`` through ``1920``
  pixels. Default is ``640`` when ``frame`` is not used.
- ``height``: Optional even integer output height from ``2`` through ``1080``
  pixels. Default is ``480`` when ``frame`` is not used.
- ``frame``: Optional convenience-size string used only when neither ``width``
  nor ``height`` is supplied. Accepted values are ``"QVGA"`` (320 by 240),
  ``"VGA"`` (640 by 480), ``"SVGA"`` (800 by 600), and ``"HD"`` (1280 by
  720). Default is ``"VGA"``.
- ``quality``: Optional integer JPEG quality from ``1`` through ``100``.
  Default is ``80``. It is used only when ``format`` is ``"JPEG"``.
- ``vflip``: Optional Boolean. Set it to ``true`` to flip the output vertically.
  Default is ``false``.
- ``hmirror``: Optional Boolean. Set it to ``true`` to mirror the output
  horizontally. Default is ``false``.

The P4 ``read`` result has one of these layouts:

- ``JPEG`` returns a complete JPEG byte string.
- ``RGB565`` returns packed raw RGB565 data with two bytes per pixel.
- ``YUV422`` returns packed raw UYVY data with two bytes per pixel.

Example:

.. code-block:: lua

   local cam, err = esp32.cam{
      format = "JPEG",
      frame = "VGA",
      quality = 80,
      sda = 7,
      scl = 8,
      xclk = 22
   }

   if cam then
      local jpg, readErr = cam:read()
      if jpg then
         trace("JPEG bytes:", #jpg)
      else
         trace(readErr)
      end
      assert(cam:close())
   else
      trace(err)
   end

Practical Guidance
------------------

- Start with a known-good pin map for your exact board.
- If camera initialization fails, the first thing to verify is the board's pin
  mapping.
- If the image orientation is wrong, try ``vflip`` and ``hmirror`` before
  changing anything else.
- Lower clock rates can sometimes improve stability during bring-up.
- Camera support depends on the firmware build. If ``esp32.cam`` is not
  available, the current firmware was built without camera support.
- A successful P4 compile does not verify the sensor, companion ESP32-C6,
  Ethernet PHY, or board wiring. Test those paths on the intended board.
