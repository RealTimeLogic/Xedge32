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
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#if CONFIG_mDNS_ENABLED
#include <mdns.h>
#endif
#include <esp_efuse.h>
#include <esp_console.h>
#include <linenoise/linenoise.h>
#include <xedge.h>
#include "BaESP32.h"
#include "NetESP32.h"
#include "CfgESP32.h"

/*
 * To enable the thread debug please execute idf.py menuconfig and go
 * to xedge->debug thread.
 */
#if CONFIG_DEBUG_THREADS
    #include <freertos/task_snapshot.h>
    #include <esp_debug_helpers.h>
    static lua_State* Lg; /* Global state */
    static void dbgThreads();
#endif

struct {
   char* buf;
   size_t ix;
   size_t size;
   SemaphoreHandle_t sem; 
} luaLineBuffer = {0};

static const char TAG[]={"X"};

static bool gotSdCard = FALSE;
static const char mountPoint[] = {"/sdcard"};
static sdmmc_card_t *card = NULL;

/* mark in error messages for incomplete statements */
#define EOFMARK		"<eof>"
#define marklen		(sizeof(EOFMARK)/sizeof(char) - 1)

#if CONFIG_mDNS_ENABLED
static void startMdnsService()
{
   //initialize mDNS service
   char buf[80]={0};
   const char* ptr = ESP_OK == mDnsCfg(buf) ? buf : "Xedge32";
   ESP_ERROR_CHECK(mdns_init());
   mdns_hostname_set(ptr);
   HttpTrace_printf(9,"mDNS: %s\n",ptr); 
   mdns_instance_name_set("Xedge32");
   mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}
#else
#define startMdnsService()
#endif


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
   
   if(LUA_OK == status)
   {
      lua_pcall(L, 0, 0, msgh);
      luaLineBuffer.buf[luaLineBuffer.ix] = 0;
      linenoiseHistoryAdd(luaLineBuffer.buf);
      luaLineBuffer.ix = 0;
   }
   else 
   {
      const char* emsg = lcomplete(L, status);
      if(emsg)
      {
         ESP_LOGE(TAG, "Syntax error: %s\n", emsg);
         luaLineBuffer.ix = 0;
      }
   }

   xSemaphoreGive(luaLineBuffer.sem);
}


/* Do we have the partition where we save the xedge configuration file?
 */
static const esp_partition_t* openXCfgPartition()
{
   const esp_partition_t* xcfgPart=esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,"xcfg");
   if(xcfgPart)
      return xcfgPart;
   ESP_LOGE(TAG,"xcfg partition not found!\n");
   return 0;
}


/* Read or write the xedge.conf file. Called from Lua: Xedge .config
 */
static int xedgeCfgFile(lua_State* L)
{
   esp_err_t err;
   const esp_partition_t* xcfgPart=openXCfgPartition();
   if(xcfgPart)
   {
      uint8_t* data;
      size_t size;
      if(lua_isstring(L, 1)) /* Write */
      {
         uint8_t* data =(uint8_t*)lua_tolstring(L, 1, &size);
         if(ESP_OK == (err=esp_partition_erase_range(xcfgPart, 0, xcfgPart->size)) &&
            ESP_OK == (err=esp_partition_write(xcfgPart, 0, &size, sizeof(size))) &&
            ESP_OK == (err=esp_partition_write(xcfgPart, sizeof(size), data, size)))
         {
            lua_pushboolean(L, TRUE);
            return 1;
         }
      }
      else  /* Read */
      {
         if(ESP_OK == (err=esp_partition_read(xcfgPart, 0, &size, sizeof(size))))
         {
            if(0 == size || size > (xcfgPart->size - sizeof(size)))
               return 0; /* No data. Assume first time access. */
            luaL_Buffer b;
            data = (uint8_t*)luaL_buffinitsize(L,&b,size+1);
            if(ESP_OK == (err=esp_partition_read(xcfgPart, sizeof(size), data, size)))
            {
               luaL_addsize(&b, size);
               luaL_pushresult(&b);
               return 1;
            }
         }
      }
   }
   else
      err=ESP_ERR_NOT_FOUND;
   return pushEspRetVal(L, err, "xcfg partition", FALSE);
}


