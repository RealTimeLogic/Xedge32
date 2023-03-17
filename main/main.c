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
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
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

#define ADD_THREAD_DBG

#ifdef ADD_THREAD_DBG
#include <task_snapshot.h>
#include <esp_debug_helpers.h>
static lua_State* Lg; /* Global state */
static void dbgThreads();
#endif

#define WIFI_SCAN_LIST_SIZE 10

struct {
   char* buf;
   size_t ix;
   size_t size;
} luaLineBuffer = {0};

static const char TAG[]={"X"};

static U8 gotIP=FALSE; /* if IP set */
static U8 gotSdCard=FALSE;
static const char mountPoint[]={"/sdcard"};

static nvs_handle_t nvsh;
static SemaphoreHandle_t semGotIp = 0;
extern int (*platformInitDiskIo)(DiskIo*);  /* xedge.c */

typedef struct
{
   ThreadJob super;
   const char* cmd;
   char* param1;
   char* param2;
   char* param3;
} ExecXedgeEventJob;

static void* checkAlloc(void* mem)
{
   if( ! mem )
      baFatalE(FE_MALLOC,0);
   return mem;
}


/* This function runs in the context of a thread in the LThreadMgr, and
   is triggered by the dispatchExecXedgeEvent.
 */
static void execXedgeEventCB2(ThreadJob* jb, int msgh, LThreadMgr* mgr)
{
   static const char XedgeEvent[]={"_XedgeEvent"};
   lua_State* L = jb->Lt;
   ExecXedgeEventJob* job = (ExecXedgeEventJob*)jb;
   lua_pushglobaltable(L);
   lua_getfield(L, -1, XedgeEvent);
   if(lua_isfunction(L, -1))
   {
      int params=1;
      lua_pushstring(L, job->cmd);
      if(job->param1) { lua_pushstring(L, job->param1); params++; }
      if(job->param2) { lua_pushstring(L, job->param2); params++; }
      if(job->param3) { lua_pushstring(L, job->param3); params++; }
      lua_pcall(L, params, 0, msgh);
   }
   else
   {
      ESP_LOGE(TAG, "Lua function '%s' missing\n", XedgeEvent);
   }
   if(job->param1) baFree(job->param1);
   if(job->param2) baFree(job->param2);
   if(job->param3) baFree(job->param3);
}


/* This CB runs in the context of the eventBrokerTask.
 */
static void execXedgeEventCB1(EventBrokerCallbackArg arg)
{
   ThreadMutex_set(soDispMutex);
   LThreadMgr_run(&ltMgr, (ThreadJob*)arg.ptr);
   ThreadMutex_release(soDispMutex);
}


/* Function execXedgeEvent is used by callbacks that run in the
 * context of the lwIP thread. We do not want to lock the soDispMutex
 * here since this could lead to a deadlock.
 */
