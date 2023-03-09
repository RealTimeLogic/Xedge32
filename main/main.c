/* 
   ESP-IDF startup code calling xedge.c after ESP initialization.

   Copyright (c) Real Time Logic

   This software may only be used by the terms and conditions
   stipulated in the corresponding license agreement under which the
   software has been supplied. All use of this code is subject to the
   terms and conditions of the included License Agreement.
*/

#include <sys/param.h>
#include <time.h>
#include <sys/time.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_console.h>
#include <linenoise/linenoise.h>
#include <protocol_examples_common.h>
#include <barracuda.h>
#include "BaESP32.h"

#define WIFI_SCAN_LIST_SIZE 10

struct {
   char* buf;
   size_t ix;
   size_t size;
} luaLineBuffer = {0};


static U8 gotIP=FALSE; /* if IP set */
static SemaphoreHandle_t semGotIp = 0;
extern int (*platformInitDiskIo)(DiskIo*);  /* xedge.c */

typedef struct
{
   ThreadJob super;
   const char* function;
   char* param1;
   char* param2;
   char* param3;
} ExecuteGlobalLFuncJob;

static void* checkAlloc(void* mem)
{
   if( ! mem )
      baFatalE(FE_MALLOC,0);
   return mem;
}


/* This function runs in the context of a thread in the LThreadMgr, and
   is triggered by the dispatchExecuteGlobalLFunc.
 */
static void executeGlobalLFuncCB(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   lua_State* L = jb->Lt;
   ExecuteGlobalLFuncJob* job = (ExecuteGlobalLFuncJob*)jb;
   lua_pushglobaltable(L);
   lua_getfield(L, -1, job->function);
   if(lua_isfunction(L, -1))
   {
      int params=0;
      if(job->param1) { lua_pushstring(L, job->param1); params++; }
      if(job->param2) { lua_pushstring(L, job->param2); params++; }
      if(job->param3) { lua_pushstring(L, job->param3); params++; }
      lua_pcall(L, params, 0, msgh);
   }
   else
   {
      HttpTrace_printf(0, "Warn: Lua function '%s' missing\n", job->function);
   }
   if(job->param1) baFree(job->param1);
   if(job->param2) baFree(job->param2);
   if(job->param3) baFree(job->param3);
}


static void executeGlobalLFunc(
   const char* function, char* param1, char* param2, char* param3)
{
   ExecuteGlobalLFuncJob* job=(ExecuteGlobalLFuncJob*)checkAlloc(
      ThreadJob_lcreate(sizeof(ExecuteGlobalLFuncJob),executeGlobalLFuncCB));
   job->function=function;
   job->param1=param1;
   job->param2=param2;
   job->param3=param3;
   ThreadMutex_set(soDispMutex);
   LThreadMgr_run(&ltMgr, (ThreadJob*)job);
   ThreadMutex_release(soDispMutex);
}


/* mark in error messages for incomplete statements */
#define EOFMARK		"<eof>"
#define marklen		(sizeof(EOFMARK)/sizeof(char) - 1)

/*
  Check whether 'status' signals a syntax error and the error message
  at the top of the stack ends with the above mark for incomplete
  statements.
*/
static const char* lcomplete(lua_State* L, int status)
{
   const char* msg=0;
   if (status == LUA_ERRSYNTAX)
   {
      size_t lmsg;
      msg = lua_tolstring(L, -1, &lmsg);
      if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0)
      {
         return 0;
      }
   }
   return msg;
}


/* This function runs in the context of a thread in the LThreadMgr, and
   is triggered by the Lua REPL in the main function.
 */
static void executeOnLuaReplCB(ThreadJob* job, int msgh, LThreadMgr* mgr)
{
   lua_State* L = job->Lt;
   int status = luaL_loadbuffer(L,luaLineBuffer.buf,luaLineBuffer.ix,"=stdin");
   if(LUA_OK != status)
   {
      const char* emsg = lcomplete(L, status);
      if( ! emsg )
         return; /* incomplete */
      HttpTrace_printf(0, "Syntax error: %s\n", emsg);
   }
   if (LUA_OK == status)
   {
      lua_pcall(L, 0, 0, msgh);
      luaLineBuffer.buf[luaLineBuffer.ix]=0;
      linenoiseHistoryAdd(luaLineBuffer.buf);
   }
   luaLineBuffer.ix = 0;
}


