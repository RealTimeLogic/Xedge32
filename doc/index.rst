.. ESP32 Lua API documentation master file, created by
   sphinx-quickstart on Wed Feb 22 12:03:44 2023.

Xedge32 Introduction
=======================

:ref:`Xedge32` is `Xedge <https://realtimelogic.com/ba/doc/?url=Xedge.html>`_ with a peripheral Lua API designed for the ESP32. Xedge is a Lua REPL designed for edge devices and powered by the `Barracuda App Server library <https://github.com/RealTimeLogic/BAS>`_.


.. image:: https://realtimelogic.com/GZ/images/BAS-ESP32.svg
   :alt: Xedge32

**Think of Xedge32 as an enhanced version of Xedge, featuring an additional peripheral south bridge specifically engineered for the ESP32.**

Xedge32 significantly accelerates the development of IoT devices, combining a comprehensive ready-to-use `IoT north bridge API <../doc/?url=IoT.html>`_ with the ESP32 peripheral, aka south bridge API. This approach, which does not require C coding, streamlines working with device components such as sensors. The added advantage of using the ESP32, a cost-effective solution, makes it easy to retrofit existing products with IoT capabilities. By streamlining development and testing stages, Xedge32 dramatically reduces the time it takes to bring secure IoT-enabled sensor products to market. All these benefits make Xedge32 an exceptionally user-friendly and efficient choice for IoT device development.

Xedge32 leverages the high-level Lua language. Lua's learning curve is considerably less steep than other computer languages, making it an invaluable tool in high-paced development environments, including creating secure IoT-enabled device products. If you're new to Lua, be sure to check out the `Online Interactive Lua Tutorials <https://tutorial.realtimelogic.com/Lua-Types.lsp>`_ for an easy and accessible way to learn the language.

**Xedge vs. Xedge32**

This documentation exclusively covers Xedge32 specific features, the ESP32 peripheral Lua API, and how to use Xedge on the ESP32. If you are looking for documentation on network-related and other APIs provided by the Barracuda App Server development foundation, please refer to the `Barracuda App Server API <https://realtimelogic.com/ba/doc/>`_ and the generic `Xedge documentation <../doc/?url=Xedge.html>`_.

Note that the ESP32 LUA API, provided by Xedge32, currently exports a subset of the ESP-IDF C peripheral API to Lua.




Interrupts
-----------

The ESP32 Lua API is designed to take advantage of interrupt-driven events, and it is highly recommended to use the interrupt-driven (callback) API when it is available. While it is possible to poll for data using a `timer <../ba/doc/?url=ua.html#ba_timer>`_, it is not the recommended approach for APIs that provide event-driven data reading.

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
   source/ADC
   source/GPIO
   source/I2C
   source/PWM
   source/UART
   source/BME280
   source/cam
   source/misc
   source/license



Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