static void execXedgeEvent(
   const char* cmd, char* param1, char* param2, char* param3)
{
   ExecXedgeEventJob* job=(ExecXedgeEventJob*)checkAlloc(
      ThreadJob_lcreate(sizeof(ExecXedgeEventJob),execXedgeEventCB2));
   job->cmd=cmd;
   job->param1=param1;
   job->param2=param2;
   job->param3=param3;
   EventBrokerQueueNode n = { .callback=execXedgeEventCB1, .arg.ptr=job };
   xQueueSend(eventBrokerQueue, &n, 0);
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
      ESP_LOGE(TAG, "Syntax error: %s\n", emsg);
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
#ifdef ADD_THREAD_DBG
   Lg=L;
#endif
   installESP32Libs(L);
   if(gotSdCard)
   {
      /* Will not GC before VM terminates */
      DiskIo* sdCard = (DiskIo*)lua_newuserdatauv(L, sizeof(DiskIo), 0);
      luaL_ref(L, LUA_REGISTRYINDEX);

      DiskIo_constructor(sdCard);
      DiskIo_setRootDir(sdCard,mountPoint);
      balua_iointf(L, "sd",  (IoIntf*)sdCard);
   }

   /* We return when we get an IP address, thus preventing the
    * Barracuda App Server from starting until the network is ready.
    */
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
  ESP_LOGE(TAG, "Fatal error in Barracuda %d %d %s %d\n",
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
      HttpTrace_printf(9,"Mounting internal FAT filesystem\n"); 
      esp_err_t err=esp_vfs_fat_spiflash_mount_rw_wl(bp,"storage",&mcfg,&hndl); 
      if (err != ESP_OK)
      {
         ESP_LOGE(TAG, "mounting FATFS failed (%s)", esp_err_to_name(err));
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
#if CONFIG_IDF_TARGET_ESP32S3
  EXT_RAM_BSS_ATTR static char poolBuf[7*1024*1024 + 5*1024];
#else
  EXT_RAM_BSS_ATTR static char poolBuf[3*1024*1024 + 5*1024];
#endif
  init_dlmalloc(poolBuf, poolBuf + sizeof(poolBuf));
#else   
#error must use dlmalloc
#endif
  platformInitDiskIo=initDiskIo;
  HttpTrace_setFLushCallback(writeHttpTrace);
  HttpServer_setErrHnd(myErrHandler); 
  barracuda(); /* Does not return */
}

static void eraseWifiCred()
{
   nvs_erase_key(nvsh,"ssid");
   nvs_erase_key(nvsh,"password");
}


static void onWifiEvent(void *arg, esp_event_base_t eventBase,
                        int32_t eventId, void *eventData)
{
   if(eventBase == WIFI_EVENT)
   {
      if(  WIFI_EVENT_STA_CONNECTED == eventId)
      {
         execXedgeEvent("wifi", baStrdup("up"), 0, 0);
      }
      else if (WIFI_EVENT_STA_DISCONNECTED == eventId) 
      {
         wifi_event_sta_disconnected_t* d =
            (wifi_event_sta_disconnected_t*)eventData;
         HttpTrace_printf(9,"WiFi disconnect ev. %d\n",d->reason);
         if(gotIP)
         {
            execXedgeEvent("wifi", baStrdup("down"), 0, 0);
         }
         char* param = (char*)baMalloc(20);
         if(param)
         {
            basnprintf(param,20,"%d",d->reason);
            execXedgeEvent("wifi", param, 0, 0);
         }
         gotIP=FALSE;
         esp_wifi_connect();
      }
      else
         ESP_LOGD(TAG, "Non managed WiFi event %ld\n",eventId);
   }
   else if(IP_EVENT == eventBase && IP_EVENT_STA_GOT_IP == eventId)
   {
      ip_event_got_ip_t* event = (ip_event_got_ip_t*)eventData;
      char* param1 = (char*)checkAlloc(baMalloc(16));
      char* param2 = (char*)checkAlloc(baMalloc(16));
      char* param3 = (char*)checkAlloc(baMalloc(16));
      gotIP=TRUE;
      basprintf(param1,IPSTR,IP2STR(&event->ip_info.ip));
      basprintf(param2,IPSTR,IP2STR(&event->ip_info.netmask));
      basprintf(param3,IPSTR,IP2STR(&event->ip_info.gw));
      execXedgeEvent("wip", param1, param2, param3);
      HttpTrace_printf(9,"Interface \"%s\" up\n",
                       esp_netif_get_desc(event->esp_netif));
      if(semGotIp)
         xSemaphoreGive(semGotIp);
   }
}


static void onSntpSync(struct timeval *tv)
{
   execXedgeEvent("sntp", 0, 0, 0);
}


esp_err_t wconnect(const char* ssid, const char* pwd)
{
   esp_wifi_disconnect();
   if(ssid)
   {
      // FIXME esp_wifi_set_country_code

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
         {
            ESP_ERROR_CHECK(nvs_set_str(nvsh,"ssid",ssid));
            ESP_ERROR_CHECK(nvs_set_str(nvsh,"password",pwd));
            return ESP_OK;
         }
         ESP_LOGD(TAG, "WiFi connect failed: %x\n", ret);
         return ESP_ERR_WIFI_SSID;
      }
      esp_wifi_stop();
      return ESP_ERR_INVALID_ARG;
   }
   eraseWifiCred();
   return ESP_OK;
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



static void openSdCard()
{
   esp_err_t ret;
   esp_vfs_fat_sdmmc_mount_config_t mountCfg = {
      .format_if_mount_failed = true,
      .max_files = 20, /* max open files */
      .allocation_unit_size = 16 * 1024
   };
   sdmmc_card_t *card;
   ESP_LOGD(TAG, "Initializing SD card");
   sdmmc_host_t host = SDMMC_HOST_DEFAULT();
   sdmmc_slot_config_t slotCfg = SDMMC_SLOT_CONFIG_DEFAULT();
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
   slotCfg.width = 4;
#else
   slotCfg.width = 1;
#endif
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
   slotCfg.clk = CONFIG_EXAMPLE_PIN_CLK;
   slotCfg.cmd = CONFIG_EXAMPLE_PIN_CMD;
   slotCfg.d0 = CONFIG_EXAMPLE_PIN_D0;
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
   slotCfg.d1 = CONFIG_EXAMPLE_PIN_D1;
   slotCfg.d2 = CONFIG_EXAMPLE_PIN_D2;
   slotCfg.d3 = CONFIG_EXAMPLE_PIN_D3;
#endif
#endif
   slotCfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
   ret = esp_vfs_fat_sdmmc_mount(mountPoint,&host,&slotCfg,&mountCfg,&card);
   if (ret != ESP_OK) 
   {
      if(ret == ESP_ERR_TIMEOUT)
         ESP_LOGI(TAG, "SD card not detected");
      else if(ret == ESP_FAIL)
         ESP_LOGE(TAG, "Mounting SD card failed");
      else
         ESP_LOGE(TAG, "Cannot initialize SD card (%s)",esp_err_to_name(ret));
      return;
   }
   ESP_LOGI(TAG, "SD card mounted");
   sdmmc_card_print_info(stdout, card);
   gotSdCard=true;
}



static void initComponents()
{
   static Thread t;
   openSdCard();

   esp_err_t err = nvs_flash_init();
   if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
   {
      ESP_LOGE(TAG, "NVS init failed! Erasing memory.");
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
   }
   ESP_ERROR_CHECK(err);
   ESP_ERROR_CHECK(esp_netif_init());
   ESP_ERROR_CHECK(esp_event_loop_create_default());
   ESP_ERROR_CHECK(nvs_open("WiFi", NVS_READWRITE, &nvsh));

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
                      WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, NULL) );
   ESP_ERROR_CHECK(esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &onWifiEvent, netif));
   esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
   esp_sntp_setservername(0, "pool.ntp.org");
   esp_sntp_init();
   sntp_set_time_sync_notification_cb(onSntpSync);

   char ssid[sizeof(((wifi_config_t*)0)->sta.ssid)];
   char password[sizeof(((wifi_config_t*)0)->sta.password)];
   size_t size=sizeof(ssid);
   if(ESP_OK == nvs_get_str(nvsh,"ssid",ssid, &size))
   {
      size=sizeof(password);
      if(ESP_OK == nvs_get_str(nvsh,"password",password,&size) && *ssid)
      {
         printf("Connecting to: %s\n",ssid);
         wconnect(ssid,password);
      }
   }

   /* Using BAS thread porting API */
   Thread_constructor(&t, mainServerTask, ThreadPrioNormal, BA_STACKSZ);
   Thread_start(&t);
}


