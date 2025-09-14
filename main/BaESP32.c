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

LThreadMgr Documentation:
  LThreadMgr concept:
    https://realtimelogic.com/ba/doc/en/C/reference/html/md_en_C_md_LuaBindings.html#fullsolution
  LThreadMgr API:
    https://realtimelogic.com/ba/doc/en/C/reference/html/structThreadJob.html
  Introductory example:
    https://github.com/RealTimeLogic/BAS/blob/main/examples/xedge/src/AsynchLua.c
*/ 

#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_continuous.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include <driver/uart.h>
#include <driver/ledc.h>
#include <driver/pulse_cnt.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_encoder.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <esp_ota_ops.h>
#include "esp_idf_version.h"

#include "BaESP32.h"
#include "CfgESP32.h"

#define ECHK ESP_ERROR_CHECK


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
#define GPIO_QUEUE_SIZE 10 /* See executeLuaGpioCB() */

typedef struct LGPIO
{
   int callbackRef;
   gpio_num_t pin;
   int queueLen;
   gpio_mode_t mode;
   U8 queue[GPIO_QUEUE_SIZE]; /* holds GPIO level(s) high/low */
} LGPIO;

typedef struct
{
   ThreadJob super;
   gpio_num_t pin;
} GpioThreadJob;

/* Array of pointers with len GPIO_NUM_MAX */
LGPIO* activeGPIO[GPIO_NUM_MAX];


/*********************************************************************
 *********************************************************************
                           Misc functions
 *********************************************************************
 *********************************************************************/

static void dispatchThreadJob(gpio_num_t pin, ThreadJob_LRun lrun);

static int throwInvArg(lua_State* L, const char* type)
{
   return luaL_error(L,"Invalidarg %s", type); /* does not return; throws */
}

static int throwPinInUse(lua_State* L, int pin)
{
   return luaL_error(L,"Pin %d in use",pin);
}

static int pushErr(lua_State* L, const char* msg)
{
   lua_pushnil(L);
   lua_pushstring(L,msg);
   return 2;
}


