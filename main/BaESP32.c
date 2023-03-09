/* 
Copyright (c) Real Time Logic
This software may only be used by the terms and conditions stipulated
in the corresponding license agreement under which the software has
been supplied. All use of this code is subject to the terms and
conditions of the included License Agreement.


   ESP32 Lua Bindings

The APIs that allow for asynchronous Lua callbacks work in the
following way: an interrupt is triggered and a message is inserted
into a queue using xQueueSendFromISR. A high-priority thread waits for
messages using xQueueReceive. Once a message is received, it is
packaged into a ThreadJob and dispatched to the LThreadMgr
instance. The LThreadMgr selects a thread from its pool and calls a
callback that then calls the appropriate Lua callback.


Interrupt Function
   Event Queue
        |
        |                         LThreadMgr instance
        v                        +-------------------------------+
+---------------+                |                               |
|               |                |    +-------+      +-------+   |
| High Priority |--------------->|    | Task1 |      | Task2 |   |
| Task (Thread) |  LThreadMgr    |    +---+---+      +---+---+   |
|               |   Queue        |        |              |       |
+---------------+                ---------+--------------+-------+
                                          |              |
                                          |              |
                                          v              v
                                      C Callback:   C Callback:
                                      Execute Lua   Execute Lua

Let's look at how the PWM/LEDC implementation below works to
understand better how this works. In this code, we have an interrupt
handler called ledInterruptHandler. When this function gets called by
an interrupt, it sends a message to the eventBrokerTask using a
message queue.

Once the eventBrokerTask receives the message, it wakes up and calls a
function called ledEventCB. This function creates a ThreadJob and
dispatches it to the LThreadMgr instance. The LThreadMgr instance then
selects one of its tasks to handle the job.

The selected task calls executeLuaLedEventCB, which in turn calls the
Lua callback function. This Lua function runs in the context of the
LThreadMgr task.

LThreadMgr concept:
https://realtimelogic.com/ba/doc/en/C/reference/html/md_en_C_md_LuaBindings.html#fullsolution
LThreadMgr API:
https://realtimelogic.com/ba/doc/en/C/reference/html/structThreadJob.html
Introductory example:
https://github.com/RealTimeLogic/BAS/blob/main/examples/xedge/src/AsynchLua.c
*/ 

#include <driver/i2c.h>
#include <driver/uart.h>
#include <driver/ledc.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_log.h>
#include "BaESP32.h"

/*
  The Socket Dispatcher (SoDisp) mutex protecting everything; set in
  installESP32Libs() below.
  https://realtimelogic.com/ba/doc/en/C/reference/html/structThreadMutex.html
*/
ThreadMutex* soDispMutex;

/*
  A mutex used for protecting small regions
*/
static ThreadMutex rMutex;

/*********************************************************************
 *********************************************************************
                           Misc functions
 *********************************************************************
 *********************************************************************/

static int throwInvArg(lua_State* L, const char* type)
{
   return luaL_error(L,"Invalidarg: %d", type); /* does not return; throws */
}

static int pushEspRetVal(lua_State* L, esp_err_t err, const char* msg)
{
   const char* emsg=0;
   switch(err)
   {
      case ESP_OK: lua_pushboolean(L,TRUE); return 1;
      case ESP_ERR_INVALID_ARG: emsg="invalidarg"; break;
      case ESP_ERR_NO_MEM: emsg="nomem"; break;
      case ESP_ERR_INVALID_STATE: emsg="invalidstate"; break;
      case ESP_ERR_TIMEOUT: emsg="timeout"; break;
      case ESP_ERR_WIFI_SSID: emsg="wifi ssid"; break;
      default:
         emsg="fail";
   }
   lua_pushnil(L);
   if(msg)
      lua_pushfstring(L,"%s: %s",msg,emsg);
   else
      lua_pushstring(L,emsg);
   return 2;
}


/* This function creates and pushes on the stack a userdata object
   with one associated Lua value. The function also sets the object's
   metatable if it is the first time the function is called with 'tname'.
   The space for one associated Lua value is for function lReferenceCallback().
   Params:
     size: Required userdata size.
     tname: The name of the metatable
     l: Registers all functions listed in l in metatable, if created.
 */

static void* lNewUdata(
   lua_State *L, size_t size, const char *tname, const luaL_Reg *l)
{
   void* o = lua_newuserdatauv(L, size, 1);
   memset(o,0,size);
   if(luaL_newmetatable(L, tname))
   {  /*
        Create metatable and register functions:
      */
      lua_pushvalue(L, -1); /* Copy meta */
      lua_setfield(L, -2, "__index"); /* meta.__index=meta */
      /* set all function in meta: for-loop -> meta[fname]=f */
      luaL_setfuncs(L,l,0);
   }
   lua_setmetatable(L, -2); /*  setmetatable(userdata, table) */
   return o;
}


/* Sets userdata[associated Lua value postition 1] = callback. The
   above code prevents the garbage collector from collecting the Lua
   callback function as long as the user data associated with it still
   exists. The callback is also registered in the server's weak table,
   making it easy to push the function on the stack using the call:
   balua_wkPush().
   https://realtimelogic.com/ba/doc/en/C/reference/html/group__WeakT.html
   This function returns a reference to the function, but does not
   push any values on the stack.  */
