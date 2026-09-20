Applications
============

Creating and Managing Xedge32 Applications
------------------------------------------

An Xedge32 application is typically packaged as a ZIP file containing Lua code
and, when needed, supporting assets such as HTML, CSS, JavaScript, images, and
configuration scripts. Xedge32 can run this ZIP file directly, which makes
deployment simple and keeps the upload workflow friendly for both development
and production use.

This packaging model is helpful because it lets you move a complete application
onto the device as a single artifact instead of uploading individual files one
by one.

Uploading Your Application
--------------------------

You can install an application ZIP file in several ways:

1. **Firmware Update & App Upload page**
   Open the page from the three-dot menu in the upper-right corner of the
   Xedge32 interface and drag the ZIP file into the browser window.
2. **Web File Manager**
   Upload the ZIP file manually, then add it as an application from the Xedge32
   IDE.
3. **WebDAV**
   Mount the device as a drive, copy the ZIP file over, and then enable it as
   an application from the IDE.

The first method is the easiest for most users because it uploads and installs
the ZIP file in one step. Some packaged applications also depend on this path
because they include `post-installation configuration scripts <mkapp_>`_ that
should run automatically during installation.

ZIP File Applications
---------------------

A ZIP file uploaded as an application is treated as a ready-to-run deployment
unit. This is different from editing loose Lua files directly on the device.

This approach gives you two useful workflows:

- keep the application packaged when you want a clean deployment artifact, or
- unpack and modify the application on the device when you want to iterate
  directly in Xedge32.

.. note::

   You can inspect files stored inside an uploaded ZIP archive, but you cannot
   save changes back into the archive itself. If you want to edit files on the
   device, unpack the application first.

For more examples of the upload workflow, see:

- `Designing Your First Professional Embedded Web Interface
  <https://realtimelogic.com/articles/Designing-Your-First-Professional-Embedded-Web-Interface#upload>`_
- `Your First Embedded Single Page Application
  <https://realtimelogic.com/articles/Your-First-Embedded-Single-Page-Application#upload>`_

.. _mkapp:

Creating an Xedge32 Application
-------------------------------

To build your own application package:

1. Gather your Lua files and any supporting assets.
2. Place them in a directory structure exactly as you want them to appear in
   the application.
3. Create a ZIP file from the *contents* of that directory, not from the
   directory itself.

In practice, this means files such as ``.preload`` or ``.config`` must be at
the root of the ZIP archive if you want Xedge32 to detect them correctly.

Optional ``.config`` Script
---------------------------

An application may include a ``.config`` script for metadata and installation
hooks. If present, the script must return a Lua table with any of the following
optional fields:

- ``name``: String shown for the application in the Xedge32 left-hand pane. If
  omitted, the ZIP filename is used.
- ``autostart``: Boolean that enables automatic startup and marks the app as
  running after installation.
- ``install``: Function called for a new installation.
- ``upgrade``: Function called when an existing application is updated.

Example ``.config`` Script
--------------------------

.. code-block:: lua

   -- Argument 'io' is the application's IO instance
   local function install(io)
      trace("install", io:realpath(""))
      return "This message is sent to the browser and displayed after installation"
   end

   local function upgrade(io)
      return "This message is sent to the browser and displayed after upgrade"
   end

   return {
      name = "MY APP",
      autostart = true,
      install = install,
      upgrade = upgrade
   }

The ``install`` and ``upgrade`` hooks are a good place to perform first-run
setup, initialize directories, migrate stored data, or provide a helpful status
message back to the browser.
