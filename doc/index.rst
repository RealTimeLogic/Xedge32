.. ESP32 Lua API documentation master file, created by
   sphinx-quickstart on Wed Feb 22 12:03:44 2023.

Welcome to the `Barracuda App Server <https://realtimelogic.com/products/barracuda-application-server/>`_'s ESP32 peripheral API documentation! This API allows Lua code to easily call the underlying C functions provided by the `ESP-IDF <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html>`_ without the need to understand the C API. The LUA API, provided by the `C function BaESP32.c <https://github.com/RealTimeLogic/LspAppMgr-ESP32/blob/master/main/BaESP32.c>`_, exports a subset of the ESP-IDF C peripheral API to Lua.

Please note that this documentation exclusively documents the ESP32 peripheral API. If you are looking for documentation on network-related and other APIs provided by the Barracuda App Server development foundation, please refer to the `Barracuda App Server API <https://realtimelogic.com/ba/doc/>`_.

The ESP32 Lua API is designed to take advantage of interrupt-driven events, and it is highly recommended to use the interrupt-driven (callback) API when it is available. While it is possible to poll for data using a `timer <https://realtimelogic.com/ba/doc/?url=ua.html#ba_timer>`_, it is not the recommended approach for APIs that provide event-driven data reading.

Lua includes a garbage collector that reclaims memory when no more references exist to the object. The ESP32 API takes advantage of this by creating Lua objects for the ESP32 peripheral hardware. When using an LSP page as a REPL, the objects no longer have any references once the page has been executed. This construction can be made to automatically reclaim hardware resources that are no longer in use, simplifying the hot reloading of the new code.

For example, you can modify an `LSP page <https://tutorial.realtimelogic.com/HTML-Forms.lsp>`_ containing Lua code and refresh the page to compile the new code. This allows you to quickly make changes and test them without worrying about manually releasing the hardware resources since the garbage collector takes care of it automatically. See the :ref:`GPIO examples<GpioExamples>` for how this can be done.



ESP32 Lua API
=========================================

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   source/GPIO
   source/I2C
   source/PWM
   source/UART
   source/BME280



Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