static int
lReferenceCallback(lua_State* L, int userdataIx, int callbackIx)
{
   lua_pushvalue(L, callbackIx);
   baAssert(lua_type(L, userdataIx) == LUA_TUSERDATA);
   baAssert(lua_type(L, -1) == LUA_TFUNCTION);
   lua_setiuservalue(L, userdataIx, 1);
   lua_pushvalue(L, callbackIx);
   return balua_wkRef(L);
}

/* Checks if index 'ix' is a table and if not, creates a table at 'ix'.
 */
static void lInitConfigTable(lua_State* L, int ix)
{
   if( ! lua_istable(L, ix) )
   {
      lua_settop(L,ix -1);
      lua_createtable(L, 0, 0); /* empty config table */
   }
   else
      lua_settop(L,ix);
}

typedef void (*EventBrokerCallback)(gpio_num_t pin);

typedef struct {
   EventBrokerCallback callback;
   gpio_num_t pin;
} EventBrokerQueueNode;


static QueueHandle_t eventBrokerQueue;

/* This high-priority task waits for 'eventBrokerQueue' events and
 * dispatches them to a callback function running in the context of an
 * LThreadMgr thread.
 */
static void eventBrokerTask(void *params)
{
   (void)params;
   for(;;)
   {
      EventBrokerQueueNode n;
      if(xQueueReceive(eventBrokerQueue, &n, portMAX_DELAY))
      {
         n.callback(n.pin);
      }
   }
}






/*********************************************************************
 *********************************************************************
                                 GPIO
 *********************************************************************
 *********************************************************************/

#define GPIO_QUEUE_SIZE 10

typedef struct
{
   int callbackRef;
   gpio_num_t pin;
   int queueLen;
   gpio_mode_t mode;
   U8 queue[GPIO_QUEUE_SIZE]; /* holds GPIO level(s) high/low */
} LGPIO;

static LGPIO** activeGPOI; /* Array of LGPIO pointers with len GPIO_NUM_MAX */


typedef struct
{
   ThreadJob super;
   gpio_num_t pin;
} GpioThreadJob;


static void GPIO_close(lua_State* L, LGPIO* o)
{
   if(GPIO_NUM_MAX != o->pin)
   {
      gpio_reset_pin(o->pin);
      activeGPOI[o->pin]=0; /* should be atomic */
      o->pin = GPIO_NUM_MAX;
   }
}

/* This CB runs in the context of a thread in the LThreadMgr.
 */
static void executeLuaGpioCB(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   int queueLen,ix;
   U8 queue[GPIO_QUEUE_SIZE];
   GpioThreadJob* job = (GpioThreadJob*)jb;
   ThreadMutex_set(&rMutex);
   LGPIO* gpio = activeGPOI[job->pin];
   if(gpio)
   {
      queueLen = gpio->queueLen;
      memcpy(queue,gpio->queue,queueLen);
      gpio->queueLen=0;
   }
   ThreadMutex_release(&rMutex);
   if(gpio)
   {
      lua_State* L = jb->Lt;
      for(ix=0 ; ix < queueLen && GPIO_NUM_MAX != gpio->pin ; ix++)
      {
         /* Push user's callback on the stack */
         balua_wkPush(L, gpio->callbackRef);
         lua_pushboolean(L, queue[ix]);
         if(LUA_OK != lua_pcall(L, 1, 0, msgh))
         {
            GPIO_close(L,gpio);
            break;
         }
         lua_settop(L,1);
      }
   }
}


/* Create a GpioThreadJob and send job to the LThreadMgr instance.
 */
static void dispatchGpioThreadJob(gpio_num_t pin, ThreadJob_LRun lrun)
{
   GpioThreadJob* job;
   job = (GpioThreadJob*)ThreadJob_lcreate(sizeof(GpioThreadJob),lrun);
   if( ! job )
      baFatalE(FE_MALLOC,0);
   job->pin=pin;
   ThreadMutex_set(soDispMutex);
   LThreadMgr_run(&ltMgr, (ThreadJob*)job);
   ThreadMutex_release(soDispMutex);
}


/* This CB runs in the context of the eventBrokerTask.
 */
static void gpioEventCB(gpio_num_t pin)
{
   LGPIO* gpio;
   int queueLen=0;
   int level = gpio_get_level(pin);

   ThreadMutex_set(&rMutex);
   gpio = activeGPOI[pin];
   if(gpio)
   {
      if(gpio->queueLen < GPIO_QUEUE_SIZE)
      {
         gpio->queue[gpio->queueLen++] = level;
         queueLen=gpio->queueLen;
      }
   }
   ThreadMutex_release(&rMutex);

   /* If transitioning from empty to one element in the queue */
   if(1 == queueLen)
      dispatchGpioThreadJob(pin,executeLuaGpioCB);
}


/* Send a GPIO event to the eventBrokerTask, which then
 * calls function gpioEventCB.
 */
static void IRAM_ATTR gpioInterruptHandler(void *arg)
{
   EventBrokerQueueNode n = {
      .callback=gpioEventCB,
      .pin=(gpio_num_t)arg
   };
   xQueueSendFromISR(eventBrokerQueue, &n, 0);
}



/*******************************  Lua API  ***************************/


#define BAGPIO "GPIO"

static LGPIO* GPIO_getUD(lua_State* L)
{
   return (LGPIO*)luaL_checkudata(L,1,BAGPIO);
}


static LGPIO* GPIO_checkUD(lua_State* L)
{
   LGPIO* o = GPIO_getUD(L);
   if(GPIO_NUM_MAX == o->pin)
      luaL_error(L,"CLOSED");
   return o;
}


