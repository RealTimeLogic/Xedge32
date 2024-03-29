Camera API
===========

Xedge32 features the ESP32 Camera Driver and a Lua interface to this driver. To use the camera API, follow these steps:

**Note that the cam documentation is not yet complete!!!**

1. Create a camera object by invoking ``esp32.cam``, passing a configuration table as an argument.
2. Call the ``cam:read`` method as many times as needed to fetch new images.
3. Once done, call the ``cam:close`` method to close the camera object and free up resources.

The API functions and methods are detailed below:

``esp32.cam(cfg)``
------------------

This function creates a camera object.

- **cfg**: A configuration table, whose structure is detailed below.

The function returns the camera object if successful. If the creation of the camera object fails, it returns ``nil``, ``error message``.

The camera object provides the following methods:

``cam:read()``
--------------

This method is used to fetch a new image from the camera.

``cam:close()``
---------------

This method closes the camera object, freeing up any resources it was using.

The Configuration Table
-------------------------

The argument accepted by esp32.cam is a Lua table that maps various parameters to specific GPIO pins on the ESP32 board. These settings are used to initialize the camera module on the ESP32-CAM board.

Required parameters:
~~~~~~~~~~~~~~~~~~~~~

- **d0 to d7**: These are the GPIO pins that the camera module's pixel data lines (D0-D7) are connected to. The camera module sends digital pixel data to the ESP32 through these lines. 
- **xclk**: This is the GPIO pin that the camera clock (XCLK) is connected to. 
- **pclk**: This is the GPIO pin that the pixel clock (PCLK) is connected to. 
- **vsync**: This is the GPIO pin that the vertical sync (VSYNC) is connected to. 
- **href**: This is the GPIO pin that the horizontal reference (HREF) is connected to.
- **sda**: This is the GPIO pin that the SCCB Data (SIOD) is connected to. 
- **scl**: This is the GPIO pin that the SCCB Clock (SIOC) is connected to. 
- **reset**: This is the GPIO pin that the camera reset line is connected to. Using this pin, you can perform a hardware reset of the camera.
- **freq**: This sets the frequency of the XCLK signal to the camera module. It's typically set to a value between 10 MHz and 20 MHz. Lower frequencies might be more stable, but could lead to lower frame rates.

Optional parameters:
~~~~~~~~~~~~~~~~~~~~~
- **pwdn**: This is the GPIO pin that the Power Down (PWDN) line is connected to.
- **frame**: This defines the resolution of the camera. "HD" stands for High Definition, which is typically 1280x720 pixels. This is not related to a GPIO pin but rather a configuration setting for the camera.
- **format**: This sets the pixel format for the camera. Typical values include "RGB565", "YUV422", "GRAYSCALE", or "JPEG". The exact formats supported can depend on the specific camera module used.
- **vflip**: If set to true, this setting flips the image vertically. This can be useful if the camera is physically installed upside down.
- **hmirror**: If set to true, this setting mirrors the image horizontally. This can be useful if the camera image needs to be mirrored for some reason.


Configuration Table Examples:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: lua

   -- Settings for Seeed Studio XIAO ESP32S3 Sense
   local cfg={
      d0=15, d1=17, d2=18, d3=16, d4=14, d5=12, d6=11, d7=48,
      xclk=10, pclk=13, vsync=38, href=47, sda=40, scl=39, pwdn=-1,
      reset=-1, freq="20000000", frame="HD"
   }

   -- Settings for Aideepen ESP32-CAM
   local cfg={
      d0=5, d1=18, d2=19, d3=21, d4=36, d5=39, d6=34, d7=35,
      xclk=0, pclk=22, vsync=25, href=23, sda=26, scl=27, pwdn=32,
      reset=-1, freq="20000000", frame="HD"
   }

   -- Settings for FREENOVE  ESP32-S3 WROOM
   local cfg={
      d0=11, d1=9, d2=8, d3=10, d4=12, d5=18, d6=17, d7=16,
      xclk=15, pclk=13, vsync=6, href=7, sda=4, scl=5, pwdn=-1,
      reset=-1, freq="20000000", frame="HD"
   }
   