/* xedge.c calls this function. We use it to register the ESP32 Lua bindings.
 * This code also starts the ESP32 network. Function xedgeOpenAUX is
 * called when all Xedge objects used elsewhere in the ESP32-specific
 * code have been initialized, but the function is called before the
 * Xedge Lua app is started.
*/
int xedgeOpenAUX(XedgeOpenAUX* aux)
{
   lua_State* L = aux->L;
#if CONFIG_DEBUG_THREADS
   Lg=L;
#endif
   /*
     installESP32Libs must be called before netInit since it sets up a
     mutex used by all net callbacks.
    */
   installESP32Libs(L);
   netInit();
   startMdnsService();

   netConfig_t cfg;

   /* Delay execution until this point to avoid generating any events
    * that might use the soDispMutex before it is initialized. The
    * semaphore is initialized within the installESP32Libs function.
    */
   cfgGetNet(&cfg);

#if SOC_WIFI_SUPPORTED
   if(!strcmp("wifi", cfg.adapter))
   {
      netWifiConnect(cfg.ssid, cfg.password);
   }
   else if(netIsAdapterSpi(cfg.adapter) || netIsAdapterRmii(cfg.adapter))
   {
      netEthConnect();
   }  
#endif

   if(gotSdCard)
   {
      DiskIo* sdCard = (DiskIo*)lua_newuserdatauv(L, sizeof(DiskIo), 0);
      /* Make sure it will not GC until VM terminates */
      luaL_ref(L, LUA_REGISTRYINDEX);
      DiskIo_constructor(sdCard);
      DiskIo_setRootDir(sdCard,mountPoint);
      balua_iointf(L, "sd",  (IoIntf*)sdCard);
   }

   uint8_t keyBuf[32]; // 256 bits = 32 bytes
#if CONFIG_softTPM_EFUSE_ENABLED
   static const esp_efuse_desc_t efuseSecDescriptor =
      {EFUSE_BLK3, 0, 256}; // Use entire block
   static const esp_efuse_desc_t* efuseSecBlock[] = {
      &efuseSecDescriptor, // Point to the descriptor
      NULL
   };
   // Read the key from the custom efuse field
   esp_err_t err = esp_efuse_read_field_blob(efuseSecBlock, keyBuf, 256);
   if(ESP_OK != err)
   {
      ESP_LOGE(TAG,"eFuse TPM read err: %s\n", esp_err_to_name(err));
      return -1;
   }
   // Check if the key is all zeros (not set)
   bool isKeySet = false;
   for (int i = 0; i < 32; i++)
   {
      if (keyBuf[i] != 0)
      {
         isKeySet = true;
         break;
      }
   }
   if( ! isKeySet )
   {
      HttpTrace_printf(5, "Creating TPM eFuse key\n");
      // Generate a random 256-bit number
      esp_fill_random(keyBuf, 32);
      // Burn the key into the custom eFuse field
      err = esp_efuse_write_field_blob(efuseSecBlock, keyBuf, 256);
      if(ESP_OK != err)
      {
         ESP_LOGE(TAG,"eFuse TPM write err: %s\n", esp_err_to_name(err));
         return -1;
      }
   }
#else
   IoStat sb;
   int status=0;
   IoIntfPtr io = aux->dio;
   size_t size=0;
   static const char pmkey[]={"cert/softpmkey.bin"};
   if(io->statFp(io, "cert", &sb))
      io->mkDirFp(io, "cert", 0);
   ResIntf* fp = io->openResFp(io,pmkey,OpenRes_READ,&status,0);
   if(fp)
   {
      if(fp->readFp(fp, keyBuf, sizeof(keyBuf),&size))
      {
         ESP_LOGE(TAG, "Cannot save %s\n",pmkey);
         goto L_create;
      }
   }
   else
   {
     L_create:
      HttpTrace_printf(5,"Creating %s\n",pmkey);
      esp_fill_random(keyBuf, sizeof(keyBuf));
      fp = io->openResFp(io,pmkey, OpenRes_WRITE, &status, 0);
      if(!fp || fp->writeFp(fp, keyBuf, sizeof(keyBuf)))
         ESP_LOGE(TAG, "Cannot save %s\n",pmkey);
   }
   if(fp)
      fp->closeFp(fp);
#endif
   aux->addSecret(aux, TRUE, keyBuf, sizeof(keyBuf)); /* Send DDUI to TPM */
   aux->xedgeCfgFile = openXCfgPartition() ? xedgeCfgFile : 0;

   /* We return when we get an IP address, thus preventing the
    * Barracuda App Server from starting until the network is ready.
    */
   //netWaitIP(); // Function in NetESP32.c
   return 0;
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
  Thread_sleep(1000);
  abort();
}