static int GPIO_value(lua_State* L)
{
   LGPIO* o = GPIO_checkUD(L);
   if(GPIO_MODE_OUTPUT == o->mode || GPIO_MODE_OUTPUT_OD == o->mode)
   {
     L_write:
      return pushEspRetVal(L,gpio_set_level(o->pin,balua_checkboolean(L, 2)),0);
   }
   else
   {
      if(lua_isboolean(L, 2))
      {
         goto L_write;
      }
      else
      {
         lua_pushboolean(L,gpio_get_level(o->pin));
      }
   }
   return 1;
}


static int GPIO_lclose(lua_State* L)
{
   GPIO_close(L,GPIO_getUD(L));
   return 0;
}


static const luaL_Reg gpioObjLib[] = {
   {"value", GPIO_value},
   {"close", GPIO_lclose},
   {"__close", GPIO_lclose},
   {"__gc", GPIO_lclose},
   {NULL, NULL}
};


static int lgpio(lua_State* L)
{
   gpio_config_t cfg;
   gpio_num_t pin=(gpio_num_t)lua_tointeger(L, 1);
   if(pin < GPIO_NUM_0 || pin >= GPIO_NUM_MAX)
      luaL_argerror(L, 1, "Invalid pin");
   const char* mode = luaL_checkstring(L,2);
   cfg.mode = 'I' == mode[0] && !mode[2] ? GPIO_MODE_INPUT :
      ('O' == mode[0] && !mode[3] ? GPIO_MODE_OUTPUT :
       ('O' == mode[0] ? GPIO_MODE_OUTPUT_OD :
        ('I' == mode[0] && !mode[5] ? GPIO_MODE_INPUT_OUTPUT :
         GPIO_MODE_INPUT_OUTPUT_OD)));
   if(activeGPOI[pin])
      luaL_error(L,"INUSE");
   lInitConfigTable(L, 3);
   lua_getfield(L, 3, "callback"); /* callback IX is now 4 */
   int hasCB = lua_isfunction(L, 4);
   cfg.pull_up_en = balua_getBoolField(L, 3, "pullup", FALSE) ?
      GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
   cfg.pull_down_en = balua_getBoolField(L, 3, "pulldown", FALSE) ?
      GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
   if(hasCB)
   {
      const char* type = balua_getStringField(L, 3, "type", 0);
      cfg.intr_type = type ?
         ('L' == *type ? GPIO_INTR_LOW_LEVEL   :
          ('H' == *type ? GPIO_INTR_HIGH_LEVEL :
           ('N' == *type ? GPIO_INTR_NEGEDGE   :
            ('A' == *type ? GPIO_INTR_ANYEDGE  : GPIO_INTR_POSEDGE )))) :
         GPIO_INTR_POSEDGE;
   }
   else
      cfg.intr_type = GPIO_INTR_DISABLE;
   cfg.pin_bit_mask = BIT64(pin);
   LGPIO* gpio = (LGPIO*)lNewUdata(L,sizeof(LGPIO),BAGPIO,gpioObjLib);
   gpio->mode = cfg.mode;
   gpio->pin=pin;
   gpio_reset_pin(pin);
   if(ESP_OK != gpio_config(&cfg))
      throwInvArg(L,"config");
   activeGPOI[pin]=gpio;
   if(hasCB)
   {
      /* Userdata at -1 and callback ix is 4 */
      gpio->callbackRef=lReferenceCallback(L, lua_absindex(L,-1), 4);
      gpio_isr_handler_add(pin, gpioInterruptHandler, (void *)pin); 
   }
   return 1;
}


/*********************************************************************
 *********************************************************************
                       PWM:   LED Control (LEDC)
 *********************************************************************
 *********************************************************************/

#define BALEDC "LEDC"


typedef struct{
   gpio_num_t pin;
   ledc_mode_t mode;
   ledc_channel_t channel;
   int callbackRef;
   U8 running;
} LLEDC;

static int ledsRunning=0;

static LLEDC* LLEDC_getUD(lua_State* L)
{
   return (LLEDC*)luaL_checkudata(L,1,BALEDC);
}

static LLEDC* LLEDC_checkUD(lua_State* L)
{
   LLEDC* o = LLEDC_getUD(L);
   if( ! o->running )
      luaL_error(L,"CLOSED");
   return o;
}


static int LLEDC_duty(lua_State* L)
{
   LLEDC* o = LLEDC_checkUD(L);
   uint32_t duty = (uint32_t)luaL_checkinteger(L, 2);
   uint32_t hpoint = (uint32_t)(lua_isinteger(L,3) ? lua_tointeger(L,3) :
                                ledc_get_hpoint(o->mode,o->channel));
   return pushEspRetVal(L,ledc_set_duty_and_update(
                           o->mode,o->channel, duty, hpoint),0);
}


static int LLEDC_fade(lua_State* L)
{
   LLEDC* o = LLEDC_checkUD(L);
   if(!o->callbackRef)
      luaL_error(L,"No callback");
   uint32_t duty = (uint32_t)luaL_checkinteger(L, 2);
   uint32_t fadeTime = (uint32_t)luaL_checkinteger(L, 3);
   return pushEspRetVal(L,ledc_set_fade_time_and_start(
      o->mode,o->channel,duty,fadeTime,LEDC_FADE_NO_WAIT),0);
}


static void LLEDC_close(lua_State* L, LLEDC* o)
{
   if(o->running)
   {
      activeGPOI[o->pin]=0;
      ESP_ERROR_CHECK(ledc_stop(o->mode,o->channel,0));
      if(0 == --ledsRunning)
         ledc_fade_func_uninstall();
      gpio_reset_pin(o->pin);
      o->running=FALSE;
   }
}


