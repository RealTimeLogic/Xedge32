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
esp_err_t wconnect(const char* ssid, const char* pwd);
void wifiScan(int print, lua_State* L, /* main.c */
              void (*cb)(lua_State* L, const uint8_t* ssid, int rssi,
                        const char* authmode,const char*  pchiper,
                        const char* gcipher, int channel));

/*
  The LThreadMgr created and configured in xedge.c
*/
extern LThreadMgr ltMgr;
extern ThreadMutex* soDispMutex; /* BaESP32.c */
