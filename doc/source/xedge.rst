Xedge32
================

Xedge32 is a development tool for embedded edge devices. Its web-based user interface makes it incredibly easy to develop embedded software using the Lua language.

.. image:: https://realtimelogic.com/images/xedge/v1/Xedge.png
   :alt: Xedge32 UI

Xedge is a versatile tool that runs on various platforms, including ESP32. This documentation focuses on getting started with Xedge on ESP32. For more details on using Xedge, please refer to the `Xedge Main Documentation <https://realtimelogic.com/ba/doc/?url=Xedge.html>`_.

How To Use Xedge32
---------------------------------------

The LSP-Example's GitHub repository includes `ESP32 specific examples <https://github.com/RealTimeLogic/LSP-Examples/tree/master/ESP32>`_ that we will use in the instructions below. First, download these files to your computer and then upload them to the ESP32 using the built-in Web File Manager. You can use the combined WebDAV and Web File Manager URL at ``http://ip-address/rtl/apps/`` to upload files to the ESP32. You can refer to the :download:`WebDAV how-to video <https://youtu.be/i5ubScGwUOc>` for more information on how to mount a WebDAV drive.

Upload this directory to the ESP32 by using WebDAV or If using the Web File Manager as shown below, use a browser and navigate to ``http://ip-address/rtl/apps/``

|Web File manager: Drag and Drop|

Navigate to ``http://ip-address/rtl/apps/`` and click the :guilabel:`+` button to create a directory named "Lua-Examples". Then, click the "Lua-Examples" link to navigate to ``http://ip-address/rtl/apps/Lua-Examples/``, select the files, and drag & drop the files into the browser window at ``http://ip-address/rtl/apps/Lua-Examples/``. The Web File Manager starts uploading the files as soon as you drop them into the browser window.

|Web File Manager: Upload|

After completing the upload process, navigate to ``http://ip-address/rtl/``, expand :guilabel:`disk` in the left pane, and right click the "Lua-Examples" directory. Then, click :guilabel:`New App`. In the dialog, enable :guilabel:`Running` and :guilabel:`LSP App`. click :guilabel:`Save`

In the Xedge Lua IDE, you can expand the examples in the left pane, and click any of the examples to open the source code in the editor. Then, click the run button to execute the example. 

How To Use an External IDE
--------------------------

While the internal web-based Lua IDE is easy to use since it is an integral part of Xedge32, a more advanced IDE may be preferred and is required for debugging Lua code.

You can use any external IDE or editor to edit files directly on the ESP32 by mounting the device as a WebDAV drive. For instructions on how to mount the WebDAV drive using various client operating systems, visit the `How to Mount a WebDAV Drive <https://fuguhub.com/FileServer.lsp>`__ page on the FuguHub site.

To mount the ESP32's WebDAV server, use the URL https://ip-address/rtl/apps/.

.. _LuaDebug:


How To Debug Lua Code
---------------------

Debugging Lua code is made easy with the Barracuda App Server's `Lua debug module <https://realtimelogic.com/ba/doc/?url=auxlua.html#dbgmon>`__, which is compatible with Visual Studio Code and the Lua plugin. To use the debugger, you need access to the Lua files, but it's not possible to access them via WebDAV from the device when debugging. This is because the debug module stops all activity in the server, including the WebDAV server, when a breakpoint is hit. Instead, keep the Lua files on your host computer and access them from the device using the NetIo feature. Refer to the Xedge32 documentation for `additional NetIo details <https://realtimelogic.com/ba/doc/?url=xedge/readme.html#netio>`__.

**Configure a debug session as follows:**

Follow the instructions in our `How to Debug Lua Code Using Visual Studio Code <https://github.com/RealTimeLogic/LSP-Examples/tree/master/Lua-Debug>`__ guide on GitHub. Make sure to install all tools listed in the `Prerequisites <https://github.com/RealTimeLogic/LSP-Examples/tree/master/Lua-Debug#prerequisites>`__ section and configure Visual Studio Code as explained in the `Configuring VS <https://github.com/RealTimeLogic/LSP-Examples/tree/master/Lua-Debug#configuring-vs>`__ section.

Next, navigate to your local ``xedge`` directory and copy the `File Server Directory from GitHub <https://github.com/RealTimeLogic/LSP-Examples/tree/master/Lua-Debug/FileServer>`__ to ``xedge-ESP32/BAS/examples/xedge/FileServer``.

To start the Mako Server, run the following command in the ``xedge-ESP32/BAS/examples/xedge`` directory:

.. container:: cmd

   mako -l::FileServer

If the Mako Server is not in the path, use this command instead:

.. container:: cmd

   /path/2/mako -l::FileServer

The File Server App sets up a file server that is accessed by the NetIo client running in the device. The File Server App should automatically open a browser window with the Web File Manager. Click the Lua-Examples link (1) and copy the full URL (2).

|Web File Server|

Using the browser, navigate to ``http://ip-address/rtl/``, right-click :guilabel:`net` in the left pane and paste in the URL in the app dialog's :guilabel:`URL` field. Enable :guilabel:`Running` and :guilabel:`LSP App`. click :guilabel:`Save`.

