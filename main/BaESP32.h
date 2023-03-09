#include <barracuda.h>

void init_dlmalloc(char* heapstart, char* heapend); /* BAS.c */
void barracuda(void); /* xedge.c */
void installESP32Libs(lua_State* L); /* BaESP32.c */
void consoleInit(void); /* console.c */
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

