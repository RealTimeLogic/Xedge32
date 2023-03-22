<!DOCTYPE html>
<?lsp

--[[

Change 'gpioPin' in the code if you want to use a GPIO other than 14

Make sure to read the information in servo.lsp prior to testing this
example.

This example shows how to control the servo using a web based user
interface. The example uses the following slider:

https://roundsliderui.com/

The slider value is sent in real time from the browser to the device
using WebSockets or more specifically by using SMQ over WebSockets:

https://realtimelogic.com/ba/doc/?url=SMQ.html

The design pattern follows what is outlined in the following tutorial:

https://realtimelogic.com/articles/Modern-Approach-to-Embedding-a-Web-Server-in-a-Device

Open two browser windows to this page, and note that both sliders are
updated in real time.

--]]


local gpioPin=14

local bits=13 -- pwm resolution = 13 bits
local maxPwm=2^bits - 1 -- any value between 0 and maxPwm
local minPulseWidth = 1000  -- 1 ms
local cycleTime = 20000      -- 20 ms

local function calculatePwmDutyCycle(angle)
   return (maxPwm / cycleTime) * (angle * 1000 / 180 + minPulseWidth)
end


if page.smq then
   if require"smq.hub".isSMQ(request) then
      -- Upgrade HTTP(S) request to SMQ connection
      page.smq:connect(request)
      return
   end
else
   collectgarbage()
   local ok,err=esp32.pwmtimer{
      mode="LOW", -- speed_mode
      bits=bits, -- duty_resolution (bits)
      timer=0, -- timer_num
      freq=50, 
   }
   trace(ok,err)
   if ok then
      local duty = 2000 / 20000 * 100
      local pwm,err=esp32.pwmchannel{
         mode="LOW",
         channel=1,
         timer=0, -- timer_sel
         gpio=14,
         duty = calculatePwmDutyCycle(180),
         hpoint=0,
      }
      trace(ok,err)
      if ok then
         local angle
         local function newClient(tid)
            trace"New browser window"
            smq:publish({angle=angle},tid,"servo")
         end
         smq = require"smq.hub".create{onconnect=newClient}
         local function servo(d)
            angle=d.angle
            trace("Angle", angle)
            pwm:duty(calculatePwmDutyCycle(angle))
         end
         smq:subscribe("servo",{json=true,onmsg=servo})
         page.smq=smq
         trace"Starting servo example."
      end
   end
end
?>
<html>
<head>
<link href="https://cdn.jsdelivr.net/npm/round-slider@1.6.1/dist/roundslider.min.css" rel="stylesheet" />
<script src="/rtl/jquery.js"></script>
<script src="/rtl/smq.js"></script>
<script src="https://cdn.jsdelivr.net/npm/round-slider@1.6.1/dist/roundslider.min.js"></script>
<style>

html,body{
    position: relative;
    height:100%;
    width:100%;
    padding:0;
    margin:0;
}

#ServoSlider {
    width: 260px;
    height: 130px;
    padding: 20px;
    position: absolute;
    top: 50%;
    left: 50%;
    margin: -85px 0 0 -130px;
}

#ServoSlider .rs-handle  {
    background-color: transparent;
    border: 8px solid transparent;
    border-right-color: black;
    margin: -8px 0 0 14px !important;
}
#ServoSlider .rs-handle:before  {
    display: block;
    content: " ";
    position: absolute;
    height: 12px;
    width: 12px;
    background: black;
    right: -6px;
    bottom: -6px;
    border-radius: 100%;
}
#ServoSlider .rs-handle:after  {
    display: block;
    content: " ";
    width: 106px;
    position: absolute;
    top: -1px;
    right: 0px;
    border-top: 2px solid black;
}

</style>
<script>
$(function() {
    var smq = SMQ.Client(); // No args: connect back to 'origin'.
    let active=true;

    function onSmqMsg(d,ptid) {
        if(ptid != smq.gettid()) { //Ignore messages from 'self'
            active=true;
            $("#ServoSlider").roundSlider("option", "value", Math.floor(d.angle * 100 / 180));
            active=false;
        }
    }
    
    smq.subscribe("self",{datatype:"json",onmsg:onSmqMsg});
    smq.subscribe("servo",{datatype:"json",onmsg:onSmqMsg});
    function onChange (e) {
        if(!active)
            smq.pubjson({angle:Math.floor(e.value * 180 / 100)}, "servo");
    }
    $("#ServoSlider").roundSlider({
        animation:false,
        sliderType: "min-range",
        radius: 130,
        showTooltip: false,
        width: 16,
        value: 0,
        handleSize: 0,
        handleShape: "square",
        circleShape: "half-top",
        change: onChange,
        tooltipFormat: onChange
    });
    active=false;
});
</script>
</head>
<body>

<div id="ServoSlider">
</div>

</body></html>