/* xedge.c calls this function. We use it to register the auto
 * generated bindings and the bindings in installESP32Libs.
*/
void luaopen_AUX(lua_State* L)
{
   installESP32Libs(L);
   if( ! gotIP )
   {
      semGotIp = xSemaphoreCreateBinary();
      baAssert(ThreadMutex_isOwner(soDispMutex));
      ThreadMutex_release(soDispMutex);
      xSemaphoreTake(semGotIp, portMAX_DELAY);
      ThreadMutex_set(soDispMutex);
      vSemaphoreDelete(semGotIp);
      semGotIp=0;
   }
}


/* Barracuda App Server fatal error callback */
static void
myErrHandler(BaFatalErrorCodes ecode1,
             unsigned int ecode2,
             const char* file,
             int line)
{
  ESP_LOGE("BAS", "Fatal error in Barracuda %d %d %s %d",
           ecode1, ecode2, file, line);
  abort();
}

/* Send data from the server's trace to ESP console */
static void
writeHttpTrace(char* buf, int bufLen)
{
  buf[bufLen]=0; /* Zero terminate. Bufsize is at least bufLen+1. */
  printf("%s",buf);
}

/* Configures DISK IO for examples/xedge/src/xedge.c
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
      HttpTrace_printf(9,"Mounting FAT filesystem\n"); 
      esp_err_t err=esp_vfs_fat_spiflash_mount_rw_wl(bp,"storage",&mcfg,&hndl); 
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

/* FreeRTOS task calling function barracuda() found in xedge.c
 */
static void
mainServerTask(Thread *t)
{
  (void)t;
#ifdef USE_DLMALLOC
  /* Allocate as much pSRAM as possible */
  EXT_RAM_BSS_ATTR static char poolBuf[3*1024*1024 + 5*1024];
  init_dlmalloc(poolBuf, poolBuf + sizeof(poolBuf));
#else   
#error must use dlmalloc
#endif
  platformInitDiskIo=initDiskIo;
  HttpTrace_setFLushCallback(writeHttpTrace);
  HttpServer_setErrHnd(myErrHandler); 
  barracuda(); /* Does not return */
}


static void onWifiDisconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
   if(gotIP)
   {
      gotIP=FALSE;
      executeGlobalLFunc("_WiFi", strdup("down"), 0, 0);
   }
   esp_wifi_connect();
}


static void onWifiConnect(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
   executeGlobalLFunc("_WiFi", baStrdup("up"), 0, 0);
}


static void onGotIP(void *arg, esp_event_base_t eventBase,
                      int32_t eventId, void *eventData)
{
   ip_event_got_ip_t* event = (ip_event_got_ip_t*)eventData;
   char* param1 = (char*)checkAlloc(baMalloc(16));
   char* param2 = (char*)checkAlloc(baMalloc(16));
   char* param3 = (char*)checkAlloc(baMalloc(16));
   gotIP=TRUE;
   basprintf(param1,IPSTR,IP2STR(&event->ip_info.ip));
   basprintf(param2,IPSTR,IP2STR(&event->ip_info.netmask));
   basprintf(param3,IPSTR,IP2STR(&event->ip_info.gw));
   executeGlobalLFunc("_gotIP", param1, param2, param3);
   HttpTrace_printf(9,"Interface \"%s\" up\n",
                    esp_netif_get_desc(event->esp_netif));
   if(semGotIp)
      xSemaphoreGive(semGotIp);
}


static void onSntpSync(struct timeval *tv)
{
   executeGlobalLFunc("_gotTime", 0, 0, 0);
}