static int LLEDC_lclose(lua_State* L)
{
   LLEDC_close(L,LLEDC_getUD(L));
   return 0;
}


static const luaL_Reg ledcObjLib[] = {
   {"duty", LLEDC_duty},
   {"fade", LLEDC_fade},
   {"close", LLEDC_lclose},
   {"__close", LLEDC_lclose},
   {"__gc", LLEDC_lclose},
   {NULL, NULL}
};


/* This CB runs in the context of a thread in the LThreadMgr.
 */
static void executeLuaLedEventCB(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   LLEDC* led = (LLEDC*)activeGPOI[((GpioThreadJob*)jb)->pin];
   if(led)
   {
      lua_State* L = jb->Lt;
      /* Push user's callback on the stack */
      balua_wkPush(L, led->callbackRef);
      if(LUA_OK != lua_pcall(L, 0, 0, msgh))
         LLEDC_close(L,led);
   }
}


/* This CB runs in the context of the eventBrokerTask.
 */
static void ledEventCB(gpio_num_t pin)
{
   if(activeGPOI[pin])
      dispatchGpioThreadJob(pin, executeLuaLedEventCB);
}


/* Interrupt CB: send event to ledEventCB
 */
static IRAM_ATTR bool
ledInterruptHandler(const ledc_cb_param_t* param, void* arg)
{
    if(param->event == LEDC_FADE_END_EVT)
    {
       EventBrokerQueueNode n = {
          .callback=ledEventCB,
          .pin=(gpio_num_t)arg
       };
       xQueueSendFromISR(eventBrokerQueue, &n, 0);
       return TRUE;
    }
    return FALSE;
}


static ledc_mode_t lLedGetSpeedMode(lua_State* L, int ix)
{
   const char* mode = balua_checkStringField(L, 1, "mode");
   return 'H' == mode[0] ? LEDC_HIGH_SPEED_MODE :
      ('L' == mode[0] ? LEDC_LOW_SPEED_MODE : throwInvArg(L, "mode"));
}


static int lLedChannel(lua_State* L)
{
   luaL_checktype(L, 1, LUA_TTABLE);
   gpio_num_t pin = (gpio_num_t)balua_checkIntField(L, 1, "gpio");
   ledc_mode_t mode = lLedGetSpeedMode(L,1);
   ledc_channel_t channel = (ledc_channel_t)balua_checkIntField(L,1,"channel");
   ledc_timer_t timer = (ledc_timer_t)balua_checkIntField(L, 1, "timer");
   uint32_t duty = (uint32_t)balua_getIntField(L, 1, "duty", 0);
   uint32_t hpoint = (uint32_t)balua_getIntField(L, 1, "hpoint", 0);
   lua_settop(L,1);
   lua_getfield(L, 1, "callback"); /* callback IX is now 2 */
   int hasCB = lua_isfunction(L, 2);
   if(pin < GPIO_NUM_0 || pin >= GPIO_NUM_MAX)
      luaL_argerror(L, 1, "Invalid GPIO pin");
   if(activeGPOI[pin])
      luaL_error(L,"GPIO INUSE");
   if(channel < LEDC_CHANNEL_0 || channel >= LEDC_CHANNEL_MAX)
      throwInvArg(L, "channel");
   if(timer < LEDC_TIMER_0 || timer >= LEDC_TIMER_MAX)
      throwInvArg(L, "timer");
   if(0 == ledsRunning++)
      ledc_fade_func_install(0);
   ledc_channel_config_t cfg={
      .gpio_num=pin,
      .speed_mode=mode,
      .channel=channel,
      .intr_type =  hasCB ? LEDC_INTR_FADE_END : LEDC_INTR_DISABLE,
      .timer_sel = timer,
      .duty= duty
   };
   esp_err_t status = ledc_channel_config(&cfg);
   if(ESP_OK != status)
      return pushEspRetVal(L,status,"channel_config");
   status = ledc_set_duty_and_update(mode, channel, duty, hpoint);
   if(ESP_OK != status)
      return pushEspRetVal(L,status,"set_duty_and_update");
   LLEDC* led = (LLEDC*)lNewUdata(L,sizeof(LLEDC),BALEDC,ledcObjLib);
   led->pin=pin;
   led->mode=mode;
   led->channel=channel;
   led->running=TRUE;
   activeGPOI[pin]=(LGPIO*)led;
   if(hasCB)
   {
      led->callbackRef=lReferenceCallback(L, lua_absindex(L,-1), 2);
      ledc_cbs_t cb = { .fade_cb = ledInterruptHandler };
      ESP_ERROR_CHECK(ledc_cb_register(mode,channel,&cb,(void*)pin));
   }
   return 1;
}


static int lLedTimer(lua_State* L)
{
   luaL_checktype(L, 1, LUA_TTABLE);
   ledc_mode_t mode = lLedGetSpeedMode(L,1);
   ledc_timer_bit_t bits = (ledc_timer_bit_t)balua_checkIntField(L, 1, "bits");
   ledc_timer_t timer = (ledc_timer_t)balua_checkIntField(L, 1, "timer");
   uint32_t freq = (uint32_t)balua_checkIntField(L, 1, "freq");
   if(bits < LEDC_TIMER_1_BIT || bits >= LEDC_TIMER_BIT_MAX)
      throwInvArg(L, "bits");
   if(timer < LEDC_TIMER_0 || timer >= LEDC_TIMER_MAX)
      throwInvArg(L, "timer");
   ledc_timer_config_t cfg={
      .speed_mode=mode,
      .duty_resolution=bits,
      .timer_num=timer,
      .freq_hz=freq,
      .clk_cfg=LEDC_AUTO_CLK
   };
   return pushEspRetVal(L,ledc_timer_config(&cfg),0);
}



