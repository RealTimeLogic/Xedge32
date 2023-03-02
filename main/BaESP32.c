
#include <driver/i2c.h>
#include <barracuda.h>

/*
  The LThreadMgr configured in LspAppMgr.c
  https://realtimelogic.com/ba/doc/en/C/reference/html/structLThreadMgr.html
*/
extern LThreadMgr ltMgr;

/*
  The Socket Dispatcher (SoDisp) mutex protecting everything; set in
  installESP32Libs() below.
  https://realtimelogic.com/ba/doc/en/C/reference/html/structThreadMutex.html
*/
static ThreadMutex* soDispMutex;

/*
  A mutex used for protecting small regions
*/
static ThreadMutex rMutex;

/*********************************************************************
 *********************************************************************
                           Misc functions
 *********************************************************************
 *********************************************************************/

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
   above prevents the garbage collector from collecting the callback
   as long as the userdata exists. The callback is also registered in
   the server's weak table, making it easy to push the function on the
   stack using the call: balua_wkPush().
   https://realtimelogic.com/ba/doc/en/C/reference/html/group__WeakT.html

   This function does not push any values on the stack.
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


typedef void (*EventBrokerCallback)(gpio_num_t pin);

typedef struct {
   EventBrokerCallback callback;
   gpio_num_t pin;
} EventBrokerQueueNode;


static QueueHandle_t eventBrokerQueue;

/*
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


/* This function runs in the context of a thread in the LThreadMgr.
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


/* This function runs in the context of the eventBrokerTask.
 */
static void gpioEventBroker(gpio_num_t pin)
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
   {
      GpioThreadJob* job=(GpioThreadJob*)ThreadJob_lcreate(
         sizeof(GpioThreadJob),executeLuaGpioCB);
      if( ! job )
         baFatalE(FE_MALLOC,0);
      job->pin=pin;
      ThreadMutex_set(soDispMutex);
      LThreadMgr_run(&ltMgr, (ThreadJob*)job);
      ThreadMutex_release(soDispMutex);
   }
}


/* Queue a GPIO event and send to the eventBrokerTask, which then
 * calls function gpioEventBroker.
 */
static void IRAM_ATTR gpioInterruptHandler(void *arg)
{
   EventBrokerQueueNode n = {
      .callback=gpioEventBroker,
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



static int GPIO_read(lua_State* L)
{
   lua_pushboolean(L,gpio_get_level(GPIO_checkUD(L)->pin));
   return 1;
}


static int GPIO_write(lua_State* L)
{
   gpio_set_level(GPIO_checkUD(L)->pin, balua_checkboolean(L, 2));
   return 0;
}


static int GPIO_lclose(lua_State* L)
{
   GPIO_close(L,GPIO_getUD(L));
   return 0;
}


static const luaL_Reg gpioObjLib[] = {
   {"read", GPIO_read},
   {"write", GPIO_write},
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
   if( ! lua_istable(L, 3) )
   {
      lua_settop(L,2);
      lua_createtable(L, 0, 0); /* empty config table */
   }
   else
      lua_settop(L,3);
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
   gpio->pin=pin;
   gpio_reset_pin(pin);
   if(ESP_OK != gpio_config(&cfg))
      luaL_error(L,"INVALIDARG");
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

static LI2CMaster* I2CMaster_getUD(lua_State* L)
{
   return (LI2CMaster*)luaL_checkudata(L,1,BAI2CMASTER);
}

static void throwInvArg(lua_State* L)
{
   luaL_error(L, "Invalid arg. comb.");
}

static void throwInvDirection(lua_State* L)
{
   luaL_error(L, "Invalid direction");
}



static int pushEspRetVal(lua_State* L, esp_err_t err)
{
   const char* emsg=0;
   switch(err)
   {
      case ESP_OK: lua_pushboolean(L,TRUE); return 1;
      case ESP_ERR_INVALID_ARG: emsg="invalidarg"; break;
      case ESP_ERR_NO_MEM: emsg="nomem"; break;
      case ESP_ERR_INVALID_STATE: emsg="invalidstate"; break;
      case ESP_ERR_TIMEOUT: emsg="timeout"; break;
      default:
         emsg="fail";
   }
   lua_pushnil(L);
   lua_pushstring(L,emsg);
   return 2;
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
      L,i2cm->cmd ? i2c_master_start(i2cm->cmd) : ESP_ERR_NO_MEM);
}

/* i2cm:address(addr, direction, [,ack]) */
static int I2CMaster_address(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   i2cm->direction = (uint8_t)luaL_checkinteger(L, 3);
   if(i2cm->direction != I2C_MASTER_READ && i2cm->direction != I2C_MASTER_WRITE)
      throwInvArg(L);
   return pushEspRetVal(L,i2c_master_write_byte(
      i2cm->cmd,
      ((uint8_t)luaL_checkinteger(L, 2) << 1) | i2cm->direction,
      balua_optboolean(L, 4, TRUE)));
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
                        i2c_master_write(i2cm->cmd,data,dlen,ack));
}

/* i2cm:read(len [, i2cm.ACK | i2cm.NACK | i2cm.LAST_NACK]) */
static int I2CMaster_read(lua_State* L)
{
   esp_err_t status;
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   if(i2cm->direction != I2C_MASTER_READ)
      throwInvDirection(L);
   i2cm->recblen = (size_t )luaL_checkinteger(L, 2);
   lua_Integer ack = luaL_optinteger(L, 3, -1);
   if(i2cm->recbuf || i2cm->recblen == 0)
      throwInvArg(L);
   i2cm->recbuf = (uint8_t*)baLMalloc(L, i2cm->recblen+1);
   if( ! i2cm->recbuf )
      return pushEspRetVal(L,ESP_ERR_NO_MEM);
   if(ack == -1 && i2cm->recblen > 1)
   {
      i2c_master_read(i2cm->cmd,i2cm->recbuf,i2cm->recblen-1,I2C_MASTER_ACK);
      status=i2c_master_read_byte(
         i2cm->cmd,i2cm->recbuf+i2cm->recblen-1,I2C_MASTER_NACK);
   }
   else
   {
      if(ack == -1)
         ack=I2C_MASTER_NACK;
      status = i2cm->recblen == 1 ?
         i2c_master_read_byte(i2cm->cmd,i2cm->recbuf,ack) :
         i2c_master_read(i2cm->cmd,i2cm->recbuf,i2cm->recblen,ack);
   }
   if(status != ESP_OK)
   {
      baFree(i2cm->recbuf);
      i2cm->recbuf=0;
   }
   return pushEspRetVal(L,status);
}


/* i2cm:commit([timeout]) */
static int I2CMaster_commit(lua_State* L)
{
   LI2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
   ThreadMutex* m = balua_getmutex(L);
   i2c_master_stop(i2cm->cmd);
   balua_releasemutex(m);
   int status=i2c_master_cmd_begin(i2cm->port,i2cm->cmd,I2CWT(L,2));
   balua_setmutex(m);
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
   return pushEspRetVal(L,status);
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






/*********************************************************************
 *********************************************************************
                             Install ESP32
 *********************************************************************
 *********************************************************************/



static const luaL_Reg esp32Lib[] = {
   {"i2cmaster", li2cMaster},
   {"gpio", lgpio},
   {NULL, NULL}
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
}
