.. _LuaShell32:

LuaShell32
==========

LuaShell32 is the interactive Lua prompt included with Xedge32. You can access
it either through a USB serial connection or through the web interface. It is
the quickest way to test commands, inspect the current runtime environment, and
configure networking without having to create a full application first.

Think of LuaShell32 as your live control console for the device. You can use it
for one-off experiments, hardware bring-up, diagnostics, and small setup tasks.

Starting the Shell
------------------

You can open LuaShell32 in two ways:

- **USB/serial**: Connect the ESP32 to your computer, open a serial terminal,
  and connect at the appropriate baud rate, typically ``115200``.
- **Web shell**: In the Xedge editor, click the three dots (``...``) in the
  upper-right corner and select **Lua Shell**.

.. note::

   The web-based shell keeps previously entered text in the editor. Before you
   run a new command, clear out old input so you do not accidentally submit a
   longer command sequence than intended.

What the Prompt Looks Like
--------------------------

After the ESP32 finishes booting, the shell displays a prompt like this:

::

   >

The ``>`` character means LuaShell32 is ready to accept Lua code.

Running Your First Command
--------------------------

Type a Lua expression or statement and press Enter. For example:

::

   print("Hello, world!")

LuaShell32 runs the code immediately and prints the result:

::

   > print("Hello, world!")
   Hello, world!

Working Interactively
---------------------

You are not limited to single-line commands. You can create variables, call
functions, and explore APIs directly from the shell.

For example:

::

   x = 42
   y = x + 8
   print(y)

This produces:

::

   > x = 42
   > y = x + 8
   > print(y)
   50

What You Can Access
-------------------

Inside LuaShell32 you have access to:

- the standard Lua libraries,
- the Barracuda App Server APIs,
- the generic Xedge APIs, and
- the Xedge32 ESP32-specific APIs documented in this manual.

This makes the shell a practical place to try hardware calls such as
``esp32.gpio(...)``, inspect network information, or test a sensor before you
wrap the code in an LSP page or application.

When to Use LuaShell32
----------------------

LuaShell32 is especially useful for:

- entering Wi-Fi credentials during first-time setup,
- verifying pin mappings and peripheral behavior,
- testing small snippets before turning them into reusable code,
- checking event delivery and network state, and
- troubleshooting a device in the field.

For larger development work, the :ref:`Web-Based Lua REPL <Xedge32>` in Xedge
usually provides a better experience because you can edit LSP pages, reload
them in the browser, and keep your code organized in files.

See :ref:`Xedge32` for the broader development workflow.
