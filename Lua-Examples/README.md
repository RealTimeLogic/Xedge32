### LSP Example Applications

Simply copy the content of these files into an LSP page created using
the Web-IDE or copy the files directly to the directory for an
application created using the LSP App Manager.

### Blinking LED

The LSP file blinkled.lsp includes a very simple example that shows
how to blink an LED via the auto generated ESP-IDF Lua bindings.

### Servo Control

The LSP file servo.lsp shows how to control a servo by using the auto
generated ESP-IDF Lua bindings.

The LSP file uiservo.lsp extends servo.lsp and provides an HTML5 based
real time user interface for controlling the servo.

### HTTP Client

The httpclient.lsp example shows how to use the HTTP client library
for connecting to online REST services.

### WebSockets

The following example shows how to use WebSockets via the SMQ
protocol. The SMQ protocol is easier to use than WebSockets when
communicating and updating several browsers in real time.

**Run the example as follows:**

1. Copy the example to an LSP page created with the Web-IDE. 
2. When the example is in the Web-IDE, click the "Open" button to open
   the page in a separate browser window.
3. Copy the URL. The URL will be used for the ready to use SMQ
   JavaScript client example (see below).
4. Navigate to: https://simplemq.com/chat/
5. Right click in the browser window and select "Save as.."
6. Save the file in any location on your host computer.
7. Open the saved file in an editor and search for the following line:
       var smq = SMQ.Client(SMQ.wsURL("/smq.lsp"));
8. Change the line to the following:
       var smq = SMQ.Client("ws://URI");
9. The URI is the URL copied in step 3 above without the initial http://
10. Open the saved HTML file in a browser. The chat client now
    connects to your embedded device.
11. Repeat the above step using a second browser.


### Additional Tutorials

We recommend using the Mako Server and the included tutorials for a
deeper dive into using LSP. You may copy and run most of the Mako
Server tutorials. The easiest way to copy the tutorials into the LSP
App Manager's Web-IDE is to copy the examples directly from the online
tutorial server:

https://tutorial.realtimelogic.com/

### Mako Server LSP Book

https://makoserver.net/book/

