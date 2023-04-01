<?lsp
if request:header"Sec-WebSocket-Key" then
   local s = ba.socket.req2sock(request)
   if s then
      trace"New WebSocket connection"
      local sockets=page.sockets or {}
      page.sockets=sockets
      if not page.adc then
         local function onData(data,err)
            if data then
                for s in pairs(sockets) do s:write(data) end
            else
                trace(err)
            end
         end
         page.adc=esp32.adc(1, 0, {
                               bl=20000,
                               callback=onData,
                               fs=20000,
                               filter="data",
                            })
      end
      sockets[s]=true
      s:event(function(s)
                 s:read()
                 sockets[s]=nil
                 if not next(sockets) then
                    page.adc:close()
                    page.adc=nil
                 end
              end, "s")
      return
   end
end
?>
<!DOCTYPE html>
<html lang="en">
<head>
   <script src="https://cdn.jsdelivr.net/npm/pcm-player-ex@0.3.10/dist/index.min.js"></script>
   <meta charset="UTF-8">
   <title>Opus to PCM</title>
</head>
<body>
<div id="container" style="width: 400px; margin: 0 auto;">
    <h2>ESP32 Audio!</h2>
    <p>Click this text to instruct the browser you really want to listen to the audio!</p>
</div>
<script>
 window.onload = function() {
   var socketURL = 'ws://<?lsp=request:url():match".-//(.+)"?>';
   var player = new PCMPlayer({
        encoding: 'Int16',
        channels: 1,
        sampleRate: 17000,
        flushingTime: 0,
        volume: 1
   });

     var ws = new WebSocket(socketURL);
     ws.binaryType = 'arraybuffer';
     ws.addEventListener('message',function(event) {
         var data = new Uint8Array(event.data);
         player.feed(data);
     });
 }   
</script>
</body>
</html>
