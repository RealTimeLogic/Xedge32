LuaShell32
=======================

The LuaShell32 is an interactive Lua prompt that is available via USB/serial for the ESP32 microcontroller. With this prompt, you can enter and execute Lua code in real-time, providing you with full control over the behavior of your ESP32 device.

To get started, connect your ESP32 device to your computer using a USB cable and open a terminal program that supports serial communication. Set the serial port to the appropriate baud rate (e.g. 115200) and connect to the ESP32 device.

Once you're connected and the ESP32 boot process completes, you should see a prompt that looks like this:

::

    >

This is the LuaShell32 prompt, which indicates that the prompt is ready to accept Lua code. To execute a command, simply type it into the prompt and press Enter. For example, you could enter the following command to print a message to the console:

::

    print("Hello, world!")

When you press Enter, LuaShell32 will execute the command and display the output:

::

    > print("Hello, world!")
    Hello, world!

You can also define variables and functions in LuaShell32. For example, you could define a variable like this:

::

    x = 42

And then use it in a calculation like this:

::

    y = x + 8
    print(y)

This would output:

::

    > x = 42
    > y = x + 8
    > print(y)
    50


In LuaShell32, you have access to all of the standard Lua libraries and functions, the Barracuda App Server APIs, and the ESP32 APIs.

That's a quick introduction to LuaShell32. With this powerful tool at your fingertips, you can experiment with Lua code and explore the capabilities of the ESP32 microcontroller. However, note that the web-based Lua REPL included in the LSP Application Manager provides a much better user experience. For example, you can modify an LSP page containing Lua code and refresh the page, using your browser, to compile the new code.

See the :ref:`LSP App Manager` for more information.
