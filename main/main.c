/* 
   Example ESP-IDF startup code calling LspAppMgr.c after ESP initialization.
*/

#include <sys/param.h>
#include <time.h>
#include <sys/time.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
//#include <mdns.h>
#include <barracuda.h>

extern void init_dlmalloc(char* heapstart, char* heapend); /* libbas.a */
extern void barracuda(void); /* LspAppMgr.c */
extern void smartConfig(void);
extern int (*platformInitDiskIo)(DiskIo*);  /* LspAppMgr.c */
extern void installESP32Libs(lua_State* L); /* BaESP32.c */

/* LspAppMgr.c calls this function. We use it to register the auto
 * generated bindings and the bindings in installESP32Libs.
*/
void luaopen_AUX(lua_State* L)
{
   installESP32Libs(L);
}


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
  ESP_LOGI("BAS","%s",buf);
}

/* Configures DISK IO for examples/lspappmgr/src/LspAppMgr.c
 */
static int initDiskIo(DiskIo* io)
{
   static const char bp[] = {"/spiflash"}; 
   const esp_vfs_fat_mount_config_t mcfg = {
      .max_files = 20,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
   };
   static BaBool mounted=FALSE;
   if( ! mounted )
   {
      wl_handle_t hndl = WL_INVALID_HANDLE;
      HttpTrace_printf(0,"Mounting FAT filesystem\n"); 
      esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(bp, "storage", &mcfg, &hndl); 
      if (err != ESP_OK)
      {
         HttpTrace_printf(0,"Failed to mount FATFS (%s)", esp_err_to_name(err));
         return -1;
      }
      mounted=TRUE;
   }
   /* Else: restarted internally by debugger */
   DiskIo_setRootDir(io,bp);
   return 0;
}

/* FreeRTOS task calling function barracuda() found in LspAppMgr.c
 */
static void
mainServerTask(Thread *t)
{
  (void)t;
#ifdef USE_DLMALLOC
  /* Allocate server pool using 3MB pSRAM  */
  EXT_RAM_BSS_ATTR static char poolBuf[3*1024*1024];
  init_dlmalloc(poolBuf, poolBuf + sizeof(poolBuf));
#else   
#error must use dlmalloc
#endif
  platformInitDiskIo=initDiskIo; 
  HttpTrace_setFLushCallback(writeHttpTrace);
  HttpServer_setErrHnd(myErrHandler); 
  barracuda(); /* Does not return */
}

#if 0
static void
startMdnsService()
{
   //initialize mDNS service
  ESP_ERROR_CHECK(mdns_init());
  mdns_hostname_set("BAS");
  mdns_instance_name_set("Barracuda App Server");
  mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}
#endif


void
app_main(void)
{
  static Thread t;
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
#ifdef SMARTCONFIG
  smartConfig();
#else
  /* This helper function configures Wi-Fi or Ethernet, as selected in
   * menuconfig.  See ESP-IDF documentation for additional info.
   */
  ESP_ERROR_CHECK(example_connect());
#endif


  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  /* Using BAS thread porting API */
  Thread_constructor(&t, mainServerTask, ThreadPrioNormal, BA_STACKSZ);
  Thread_start(&t);
  //startMdnsService();
}