/* Send data from the server's trace to ESP console */
static void writeHttpTrace(char* buf, int bufLen)
{
  buf[bufLen]=0; /* Zero terminate. Bufsize is at least bufLen+1. */
  printf("%s",buf);
}


/* Configures DISK IO for the C file: examples/xedge/src/xedge.c
 */
int xedgeInitDiskIo(DiskIo* io) /* Called by xedge.c */
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
static void mainServerTask(Thread *t)
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

   for(int i = 0 ; i < 16 ; i++)
   {
      sharkssl_entropy(esp_random());
   }

   HttpTrace_setFLushCallback(writeHttpTrace);
   HttpServer_setErrHnd(myErrHandler); 

   barracuda(); /* Does not return */
}

/**
 * @brief Low-level function to open the SD card.
 *
 * @param slotCfg Pointer to the SD card slot configuration structure.
 *                The `slotCfg` structure should be initialized with the following fields:
 *                - `clk`: GPIO number for the SD card clock pin.
 *                - `cmd`: GPIO number for the SD card command pin.
 *                - `d0`: GPIO number for the SD card data pin 0.
 *                - `d1`: GPIO number for the SD card data pin 1 (applicable only for 4-bit wide bus).
 *                - `d2`: GPIO number for the SD card data pin 2 (applicable only for 4-bit wide bus).
 *                - `d3`: GPIO number for the SD card data pin 3 (applicable only for 4-bit wide bus).
 *                - `width`: Bus width of the SD card. Set to 1 for 1-bit wide bus or 4 for 4-bit wide bus.
 *
 * @return esp_err_t Returns ESP_OK if the connection was successful.
 */
static esp_err_t openSdCard(sdmmc_slot_config_t* slotCfg)
{
   esp_err_t ret;
   esp_vfs_fat_sdmmc_mount_config_t mountCfg = {
      .format_if_mount_failed = true,
      .max_files = 20, /* max open files */
      .allocation_unit_size = 16 * 1024
   };
   
   sdmmc_host_t host = SDMMC_HOST_DEFAULT();
   slotCfg->flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
   ret = esp_vfs_fat_sdmmc_mount(mountPoint, &host, slotCfg, &mountCfg, &card);
   
   gotSdCard = (ret == ESP_OK) ? true : false;
   
   if(ret == ESP_OK)
   {
      ESP_LOGI(TAG, "SD card mounted");
      sdmmc_card_print_info(stdout, card);
   }   
   else if(ret == ESP_ERR_TIMEOUT)
   {
      ESP_LOGI(TAG, "SD card not detected");
   }
   else if(ret == ESP_FAIL)
   {
      ESP_LOGE(TAG, "Mounting SD card failed");
   }
   else
   {
      ESP_LOGE(TAG, "Cannot initialize SD card (%s)", esp_err_to_name(ret));
   }
   
   return ret;
}

