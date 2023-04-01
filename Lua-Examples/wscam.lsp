<?lsp

-- This ESP32 CAM example captures images from the camera and sends
-- them to the browser via WebSockets. The example employs a basic
-- self-regulating flow control mechanism. Initially, the server-side
-- code reads and sends one image to the browser. Upon receiving the
-- image, the browser sends a short response message back to the
-- server. Then, the server fetches another image and sends it to the
-- browser. This cycle repeats as long as the client is connected. The
-- example supports multiple browser clients by maintaining a list of
-- connected sockets. The WebSocket sendMessage() function iterates
-- over all connected sockets, transmitting image data to each
-- one. Note that this LSP page stores persistent data in the
-- persistent page table. For more information on the page object,
-- refer to the following link:
-- https://realtimelogic.com/ba/doc/?url=lua.html#CMDE

local cfg={
   d0=5,
   d1=18,
   d2=19,
   d3=21,
   d4=36,
   d5=39,
   d6=34,
   d7=35,
   xclk=0,
   pclk=22,
   vsync=25,
   href=23,
   sda=26,
   scl=27,
   pwdn=32,
   frame="HD"
}

if request:header"Sec-WebSocket-Key" then
   local s = ba.socket.req2sock(request)
   if s then
      trace"New client"
      local sockets=page.sockets or {}
      page.sockets=sockets
      sockets[s]=true
      if not page.cam then
         trace"Creating cam object"
         local function sendMessage(data,txtFrame)
            for s in pairs(sockets) do
               if not s:write(#data <= 0xFFFF and data or "", txtFrame) then
                  s:close()
                  sockets[s]=nil
               end
               page.messages = page.messages + 1
            end
         end
         function page.sendImage()
            if page.pending > 1 then return end
            while 0 ~= page.pending do
               if not page.cam then return end
               local img,err = page.cam:read()
               if not img then
                  local msg = "Reading failed: "..(err or "")
                  trace(msg)
                  sendMessage(msg,true)
                  break
               end
               sendMessage(img,false)
               page.pending = page.pending - 1
            end
         end
         local err
         page.cam,err=esp32.cam(cfg)
         if not page.cam then
            local msg = "Cannot open cam: "..(err or "")
            trace(msg)
            s:write(msg,true)
            page.sockets=nil
            ba.sleep(100) esp32.execute"restart" -- PATCH temporary test
            return
         end
         page.pending=1
         page.clients=0
         page.messages=0
         page.sendImage()
      end
      page.clients=page.clients+1
      s:event(function(s)
                 while s:read() do
                    page.messages = page.messages - 1
                    if page.messages < page.clients and page.pending < 2 then
                       page.pending = page.pending + 1
                       ba.thread.run(page.sendImage)
                    end
                 end
                 sockets[s]=nil
                 trace"Closing client"
                 page.clients=page.clients-1
                 page.messages = page.messages - 1
                 if 0 == page.clients then
                    trace"Closing cam"
                    page.cam:close()
                    page.cam=nil
                 else
                    ba.thread.run(page.sendImage)
                 end
              end, "r")
   end
   return -- Done WS
end
-- Else: standard HTTP GET -- i.e. load page below
?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>Cam Images Over WebSockets</title>
  <style>
    /* Add the following CSS */
    #image-container {
      display: flex;
      justify-content: center;
      align-items: center;
      width: 100%;
    }
    #image {
      max-width: 100%;
      height: auto;
    }
    h2, p {
    text-align: center;
    }
  </style>
</head>
<body>
    <h2>Cam Images Over WebSockets</h2>
   <div id="image-container">
    <img id="image"/>
  </div>
    <p id="msg">Connecting...</p>
<script>

function connect() {
    var socketURL = 'ws://<?lsp=request:url():match".-//(.+)"?>';
    var ws = new WebSocket(socketURL);
    ws.binaryType = 'arraybuffer';
    let img = document.getElementById("image");
    let msg = document.getElementById("msg");
    let frameCounter=0;
    let chkCounter=0;
    let intvTimer=null;
    ws.onopen=function() {
        msg.textContent="Connected; Waiting for image...";
        intvTimer=setInterval(function() {
            if(frameCounter == chkCounter) {
                msg.textContent="Checking server...";
                ws.send("ok");
            }
            chkCounter=frameCounter;
        }, 7000);
    };
    ws.onmessage=function(event) {
        if(event.data instanceof ArrayBuffer) {
            if(event.data.byteLength > 1) {
                const blob = new Blob([event.data], { type: 'image/jpeg' });
                img.src = URL.createObjectURL(blob);
                frameCounter++;
                msg.textContent = `Frames: ${frameCounter}`;
            }
            else {
                msg.textContent = "Image too big";
            }
            ws.send("ok");
        }
        else { // Text, error message
            msg.textContent = "Server error "+event.data;
            ws.close()
            if(intvTimer) clearInterval(intvTimer);
            intvTimer=null;
            setTimeout(connect, 5000);
        }
    };
    ws.onclose = function() {
        if(intvTimer) clearInterval(intvTimer);
        intvTimer=null;
        msg.textContent = 'Disconnected, reconnecting...';
        setTimeout(connect, 3000);
    };
};
window.onload = connect;
</script>
</body>
</html>