static void initComponents()
{
   static Thread t;
   esp_err_t err = nvs_flash_init();
   if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
   {
      HttpTrace_printf(0,"NVS init failed! Erasing memory.");
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
   }
   ESP_ERROR_CHECK(err);
   ESP_ERROR_CHECK(esp_netif_init());
   ESP_ERROR_CHECK(esp_event_loop_create_default());

   /* Fixme rewrite to not use EXAMPLE_xxxx */

   esp_netif_t *netif;
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));
   esp_netif_inherent_config_t esp_netif_config =
      ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
   esp_netif_config.if_desc = EXAMPLE_NETIF_DESC_STA;
   esp_netif_config.route_prio = 128;
   netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
   esp_wifi_set_default_wifi_sta_handlers();
   ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
   ESP_ERROR_CHECK(esp_wifi_start());
   ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &onWifiDisconnect, NULL));
   ESP_ERROR_CHECK(esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &onGotIP, NULL));
   ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,&onWifiConnect, netif));
   esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
   esp_sntp_setservername(0, "pool.ntp.org");
   esp_sntp_init();
   sntp_set_time_sync_notification_cb(onSntpSync);

   /* Using BAS thread porting API */
   Thread_constructor(&t, mainServerTask, ThreadPrioNormal, BA_STACKSZ);
   Thread_start(&t);
}


esp_err_t wconnect(const char* ssid, const char* pwd)
{
   if(ssid)
   {
      wifi_config_t cfg = {
         .sta = {
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = CONFIG_EXAMPLE_WIFI_SCAN_RSSI_THRESHOLD,
            .threshold.authmode = WIFI_AUTH_OPEN
         }
      };
      if(strlen(ssid) < sizeof(cfg.sta.ssid) &&
         strlen(pwd) < sizeof(cfg.sta.password))
      {
         strcpy((char*)cfg.sta.ssid, ssid);
         strcpy((char*)cfg.sta.password, pwd);

         ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
         esp_err_t ret = esp_wifi_connect();
         if(ESP_ERR_WIFI_NOT_STARTED == ret)
         {
            esp_wifi_start();
            ret = esp_wifi_connect();
         }
         if(ESP_OK == ret)
            return ESP_OK;
         HttpTrace_printf(0,"WiFi connect failed! ret:%x\n", ret);
         return ESP_ERR_WIFI_SSID;
      }
      return ESP_ERR_INVALID_ARG;
   }
   return esp_wifi_stop();
}

static const char* wifiAuthMode(int authmode, int print)
{
   const char* msg;
   const char pre[]={"Authmode \t"};
   switch (authmode)
   {
      case WIFI_AUTH_OPEN: msg="OPEN"; break;
      case WIFI_AUTH_OWE: msg="OWE"; break;
      case WIFI_AUTH_WEP: msg="WEP"; break;
      case WIFI_AUTH_WPA_PSK: msg="WPA_PSK"; break;
      case WIFI_AUTH_WPA2_PSK: msg="WPA2_PSK"; break;
      case WIFI_AUTH_WPA_WPA2_PSK: msg="WPA_WPA2_PSK"; break;
      case WIFI_AUTH_WPA2_ENTERPRISE: msg="WPA2_ENTERPRISE"; break;
      case WIFI_AUTH_WPA3_PSK: msg="WPA3_PSK"; break;
      case WIFI_AUTH_WPA2_WPA3_PSK: msg="WPA2_WPA3_PSK"; break;
      default: msg="UNKNOWN"; break;
   }
   if(print)
      HttpTrace_printf(0,"%s%s\n",pre,msg);
   return msg;
}

