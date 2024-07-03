Applications
=============

Creating and Managing Xedge32 Applications
--------------------------------------------

An Xedge32 application is essentially a Lua-based application packaged as a ZIP file. This convenient packaging allows you to upload and execute the application directly on the Xedge32 platform without unpacking. Follow these steps to get your application up and running:

Uploading Your Application
--------------------------

1. **Firmware Update & App Upload Page**: To upload your application, you can simply drag and drop the ZIP file onto the "Firmware Update & App Upload" page. Access this page by clicking the three dots at the top right corner of the Xedge32 interface and selecting "Firmware Update & App Upload."

2. **Web File Manager**: Another method involves navigating to the Web File Manager, where you can drag and drop your ZIP file. Afterward, add the ZIP file as an application via the Xedge32 IDE.

3. **WebDAV Plugin**: Alternatively, mount the device as a network drive using the WebDAV plugin. Upload the ZIP file and then, similar to the previous methods, add the ZIP file as an application using the Xedge32 IDE.

The first option is notably the most straightforward: It automatically installs the ZIP file as an application on your device. Certain packaged applications may require this approach due to `specific post-installation configuration scripts <mkapp_>`_ that must be run.

ZIP File Applications
----------------------

A ZIP file is considered a ready-to-run deployed application, distinct from directly creating and uploading Lua files to the Xedge32. This upload feature also provides the flexibility to unpack the ZIP file post-upload, allowing for modifications to the code directly on the Xedge32.

**Note:** While you can open files within an uploaded ZIP file, saving them is not an option. 

For additional insights on leveraging the second and third options, refer to the tutorials `Designing Your First Professional Embedded Web Interface, section Getting Started <https://realtimelogic.com/articles/Designing-Your-First-Professional-Embedded-Web-Interface#upload>`_ and `Your First Embedded Single Page Application, section Uploading the SPA and Server-Side Code <https://realtimelogic.com/articles/Your-First-Embedded-Single-Page-Application#upload>`_.

.. _mkapp:

Creating an Xedge32 Application
-------------------------------

To craft an Xedge32 application, compile your Lua code and any necessary CSS, HTML, etc., into a ZIP file. Ensure the directory name is not included within the ZIP. For instance, if you include a `.preload` script, this file should reside at the root of the ZIP.

An Xedge32 application may also contain a `.config` script for specifying additional details and executing code post-installation. Should your ZIP file include a `.config` script, it must return a table with these optional entries:

- **String name**: Designates the application's name in the Xedge32's left pane menu. The ZIP file's name is used if the name is absent.
- **Boolean autostart**: If true, the application's autostart flag is activated, and its status is set to running, enabling automatic startup post-upload.
- **Function install**: Executes for new installations.
- **Function upgrade**: Invoked when the application is re-uploaded.

Example code:

.. code-block:: lua

   -- Argument 'io' is the application's IO instance
   local function install(io)
      trace("install", io:realpath("")) -- Prints empty string if ZIP file
      return "This message is sent to the browser and displayed after completing the installation"

   end

   local function upgrade(io)
      return "This message is sent to the browser and displayed after completing the upgrade"
   end


   return {
      name="MY APP",
      autostart=true,
      install=install,
      upgrade=upgrade
   }
