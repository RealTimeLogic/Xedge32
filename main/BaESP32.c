
#include <driver/i2c.h>
#include <balua.h>

#define BAI2CMASTER "I2CMASTER"

typedef struct
{
   i2c_cmd_handle_t cmd;
   uint8_t* recbuf;
   size_t recblen;
   i2c_port_t port;
   uint8_t direction;
} I2CMaster;

/* Time to wait for I2c. Default is 500ms */
#define I2CWT(L,ix) ((TickType_t)luaL_optinteger(L,ix,500)/portTICK_PERIOD_MS)

static I2CMaster* I2CMaster_getUD(lua_State* L)
{
   return (I2CMaster*)luaL_checkudata(L,1,BAI2CMASTER);
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

static I2CMaster* I2CMaster_checkUD(lua_State* L, int checkCmd)
{
   I2CMaster* i2cm = I2CMaster_getUD(L);
   if(i2cm->port < 0)
      luaL_error(L, "Closed");
   if(checkCmd && ! i2cm->cmd)
      luaL_error(L, "Not started");
   return i2cm;
}

/* i2cm:start() */
static int I2CMaster_start(lua_State* L)
{
   I2CMaster* i2cm = I2CMaster_checkUD(L, FALSE);
   if( ! i2cm->cmd )
      i2cm->cmd = i2c_cmd_link_create();
   /* else recursive: https://www.i2c-bus.org/repeated-start-condition/ */
   return pushEspRetVal(L,i2cm->cmd ? i2c_master_start(i2cm->cmd) : ESP_ERR_NO_MEM);
}

/* i2cm:address(addr, direction, [,ack]) */
static int I2CMaster_address(lua_State* L)
{
   I2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
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
   I2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
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
   I2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
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
      status=i2c_master_read_byte(i2cm->cmd,i2cm->recbuf+i2cm->recblen-1,I2C_MASTER_NACK);
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
   I2CMaster* i2cm = I2CMaster_checkUD(L, TRUE);
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
static int I2CMaster_delete(lua_State* L)
{
   I2CMaster* i2cm = I2CMaster_getUD(L);
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
   {"close", I2CMaster_delete},
   {"__close", I2CMaster_delete},
   {"__gc", I2CMaster_delete},
   {NULL, NULL}
};

/* ic2.master(port, pinSDA, pinSCL, speed) */
static int i2cMaster(lua_State* L)
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
   I2CMaster* i2cm = (I2CMaster*)lua_newuserdata(L, sizeof(I2CMaster));
   memset(i2cm,0,sizeof(I2CMaster));
   i2cm->port = port;
   /* Create meta table if this is the first time this function is called */
   if(luaL_newmetatable(L, BAI2CMASTER))
   {
      lua_pushvalue(L, -1);
      lua_setfield(L, -2, "__index");
      luaL_setfuncs(L,i2cMasterLib,0);
      struct { int t; const char* n; } types[] = {
         {I2C_MASTER_ACK, "ACK"},
         {I2C_MASTER_NACK, "NACK"},
         {I2C_MASTER_LAST_NACK, "LAST_NACK"},
         {I2C_MASTER_READ,"READ"},
         {I2C_MASTER_WRITE,"WRITE"}
      };
      /* Add the ACK types to the table */
      for(int i = 0; i < sizeof(types) / sizeof(types[0]); i++)
      {
         lua_pushinteger(L, types[i].t);
         lua_setfield(L, -2, types[i].n);
      }
   }
   lua_setmetatable(L, -2); /* Set meta for userdata */
   return 1;
}

static const luaL_Reg i2cLib[] = {
   {"master", i2cMaster},
   {NULL, NULL}
};

void installESP32Libs(lua_State* L)
{
   lua_createtable(L, 0, 1);
   luaL_setfuncs(L, i2cLib,0);
   lua_setglobal(L,"i2c");
}