static const char*
wifiCipherType(int pcipher, int gcipher, int print, const char** pciphers)
{
   const char* msg;
   const char* pre="Pairwise Cipher\t";
   switch(pcipher)
   {
      case WIFI_CIPHER_TYPE_NONE: msg="NONE"; break;
      case WIFI_CIPHER_TYPE_WEP40: msg="WEP40"; break;
      case WIFI_CIPHER_TYPE_WEP104: msg="WEP104"; break;
      case WIFI_CIPHER_TYPE_TKIP: msg="TKIP"; break;
      case WIFI_CIPHER_TYPE_CCMP: msg="CCMP"; break;
      case WIFI_CIPHER_TYPE_TKIP_CCMP: msg="TKIP_CCMP"; break;
      default: msg="UNKNOWN"; break;
   }
   if(print)
      HttpTrace_printf(0,"%s%s\n",pre,msg);
   *pciphers=msg;
   pre="Group Cipher \t";
   switch(gcipher)
   {
      case WIFI_CIPHER_TYPE_NONE: msg="NONE"; break;
      case WIFI_CIPHER_TYPE_WEP40: msg="WEP40"; break;
      case WIFI_CIPHER_TYPE_WEP104: msg="WEP104"; break;
      case WIFI_CIPHER_TYPE_TKIP: msg="TKIP"; break;
      case WIFI_CIPHER_TYPE_CCMP: msg="CCMP"; break;
      case WIFI_CIPHER_TYPE_TKIP_CCMP: msg="TKIP_CCMP"; break;
      default: msg="UNKNOWN"; break;
   }
   if(print)
      HttpTrace_printf(0,"%s%s\n",pre,msg);
   return msg;
}


void wifiScan(int print, lua_State* L,
              void (*cb)(lua_State* L, const uint8_t* ssid, int rssi,
                        const char* authmode,const char*  pchiper,
                        const char* gcipher, int channel))
{
   uint16_t number = WIFI_SCAN_LIST_SIZE;
   wifi_ap_record_t apInfo[WIFI_SCAN_LIST_SIZE];
   uint16_t apCount = 0;
   memset(apInfo, 0, sizeof(apInfo));

   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
   ESP_ERROR_CHECK(esp_wifi_start());
   esp_wifi_scan_start(NULL, true);
   ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, apInfo));
   ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&apCount));
   if(print)
      HttpTrace_printf(0,"Total APs scanned = %u\n", apCount);
   for(int i = 0; (i < WIFI_SCAN_LIST_SIZE) && (i < apCount); i++)
   {
      if(print)
      {
         HttpTrace_printf(0,"SSID \t\t%s\n", apInfo[i].ssid);
         HttpTrace_printf(0,"RSSI \t\t%d\n", apInfo[i].rssi);
      }
      const char* authmode=wifiAuthMode(apInfo[i].authmode,print);
      const char* pcipher=0;
      const char* gcipher=0;
      if(apInfo[i].authmode != WIFI_AUTH_WEP) {
         gcipher=wifiCipherType(
            apInfo[i].pairwise_cipher,apInfo[i].group_cipher,print,&pcipher);
      }
      if(print)
         HttpTrace_printf(0,"Channel \t\t%d\n\n", apInfo[i].primary);
      cb(L,apInfo[i].ssid,apInfo[i].rssi,authmode,pcipher,gcipher,
         apInfo[i].primary);
   }
}



void
app_main(void)
{
   initComponents();
   consoleInit();
   HttpTrace_printf(5,"\n\nLuaShell32 ready.\n");
   for(;;)
   {
      char* line = linenoise("> ");
      if(line == NULL || !line[0])
         continue;
      size_t left = luaLineBuffer.size - luaLineBuffer.ix;
      size_t len = strlen(line);
      if (left < len)
      {
         luaLineBuffer.size += len + 100;
         luaLineBuffer.buf = checkAlloc(
            baRealloc(luaLineBuffer.buf, luaLineBuffer.size+1));
      }
      memcpy(luaLineBuffer.buf + luaLineBuffer.ix, line, len);
      luaLineBuffer.ix += len;
      linenoiseFree(line);
      ThreadJob* job=ThreadJob_lcreate(sizeof(ThreadJob),executeOnLuaReplCB);
      if( ! job ) baFatalE(FE_MALLOC,0);
      ThreadMutex_set(soDispMutex);
      LThreadMgr_run(&ltMgr, (ThreadJob*)job);
      ThreadMutex_release(soDispMutex);
   }
}