/*********************************************************************
 *********************************************************************
                                 I2C
 *********************************************************************
 *********************************************************************/


#define BAI2CMASTER "I2CMASTER"

typedef struct
{
   i2c_cmd_handle_t cmd;
   uint8_t* recbuf;
   size_t recblen;
   i2c_port_t port;
   uint8_t direction;
} LI2CMaster;

/* Time to wait for I2c. Default is 500ms */
#define I2CWT(L,ix) ((TickType_t)luaL_optinteger(L,ix,500)/portTICK_PERIOD_MS)


static void throwInvDirection(lua_State* L)
{
   luaL_error(L, "Invalid direction");
}

static LI2CMaster* I2CMaster_getUD(lua_State* L)
{
   return (LI2CMaster*)luaL_checkudata(L,1,BAI2CMASTER);
}


static LI2CMaster* I2CMaster_checkUD(lua_State* L, int checkCmd)
{
   LI2CMaster* i2cm = I2CMaster_getUD(L);
   if(i2cm->port < 0)
      luaL_error(L, "Closed");
   if(checkCmd && ! i2cm->cmd)
      luaL_error(L, "Not started");
   return i2cm;
}


/* i2cm:start() */
static int I2CMaster_start(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_checkUD(L, FALSE);
   if( ! i2cm->cmd )
      i2cm->cmd = i2c_cmd_link_create();
   /* else recursive: https://www.i2c-bus.org/repeated-start-condition/ */
   return pushEspRetVal(
      L,i2cm->cmd ? i2c_master_start(i2cm->cmd) : ESP_ERR_NO_MEM, 0);
}


/* i2cm:address(addr, direction, [,ack]) */
static int I2CMaster_address(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   const char* d = luaL_checkstring(L,3);
   i2cm->direction = 'R' == d[0] ? I2C_MASTER_READ :
      ('W' == d[0] ? I2C_MASTER_WRITE : throwInvArg(L, "direction"));
   return pushEspRetVal(L,i2c_master_write_byte(
      i2cm->cmd,
      ((uint8_t)luaL_checkinteger(L, 2) << 1) | i2cm->direction,
      balua_optboolean(L, 4, TRUE)), 0);
}


/* i2cm:write(data [,ack]) */
static int I2CMaster_write(lua_State* L)
{
   const uint8_t* data;
   size_t dlen;
   uint8_t byte;
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   if(i2cm->direction != I2C_MASTER_WRITE)
      throwInvDirection(L);
   if(lua_isinteger(L,2))
   {
      byte=(uint8_t)lua_tointeger(L, 2);
      data=&byte;
      dlen=1;
   }
   else
   {
      data = (uint8_t*)luaL_checklstring(L,2,&dlen);
   }
   bool ack = balua_optboolean(L, 3, TRUE);
   return pushEspRetVal(L,dlen==1 ? i2c_master_write_byte(i2cm->cmd,*data,ack) :
                        i2c_master_write(i2cm->cmd,data,dlen,ack), 0);
}


/* i2cm:read(len [, "ACK" "NACK" "LASTNACK"]) */
static int I2CMaster_read(lua_State* L)
{
   esp_err_t status;
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   if(i2cm->direction != I2C_MASTER_READ)
      throwInvDirection(L);
   i2cm->recblen = (size_t )luaL_checkinteger(L, 2);
   const char* as = luaL_optstring(L, 3, 0);
   i2c_ack_type_t ack = as ?
      ('L' == as[0] ? I2C_MASTER_LAST_NACK :
       ('A' == as[0] ? I2C_MASTER_ACK : I2C_MASTER_NACK)) : I2C_MASTER_NACK;
   if(i2cm->recbuf || i2cm->recblen == 0)
      throwInvArg(L, "no recbuf");
   i2cm->recbuf = (uint8_t*)baLMalloc(L, i2cm->recblen+1);
   if( ! i2cm->recbuf )
      return pushEspRetVal(L,ESP_ERR_NO_MEM, 0);
   if(I2C_MASTER_NACK == ack && i2cm->recblen > 1)
   {
      i2c_master_read(i2cm->cmd,i2cm->recbuf,i2cm->recblen-1,I2C_MASTER_ACK);
      status=i2c_master_read_byte(
         i2cm->cmd,i2cm->recbuf+i2cm->recblen-1,I2C_MASTER_NACK);
   }
   else
   {
      status = i2cm->recblen == 1 ?
         i2c_master_read_byte(i2cm->cmd,i2cm->recbuf,ack) :
         i2c_master_read(i2cm->cmd,i2cm->recbuf,i2cm->recblen,ack);
   }
   if(status != ESP_OK)
   {
      baFree(i2cm->recbuf);
      i2cm->recbuf=0;
   }
   return pushEspRetVal(L,status, 0);
}


