

   ____  ___       __      __    __________   
   / __ )/ (_)___  / /__   / /   / ____/ __ \  
  / __  / / / __ \/ //_/  / /   / __/ / / / /  
 / /_/ / / / / / / ,<    / /___/ /___/ /_/ /   
/_________/_/ /_/_/|_|  /_____/___________/    
  / ____/___  _________  __  __/ /_(_)___  ___ 
 / /   / __ \/ ___/ __ \/ / / / __/ / __ \/ _ \
/ /___/ /_/ / /  / /_/ / /_/ / /_/ / / / /  __/
\____/\____/_/   \____/\__,_/\__/_/_/ /_/\___/ 
                                               


This LSP page creates a new blinking LED timer coroutine each time it runs.
Click the refresh button to create a new.

The timer object is free-floating since we are not referencing
(anchoring) the object. The created timer(s) will eventually be
collected by the Lua garbage collector. You can speed up the
collecting by calling the Lua function: collectgarbage()

Timer documentation:
https://realtimelogic.com/ba/doc/?url=lua.html#ba_timer

The example uses GPIO #4. Connect an LED between GPIO 4 and ground.

The Lua code is using the SWIG generated Lua bindings and is
based on:
https://github.com/espressif/esp-idf/blob/master/examples/get-started/blink/main/blink.c

<?lsp
response:setcontenttype"text/plain"

collectgarbage()

local function blink()
    local pin = esp32.gpio(4,"OUT")
   while true do
      trace"blink"
      pin:value(true)
      coroutine.yield(true) -- Sleep
      pin:value(false)
      coroutine.yield(true) -- Sleep
   end
end
ba.timer(blink):set(1000)
?>
