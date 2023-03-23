/* 
   Optional Camera API
*/

#include "BaESP32.h"

#define BACAM "CAM"

typedef struct{
   // anything here
} LCAM;


static LCAM* LCAM_getUD(lua_State* L)
{
   return (LCAM*)luaL_checkudata(L,1,BACAM);
}

static int LCAM_lclose(lua_State* L)
{
   LCAM* o = LCAM_getUD(L);
   return 0;
}

static int LCAM_lread(lua_State* L)
{
   LCAM* o = LCAM_getUD(L);
   return 0;
}


static const luaL_Reg camObjLib[] = {
   {"read", LCAM_lread},
   {"close", LCAM_lclose},
   {"__close", LCAM_lclose},
   {"__gc", LCAM_lclose},
   {NULL, NULL}
};


int lcam(lua_State* L)
{
   lInitConfigTable(L, 1);
   lua_Integer d0 = balua_getIntField(L, 2, "d0", 5);
   lua_Integer d1 = balua_getIntField(L, 2, "d1", 6);
   lua_Integer d2 = balua_getIntField(L, 2, "d2", 7);
   lua_Integer d3 = balua_getIntField(L, 2, "d3", 8);
   lua_Integer d4 = balua_getIntField(L, 2, "d4", 9);
   lua_Integer d5 = balua_getIntField(L, 2, "d5", 10);
   lua_Integer d6 = balua_getIntField(L, 2, "d6", 11);
   lua_Integer d7 = balua_getIntField(L, 2, "d7", 12);
   lua_Integer xclk = balua_getIntField(L, 2, "xclk", 2);
   lua_Integer pclk = balua_getIntField(L, 2, "pclk", 4);
   lua_Integer vsync = balua_getIntField(L, 2, "vsync", 5);
   lua_Integer href = balua_getIntField(L, 2, "href", 14);
   lua_Integer sda = balua_getIntField(L, 2, "sda", 15);
   lua_Integer scl = balua_getIntField(L, 2, "scl", 13);
   lua_Integer reset = balua_getIntField(L, 2, "reset", 12);
   lua_Integer pwdn = balua_getIntField(L, 2, "pwdn", -1);
   lua_Integer freq = balua_getIntField(L, 2, "freq", 10000000);
   const char* format = balua_getStringField(L, 2, "format", "YUV422");


  LCAM* led = (LCAM*)lNewUdata(L,sizeof(LCAM),BACAM,camObjLib);
  // Init struct

  return 1;
}