The following printout should appear in the File Server console as soon as the NetIo client connects to the File Server App running on your host:

::

   Creating 'Visual Studio Code' config file:   Lua-Examples/.vscode/launch.json



As detailed in the GitHub documentation, a launch.json file is required with a `sourceMaps attribute <https://github.com/RealTimeLogic/LSP-Examples/tree/master/Lua-Debug#setting-up-sourcemaps-in-launchjson>`__.  This file with the sourceMaps attribute is automatically created by the File Server App.

Start Visual Studio Code and open the local directory: xedge-ESP32/BAS/examples/xedge/Lua-Examples

Click on the httpclient.lsp file and add the two following lines at the top of the file just below the comment:

.. code-block:: lua

   require"ldbgmon".connect{client=false}
   trace"Running LSP page"

The following screenshot shows the code modification:

|Visual Studio Code with Lua|

Using your browser, navigate to ``http://ip-address/LuaExamples/httpclient.lsp``, where ip-address is your ESP32's IP address. The browser will now be waiting (spinning) since the web server is now frozen and waiting for the debugger client (Visual Studio Code) to connect. The web server can at this point only be resumed by the debugger.

In Visual Studio Code, press the F5 button to start a debug session.  Visual Studio Code should now connect to the debug monitor and automatically halt the code as shown in the screenshot below.

|Visual Studio Code with Lua Http Client|

You can now step through the code or simply resume by pressing F5. When the server resumes, the browser window stops spinning. Refreshing the browser window will not stop the LSP page again now that you have an established debugger connection. To stop the code at the same location, set a breakpoint at the trace() call in the editor.

|Visual Studio Code Set Breakpoint|

You can now refresh the browser and the new breakpoint will be hit. Try stepping into the code (F11). Notice that you can step into the HTTP client library `httpm <https://realtimelogic.com/ba/doc/?url=auxlua.html#managed>`__, which is partly implemented in Lua. The code is not part of your application, but is embedded inside the firmware. The debug monitor sends the Lua source code to Visual Studio Code from the Xedge32's ZIP file embedded in the firmware.

The following short video shows how to remotely debug Lua code on a Raspberry Pi. The instructions are similar; however, the server running on the Raspberry Pi (the one being debugged) is the Mako Server and not Xedge32.

Further Reading
---------------

   Prior to reading any of the following tutorials, check out the `online Lua Tutorials <https://tutorial.realtimelogic.com/>`__ and read the `Xedge Documentation <https://realtimelogic.com/ba/doc/?url=Xedge.html>`_.

**Lua examples designed for ESP32:**

See the `ESP32 section <https://github.com/RealTimeLogic/LSP-Examples/tree/master/ESP32>`_ in the LSP-Example's GitHub repository.

**Lua examples and tutorials compatible with ESP32:**

.. container:: list

   -  `How to connect the ESP32 to an MQTT cloud server using Mutual
      TLS
      Authentication <https://makoserver.net/articles/How-to-Connect-to-AWS-IoT-Core-using-MQTT-amp-ALPN>`__
   -  `How to design a web user interface by using a ready to use
      dashboard app
      template <https://makoserver.net/articles/How-to-Build-an-Interactive-Dashboard-App>`__
   -  `How to perform rapid IoT device client development using
      Lua <https://realtimelogic.com/articles/Rapid-Firmware-Development-with-the-Barracuda-App-Server>`__
   -  `How to access the web server behind a firewall without using port
      forwarding <https://makoserver.net/articles/Secure-Remote-Access>`__
   -  `Ajax for
      beginners <https://makoserver.net/articles/Ajax-for-Beginners>`__
   -  `How to design a modern multiuser reactive web
      interface <https://realtimelogic.com/articles/Modern-Approach-to-Embedding-a-Web-Server-in-a-Device>`__
   -  `Why use Lua for embedded web application
      development <https://realtimelogic.com/articles/Lua-FastTracks-Embedded-Web-Application-Development>`__
   -  `Using the ESP32 as a foundation for your On-Premises IoT
      Platform <https://realtimelogic.com/articles/OnPremises-IoT-Platform>`__

.. |Web File manager: Drag and Drop| image:: https://realtimelogic.com/downloads/bas/rt1020/Web-File-manager-Drag-Drop.png
   :class: fright
.. |Web File Manager: Upload| image:: https://realtimelogic.com/downloads/bas/rt1020/Web-File-Manager-Upload.png
   :class: fright
.. |Lua Debugger Screenshot| image:: https://makoserver.net/blogmedia/Lua-Debugger.gif
.. |Web File Server| image:: https://realtimelogic.com/downloads/bas/rt1020/FileServer-URL.png
.. |Visual Studio Code with Lua| image:: https://realtimelogic.com/downloads/bas/rt1020/VS-HttpClient-Mod.png
.. |Visual Studio Code with Lua Http Client| image:: https://realtimelogic.com/downloads/bas/rt1020/VS-HttpClient-Auto-BP.png
.. |Visual Studio Code Set Breakpoint| image:: https://realtimelogic.com/downloads/bas/rt1020/VS-HttpClient-Set-BP.png

