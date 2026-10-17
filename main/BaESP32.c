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

#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
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
#include "soc/soc_caps.h"
#if CONFIG_IDF_TARGET_ESP32 && CONFIG_SPIRAM
#include "esp_private/esp_gpio_reserve.h"
#endif

#include "BaESP32.h"
#include "CfgESP32.h"
#if CONFIG_mDNS_ENABLED
#include <mdns.h>
#include <esp_netif.h>
#endif

#define ECHK ESP_ERROR_CHECK


/*
  The Socket Dispatcher (SoDisp) mutex protecting everything; set in
  installESP32Libs() below.
  https://realtimelogic.com/ba/doc/en/C/reference/html/structThreadMutex.html
*/
ThreadMutex* soDispMutex;

#define GPIO_QUEUE_SIZE 10 /* See executeLuaGpioCB() */

typedef struct LGPIO
{
   int selfRef;
   gpio_num_t pin;
   int queueLen;
   gpio_mode_t mode;
   BaBool isrInstalled;
   U8 queue[GPIO_QUEUE_SIZE]; /* holds GPIO level(s) high/low */
} LGPIO;

typedef struct
{
   ThreadJob super;
   int selfRef;
} GpioThreadJob;

/* Pin owners (or reserved marker 1). Every real owner starts with int selfRef;
   recovery uses that BAS weak reference without keeping unused objects alive. */
LGPIO* activeGPIO[GPIO_NUM_MAX];


/*********************************************************************
 *********************************************************************
                           Misc functions
 *********************************************************************
 *********************************************************************/

static void dispatchThreadJob(int selfRef, ThreadJob_LRun lrun);

/* Report a Lua API argument error; type names the invalid argument/category.
   L is the caller's Lua state, with the dispatcher mutex held.
   Raises a Lua error and does not return normally; no Lua results are returned.
 */
static int throwInvArg(lua_State* L, const char* type)
{
   return luaL_error(L,"Invalidarg %s", type); /* does not return; throws */
}

/* Reject acquisition of pin, an integer GPIO already owned/reserved.
   L is the caller's Lua state, with the dispatcher mutex held.
   Raises a Lua error and does not return normally; no Lua results are returned.
 */
static int throwPinInUse(lua_State* L, int pin)
{
   return luaL_error(L,"Pin %d in use",pin);
}

/* Return an operational failure to Lua without throwing.
   L is the caller's locked Lua state; msg is the error string to copy.
   Pushes nil and msg; returns 2, the number of Lua results.
 */
static int pushErr(lua_State* L, const char* msg)
{
   lua_pushnil(L);
   lua_pushstring(L,msg);
   return 2;
}


/* Convert an ESP-IDF result to the Lua success/error convention.
   Inputs: locked Lua state L; native result err; optional message prefix msg;
   throwOnInvArg selects a Lua error for ESP_ERR_INVALID_ARG instead of results.
   ESP_OK pushes true and returns 1. Other errors push nil and an error string
   and return 2, unless the selected invalid-argument path raises a Lua error.
 */
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


/* Create, zero and push userdata, then attach its named metatable.
   Inputs (caller holds the dispatcher mutex):
     L: Lua state receiving the new object.
     size: Userdata allocation size in bytes.
     tname: Metatable name; create it and register l on first use.
     l: NULL-terminated luaL_Reg method array.
     nuv: Number of Lua uservalue slots (0 means none), initially nil.
          Slot 1 holds the callback where used (see lReferenceCallback).
          RMT requests 2 slots: slot 2 retains the linked RX/TX partner.
   Returns the C pointer to the zeroed storage and leaves exactly one new
   userdata on L's stack. Allocation failure raises a Lua error.
 */