/* i2cm:commit([timeout]) */
static int I2CMaster_commit(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   i2c_master_stop(i2cm->cmd);
   ThreadMutex_release(soDispMutex);
   int status=i2c_master_cmd_begin(i2cm->port,i2cm->cmd,I2CWT(L,2));
   ThreadMutex_set(soDispMutex);
   i2c_cmd_link_delete(i2cm->cmd);
   i2cm->cmd=0;
   if(i2cm->direction == I2C_MASTER_READ && status == ESP_OK)
   {
      if(i2cm->recbuf)
      {
         lua_pushlstring(L, (char*)i2cm->recbuf, i2cm->recblen);
         baFree(i2cm->recbuf);
         i2cm->recbuf=0;
         return 1;
      }
      status=ESP_FAIL;
   }
   if(i2cm->recbuf)
   {
      baFree(i2cm->recbuf);
      i2cm->recbuf=0;
   }
   return pushEspRetVal(L,status,0);
}


/* Lua close */
static int I2CMaster_close(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_getUD(L);
   lua_pushboolean(L, i2cm->port >= 0); 
   if(i2cm->port >= 0)
   {
      if(i2cm->cmd)
         i2c_cmd_link_delete(i2cm->cmd);
      if(i2cm->recbuf)
         baFree(i2cm->recbuf);
      i2c_driver_delete(i2cm->port);
      i2cm->port=-1; 
   }
   return 1;
}


static const luaL_Reg i2cMasterLib[] = {
   {"start", I2CMaster_start},
   {"address", I2CMaster_address},
   {"write", I2CMaster_write},
   {"read", I2CMaster_read},
   {"commit", I2CMaster_commit},
   {"close", I2CMaster_close},
   {"__close", I2CMaster_close},
   {"__gc", I2CMaster_close},
   {NULL, NULL}
};


/* ic2.master(port, pinSDA, pinSCL, speed) */
static int li2cMaster(lua_State* L)
{
   i2c_port_t port=(i2c_port_t)luaL_checkinteger(L, 1); /* port */
   if(port < 0 || port >= I2C_NUM_MAX)
      luaL_error(L, "Port num. range err.");
   i2c_config_t i2cConfig = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = luaL_checkinteger(L, 2), /* pinSDA */
      .scl_io_num = luaL_checkinteger(L, 3),  /* pinSCL */
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = luaL_checkinteger(L, 4) /* speed */
   };
   i2c_param_config(port, &i2cConfig);
   i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0);
   LI2CMaster* i2cm = (LI2CMaster*)lNewUdata(
      L,sizeof(LI2CMaster),BAI2CMASTER,i2cMasterLib);
   i2cm->port = port;
   return 1;
}




/*********************************************************************
 *********************************************************************
                                  UART
 *********************************************************************
 *********************************************************************/

#define BALUART "UART"


typedef struct {
   QueueHandle_t uartQueue; /* Used by uartTask */
   uart_port_t port;
   int patternLen;
   int callbackRef;
   int jobInQueue;
   TaskHandle_t uartTaskHandle;
} LUart;


typedef struct {
   ThreadJob super;
   LUart* lu;
   uart_event_type_t etype;
} LUartJob;


static int Uart_lclose(lua_State* L);


/* This function runs in the context of a thread in the LThreadMgr.
 */
static void executeLuaUartCB(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   luaL_Buffer lb;
   char* buf;
   int len=0;
   int lArgs=0;

   LUartJob* ujob=(LUartJob*)jb;
   LUart* lu = ujob->lu;
   lua_State* L = jb->Lt;
   const char* emsg = 0;
   int luaStatus = LUA_OK;
   /* Push user's callback on the stack */
   balua_wkPush(L, lu->callbackRef);
   switch(ujob->etype)
   {
      case UART_BUFFER_FULL:
         emsg="full";
      case UART_FIFO_OVF:
         if(!emsg) emsg="overflow";
      case UART_FRAME_ERR:
         if(!emsg) emsg="frame";
      case UART_PARITY_ERR:
         if(!emsg) emsg="parity";
      case UART_DATA_BREAK:
         if(!emsg) emsg="databreak";
      case UART_BREAK:
         if(!emsg) emsg="break";
         lua_pushnil(L);
         lua_pushstring(L, emsg);
         luaStatus=lua_pcall(L, 2, 0, msgh);
         break; /* End fall through */

      case UART_PATTERN_DET:
         if(lu->patternLen)
         {
            len = uart_pattern_pop_pos(lu->port);
            if(len > 0)
            {
               luaL_buffinit(L, &lb);
               buf = luaL_prepbuffsize(&lb, len);
               uart_read_bytes(lu->port, buf, len, 0);
               luaL_pushresultsize(&lb, len);
               lArgs=1;
               len=lu->patternLen;
            }
            else
               len=0;
         }
         /* Fall through */
      case UART_DATA:
         if(0 == len)
         {
            size_t size;
            ESP_ERROR_CHECK(uart_get_buffered_data_len(lu->port, &size));
            len=(int)size;
         }
         if(len > 0)
         {
            luaL_buffinit(L, &lb);
            buf = luaL_prepbuffsize(&lb, len);
            uart_read_bytes(lu->port, buf, len, 0);
            luaL_pushresultsize(&lb, len);
            luaStatus=lua_pcall(L, lArgs+1, 0, msgh);
         }
         break;

      default:
   }
   lu->jobInQueue=FALSE;
   if(LUA_OK != luaStatus)
   {
      Uart_lclose(L);
   }
}


/* This high priority UART task waits for UART messages and sends the
 * messages to executeLuaUartCB() via LThreadMgr.
 */