/**
 * @brief Low-level function to close the SD card.
 *
 * @return esp_err_t Returns ESP_OK if the connection was successful.
 */
static esp_err_t closeSdcard(void)
{
   esp_err_t err = ESP_OK;

   if(card)
   {
      err = esp_vfs_fat_sdcard_unmount(mountPoint, card);
      
      if(err == ESP_OK)
      {
         ESP_LOGI(TAG, "SD card unmounted");
         card = NULL;
         gotSdCard = false;
      }
   }
   
   return err;
}

/**
 * @brief Check that the number of parameters for init the sdcard.
 *
 * @param params Count of parametes.
 *        width Bit width.
 *
 * @return TRUE when valid.
 */
static int checkSdcardParams(int params, int width)
{
   if((width != 8) && (width != 4) && (width != 1))
   {
      return FALSE;
   }
   
   //  Only allows arbitrary GPIOs with the ESP32-S3.
   if(params > 1)
   {
#ifdef SOC_SDMMC_USE_GPIO_MATRIX  
      if((width == 8) && (params != 11))
      {
         return FALSE;
      }
      
      if((width == 4) && (params != 7))
      {
         return FALSE;
      }
      
      if((width == 1) && (params != 4))
      {
         return FALSE;
      }
#else
   return FALSE;
#endif
   }
   
   return TRUE;
}

/**
 * @brief Lua binding function for SD card.
 *        for default pins use: esp32.sdcard(width)
 *        for customiced 1 bit wide use: esp32.sdcard(1, clk, cmd, d0)
 *        for customiced 4 bit wide use: esp32.sdcard(4, clk, cmd, d0, d1, d2, d3)
 *        for customiced 8 bit wide use: esp32.sdcard(8, clk, cmd, d0, d1, d2, d3, d4, d5, d6, d7)
 *
 * @note: customiced pins is only for esp32-s3.
 *
 * @param L Lua state pointer.
 * @return int Number of values returned to Lua.
 */
int lsdcard(lua_State* L)
{
   int params = lua_gettop(L);
   
   // The width parameter is mandatory.
   if(params >= 1)
   {
      sdmmc_slot_config_t cfg = SDMMC_SLOT_CONFIG_DEFAULT();
      
      cfg.width = luaL_checkinteger(L, 1);

      // Check that the number of parameters corresponds to the bit width and capabilities of the CPU.
      if(checkSdcardParams(params, cfg.width))
      {
#ifdef SOC_SDMMC_USE_GPIO_MATRIX
         if(params >= 4)
         {
            cfg.clk = luaL_checkinteger(L, 2);
            cfg.cmd = luaL_checkinteger(L, 3);
            cfg.d0 = luaL_checkinteger(L, 4);
         }   
   
         if(params >= 7)
         {  
            cfg.d1 = luaL_checkinteger(L, 5);
            cfg.d2 = luaL_checkinteger(L, 6);
            cfg.d3 = luaL_checkinteger(L, 7);
         }

         if(params == 11)
         {  
            cfg.d4 = luaL_checkinteger(L, 8);
            cfg.d5 = luaL_checkinteger(L, 9);
            cfg.d6 = luaL_checkinteger(L, 10);
            cfg.d7 = luaL_checkinteger(L, 11);
         }
#endif     
         esp_err_t err = closeSdcard();
         if(ESP_OK == err)
         {           
            err = openSdCard(&cfg);
            if(ESP_OK == err)
            {
               cfgSetSdCard(&cfg);
               // TODO: When I execute esp_restart with esp32-s3 and the console 
               // is CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG, the restart hangs, but 
               // when I put a delay of 1000 mS it works.
#if CONFIG_IDF_TARGET_ESP32S3
               Thread_sleep(1000);
#endif
               esp_restart();
            }
         }
         
         return pushEspRetVal(L, err, 0, TRUE);
      }
      else
      {
          luaL_argerror(L, 1, "Invalid paramenters");
      }
   }
   
   // If the number of arguments is less 0, erase the SD card config.
   closeSdcard();
   lua_pushboolean(L, (cfgEraseSdCard() == ESP_OK));
   return 1;
}