void* lNewUdataUV(lua_State *L, size_t size, const char *tname,
                  const luaL_Reg *l, int nuv)
{
   void* o = lua_newuserdatauv(L, size, nuv);
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


/* Expose an operational hardware result without treating invalid state or
   arguments returned by a driver as Lua API misuse. Inputs: locked Lua state
   L and ESP-IDF result err. Pushes true/returns 1 on ESP_OK; otherwise pushes
   nil plus esp_err_to_name(err) and returns 2. These are Lua result counts.
 */
static int hardwareStatus(lua_State* L, esp_err_t err)
{
   return pushEspRetVal(L,err,NULL,FALSE);
}

/* Hardware userdata, callbacks and weak references

   A normal Lua reference keeps a value alive. A weak-table value can be found
   while it exists, but the table does not prevent the garbage collector (GC)
   from collecting it. The shared BAS weak table maps selfRef -> userdata;
   balua_wkRef() registers it and balua_wkPush() retrieves userdata or nil.
   selfRef is an integer lookup key, not a pointer or a Lua ownership reference.

   There are three different kinds of Lua references here:

   1. userdata uservalue 1 -> callback is STRONG. As long as the application
      keeps the hardware object, its callback must remain callable even if the
      application keeps no separate reference to the function. A callback may
      capture its own userdata; that cycle is still collectible when nothing
      outside the cycle references it. RMT's slot 2 similarly retains its
      linked RX/TX partner so their native relationship cannot outlive it.

   2. BAS weak table selfRef -> userdata is WEAK. Permanently registering the
      userdata strongly would prevent GC from ever closing an abandoned object.
      Referencing only the callback is also wrong: the function could survive
      elsewhere after its hardware userdata has gone away. We need to find the
      original object, not merely a still-callable function or a reused pin.

      Asynchronous notifications carry selfRef through the broker and worker
      queues. Under the dispatcher mutex each stage looks up the owner again:
      it may be collected or closed while waiting in either queue. A nil lookup
      is skipped; a live userdata is also checked for closed/closing state.
      The worker keeps the retrieved userdata on its Lua stack while calling
      slot 1. That TEMPORARY STRONG reference protects its memory during Lua
      allocations, callback-triggered GC and error handling. It does not stop
      a callback from explicitly closing the hardware, so state checks still
      matter after calling Lua. The stack reference is released when the job
      finishes; queued notifications alone do not keep unused hardware alive.

   3. cleanupOwners is a separate STRONG table, used only when native teardown
      fails. A driver may still hold pointers into the userdata, so GC must not
      free it yet. hardwareRecover() retries teardown; successful cleanup
      removes this temporary ownership. This also covers partially constructed
      objects that have no pin registered in activeGPIO.

   activeGPIO contains raw C pointers, not Lua references. It tracks pin
   ownership and lets recovery locate pending work after a lost notification;
   it neither keeps userdata alive nor identifies delayed events. Successful
   cleanup clears its entries before the userdata can be freed.
 */
static int cleanupOwners;
static atomic_bool hardwareRetry;
static BaBool recoveryQueued;
static void hardwareRecover(ThreadJob* jb, int msgh, LThreadMgr* mgr);

/* Find the Lua owner of native userdata address o before retaining cleanup.
   Inputs: locked Lua state L and the address o. Search existing stack values
   and their uservalue-2 RMT partners; do not dereference an unrooted C pointer.
   Returns 1 and pushes the matching userdata, or returns 0 with the stack
   unchanged. The pushed value keeps the owner alive for the caller.
 */
static int pushHardwareOwner(lua_State* L, void* o)
{
   int top=lua_gettop(L);
   for(int i=1; i<=top; ++i)
   {
      if(lua_touserdata(L,i) == o)
      {
         lua_pushvalue(L,i);
         return 1;
      }
      if(lua_type(L,i) == LUA_TUSERDATA)
      {
         lua_getiuservalue(L,i,2); /* RMT partner, if present */
         if(lua_touserdata(L,-1) == o) return 1;
         lua_pop(L,1);
      }
   }
   return 0;
}

/* Remove every activeGPIO entry owned by native userdata address o.
   Call with the dispatcher mutex held, after native teardown succeeds.
   Does not reset hardware or free userdata. No return value or Lua stack use.
 */
static void releaseHardwarePins(void* o)
{
   for(int i=0; i<GPIO_NUM_MAX; ++i)
      if(activeGPIO[i] == (LGPIO*)o) activeGPIO[i]=NULL;
}

/* Finish a teardown attempt or retain its owner for another attempt.
   Inputs: locked Lua state L, native userdata address o, and teardown result
   err. On failure o must be discoverable on L's stack or as an RMT partner.
   ESP_OK releases pin ownership and removes any strong cleanup reference;
   an error retains the userdata in cleanupOwners and requests recovery.
   Returns err unchanged (a native status, not a Lua result count). Stack
   height is unchanged. A missing failure owner is an internal fatal error.
 */
static esp_err_t hardwareCleanup(lua_State* L, void* o, esp_err_t err)
{
   lua_rawgeti(L,LUA_REGISTRYINDEX,cleanupOwners);
   if(err == ESP_OK)
   {
      releaseHardwarePins(o);
      lua_pushnil(L);
   }
   else
   {
      int found=pushHardwareOwner(L,o);
      baAssert(found);
      if(!found) baFatalE(FE_MALLOC,0);
      atomic_store(&hardwareRetry,TRUE);
   }
   lua_rawsetp(L,-2,o);
   lua_pop(L,1);
   return err;
}

/* Set userdata[associated Lua value position 1] = callback. This keeps the
   callback alive while its userdata is reachable, without keeping otherwise
   unused userdata alive just because it has an interrupt/event registration.

   Params (validated by the caller):
     userdataIx: Absolute stack index of userdata with at least one uservalue.
     callbackIx: Absolute stack index of the Lua callback function.
   The caller must hold the BAS dispatcher mutex. The Lua stack is unchanged.

   Returns a BAS weak reference to the USERDATA, not to the callback. Queued
   events carry this reference so delivery can first find the original owner.
   A callback alone is insufficient: it may still exist after its hardware
   userdata has been collected. eventOwner() uses balua_wkPush() to push the
   userdata (or nil if gone); keeping it on the worker stack protects it during
   callback execution, including GC triggered by the callback. The delivery
   code also checks the owner's closed/closing state before calling slot 1.
   The weak reference itself does not prevent userdata/callback cycles from
   being collected when the application drops its last reference.
   https://realtimelogic.com/ba/doc/en/C/reference/html/group__WeakT.html
 */
static int lReferenceCallback(lua_State* L, int userdataIx, int callbackIx)
{
   lua_pushvalue(L,callbackIx);
   lua_setiuservalue(L,userdataIx,1);
   lua_pushvalue(L,userdataIx);
   return balua_wkRef(L);
}

/* Resolve a queued hardware identity while protecting the retrieved owner.
   Inputs: locked Lua state L and BAS weak reference ref, identifying userdata.
   Pushes that userdata or nil; returns its C address or NULL respectively.
   This does not check closed/closing state. The caller must keep the pushed
   value on the stack while using the address and check peripheral state.
   In a worker this stack entry occupies msgh+1; its slot 1 holds the callback.
 */
static void* eventOwner(lua_State* L, int ref)
{
   balua_wkPush(L,ref);
   return lua_touserdata(L,-1);
}

/* Begin a broker notification: acquire the dispatcher mutex, then resolve
   arg.ref (the queued BAS weak userdata reference) on ltMgr.Lg.
   Returns the native owner pointer or NULL and pushes userdata or nil there.
   Pair every call with eventEnd(), including a NULL result; the mutex remains
   held and the pushed value roots the owner until that matching call.
 */
static void* eventBegin(EventBrokerCallbackArg arg)
{
   ThreadMutex_set(soDispMutex);
   return eventOwner(ltMgr.Lg,arg.ref);
}

/* End the scope opened by eventBegin(); no arguments or return value.
   Pops its one owner/nil entry from ltMgr.Lg and releases the dispatcher
   mutex. The caller must first remove any additional temporary stack values.
 */
static void eventEnd(void)
{
   lua_pop(ltMgr.Lg,1);
   ThreadMutex_release(soDispMutex);
}

/* Normalize the configuration argument at positive stack index ix.
   Inputs: locked Lua state L and ix. If it already holds a table, discard
   values above it; otherwise discard from ix upward and create an empty table.
   Returns nothing; L's stack ends at ix with the configuration table there.
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

/* Run the FreeRTOS hardware-event broker; params is unused.
   Queue callbacks execute in THIS task, not an LThreadMgr worker. They may
   schedule Lua work via dispatchThreadJob(). The loop also requests a recovery
   job when hardwareRetry is set. It never returns and returns no Lua results;
   Lua stack/mutex handling belongs to the callbacks and recovery scheduling.
 */
void eventBrokerTask(void *params)
{
   (void)params;
   for(;;)
   {
      EventBrokerQueueNode n;
      if(xQueueReceive(eventBrokerQueue, &n, pdMS_TO_TICKS(20)))
      {
         n.callback(n.arg);
      }
      if(atomic_load(&hardwareRetry))
      {
         ThreadMutex_set(soDispMutex);
         if(!recoveryQueued)
         {
            recoveryQueued=TRUE;
            atomic_store(&hardwareRetry,FALSE);
            dispatchThreadJob(0,hardwareRecover);
         }
         ThreadMutex_release(soDispMutex);
      }
   }
}


enum { HW_RMT_TX, HW_RMT_RX, HW_PCNT_UNIT, HW_PCNT_WATCH };

typedef struct
{
   const void* cfg;
   void* handle;
   void* userdata;
   SemaphoreHandle_t done;
   esp_err_t status;
   int op;
} HardwareInit;

/* Execute one affinity-sensitive IDF operation on CPU 0.
   arg points to the caller-owned HardwareInit request, kept alive while the
   caller waits. Writes its native status, signals done, then deletes this
   temporary task. No normal return value and no Lua API or stack use.
 */
static void hardwareInitTask(void* arg)
{
   HardwareInit* c=arg;
   switch(c->op)
   {
      case HW_RMT_TX:
         c->status=rmt_new_tx_channel(c->cfg,c->handle);
         break;
      case HW_RMT_RX:
         c->status=rmt_new_rx_channel(c->cfg,c->handle);
         break;
      case HW_PCNT_UNIT:
         c->status=pcnt_new_unit(c->cfg,c->handle);
         break;
      case HW_PCNT_WATCH:
         c->status=pcnt_unit_register_event_callbacks(c->handle,c->cfg,c->userdata);
         break;
   }
   xSemaphoreGive(c->done);
   vTaskDelete(NULL);
}

/* Run bounded RMT/PCNT initialization on CPU 0, where IDF's shared
   interrupts must be allocated. Caller retains the dispatcher mutex; these
   operations neither call Lua nor wait for a Lua worker.
   Inputs: op selects HW_RMT_TX/RX, HW_PCNT_UNIT or HW_PCNT_WATCH.
   For creation, cfg points to the matching IDF configuration and handle points
   to output handle storage; userdata is unused. For HW_PCNT_WATCH, cfg points
   to pcnt_event_callbacks_t, handle is the existing unit and userdata is the
   callback context. All inputs remain valid until this synchronous call ends.
   Returns ESP_ERR_NO_MEM if task creation fails, otherwise the IDF status.
   No Lua stack use or Lua results.
 */
static esp_err_t hardwareInit(int op, const void* cfg, void* handle, void* userdata)
{
   StaticSemaphore_t sem;
   HardwareInit c={.cfg=cfg,.handle=handle,.userdata=userdata,
                  .done=xSemaphoreCreateBinaryStatic(&sem),.op=op};
   if(xTaskCreatePinnedToCore(hardwareInitTask,"hardwareInit",3072,&c,
                             configMAX_PRIORITIES-1,NULL,0) != pdPASS)
      return ESP_ERR_NO_MEM;
   xSemaphoreTake(c.done,portMAX_DELAY);
   vSemaphoreDelete(c.done);
   return c.status;
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
   int selfRef;
   int type; /* 0: closed, 1: one shot, 2: continuous */
   BaBool busy, closing;
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
         esp_err_t stopErr;
         atomic_bool started;
         uint16_t* blBuf;
         int blLen;
         int blBufIx;
         size_t bufSize;
         uint8_t running;
         atomic_bool overflow;
         uint8_t buf[1]; /* must be last, see lNewUdataUV i.e. malloc */
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
   LADC* o=(LADC*)arg.ptr;
   esp_err_t err=ESP_OK;
   if(o->u.contin.started)
   {
      err=adc_continuous_stop(o->u.contin.handle);
      if(err == ESP_OK) o->u.contin.started=FALSE;
   }
   if(err == ESP_OK && o->u.contin.handle)
   {
      err=adc_continuous_deinit(o->u.contin.handle);
      if(err == ESP_OK) o->u.contin.handle=NULL;
   }
   o->u.contin.stopErr=err;
   xSemaphoreGive(o->u.contin.stopSem);
}


/* Stop/deinitialize continuous or oneshot ADC and delete calibration.
   Inputs: locked Lua state L and ADC userdata pointer o. Keep the owner on
   L's stack for hardwareCleanup(). Supports
   partial construction and repeated close; failed native teardown is retained
   for recovery. Returns ESP_OK or the native failure code, NOT a Lua result
   count. Leaves the Lua stack unchanged.
   A busy object returns ESP_ERR_INVALID_STATE without teardown.
   Continuous stop waits for the broker with the dispatcher mutex released.
 */
static esp_err_t ADC_close(lua_State* L, LADC* o)
{
   esp_err_t err=ESP_OK;
   if(o->busy) return ESP_ERR_INVALID_STATE;
   o->closing=TRUE;
   if(o->type == 1 && o->u.oneShot.handle)
   {
      err=adc_oneshot_del_unit(o->u.oneShot.handle);
      if(err != ESP_OK) goto done;
      o->u.oneShot.handle=NULL;
   }
   else if(o->type == 2 && o->u.contin.handle)
   {
      o->u.contin.stopSem=xSemaphoreCreateBinary();
      if(!o->u.contin.stopSem)
      {
         err=ESP_ERR_NO_MEM;
         goto done;
      }
      EventBrokerQueueNode n={.callback=adcStopContinuousCB,.arg.ptr=o};
      o->busy=TRUE;
      ThreadMutex_release(soDispMutex);
      xQueueSend(eventBrokerQueue,&n,portMAX_DELAY);
      xSemaphoreTake(o->u.contin.stopSem,portMAX_DELAY);
      ThreadMutex_set(soDispMutex);
      o->busy=FALSE;
      vSemaphoreDelete(o->u.contin.stopSem);
      o->u.contin.stopSem=NULL;
      err=o->u.contin.stopErr;
      if(err != ESP_OK) goto done;
   }
   if(o->caliHandle)
   {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
      err=adc_cali_delete_scheme_curve_fitting(o->caliHandle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
      err=adc_cali_delete_scheme_line_fitting(o->caliHandle);
#endif
      if(err != ESP_OK) goto done;
      o->caliHandle=NULL;
   }
   o->type=0;
done:
   return hardwareCleanup(L,o,err);
}


static LADC* ADC_getUD(lua_State* L)
{
   return (LADC*)luaL_checkudata(L,1,BAADC);
}


static LADC* ADC_checkUD(lua_State* L)
{
   LADC* o = ADC_getUD(L);
   if(!o->type || o->closing)
      luaL_error(L,"CLOSED");
   return o;
}


static int ADC_lclose(lua_State* L)
{
   return hardwareStatus(L,ADC_close(L,ADC_getUD(L)));
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
   lua_getiuservalue(L,msgh+1,1);
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
   lua_getiuservalue(L,msgh+1,1);
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
   LADC* adc = (LADC*)eventOwner(jb->Lt,((GpioThreadJob*)jb)->selfRef);
   if(adc)
   {
      lua_State* L = jb->Lt;
      int ownerIx=lua_gettop(L);
      while(adc->type && !adc->closing)
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
                  if(!adc->type || adc->closing) return;
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
            lua_getiuservalue(L,msgh+1,1);
            pushErr(L,adc->u.contin.overflow ?
                    "overflow" : esp_err_to_name(err));
            if(LUA_OK != lua_pcall(L, 2, 0, msgh))
            {
               ADC_close(L,adc);
               return;
            }
            if(!adc->type || adc->closing) return;
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
         lua_settop(L,ownerIx);
         if(adc->u.contin.overflow)
            goto L_err;
      }
      adc->u.contin.running=FALSE;
   }
}


/* Stage 2: This CB runs in the context of the eventBrokerTask.
 */
static void adcEventCB(EventBrokerCallbackArg arg)
{
   LADC* adc = (LADC*)eventBegin(arg);
   if(adc && adc->type && !adc->closing && ! adc->u.contin.running)
   {
      adc->u.contin.running = TRUE;
      dispatchThreadJob(arg.ref, adcExecuteLuaEventCB);
   }
   eventEnd();
}


/* Stage 2: This CB runs in the context of the eventBrokerTask.
 */
static void adcOverflowCB(EventBrokerCallbackArg arg)
{
   adcEventCB(arg);
}


/* Stage 1 : overflow
 */
static IRAM_ATTR bool adcOvfInterruptHandler(
   adc_continuous_handle_t handle, const adc_continuous_evt_data_t* edata,
   void* pin)
{
   LADC* adc=(LADC*)pin;
   atomic_store(&adc->u.contin.overflow,TRUE);
   BaseType_t hwakeup = pdFALSE;
   EventBrokerQueueNode n = {.callback=adcOverflowCB,.arg.ref=adc->selfRef};
   if(xQueueSendFromISR(eventBrokerQueue,&n,&hwakeup) != pdTRUE)
      atomic_store(&hardwareRetry,TRUE); /* Trigger adcEventCB() */
   return hwakeup == pdTRUE;
}


/* Stage 1 : conversion result
 */
static IRAM_ATTR bool adcConvDoneInterruptHandler(
   adc_continuous_handle_t handle, const adc_continuous_evt_data_t* edata,
   void* pin)
{
   LADC* adc=(LADC*)pin;
   BaseType_t hwakeup = pdFALSE;
   EventBrokerQueueNode n = {.callback=adcEventCB,.arg.ref=adc->selfRef};
   if(xQueueSendFromISR(eventBrokerQueue,&n,&hwakeup) != pdTRUE)
      atomic_store(&hardwareRetry,TRUE); /* Trigger adcEventCB() */
   return hwakeup == pdTRUE;
}


/* This CB runs in the context of the eventBrokerTask.
   See the comment in adcStopContinuousCB() for details on this construction.
 */
static void adcStartContinuousCB(EventBrokerCallbackArg arg)
{
   LADC* adc = (LADC*)arg.ptr;
   baAssert(adc);
   if(adc)
   {
      esp_err_t err=adc_continuous_start(adc->u.contin.handle);
      if(err == ESP_OK) adc->u.contin.started=TRUE;
      if(ESP_OK != err)
      { /* We use the 'overflow' event to signal that we could not start */
         ThreadMutex_set(soDispMutex);
         adc->u.contin.overflow=TRUE;
         dispatchThreadJob(adc->selfRef, adcExecuteLuaEventCB);
         ThreadMutex_release(soDispMutex);
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
   LADC* adc=NULL;
   adc_oneshot_unit_handle_t oneShotHandle=NULL;
   adc_continuous_handle_t contHandle=NULL;
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
      if(blLen < 0 || blLen > (INT_MAX-(int)sizeof(LADC))/4) return throwInvArg(L,"bl");
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
      adc=(LADC*)lNewUdataUV(L,sizeof(LADC)+bufSize+blLen*sizeof(uint16_t),BAADC,adcObjLib,1);
      adc->type=2;
      err=adc_continuous_new_handle(&adcHandleConfig, &contHandle);
      adc->u.contin.handle=contHandle;
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
         .conv_mode = unitId == ADC_UNIT_1 ? ADC_CONV_SINGLE_UNIT_1 : ADC_CONV_SINGLE_UNIT_2,
         .format = ADC_OUTPUT_TYPE,
         .pattern_num = 1,
         .adc_pattern = adcPattern
      };
      err=adc_continuous_config(contHandle, &adcConfig);
      if(err != ESP_OK) goto fail;
   }
   else /* Oneshot mode */
   {
      adc_oneshot_unit_init_cfg_t oneShotCfg = { .unit_id = unitId };
      adc=(LADC*)lNewUdataUV(L,sizeof(LADC),BAADC,adcObjLib,1);
      adc->type=1;
      err=adc_oneshot_new_unit(&oneShotCfg,&oneShotHandle);
      adc->u.oneShot.handle=oneShotHandle;
      adc_oneshot_chan_cfg_t cfg = {
         .bitwidth = bitwidth,
         .atten = atten
      };
      if(ESP_OK == err)
         err=adc_oneshot_config_channel(oneShotHandle, channel, &cfg);
      if(err != ESP_OK) goto fail;
   }
   activeGPIO[pin]=(LGPIO*)adc; /* mark pin in use */
   if(hasCB)
   {
      adc->u.contin.filter=filter;
      adc->u.contin.bufSize=bufSize;
      adc->u.contin.blLen = blLen;
      adc->u.contin.blBuf = (uint16_t*)(((char*)(adc+1)) + bufSize);
      /* Userdata at -1 and callback ix is 4 */
      adc->selfRef=lReferenceCallback(L, lua_absindex(L,-1), 4);
      adc_continuous_evt_cbs_t cbs = {
         .on_conv_done = adcConvDoneInterruptHandler,
         .on_pool_ovf = adcOvfInterruptHandler
      };
      err=adc_continuous_register_event_callbacks(
         contHandle,&cbs,adc);
      if(err != ESP_OK) goto fail;
   }
   adc->channel=channel;
   if(volt)
   {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
      adc_cali_curve_fitting_config_t caCfg1 = {
         .unit_id = unitId,
         .chan = channel,
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
      if(err != ESP_OK) goto fail;
   }
   if(hasCB)
   {
      EventBrokerQueueNode n = {
         .callback=adcStartContinuousCB,.arg.ptr=adc};
      ThreadMutex_release(soDispMutex);
      xQueueSend(eventBrokerQueue, &n, portMAX_DELAY);
      ThreadMutex_set(soDispMutex);
   }
   lua_pushinteger(L,pin);
   return 2;
fail:
   ADC_close(L,adc);
   return hardwareStatus(L,err);
}






/*********************************************************************
 *********************************************************************
                                 GPIO
 *********************************************************************
 *********************************************************************/

/* Remove the installed ISR and reset the GPIO pin.
   Inputs: locked Lua state L and GPIO userdata pointer o. Keep the owner on
   L's stack for hardwareCleanup(). Supports
   partial construction and repeated close; failed native teardown is retained
   for recovery. Returns ESP_OK or the native failure code, NOT a Lua result
   count. Leaves the Lua stack unchanged.
 */
static esp_err_t GPIO_close(lua_State* L, LGPIO* o)
{
   esp_err_t err=ESP_OK;
   if(o->isrInstalled)
   {
      err=gpio_isr_handler_remove(o->pin);
      if(err != ESP_OK) goto done;
      o->isrInstalled=FALSE;
   }
   if(o->pin != GPIO_NUM_MAX)
   {
      err=gpio_reset_pin(o->pin);
      if(err != ESP_OK) goto done;
      o->pin=GPIO_NUM_MAX;
   }
done:
   return hardwareCleanup(L,o,err);
}

/* This CB runs in the context of a thread in the LThreadMgr.
 */
static void executeLuaGpioCB(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   int queueLen=0,ix;
   U8 queue[GPIO_QUEUE_SIZE];
   GpioThreadJob* job = (GpioThreadJob*)jb;
   LGPIO* gpio = (LGPIO*)eventOwner(jb->Lt,job->selfRef);
   if(gpio && gpio->pin != GPIO_NUM_MAX)
   {
      queueLen = gpio->queueLen;
      memcpy(queue,gpio->queue,queueLen);
      gpio->queueLen=0;
   }
   if(gpio && gpio->pin != GPIO_NUM_MAX)
   {
      lua_State* L = jb->Lt;
      for(ix=0 ; ix < queueLen && GPIO_NUM_MAX != gpio->pin ; ix++)
      {
         /* Owner stays at msgh+1 while its slot-1 callback runs. */
         lua_getiuservalue(L,msgh+1,1);
         lua_pushboolean(L, queue[ix]);
         if(LUA_OK != lua_pcall(L, 1, 0, msgh))
         {
            GPIO_close(L,gpio);
            break;
         }
         lua_settop(L,msgh+1);
      }
   }
}


/* Queue a Lua worker callback after the broker has accepted a notification.
   Inputs: BAS weak userdata identity selfRef (0 for a recovery job) and worker
   entry point lrun. Caller holds the dispatcher mutex. The job stores only
   the identity, so lrun must resolve the owner again when it executes.
   Returns nothing and leaves the caller's Lua stack unchanged. Job allocation
   failure is fatal; no userdata is kept alive merely by queuing this job.
 */
static void dispatchThreadJob(int selfRef, ThreadJob_LRun lrun)
{
   GpioThreadJob* job;
   job = (GpioThreadJob*)ThreadJob_lcreate(sizeof(GpioThreadJob),lrun);
   if( ! job )
      baFatalE(FE_MALLOC,0);
   job->selfRef=selfRef;
   LThreadMgr_run(&ltMgr, (ThreadJob*)job);
}


/* This CB runs in the context of the eventBrokerTask.
 */
static void gpioEventCB(EventBrokerCallbackArg arg)
{
   LGPIO* gpio = (LGPIO*)eventBegin(arg);
   int queueLen=0;

   if(gpio && gpio->pin != GPIO_NUM_MAX)
   {
      if(gpio->queueLen < GPIO_QUEUE_SIZE)
      {
         gpio->queue[gpio->queueLen++] = gpio_get_level(gpio->pin);
         queueLen=gpio->queueLen;
      }
   }

   /* If transitioning from empty to one element in the queue */
   if(1 == queueLen)
      dispatchThreadJob(arg.ref,executeLuaGpioCB);
   eventEnd();
}


/* Send a GPIO event to the eventBrokerTask, which then
 * calls function gpioEventCB.
 */
static void IRAM_ATTR gpioInterruptHandler(void *arg)
{
   EventBrokerQueueNode n = {
      .callback=gpioEventCB,
      .arg.ref=(int)(intptr_t)arg
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
      return hardwareStatus(L,gpio_set_level(o->pin,balua_checkboolean(L, 2)));
   }
   else
   {
      lua_pushboolean(L,gpio_get_level(o->pin));
   }
   return 1;
}


static int GPIO_lclose(lua_State* L)
{
   return hardwareStatus(L,GPIO_close(L,GPIO_getUD(L)));
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
   lua_Integer pin=luaL_checkinteger(L, 1);
   if(!GPIO_IS_VALID_GPIO(pin))
      luaL_argerror(L, 1, "Invalid pin");
   const char* mode = luaL_checkstring(L,2);
   cfg.mode = 'I' == mode[0] && !mode[2] ? GPIO_MODE_INPUT :
      ('O' == mode[0] && !mode[3] ? GPIO_MODE_OUTPUT :
       ('O' == mode[0] ? GPIO_MODE_OUTPUT_OD :
        ('I' == mode[0] && !mode[5] ? GPIO_MODE_INPUT_OUTPUT :
         GPIO_MODE_INPUT_OUTPUT_OD)));
   if((cfg.mode & GPIO_MODE_DEF_OUTPUT) && !GPIO_IS_VALID_OUTPUT_GPIO(pin))
      luaL_argerror(L, 1, "Invalid output pin");
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
   LGPIO* gpio = (LGPIO*)lNewUdataUV(L,sizeof(LGPIO),BAGPIO,gpioObjLib,1);
   gpio->mode = cfg.mode;
   gpio->pin=GPIO_NUM_MAX;
   gpio_reset_pin(pin);
   esp_err_t err=gpio_config(&cfg);
   if(err != ESP_OK) return pushEspRetVal(L,err,"config",FALSE);
   gpio->pin=pin;
   activeGPIO[pin]=gpio;
   if(hasCB)
   {
      /* Userdata at -1 and callback ix is 4 */
      gpio->selfRef=lReferenceCallback(L, lua_absindex(L,-1), 4);
      err=gpio_isr_handler_add(pin,gpioInterruptHandler,(void*)(intptr_t)gpio->selfRef);
      if(err != ESP_OK)
      {
         GPIO_close(L,gpio);
         return pushEspRetVal(L,err,"ISR",FALSE);
      }
      gpio->isrInstalled=TRUE;
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
static portMUX_TYPE pcntMux = portMUX_INITIALIZER_UNLOCKED;

typedef struct
{
   int selfRef;
   pcnt_unit_handle_t unit;
   int noOfChannels;
   BaBool enabled, closing;
   int qHead;
   int qTail;
   int lCbRunning; // Notification queued or Lua worker running; protected by pcntMux
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
   if(!o->unit || o->closing)
      luaL_error(L,"CLOSED");
   return o;
}

/* Stop/disable the counter and delete its channels and unit.
   Inputs: locked Lua state L and PCNT userdata pointer o. Keep the owner on
   L's stack for hardwareCleanup(). Supports
   partial construction and repeated close; failed native teardown is retained
   for recovery. Returns ESP_OK or the native failure code, NOT a Lua result
   count. Leaves the Lua stack unchanged.
 */
static int LPCNT_close(lua_State* L, LPCNT* o)
{
   esp_err_t err=ESP_OK;
   o->closing=TRUE;
   if(o->enabled)
   {
      err=pcnt_unit_stop(o->unit);
      if(err != ESP_OK) goto done;
      err=pcnt_unit_disable(o->unit);
      if(err != ESP_OK) goto done;
      o->enabled=FALSE;
   }
   for(int i=0;i<o->noOfChannels;++i)
      if(o->channels[i])
      {
         err=pcnt_del_channel(o->channels[i]);
         if(err != ESP_OK) goto done;
         o->channels[i]=NULL;
      }
   if(o->unit)
   {
      err=pcnt_del_unit(o->unit);
      if(err != ESP_OK) goto done;
      o->unit=NULL;
   }
   o->noOfChannels=0;
done:
   return hardwareCleanup(L,o,err);
}

static int LPCNT_lclose(lua_State* L)
{
   return hardwareStatus(L,LPCNT_close(L,LPCNT_getUD(L)));
}

static int LPCNT_start(lua_State* L)
{
   return hardwareStatus(L,pcnt_unit_start(LPCNT_checkUD(L)->unit));
}

static int LPCNT_stop(lua_State* L)
{
   return hardwareStatus(L,pcnt_unit_stop(LPCNT_checkUD(L)->unit));
}

static int LPCNT_count(lua_State* L)
{
   int count;
   esp_err_t status = pcnt_unit_get_count(LPCNT_checkUD(L)->unit, &count);
   if(ESP_OK != status)
      return hardwareStatus(L,status);
   lua_pushinteger(L,count);
   return 1;
}

static int LPCNT_clear(lua_State* L)
{
   return hardwareStatus(L,pcnt_unit_clear_count(LPCNT_checkUD(L)->unit));
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
   int top=lua_gettop(L);
   esp_err_t status = ESP_OK;
   int watchIx=balua_getTabField(L, 1, "watch");
   if(watchIx)
   {
      lua_getfield(L, watchIx, "callback");
      if( ! lua_isfunction(L, -1) )
         LPCNT_throwWatchMissing(L, "callback");
      if(pcnt)
         pcnt->selfRef=lReferenceCallback(L,userdataIx,lua_absindex(L,-1));
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
         lua_pop(L,1);
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
   lua_settop(L,top);
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
         int pins[]={chanCfg.edge_gpio_num,chanCfg.level_gpio_num};
         for(int i=0;i<2;++i)
         {
            if(pins[i] == -1) continue;
            if(!GPIO_IS_VALID_GPIO(pins[i])) return luaL_error(L,"Invalid pin");
            if(activeGPIO[pins[i]]) throwPinInUse(L,pins[i]);
            *pinPtr=pins[i];
         }

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
            LPCNT* owner=(LPCNT*)((char*)channels - offsetof(LPCNT,channels));
            if(chanCfg.edge_gpio_num >= 0) activeGPIO[chanCfg.edge_gpio_num]=(LGPIO*)owner;
            if(chanCfg.level_gpio_num >= 0) activeGPIO[chanCfg.level_gpio_num]=(LGPIO*)owner;
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
   LPCNT* o = (LPCNT*)eventOwner(jb->Lt,job->selfRef);
   if(o)
   {
      lua_State* L = jb->Lt;
      while(o->noOfChannels && !o->closing)
      {
         portENTER_CRITICAL(&pcntMux);
         if(o->qHead == o->qTail)
         {
            o->lCbRunning=FALSE;
            portEXIT_CRITICAL(&pcntMux);
            break;
         }
         portEXIT_CRITICAL(&pcntMux);
         /* Push user's callback on the stack */
         lua_getiuservalue(L,msgh+1,1);
         lua_pushinteger(L,o->onReachQueueVal[o->qTail]);
         lua_pushinteger(L,o->onReachQueueCrossMode[o->qTail]);
         if(LUA_OK != lua_pcall(L, 2, 0, msgh))
         {
            LPCNT_close(L, o);
            break;
         }
         lua_settop(L,msgh+1);
         if(!o->noOfChannels) break;
         portENTER_CRITICAL(&pcntMux);
         int next = o->qTail+1;
         if(next >= ONREACH_QUEUE_SIZE)
            next=0;
         o->qTail = next;
         portEXIT_CRITICAL(&pcntMux);
      }
   }
}


/* 2 of 3:
   This CB runs in the context of the eventBrokerTask.
 */
static void LPCNT_onReachCB(EventBrokerCallbackArg arg)
{
   LPCNT* o = (LPCNT*)eventBegin(arg);
   if(o && o->noOfChannels && !o->closing)
      dispatchThreadJob(arg.ref,LPCNT_onReachLuaCB);
   eventEnd();
}


/* 1 of 3
 */
static bool LPCNT_onReachInterruptHandler(
   pcnt_unit_handle_t unit, const pcnt_watch_event_data_t* edata, void* pin)
{
   BaseType_t highTaskWakeup=pdFALSE;
   LPCNT* o=(LPCNT*)pin;
   portENTER_CRITICAL_ISR(&pcntMux);
   int next = o->qHead+1;
   if(next >= ONREACH_QUEUE_SIZE)
      next=0;
   if(next != o->qTail) // Not full
   {
      o->onReachQueueVal[o->qHead] = edata->watch_point_value;
      o->onReachQueueCrossMode[o->qHead] = edata->zero_cross_mode;
      o->qHead=next;
   }
   /* Retry notification even if earlier failures filled the pulse buffer. */
   if( ! o->lCbRunning )
   {
      EventBrokerQueueNode n = {.callback=LPCNT_onReachCB,.arg.ref=o->selfRef};
      o->lCbRunning=TRUE;
      if(xQueueSendFromISR(eventBrokerQueue, &n, &highTaskWakeup) != pdTRUE)
      {
         o->lCbRunning=FALSE;
         atomic_store(&hardwareRetry,TRUE);
      }
   }
   portEXIT_CRITICAL_ISR(&pcntMux);
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
   int gpioPin=-1; /* Validate that at least one input pin is configured. */
   /* Validate input table and count channels. This may throw error message. */
   LPCNT_channels(L,0,&noOfChannels,0, &gpioPin);
   if(gpioPin == -1) return throwInvArg(L,"channel pins");
   /* Validate optional watch table: throw on error */
   LPCNT_watch(L, 0, 0);

   LPCNT* pcnt = (LPCNT*)lNewUdataUV(
      L, sizeof(LPCNT)+noOfChannels*sizeof(pcnt_channel_handle_t),BAPCNT, pcntLib,1);
   pcnt->noOfChannels=noOfChannels;
   int userdataIx=lua_absindex(L,-1);

   /* Create and configure PCNT */
   esp_err_t status = hardwareInit(HW_PCNT_UNIT,&ucfg,&pcnt->unit,NULL);
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
            status=pcnt->selfRef ? hardwareInit(HW_PCNT_WATCH,&cbs,pcnt->unit,pcnt) : ESP_OK;
            if(status == ESP_OK) status=pcnt_unit_enable(pcnt->unit);
            if(status == ESP_OK)
            {
               pcnt->enabled=TRUE;
               status=pcnt_unit_clear_count(pcnt->unit);
            }
            if(status == ESP_OK)
            {
               lua_settop(L,userdataIx);
               return 1;
            }
            hardwareStatus(L,status);
         }

      }
   }
   LPCNT_close(L,pcnt);
   return 2; /* Error values from pushEspRetVal */
}


/*********************************************************************
 *********************************************************************
                       PWM: LED Control (LEDC)
 *********************************************************************
 *********************************************************************/

#define BALEDC "LEDC"


typedef struct{
   int selfRef;
   gpio_num_t pin;
   ledc_mode_t mode;
   ledc_channel_t channel;
   U8 running;
   BaBool busy, closing, installed, jobQueued;
   ledc_timer_t timer;
} LLEDC;

static int ledsRunning=0;
static LLEDC* ledChannels[LEDC_SPEED_MODE_MAX][LEDC_CHANNEL_MAX];
static struct
{
   int ref;
   unsigned pending;
} ledEvents[LEDC_SPEED_MODE_MAX][LEDC_CHANNEL_MAX];
static portMUX_TYPE ledMux=portMUX_INITIALIZER_UNLOCKED;

/* Inspect or consume the completion count for LEDC userdata o.
   take=TRUE consumes one event if available; FALSE only inspects the count.
   Returns the count BEFORE consumption, not the remaining count. The caller
   keeps o alive; ledMux serializes the counter with the ISR. No Lua stack use.
 */
static unsigned ledPending(LLEDC* o, BaBool take)
{
   portENTER_CRITICAL(&ledMux);
   unsigned n=ledEvents[o->mode][o->channel].pending;
   if(n && take) --ledEvents[o->mode][o->channel].pending;
   portEXIT_CRITICAL(&ledMux);
   return n;
}

static LLEDC* LLEDC_getUD(lua_State* L)
{
   return (LLEDC*)luaL_checkudata(L,1,BALEDC);
}

static LLEDC* LLEDC_checkUD(lua_State* L)
{
   LLEDC* o = LLEDC_getUD(L);
   if(!o->running || o->closing)
      luaL_error(L,"CLOSED");
   if(o->busy) luaL_error(L,"Busy");
   return o;
}


static int LLEDC_duty(lua_State* L)
{
   LLEDC* o = LLEDC_checkUD(L);
   uint32_t duty = (uint32_t)luaL_checkinteger(L, 2);
   uint32_t hpoint = (uint32_t)(lua_isinteger(L,3) ? lua_tointeger(L,3) :
                                ledc_get_hpoint(o->mode,o->channel));
   o->busy=TRUE;
   ThreadMutex_release(soDispMutex);
   esp_err_t err=ledc_set_duty_and_update(
                           o->mode,o->channel, duty, hpoint);
   ThreadMutex_set(soDispMutex);
   o->busy=FALSE;
   return hardwareStatus(L,err);
}


static int LLEDC_fade(lua_State* L)
{
   LLEDC* o = LLEDC_checkUD(L);
   if(!o->selfRef)
      luaL_error(L,"No callback");
   uint32_t duty = (uint32_t)luaL_checkinteger(L, 2);
   uint32_t fadeTime = (uint32_t)luaL_checkinteger(L, 3);
   o->busy=TRUE;
   ThreadMutex_release(soDispMutex);
   esp_err_t err=ledc_set_fade_time_and_start(
                           o->mode,o->channel,duty,fadeTime,LEDC_FADE_NO_WAIT);
   ThreadMutex_set(soDispMutex);
   o->busy=FALSE;
   return hardwareStatus(L,err);
}


/* Synchronize with fade completion and release channel/service ownership.
   Inputs: locked Lua state L and LEDC userdata pointer o. Keep the owner on
   L's stack for hardwareCleanup(). Supports
   partial construction and repeated close; failed native teardown is retained
   for recovery. Returns ESP_OK or the native failure code, NOT a Lua result
   count. Leaves the Lua stack unchanged.
   A busy object returns ESP_ERR_INVALID_STATE without teardown.
   The dispatcher mutex is released around the blocking fade synchronization.
 */
static esp_err_t LLEDC_close(lua_State* L, LLEDC* o)
{
   esp_err_t err=ESP_OK;
   if(o->busy) return ESP_ERR_INVALID_STATE;
   o->closing=TRUE;
   if(o->running)
   {
      portENTER_CRITICAL(&ledMux);
      ledEvents[o->mode][o->channel].ref=0;
      portEXIT_CRITICAL(&ledMux);
      /* Synchronize with a fade before removing its callback context. */
      o->busy=TRUE;
      ThreadMutex_release(soDispMutex);
#if SOC_LEDC_SUPPORT_FADE_STOP
      err=ledc_fade_stop(o->mode,o->channel);
#else
      err=ledc_set_duty_and_update(o->mode,o->channel,0,0);
#endif
      ThreadMutex_set(soDispMutex);
      o->busy=FALSE;
      if(err != ESP_OK) goto done;
      ledc_cbs_t cb={0};
      err=ledc_cb_register(o->mode,o->channel,&cb,NULL);
      if(err != ESP_OK) goto done;
      err=ledc_stop(o->mode,o->channel,0);
      if(err != ESP_OK) goto done;
      err=gpio_reset_pin(o->pin);
      if(err != ESP_OK) goto done;
      o->running=FALSE;
      ledChannels[o->mode][o->channel]=NULL;
   }
   if(o->installed)
   {
      o->installed=FALSE;
      if(--ledsRunning == 0) ledc_fade_func_uninstall();
   }
done:
   return hardwareCleanup(L,o,err);
}


static int LLEDC_lclose(lua_State* L)
{
   return hardwareStatus(L,LLEDC_close(L,LLEDC_getUD(L)));
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
   lua_State* L=jb->Lt;
   LLEDC* o=(LLEDC*)eventOwner(L,((GpioThreadJob*)jb)->selfRef);
   if(!o) return;
   while(o->running && !o->closing && ledPending(o,TRUE))
   {
      lua_getiuservalue(L,msgh+1,1);
      if(lua_pcall(L,0,0,msgh) != LUA_OK)
      {
         if(o->busy)
         {
            o->closing=TRUE;
            hardwareCleanup(L,o,ESP_ERR_INVALID_STATE);
         }
         else LLEDC_close(L,o);
         break;
      }
   }
   o->jobQueued=FALSE;
}


/* This CB runs in the context of the eventBrokerTask.
 */
static void ledEventCB(EventBrokerCallbackArg arg)
{
   LLEDC* o = (LLEDC*)eventBegin(arg);
   if(o && o->running && !o->closing && !o->jobQueued && ledPending(o,FALSE))
   {
      o->jobQueued=TRUE;
      dispatchThreadJob(arg.ref,executeLuaLedEventCB);
   }
   eventEnd();
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
         .arg.ref=(int)(intptr_t)arg
      };
      portENTER_CRITICAL_ISR(&ledMux);
      if(ledEvents[param->speed_mode][param->channel].ref != n.arg.ref)
      {
         portEXIT_CRITICAL_ISR(&ledMux);
         return false;
      }
      ++ledEvents[param->speed_mode][param->channel].pending;
      portEXIT_CRITICAL_ISR(&ledMux);
      if(xQueueSendFromISR(eventBrokerQueue,&n,&hwakeup) != pdTRUE)
         atomic_store(&hardwareRetry,TRUE);
   }
   return hwakeup == pdTRUE;
}

