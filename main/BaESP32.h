#pragma once

#include <driver/gpio.h>
#include <barracuda.h>

typedef union {
   gpio_num_t pin;
   void* ptr;
} EventBrokerCallbackArg;

typedef void (*EventBrokerCallback)(EventBrokerCallbackArg arg);

typedef struct {
   EventBrokerCallback callback;
   EventBrokerCallbackArg arg;
} EventBrokerQueueNode;

extern QueueHandle_t eventBrokerQueue;

void init_dlmalloc(char* heapstart, char* heapend); /* BAS.c */
void barracuda(void); /* xedge.c */
void installESP32Libs(lua_State* L); /* BaESP32.c */
void manageConsole(bool start); /* console.c */
void wifiScan(int print, lua_State* L, /* main.c */
              void (*cb)(lua_State* L, const uint8_t* ssid, int rssi,
                        const char* authmode,const char*  pchiper,
                        const char* gcipher, int channel));
void eventBrokerTask(void *params);
#ifdef CONFIG_CAM_ENABLED
int lcam(lua_State* L);  /* BaCam.c */
#endif
void lInitConfigTable(lua_State* L, int ix);
void* lNewUdata(lua_State *L,size_t size,const char *tname,const luaL_Reg *l);
int lsdcard(lua_State* L);
int pushEspRetVal(lua_State* L, esp_err_t err, const char* msg);

const char* wifiCipherType(
   int pcipher, int gcipher, int print, const char** pciphers);
const char* wifiAuthMode(int authmode, int print);



/*
  The LThreadMgr created and configured in xedge.c
*/
extern LThreadMgr ltMgr;
extern ThreadMutex* soDispMutex; /* BaESP32.c */