static void uartTask(void *param)
{
   LUart* lu = (LUart*)param;
   for(;;)
   {
      uart_event_t event;
      if(xQueueReceive(lu->uartQueue, &event, portMAX_DELAY))
      {
         switch(event.type)
         {
            case UART_DATA: if(lu->jobInQueue) break;
            case UART_BREAK:
            case UART_DATA_BREAK:
            case UART_BUFFER_FULL:
            case UART_FIFO_OVF:
            case UART_FRAME_ERR:
            case UART_PARITY_ERR:
            case UART_PATTERN_DET:
               LUartJob* ujob=(LUartJob*)ThreadJob_lcreate(
                  sizeof(LUartJob),executeLuaUartCB);
               if( ! ujob )
                  baFatalE(FE_MALLOC,0);
               ujob->lu=lu;
               ujob->etype = event.type;
               ThreadMutex_set(soDispMutex);
               lu->jobInQueue=TRUE;
               LThreadMgr_run(&ltMgr, (ThreadJob*)ujob);
               ThreadMutex_release(soDispMutex);
               if(UART_BUFFER_FULL == event.type || UART_FIFO_OVF == event.type)
               {
                  uart_flush_input(lu->port);
                  xQueueReset(lu->uartQueue);
               }
            default:
         }
      }
   }
}


static LUart* Uart_getUD(lua_State* L)
{
   return (LUart*)luaL_checkudata(L,1, BALUART);
}


static LUart* Uart_checkUD(lua_State* L)
{
   LUart* o = Uart_getUD(L);
   if(o->port == UART_NUM_MAX)
      luaL_error(L,"CLOSED");
   return o;
}


/* uart:read([timeout]) */
static int Uart_read(lua_State* L)
{
   size_t size;
   LUart* o = Uart_checkUD(L);
   lua_Integer msSec = luaL_optinteger(L, 2, 0);
   ESP_ERROR_CHECK(uart_get_buffered_data_len(o->port,&size));
   if(size || msSec)
   {
      luaL_Buffer lb;
      if(0 == size)
         size=1024;
      luaL_buffinit(L, &lb);
      char* buf = luaL_prepbuffsize(&lb, size);
      ThreadMutex_release(soDispMutex);
      int len = uart_read_bytes(
         o->port,buf,size,msSec ? (TickType_t)(msSec/portTICK_PERIOD_MS) : 0);
      ThreadMutex_set(soDispMutex);
      if(len > 0)
      {
         luaL_pushresultsize(&lb, len);
         return 1;
      }
   }
   lua_pushnil(L);
   return 1;
}


/* uart:write(data) */
static int Uart_write(lua_State* L)
{
   size_t size;
   LUart* o = Uart_checkUD(L);
   const char* data = luaL_checklstring(L, 2, &size);
   ThreadMutex_release(soDispMutex);
   uart_write_bytes(o->port, data, size);
   ThreadMutex_set(soDispMutex);
   return 0;
}


/* uart:close() */
static int Uart_lclose(lua_State* L)
{
   LUart* o = Uart_getUD(L);
   if(o->port < UART_NUM_MAX)
   {
      uart_driver_delete(o->port);
      if(o->uartTaskHandle)
         vTaskDelete(o->uartTaskHandle);
      o->port=UART_NUM_MAX;
   }
   return 0;
}


static const luaL_Reg uartObjLib[] = {
   {"read", Uart_read},
   {"write", Uart_write},
   {"close", Uart_lclose},
   {"__close", Uart_lclose},
   {"__gc", Uart_lclose},
   {NULL, NULL}
};


/* esp32.uart(port [,config])
 */