// If the current processor does NOT have High Speed ​​hardware (such as the S3 or P4),
// we silently redirect High to Low speed requests.
#if !SOC_LEDC_SUPPORT_HS_MODE
    #define LEDC_HIGH_SPEED_MODE LEDC_LOW_SPEED_MODE
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
   if(!GPIO_IS_VALID_OUTPUT_GPIO(pin))
      luaL_argerror(L, 1, "Invalid GPIO pin");
   if(activeGPIO[pin])
      throwPinInUse(L,pin);
   if(channel < LEDC_CHANNEL_0 || channel >= LEDC_CHANNEL_MAX)
      throwInvArg(L, "channel");
   if(timer < LEDC_TIMER_0 || timer >= LEDC_TIMER_MAX)
      throwInvArg(L, "timer");
   if(ledChannels[mode][channel]) return luaL_error(L,"Channel in use");
   LLEDC* led=(LLEDC*)lNewUdataUV(L,sizeof(LLEDC),BALEDC,ledcObjLib,1);
   led->pin=pin;
   led->mode=mode;
   led->channel=channel;
   led->timer=timer;
   if(hasCB) led->selfRef=lReferenceCallback(L,lua_absindex(L,-1),2);
   esp_err_t status=ledsRunning ? ESP_OK : ledc_fade_func_install(0);
   if(status != ESP_OK) return hardwareStatus(L,status);
   ++ledsRunning;
   led->installed=TRUE;
   ledc_channel_config_t cfg={
      .gpio_num=pin,
      .speed_mode=mode,
      .channel=channel,
      .intr_type =  hasCB ? LEDC_INTR_FADE_END : LEDC_INTR_DISABLE,
      .timer_sel = timer,
      .duty=duty,
      .hpoint=hpoint
   };
   status=ledc_channel_config(&cfg);
   if(status != ESP_OK) goto fail;
   led->running=TRUE;
   activeGPIO[pin]=(LGPIO*)led;
   ledChannels[mode][channel]=led;
   portENTER_CRITICAL(&ledMux);
   ledEvents[mode][channel].ref=led->selfRef;
   ledEvents[mode][channel].pending=0;
   portEXIT_CRITICAL(&ledMux);
   /* Initialize fade state even without a Lua callback (used during close). */
   ledc_cbs_t cb={.fade_cb=hasCB ? ledInterruptHandler : NULL};
   status=ledc_cb_register(mode,channel,&cb,(void*)(intptr_t)led->selfRef);
   if(status != ESP_OK) goto fail;
   return 1;
