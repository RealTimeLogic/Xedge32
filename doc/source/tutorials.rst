Tutorials and AUX APIs
======================

This documentation set focuses on the Xedge32-specific south bridge APIs. In
practice, most Xedge32 projects also make use of the broader Lua, Xedge, and
Barracuda App Server documentation. The links below are the quickest way to
expand beyond the ESP32 hardware pages in this manual.

Recommended Learning Resources
------------------------------

- `How Xedge32, Xedge, and Barracuda App Server Work Together
  <https://realtimelogic.com/articles/How-Xedge32-Xedge-and-Barracuda-App-Server-Work-Together>`_
  The best first read when you need to understand which documentation set to
  use for each layer.
- `Xedge32 Tutorials <https://realtimelogic.com/xedge32-tutorials/>`_
  A collection of step-by-step guides focused on getting productive with
  Xedge32.
- `Online Interactive Lua Tutorial <https://tutorial.realtimelogic.com/Lua-Types.lsp>`_
  A hands-on introduction to Lua for users who are new to the language.
- `GitHub Xedge32 Examples <https://github.com/RealTimeLogic/LSP-Examples/tree/master/ESP32>`_
  Ready-to-run example programs and reference code for ESP32 targets.
- `Mako Server Tutorials <https://makoserver.net/tutorials/>`_
  Broader tutorials that often apply directly to Xedge32 because both products
  share the same underlying development model.

How Xedge32 Fits Together
-------------------------

Xedge32 is easiest to understand when you view it as three layers working
together:

1. **Barracuda App Server**
   This is the underlying runtime and IoT foundation. It provides the majority
   of the network, protocol, security, storage, and web capabilities used by
   Xedge32 applications.

   - `Lua API <https://realtimelogic.com/ba/doc/?url=lua.html>`_
   - `IoT Protocols <https://realtimelogic.com/ba/doc/?url=IoT.html>`_
   - `Product page <https://realtimelogic.com/products/barracuda-application-server/>`_

2. **Xedge**
   Xedge is the Lua-oriented development environment and workflow layer. It
   gives you the browser-based editor, rapid prototyping model, and application
   runtime used on several platforms, not only ESP32.

   - `Xedge API and how-to documentation <https://realtimelogic.com/ba/doc/?url=Xedge.html>`_
   - `Online demo <https://tutorial.realtimelogic.com/rtl/>`_

3. **Xedge32 for ESP32**
   This is the ESP32-specific extension layer documented in this manual. It
   adds the south bridge API for GPIO, ADC, UART, RMT, camera access, and other
   peripherals.

   - `Introduction <../index.html>`_

Why This Matters
----------------

New users often start by reading only the ESP32 peripheral pages. That is a
good starting point, but most real applications combine:

- Xedge32 peripheral access,
- generic Xedge application behavior, and
- Barracuda App Server networking or IoT protocol features.

If you keep those three layers in mind while learning, the rest of the
documentation becomes much easier to navigate.
