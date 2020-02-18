<!DOCTYPE html>
<?lsp

--[[

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


local gpioNum=25 -- You may have to change the GPIO number

if page.smq then
   if require"smq.hub".isSMQ(request) then
      -- Upgrade HTTP(S) request to SMQ connection
      page.smq:connect(request)
      return
   end
else
   -- The following code would normally be put in a .preload/.config
   -- script.
   local smq
   local SERVO_MIN_PULSEWIDTH=1000 -- Minimum pulse width in microsecond
   local SERVO_MAX_PULSEWIDTH=2000 -- Maximum pulse width in microsecond
   local SERVO_MAX_DEGREE=180 -- Max servo angle
   local function deg2duty(angle)
      angle = SERVO_MAX_DEGREE - angle
      return (SERVO_MIN_PULSEWIDTH + (((SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH) * (angle)) /
                                      (SERVO_MAX_DEGREE)))
   end
   local angle=90
   esp.mcpwm_gpio_init(esp.MCPWM_UNIT_0, esp.MCPWM0A, gpioNum)
   local cfg=esp.mcpwm_config_t()
   cfg.frequency = 50    --frequency = 50Hz, i.e. for every servo motor time period should be 20ms
   cfg.cmpr_a = 0    --duty cycle of PWMxA = 0
   cfg.cmpr_b = 0    --duty cycle of PWMxb = 0
   cfg.counter_mode = esp.MCPWM_UP_COUNTER
   cfg.duty_mode = esp.MCPWM_DUTY_MODE_0
   esp.mcpwm_init(esp.MCPWM_UNIT_0, esp.MCPWM_TIMER_0, cfg) --Configure PWM0A & PWM0B with above settings
   esp.mcpwm_set_duty_in_us(esp.MCPWM_UNIT_0, esp.MCPWM_TIMER_0, esp.MCPWM_GEN_A, deg2duty(angle))
   local function newClient(tid)
      trace"New browser window"
      smq:publish({angle=angle},tid,"servo")
   end
   smq = require"smq.hub".create{onconnect=newClient}
   local function servo(d)
      angle=d.angle
      esp.mcpwm_set_duty_in_us(esp.MCPWM_UNIT_0, esp.MCPWM_TIMER_0, esp.MCPWM_GEN_A, deg2duty(angle))
      trace(angle)
   end
   smq:subscribe("servo",{json=true,onmsg=servo})
   page.smq=smq
   trace"Starting servo example."
end
?>
<html>
<head>
<link href="https://cdn.jsdelivr.net/npm/round-slider@1.4.0/dist/roundslider.min.css" rel="stylesheet" />
<script src="/rtl/jquery.js"></script>
<script src="/rtl/smq.js"></script>
<script src="https://cdn.jsdelivr.net/npm/round-slider@1.4.0/dist/roundslider.min.js"></script>
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