int pushEspRetVal(lua_State* L, esp_err_t err, const char* msg, int throwOnInvArg)
{
   if(ESP_ERR_INVALID_ARG == err && throwOnInvArg)
      throwInvArg(L, msg ? msg : "");
   if(ESP_OK == err)
   {
      lua_pushboolean(L, TRUE);
      return 1;
   }
   const char* emsg=esp_err_to_name(err);
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

void* lNewUdata(lua_State *L, size_t size, const char *tname, const luaL_Reg *l)
{
   void* o = lua_newuserdatauv(L, size, 1);
   memset(o,0,size);
   if(luaL_newmetatable(L, tname))
   {  /*
        Create metatable and register functions:
      */
      lua_pushvalue(L, -1); /* Copy meta */
      lua_setfield(L, -2, "__index"); /* meta.__index=meta */
      /* set all functions in meta: for-loop -> meta[fname]=f */
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
   push any values on the stack.
*/
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
void lInitConfigTable(lua_State* L, int ix)
{
   if( ! lua_istable(L, ix) )
   {
      lua_settop(L,ix -1);
      lua_createtable(L, 0, 0); /* empty config table */
   }
   else
      lua_settop(L,ix);
}

QueueHandle_t eventBrokerQueue; /* Initialized in NetESP32.c */

/* This high-priority task waits for 'eventBrokerQueue' events and
 * dispatches them to a callback function running in the context of an
 * LThreadMgr thread.
 */
void eventBrokerTask(void *params)
{
   (void)params;
   for(;;)
   {
      EventBrokerQueueNode n;
      if(xQueueReceive(eventBrokerQueue, &n, portMAX_DELAY))
      {
         n.callback(n.arg);
      }
   }
}



/*********************************************************************
 *********************************************************************
                                 ADC
 *********************************************************************
 *********************************************************************/

#define BAADC "ADC"

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#define ADC_OUTPUT_TYPE             ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define ADC_GET_CHANNEL(p_data)     ((p_data)->type1.channel)
#define ADC_GET_DATA(p_data)        ((p_data)->type1.data)
#else
#define ADC_OUTPUT_TYPE             ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define ADC_GET_CHANNEL(p_data)     ((p_data)->type2.channel)
#define ADC_GET_DATA(p_data)        ((p_data)->type2.data)
#endif


struct LADC;
typedef int (*AdcLuaFilter)(lua_State* L, int msgh, struct LADC* adc);

typedef struct LADC
{
   int type; /* 0: closed, 1: one shot, 2: continuous */
   int pin; /* gpio */
   int callbackRef;
   adc_channel_t channel;
   adc_cali_handle_t caliHandle;
   union { /* Must be last, see buf below */
      struct {
         adc_oneshot_unit_handle_t handle;
      } oneShot;
      struct {
         AdcLuaFilter filter;
         adc_continuous_handle_t handle;
         SemaphoreHandle_t stopSem;
         uint16_t* blBuf;
         int blLen;
         int blBufIx;
         int callbackRef;
         size_t bufSize;
         uint8_t running;
         uint8_t overflow;
         uint8_t buf[1]; /* must be last, see lNewUdata i.e. malloc */
      } contin;
   } u;
} LADC;


/* It is an ESP-IDF requirement that the adc_continuous_start/stop
   functions run from the same task. We, therefore, use two callbacks
   for starting (adcStartContinuousCB) and stopping continuous
   mode. These two callbacks run in the context of the
   eventBrokerTask. This callback is triggered by ADC_close().
 */
static void adcStopContinuousCB(EventBrokerCallbackArg arg)
{
   LADC* adc = (LADC*)activeGPIO[arg.pin];
   baAssert(adc);
   if(adc)
   {
      activeGPIO[arg.pin]=0;
      adc_continuous_stop(adc->u.contin.handle);
      adc_continuous_deinit(adc->u.contin.handle);
      xSemaphoreGive(adc->u.contin.stopSem); /* Continue with ADC_close() */
   }
}


static void ADC_close(lua_State* L, LADC* o)
{
   if(o && o->type)
   {
      int type = o->type;
      o->type=0;
      if(1 == type)
      {
         ECHK(adc_oneshot_del_unit(o->u.oneShot.handle));
      }
      else
      {
         o->u.contin.stopSem = xSemaphoreCreateBinary();
         if( ! o->u.contin.stopSem ) baFatalE(FE_MALLOC,0);
         EventBrokerQueueNode n = {
            .callback=adcStopContinuousCB,.arg.pin=(gpio_num_t)o->pin};
         ThreadMutex_release(soDispMutex);
         xQueueSend(eventBrokerQueue, &n, portMAX_DELAY);
         xSemaphoreTake(o->u.contin.stopSem, portMAX_DELAY);
         ThreadMutex_set(soDispMutex);
         vSemaphoreDelete(o->u.contin.stopSem);
      }
      activeGPIO[o->pin]=0;
      if(o->caliHandle)
      {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
         ECHK(adc_cali_delete_scheme_curve_fitting(o->caliHandle));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
         ECHK(adc_cali_delete_scheme_line_fitting(o->caliHandle));
#endif
      }
   }
}


static LADC* ADC_getUD(lua_State* L)
{
   return (LADC*)luaL_checkudata(L,1,BAADC);
}


static LADC* ADC_checkUD(lua_State* L)
{
   LADC* o = ADC_getUD(L);
   if( ! o->type )
      luaL_error(L,"CLOSED");
   return o;
}


static int ADC_lclose(lua_State* L)
{
   ADC_close(L,ADC_getUD(L));
   return 0;
}

static int ADC_lread(lua_State* L)
{
   esp_err_t err;
   int val;
   LADC* o = ADC_checkUD(L);
   if(1 != o->type)
      luaL_error(L,"Continuous mode");
   err = adc_oneshot_read(o->u.oneShot.handle, o->channel, &val);
   if(ESP_OK != err)
      return pushEspRetVal(L, err, "read",FALSE);
   lua_pushinteger(L,val);
   if(o->caliHandle)
   {
      int volt;
      err = adc_cali_raw_to_voltage(o->caliHandle, val, &volt);
      if(ESP_OK != err)
         return pushEspRetVal(L, err, "calibrate",FALSE);
      lua_pushinteger(L,volt);
      return 2;
   }
   return 1;
}


static int adcDataFilter(lua_State* L, int msgh, LADC* adc)
{
   balua_wkPush(L, adc->callbackRef);
   size_t size=adc->u.contin.blLen*sizeof(uint16_t);
   lua_pushlstring(L, (char*)adc->u.contin.blBuf, size);
   return LUA_OK == lua_pcall(L, 1, 0, msgh) ? 0 : -1;
}


static int adcMeanFilter(lua_State* L, int msgh, LADC* adc)
{
   /* Calculate mean using Kahan summation */
   double sum = 0.0f;
   double c = 0.0f;
   int n=0;
   int stepSZ = adc->u.contin.blLen/20; /* Max 20 samples */
   if(stepSZ == 0) stepSZ=1;
   for(int i = 0; i < adc->u.contin.blLen; i+=stepSZ)
   {
      double t = sum;
      double y = adc->u.contin.blBuf[i] - c;
      sum = t + y;
      c = (t - sum) + y;
      n++;
   }
   uint32_t val = (uint32_t)(sum/n);
   balua_wkPush(L, adc->callbackRef);
   lua_pushinteger(L,val);
   if(adc->caliHandle)
   {
      int volt;
      esp_err_t err = adc_cali_raw_to_voltage(adc->caliHandle, val, &volt);
      if(ESP_OK == err)
         lua_pushinteger(L,volt);
      else
      {
         lua_pop(L,1);
         pushErr(L,esp_err_to_name(err));
      }
   }
   return LUA_OK == lua_pcall(L, adc->caliHandle ? 2 : 1, 0, msgh) ? 0 : -1;
}


/* Stage 3: This CB runs in the context of a thread in the LThreadMgr.
 */
static void adcExecuteLuaEventCB(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   LADC* adc = (LADC*)activeGPIO[((GpioThreadJob*)jb)->pin];
   if(adc)
   {
      lua_State* L = jb->Lt;
      while(adc->type)
      {
         uint32_t rLen;
         esp_err_t err =
            adc_continuous_read(adc->u.contin.handle, adc->u.contin.buf,
                                adc->u.contin.bufSize, &rLen, 0);
         if(ESP_OK == err)
         {
            for(int i = 0; i < rLen; i += SOC_ADC_DIGI_RESULT_BYTES)
            {
               adc_digi_output_data_t *p = (void*)&adc->u.contin.buf[i];
               adc->u.contin.blBuf[adc->u.contin.blBufIx++]=ADC_GET_DATA(p);
               if(adc->u.contin.blBufIx == adc->u.contin.blLen)
               {
                  adc->u.contin.blBufIx=0;
                  if(adc->u.contin.filter(L, msgh, adc))
                  {
                     ADC_close(L,adc);
                     return;
                  }
               }
            }
         }
         else if(ESP_ERR_TIMEOUT == err)
         {
            break;
         }
         else
         {
           L_err:
            /* Empty */
            while(ESP_OK == adc_continuous_read(
                     adc->u.contin.handle, adc->u.contin.buf,
                     adc->u.contin.bufSize, &rLen, 0));
            balua_wkPush(L, adc->callbackRef);
            pushErr(L,adc->u.contin.overflow ?
                    "overflow" : esp_err_to_name(err));
            if(LUA_OK != lua_pcall(L, 2, 0, msgh))
            {
               ADC_close(L,adc);
               return;
            }
            if(adc->u.contin.overflow)
            {
               while(ESP_OK == adc_continuous_read(
                        adc->u.contin.handle, adc->u.contin.buf,
                        adc->u.contin.bufSize, &rLen, 0));
               adc->u.contin.overflow=FALSE;
            }
            else
               break;
         }
         lua_settop(L,0);
         if(adc->u.contin.overflow)
            goto L_err;
         adc->u.contin.running=FALSE;
      }
      adc->u.contin.running=FALSE;
   }
}


/* Stage 2: This CB runs in the context of the eventBrokerTask.
 */
static void adcEventCB(EventBrokerCallbackArg arg)
{
   LADC* adc = (LADC*)activeGPIO[arg.pin];
   if(adc && ! adc->u.contin.running)
   {
      adc->u.contin.running = TRUE;
      dispatchThreadJob(arg.pin, adcExecuteLuaEventCB);
   }
}


/* Stage 2: This CB runs in the context of the eventBrokerTask.
 */
static void adcOverflowCB(EventBrokerCallbackArg arg)
{
   LADC* adc = (LADC*)activeGPIO[arg.pin];
   if(adc)
      adc->u.contin.overflow=TRUE;
}


/* Stage 1 : overflow
 */
static IRAM_ATTR bool adcOvfInterruptHandler(
   adc_continuous_handle_t handle, const adc_continuous_evt_data_t* edata,
   void* pin)
{
   BaseType_t hwakeup = pdFALSE;
   EventBrokerQueueNode n = {.callback=adcOverflowCB,.arg.pin=(gpio_num_t)pin};
   xQueueSendFromISR(eventBrokerQueue, &n, &hwakeup); /* Trigger adcEventCB() */
   return hwakeup == pdTRUE;
}


/* Stage 1 : conversion result
 */
static IRAM_ATTR bool adcConvDoneInterruptHandler(
   adc_continuous_handle_t handle, const adc_continuous_evt_data_t* edata,
   void* pin)
{
   BaseType_t hwakeup = pdFALSE;
   EventBrokerQueueNode n = {.callback=adcEventCB,.arg.pin=(gpio_num_t)pin};
   xQueueSendFromISR(eventBrokerQueue, &n, &hwakeup); /* Trigger adcEventCB() */
   return hwakeup == pdTRUE;
}


/* This CB runs in the context of the eventBrokerTask.
   See the comment in adcStopContinuousCB() for details on this construction.
 */
static void adcStartContinuousCB(EventBrokerCallbackArg arg)
{
   LADC* adc = (LADC*)activeGPIO[arg.pin];
   baAssert(adc);
   if(adc)
   {
      esp_err_t err=adc_continuous_start(adc->u.contin.handle);
      if(ESP_OK != err)
      { /* We use the 'overflow' event to signal that we could not start */
         adc->u.contin.overflow=TRUE;
         dispatchThreadJob(arg.pin, adcExecuteLuaEventCB);
      }
   }
}


static const luaL_Reg adcObjLib[] = {
   {"read", ADC_lread},
   {"close", ADC_lclose},
   {"__close", ADC_lclose},
   {"__gc", ADC_lclose},
   {NULL, NULL}
};

/*
  esp32.adc(unit, channel [, cfg])
 */
static int ladc(lua_State* L)
{
   esp_err_t err;
   int pin;
   adc_oneshot_unit_handle_t oneShotHandle;
   adc_continuous_handle_t contHandle;
   uint32_t bufSize=0; /* continuous mode */
   AdcLuaFilter filter=0;
   int blLen=0;
   int thresHigh = SOC_ADC_SAMPLE_FREQ_THRES_HIGH < 48000 ?
      SOC_ADC_SAMPLE_FREQ_THRES_HIGH : 48000;
   if(lua_gettop(L) == 0)
   {
      lua_pushinteger(L, SOC_ADC_SAMPLE_FREQ_THRES_LOW);
      lua_pushinteger(L, thresHigh);
      return 2;
   }
   adc_unit_t unitId = 1 == luaL_checkinteger(L, 1) ? ADC_UNIT_1 : ADC_UNIT_2;
   baAssert(0 == ADC_CHANNEL_0); /* next line fails if this is not true */
   adc_channel_t channel = (adc_channel_t)luaL_checkinteger(L, 2);
   lInitConfigTable(L, 3);
   lua_getfield(L, 3, "callback"); /* callback IX is now 4 */
   int hasCB = lua_isfunction(L, 4);
   const char* aStr = balua_getStringField(L, 3, "attenuation", "11db");
   adc_atten_t atten = '0' == *aStr ? ADC_ATTEN_DB_0 :
      ('2' == *aStr ? ADC_ATTEN_DB_2_5 :
       ('6' == *aStr ? ADC_ATTEN_DB_6 : ADC_ATTEN_DB_12));
   int volt = balua_getBoolField(L, 3, "volt", FALSE);
   adc_bitwidth_t bitwidth = (adc_bitwidth_t)balua_getIntField(
      L, 3, "bitwidth", ADC_BITWIDTH_DEFAULT);
   if(SOC_ADC_DIGI_MIN_BITWIDTH > bitwidth)
      bitwidth=SOC_ADC_DIGI_MIN_BITWIDTH;
   else if(SOC_ADC_DIGI_MAX_BITWIDTH < bitwidth)
      bitwidth=SOC_ADC_DIGI_MAX_BITWIDTH;
   err = adc_oneshot_channel_to_io(unitId, channel, &pin);
   if(ESP_OK != err)
      return pushEspRetVal(L, err, "channel_to_io",TRUE);
   if(activeGPIO[pin])
      throwPinInUse(L,pin);
   if(hasCB) /* continuous mode */
   {
      const char* f = balua_checkStringField(L, 3, "filter");
      filter = 'd' == *f ? adcDataFilter :
         ('m' == *f ? adcMeanFilter :
          (void*)throwInvArg(L, "filter"));

      /* frequency sample */
      uint32_t fs=balua_getIntField(L,3,"fs",SOC_ADC_SAMPLE_FREQ_THRES_LOW);
      if(SOC_ADC_SAMPLE_FREQ_THRES_LOW > fs) fs=SOC_ADC_SAMPLE_FREQ_THRES_LOW;
      else if(fs > thresHigh) fs=thresHigh;
      uint32_t minbl = fs / 100; /* max 100 Lua callbacks per second */
      /* block length i.e. number of samples, the conversion frame len */
      blLen=balua_checkIntField(L,3,"bl");
      if(blLen < minbl) blLen=minbl;

      /* The following calculations are based max 48K sampling.
         This should generate a max of 48K/480 = 100 messages second */
      bufSize = 480 * SOC_ADC_DIGI_RESULT_BYTES;
      /* Add 800 milliseconds of extra samples */
      uint32_t safetyBuf = (fs / 12) * SOC_ADC_DIGI_RESULT_BYTES;
      if(safetyBuf < 2*bufSize) safetyBuf=2*bufSize;
      adc_continuous_handle_cfg_t adcHandleConfig = {
         .max_store_buf_size = bufSize + safetyBuf,
         .conv_frame_size = bufSize,
      };
      err=adc_continuous_new_handle(&adcHandleConfig, &contHandle);
      if(ESP_OK != err) return pushEspRetVal(L, err, "new_handle",TRUE);
      adc_digi_pattern_config_t adcPattern[1]={
         {
            .atten = atten,
            .channel = channel,
            .unit = unitId,
            .bit_width = bitwidth
         }
      };
      adc_continuous_config_t adcConfig = {
         .sample_freq_hz = fs,
         .conv_mode = ADC_CONV_SINGLE_UNIT_1, /* Other options ? */
         .format = ADC_OUTPUT_TYPE,
         .pattern_num = 1,
         .adc_pattern = adcPattern
      };
      err=adc_continuous_config(contHandle, &adcConfig);
      if(ESP_OK == err)
      {
         adc_continuous_evt_cbs_t cbs = {
            .on_conv_done = adcConvDoneInterruptHandler,
            .on_pool_ovf = adcOvfInterruptHandler
         };
         err=adc_continuous_register_event_callbacks(
            contHandle,&cbs,(void*)pin);
      }
      if(ESP_OK != err) 
      {
         adc_continuous_deinit(contHandle);
         return pushEspRetVal(L, err, "continuous_config",TRUE);
      }
   }
   else /* Oneshot mode */
   {
      adc_oneshot_unit_init_cfg_t oneShotCfg = { .unit_id = unitId };
      err=adc_oneshot_new_unit(&oneShotCfg, &oneShotHandle);
      adc_oneshot_chan_cfg_t cfg = {
         .bitwidth = bitwidth,
         .atten = atten
      };
      if(ESP_OK == err)
         err=adc_oneshot_config_channel(oneShotHandle, channel, &cfg);
      if(ESP_OK != err)
      {
         adc_oneshot_del_unit(oneShotHandle);
         return pushEspRetVal(L, err, "oneshot",TRUE);
      }
   }
   LADC* adc = (LADC*)lNewUdata(L, sizeof(LADC)+bufSize+blLen*sizeof(uint16_t),
                                BAADC, adcObjLib);
   activeGPIO[pin]=(LGPIO*)adc; /* mark pin in use */
   if(hasCB)
   {
      adc->u.contin.filter=filter;
      adc->type=2;
      adc->u.contin.handle=contHandle;
      adc->u.contin.bufSize=bufSize;
      adc->u.contin.blLen = blLen;
      adc->u.contin.blBuf = (uint16_t*)(((char*)(adc+1)) + bufSize);
      /* Userdata at -1 and callback ix is 4 */
      adc->callbackRef=lReferenceCallback(L, lua_absindex(L,-1), 4);
      EventBrokerQueueNode n = {
         .callback=adcStartContinuousCB,.arg.pin=(gpio_num_t)pin};
      ThreadMutex_release(soDispMutex);
      xQueueSend(eventBrokerQueue, &n, portMAX_DELAY);
      ThreadMutex_set(soDispMutex);
   }
   else
   {
      adc->type=1;
      adc->u.oneShot.handle=oneShotHandle;
   }
   adc->pin=pin;
   adc->channel=channel;
   if(volt)
   {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
      adc_cali_curve_fitting_config_t caCfg1 = {
         .unit_id = unitId,
         .atten = atten,
         .bitwidth = bitwidth,
      };
      err = adc_cali_create_scheme_curve_fitting(&caCfg1, &adc->caliHandle);
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
      adc_cali_line_fitting_config_t caCfg2 = {
         .unit_id = unitId,
         .atten = atten,
         .bitwidth = bitwidth,
      };
      err = adc_cali_create_scheme_line_fitting(&caCfg2, &adc->caliHandle);
#endif
      if(ESP_OK != err)
         return pushEspRetVal(L, err, "calibrate",TRUE);
   }
   lua_pushinteger(L,pin);
   return 2;
}






/*********************************************************************
 *********************************************************************
                                 GPIO
 *********************************************************************
 *********************************************************************/

static void GPIO_close(lua_State* L, LGPIO* o)
{
   if(GPIO_NUM_MAX != o->pin)
   {
      gpio_reset_pin(o->pin);
      activeGPIO[o->pin]=0; /* should be atomic */
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
   LGPIO* gpio = activeGPIO[job->pin];
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
static void dispatchThreadJob(gpio_num_t pin, ThreadJob_LRun lrun)
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
static void gpioEventCB(EventBrokerCallbackArg arg)
{
   LGPIO* gpio;
   int queueLen=0;
   int level = gpio_get_level(arg.pin);

   ThreadMutex_set(&rMutex);
   gpio = activeGPIO[arg.pin];
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
      dispatchThreadJob(arg.pin,executeLuaGpioCB);
}


/* Send a GPIO event to the eventBrokerTask, which then
 * calls function gpioEventCB.
 */
static void IRAM_ATTR gpioInterruptHandler(void *arg)
{
   EventBrokerQueueNode n = {
      .callback=gpioEventCB,
      .arg.pin=(gpio_num_t)arg
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
   if( GPIO_MODE_OUTPUT == o->mode || GPIO_MODE_OUTPUT_OD == o->mode ||
       lua_isboolean(L, 2))
   {
      return pushEspRetVal(L,gpio_set_level(o->pin,balua_checkboolean(L, 2)),0,FALSE);
   }
   else
   {
      lua_pushboolean(L,gpio_get_level(o->pin));
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
   if(activeGPIO[pin])
      throwPinInUse(L,pin);
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
   activeGPIO[pin]=gpio;
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
                       PCNT:   Pulse Counter
 *********************************************************************
 *********************************************************************/

#define BAPCNT "PCNT"
#define ONREACH_QUEUE_SIZE 5

typedef struct
{
   pcnt_unit_handle_t unit;
   int callbackRef;
   gpio_num_t pin; // One of the GPIO pins used; We use it as index in activeGPIO 
   int noOfChannels;
   int qHead;
   int qTail;
   int lCbRunning; // LPCNT_onReachLuaCB running flag
   int onReachQueueVal[ONREACH_QUEUE_SIZE]; // holds counter values
   U8 onReachQueueCrossMode[ONREACH_QUEUE_SIZE]; // holds cross mode
   pcnt_channel_handle_t channels[1]; // Must be last
} LPCNT;

static LPCNT* LPCNT_getUD(lua_State* L)
{
   return (LPCNT*)luaL_checkudata(L,1,BAPCNT);
}

static LPCNT* LPCNT_checkUD(lua_State* L)
{
   LPCNT* o = LPCNT_getUD(L);
   if( ! o->callbackRef )
      luaL_error(L,"CLOSED");
   return o;
}

static int LPCNT_close(lua_State* L, LPCNT* o)
{
   if(o->noOfChannels)
   {
      esp_err_t status;
      pcnt_unit_stop(o->unit);
      pcnt_unit_disable(o->unit);
      for(int i=0 ; i < o->noOfChannels ; i++)
      {
         status = pcnt_del_channel(o->channels[i]);
         if(ESP_OK != status)
            HttpTrace_printf(5, "Cannot close PCNT channel %d", i, esp_err_to_name(status));
      }
      o->noOfChannels=0;
      status = pcnt_del_unit(o->unit);
      if(ESP_OK != status)
         HttpTrace_printf(5, "Cannot close PCNT unit", esp_err_to_name(status));
      activeGPIO[o->pin]=0; /* release */
   }
   return 0;
}

static int LPCNT_lclose(lua_State* L)
{
   return LPCNT_close(L, LPCNT_getUD(L));
}

static int LPCNT_start(lua_State* L)
{
   return pushEspRetVal(L,pcnt_unit_start(LPCNT_checkUD(L)->unit),0,FALSE);
}

static int LPCNT_stop(lua_State* L)
{
   return pushEspRetVal(L,pcnt_unit_stop(LPCNT_checkUD(L)->unit),0,FALSE);
}

static int LPCNT_count(lua_State* L)
{
   int count;
   esp_err_t status = pcnt_unit_get_count(LPCNT_checkUD(L)->unit, &count);
   if(ESP_OK != status)
      return pushEspRetVal(L,pcnt_unit_start(LPCNT_checkUD(L)->unit),0,FALSE);
   lua_pushinteger(L,count);
   return 1;
}

static int LPCNT_clear(lua_State* L)
{
   return pushEspRetVal(L,pcnt_unit_clear_count(LPCNT_checkUD(L)->unit),0,FALSE);
}


static const luaL_Reg pcntLib[] = {
   {"close", LPCNT_lclose},
   {"__close", LPCNT_lclose},
   {"__gc", LPCNT_lclose},
   {"start", LPCNT_start},
   {"stop", LPCNT_stop},
   {"count", LPCNT_count},
   {"clear", LPCNT_clear},
   {NULL, NULL}
};

static void
LPCNT_throwWatchMissing(lua_State* L, const char* missing)
{
   luaL_error(L, "watch missing %s",missing);
}

static esp_err_t
LPCNT_watch(lua_State* L, LPCNT* pcnt, int userdataIx)
{
   esp_err_t status = ESP_OK;
   int watchIx=balua_getTabField(L, 1, "watch");
   if(watchIx)
   {
      lua_getfield(L, watchIx, "callback");
      if( ! lua_isfunction(L, -1) )
         LPCNT_throwWatchMissing(L, "callback");
      if(pcnt)
         pcnt->callbackRef=lReferenceCallback(L,userdataIx,lua_absindex(L,-1));
      int pointsIx=balua_getTabField(L, watchIx, "points");
      if( ! pointsIx )
         LPCNT_throwWatchMissing(L, "points");
      int points;
      for(points = 1 ; ; points++) /* Iterate, start at Lua ix 1 */
      {
         lua_rawgeti(L, pointsIx, points);
         if(lua_isnil(L, -1))
            break;
         int point = (int)luaL_checkinteger(L, -1);
         if(pcnt)
         {
            status=pcnt_unit_add_watch_point(pcnt->unit, point);
            if(ESP_OK != status)
            {
               pushEspRetVal(L,status,"watch_point", FALSE);
               return status;
            }
         }
      }
   }
   return status;
}

static pcnt_channel_edge_action_t
LPCNT_getEdgeAction(lua_State* L, int tabIx, const char* key)
{
   const char* action=balua_checkStringField(L,tabIx,key);
   return 'H' == action[0] ? PCNT_CHANNEL_EDGE_ACTION_HOLD :
      ('I' == action[0] ? PCNT_CHANNEL_EDGE_ACTION_INCREASE :
       PCNT_CHANNEL_EDGE_ACTION_DECREASE);
}

static pcnt_channel_level_action_t
LPCNT_getLevelAction(lua_State* L, int tabIx, const char* key)
{
   const char* action=balua_checkStringField(L,tabIx,key);
   return 'K' == action[0] ?  PCNT_CHANNEL_LEVEL_ACTION_KEEP :
      ('I' == action[0] ? PCNT_CHANNEL_LEVEL_ACTION_INVERSE :
       PCNT_CHANNEL_LEVEL_ACTION_HOLD);
}


static esp_err_t
LPCNT_channels(lua_State* L,pcnt_unit_handle_t unit,
               int* noOfChannelsPtr,pcnt_channel_handle_t* channels, int* pinPtr)
{
   esp_err_t status = ESP_OK;
   int top=lua_gettop(L);
   int channelsIx=balua_getTabField(L, 1, "channels");
   if( ! channelsIx )
      luaL_error(L, "channels required");
   int noOfChannels;
   for(noOfChannels = 1 ; ; noOfChannels++) /* Iterate, start at Lua ix 1 */
   {
      lua_rawgeti(L, channelsIx, noOfChannels);
      if(lua_isnil(L, -1))
         break;
      int channelIx=lua_gettop(L);
      if(LUA_TTABLE != lua_type(L, channelIx))
         luaL_error(L,"Channel %d not a table",noOfChannels);
      int gpioIx=balua_getTabField(L, channelIx, "gpio");
      int actionIx=balua_getTabField(L, channelIx, "action");
      if(!gpioIx || !actionIx)
         luaL_error(L,"Channel %d missing %s",noOfChannels,gpioIx ? "action" : "gpio");
      pcnt_chan_config_t chanCfg = {
         .edge_gpio_num = (int)balua_checkIntField(L, gpioIx, "edge"),
         .level_gpio_num = (int)balua_checkIntField(L, gpioIx, "level")
      };
      /* Only check during phase 1, the throw phase */
      if( ! channels )
      {
         if(activeGPIO[chanCfg.edge_gpio_num])
            throwPinInUse(L,chanCfg.edge_gpio_num);
         if(activeGPIO[chanCfg.level_gpio_num])
            throwPinInUse(L,chanCfg.level_gpio_num);
         /* Select any GPIO we use. Used for activeGPIO[] lookup */ 
         *pinPtr=chanCfg.edge_gpio_num;
      }
      int edgeIx=balua_getTabField(L, actionIx, "edge");
      int levelIx=balua_getTabField(L, actionIx, "level");
      if(!edgeIx || !levelIx)
         luaL_error(L,"Channel %d missing %s",noOfChannels,edgeIx ? "level" : "edge");
      pcnt_channel_edge_action_t positive = LPCNT_getEdgeAction(L, edgeIx, "positive");
      pcnt_channel_edge_action_t negative = LPCNT_getEdgeAction(L, edgeIx, "negative");
      pcnt_channel_level_action_t high = LPCNT_getLevelAction(L, levelIx, "high");
      pcnt_channel_level_action_t low = LPCNT_getLevelAction(L, levelIx, "low");
      if(channels)
      {
         pcnt_channel_handle_t channel;
         status = pcnt_new_channel(unit, &chanCfg, &channel);
         if(ESP_OK == status)
         {
            channels[noOfChannels-1]=channel;
            status = pcnt_channel_set_edge_action(channel,positive,negative);
            if(ESP_OK == status)
            {
               status = pcnt_channel_set_level_action(channel,high,low);
               if(ESP_OK == status)
               {
                  lua_settop(L,channelIx-1);
                  continue;
               }
               else
                  pushEspRetVal(L,status,"level_action", FALSE);
            }
            else
               pushEspRetVal(L,status,"edge_action", FALSE);
         }
         else
            pushEspRetVal(L,status,"new_channel", FALSE);
         /* Iterate, start at C ix */
         for(--noOfChannels ; noOfChannels >= 0; noOfChannels--)
            pcnt_del_channel(channels[noOfChannels]);
         return status;
      }
      lua_settop(L,channelIx-1);
   }
   if(1 == noOfChannels)
      luaL_error(L, "channels empty");
   *noOfChannelsPtr=noOfChannels-1; /* from Lua Ix to C Ix */
   lua_settop(L,top);
   return ESP_OK;
}

/* 3 of 3:
   This CB runs in the context of a thread in the LThreadMgr.
 */
static void LPCNT_onReachLuaCB(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   GpioThreadJob* job = (GpioThreadJob*)jb;
   LPCNT* o = (LPCNT*)activeGPIO[job->pin];
   if(o)
   {
      lua_State* L = jb->Lt;
      /* This code, which can be interrupted by
       * LPCNT_onReachInterruptHandler, is designed such that its
       * operation should be safe. The code only updates the tail.
       */
      while(o->qHead != o->qTail)
      {
         o->lCbRunning=TRUE;
         /* Push user's callback on the stack */
         balua_wkPush(L, o->callbackRef);
         lua_pushinteger(L,o->onReachQueueVal[o->qTail]);
         lua_pushinteger(L,o->onReachQueueCrossMode[o->qTail]);
         if(LUA_OK != lua_pcall(L, 2, 0, msgh))
         {
            LPCNT_close(L, o);
            break;
         }
         lua_settop(L,1);
         int next = o->qTail+1;
         if(next >= ONREACH_QUEUE_SIZE)
            next=0;
         o->qTail = next; // Atomic with regards to LPCNT_onReachInterruptHandler
         o->lCbRunning=FALSE;
      }
   }
}


/* 2 of 3:
   This CB runs in the context of the eventBrokerTask.
 */
static void LPCNT_onReachCB(EventBrokerCallbackArg arg)
{
   if(activeGPIO[arg.pin])
      dispatchThreadJob(arg.pin,LPCNT_onReachLuaCB);
}


/* 1 of 3
 */
static bool LPCNT_onReachInterruptHandler(
   pcnt_unit_handle_t unit, const pcnt_watch_event_data_t* edata, void* pin)
{
   BaseType_t highTaskWakeup=pdFALSE;
   LPCNT* o=(LPCNT*)activeGPIO[(gpio_num_t)pin];
   /* This interrupt function has higher priority than step 3;
    * thus, the following appears as an atomic operation to LPCNT_onReachLuaCB
    */
   int next = o->qHead+1;
   if(next >= ONREACH_QUEUE_SIZE)
      next=0;
   if(next != o->qTail) // Not full
   {
      o->onReachQueueVal[o->qHead] = edata->watch_point_value;
      o->onReachQueueCrossMode[o->qHead] = edata->zero_cross_mode;
      o->qHead=next;
      if( ! o->lCbRunning )
      {
         EventBrokerQueueNode n = {.callback=LPCNT_onReachCB,.arg.pin=(gpio_num_t)pin};
         xQueueSendFromISR(eventBrokerQueue, &n, &highTaskWakeup);
         o->lCbRunning=TRUE;
      }
   }
   return highTaskWakeup == pdTRUE;
}


static int lLPCNT(lua_State* L)
{
   luaL_checktype(L, 1, LUA_TTABLE);
   pcnt_unit_config_t ucfg = {
      .high_limit = (int)balua_checkIntField(L, 1, "high"),
      .low_limit = (int)balua_checkIntField(L, 1, "low"),
      .flags.accum_count = balua_getBoolField(L, 1, "accumulator", FALSE)
   };
   int noOfChannels;
   int gpioPin=-1; /* One of the pins used. Used for activeGPIO[gpioPin] lookup */
   /* Validate input table and count channels. This may throw error message. */
   LPCNT_channels(L,0,&noOfChannels,0, &gpioPin);
   baAssert(gpioPin != -1);
   /* Validate optional watch table: throw on error */
   LPCNT_watch(L, 0, 0);

   LPCNT* pcnt = (LPCNT*)lNewUdata(
      L, sizeof(LPCNT)+noOfChannels*sizeof(pcnt_channel_handle_t),BAPCNT, pcntLib);
   memset(pcnt, 0, sizeof(LPCNT)); /* default vals for the struct */
   pcnt->pin=gpioPin;
   int userdataIx=lua_absindex(L,-1);

   /* Create and configure PCNT */
   esp_err_t status = pcnt_new_unit(&ucfg, &pcnt->unit);
   if(ESP_OK != status)
      return pushEspRetVal(L,status,"new_unit",TRUE);
   if(0 != balua_getIntField(L, 1, "glitch", 0))
   {
      pcnt_glitch_filter_config_t filterCfg = {
         .max_glitch_ns = (uint32_t)balua_getIntField(L, 1, "glitch", 0),
      };
      status=pcnt_unit_set_glitch_filter(pcnt->unit, &filterCfg);
      if(ESP_OK != status)
         pushEspRetVal(L,status,"glitch_filter",FALSE);
   }
   if(ESP_OK == status)
   {
      if(ESP_OK == LPCNT_channels(L,pcnt->unit,&pcnt->noOfChannels,pcnt->channels,&gpioPin))
      {
         baAssert(noOfChannels == pcnt->noOfChannels);
         if(ESP_OK == LPCNT_watch(L, pcnt, userdataIx))
         {
            pcnt_event_callbacks_t cbs = {
               .on_reach = LPCNT_onReachInterruptHandler,
            };
            pcnt_unit_register_event_callbacks(pcnt->unit, &cbs, (void *)gpioPin);
            pcnt_unit_enable(pcnt->unit);
            pcnt_unit_clear_count(pcnt->unit);
            activeGPIO[gpioPin]=(LGPIO*)pcnt; /* used for activeGPIO[] lookup */
            lua_settop(L,userdataIx);
            return 1; /* LPCNT userdata */
         }
         for( ; noOfChannels >= 0; noOfChannels--)
            pcnt_del_channel(pcnt->channels[noOfChannels]);
      }
   }
   pcnt_del_unit(pcnt->unit);
   pcnt->noOfChannels=0; /* Closed */
   return 2; /* Error values from pushEspRetVal */
}


/*********************************************************************
 *********************************************************************
                       PWM: LED Control (LEDC)
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
                           o->mode,o->channel, duty, hpoint),0,FALSE);
}


static int LLEDC_fade(lua_State* L)
{
   LLEDC* o = LLEDC_checkUD(L);
   if(!o->callbackRef)
      luaL_error(L,"No callback");
   uint32_t duty = (uint32_t)luaL_checkinteger(L, 2);
   uint32_t fadeTime = (uint32_t)luaL_checkinteger(L, 3);
   return pushEspRetVal(L,ledc_set_fade_time_and_start(
                           o->mode,o->channel,duty,fadeTime,LEDC_FADE_NO_WAIT),0,FALSE);
}


static void LLEDC_close(lua_State* L, LLEDC* o)
{
   if(o->running)
   {
      activeGPIO[o->pin]=0;
      ECHK(ledc_stop(o->mode,o->channel,0));
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
   LLEDC* led = (LLEDC*)activeGPIO[((GpioThreadJob*)jb)->pin];
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
static void ledEventCB(EventBrokerCallbackArg arg)
{
   if(activeGPIO[arg.pin])
      dispatchThreadJob(arg.pin, executeLuaLedEventCB);
}


/* Interrupt CB: send event to ledEventCB
 */
static IRAM_ATTR bool
ledInterruptHandler(const ledc_cb_param_t* param, void* arg)
{
   BaseType_t hwakeup = pdFALSE;
   if(param->event == LEDC_FADE_END_EVT)
   {
      EventBrokerQueueNode n = {
         .callback=ledEventCB,
         .arg.pin=(gpio_num_t)arg
      };
      xQueueSendFromISR(eventBrokerQueue, &n, &hwakeup);
   }
   return hwakeup == pdTRUE;
}


#if CONFIG_IDF_TARGET_ESP32S3
#define LEDC_HIGH_SPEED_MODE LEDC_SPEED_MODE_MAX
#endif
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
   if(activeGPIO[pin])
      throwPinInUse(L,pin);
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
      return pushEspRetVal(L,status,"channel_config",FALSE);
   status = ledc_set_duty_and_update(mode, channel, duty, hpoint);
   if(ESP_OK != status)
      return pushEspRetVal(L,status,"set_duty_and_update",FALSE);
   LLEDC* led = (LLEDC*)lNewUdata(L,sizeof(LLEDC),BALEDC,ledcObjLib);
   led->pin=pin;
   led->mode=mode;
   led->channel=channel;
   led->running=TRUE;
   activeGPIO[pin]=(LGPIO*)led;
   if(hasCB)
   {
      led->callbackRef=lReferenceCallback(L, lua_absindex(L,-1), 2);
      ledc_cbs_t cb = { .fade_cb = ledInterruptHandler };
      ECHK(ledc_cb_register(mode,channel,&cb,(void*)pin));
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
   return pushEspRetVal(L,ledc_timer_config(&cfg),0,TRUE);
}



/*********************************************************************
 *********************************************************************
                                 I2C
 *********************************************************************
 *********************************************************************/

#define BAI2CMASTER "I2CMASTER"         // Lua I2C master userdata type

// Struct to store I2C configurations, buffers, and handles
typedef struct
{
   i2c_master_bus_config_t bus_cfg;     // I2C bus configuration
   i2c_master_bus_handle_t bus_handle;  // Handle for the I2C bus
   i2c_device_config_t dev_cfg;         // I2C device configuration
   i2c_master_dev_handle_t dev_handle;  // Handle for the I2C device
} LI2CMaster;

/* write_buffer_t Structure
 * Structure used to manage data intended for I2C write operations.
 * - len  : Length of the data to be written (in bytes).
 * - byte : Stores a single byte of data if the input is a single integer.
 * - data : Pointer to the data buffer, either pointing to `byte` (for single-byte writes) 
 *          or a Lua string (for multi-byte writes).
 */
typedef struct
{
  size_t len;
  uint8_t byte;
  const uint8_t* data;
} write_buffer_t;

// Default timeout for I2C operations, 500ms unless specified in Lua
#define I2CWT(L,ix) ((TickType_t)luaL_optinteger(L,ix,500)/portTICK_PERIOD_MS)

// Get I2C master userdata from Lua
static LI2CMaster* I2CMaster_getUD(lua_State* L)
{
   return (LI2CMaster*)luaL_checkudata(L,1,BAI2CMASTER);
}

// Check if I2C master is initialized; if not, raise an error
static LI2CMaster* I2CMaster_checkUD(lua_State* L, int checkCmd)
{
   LI2CMaster* i2cm = I2CMaster_getUD(L);
   if(i2cm->bus_handle == 0)
   {
      luaL_error(L, "Closed");
   }
   return i2cm;
}

/* I2CMaster_addDevice(i2cm, address)
 * Configures or reconfigures an I2C device on the bus with the specified address.
 * 
 * Parameters:
 *   i2cm   : Pointer to the LI2CMaster structure, containing the bus and device configuration.
 *   address: I2C address of the device to add.
 * 
 * Logic:
 *   - Checks if the device with the specified address is already configured on the bus.
 *   - If `dev_handle` exists but the address differs, it removes the previous device handle 
 *     using `i2c_master_bus_rm_device`.
 *   - If the address is new, it updates the `device_address` in `dev_cfg` and adds the new device 
 *     to the bus with `i2c_master_bus_add_device`, setting `dev_handle`.
 * 
 * Returns:
 *   ESP_OK on success, or an error code if unable to add or reconfigure the device.
 */
static int I2CMaster_addDevice(LI2CMaster* i2cm, uint16_t address)
{
   esp_err_t ret_val = ESP_OK;
   
   // If a device with a different address is already configured, remove it
   if(i2cm->dev_handle && (i2cm->dev_cfg.device_address != address))
   {
      ret_val = i2c_master_bus_rm_device(i2cm->dev_handle);  
   }

   // Set new device address if necessary, then add it to the bus
   if((ret_val == ESP_OK) && (i2cm->dev_cfg.device_address != address))
   {
      i2cm->dev_cfg.device_address = address;     
      ret_val = i2c_master_bus_add_device(i2cm->bus_handle, &i2cm->dev_cfg, &i2cm->dev_handle);
   }
  
   return ret_val;
}

/* I2CMaster_getWriteData(L, index, wbuf)
 * Populates the write_buffer_t structure with data for I2C write operations.
 * Determines if the Lua input at the given index is a single integer or a string.
 * 
 * Parameters:
 *   L     : The Lua state.
 *   index : The index in the Lua stack to retrieve the data.
 *   wbuf  : Pointer to the write_buffer_t structure to store the result.
 * 
 * Logic:
 *   - If the Lua input is an integer, it is treated as a single byte, stored in `byte`, and `data` points to `byte`.
 *   - If the Lua input is a string, it is treated as a multi-byte buffer, and `data` points to the string data.
 */
static void I2CMaster_getWriteData(lua_State* L, int index, write_buffer_t* wbuf)
{
   if(lua_isinteger(L, index))
   {
      wbuf->byte = (uint8_t)lua_tointeger(L, index);
      wbuf->data = &wbuf->byte;
      wbuf->len = 1;
   }
   else
   {
      wbuf->data = (uint8_t*)luaL_checklstring(L, index, &wbuf->len);
   }
}

/* i2cm:probe(address, [timeout])
 * Lua API: Checks for the presence of a device at the specified I2C address.
 * 
 * This function sends a START condition followed by the device address in read mode (R).
 * If the device acknowledges (ACK) and the probe is successful, the function returns ESP_OK.
 * A STOP condition is issued after the probe attempt.
 * 
 * Important:
 *   - Ensure pull-up resistors are connected to the SDA and SCL lines when using this function.
 *   - If the function returns ESP_ERR_TIMEOUT and the timeout was correctly specified, 
 *     it is likely due to missing or insufficient pull-up resistors.
 * 
 * Parameters:
 *   address (int)         : The I2C address of the device to probe.
 *   timeout (optional int): Timeout for the probe operation in ms (default 500ms).
 * 
 * Returns:
 *   Boolean true if the device is detected (ESP_OK), or false with an error code otherwise.
 */
static int I2CMaster_probe(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   
   // Retrieve optional timeout from Lua, defaulting to I2CWT if not provided
   TickType_t timeout = I2CWT(L, 3);
   
   // Get the address from Lua
   uint16_t address = (uint16_t) luaL_checkinteger(L, 2);
   
   ThreadMutex_release(soDispMutex);
   esp_err_t ret_val = i2c_master_probe(i2cm->bus_handle, address, timeout); 
   ThreadMutex_set(soDispMutex);
   
   return pushEspRetVal(L, ret_val, 0, FALSE);
}

/* i2cm:write(address, data, [timeout])
 * Lua API: Writes data to the specified address on the I2C device.
 * Params:
 *   address (int): The I2C address of the device.
 *   data (int or string): Data to write; single byte or Lua string for multi-byte.
 *   timeout (optional int): Timeout for the write operation in ms (default 500ms).
 * Returns:
 *   Boolean true on success, or false and error code if unsuccessful.
 */
static int I2CMaster_write(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   uint16_t address = (uint16_t)luaL_checkinteger(L, 2);
   TickType_t timeout = I2CWT(L, 4);

   // Configure the device address on the bus
   esp_err_t ret_val = I2CMaster_addDevice(i2cm, address);

   if(ret_val == ESP_OK)
   {
      write_buffer_t wbuf;
      I2CMaster_getWriteData(L, 3, &wbuf);
      
      // Ensure data is available to write
      if(wbuf.len == 0)
      {
         throwInvArg(L, "no write buffer");
      }

      // Transmit data
      ThreadMutex_release(soDispMutex);
      ret_val = i2c_master_transmit(i2cm->dev_handle, wbuf.data, wbuf.len, timeout); 
      ThreadMutex_set(soDispMutex);
   }
   
   return pushEspRetVal(L, ret_val, 0, FALSE);
}

/* i2cm:read(address, len, [timeout])
 * Lua API: Reads a specified number of bytes from the I2C device.
 * Params:
 *   address (int): The I2C address of the device.
 *   len (int): Number of bytes to read.
 *   timeout (optional int): Timeout for the read operation in ms (default 500ms).
 * Returns:
 *   Lua string with the data read, or nil and error code if unsuccessful.
 */
static int I2CMaster_read(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   uint16_t address = (uint16_t)luaL_checkinteger(L, 2);
   size_t recblen = (size_t)luaL_checkinteger(L, 3);
   TickType_t timeout = I2CWT(L, 4);

   // Configure the device address
   esp_err_t ret_val = I2CMaster_addDevice(i2cm, address);

   if(ret_val == ESP_OK)
   {
      if(recblen == 0)
      {
         throwInvArg(L, "no recbuf");
      }

      uint8_t* recbuf = (uint8_t*)baLMalloc(L, recblen);

      if(recbuf)
      {
         ThreadMutex_release(soDispMutex);
         ret_val = i2c_master_receive(i2cm->dev_handle, recbuf, recblen, timeout);
         ThreadMutex_set(soDispMutex);

         if(ret_val == ESP_OK)
         {
            lua_pushlstring(L, (char*)recbuf, recblen);
            baFree(recbuf);
            return 1;
         }
         
         baFree(recbuf);
      }
      else
      {
         ret_val = ESP_ERR_NO_MEM;
      }
   }
  
   return pushEspRetVal(L, ret_val, 0, FALSE);
}

 /* i2cm:readfrom(address, register, len, [timeout])
 * Lua API: Reads data from a specific register on the I2C device.
 * 
 * This function sends a write command to specify the register, followed immediately by a read command 
 * to retrieve data from the device, without issuing a STOP condition between the write and read operations.
 * This is suited for devices that require a repeated START condition when reading registers.
 * 
 * Parameters:
 *   address (int)   : The I2C address of the device.
 *   register (int)  : The register address to read from.
 *   len (int)       : Number of bytes to read from the register.
 *   timeout (optional int): Timeout for the operation in ms (default 500ms).
 * 
 * Returns:
 *   Lua string with the data read, or nil and error code if unsuccessful.
 */
static int I2CMaster_readfrom(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   uint16_t address = (uint16_t)luaL_checkinteger(L, 2);
   TickType_t timeout = I2CWT(L, 5);

   esp_err_t ret_val = I2CMaster_addDevice(i2cm, address);

   if(ret_val == ESP_OK)
   {
      write_buffer_t wbuf;
      I2CMaster_getWriteData(L, 3, &wbuf);
      size_t recblen = (size_t)luaL_checkinteger(L, 4);

      if(recblen == 0 || wbuf.len == 0)
      {
         throwInvArg(L, "invalid buffer length");
      }

      uint8_t* recbuf = (uint8_t*)baLMalloc(L, recblen);

      if(recbuf)
      {
         ThreadMutex_release(soDispMutex);
         ret_val = i2c_master_transmit_receive(i2cm->dev_handle, wbuf.data, wbuf.len, recbuf, recblen, timeout);
         ThreadMutex_set(soDispMutex);

         if(ret_val == ESP_OK)
         {
            lua_pushlstring(L, (char*)recbuf, recblen);
            baFree(recbuf);
            return 1;
         }
         
         baFree(recbuf);
      }
      else
      {
         ret_val = ESP_ERR_NO_MEM;
      }
   }

   return pushEspRetVal(L, ret_val, 0, FALSE);
}

/* i2cm:close()
 * Lua API: Closes the I2C connection and releases all resources.
 * Params: None.
 * Returns:
 *   Boolean true on success.
 */
static int I2CMaster_close(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_getUD(L);
   
   if(i2cm->dev_handle) 
   {
      i2c_master_bus_rm_device(i2cm->dev_handle);  
      i2cm->dev_handle = 0;
   }
   
   if(i2cm->bus_handle)
   {
      activeGPIO[i2cm->bus_cfg.sda_io_num] = 0; 
      activeGPIO[i2cm->bus_cfg.scl_io_num] = 0;
      i2c_del_master_bus(i2cm->bus_handle);
      i2cm->bus_handle = 0;
   }

   return 1;
}

static const luaL_Reg i2cMasterLib[] = {
   {"probe", I2CMaster_probe},
   {"readfrom", I2CMaster_readfrom},
   {"write", I2CMaster_write},
   {"read", I2CMaster_read},   
   {"close", I2CMaster_close},
   {"__close", I2CMaster_close},
   {"__gc", I2CMaster_close},
   {NULL, NULL}
};

/* li2cMaster(port, pinSDA, pinSCL, speed)
 * Initializes a new I2C master object and configures the I2C bus.
 * 
 * Parameters:
 *   port (int)      : The I2C port number, e.g., 0 or 1.
 *   pinSDA (int)    : The GPIO number for I2C Serial Data (SDA) line.
 *   pinSCL (int)    : The GPIO number for I2C Serial Clock (SCL) line.
 *   speed (int)     : The clock speed in Hz for the I2C bus.
 * 
 * Returns:
 *   Lua object      : A new I2C master instance or an error if initialization fails.
 * 
 * Description:
 *   - Checks that the specified port number is valid.
 *   - Allocates and initializes an `LI2CMaster` object to manage the I2C bus.
 *   - Configures the I2C bus with the specified pins, speed, and default settings.
 *   - Attempts to create a new master bus handle with the configuration.
 *   - Sets the GPIO pins as active to prevent reuse, and registers the I2C bus handle.
 */
static int li2cMaster(lua_State* L)
{
  // Get the I2C port from Lua and validate its range
  i2c_port_t port = (i2c_port_t)luaL_checkinteger(L, 1); 
  
  if((port < 0) || (port >= I2C_NUM_MAX))
  {
     luaL_error(L, "Port num. range err.");
  }
   // Allocate memory for the new I2C master object and register it in Lua
  LI2CMaster* i2cm = (LI2CMaster*)lNewUdata(L, sizeof(LI2CMaster), BAI2CMASTER, i2cMasterLib);   
  // Configure the I2C bus parameters (clock source, port, SDA, SCL, and pullup resistors)
  i2cm->bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  i2cm->bus_cfg.i2c_port = port;
  i2cm->bus_cfg.sda_io_num = luaL_checkinteger(L, 2);  // pinSDA
  i2cm->bus_cfg.scl_io_num = luaL_checkinteger(L, 3);  // pinSCL
  i2cm->bus_cfg.glitch_ignore_cnt = 7;
  i2cm->bus_cfg.flags.enable_internal_pullup = 1;
  // Configure the I2C device parameters (7-bit address, initial address 0, and bus speed)
   i2cm->dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  i2cm->dev_cfg.device_address = 0;
  i2cm->dev_cfg.scl_speed_hz = luaL_checkinteger(L, 4);// speed 
  // Create a new I2C master bus with the configured settings
  esp_err_t ret_val = i2c_new_master_bus(&i2cm->bus_cfg, &i2cm->bus_handle);
  // Check if bus creation was successful; if not, return an error
  if(ESP_OK != ret_val)
  {
     return pushEspRetVal(L, ret_val, "i2cm", TRUE);
  }
  // Check if SDA pin is already in use, raise error if so
  if(activeGPIO[i2cm->bus_cfg.sda_io_num])
  {
     throwPinInUse(L, i2cm->bus_cfg.sda_io_num);
  }
  // Check if SCL pin is already in use, raise error if so
  if(activeGPIO[i2cm->bus_cfg.scl_io_num])
  {
     throwPinInUse(L, i2cm->bus_cfg.scl_io_num);
  }
  
  activeGPIO[i2cm->bus_cfg.sda_io_num] = (LGPIO*)i2cm;
  activeGPIO[i2cm->bus_cfg.scl_io_num] = (LGPIO*)i2cm;

  return 1; // Return the new I2C master object to Lua
}

/*********************************************************************
 *********************************************************************
                                  RMT
 *********************************************************************
 *********************************************************************/

#define BARMTTX "RMTT"
#define BARMTRX "RMTR"

struct LRmtTx;

typedef struct
{
   struct LRmtTx* tx;
} LRmtBase;

typedef struct
{
   LRmtBase super;
   rmt_receive_config_t cfg;
   rmt_channel_handle_t rxChannel;
   gpio_num_t pin;
   int callbackRef;
   rmt_symbol_word_t* symBuf;
   size_t symSize;
   size_t receivedSymbols;
   BaBool overflow;
   BaBool pendingReceive;
} LRmtRx;


typedef struct LRmtTx
{
   LRmtBase super;
   LRmtRx* rx; /* Set when linked on the same GPIO */
   rmt_channel_handle_t txChannel;
   rmt_encoder_handle_t txEncoder;
   BaBool enabled;
   gpio_num_t pin;
   int callbackRef;
   int transQueueBufHead;
   int transQueueBufTail;
   int transQueueBufLen;
   int transQueueBufRef[1]; /* Must be last */
} LRmtTx;

static void LRmtRx_onRxComplete(EventBrokerCallbackArg arg);

#define LRmtTx_getUD(L) (LRmtTx*)luaL_checkudata(L,1,BARMTTX)
#define LRmtRx_getUD(L) (LRmtRx*)luaL_checkudata(L,1,BARMTRX)

static LRmtTx* LRmtTx_checkUD(lua_State* L)
{
   LRmtTx* o = LRmtTx_getUD(L);
   if( ! o->txChannel )
      luaL_error(L,"CLOSED");
   return o;
}

static LRmtRx* LRmtRx_checkUD(lua_State* L)
{
   LRmtRx* o = LRmtRx_getUD(L);
   if( ! o->rxChannel )
      luaL_error(L,"CLOSED");
   return o;
}

/* Note: does not pop the value */
static lua_Integer LRmtTx_getIntAtIx(lua_State *L, int index, lua_Integer n)
{
   lua_rawgeti(L, index, n);
   lua_Integer i=lua_tointeger(L, -1);
   return i;
}

static void copyTuple2Symbol(lua_State *L, int index, rmt_symbol_word_t* symbol)
{
   index=lua_absindex(L, index);
   symbol->level0=(uint16_t)LRmtTx_getIntAtIx(L,index,1);
   symbol->duration0 = (uint16_t)LRmtTx_getIntAtIx(L, index, 2);
   symbol->level1=(uint16_t)LRmtTx_getIntAtIx(L,index,3);
   symbol->duration1 = (uint16_t)LRmtTx_getIntAtIx(L, index, 4);
   lua_pop(L,4);
}


static int LRmtTx_transmit(lua_State* L)
{
   LRmtTx* o = LRmtTx_checkUD(L);
   if( ! o->enabled )
   {
      luaL_error(L,"Not enabled");
   }
   int nextTransQueueBufHead = o->transQueueBufHead+1;
   if(nextTransQueueBufHead >= o->transQueueBufLen)
      nextTransQueueBufHead=0;
   if(nextTransQueueBufHead == o->transQueueBufTail)
      luaL_error(L, "Transmit queue full");
   luaL_checktype(L, 2, LUA_TTABLE);
   luaL_checktype(L, 3, LUA_TTABLE);
   rmt_transmit_config_t cfg = {
      .loop_count = (int)balua_getIntField(L,2,"loop", 0),
      .flags.eot_level = (uint32_t)balua_getIntField(L,2,"eot", 0),
      .flags.queue_nonblocking = TRUE
   };
   lua_settop(L, 3);

   /* Calculate rmt_symbol_word_t array length */
   int bufLen=0;
   for(int ix=1 ; ix <= lua_rawlen(L,3) ; ix++)
   {
      lua_geti(L,3,ix);
      if(LUA_TTABLE != lua_type(L, -1))
         luaL_error(L,"Index %d not a tuple",ix);
      int tlen=(int)lua_rawlen(L, -1);
      if(4 != tlen)
      {
        L_err:
         luaL_error(L,"Invalid tuple at Index %d",ix);
      }
      lua_rawgeti(L, -1, 1);
      if(LUA_TBOOLEAN == lua_type(L, -1))
      {
         lua_rawgeti(L, -2, 4);
         if(LUA_TTABLE != lua_type(L, -1)) goto L_err;
         bufLen += (int)lua_rawlen(L,-1) * 8;
         lua_pop(L,1);
      }
      else
      {
         bufLen++;
      }
      lua_pop(L,2);
   }
   baAssert(3 == lua_gettop(L));

   /* Create the rmt_symbol_word_t array */
   rmt_symbol_word_t* symbols = (rmt_symbol_word_t*)lua_newuserdatauv(
      L,sizeof(rmt_symbol_word_t) * bufLen, 0);
   rmt_symbol_word_t* symbolPtr = symbols;
   for(int ix=1; ix <= lua_rawlen(L,3) ; ix++)
   {
      lua_geti(L,3,ix);
      lua_rawgeti(L, -1, 1);
      if(LUA_TBOOLEAN == lua_type(L, -1))
      {
         rmt_symbol_word_t lowBit,highBit;
         int msb = lua_toboolean(L, -1);
         lua_rawgeti(L, -2, 2);
         if(LUA_TTABLE != lua_type(L, -1)) goto L_err;
         copyTuple2Symbol(L, -1, &lowBit);
         lua_rawgeti(L, -3, 3);
         if(LUA_TTABLE != lua_type(L, -1)) goto L_err;
         copyTuple2Symbol(L, -1, &highBit);
         lua_rawgeti(L, -4, 4);
         if(LUA_TTABLE != lua_type(L, -1)) goto L_err;
         for(int byteIx=1 ; byteIx <= lua_rawlen(L,-1) ; byteIx++)
         {
            uint8_t byte = (uint8_t)LRmtTx_getIntAtIx(L, -1, byteIx);
            lua_pop(L,1);
            if(msb)
            {
               for(unsigned int mask = 0x80 ; mask > 0 ; mask >>= 1)
               {
                  *symbolPtr = mask & byte ? highBit : lowBit;
                  symbolPtr++;
               }
            }
            else
            {
               for(unsigned int mask = 1 ; mask <= 0x80 ; mask <<= 1)
               {
                  *symbolPtr = mask & byte ? highBit : lowBit;
                  symbolPtr++;
               }
            }
         }
         lua_pop(L,3);
      }
      else
      {
         copyTuple2Symbol(L, -2, symbolPtr);
         symbolPtr++;
      }
      lua_pop(L,2);
   }
   baAssert((symbolPtr - symbols) == bufLen);
   esp_err_t status=rmt_transmit(
      o->txChannel,o->txEncoder,symbols,sizeof(rmt_symbol_word_t)*bufLen,&cfg);
   if(ESP_OK == status)
   {
      lua_pushvalue(L, 4); /* rmt_symbol_word_t array */
      baAssert(LUA_TUSERDATA == lua_type(L, -1));
      /* We must reference buffer used by rmt_transmit() to prevent GC
       * since rmt_transmit() keeps a reference to the buffer and does
       * not copy it. We release this buffer in
       * LRmtTx_executeOnTxComplete() when the data has been
       * transmitted.
       */
      o->transQueueBufRef[o->transQueueBufHead]=luaL_ref(L, LUA_REGISTRYINDEX);
      o->transQueueBufHead = nextTransQueueBufHead;
   }
   return pushEspRetVal(L, status, "transmit", TRUE);
}


static int LRmtTx_enable(lua_State* L)
{
   LRmtTx* o = LRmtTx_checkUD(L);
   int status;
   if(o->enabled)
   {
      status=ESP_ERR_INVALID_ARG;
   }
   else
   {
      status=rmt_enable(o->txChannel);
      if(ESP_OK == status)
         o->enabled=TRUE;
   }
   return pushEspRetVal(L, status, NULL, TRUE);
}


static int LRmtTx_disableAndReleaseBuf(LRmtTx* o,lua_State* L)
{
   int status=rmt_disable(o->txChannel);
   int ix=o->transQueueBufTail;
   while(ix != o->transQueueBufHead)
   {
      luaL_unref(L, LUA_REGISTRYINDEX, o->transQueueBufRef[ix]);
      ix++;
      if(ix >= o->transQueueBufLen)
         ix=0;
   }
   o->transQueueBufHead=o->transQueueBufTail=0;
   return status;
}


static int LRmtTx_disable(lua_State* L)
{
   int status;
   LRmtTx* o = LRmtTx_checkUD(L);
   if(o->enabled)
   {
      status=LRmtTx_disableAndReleaseBuf(o,L);
      if(ESP_OK == status)
         o->enabled=FALSE;
   }
   else
   {
      status=ESP_ERR_INVALID_ARG;
   }
   return pushEspRetVal(L,status,NULL,TRUE);
}

static void LRmtTx_del(LRmtTx* o, lua_State* L)
{
   if(o)
   {
      if(o->txChannel)
      {
         LRmtTx_disableAndReleaseBuf(o,L);
         rmt_del_encoder(o->txEncoder);
         rmt_del_channel(o->txChannel);
         o->txChannel=0;
         activeGPIO[o->pin]=0;
      }
      o->rx=NULL;
   }
}


static int LRmtTx_close(lua_State* L)
{
   LRmtTx_del(LRmtTx_getUD(L), L);
   return 0;
}


/* This CB runs in the context of a thread in the LThreadMgr.
 */
static void LRmtTx_executeOnTxComplete(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   LRmtBase* base = (LRmtBase*)activeGPIO[((GpioThreadJob*)jb)->pin];
   if(base && base->tx)
   {
      LRmtTx* o = base->tx;
      //printf("executeOnTxComplete %d : %d\n",o->transQueueBufTail,o->transQueueBufHead); // PATCH TEST CODE
      if(o->transQueueBufTail != o->transQueueBufHead) /* not empty */
      {
         lua_State* L = jb->Lt;
         /* Release rmt_symbol_word_t array memory lock */
         luaL_unref(L, LUA_REGISTRYINDEX, o->transQueueBufRef[o->transQueueBufTail]);
         int next = o->transQueueBufTail+1;
         if(next >= o->transQueueBufLen)
            next=0;
         o->transQueueBufTail = next;
         if(o->callbackRef)
         {
            /* Push user's callback on the stack */
            balua_wkPush(L, o->callbackRef);
            if(LUA_OK != lua_pcall(L, 0, 0, msgh))
               LRmtTx_del(o, L);
         }
      }
   }
}


/* This CB runs in the context of the eventBrokerTask.
 */
static void LRmtTx_onTxComplete(EventBrokerCallbackArg arg)
{
   if(activeGPIO[arg.pin])
      dispatchThreadJob(arg.pin, LRmtTx_executeOnTxComplete);
}


static bool IRAM_ATTR LRmtTx_interruptHandler(
   rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *arg)
{
   BaseType_t hwakeup = pdFALSE;
   LRmtBase* base = (LRmtBase*)activeGPIO[(gpio_num_t)arg];
   if(base && base->tx)
   {
      LRmtRx* rx = base->tx->rx;
      if(rx && rx->pendingReceive)
      {
         if(ESP_OK != rmt_receive(rx->rxChannel,rx->symBuf,rx->symSize,&rx->cfg))
         {
            /* Should not be possible, but if it happens, signal error
             * to RX callback as follows:
             */
            EventBrokerQueueNode n = {
               .callback=LRmtRx_onRxComplete,
               .arg.pin=(gpio_num_t)arg
            };
            rx->receivedSymbols=0;
            rx->overflow = TRUE;
            xQueueSendFromISR(eventBrokerQueue, &n, &hwakeup);
         }
      }
      EventBrokerQueueNode n = {
         .callback=LRmtTx_onTxComplete,
         .arg.pin=(gpio_num_t)arg
      };
      xQueueSendFromISR(eventBrokerQueue, &n, &hwakeup);
   }
   return hwakeup == pdTRUE;
};


static const luaL_Reg rmtTxLib[] = {
   {"transmit", LRmtTx_transmit},
   {"enable", LRmtTx_enable},
   {"disable", LRmtTx_disable},
   {"close", LRmtTx_close},
   {"__close", LRmtTx_close},
   {"__gc", LRmtTx_close},
   {NULL, NULL}
};


static int LRmtTx_create(lua_State* L)
{
   luaL_checktype(L, 1, LUA_TTABLE);
   rmt_tx_channel_config_t txCfg = {
      .gpio_num = (uint32_t)balua_checkIntField(L,1,"gpio"),
      .clk_src = RMT_CLK_SRC_DEFAULT, // select clock source
      .resolution_hz = (uint32_t)balua_checkIntField(L,1,"resolution"),
      .mem_block_symbols = (uint32_t)balua_getIntField(L,1,"mem",64),
      .trans_queue_depth = (uint32_t)balua_getIntField(L,1,"queue",4),
      .intr_priority = (int)balua_getIntField(L,1,"priority",0),
      .flags.invert_out=balua_getBoolField(L,1,"invert", FALSE),
      .flags.with_dma=balua_getBoolField(L,1,"dma", FALSE),
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
      .flags.io_od_mode=balua_getBoolField(L,1,"opendrain",FALSE);
#endif
   };
      
   /* fixme: analyze this; ESP-IDF assert rmt_ll_tx_set_channel_clock_div if larger */
   if(txCfg.resolution_hz > 80000000)
      return pushEspRetVal(L,ESP_ERR_INVALID_ARG,"resolution",TRUE);
   if(txCfg.trans_queue_depth < 1)
      return pushEspRetVal(L,ESP_ERR_INVALID_ARG,"queue",TRUE);
   if(txCfg.gpio_num < GPIO_NUM_0 || txCfg.gpio_num >= GPIO_NUM_MAX)
      luaL_argerror(L, 1, "Invalid GPIO pin");
   LRmtRx* rx=NULL;
   bool hasRx = LUA_TUSERDATA == lua_type(L, 2);
   LRmtTx* tx = (LRmtTx*)lNewUdata(
      L,sizeof(LRmtTx)+sizeof(int)*txCfg.trans_queue_depth,BARMTTX,rmtTxLib);
   memset(tx,0,sizeof(LRmtTx));
   ((LRmtBase*)tx)->tx=tx;
   if(activeGPIO[txCfg.gpio_num])
   {
      if(hasRx)
      {
         rx = (LRmtRx*)luaL_checkudata(L,2,BARMTRX);
         if((LRmtRx*)activeGPIO[txCfg.gpio_num] == rx && rx->rxChannel)
         {
            ((LRmtBase*)rx)->tx=tx;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
            txCfg.flags.io_loop_back=TRUE;
#endif
            goto L_ok;
         }
      }
      throwPinInUse(L,txCfg.gpio_num);
   }
  L_ok:
   tx->transQueueBufLen=txCfg.trans_queue_depth;
   tx->pin=txCfg.gpio_num;
   esp_err_t status=rmt_new_tx_channel(&txCfg, &tx->txChannel);
   if(ESP_OK == status)
   {
// Open-drain now via GPIO API (io_od_mode removed)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
      if(balua_getBoolField(L,1,"opendrain",FALSE))
      {
         gpio_od_enable(tx->pin);
      }
#endif
      lua_getfield(L, 1, "dutycycle");
      if(lua_isnumber(L, -1))
      {
         rmt_carrier_config_t cCfg = {
            .duty_cycle = (float)lua_tonumber(L, -1),
            .frequency_hz = (uint32_t)balua_checkIntField(L,1,"frequency"),
            .flags.polarity_active_low = balua_getBoolField(L,1,"polaritylow", FALSE)
         };
         status=rmt_apply_carrier(tx->txChannel, &cCfg);
         if(ESP_OK != status)
            goto L_err;
      }
      lua_pop(L,1);
      rmt_tx_event_callbacks_t cb = {
         .on_trans_done = LRmtTx_interruptHandler
      };
      status=rmt_tx_register_event_callbacks(tx->txChannel, &cb, (void*)txCfg.gpio_num);
      if(ESP_OK == status)
      {
         rmt_copy_encoder_config_t cfg={};
         status=rmt_new_copy_encoder(&cfg, &tx->txEncoder);
         if(ESP_OK == status)
         {
            lua_getfield(L, 1, "callback");
            if(lua_isfunction(L, -1))
               tx->callbackRef=lReferenceCallback(L, lua_absindex(L,-2), lua_absindex(L,-1));
            lua_pop(L,1);
            if(rx)
               tx->rx=rx;
            else
               activeGPIO[tx->pin]=(LGPIO*)tx;
            return 1;
         }
      }
     L_err:
      rmt_del_channel(tx->txChannel);
   }
   return pushEspRetVal(L,status,NULL,TRUE);
}

static void LRmtRx_del(lua_State* L, LRmtRx* o)
{
   if(o->rxChannel)
   {
      activeGPIO[o->pin]=0;
      rmt_disable(o->rxChannel);
      rmt_del_channel(o->rxChannel);
      o->rxChannel=0;
      if(o->symBuf)
         baFree(o->symBuf);
      LRmtTx_del(((LRmtBase*)o)->tx, L);
   }
}


/* This CB runs in the context of a thread in the LThreadMgr.
 */
static void LRmtRx_executeOnRxComplete(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   LRmtRx* o = (LRmtRx*)activeGPIO[((GpioThreadJob*)jb)->pin];
   if(o && o->symBuf)
   {
      lua_State* L = jb->Lt;
      rmt_symbol_word_t* symPtr=o->symBuf;
      // Prevent GC from calling LRmtRx_close, thus terminating this object.
      lua_gc(L,LUA_GCSTOP);
      lua_createtable(L,0,o->receivedSymbols);
      for(size_t ix=1 ; ix <= o->receivedSymbols; ix++,symPtr++)
      {
         lua_createtable(L,0,4);
         lua_pushinteger(L,symPtr->level0); lua_rawseti(L,-2, 1);
         lua_pushinteger(L,symPtr->duration0); lua_rawseti(L,-2, 2);
         lua_pushinteger(L,symPtr->level1); lua_rawseti(L,-2, 3);
         lua_pushinteger(L,symPtr->duration1); lua_rawseti(L,-2, 4);
         lua_rawseti(L,-2, ix);
      }
      balua_wkPush(L, o->callbackRef);
      lua_rotate(L, -2, 1);
      lua_pushboolean(L, o->overflow);
      baFree(o->symBuf);
      o->symBuf=0;
      o->pendingReceive=o->overflow=FALSE;
      lua_gc(L,LUA_GCRESTART);
      if(LUA_OK != lua_pcall(L, 2, 0, msgh))
         LRmtRx_del(L, o);
   }
}


/* This CB runs in the context of the eventBrokerTask.
 */
static void LRmtRx_onRxComplete(EventBrokerCallbackArg arg)
{
   if(activeGPIO[arg.pin])
      dispatchThreadJob(arg.pin, LRmtRx_executeOnRxComplete);
}


static bool IRAM_ATTR LRmtRx_interruptHandler(
   rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* edata, void* arg)
{
   BaseType_t hwakeup = pdFALSE;
   LRmtRx* o = (LRmtRx*)activeGPIO[(gpio_num_t)arg];
   if(o && o->symBuf)
   {
      EventBrokerQueueNode n = {
         .callback=LRmtRx_onRxComplete,
         .arg.pin=(gpio_num_t)arg
      };
      o->receivedSymbols=edata->num_symbols;
      o->overflow = !edata->flags.is_last;
      xQueueSendFromISR(eventBrokerQueue, &n, &hwakeup);
   }
   return hwakeup == pdTRUE;
}


static int LRmtRx_receive(lua_State* L)
{
   esp_err_t status;
   LRmtRx* o = LRmtRx_checkUD(L);
   if(o->symBuf)
      luaL_error(L,"Busy");
   memset(&o->cfg, 0 , sizeof(o->cfg));
   o->cfg.signal_range_min_ns = (uint32_t)balua_checkIntField(L,2,"min");
   o->cfg.signal_range_max_ns = (uint32_t)balua_checkIntField(L,2,"max");
   o->symSize = (size_t)balua_getIntField(L,2,"len",512) * sizeof(rmt_symbol_word_t);
   BaBool defer = balua_getBoolField(L,2,"defer", FALSE);
   o->symBuf = (rmt_symbol_word_t*)baLMalloc(L, o->symSize);
   if(o->symBuf)
   {
       /* if tx and rx linked */
      if(((LRmtBase*)o)->tx && defer)
      {
         status=ESP_OK;
         o->pendingReceive=TRUE;
      }
      else
      {
         status=rmt_receive(o->rxChannel, o->symBuf, o->symSize, &o->cfg);
         if(ESP_OK != status)
         {
            baFree(o->symBuf);
            o->symBuf=0;
         }
      }
   }
   else
      status=ESP_ERR_NO_MEM;
   return pushEspRetVal(L,status, NULL, TRUE);

}


static int LRmtRx_close(lua_State* L)
{
   LRmtRx* o = LRmtRx_getUD(L);
   if(o)
      LRmtRx_del(L,o);
   return 0;
}



static const luaL_Reg rmtRxLib[] = {
   {"receive", LRmtRx_receive},
   {"close", LRmtRx_close},
   {"__close", LRmtRx_close},
   {"__gc", LRmtRx_close},
   {NULL, NULL}
};


static int LRmtRx_create(lua_State* L)
{
   luaL_checktype(L, 1, LUA_TTABLE);
   rmt_rx_channel_config_t cfg = {
      .gpio_num = (uint32_t)balua_checkIntField(L,1,"gpio"),
      .clk_src = RMT_CLK_SRC_DEFAULT, // select clock source
      .resolution_hz = (uint32_t)balua_checkIntField(L,1,"resolution"),
      .mem_block_symbols = (uint32_t)balua_getIntField(L,1,"mem",64),
      .intr_priority = (int)balua_getIntField(L,1,"priority",0),
      .flags.invert_in=balua_getBoolField(L,1,"invert", FALSE),
      .flags.with_dma=balua_getBoolField(L,1,"dma", FALSE),
   };
   if(cfg.gpio_num < GPIO_NUM_0 || cfg.gpio_num >= GPIO_NUM_MAX)
      luaL_argerror(L, 1, "Invalid GPIO pin");
   if(activeGPIO[cfg.gpio_num])
      throwPinInUse(L,cfg.gpio_num);
   LRmtRx* rx = (LRmtRx*)lNewUdata(L,sizeof(LRmtRx),BARMTRX,rmtRxLib);
   memset(rx,0,sizeof(LRmtRx));
   rx->pin=cfg.gpio_num;
   lua_getfield(L, 1, "callback");
   if(!lua_isfunction(L, -1))
      luaL_error(L,"Callback required");
   rx->callbackRef=lReferenceCallback(L, lua_absindex(L,-2), lua_absindex(L,-1));
   lua_pop(L,1);
   esp_err_t status=rmt_new_rx_channel(&cfg, &rx->rxChannel);
   if(ESP_OK == status)
   {
      rmt_rx_event_callbacks_t cbs = {
         .on_recv_done = LRmtRx_interruptHandler,
      };
      status=rmt_rx_register_event_callbacks(rx->rxChannel, &cbs, (void*)cfg.gpio_num);
      if(ESP_OK == status)
      {
         status=rmt_enable(rx->rxChannel);
         if(ESP_OK == status)
         {
            activeGPIO[rx->pin]=(LGPIO*)rx;
            return 1;
         }
      }
   }
   return pushEspRetVal(L,status,NULL,TRUE);
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
   TaskHandle_t uartTaskHandle;
} LUart;


typedef struct {
   ThreadJob super;
   LUart* lu;
   uart_event_type_t etype;
} LUartJob;

static int Uart_close(LUart* o);


/* This function runs in the context of a thread in the LThreadMgr.
 */
static void executeLuaUartCB(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   luaL_Buffer lb;
   char* buf;
   int len=0;
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
         switch(ujob->etype)
         {
            case UART_BUFFER_FULL:
            case UART_FIFO_OVF:
            case UART_FRAME_ERR:
            case UART_PARITY_ERR:
               uart_flush_input(lu->port);
            default:
         }
         break; /* End fall through */

      case UART_PATTERN_DET:
         if(lu->patternLen && lu->port < UART_NUM_MAX)
         {
            len = uart_pattern_pop_pos(lu->port);
            if(len > 0)
            {
               luaL_buffinit(L, &lb);
               buf = luaL_prepbuffsize(&lb, len);
               uart_read_bytes(lu->port, buf, len, 0);
               luaL_pushresultsize(&lb, len);
               /* Discard pattern */
               luaL_buffinit(L, &lb);
               buf = luaL_prepbuffsize(&lb, lu->patternLen);
               uart_read_bytes(lu->port, buf, lu->patternLen, 0);
               luaL_pushresultsize(&lb, 0);
               lua_pop(L,1);
               goto L_execLua;
            }
            if(len < 0)
            {
               uart_flush_input(lu->port);
               lua_pushnil(L);
               lua_pushliteral(L, "overflow");
               luaStatus=lua_pcall(L, 2, 0, msgh);
               break;
            }
            break;
         }
         /* Fall through */
      case UART_DATA:
         size_t size;
         if(lu->port < UART_NUM_MAX)
         {
            uart_get_buffered_data_len(lu->port, &size);
            len=(int)size;
            if(len > 0)
            {
               luaL_buffinit(L, &lb);
               buf = luaL_prepbuffsize(&lb, len);
               uart_read_bytes(lu->port, buf, len, 0);
               luaL_pushresultsize(&lb, len);
              L_execLua:
               lua_pushboolean(L,UART_PATTERN_DET == ujob->etype);
               luaStatus=lua_pcall(L, 2, 0, msgh);
            }
            else
            {
               lua_pushnil(L);
               lua_pushliteral(L, "IO err");
               luaStatus=lua_pcall(L, 2, 0, msgh);
            }
         }
         break;

      default:
   }
   if(LUA_OK != luaStatus)
   {
      Uart_close(lu);
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
         LUartJob* ujob=(LUartJob*)ThreadJob_lcreate(
            sizeof(LUartJob),executeLuaUartCB);
         if( ! ujob )
            baFatalE(FE_MALLOC,0);
         ujob->lu=lu;
         ujob->etype = event.type;
         ThreadMutex_set(soDispMutex);
         LThreadMgr_run(&ltMgr, (ThreadJob*)ujob);
         ThreadMutex_release(soDispMutex);
         if(UART_BUFFER_FULL == event.type || UART_FIFO_OVF == event.type)
         {
            uart_flush_input(lu->port);
            xQueueReset(lu->uartQueue);
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
   if(o->callbackRef)
      return luaL_error(L,"RX callback");
   lua_Integer msSec = luaL_optinteger(L, 2, 0);
   ECHK(uart_get_buffered_data_len(o->port,&size));
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

/* uart:txsize(data) */
static int Uart_txsize(lua_State* L)
{
   size_t size;
   uart_get_tx_buffer_free_size(Uart_checkUD(L)->port, &size);
   lua_pushinteger(L,size);
   return 1;
}

static int Uart_close(LUart* o)
{
   if(o->port < UART_NUM_MAX)
   {
      uart_driver_delete(o->port);
      if(o->uartTaskHandle)
         vTaskDelete(o->uartTaskHandle);
      o->port=UART_NUM_MAX;
   }
   return 0;
}

/* uart:close() */
static int Uart_lclose(lua_State* L)
{
   Uart_close(Uart_getUD(L));
   return 0;
}


static const luaL_Reg uartObjLib[] = {
   {"read", Uart_read},
   {"write", Uart_write},
   {"txsize",Uart_txsize},
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
   int rs485 = balua_getBoolField(L, 2, "rs485", FALSE);

   /* The following is used if pattern enabled */
   const char* pattern = balua_getStringField(L, 2, "pattern", 0);
   int maxlen = (int)balua_getIntField(L, 2, "maxlen", 0);
   int chr_tout = (int)balua_getIntField(L, 2, "timeout", 0);
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
   if(rs485)
      uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX);
   if(hasCB)
   {
      if(pattern)
      {
         if(ESP_OK != uart_enable_pattern_det_baud_intr(
               port,pattern[0],(uint8_t)strlen(pattern),chr_tout,pre_idle,post_idle))
         {
            uart_driver_delete(port);
            throwInvArg(L,"pattern");
         }
         if(maxlen)
         {
            esp_err_t status=uart_pattern_queue_reset(port, maxlen);
            if(ESP_OK != status)
            {
               uart_driver_delete(port);
               return pushEspRetVal(L, status, "queue_reset", TRUE);
            }
         }
      }
      else if(chr_tout > 0)
      {
         if(ESP_OK != uart_set_rx_timeout(port, chr_tout))
         {
            uart_driver_delete(port);
            throwInvArg(L,"timeout");
         }
      }
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

static int
pushApInfo(lua_State* L, const uint8_t* ssid, int rssi,
         const char* authmode,const char*  pchiper,
         const char* gcipher, int channel)
{
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
   return 1;
}

static void
wifiScanCB(lua_State* L, const uint8_t* ssid, int rssi,
         const char* authmode,const char*  pchiper,
         const char* gcipher, int channel)
{
   ThreadMutex_set(soDispMutex);
   pushApInfo(L,ssid,rssi,authmode,pchiper,gcipher,channel);
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

/**
 * @brief Connect to a WiFi or wired network by providing the required configuration parameters.
 *
 * @param network A string representing the network adapter type. Valid options are "wifi", "W5500", and "DP83848".
 * @param cfg     A required configuration table with specific parameters for each adapter. 
 *                Omit the cfg table argument to disconnect from a network.
 *
 * @return Number of return values pushed to the Lua stack.
 *
 * @note The configuration parameters provided in the cfg table are stored persistently in NVRAM 
 *       if the ESP32 successfully connects to the network. These parameters will be used 
 *       to automatically connect to the network when the ESP32 restarts.
 */
static int lnetconnect(lua_State* L)
{
netConfig_t cfg = {0};
   
   // Set the network adapter from Lua string argument.
   strcpy(cfg.adapter, (char*)luaL_checkstring(L, 1));
   
   // Disconnects the network when the configuration table argument is not provided. 
   if(lua_gettop(L) == 1)
   {
      strcpy(cfg.adapter, "close");   
   }
   // Load the parameters for wifi.
   else if(!strcmp("wifi", cfg.adapter))
   {
      luaL_checktype(L, 2, LUA_TTABLE);
      strcpy(cfg.ssid, balua_checkStringField(L, 2, "ssid"));
      strcpy(cfg.password, balua_checkStringField(L, 2, "pwd"));
   }
   // Load the parameters for Ethernet by SPI.
   else if(netIsAdapterSpi(cfg.adapter))
   {
      cfg.spi.hostId = (int)balua_checkIntField(L, 2, "spi");
      cfg.spi.clk = (int)balua_checkIntField(L, 2, "clk");
      cfg.spi.mosi = (int)balua_checkIntField(L, 2, "mosi");
      cfg.spi.miso = (int)balua_checkIntField(L, 2,"miso");
      cfg.spi.cs = (int)balua_checkIntField(L, 2, "cs");
      cfg.spi.irq = (int)balua_checkIntField(L, 2, "irq");
      cfg.spi.freq = (int)balua_checkIntField(L, 2, "freq");
      cfg.phyRstPin = balua_getIntField(L, 2, "rst", -1);
   }
   // Load the parameters for Ethernet by RMII (only ESP32 legacy devices).
   else if(netIsAdapterRmii(cfg.adapter))
   {
      cfg.phyRstPin = (int)balua_checkIntField(L, 2, "rst");
      cfg.phyMdioPin = (int)balua_checkIntField(L, 2, "mdio");
      cfg.phyMdcPin = (int)balua_checkIntField(L, 2, "mdc");
   }
   else 
   {
      luaL_error(L,"Unknown adapter '%s'", cfg.adapter);
   }
   HttpTrace_printf(0, "\nExecuting network '%s' command\n",cfg.adapter);
   ThreadMutex_release(soDispMutex);
   Thread_sleep(1000);
   ThreadMutex_set(soDispMutex);
   // Call the netConnect function and push the return value to the Lua stack.
   return pushEspRetVal(L, netConnect(&cfg), 0, TRUE);
}
 
/**
 * @brief Set the log level for the ESP32 system log.
 *
 * This function allows you to configure the log level for the ESP32 system log.
 * The log level determines which log messages are displayed, with options ranging
 * from "none" (no logs) to "verbose" (all logs).
 *
 * @param level A string representing the desired log level. Valid options are:
 *        - "none": No log messages will be displayed.
 *        - "error": Only error messages will be displayed.
 *        - "warn": Error and warning messages will be displayed.
 *        - "info": Error, warning, and informational messages will be displayed.
 *        - "debug": Error, warning, informational, and debug messages will be displayed.
 *        - "verbose": All log messages, including verbose debug messages, will be displayed.
 *
 * @return Number of values pushed to the Lua stack (always 0 in this case).
 *
 * @note The maximum log level is configured in the menuconfig settings.
 * If an invalid log level is provided, or if the requested log level exceeds the
 * maximum level set in menuconfig, an error will be raised with a detailed message.
 */ 
static int lloglevel(lua_State* L)
{
   static const char* levelNames[] =  {
      "none",
      "error",
      "warn",
      "info",
      "debug",
      "verbose"
   };
   const char* levelName = luaL_checkstring(L, 1);
   esp_log_level_t level=ESP_LOG_NONE;
   for(int i=0 ;  i < sizeof(levelNames)/sizeof(levelNames[0]) ; i++,level++)
   {
      if(strcmp(levelName, levelNames[i]) == 0)
      {
         break;
      }
   }
   if(level > ESP_LOG_VERBOSE) 
   {
      luaL_error(L, "Invalid log level '%s', choose from none|error|warn|info|debug|verbose\n", levelName);
   }
   else if(level > CONFIG_LOG_MAXIMUM_LEVEL) 
   {
      luaL_error(L, "Can't set log level to %s; max level limited in menuconfig to %s. "
                 "Please increase CONFIG_LOG_MAXIMUM_LEVEL in menuconfig.\n",
                 levelName, levelNames[CONFIG_LOG_MAXIMUM_LEVEL]);
   }
   else 
   {
      esp_log_level_set("*", level);
   }
   return 0;
}

static int lapinfo(lua_State* L)
{
   wifi_ap_record_t ap;
   esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
   if(err == ESP_OK)
   {
      const char* pciphers;
      const char* gcipher=wifiCipherType(
         ap.pairwise_cipher, ap.group_cipher, false, &pciphers);
      return pushApInfo(L,ap.ssid,ap.rssi,wifiAuthMode(ap.authmode, FALSE),
                        pciphers,gcipher,ap.primary);
   }
   return pushEspRetVal(L,err,0,FALSE);
}


static int lmac(lua_State* L)
{
   uint8_t base[6];
   esp_base_mac_addr_get(base);
   lua_pushlstring(L,(char*)base,6);
   return 1;
}


static int lerase(lua_State* L)
{
   const esp_partition_t *fat_partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
   if(fat_partition == NULL)
   {
      printf("FAT partition not found.\n");
   }
   else
   {
      esp_err_t err = esp_partition_erase_range(
         fat_partition, 0, fat_partition->size);
      if(err == ESP_OK)
      {
         printf("FAT partition erased.\n");
         esp_restart();
      }
      else
         printf("Failed to erase FAT partition. Error code: %d\n", err);
   }
   return 0;
}

static int lexecute(lua_State* L)
{
   const char* cmd = luaL_checkstring(L,1);
   if(cmd[0] == 'e') /* erase */
      lerase(L);
   else if(cmd[0] == 'r') /* restart */
      esp_restart();
   else if(cmd[0] == 'k') /* killmain */
      manageConsole(false);
#if CONFIG_mDNS_ENABLED
   else if(cmd[0] == 'm') /* mdns */
   {
      size_t len;
      const char* name = luaL_checklstring(L,2,&len);
      if(len >= 80) /* See main.c: startMdnsService */
         goto L_err;
      mDnsCfg((char*)name);
   }
#endif
   else
   {
#if CONFIG_mDNS_ENABLED
     L_err:
#endif
      luaL_argerror(L, 1, cmd);
   }
   return 0;
}


static int lCRC(lua_State* L)
{
   const char* type = luaL_checkstring(L,1);
   size_t size;
   const uint8_t* data = (const uint8_t*)luaL_checklstring(L,2,&size);
   if( ! strcmp("modbus", type) )
   {
      uint16_t crc = 0xFFFF; // Initial CRC value
      for(size_t i = 0; i < size; ++i)
      {
         crc ^= data[i]; // XOR byte into least significant byte of crc
         for (int j = 0; j < 8; j++)
         { // Loop over each bit
            if ((crc & 0x0001) != 0)
            { // If the LSB is set
               crc >>= 1; // Shift right and XOR with polynomial
               crc ^= 0xA001;
            }
            else
            {
               crc >>= 1; // Just shift right
            }
         }
      }
      if(lua_isnumber(L,3))
      {
         lua_pushboolean(L,luaL_checkinteger(L, 3) == crc);
      }
      else
      {
         lua_pushinteger(L,crc);
      }
      return 1;
   }
   return throwInvArg(L,"type");  
}


/*********************************************************************
 *********************************************************************
                             OTA
 *********************************************************************
 *********************************************************************/
#if CONFIG_IDF_TARGET_ESP32S3

#define BAOTA "OTA"

typedef struct {
    esp_ota_handle_t handle;
} LOTA;


static LOTA* LOTA_getUD(lua_State* L)
{
   return (LOTA*)luaL_checkudata(L,1,BAOTA);
}


static int LOTA_write(lua_State *L)
{
   LOTA* ota = LOTA_getUD(L);
   size_t len;
   const char* data = luaL_checklstring(L, 2, &len);
   esp_err_t err =  ota->handle ? esp_ota_write(ota->handle, data, len) : ESP_ERR_INVALID_STATE;
   if(LUA_OK != err)
      return pushErr(L,esp_err_to_name(err));
   lua_pushboolean(L, TRUE);
   return 1;
}

static int LOTA_commit(lua_State *L)
{
   LOTA* ota = LOTA_getUD(L);
   esp_err_t err = ota->handle ? esp_ota_end(ota->handle) : ESP_ERR_INVALID_STATE;
   ota->handle=0;
   if(LUA_OK == err)
   {
      err=esp_ota_set_boot_partition(esp_ota_get_next_update_partition(0));
   }
   if(LUA_OK == err)
   {
      lua_pushboolean(L, TRUE);
      return 1;
   }
   return pushErr(L,esp_err_to_name(err));
}

static int LOTA_abort(lua_State *L)
{
   LOTA* ota = LOTA_getUD(L);
   esp_err_t err = ota->handle ? esp_ota_abort(ota->handle) : ESP_ERR_INVALID_STATE;
   if(LUA_OK != err)
      return pushErr(L,esp_err_to_name(err));
   lua_pushboolean(L, TRUE);
   return 1;
}

static const struct luaL_Reg otaObjLib[] = {
    {"write", LOTA_write},
    {"commit", LOTA_commit},
    {"abort", LOTA_abort},
    {"__gc", LOTA_abort},
    {NULL, NULL}
};


static int lota(lua_State *L)
{
   const char* action=luaL_optstring(L,1,0);
   if(!action)
   {
      const esp_app_desc_t* desc=esp_app_get_description();
      lua_createtable(L, 0, 7);
      lua_pushinteger(L,desc->secure_version); lua_setfield(L,-2,"secureversion");
      lua_pushstring(L,desc->version); lua_setfield(L,-2,"version");
      lua_pushstring(L,desc->project_name); lua_setfield(L,-2,"projectname");
      lua_pushstring(L,desc->time); lua_setfield(L,-2,"time");
      lua_pushstring(L,desc->date); lua_setfield(L,-2,"date");
      lua_pushstring(L,desc->idf_ver); lua_setfield(L,-2,"idfver");
      lua_pushlstring(L,(const char *)desc->app_elf_sha256,32); lua_setfield(L,-2,"sha256");
      return 1;
   }
   if( ! strcmp("begin",action) )
   {
      LOTA* ota=(LOTA*)lNewUdata(L, sizeof(LOTA), BAOTA, otaObjLib);
      const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
      esp_err_t err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota->handle);
      if(LUA_OK != err)
         return pushErr(L,esp_err_to_name(err));
      return 1;
   }
   return throwInvArg(L, "action");
}

#endif /* CONFIG_IDF_TARGET_ESP32S3 (OTA) */


/*********************************************************************
 *********************************************************************
                             Install ESP32
 *********************************************************************
 *********************************************************************/


static const luaL_Reg esp32Lib[] = {
   {"adc", ladc},
#if CONFIG_CAM_ENABLED
   {"cam", lcam},
#endif
   {"gpio", lgpio},
   {"i2cmaster", li2cMaster},
   {"pwmtimer", lLedTimer},
   {"pcnt", lLPCNT},
   {"pwmchannel", lLedChannel},
   {"uart", luart},
   {"wscan", lwscan},
   {"netconnect", lnetconnect},
   {"apinfo", lapinfo},
   {"mac", lmac},
   {"crc",lCRC},
   {"execute", lexecute},
   {"sdcard", lsdcard},
   {"loglevel", lloglevel},
   {"rmttx", LRmtTx_create},
   {"rmtrx", LRmtRx_create},
#if CONFIG_IDF_TARGET_ESP32S3
   {"ota",lota},
#endif
   {NULL, NULL}
};

static const luaL_Reg basLib[] = {
   {"mac", lmac}
};

#define RESERVED_PIN (void*)1  // Define a flag for reserved pins

void installESP32Libs(lua_State* L)
{
   memset(activeGPIO, 0, sizeof(void*)*GPIO_NUM_MAX);

// --- RESERVED PINS FOR ESP32 ---
#ifdef CONFIG_IDF_TARGET_ESP32
    activeGPIO[6]  = RESERVED_PIN; // GPIO6  - Connected to SPI Flash SCK (CLK)
    activeGPIO[7]  = RESERVED_PIN; // GPIO7  - Connected to SPI Flash SDO (MOSI)
    activeGPIO[8]  = RESERVED_PIN; // GPIO8  - Connected to SPI Flash SDI (MISO)
    activeGPIO[9]  = RESERVED_PIN; // GPIO9  - Connected to SPI Flash WP
    activeGPIO[10] = RESERVED_PIN; // GPIO10 - Connected to SPI Flash HD
    activeGPIO[11] = RESERVED_PIN; // GPIO11 - Connected to SPI Flash CS
#endif

   // --- RESERVED PINS FOR ESP32-S3 ---
#ifdef CONFIG_IDF_TARGET_ESP32S3
    activeGPIO[26] = RESERVED_PIN; // GPIO26 - Connected to SPI Flash SCK (CLK)
    activeGPIO[27] = RESERVED_PIN; // GPIO27 - Connected to SPI Flash SDO (MOSI)
    activeGPIO[28] = RESERVED_PIN; // GPIO28 - Connected to SPI Flash SDI (MISO)
    activeGPIO[29] = RESERVED_PIN; // GPIO29 - Connected to SPI Flash WP
    activeGPIO[30] = RESERVED_PIN; // GPIO30 - Connected to SPI Flash HD
    activeGPIO[31] = RESERVED_PIN; // GPIO31 - Connected to SPI Flash CS

    // PSRAM Pins (Reserved if PSRAM is enabled)
    activeGPIO[32] = RESERVED_PIN; // GPIO32 - Connected to PSRAM SCK (CLK)
    activeGPIO[33] = RESERVED_PIN; // GPIO33 - Connected to PSRAM CS
    activeGPIO[34] = RESERVED_PIN; // GPIO34 - Connected to PSRAM SDO
    activeGPIO[35] = RESERVED_PIN; // GPIO35 - Connected to PSRAM SDI
    activeGPIO[36] = RESERVED_PIN; // GPIO36 - Connected to PSRAM WP
    activeGPIO[37] = RESERVED_PIN; // GPIO37 - Connected to PSRAM HD
#endif

   ThreadMutex_constructor(&rMutex);
   soDispMutex = HttpServer_getMutex(ltMgr.server);
   luaL_newlib(L, esp32Lib);
   lua_setglobal(L,"esp32");
   lua_getglobal(L, "ba");
   luaL_setfuncs(L,basLib,0);
   lua_pop(L,1);
}
