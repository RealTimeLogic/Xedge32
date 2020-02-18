/* 
   Example ESP-IDF startup code calling LspAppMgr.c after ESP initialization.
*/

#include <sys/param.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include <barracuda.h>

extern void init_dlmalloc(char* heapstart, char* heapend); /* libbas.a */
extern void barracuda(void); /* LspAppMgr.c */

/* Barracuda App Server fatal error callback */
static void
myErrHandler(BaFatalErrorCodes ecode1,
             unsigned int ecode2,
             const char* file,
             int line)
{
  ESP_LOGE("BAS", "Fatal error in Barracuda %d %d %s %d", ecode1, ecode2, file, line);
  abort();
}

/* Send data from the server's trace to ESP console */
static void
writeHttpTrace(char* buf, int bufLen)
{
  buf[bufLen]=0; /* Zero terminate. Bufsize is at least bufLen+1. */
  ets_printf("%s",buf);
}

/* FreeRTOS task calling function barracuda() found in LspAppMgr.c
 */
static void
mainServerTask(Thread *t)
{
  (void)t;
#ifdef USE_DLMALLOC
  /* Allocate server pool from 2MB pSRAM  */
  EXT_RAM_ATTR static char poolBuf[2*1024*1024];
  init_dlmalloc(poolBuf, poolBuf + sizeof(poolBuf));
#else   
#error must use dlmalloc
#endif
  HttpTrace_setFLushCallback(writeHttpTrace);
  HttpServer_setErrHnd(myErrHandler); 
  barracuda(); /* Does not return */
}



void
app_main(void)
{
  static Thread t;

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
   * See ESP-IDF documentation for additional info.
   */
  ESP_ERROR_CHECK(example_connect());

  /* Using BAS thread porting API */
  Thread_constructor(&t, mainServerTask, ThreadPrioNormal, BA_STACKSZ);
  Thread_start(&t);
}
