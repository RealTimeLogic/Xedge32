.. ESP32 Lua API documentation master file, created by
   sphinx-quickstart on Wed Feb 22 12:03:44 2023.

:ref:`Xedge32` is `Xedge <https://realtimelogic.com/ba/doc/?url=examples/xedge/readme.html>`_ with a peripheral Lua API designed for the ESP32. Xedge is a Lua REPL designed for edge devices and powered by the `Barracuda App Server library <https://github.com/RealTimeLogic/BAS>`_. Xedge32 enables developers to quickly test and prototype code without the need for a full compile and deploy cycle, enabling faster iteration and development. Additionally, Xedge32's Lua peripheral API simplifies the interaction with the device's sensors, motors, and other hardware components, making it easier to develop and test code that interacts with the device's peripherals. The immediate feedback provided by the REPL also makes it easier to debug and troubleshoot code, reducing development time and improving the overall quality of the code. The use of the Xedge32 LUA REPL can help to reduce development costs by allowing for faster development and testing cycles and by making it easier to reuse and share code across different projects.

Note that the LUA API, provided by the `C function BaESP32.c <https://github.com/RealTimeLogic/xedge-ESP32/blob/master/main/BaESP32.c>`_, currently exports a subset of the ESP-IDF C peripheral API to Lua.

This documentation exclusively covers Xedge32 specific features, the ESP32 peripheral Lua API, and how to use Xedge on the ESP32. If you are looking for documentation on network-related and other APIs provided by the Barracuda App Server development foundation, please refer to the `Barracuda App Server API <https://realtimelogic.com/ba/doc/>`_.

Interrupts
-----------

The ESP32 Lua API is designed to take advantage of interrupt-driven events, and it is highly recommended to use the interrupt-driven (callback) API when it is available. While it is possible to poll for data using a `timer <https://realtimelogic.com/ba/doc/?url=ua.html#ba_timer>`_, it is not the recommended approach for APIs that provide event-driven data reading.

Automatic collection of HW resources
-------------------------------------

Lua includes a garbage collector that reclaims memory when no more references exist to the object. The ESP32 API takes advantage of this by creating Lua objects for the ESP32 peripheral hardware. When using an LSP page as a REPL, the objects no longer have any references once the page has been executed. This construction can be made to automatically reclaim hardware resources that are no longer in use, simplifying the hot reloading of the new code.

For example, you can modify an `LSP page <https://tutorial.realtimelogic.com/HTML-Forms.lsp>`_ containing Lua code and refresh the page to compile the new code. This allows you to quickly make changes and test them without worrying about manually releasing the hardware resources since the garbage collector takes care of it automatically. See the :ref:`GPIO examples<GpioExamples>` for how this can be done.



Xedge ESP32
==============

.. toctree::
   :maxdepth: 3
   :caption: Contents:

   source/GettingStarted
   source/LuaShell32
   source/xedge
   source/GPIO
   source/I2C
   source/PWM
   source/UART
   source/BME280
   source/misc



Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