void
app_main(void)
{
   initComponents();
   consoleInit();
   for(int i = 0; i < 50 ; i++)
   {
      if(gotIP) break;
      Thread_sleep(100);
   }

   HttpTrace_printf(5,
                    "\n\n __   __        _            \n \\ \\ / /       | |"
                    "           \n  \\ V / ___  __| | __ _  ___ \n   > < / _"
                    " \\/ _` |/ _` |/ _ \\\n  / . \\  __/ (_| | (_| |  __/\n"
                    " /_/ \\_\\___|\\__,_|\\__, |\\___|\n                   "
                    "__/ |     \n                  |___/      \n\n");
#if 0
   if(gotIP)
   {
      HttpTrace_printf(5,"%s",
         "Since you have a network connection, there's no need to start LuaShell32.\n"
         "Please use the Web-based REPL instead.");
      return;
   }
#endif
   HttpTrace_printf(5,"LuaShell32 ready.\n");
   for(;;)
   {
      char* line = linenoise("> ");
      if(line == NULL || !line[0])
         continue;

#ifdef ADD_THREAD_DBG
      if( ! strcmp(line, "threads") )
      { 
         dbgThreads();
         continue;
      }
#endif

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


/* The following code can be enabled as an emergency if detailed
 * thread and stack analysis is needed..
 */
#ifdef ADD_THREAD_DBG
static void dbgThreads()
{ /* The following code uses heuristics to find processes that are not
   * executing.
   */
   UBaseType_t tcbSize;
   static TaskSnapshot_t taskSnapshots[20];
   portDISABLE_INTERRUPTS();
   UBaseType_t snaps=uxTaskGetSnapshotAll(taskSnapshots, 20, &tcbSize);
   portENABLE_INTERRUPTS();
   long a,b,aIx,bIx;
   a=b=aIx=bIx=0;
   for(UBaseType_t i = 0 ; i < snaps ; i++)
   {
      XtExcFrame* f = (XtExcFrame*)taskSnapshots[i].pxTopOfStack;
      if( ! a ) a = f->exit;
      else if( ! b ) b = f->exit;
      if(a == f->exit) aIx++;
      else if(b == f->exit) bIx++;
   }
   if(bIx > aIx) a=b;
   /* Print all thread stacks. This can be decoded using xtensa-esp32-elf-addr2line
    */
   for(UBaseType_t i = 0 ; i < snaps ; i++)
   {
      XtExcFrame* f = (XtExcFrame*)taskSnapshots[i].pxTopOfStack;
      if(a == f->exit) /* If not running */
      {
         esp_backtrace_frame_t bf = {
            .pc = f->pc, .sp = f->a1, .next_pc = f->a0, .exc_frame = f}; 
         printf("\nTASK %d", i);
         esp_backtrace_print_from_frame(100, &bf, true);
      }
   }
   /* Print thread info.
    */
   TaskStatus_t *pxTaskStatusArray;
   volatile UBaseType_t uxArraySize, uxTaskCount;
   uxTaskCount = uxTaskGetNumberOfTasks();
   pxTaskStatusArray =
      (TaskStatus_t *) pvPortMalloc(uxTaskCount * sizeof(TaskStatus_t));
   uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxTaskCount, NULL);
   for(int i = 0; i < uxArraySize; i++)
   {
      printf("\nTask Name: %s\n", pxTaskStatusArray[i].pcTaskName);
      printf("Task Priority: %u\n", pxTaskStatusArray[i].uxCurrentPriority);
      printf("Task Handle: %p\n", pxTaskStatusArray[i].xHandle);
      printf("Task Stack High Water Mark: %lu\n",
             pxTaskStatusArray[i].usStackHighWaterMark);
   }
   vPortFree(pxTaskStatusArray);
   vTaskPrioritySet(xTaskGetCurrentTaskHandle(), configMAX_PRIORITIES - 1);
   /* Assume deadlock if not taken within 3 seconds */
   int taken = xSemaphoreTake(soDispMutex->mutex, 3000/portTICK_PERIOD_MS);
   printf("Sem taken: %d\n", taken);
   int top = lua_gettop(Lg);
   int status = luaL_loadstring(Lg,
      "local t={} "
      "for _,thread in ipairs(ba.thread.dbg()) do "
      "table.insert(t, (debug.traceback(thread))) end "
      "return table.concat(t)");
   if(LUA_OK == status)
   {
      status = lua_pcall(Lg, 0, 1, 0);
      if(LUA_OK == status)
      {
         printf("%s\n",lua_tostring(Lg, -1));
      }
   }
   if(taken)
      ThreadMutex_release(soDispMutex);
   printf("Status %d\n",status);
   lua_settop(Lg,top);
   esp_wifi_stop();
}
#endif
