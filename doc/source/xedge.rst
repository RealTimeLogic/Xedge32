.. _Xedge32:

Xedge32
=======

Xedge32 is a development environment for embedded edge devices. It combines a
web-based interface with Lua-driven application development so you can build,
test, and iterate directly on the target device.

.. image:: https://realtimelogic.com/images/xedge/v1/Xedge.png
   :alt: Xedge32 UI

Xedge itself is available on multiple platforms, but this guide focuses on the
ESP32-specific workflow. For the broader Xedge feature set, see the `Xedge main
documentation <https://realtimelogic.com/ba/doc/?url=Xedge.html>`_.

Working with Example Applications
---------------------------------

The `LSP-Examples repository
<https://github.com/RealTimeLogic/LSP-Examples/tree/master/ESP32>`_ contains a
good set of ESP32-oriented examples. A practical way to learn Xedge32 is to
upload these examples, open them in the browser IDE, and run them directly on
the device.

Basic workflow:

1. Download the ESP32 example files to your computer.
2. Upload them to the ESP32 using WebDAV or the built-in Web File Manager.
3. Create a new application from the uploaded directory.
4. Open the example files in the IDE and run them one by one.

Uploading Files to the ESP32
----------------------------

You can use the combined WebDAV and Web File Manager endpoint at
``http://ip-address/rtl/apps/``.

If you want a walkthrough for mounting the device as a WebDAV drive, see the
:download:`WebDAV how-to video <https://youtu.be/i5ubScGwUOc>`.

If you prefer the browser workflow:

1. Open ``http://ip-address/rtl/apps/``.
2. Click the :guilabel:`+` button and create a directory named
   ``Lua-Examples``.
3. Open the new directory at ``http://ip-address/rtl/apps/Lua-Examples/``.
4. Drag and drop the downloaded example files into the browser window.

The upload starts as soon as you drop the files into the page.

Creating an App from the Uploaded Directory
-------------------------------------------

After the files have been uploaded:

1. Navigate to ``http://ip-address/rtl/``.
2. Expand :guilabel:`disk` in the left pane.
3. Right-click the ``Lua-Examples`` directory.
4. Select :guilabel:`New App`.
5. Enable :guilabel:`Running` and :guilabel:`LSP App`.
6. Click :guilabel:`Save`.

You can now expand the example application in the left pane, open any example
file in the editor, and run it directly from the IDE.

Using an External IDE
---------------------

The built-in web IDE is excellent for quick iteration, but an external editor
or IDE may be more comfortable for larger projects. You can edit files directly
on the ESP32 by mounting the device as a WebDAV drive.

For operating-system-specific instructions, see the `How to Mount a WebDAV
Drive <https://fuguhub.com/FileServer.lsp>`__ guide.

The WebDAV URL is:

::

   https://ip-address/rtl/apps/

.. _LuaDebug:

Debugging Lua Code
------------------

Remote debugging is supported through the Barracuda App Server `Lua debug
module <https://realtimelogic.com/ba/doc/?url=auxlua.html#dbgmon>`__. This
workflow integrates with Visual Studio Code and lets you debug Lua running on
the device.

|Lua Debugger Screenshot|

One important detail is that breakpoints pause the server. Because of that, the
device's WebDAV service is also paused while the debugger is stopped. For this
reason, it is better to keep your source files on the host computer and let the
device access them through NetIo during a debug session.

For the NetIo background, see the `additional Xedge documentation
<https://realtimelogic.com/ba/doc/?url=xedge/readme.html#netio>`__.

Debugging Workflow
------------------

Follow the `How to Debug Lua Code Using Visual Studio Code
<https://github.com/RealTimeLogic/LSP-Examples/tree/master/Lua-Debug>`__ guide
and complete the prerequisites listed there.

Then:

1. Copy the `File Server directory
   <https://github.com/RealTimeLogic/LSP-Examples/tree/master/Lua-Debug/FileServer>`__
   into ``xedge-ESP32/BAS/examples/xedge/FileServer`` on your host machine.