static void initComponents()
{
   static Thread t;
   
   cfgInit();

   sdmmc_slot_config_t cfg = SDMMC_SLOT_CONFIG_DEFAULT();
   
   if(cfgGetSdCard(&cfg) == ESP_OK)
   {
      openSdCard(&cfg);
   }
   
   /* Using BAS thread porting API */
   Thread_constructor(&t, mainServerTask, ThreadPrioNormal, BA_STACKSZ);
   Thread_start(&t);
}

void app_main(void)
{
   // Disable the esp log system.
   esp_log_level_set("*", ESP_LOG_ERROR);
   initComponents();
   manageConsole(true);
   
   /*
    * The luaLineBuffer is shared with the thread that executes executeOnLuaReplCB callback. 
    * To ensure data integrity and prevent simultaneous access, a binary semaphore is used.
    */
   luaLineBuffer.sem = xSemaphoreCreateBinary();
   
   HttpTrace_printf(5,
                    "\n\n __   __        _            \n \\ \\ / /       | |"
                    "           \n  \\ V / ___  __| | __ _  ___ \n   > < / _"
                    " \\/ _` |/ _` |/ _ \\\n  / . \\  __/ (_| | (_| |  __/\n"
                    " /_/ \\_\\___|\\__,_|\\__, |\\___|\n                   "
                    "__/ |     \n                  |___/      \n\n");
   HttpTrace_printf(5,"LuaShell32 ready.\n");
    
   /*
    * The app_main thread originally runs at low priority (ESP_TASK_MAIN_PRIO, or ESP_TASK_PRIO_MIN + 1).
    * The linenoise library uses the read() function that can block while waiting for USB/UART characters.
    * Occasionally, the console hangs, especially when closing the network adapter. The root cause seems to be
    * priority inversion, which can impact the RX ring buffer.
    * 
    * We've identified two potential fixes:
    * 
    * 1. Lower the FreeRTOS tick frequency to 100 Hz. This approach minimizes context switches and interrupts,
    *    but it might affect the overall responsiveness of the firmware.
    * 
    * 2. Increase the priority of the app_main thread. We chose this solution to avoid altering the tick frequency
    *    and maintain firmware responsiveness.
    * 
    * The chosen solution may increase the app_main thread's priority to mitigate priority inversion. This helps
    * prevent delays in the RX ring buffer processing caused by other tasks. Note that careful consideration
    * of task priorities is necessary to avoid other unexpected behavior or conflicts.
    */
   vTaskPrioritySet(NULL, uxTaskPriorityGet(NULL) + 4);  
   for(;;)
   {
      char* line = linenoise("\033[0m> ");
      if(line == NULL || !line[0])
         continue;
#if CONFIG_DEBUG_THREADS
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
         luaLineBuffer.buf = netCheckAlloc(
                             baRealloc(luaLineBuffer.buf, luaLineBuffer.size+1));
      }
      memcpy(luaLineBuffer.buf + luaLineBuffer.ix, line, len);
      luaLineBuffer.ix += len;
      linenoiseFree(line);
      ThreadJob* job=ThreadJob_lcreate(sizeof(ThreadJob), executeOnLuaReplCB);
      if( ! job ) baFatalE(FE_MALLOC,0);
      
      ThreadMutex_set(soDispMutex);
      LThreadMgr_run(&ltMgr, (ThreadJob*)job);
      ThreadMutex_release(soDispMutex);
      
      xSemaphoreTake(luaLineBuffer.sem, portMAX_DELAY);
   }
}

/* The following code can be enabled as an emergency if detailed
 * thread and stack analysis is needed..
 */
#if CONFIG_DEBUG_THREADS
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
   /* Print all thread stacks. This can be decoded using
    * xtensa-esp32-elf-addr2line
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