fail:
   LLEDC_close(L,led);
   return hardwareStatus(L,status);
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
   for(int i=0;i<LEDC_CHANNEL_MAX;++i)
      if(ledChannels[mode][i] && ledChannels[mode][i]->timer == timer)
         return luaL_error(L,"Timer in use");
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
   int selfRef; /* Zero: this pin owner has no asynchronous notifications. */
   BaBool busy, closing;
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

// ESP-IDF expects milliseconds; -1 means wait forever.
static int I2CWT(lua_State* L, int ix)
{
   lua_Integer timeout = luaL_optinteger(L, ix, 500);
   if(timeout < -1 || timeout > INT_MAX)
      luaL_argerror(L, ix, "Invalid timeout");
   return (int)timeout;
}

// Get I2C master userdata from Lua
static LI2CMaster* I2CMaster_getUD(lua_State* L)
{
   return (LI2CMaster*)luaL_checkudata(L,1,BAI2CMASTER);
}

// Check if I2C master is initialized; if not, raise an error
static LI2CMaster* I2CMaster_checkUD(lua_State* L, int checkCmd)
{
   LI2CMaster* i2cm = I2CMaster_getUD(L);
   if(i2cm->bus_handle == 0 || i2cm->closing)
   {
      luaL_error(L, "Closed");
   }
   if(i2cm->busy) luaL_error(L,"Busy");
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
   esp_err_t err=ESP_OK;
   if(i2cm->dev_handle && i2cm->dev_cfg.device_address != address)
   {
      err=i2c_master_bus_rm_device(i2cm->dev_handle);
      if(err != ESP_OK) return err;
      i2cm->dev_handle=NULL;
   }
   if(!i2cm->dev_handle)
   {
      i2cm->dev_cfg.device_address=address;
      err=i2c_master_bus_add_device(i2cm->bus_handle,&i2cm->dev_cfg,&i2cm->dev_handle);
   }
   return err;
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
   int timeout = I2CWT(L, 3);
   // Get the address from Lua
   uint16_t address = (uint16_t) luaL_checkinteger(L, 2);
   i2cm->busy=TRUE;
   ThreadMutex_release(soDispMutex);
   esp_err_t ret_val = i2c_master_probe(i2cm->bus_handle, address, timeout); 
   ThreadMutex_set(soDispMutex);
   i2cm->busy=FALSE;
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
   int timeout = I2CWT(L, 4);

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
      i2cm->busy=TRUE;
      ThreadMutex_release(soDispMutex);
      ret_val = i2c_master_transmit(i2cm->dev_handle, wbuf.data, wbuf.len, timeout); 
      ThreadMutex_set(soDispMutex);
      i2cm->busy=FALSE;
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
   lua_Integer recblen = luaL_checkinteger(L, 3);
   int timeout = I2CWT(L, 4);

   // Configure the device address
   esp_err_t ret_val = I2CMaster_addDevice(i2cm, address);

   if(ret_val == ESP_OK)
   {
      if(recblen <= 0)
      {
         throwInvArg(L, "no recbuf");
      }

      uint8_t* recbuf = (uint8_t*)lua_newuserdatauv(L, recblen, 0);

      if(recbuf)
      {
         i2cm->busy=TRUE;
         ThreadMutex_release(soDispMutex);
         ret_val = i2c_master_receive(i2cm->dev_handle, recbuf, recblen, timeout);
         ThreadMutex_set(soDispMutex);
         i2cm->busy=FALSE;

         if(ret_val == ESP_OK)
         {
            lua_pushlstring(L, (char*)recbuf, recblen);
            return 1;
         }
         
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
   int timeout = I2CWT(L, 5);

   esp_err_t ret_val = I2CMaster_addDevice(i2cm, address);

   if(ret_val == ESP_OK)
   {
      write_buffer_t wbuf;
      I2CMaster_getWriteData(L, 3, &wbuf);
      lua_Integer recblen = luaL_checkinteger(L, 4);

      if(recblen <= 0 || wbuf.len == 0)
      {
         throwInvArg(L, "invalid buffer length");
      }

      uint8_t* recbuf = (uint8_t*)lua_newuserdatauv(L, recblen, 0);

      if(recbuf)
      {
         i2cm->busy=TRUE;
         ThreadMutex_release(soDispMutex);
         ret_val = i2c_master_transmit_receive(i2cm->dev_handle, wbuf.data, wbuf.len, recbuf, recblen, timeout);
         ThreadMutex_set(soDispMutex);
         i2cm->busy=FALSE;

         if(ret_val == ESP_OK)
         {
            lua_pushlstring(L, (char*)recbuf, recblen);
            return 1;
         }
         
      }
      else
      {
         ret_val = ESP_ERR_NO_MEM;
      }
   }

   return pushEspRetVal(L, ret_val, 0, FALSE);
}

/* Implement i2c:close(), __close and __gc, including partial construction.
   Input: I2C userdata at L[1], with the dispatcher mutex held. Remove its device
   and bus, preserving failed teardown for recovery. Busy returns nil, "Busy".
   Pushes true and returns 1 on success, or nil plus error and returns 2.
   A wrong userdata type raises a Lua argument error.
 */
static int I2CMaster_close(lua_State* L)
{
   LI2CMaster* o=I2CMaster_getUD(L);
   esp_err_t err=ESP_OK;
   if(o->busy) return pushErr(L,"Busy");
   o->closing=TRUE;
   if(o->dev_handle)
   {
      err=i2c_master_bus_rm_device(o->dev_handle);
      if(err != ESP_OK) goto done;
      o->dev_handle=NULL;
   }
   if(o->bus_handle)
   {
      err=i2c_del_master_bus(o->bus_handle);
      if(err != ESP_OK) goto done;
      o->bus_handle=NULL;
   }
done:
   return hardwareStatus(L,hardwareCleanup(L,o,err));
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
  lua_Integer port = luaL_checkinteger(L, 1);
  
  if((port < 0) || (port >= I2C_NUM_MAX))
  {
     luaL_error(L, "Port num. range err.");
  }
  lua_Integer sda = luaL_checkinteger(L, 2);
  lua_Integer scl = luaL_checkinteger(L, 3);
  if(!GPIO_IS_VALID_OUTPUT_GPIO(sda))
     luaL_argerror(L, 2, "Invalid pin");
  if(!GPIO_IS_VALID_OUTPUT_GPIO(scl) || scl == sda)
     luaL_argerror(L, 3, "Invalid pin");
   // Allocate memory for the new I2C master object and register it in Lua
  LI2CMaster* i2cm = (LI2CMaster*)lNewUdataUV(L, sizeof(LI2CMaster), BAI2CMASTER, i2cMasterLib,1);
  // Configure the I2C bus parameters (clock source, port, SDA, SCL, and pullup resistors)
  i2cm->bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  i2cm->bus_cfg.i2c_port = port;
  i2cm->bus_cfg.sda_io_num = sda;
  i2cm->bus_cfg.scl_io_num = scl;
  i2cm->bus_cfg.glitch_ignore_cnt = 7;
  i2cm->bus_cfg.flags.enable_internal_pullup = 1;
  // Configure the I2C device parameters (7-bit address, initial address 0, and bus speed)
   i2cm->dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  i2cm->dev_cfg.device_address = 0;
  i2cm->dev_cfg.scl_speed_hz = luaL_checkinteger(L, 4);// speed 
  // Reject occupied pins before the driver can reconfigure them.
  if(activeGPIO[sda])
     throwPinInUse(L, sda);
  if(activeGPIO[scl])
     throwPinInUse(L, scl);
  // Create a new I2C master bus with the configured settings
  esp_err_t ret_val = i2c_new_master_bus(&i2cm->bus_cfg, &i2cm->bus_handle);
  // Check if bus creation was successful; if not, return an error
  if(ESP_OK != ret_val)
  {
     return pushEspRetVal(L, ret_val, "i2cm", TRUE);
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
   int selfRef;
   LRmtBase super;
   rmt_receive_config_t cfg;
   rmt_channel_handle_t rxChannel;
   gpio_num_t pin;
   rmt_symbol_word_t* symBuf;
   size_t symSize;
   size_t receivedSymbols;
   BaBool overflow;
   atomic_bool pendingReceive, ready;
   BaBool enabled, closing, jobQueued;
   rmt_symbol_word_t* received;
} LRmtRx;


typedef struct LRmtTx
{
   int selfRef;
   LRmtBase super;
   LRmtRx* rx; /* Set when linked on the same GPIO */
   rmt_channel_handle_t txChannel;
   rmt_encoder_handle_t txEncoder;
   BaBool enabled, closing, jobQueued;
   atomic_uint completed;
   gpio_num_t pin;
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
   if(!o->txChannel || o->closing)
      luaL_error(L,"CLOSED");
   return o;
}

static LRmtRx* LRmtRx_checkUD(lua_State* L)
{
   LRmtRx* o = LRmtRx_getUD(L);
   if(!o->rxChannel || o->closing)
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
         size_t bytes=lua_rawlen(L,-1);
         if(bytes > (INT_MAX/sizeof(rmt_symbol_word_t)-(size_t)bufLen)/8)
            return luaL_error(L,"Too many symbols");
         bufLen += bytes*8;
         lua_pop(L,1);
      }
      else
      {
         if(bufLen == INT_MAX/sizeof(rmt_symbol_word_t))
            return luaL_error(L,"Too many symbols");
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
   /* Root the payload before handing it to an asynchronous driver. */
   lua_pushvalue(L,4);
   int payloadRef=luaL_ref(L,LUA_REGISTRYINDEX);
   esp_err_t status=rmt_transmit(
      o->txChannel,o->txEncoder,symbols,sizeof(rmt_symbol_word_t)*bufLen,&cfg);
   if(ESP_OK == status)
   {
      o->transQueueBufRef[o->transQueueBufHead]=payloadRef;
      o->transQueueBufHead = nextTransQueueBufHead;
   }
   else luaL_unref(L,LUA_REGISTRYINDEX,payloadRef);
   return pushEspRetVal(L,status,"transmit",TRUE);
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
      balua_wkUnref(L,o->selfRef); /* Invalidate completions from before disable. */
      lua_pushvalue(L,1);
      o->selfRef=balua_wkRef(L);
      o->jobQueued=FALSE;
      status=rmt_enable(o->txChannel);
      if(ESP_OK == status)
         o->enabled=TRUE;
   }
   return pushEspRetVal(L, status, NULL, TRUE);
}


/* Stop TX before releasing payload buffers retained for asynchronous transmission.
   Inputs: live TX userdata o and locked Lua state L holding its registry.
   Returns the native rmt_disable status. On failure buffers/state are retained;
   on success clears enabled/completions, unreferences queued buffers and resets
   the queue indices. Leaves the Lua stack unchanged; returns no Lua results.
 */
static int LRmtTx_disableAndReleaseBuf(LRmtTx* o,lua_State* L)
{
   int status=o->enabled ? rmt_disable(o->txChannel) : ESP_OK;
   if(status != ESP_OK) return status;
   o->enabled=FALSE;
   atomic_store(&o->completed,0);
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
   }
   else
   {
      status=ESP_ERR_INVALID_ARG;
   }
   return pushEspRetVal(L,status,NULL,TRUE);
}

/* Disable TX, release payloads/encoder/channel and unlink its RX partner.
   Inputs: locked Lua state L and RMT TX userdata pointer o. Keep the owner on
   the stack or reachable as an RMT partner for hardwareCleanup(). Supports
   partial construction and repeated close; failed native teardown is retained
   for recovery. Returns ESP_OK or the native failure code, NOT a Lua result
   count. Leaves the Lua stack unchanged.
   A NULL o is accepted and returns ESP_OK.
 */
static esp_err_t LRmtTx_del(LRmtTx* o, lua_State* L)
{
   esp_err_t err=ESP_OK;
   if(!o) return ESP_OK;
   o->closing=TRUE;
   if(o->txChannel)
   {
      err=LRmtTx_disableAndReleaseBuf(o,L);
      if(err != ESP_OK) goto done;
      err=rmt_del_channel(o->txChannel);
      if(err != ESP_OK) goto done;
      o->txChannel=NULL;
   }
   if(o->txEncoder)
   {
      err=rmt_del_encoder(o->txEncoder);
      if(err != ESP_OK) goto done;
      o->txEncoder=NULL;
   }
   if(o->rx)
   {
      o->rx->super.tx=NULL;
      if(pushHardwareOwner(L,o->rx))
      {
         lua_pushnil(L);
         lua_setiuservalue(L,-2,2);
         lua_pop(L,1);
      }
      o->rx=NULL;
   }
   if(pushHardwareOwner(L,o))
   {
      lua_pushnil(L);
      lua_setiuservalue(L,-2,2);
      lua_pop(L,1);
   }
done:
   return hardwareCleanup(L,o,err);
}


static int LRmtTx_close(lua_State* L)
{
   return hardwareStatus(L,LRmtTx_del(LRmtTx_getUD(L),L));
}


/* This CB runs in the context of a thread in the LThreadMgr.
 */
static void LRmtTx_executeOnTxComplete(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   lua_State* L=jb->Lt;
   int ref=((GpioThreadJob*)jb)->selfRef;
   LRmtTx* o=(LRmtTx*)eventOwner(L,ref);
   if(!o) return;
   while(o->txChannel && o->enabled && !o->closing && o->selfRef == ref &&
         atomic_load(&o->completed))
   {
      atomic_fetch_sub(&o->completed,1);
      if(o->transQueueBufTail != o->transQueueBufHead)
      {
         luaL_unref(L,LUA_REGISTRYINDEX,o->transQueueBufRef[o->transQueueBufTail]);
         if(++o->transQueueBufTail == o->transQueueBufLen) o->transQueueBufTail=0;
         if(lua_getiuservalue(L,msgh+1,1) == LUA_TFUNCTION)
         {
            if(lua_pcall(L,0,0,msgh) != LUA_OK)
            {
               LRmtTx_del(o,L);
               break;
            }
         }
         else lua_pop(L,1);
      }
   }
   if(o->selfRef == ref) o->jobQueued=FALSE;
}


/* This CB runs in the context of the eventBrokerTask.
 */
static void LRmtTx_onTxComplete(EventBrokerCallbackArg arg)
{
   LRmtTx* o = (LRmtTx*)eventBegin(arg);
   if(o && o->txChannel && o->enabled && !o->closing && !o->jobQueued &&
      atomic_load(&o->completed))
   {
      o->jobQueued=TRUE;
      dispatchThreadJob(arg.ref,LRmtTx_executeOnTxComplete);
   }
   eventEnd();
}


static bool IRAM_ATTR LRmtTx_interruptHandler(
   rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *arg)
{
   BaseType_t hwakeup = pdFALSE;
   LRmtTx* tx = (LRmtTx*)arg;
   if(tx->txChannel)
   {
      LRmtRx* rx = tx->rx;
      if(rx && atomic_exchange(&rx->pendingReceive,FALSE))
      {
         if(ESP_OK != rmt_receive(rx->rxChannel,rx->symBuf,rx->symSize,&rx->cfg))
         {
            /* Should not be possible, but if it happens, signal error
             * to RX callback as follows:
             */
            EventBrokerQueueNode n = {
               .callback=LRmtRx_onRxComplete,
               .arg.ref=rx->selfRef
            };
            rx->receivedSymbols=0;
            rx->overflow = TRUE;
            atomic_store(&rx->ready,TRUE);
            if(xQueueSendFromISR(eventBrokerQueue,&n,&hwakeup) != pdTRUE)
               atomic_store(&hardwareRetry,TRUE);
         }
      }
      EventBrokerQueueNode n = {
         .callback=LRmtTx_onTxComplete,
         .arg.ref=tx->selfRef
      };
      atomic_fetch_add(&tx->completed,1);
      if(xQueueSendFromISR(eventBrokerQueue,&n,&hwakeup) != pdTRUE)
         atomic_store(&hardwareRetry,TRUE);
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


/* Read a frequency from the configuration table at Lua stack index 1.
   Inputs: locked Lua state L, field name, and minimum Hz (1 for resolution,
   0 for carrier frequency, where zero disables modulation). Returns the
   uint32_t value in [minimum, 80000000]; pushes no Lua results and leaves the
   stack unchanged. Raises Invalidarg before narrowing an out-of-range Lua
   integer. Actual divider/carrier tick limits are checked by PatchRmt.cmake
   in the driver, which knows the shared group's effective clock frequency.
 */
static uint32_t rmtCheckHz(lua_State* L, const char* name, int minimum)
{
   lua_Integer hz=balua_checkIntField(L,1,name);
   if(hz < minimum || hz > 80000000)
      throwInvArg(L,name);
   return (uint32_t)hz;
}

/* Create a disabled RMT transmitter using the configuration table at index 1
   and optional RX userdata at index 2 for sharing its GPIO. Caller holds the
   dispatcher mutex. Validate frequencies before conversion; the driver checks
   the actual divider and carrier high/low ticks before programming hardware.
   On success, push/return one TX userdata, retaining a paired RX in uservalue 2.
   Invalid arguments raise a Lua error; other native failures return nil,error.
   The userdata finalizer owns partial construction; checked driver failures
   also attempt immediate teardown before returning/raising.
 */
static int LRmtTx_create(lua_State* L)
{
   luaL_checktype(L, 1, LUA_TTABLE);
   rmt_tx_channel_config_t txCfg = {
      .gpio_num = (uint32_t)balua_checkIntField(L,1,"gpio"),
      .clk_src = RMT_CLK_SRC_DEFAULT, // select clock source
      .resolution_hz = rmtCheckHz(L,"resolution",1),
      .mem_block_symbols = (uint32_t)balua_getIntField(L,1,"mem",64),
      .trans_queue_depth = (uint32_t)balua_getIntField(L,1,"queue",4),
      .intr_priority = (int)balua_getIntField(L,1,"priority",0),
      .flags.invert_out=balua_getBoolField(L,1,"invert", FALSE),
      .flags.with_dma=balua_getBoolField(L,1,"dma", FALSE),
   };
      
   if(txCfg.trans_queue_depth < 1 || txCfg.trans_queue_depth > (INT_MAX-sizeof(LRmtTx))/sizeof(int))
      return pushEspRetVal(L,ESP_ERR_INVALID_ARG,"queue",TRUE);
   if(!GPIO_IS_VALID_OUTPUT_GPIO(txCfg.gpio_num))
      luaL_argerror(L, 1, "Invalid GPIO pin");
   /* Parse before acquiring a channel; frequency zero leaves carrier off. */
   rmt_carrier_config_t cCfg={};
   lua_getfield(L,1,"dutycycle");
   if(lua_isnumber(L,-1))
   {
      cCfg=(rmt_carrier_config_t){
         .duty_cycle=(float)lua_tonumber(L,-1),
         .frequency_hz=rmtCheckHz(L,"frequency",0),
         .flags.polarity_active_low=balua_getBoolField(L,1,"polaritylow",FALSE)
      };
   }
   lua_pop(L,1);
   LRmtRx* rx=NULL;
   bool hasRx = LUA_TUSERDATA == lua_type(L, 2);
   LRmtTx* tx = (LRmtTx*)lNewUdataUV(
      L,sizeof(LRmtTx)+sizeof(int)*txCfg.trans_queue_depth,BARMTTX,rmtTxLib,2);
   tx->super.tx=tx;
   if(activeGPIO[txCfg.gpio_num])
   {
      if(hasRx)
      {
         rx = (LRmtRx*)luaL_checkudata(L,2,BARMTRX);
         if((LRmtRx*)activeGPIO[txCfg.gpio_num] == rx && rx->rxChannel && !rx->closing && !rx->super.tx)
         {
            goto L_ok;
         }
      }
      throwPinInUse(L,txCfg.gpio_num);
   }
  L_ok:
   /* TX keeps its paired RX alive; RX's reverse pointer is unlinked on close. */
   if(rx)
   {
      lua_pushvalue(L,2);
      lua_setiuservalue(L,-2,2);
   }
   lua_pushvalue(L,-1);
   tx->selfRef=balua_wkRef(L);
   tx->transQueueBufLen=txCfg.trans_queue_depth+1;
   tx->pin=txCfg.gpio_num;
   esp_err_t status=hardwareInit(HW_RMT_TX,&txCfg,&tx->txChannel,NULL);
   if(ESP_OK == status)
   {
      if(balua_getBoolField(L,1,"opendrain",FALSE))
      {
         gpio_od_enable(tx->pin);
      }
      if(cCfg.frequency_hz)
      {
         status=rmt_apply_carrier(tx->txChannel, &cCfg);
         if(ESP_OK != status)
            goto L_err;
      }
      rmt_tx_event_callbacks_t cb = {
         .on_trans_done = LRmtTx_interruptHandler
      };
      status=rmt_tx_register_event_callbacks(tx->txChannel, &cb, tx);
      if(ESP_OK == status)
      {
         rmt_copy_encoder_config_t cfg={};
         status=rmt_new_copy_encoder(&cfg, &tx->txEncoder);
         if(ESP_OK == status)
         {
            lua_getfield(L, 1, "callback");
            if(lua_isfunction(L, -1))
            {
               lua_pushvalue(L,-1);
               lua_setiuservalue(L,-3,1);
            }
            lua_pop(L,1);
            if(rx)
            {
               tx->rx=rx;
               rx->super.tx=tx;
               lua_pushvalue(L,-1);
               lua_setiuservalue(L,2,2);
            }
            else
               activeGPIO[tx->pin]=(LGPIO*)tx;
            return 1;
         }
      }
     L_err:
      LRmtTx_del(tx,L);
   }
   return pushEspRetVal(L,status,NULL,TRUE);
}

/* Close linked TX first, then disable/delete RX and release its symbol buffer.
   Inputs: locked Lua state L and RMT RX userdata pointer o. Keep the owner on
   the stack or reachable as an RMT partner for hardwareCleanup(). Supports
   partial construction and repeated close; failed native teardown is retained
   for recovery. Returns ESP_OK or the native failure code, NOT a Lua result
   count. Leaves the Lua stack unchanged.
 */
static esp_err_t LRmtRx_del(lua_State* L, LRmtRx* o)
{
   esp_err_t err=ESP_OK;
   o->closing=TRUE;
   err=LRmtTx_del(o->super.tx,L);
   if(err != ESP_OK) goto done;
   if(o->enabled)
   {
      err=rmt_disable(o->rxChannel);
      if(err != ESP_OK) goto done;
      o->enabled=FALSE;
   }
   if(o->rxChannel)
   {
      err=rmt_del_channel(o->rxChannel);
      if(err != ESP_OK) goto done;
      o->rxChannel=NULL;
   }
   if(o->symBuf)
   {
      baFree(o->symBuf);
      o->symBuf=NULL;
   }
done:
   return hardwareCleanup(L,o,err);
}


/* This CB runs in the context of a thread in the LThreadMgr.
 */
static void LRmtRx_executeOnRxComplete(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   LRmtRx* o = (LRmtRx*)eventOwner(jb->Lt,((GpioThreadJob*)jb)->selfRef);
   if(o && o->rxChannel && !o->closing && o->symBuf && atomic_exchange(&o->ready,FALSE))
   {
      lua_State* L = jb->Lt;
      rmt_symbol_word_t* symPtr=o->received;
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
      lua_getiuservalue(L,msgh+1,1);
      lua_rotate(L, -2, 1);
      lua_pushboolean(L, o->overflow);
      baFree(o->symBuf);
      o->symBuf=0;
      o->pendingReceive=FALSE;
      o->overflow=FALSE;
      o->jobQueued=FALSE;
      if(LUA_OK != lua_pcall(L, 2, 0, msgh))
         LRmtRx_del(L, o);
   }
}


/* This CB runs in the context of the eventBrokerTask.
 */
static void LRmtRx_onRxComplete(EventBrokerCallbackArg arg)
{
   LRmtRx* o = (LRmtRx*)eventBegin(arg);
   if(o && o->rxChannel && !o->closing && !o->jobQueued && atomic_load(&o->ready))
   {
      o->jobQueued=TRUE;
      dispatchThreadJob(arg.ref,LRmtRx_executeOnRxComplete);
   }
   eventEnd();
}


static bool IRAM_ATTR LRmtRx_interruptHandler(
   rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* edata, void* arg)
{
   BaseType_t hwakeup = pdFALSE;
   LRmtRx* o = (LRmtRx*)arg;
   if(o && o->symBuf)
   {
      EventBrokerQueueNode n = {
         .callback=LRmtRx_onRxComplete,
         .arg.ref=o->selfRef
      };
      o->received=edata->received_symbols;
      o->receivedSymbols=edata->num_symbols;
      o->overflow = !edata->flags.is_last;
      atomic_store(&o->ready,TRUE);
      if(xQueueSendFromISR(eventBrokerQueue,&n,&hwakeup) != pdTRUE)
         atomic_store(&hardwareRetry,TRUE);
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
   lua_Integer len=balua_getIntField(L,2,"len",512);
   if(len <= 0 || len > INT_MAX/sizeof(rmt_symbol_word_t)) return throwInvArg(L,"len");
   o->symSize=(size_t)len*sizeof(rmt_symbol_word_t);
   BaBool defer = balua_getBoolField(L,2,"defer", FALSE);
   o->symBuf = (rmt_symbol_word_t*)baLMalloc(L, o->symSize);
   if(o->symBuf)
   {
       /* if tx and rx linked */
      if(o->super.tx && defer)
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
   return hardwareStatus(L,LRmtRx_del(L,LRmtRx_getUD(L)));
}



static const luaL_Reg rmtRxLib[] = {
   {"receive", LRmtRx_receive},
   {"close", LRmtRx_close},
   {"__close", LRmtRx_close},
   {"__gc", LRmtRx_close},
   {NULL, NULL}
};


/* Create and enable an RMT receiver from the configuration table at index 1,
   including its required callback. Caller holds the dispatcher mutex. Check
   resolution before conversion; the driver rejects unrepresentable dividers
   before programming hardware. On success, push/return one RX userdata whose
   uservalue 1 retains the callback. Invalid arguments raise a Lua error; other
   native failures return nil,error. Partial native initialization is torn down
   before reporting a driver failure; GC handles earlier Lua parsing failures.
 */
static int LRmtRx_create(lua_State* L)
{
   luaL_checktype(L, 1, LUA_TTABLE);
   rmt_rx_channel_config_t cfg = {
      .gpio_num = (uint32_t)balua_checkIntField(L,1,"gpio"),
      .clk_src = RMT_CLK_SRC_DEFAULT, // select clock source
      .resolution_hz = rmtCheckHz(L,"resolution",1),
      .mem_block_symbols = (uint32_t)balua_getIntField(L,1,"mem",64),
      .intr_priority = (int)balua_getIntField(L,1,"priority",0),
      .flags.invert_in=balua_getBoolField(L,1,"invert", FALSE),
      .flags.with_dma=balua_getBoolField(L,1,"dma", FALSE),
   };
   if(cfg.gpio_num < GPIO_NUM_0 || cfg.gpio_num >= GPIO_NUM_MAX)
      luaL_argerror(L, 1, "Invalid GPIO pin");
   if(activeGPIO[cfg.gpio_num])
      throwPinInUse(L,cfg.gpio_num);
   LRmtRx* rx = (LRmtRx*)lNewUdataUV(L,sizeof(LRmtRx),BARMTRX,rmtRxLib,2);
   rx->pin=cfg.gpio_num;
   lua_getfield(L, 1, "callback");
   if(!lua_isfunction(L, -1))
      luaL_error(L,"Callback required");
   rx->selfRef=lReferenceCallback(L, lua_absindex(L,-2), lua_absindex(L,-1));
   lua_pop(L,1);
   esp_err_t status=hardwareInit(HW_RMT_RX,&cfg,&rx->rxChannel,NULL);
   if(ESP_OK == status)
   {
      rmt_rx_event_callbacks_t cbs = {
         .on_recv_done = LRmtRx_interruptHandler,
      };
      status=rmt_rx_register_event_callbacks(rx->rxChannel, &cbs, rx);
      if(ESP_OK == status)
      {
         status=rmt_enable(rx->rxChannel);
         if(ESP_OK == status)
         {
            rx->enabled=TRUE;
            activeGPIO[rx->pin]=(LGPIO*)rx;
            return 1;
         }
      }
   }
   LRmtRx_del(L,rx);
   return pushEspRetVal(L,status,NULL,TRUE);
}



/*********************************************************************
 *********************************************************************
                                  UART
 *********************************************************************
 *********************************************************************/

#define BALUART "UART"


typedef struct {
   int selfRef;
   QueueHandle_t uartQueue; /* Used by uartTask */
   uart_port_t port;
   int patternLen;
   TaskHandle_t uartTaskHandle;
   BaBool busy, closing;
} LUart;


typedef struct {
   ThreadJob super;
   int selfRef;
   uart_event_type_t etype;
} LUartJob;

static int Uart_close(lua_State* L, LUart* o);


/* This function runs in the context of a thread in the LThreadMgr.
 */
static void executeLuaUartCB(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   luaL_Buffer lb;
   char* buf;
   int len=0;
   LUartJob* ujob=(LUartJob*)jb;
   lua_State* L = jb->Lt;
   LUart* lu = (LUart*)eventOwner(L,ujob->selfRef);
   if(!lu || lu->port == UART_NUM_MAX || lu->closing) return;
   const char* emsg = 0;
   int luaStatus = LUA_OK;
   /* Push user's callback on the stack */
   lua_getiuservalue(L,msgh+1,1);
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
         if(lu->port == UART_NUM_MAX) return;
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
            if(len >= 0)
            {
               luaL_buffinit(L, &lb);
               buf = luaL_prepbuffsize(&lb, len);
               int actual=uart_read_bytes(lu->port,buf,len,0);
               if(actual < 0) actual=0;
               len=actual;
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
         if(lu->patternLen) break; /* Preserve bytes for the pattern event. */
         size_t size;
         if(lu->port < UART_NUM_MAX)
         {
            size=0;
            uart_get_buffered_data_len(lu->port, &size);
            len=(int)size;
            if(len > 0)
            {
               luaL_buffinit(L, &lb);
               buf = luaL_prepbuffsize(&lb, len);
               int actual=uart_read_bytes(lu->port,buf,len,0);
               if(actual < 0) actual=0;
               len=actual;
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
      if(lu->busy)
      {
         lu->closing=TRUE;
         hardwareCleanup(L,lu,ESP_ERR_INVALID_STATE);
      }
      else Uart_close(L,lu);
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
         ThreadMutex_set(soDispMutex);
         LUartJob* ujob=(LUartJob*)ThreadJob_lcreate(
            sizeof(LUartJob),executeLuaUartCB);
         if( ! ujob )
            baFatalE(FE_MALLOC,0);
         ujob->selfRef=lu->selfRef;
         ujob->etype = event.type;
         LThreadMgr_run(&ltMgr, (ThreadJob*)ujob);
         if(UART_BUFFER_FULL == event.type || UART_FIFO_OVF == event.type)
         {
            uart_flush_input(lu->port);
            xQueueReset(lu->uartQueue);
         }
         ThreadMutex_release(soDispMutex);
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
   if(o->port == UART_NUM_MAX || o->closing)
      luaL_error(L,"CLOSED");
   if(o->busy) luaL_error(L,"Busy");
   return o;
}


/* uart:read([timeout]) */
static int Uart_read(lua_State* L)
{
   size_t size;
   LUart* o = Uart_checkUD(L);
   if(o->selfRef)
      return luaL_error(L,"RX callback");
   lua_Integer msSec = luaL_optinteger(L, 2, 0);
   if(msSec < 0 || msSec > INT_MAX) return throwInvArg(L,"timeout");
   esp_err_t err=uart_get_buffered_data_len(o->port,&size);
   if(err != ESP_OK) return hardwareStatus(L,err);
   if(size || msSec)
   {
      luaL_Buffer lb;
      if(0 == size)
         size=1024;
      luaL_buffinit(L, &lb);
      char* buf = luaL_prepbuffsize(&lb, size);
      o->busy=TRUE;
      ThreadMutex_release(soDispMutex);
      int len = uart_read_bytes(
         o->port,buf,size,msSec ? (TickType_t)(msSec/portTICK_PERIOD_MS) : 0);
      ThreadMutex_set(soDispMutex);
      o->busy=FALSE;
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
   o->busy=TRUE;
   ThreadMutex_release(soDispMutex);
   int written=uart_write_bytes(o->port, data, size);
   ThreadMutex_set(soDispMutex);
   o->busy=FALSE;
   if(written < 0 || (size_t)written != size) return pushErr(L,"write failed");
   return 0;
}

/* uart:txsize(data) */
static int Uart_txsize(lua_State* L)
{
   size_t size;
   esp_err_t err=uart_get_tx_buffer_free_size(Uart_checkUD(L)->port,&size);
   if(err != ESP_OK) return hardwareStatus(L,err);
   lua_pushinteger(L,size);
   return 1;
}

/* Stop the UART event task and delete the driver.
   Inputs: locked Lua state L and UART userdata pointer o. Keep the owner on
   L's stack for hardwareCleanup(). Supports
   partial construction and repeated close; failed native teardown is retained
   for recovery. Returns ESP_OK or the native failure code, NOT a Lua result
   count. Leaves the Lua stack unchanged.
   A busy object returns ESP_ERR_INVALID_STATE without teardown.
 */
static int Uart_close(lua_State* L, LUart* o)
{
   esp_err_t err=ESP_OK;
   if(o->busy) return ESP_ERR_INVALID_STATE;
   o->closing=TRUE;
   if(o->uartTaskHandle)
   {
      vTaskDelete(o->uartTaskHandle);
      o->uartTaskHandle=NULL;
   }
   if(o->port < UART_NUM_MAX)
   {
      err=uart_driver_delete(o->port);
      if(err == ESP_OK) o->port=UART_NUM_MAX;
   }
   return hardwareCleanup(L,o,err);
}

/* uart:close() */
static int Uart_lclose(lua_State* L)
{
   return hardwareStatus(L,Uart_close(L,Uart_getUD(L)));
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
   lua_Integer port=luaL_checkinteger(L, 1);
   if(port < UART_NUM_0 || port >= UART_NUM_MAX)
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
   int chr_tout = (int)balua_getIntField(L, 2, "timeout", pattern ? 9 : 0);
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
   int pins[]={txpin,rxpin,rtspin,ctspin};
   for(int i=0;i<4;++i)
      if(pins[i] != UART_PIN_NO_CHANGE)
      {
         if(!GPIO_IS_VALID_GPIO(pins[i]) ||
            ((i==0 || i==2) && !GPIO_IS_VALID_OUTPUT_GPIO(pins[i])))
            return throwInvArg(L,"pin");
         if(activeGPIO[pins[i]]) return throwPinInUse(L,pins[i]);
      }
   LUart* lu=(LUart*)lNewUdataUV(L,sizeof(LUart),BALUART,uartObjLib,1);
   lu->port=UART_NUM_MAX;
   lu->patternLen=hasCB && pattern ? strlen(pattern) : 0;
   if(hasCB) lu->selfRef=lReferenceCallback(L,lua_absindex(L,-1),3);
   QueueHandle_t uartQueue=0;
   if(rxbufsize < 1024) rxbufsize=1024;
   if(txbufsize < 1024) txbufsize=1024;
   esp_err_t err=uart_driver_install(port,rxbufsize,txbufsize,10,hasCB ? &uartQueue : 0,0);
   if(err != ESP_OK) return pushEspRetVal(L,err,"install",FALSE);
   lu->port=port;
   for(int i=0;i<4;++i)
      if(pins[i] != UART_PIN_NO_CHANGE) activeGPIO[pins[i]]=(LGPIO*)lu;
   err=uart_param_config(port, &cfg);
   if(err != ESP_OK) goto fail;
   err=uart_set_pin(port, txpin, rxpin, rtspin, ctspin);
   if(err != ESP_OK) goto fail;
   if(rs485)
   {
      err=uart_set_mode(port,UART_MODE_RS485_HALF_DUPLEX);
      if(err != ESP_OK) goto fail;
   }
   if(hasCB)
   {
      if(pattern)
      {
         err=uart_enable_pattern_det_baud_intr(
            port,pattern[0],(uint8_t)strlen(pattern),chr_tout,post_idle,pre_idle);
         if(err != ESP_OK) goto fail;
         if(maxlen)
         {
            err=uart_pattern_queue_reset(port,maxlen);
            if(err != ESP_OK) goto fail;
         }
      }
      else if(chr_tout > 0)
      {
         err=uart_set_rx_timeout(port,chr_tout);
         if(err != ESP_OK) goto fail;
      }
   }
   if(hasCB)
   {
      lu->uartQueue=uartQueue;
      if(xTaskCreate(uartTask,"uart",1024,lu,configMAX_PRIORITIES-1,
                     &lu->uartTaskHandle) != pdPASS)
      {
         err=ESP_ERR_NO_MEM;
         goto fail;
      }
   }
   return 1;
fail:
   Uart_close(L,lu);
   return hardwareStatus(L,err);
}


/* Retry failed native cleanup and resume work whose notification was lost.
   Inputs: LThreadMgr job jb supplies the locked Lua state jb->Lt; msgh is its
   Lua error-handler index; mgr is unused. No hardware identity is required.
   Snapshot cleanupOwners before calling __gc, because cleanup mutates that
   table. Then scan activeGPIO, including linked RMT TX, and schedule pending
   work using each peripheral's existing duplicate-job guards.
   Returns no value or Lua results; temporary Lua values are removed, leaving
   the entry stack height unchanged. Failed cleanup requests another retry;
   recoveryQueued is cleared when this pass finishes.
 */
static void hardwareRecover(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   lua_State* L=jb->Lt;
   lua_newtable(L);
   int owners=lua_gettop(L), count=0;
   lua_rawgeti(L,LUA_REGISTRYINDEX,cleanupOwners);
   lua_pushnil(L);
   while(lua_next(L,-2)) lua_rawseti(L,owners,++count);
   lua_pop(L,1);
   for(int i=1;i<=count;++i)
   {
      lua_rawgeti(L,owners,i);
      luaL_getmetafield(L,-1,"__gc");
      lua_pushvalue(L,-2);
      if(lua_pcall(L,1,0,msgh) != LUA_OK)
      {
         lua_pop(L,1);
         atomic_store(&hardwareRetry,TRUE);
      }
      lua_pop(L,1);
   }
   lua_pop(L,1);
   for(int pin=0;pin<GPIO_NUM_MAX;++pin)
   {
      void* owner=activeGPIO[pin];
      if(!owner || owner == (void*)1) continue;
      /* Read the common first member, then root the userdata before using it. */
      int ref=*(int*)owner;
      while(ref)
      {
         balua_wkPush(L,ref);
         LRmtTx* tx=luaL_testudata(L,-1,BARMTTX);
         LRmtRx* rx=luaL_testudata(L,-1,BARMTRX);
         LLEDC* led=luaL_testudata(L,-1,BALEDC);
         LADC* adc=luaL_testudata(L,-1,BAADC);
         LPCNT* pcnt=luaL_testudata(L,-1,BAPCNT);
         if(tx && tx->enabled && !tx->closing && !tx->jobQueued && atomic_load(&tx->completed))
         {
            tx->jobQueued=TRUE;
            dispatchThreadJob(ref,LRmtTx_executeOnTxComplete);
         }
         if(rx && rx->enabled && !rx->closing && !rx->jobQueued && atomic_load(&rx->ready))
         {
            rx->jobQueued=TRUE;
            dispatchThreadJob(ref,LRmtRx_executeOnRxComplete);
         }
         if(led && led->running && !led->closing && !led->jobQueued && ledPending(led,FALSE))
         {
            led->jobQueued=TRUE;
            dispatchThreadJob(ref,executeLuaLedEventCB);
         }
         if(adc && adc->type == 2 && adc->u.contin.started && !adc->closing && !adc->u.contin.running)
         {
            adc->u.contin.running=TRUE;
            dispatchThreadJob(ref,adcExecuteLuaEventCB);
         }
         if(pcnt && pcnt->enabled && !pcnt->closing)
         {
            BaBool schedule=FALSE;
            portENTER_CRITICAL(&pcntMux);
            if(pcnt->qHead != pcnt->qTail && !pcnt->lCbRunning)
            {
               pcnt->lCbRunning=TRUE;
               schedule=TRUE;
            }
            portEXIT_CRITICAL(&pcntMux);
            if(schedule) dispatchThreadJob(ref,LPCNT_onReachLuaCB);
         }
         /* A shared RMT pin lists RX only; TX also needs completion recovery.
            Existing job flags suppress duplicates from multi-pin owners. */
         ref=rx && rx->super.tx ? rx->super.tx->selfRef : 0;
         lua_pop(L,1);
      }
   }
   recoveryQueued=FALSE;
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

/* Copy src including its terminator into the size-byte network field dst.
   Raises Invalidarg before copying an oversized string. L must be locked;
   this helper returns no value and leaves the Lua stack unchanged on success. */
static void netCopyString(lua_State* L, char* dst, size_t size, const char* src)
{
   if(strlen(src) >= size)
      throwInvArg(L, "network string length");
   strcpy(dst, src);
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
   netCopyString(L, cfg.adapter, sizeof(cfg.adapter), luaL_checkstring(L, 1));
   // Disconnects the network when the configuration table argument is not provided. 
   if(lua_gettop(L) == 1)
   {
      strcpy(cfg.adapter, "close");   
   }
   // Load the parameters for wifi.
   else if(!strcmp("wifi", cfg.adapter))
   {
      luaL_checktype(L, 2, LUA_TTABLE);
      netCopyString(L, cfg.ssid, sizeof(cfg.ssid), balua_checkStringField(L, 2, "ssid"));
      netCopyString(L, cfg.password, sizeof(cfg.password), balua_checkStringField(L, 2, "pwd"));
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
   // Load the parameters for Ethernet by RMII (P4 and ESP32 legacy devices).
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

#if CONFIG_mDNS_ENABLED
/* Lua API adapted from BamDNS.c; all responder work remains in ESP-IDF. */
#define MDNS_META "ESP32.mDNS"
typedef struct LMdns {
   struct LMdns* next;
   char name[64];
   BaBool active;
} LMdns;
static ThreadMutex mdnsMutex;
static LMdns* mdnsAliases;
static BaBool mdnsEvents;

typedef struct {
   mdns_ip_addr_t addr[2];
   unsigned count;
} LMdnsAddresses;

/* Run in TCP/IP context so selecting and reading the default netif cannot
   race its removal. An offline interface supplies an empty address list. */
static esp_err_t mdnsAddresses(void* arg)
{
   LMdnsAddresses* a = (LMdnsAddresses*)arg;
   esp_netif_t* netif = esp_netif_get_default_netif();
   esp_netif_ip_info_t ip4;
   memset(a,0,sizeof(*a));
   if(!netif || !esp_netif_is_netif_up(netif))
      return ESP_OK;
   if(esp_netif_get_ip_info(netif,&ip4) == ESP_OK && ip4.ip.addr)
   {
      a->addr[a->count].addr.type = ESP_IPADDR_TYPE_V4;
      a->addr[a->count++].addr.u_addr.ip4 = ip4.ip;
   }
#if CONFIG_LWIP_IPV6
   {
      esp_ip6_addr_t ip6;
      if(esp_netif_get_ip6_global(netif,&ip6) == ESP_OK ||
         esp_netif_get_ip6_linklocal(netif,&ip6) == ESP_OK)
      {
         a->addr[a->count].addr.type = ESP_IPADDR_TYPE_V6;
         a->addr[a->count++].addr.u_addr.ip6 = ip6;
      }
   }
#endif
   if(a->count == 2)
      a->addr[0].next = &a->addr[1];
   return ESP_OK;
}

/* IDF event task: native data only, never acquire the BAS/Lua mutex here. */
static void mdnsNetworkEvent(void* arg, esp_event_base_t base,
                             int32_t id, void* data)
{
   LMdnsAddresses a;
   (void)arg; (void)base; (void)data;
   switch(id)
   {
      case IP_EVENT_STA_GOT_IP: case IP_EVENT_STA_LOST_IP:
      case IP_EVENT_ETH_GOT_IP: case IP_EVENT_ETH_LOST_IP:
      case IP_EVENT_GOT_IP6:
      case IP_EVENT_NETIF_UP: case IP_EVENT_NETIF_DOWN:
         break;
      default: return;
   }
   ThreadMutex_set(&mdnsMutex);
   if(mdnsAliases && esp_netif_tcpip_exec(mdnsAddresses,&a) == ESP_OK)
      for(LMdns* o = mdnsAliases; o; o = o->next)
      {
         esp_err_t err = mdns_delegate_hostname_set_address(
            o->name,a.count ? a.addr : NULL);
         if(err != ESP_OK)
            ESP_LOGW("mDNS", "Cannot refresh alias: %s", esp_err_to_name(err));
      }
   ThreadMutex_release(&mdnsMutex);
}

/* Serialize ownership checks while allowing other Lua work during IDF calls. */
static void mdnsEnter(void)
{
   ThreadMutex_release(soDispMutex);
   ThreadMutex_set(&mdnsMutex);
}

static void mdnsLeave(void)
{
   ThreadMutex_release(&mdnsMutex);
   ThreadMutex_set(soDispMutex);
}

static int mdnsClose(lua_State* L, BaBool gc)
{
   LMdns* o = (LMdns*)luaL_checkudata(L,1,MDNS_META);
   esp_err_t err = ESP_OK;
   mdnsEnter();
   if(o->active)
   {
      err = mdns_delegate_hostname_remove(o->name);
      if(err == ESP_OK || err == ESP_ERR_INVALID_STATE || gc)
      {
         LMdns** link = &mdnsAliases;
         while(*link != o) link = &(*link)->next;
         *link = o->next;
         o->active = FALSE;
         if(gc && err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            ESP_LOGW("mDNS", "Cannot remove collected alias: %s", esp_err_to_name(err));
         err = ESP_OK;
      }
   }
   mdnsLeave();
   if(err != ESP_OK)
   {
      /* Explicit close retains ownership on a queue failure so it can retry. */
      ESP_LOGW("mDNS", "Cannot remove alias: %s", esp_err_to_name(err));
      return pushErr(L,esp_err_to_name(err));
   }
   return 0;
}

static int mdnsLuaClose(lua_State* L)
{
   return mdnsClose(L,FALSE);
}

static int mdnsLuaGC(lua_State* L)
{
   /* Even failed native cleanup must unlink userdata before Lua frees it. */
   return mdnsClose(L,TRUE);
}

static int mdnsLuaStatus(lua_State* L)
{
   LMdns* o = (LMdns*)luaL_checkudata(L,1,MDNS_META);
   char primary[MDNS_NAME_BUF_LEN];
   const char* error = 0;
   mdnsEnter();
   if(!o->active)
      error = "closed";
   else if(mdns_hostname_get(primary) != ESP_OK || !mdns_hostname_exists(o->name))
      error = "mDNS hostname is not registered";
   mdnsLeave();
   if(error)
      return pushErr(L,error);
   lua_pushboolean(L,TRUE);
   return 1;
}

static int mdnsLuaCreate(lua_State* L)
{
   size_t len;
   const char* name;
   const char* error = 0;
   LMdns* o;
   LMdnsAddresses addresses;
   char primary[MDNS_NAME_BUF_LEN];
   esp_err_t err;
   luaL_checktype(L,1,LUA_TSTRING);
   name = lua_tolstring(L,1,&len);
   luaL_argcheck(L,len == strlen(name),1,"name contains NUL");
   if(!len || len > 63 || name[0] == '-' || name[len-1] == '-')
      return pushErr(L,"invalid name or network configuration");
   for(size_t i = 0; i < len; ++i)
      if(!((name[i] >= 'a' && name[i] <= 'z') ||
           (name[i] >= 'A' && name[i] <= 'Z') ||
           (name[i] >= '0' && name[i] <= '9') || name[i] == '-'))
         return pushErr(L,"invalid name or network configuration");
   o = (LMdns*)lua_newuserdatauv(L,sizeof(LMdns),0);
   o->active = FALSE;
   for(size_t i = 0; i <= len; ++i)
      o->name[i] = name[i] >= 'A' && name[i] <= 'Z' ? name[i]+('a'-'A') : name[i];
   /* Attach cleanup before any native registration can succeed. */
   if(luaL_newmetatable(L, MDNS_META))
   {
      static const luaL_Reg lib[] = {
         {"status",mdnsLuaStatus},
         {"close", mdnsLuaClose},
         {"__close", mdnsLuaClose},
         {"__gc", mdnsLuaGC},
         {NULL, NULL}
      };
      lua_pushvalue(L, -1);
      lua_setfield(L, -2, "__index");
      luaL_setfuncs(L,lib,0);
   }
   lua_setmetatable(L, -2); /* Set meta for userdata */
   mdnsEnter();
   if(!mdnsEvents)
   {
      err = esp_event_handler_register(IP_EVENT,ESP_EVENT_ANY_ID,
                                       mdnsNetworkEvent,NULL);
      if(err != ESP_OK)
      {
         mdnsLeave();
         return pushErr(L,esp_err_to_name(err));
      }
      mdnsEvents = TRUE;
   }
   err = esp_netif_tcpip_exec(mdnsAddresses,&addresses);
   if(err != ESP_OK || !addresses.count)
   {
      mdnsLeave();
      return pushErr(L,"invalid name or network configuration");
   }
   err = mdns_hostname_get(primary);
   if(err == ESP_OK && mdns_hostname_exists(o->name))
      error = "mDNS hostname already in use";
   else if(err == ESP_OK)
   {
      err = mdns_delegate_hostname_add(o->name,addresses.addr);
      /* IDF can return OK even when its worker could not allocate the host. */
      if(err == ESP_OK)
      {
         o->active = mdns_hostname_exists(o->name);
         if(!o->active)
            err = ESP_ERR_NO_MEM;
         else
         {
            o->next = mdnsAliases;
            mdnsAliases = o;
         }
      }
   }
   mdnsLeave();
   if(error)
      return pushErr(L,error);
   if(err != ESP_OK)
      return pushErr(L,esp_err_to_name(err));
   return 1;
}

static void mdnsLuaOpen(lua_State* L)
{
   ThreadMutex_constructor(&mdnsMutex);
   lua_getglobal(L,"ba");
   lua_pushcfunction(L,mdnsLuaCreate);
   lua_setfield(L,-2,"createmdns");
   lua_pop(L,1);
}
#endif


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
      const char* name = "";
      esp_err_t err = ESP_OK;
      if(!lua_isnoneornil(L,2))
      {
         size_t len;
         luaL_checktype(L,2,LUA_TSTRING);
         name = lua_tolstring(L,2,&len);
         luaL_argcheck(L,len && len < 80 && len == strlen(name),2,
                       "expected a nonempty hostname shorter than 80 bytes");
      }
      mdnsEnter();
      if(!*name)
      {
         char primary[MDNS_NAME_BUF_LEN];
         err = mdns_hostname_get(primary);
         if(err == ESP_OK && *primary)
         {
            err = mdns_service_remove("_http","_tcp");
            if(err == ESP_ERR_NOT_FOUND) err = ESP_OK;
            if(err == ESP_OK) err = mdns_hostname_set("");
         }
      }
      if(err == ESP_OK) err = mDnsSet(name);
      mdnsLeave();
      if(err != ESP_OK) return pushErr(L,esp_err_to_name(err));
   }
#endif
   else
   {
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
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4

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
      LOTA* ota=(LOTA*)lNewUdataUV(L, sizeof(LOTA), BAOTA, otaObjLib,1);
      const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
      esp_err_t err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota->handle);
      if(LUA_OK != err)
         return pushErr(L,esp_err_to_name(err));
      return 1;
   }
   return throwInvArg(L, "action");
}

#endif /* CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 (OTA) */


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
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
   {"ota",lota},
#endif
   {NULL, NULL}
};

static const luaL_Reg basLib[] = {
   {"mac", lmac},
   {NULL, NULL}
};

#define RESERVED_PIN (void*)1  // Define a flag for reserved pins

void installESP32Libs(lua_State* L)
{
   memset(activeGPIO, 0, sizeof(void*)*GPIO_NUM_MAX);
   lua_newtable(L);
   cleanupOwners=luaL_ref(L,LUA_REGISTRYINDEX);

// --- RESERVED PINS FOR ESP32 ---
#ifdef CONFIG_IDF_TARGET_ESP32
    activeGPIO[6]  = RESERVED_PIN; // GPIO6  - Connected to SPI Flash SCK (CLK)
    activeGPIO[7]  = RESERVED_PIN; // GPIO7  - Connected to SPI Flash SDO (MOSI)
    activeGPIO[8]  = RESERVED_PIN; // GPIO8  - Connected to SPI Flash SDI (MISO)
    activeGPIO[9]  = RESERVED_PIN; // GPIO9  - Connected to SPI Flash WP
    activeGPIO[10] = RESERVED_PIN; // GPIO10 - Connected to SPI Flash HD
    activeGPIO[11] = RESERVED_PIN; // GPIO11 - Connected to SPI Flash CS
#if CONFIG_SPIRAM
    /* Include the PSRAM pins selected by IDF for this chip/module. */
    for(int pin=0; pin<GPIO_NUM_MAX; ++pin)
       if(esp_gpio_is_reserved(BIT64(pin))) activeGPIO[pin]=RESERVED_PIN;
#endif
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

   soDispMutex = HttpServer_getMutex(ltMgr.server);
   luaL_newlib(L, esp32Lib);
   lua_setglobal(L,"esp32");
   lua_getglobal(L, "ba");
   luaL_setfuncs(L,basLib,0);
   lua_pop(L,1);
#if CONFIG_mDNS_ENABLED
   mdnsLuaOpen(L);
#endif
}