2. Start the Mako Server from ``xedge-ESP32/BAS/examples/xedge``:

   .. container:: cmd

      mako -l::FileServer

   If ``mako`` is not in your path:

   .. container:: cmd

      /path/2/mako -l::FileServer

3. When the File Server opens in the browser, click ``Lua-Examples`` and copy
   the full URL shown by the file server.
4. In Xedge32, open ``http://ip-address/rtl/``, right-click :guilabel:`net`,
   and paste that URL into the app dialog.
5. Enable :guilabel:`Running` and :guilabel:`LSP App`, then click
   :guilabel:`Save`.

As soon as the device connects to the File Server app, you should see a message
similar to:

::

   Creating 'Visual Studio Code' config file:   Lua-Examples/.vscode/launch.json

The generated ``launch.json`` includes the required `sourceMaps attribute
<https://github.com/RealTimeLogic/LSP-Examples/tree/master/Lua-Debug#setting-up-sourcemaps-in-launchjson>`__.

Starting the First Debug Session
--------------------------------

1. Open the local directory ``xedge-ESP32/BAS/examples/xedge/Lua-Examples`` in
   Visual Studio Code.
2. Open ``httpclient.lsp`` and add the following lines near the top of the
   file:

   .. code-block:: lua

      require"ldbgmon".connect{client=false}
      trace"Running LSP page"

3. In a browser, open ``http://ip-address/LuaExamples/httpclient.lsp``.
4. The page will appear to hang. That is expected because the web server is now
   waiting for the debugger to attach.
5. Press ``F5`` in Visual Studio Code to start debugging.

Once the debugger attaches, execution stops automatically. You can now step
through the code, resume execution, and set breakpoints for later runs.

When you refresh the page again, the new breakpoint is hit. You can even step
into library code such as `httpm
<https://realtimelogic.com/ba/doc/?url=auxlua.html#managed>`__, including Lua
code embedded in the firmware image.

Further Reading
---------------

Before diving deeply into application development, it helps to read the
following:

- `Online Lua tutorials <https://tutorial.realtimelogic.com/>`__
- `Xedge32 tutorials <https://realtimelogic.com/xedge32-tutorials/>`_
- `Xedge documentation <https://realtimelogic.com/ba/doc/?url=Xedge.html>`_
- `ESP32 example section in LSP-Examples
  <https://github.com/RealTimeLogic/LSP-Examples/tree/master/ESP32>`_

Additional articles that work well with Xedge32:

- `How to connect the ESP32 to an MQTT cloud server using Mutual TLS Authentication <https://makoserver.net/articles/How-to-Connect-to-AWS-IoT-Core-using-MQTT-amp-ALPN>`__
- `How to design a web user interface by using a ready to use dashboard app template <https://makoserver.net/articles/How-to-Build-an-Interactive-Dashboard-App>`__
- `How to perform rapid IoT device client development using Lua <https://realtimelogic.com/articles/Rapid-Firmware-Development-with-the-Barracuda-App-Server>`__
- `How to access the web server behind a firewall without using port forwarding <https://makoserver.net/articles/Secure-Remote-Access>`__
- `Ajax for beginners <https://makoserver.net/articles/Ajax-for-Beginners>`__
- `How to design a modern multiuser reactive web interface <https://realtimelogic.com/articles/Modern-Approach-to-Embedding-a-Web-Server-in-a-Device>`__
- `Why use Lua for embedded web application development <https://realtimelogic.com/articles/Lua-FastTracks-Embedded-Web-Application-Development>`__
- `Using the ESP32 as a foundation for your On-Premises IoT Platform <https://realtimelogic.com/articles/OnPremises-IoT-Platform>`__

.. |Lua Debugger Screenshot| image:: https://makoserver.net/blogmedia/Lua-Debugger.gif