static int luart(lua_State* L)
{
   uart_port_t port=(uart_port_t)lua_tointeger(L, 1);
   if(port < UART_NUM_0 || port > UART_NUM_MAX)
      luaL_argerror(L, 1, "Invalid port");
   lInitConfigTable(L, 2);
   lua_getfield(L, 2, "callback"); /* callback IX is now 3 */
   int hasCB = lua_isfunction(L, 3);
   int databits = (int)balua_getIntField(L, 2, "databits", 8);
   int baudrate = (int)balua_getIntField(L, 2, "baudrate",  9600);
   int rxbufsize = (int)balua_getIntField(L, 2, "rxbufsize",  1024);
   int txbufsize = (int)balua_getIntField(L, 2, "txbufsize",  1024);
   int txpin = (int)balua_getIntField(L, 2, "txpin", UART_PIN_NO_CHANGE);
   int rxpin = (int)balua_getIntField(L, 2, "rxpin", UART_PIN_NO_CHANGE);
   int rtspin = (int)balua_getIntField(L, 2, "rtspin", UART_PIN_NO_CHANGE);
   int ctspin = (int)balua_getIntField(L, 2, "ctspin", UART_PIN_NO_CHANGE);
   int stopbits = (int)balua_getIntField(L, 2, "stopbits", 1);
   const char* parity = balua_getStringField(L, 2, "parity", 0);
   const char* flowctrl = balua_getStringField(L, 2, "flowctrl", 0);

   /* The following is used if pattern enabled */
   const char* pattern = balua_getStringField(L, 2, "pattern", 0);
   int maxlen = (int)balua_getIntField(L, 2, "maxlen", 0);
   int chr_tout = (int)balua_getIntField(L, 2, "timeout", 9);
   int pre_idle = (int)balua_getIntField(L, 2, "preidle", 0);
   int post_idle = (int)balua_getIntField(L, 2, "postidle", 0);

   uart_config_t cfg = {
      .baud_rate = baudrate,
      .data_bits = databits - 5 + UART_DATA_5_BITS,
      .parity = parity ? ('O' == *parity ?UART_PARITY_ODD :
          UART_PARITY_EVEN) : UART_PARITY_DISABLE,
      .stop_bits = 1 == stopbits ? UART_STOP_BITS_1 :
         (2 == stopbits ? UART_STOP_BITS_2 : UART_STOP_BITS_1_5),
      .flow_ctrl = flowctrl ? ('R' == *flowctrl ? UART_HW_FLOWCTRL_RTS :
         (flowctrl[3] ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_CTS)) :
         UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
   };
   QueueHandle_t uartQueue=0;
   if(rxbufsize < 1024) rxbufsize=1024;
   if(txbufsize < 1024) txbufsize=1024;
   if(ESP_OK != uart_driver_install(
         port, rxbufsize, txbufsize, 10, hasCB ? &uartQueue : 0, 0))
   {
      throwInvArg(L,"install");
   }
   if(ESP_OK != uart_param_config(port, &cfg))
   {
      uart_driver_delete(port);
      throwInvArg(L,"config");
   }
   if(ESP_OK != uart_set_pin(port, txpin, rxpin, rtspin, ctspin))
   {
      uart_driver_delete(port);
      throwInvArg(L,"setpin");
   }
   if(hasCB && pattern)
   {
      uart_enable_pattern_det_baud_intr(
         port,pattern[0],(uint8_t)strlen(pattern),chr_tout,pre_idle,post_idle);
      if(maxlen)
         uart_pattern_queue_reset(port, maxlen);
   }
   LUart* lu = (LUart*)lNewUdata(L, sizeof(LUart), BALUART, uartObjLib);
   lu->port=port;
   lu->patternLen= hasCB && pattern ? strlen(pattern) : 0;
   if(hasCB)
   {
      lu->callbackRef=lReferenceCallback(L, lua_absindex(L, -1), 3);
      lu->uartQueue=uartQueue;
      xTaskCreate(
         uartTask,"uart",1024,lu,configMAX_PRIORITIES-1,&lu->uartTaskHandle);
   }
   return 1;
}


/*********************************************************************
 *********************************************************************
                             MISC
 *********************************************************************
 *********************************************************************/

static void
wifiScanCB(lua_State* L, const uint8_t* ssid, int rssi,
         const char* authmode,const char*  pchiper,
         const char* gcipher, int channel)
{
   ThreadMutex_set(soDispMutex);
   lua_createtable(L, 0, 6);
   lua_pushstring(L,(const char*)ssid);
   lua_setfield(L,-2,"ssid");
   lua_pushinteger(L,rssi);
   lua_setfield(L,-2,"rssi");
   lua_pushstring(L,authmode);
   lua_setfield(L,-2,"authmode");
   lua_pushstring(L,pchiper);
   lua_setfield(L,-2,"pchiper");
   lua_pushstring(L,gcipher);
   lua_setfield(L,-2,"gcipher");
   lua_pushinteger(L,channel);
   lua_setfield(L,-2,"channel");
   lua_rawseti(L, -2, (int)lua_rawlen(L, -2) + 1); 
   ThreadMutex_release(soDispMutex);
}


static int lwscan(lua_State* L)
{
   int print = balua_optboolean(L,1,FALSE);
   lua_settop(L,1);
   lua_newtable(L);
   ThreadMutex_release(soDispMutex);
   wifiScan(print, L, wifiScanCB);
   ThreadMutex_set(soDispMutex);
   return 1;
}

static int lwconnect(lua_State* L)
{
   if(lua_gettop(L) == 0)
   {
      return pushEspRetVal(L,wconnect(0,0),0);
   }
   const char* ssid = luaL_checkstring(L,1);
   const char* pwd = luaL_checkstring(L,2);
   return pushEspRetVal(L,wconnect(ssid, pwd),0);
}

static int lmac(lua_State* L)
{
   uint8_t base[6];
   esp_base_mac_addr_get(base);
   lua_pushlstring(L,(char*)base,6);
   return 1;
}



/*********************************************************************
 *********************************************************************
                             Install ESP32
 *********************************************************************
 *********************************************************************/


static const luaL_Reg esp32Lib[] = {
   {"gpio", lgpio},
   {"i2cmaster", li2cMaster},
   {"pwmtimer", lLedTimer},
   {"pwmchannel", lLedChannel},
   {"uart", luart},
   {"wscan", lwscan},
   {"wconnect", lwconnect},
   {"mac", lmac},
   {NULL, NULL}
};

static const luaL_Reg basLib[] = {
   {"mac", lmac}
};



void installESP32Libs(lua_State* L)
{
   ThreadMutex_constructor(&rMutex);
   soDispMutex = HttpServer_getMutex(ltMgr.server);
   eventBrokerQueue = xQueueCreate(20, sizeof(EventBrokerQueueNode));
   xTaskCreate(eventBrokerTask,"eventBroker",2048,0,configMAX_PRIORITIES-1,0);
   gpio_install_isr_service(0);
   activeGPOI = (LGPIO**)baMalloc(sizeof(void*)*GPIO_NUM_MAX);
   memset(activeGPOI, 0, sizeof(void*)*GPIO_NUM_MAX);
   luaL_newlib(L, esp32Lib);
   lua_setglobal(L,"esp32");
   lua_getglobal(L, "ba");
   luaL_setfuncs(L,basLib,0);
   lua_pop(L,1);
}
