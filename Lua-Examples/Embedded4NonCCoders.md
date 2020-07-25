# Embedded programming for non C/C++ developers

The following code snippets are the companion examples for the video tutorial
[Embedded programming for non C/C++ developers](https://youtu.be/XkVQYJ-z7JA).

### The scratchpad template
``` lua
<?lsp
response:setcontenttype"text/plain"


?>
DONE!
```
**References:**
* [response:setcontenttype](https://realtimelogic.com/ba/doc/?url=lua.html#response_setcontenttype)


### Turn on LED connected to GPIO 22
``` lua
local go = esp.GPIO_NUM_22
esp.gpio_set_direction(go, esp.GPIO_MODE_OUTPUT);
esp.gpio_set_level(go, 0)
```
**References:**
* [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html)
* [ESP-IDF GPIO API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html)
* [video @ 5:40](https://youtu.be/XkVQYJ-z7JA?t=340)


### Blink LED connected to GPIO 22

The following code, which blinks the LED 20 times, runs in the context
of the ephemeral request/response environment thus effectively
blocking the LSP page for 20 seconds.

``` lua
local function run()
   local go = esp.GPIO_NUM_22
   esp.gpio_set_direction(go, esp.GPIO_MODE_OUTPUT);
   for i=0,20 do
      trace("on")
      esp.gpio_set_level(go, 0)
      ba.sleep(500)
      trace("off")
      esp.gpio_set_level(go, 1)
      ba.sleep(500)
   end
end

run()
```

**References:**
* [The Command (Request/Response) Environment](https://realtimelogic.com/ba/doc/?url=lua.html#CMDE)
* [ba.sleep](https://realtimelogic.com/ba/doc/?url=lua.html#ba_sleep)
* [trace](https://realtimelogic.com/ba/doc/?url=lua.html#_G_trace)
* [video @ 7:00](https://youtu.be/XkVQYJ-z7JA?t=420)


### Blink LED using an infinite loop running inside an interval timer

The following code can be created/initiated from the LSP scratchpad,
but continues to run as a background task after that the LSP page
completes (terminates).

``` lua

collectgarbage() -- Terminate old timer, if any.

local function run()
   local go = esp.GPIO_NUM_22
    esp.gpio_set_direction(go, esp.GPIO_MODE_OUTPUT);
   while true do
      esp.gpio_set_level(go, 1)
      coroutine.yield(true) -- Sleep
      esp.gpio_set_level(go, 0)
      coroutine.yield(true) -- Sleep
   end
end

ba.timer(run):set(500)
```
**References:**
* [ba.timer](https://realtimelogic.com/ba/doc/?url=lua.html#ba.timer)
* [coroutine.yield](https://realtimelogic.com/ba/doc/en/lua/man/manual.html#pdf-coroutine.yield)
* [collectgarbage](https://realtimelogic.com/ba/doc/en/lua/man/manual.html#pdf-collectgarbage)
* [video @ 8:00](https://youtu.be/XkVQYJ-z7JA?t=480)

### Interval timer for frequently polling the button state at GPIO 22

The following code rapidly polls GPIO pin 22 for state changes.

Note that the following code sends a lot of data to the browser, thus
slowing it down. You can speed up the browser by frequently clearing
the bottom pane console while running the code (double click the
pane). Make sure to terminate the code by running the collectgarbage()
function in an LSP page to stop the constant stream of data from the
server.

``` lua
collectgarbage()

local function run()
   local gi = esp.GPIO_NUM_15
   esp.gpio_set_direction(gi, esp.GPIO_MODE_INPUT);
   while true do
      trace(esp.gpio_get_level(gi))
      coroutine.yield(true) -- Sleep 50 milliseconds
   end
end

ba.timer(run):set(50)

```
**References:**
* [video @ 11:20](https://youtu.be/XkVQYJ-z7JA?t=680)

### Interval timer for polling the button state with state change filter

The above code with a filter that makes the code print to the console
only on state changes thus limiting the amount of data sent to the
browser.

``` lua
collectgarbage()

local function run()
   local gi = esp.GPIO_NUM_15
   esp.gpio_set_direction(gi, esp.GPIO_MODE_INPUT);
   local level = 0

    -- Function returns when button change state
   local function levelChange()
      while esp.gpio_get_level(gi) == level do -- while no change
         coroutine.yield(true) -- Sleep 50 milliseconds
      end
      level = level == 0 and 1 or 0 -- toggle level
   end

   while true do
      levelChange() -- Wait
      trace"push"
      levelChange() -- Wait
      trace"release"
   end
end

ba.timer(run):set(50)
```
**References:**
* [video @ 12:00](https://youtu.be/XkVQYJ-z7JA?t=720)

### Interval timer for polling button state and turning LED on/off

The following example combines the button polling code with the code for setting the LED on/off

``` lua
collectgarbage()

local function run()
   local go = esp.GPIO_NUM_22
   esp.gpio_set_direction(go, esp.GPIO_MODE_OUTPUT);
   local gi = esp.GPIO_NUM_15
   esp.gpio_set_direction(gi, esp.GPIO_MODE_INPUT);
   local level = 0

   local function levelChange()
      while esp.gpio_get_level(gi) == level do
         coroutine.yield(true)
      end
      level = level == 0 and 1 or 0 -- toggle level
   end

   esp.gpio_set_level(go, 1) -- Make sure LED is turned off
   while true do
      levelChange() -- Wait
      esp.gpio_set_level(go, 0) -- On
      levelChange() -- Wait
      esp.gpio_set_level(go, 1) -- Off
   end
end

ba.timer(run):set(50)
```
**References:**
* [video @ 12:50](https://youtu.be/XkVQYJ-z7JA?t=770)

### The above code with added SMS WebHook

The following example shows how to send an HTTP POST request to an
online server for each button press.

``` lua

local function send2ifttt()
    local url="http://maker.ifttt.com/  YOUR WEB HOOK"
    local http = require"httpc".create()
    local ok,err=http:request{url=url, method="POST"}
    trace(http:read"*a") -- Textual response from ifttt.
end

local function run()
   local go = esp.GPIO_NUM_22
   esp.gpio_set_direction(go, esp.GPIO_MODE_OUTPUT);
   local gi = esp.GPIO_NUM_15
   esp.gpio_set_direction(gi, esp.GPIO_MODE_INPUT);
   local level = 0

   local function levelChange()
      while esp.gpio_get_level(gi) == level do
         coroutine.yield(true)
      end
      level = level == 0 and 1 or 0 -- toggle level
   end

  esp.gpio_set_level(go, 1)
   while true do
      levelChange()
      esp.gpio_set_level(go, 0)
      ba.thread.run(send2ifttt)
      levelChange()
      esp.gpio_set_level(go, 1)
   end
end

ba.timer(run):set(50, true)
```

**References:**
* [IFTTT WebHooks](https://ifttt.com/maker_webhooks)
* [HTTP Client Library](https://realtimelogic.com/ba/doc/?url=auxlua.html#httplib)
* [ba.thread.run](https://realtimelogic.com/ba/doc/?url=auxlua.html#thread_lib)
* [video @ 13:51](https://youtu.be/XkVQYJ-z7JAhttps://youtu.be/XkVQYJ-z7JA?t=831)
