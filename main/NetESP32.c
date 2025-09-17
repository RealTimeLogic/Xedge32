/** 
 * Low level network module for xedge32.c
 *
 * Copyright (c) Real Time Logic
 *
 * This software may only be used by the terms and conditions
 * stipulated in the corresponding license agreement under which the
 * software has been supplied. All use of this code is subject to the
 * terms and conditions of the included License Agreement.
 */
#include <sys/param.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

#include <esp_mac.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_netif.h>

#include "esp_netif.h"
#include "BaESP32.h"
#include "CfgESP32.h"
#include "NetESP32.h"

#ifdef CONFIG_ETH_ENABLED
#include <esp_eth_mac.h>
#include <esp_eth.h>
#endif

#if SOC_WIFI_SUPPORTED
#include <esp_wifi.h>
#define WIFI_SCAN_LIST_SIZE 10
#endif

static const char TAG[]={"X"};

//static SemaphoreHandle_t semGotIp = 0;
static uint8_t gotIP = FALSE; /* if IP set */

#if SOC_WIFI_SUPPORTED
#ifdef CONFIG_ETH_ENABLED
static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;
static spi_host_device_t spiHostId = 0;
#endif

static esp_netif_t *wifi_netif = NULL;
static esp_netif_t *eth_netif = NULL;
static esp_netif_t *ap_netif = NULL;

// At power-up, the Access Point (AP) mode is disabled.
static ap_mode_t apMode = AP_MODE_DISABLED;
static uint8_t netWifiRetry = 0;
 
static esp_err_t netWifiStop(bool unregHandler);

#endif // SOC_WIFI_SUPPORTED


/* This buffer is used as the memory for the eventBrokerQueue, which
 * serves various messaging purposes, including sending messages from
 * interrupts. On an ESP32, this buffer requires the IRAM_ATTR
 * attribute. However, for the ESP32-S3, this attribute appears
 * unnecessary and must be disabled as enabling it results in a
 * rst:0x8 (TG1WDT_SYS_RST) error when Wi-Fi connects and
 * execXedgeEvent() is invoked from the onWifiEvent() function.
 */
#if CONFIG_IDF_TARGET_ESP32
   #define EVENT_RAM_TYPE static IRAM_ATTR 
#else
   #define EVENT_RAM_TYPE static
#endif

EVENT_RAM_TYPE uint8_t eventBrokerQueueBuf[20*sizeof(EventBrokerQueueNode)];

typedef struct
{
   ThreadJob super;
   const char* cmd;
   char* param1;
   char* param2;
   char* param3;
} ExecXedgeEventJob;

void* netCheckAlloc(void* mem)
{
   if( ! mem )
      baFatalE(FE_MALLOC,0);
   return mem;
}

/* This function runs in the context of a thread in the LThreadMgr, and
   is triggered by the dispatchExecXedgeEvent.
 */
static void netExecXedgeEventCB2(ThreadJob* jb, int msgh, LThreadMgr* mgr)
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
static void netExecXedgeEventCB1(EventBrokerCallbackArg arg)
{
   ThreadMutex_set(soDispMutex);
   LThreadMgr_run(&ltMgr, (ThreadJob*)arg.ptr);
   ThreadMutex_release(soDispMutex);
}

/* Function execXedgeEvent is used by callbacks that run in the
 * context of the lwIP thread. We do not want to lock the soDispMutex
 * here since this could lead to a deadlock.
 */
static void netExecXedgeEvent(const char* cmd, char* param1, char* param2, char* param3)
{
   ExecXedgeEventJob* job = (ExecXedgeEventJob*) netCheckAlloc(
                             ThreadJob_lcreate(sizeof(ExecXedgeEventJob), netExecXedgeEventCB2));
   job->cmd=cmd;
   job->param1=param1;
   job->param2=param2;
   job->param3=param3;
   EventBrokerQueueNode n = { .callback = netExecXedgeEventCB1, .arg.ptr=job };
   xQueueSend(eventBrokerQueue, &n, 0);
}

static void netXedgeEventInit(void)
{
static StaticQueue_t xStaticQueue;

   eventBrokerQueue = xQueueCreateStatic(20, sizeof(EventBrokerQueueNode), 
                                         eventBrokerQueueBuf, &xStaticQueue);
   
   xTaskCreate(eventBrokerTask, "eventBroker", 2048, 0, configMAX_PRIORITIES-1, 0);
}

#if SOC_WIFI_SUPPORTED
const char* wifiAuthMode(int authmode, int print)
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

const char* wifiCipherType(int pcipher, int gcipher, int print, const char** pciphers)
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

#endif // SOC_WIFI_SUPPORTED


static void onSntpSync(struct timeval *tv)
{
   netExecXedgeEvent("sntp", 0, 0, 0);
}

/**
 * @brief Checks if the IP address has been acquired.
 * @return TRUE if the IP address is set, 0 otherwise.
 */
int netGotIP()
{
   return (gotIP == TRUE); 
}

/**
 * @brief Informs the BAS about the EVENT_GOT_IP event.
 *
 * This function is called when the ESP32 obtains an IP address, and it notifies
 * the BAS by providing the event ID and network information.
 *
 * @param eventId The event ID (e.g., IP_EVENT_STA_GOT_IP for Wi-Fi).
 * @param ipInfo Network information, including the obtained IP details.
 */ 
void netEventGotIP(int32_t eventId, const esp_netif_ip_info_t* ipInfo)
{
   char* param1 = (char*)netCheckAlloc(baMalloc(16));
   char* param2 = (char*)netCheckAlloc(baMalloc(16));
   char* param3 = (char*)netCheckAlloc(baMalloc(16));
   gotIP=TRUE;
   basprintf(param1, IPSTR,IP2STR(&ipInfo->ip));
   basprintf(param2, IPSTR,IP2STR(&ipInfo->netmask));
   basprintf(param3, IPSTR,IP2STR(&ipInfo->gw));

   if(esp_log_level_get("*") < ESP_LOG_INFO) 
   {
      HttpTrace_printf(0, "\033[32m\nip: %d.%d.%d.%d, mask: %d.%d.%d.%d, gw: %d.%d.%d.%d\033[0m\n", 
                           IP2STR(&ipInfo->ip),
                           IP2STR(&ipInfo->netmask),
                           IP2STR(&ipInfo->gw));
   }
                        
   if(IP_EVENT_STA_GOT_IP == eventId)
   {
      netExecXedgeEvent("wip", param1, param2, param3);
   }
   else
   {
      netExecXedgeEvent("eth", param1, param2, param3);
   }

//   if(semGotIp)
//      xSemaphoreGive(semGotIp);
}

#if SOC_WIFI_SUPPORTED

/**
 * @brief Callback function for network events.
 *        This function is the callback for the IP_EVENT_STA_GOT_IP event, 
 *        which is triggered when the ESP32 successfully obtains an IP address.
 *
 * @param arg Pointer to user-defined data passed during event handler registration (unused).
 * @param eventBase Event base associated with the event.
 * @param eventId ID of the event that triggered the callback.
 * @param eventData Pointer to event-specific data.
*/
static void onNetEvent(void *arg, esp_event_base_t eventBase,
                        int32_t eventId, void *eventData)
{
   if(eventBase == WIFI_EVENT)
   {
      if(WIFI_EVENT_STA_CONNECTED == eventId)
      {
         netExecXedgeEvent("wifi", baStrdup("up"), baStrdup("sta"), 0);
         
         // Disable AP mode upon successful Wi-Fi connection.
         apMode = AP_MODE_DISABLED;
      }
      else if(WIFI_EVENT_STA_DISCONNECTED == eventId) 
      {
         wifi_event_sta_disconnected_t* d = (wifi_event_sta_disconnected_t*)eventData;
         HttpTrace_printf(9, "WiFi disconnect ev. %d\n", d->reason);
         if(gotIP)
         {
            netExecXedgeEvent("wifi", baStrdup("down"), baStrdup("sta"), 0);
         }
         char* param = (char*)baMalloc(20);
         if(param)
         {
            basnprintf(param, 20, "%d", d->reason);
            netExecXedgeEvent("wifi", param, 0, 0);
         }
         gotIP=FALSE;

         // Check if AP mode was user-requested and manage transitions accordingly
         if(apMode == AP_MODE_USER_REQUESTED)
         {   
            // Handle the case where ESP32 could not find the configured access point.
            if(d->reason == WIFI_REASON_NO_AP_FOUND) 
            {
               // Increment retry count and check if it exceeds the threshold.
               if(netWifiRetry++ > 4) 
               {
                  apMode = AP_MODE_ENABLED;
               }
            }
         
            // Handle 4-way handshake timeout (often due to incorrect password)
            if(d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)
            {
               apMode = AP_MODE_ENABLED;
            }
         }
         
         // The AP mode is set to enabled only if it was explicitly requested by the user, 
         // ensuring the system doesn't switch to AP mode due to transient errors unless specifically instructed
         if(apMode == AP_MODE_ENABLED)
         {
            // Erase network configuration (SSID and password)
            // to avoid attempting reconnection with invalid credentials.
            cfgEraseNet(); 
            netWifiStop(false); 
            netWifiApStart(false);  
         }
         else 
         {
            esp_wifi_connect();
         }
      }
      else if(eventId == WIFI_EVENT_AP_STACONNECTED) 
      {
         wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) eventData;
         HttpTrace_printf(9, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
      } 
      else if(eventId == WIFI_EVENT_AP_STADISCONNECTED) 
      {
         wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) eventData;
         HttpTrace_printf(9, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
      } 
      else
      {
         ESP_LOGD(TAG, "Non managed WiFi event %ld\n",eventId);
      }
   }
   else if((IP_EVENT == eventBase) && ((IP_EVENT_STA_GOT_IP == eventId) || 
                                      (IP_EVENT_ETH_GOT_IP == eventId)) )
   {
      ip_event_got_ip_t* event = (ip_event_got_ip_t*)eventData;
      netEventGotIP(eventId, &event->ip_info);
      HttpTrace_printf(9, "Interface \"%s\" up\n",
                       esp_netif_get_desc(event->esp_netif));
   }
}

/**
 * @brief Initializes and start the Wi-Fi network on the ESP32.
 *    
 * @return ESP_OK if the Wi-Fi connection was successfully started.
 */
static esp_err_t netWifiStart(void)
{
   ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onNetEvent, NULL));
   ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onNetEvent, NULL));
      
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));
   esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
   esp_netif_config.if_desc = "WiFi STA";
   esp_netif_config.route_prio = 128;
   wifi_netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
   assert(wifi_netif);
   esp_wifi_set_default_wifi_sta_handlers();
   ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
   ESP_ERROR_CHECK(esp_wifi_start());

   return ESP_OK;
}

/**
 * @brief Stop the WiFi network on the ESP32.
 *
 * @param[in] unregHandler If true, unregister event handlers for WIFI_EVENT and IP_EVENT.
 *
 * @return ESP_OK if the WiFi connection was stopped successfully.
 */
static esp_err_t netWifiStop(bool unregHandler) 
{
   printf("Closing Wi-Fi connection\n");
  
   if(wifi_netif != NULL) 
   {
      if(unregHandler)
      {
         esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &onNetEvent);
         esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &onNetEvent);
      }
      
      esp_wifi_disconnect();
      esp_wifi_stop();
      ESP_ERROR_CHECK(esp_wifi_deinit());
      ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(wifi_netif));

      esp_netif_destroy(wifi_netif);
      wifi_netif = NULL;
   }

   return ESP_OK;
}

/**
 * @brief Initializes and starts the Wi-Fi network in Access Point (AP) mode.
 *
 * @param[in] regHandler If true, register an event handler for WIFI_EVENT.
 *
 * @return ESP_OK if the Wi-Fi AP mode was successfully started.
 */ 
esp_err_t netWifiApStart(bool regHandler)
{
   ESP_ERROR_CHECK(esp_netif_init());
   ap_netif = esp_netif_create_default_wifi_ap();

   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));

   if(regHandler)
   {
      ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onNetEvent, NULL));
   }
    
   wifi_config_t wifi_config = 
   {
      .ap = 
      {
         .ssid = CONFIG_WIFI_AP_SSID,
         .ssid_len = strlen(CONFIG_WIFI_AP_SSID),
         .channel = CONFIG_WIFI_AP_CHANNEL,
         .password = CONFIG_WIFI_AP_PASSWORD,
         .max_connection = CONFIG_WIFI_AP_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
         .authmode = WIFI_AUTH_WPA2_WPA3_PSK, 
         .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else 
         .authmode = WIFI_AUTH_WPA2_PSK,
#endif
         .pmf_cfg = 
         {
            .required = true,
         },
      },
   };
   
   if(strlen(CONFIG_WIFI_AP_PASSWORD) == 0) 
   {
      wifi_config.ap.authmode = WIFI_AUTH_OPEN;
   }
   
   esp_netif_ip_info_t ipInfo = 
   {
      .ip = {esp_ip4addr_aton(CONFIG_WIFI_AP_IP_ADDR)},
      .gw = {esp_ip4addr_aton(CONFIG_WIFI_AP_GATEWAY)},
      .netmask = {esp_ip4addr_aton(CONFIG_WIFI_AP_NETMASK)},
   };

   printf("ap ip:%s, gw:%s, mask:%s\n", CONFIG_WIFI_AP_IP_ADDR, CONFIG_WIFI_AP_GATEWAY, CONFIG_WIFI_AP_NETMASK);
   
   esp_netif_dhcps_stop(ap_netif);
   esp_netif_set_ip_info(ap_netif, &ipInfo);
   esp_netif_dhcps_start(ap_netif);

   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
   ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
   ESP_ERROR_CHECK(esp_wifi_start());
              
   netExecXedgeEvent("wifi", baStrdup("up"), baStrdup("ap"), 0);
   netEventGotIP(IP_EVENT_STA_GOT_IP, &ipInfo);
   
   return ESP_OK;          
}

/**
 * @brief Stops the Wi-Fi network in Access Point (AP) mode on the ESP32.
 *
 * @return ESP_OK if the Wi-Fi AP mode was successfully stopped.
 */ 
static esp_err_t netWifiApStop(void) 
{
   printf("Closing Wi-Fi AP connection\n");
  
   if(ap_netif != NULL) 
   {
      netExecXedgeEvent("wifi", baStrdup("down"), baStrdup("ap"), 0);
      esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &onNetEvent);
      esp_wifi_disconnect();
      esp_wifi_stop();
      ESP_ERROR_CHECK(esp_wifi_deinit());
      ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(ap_netif));

      esp_netif_destroy(ap_netif);
      ap_netif = NULL;
   }

   return ESP_OK;
}

/**
 * @brief Stop the Ethernet network on the ESP32.
 *
 * @note: It’s not recommended to uninstall Ethernet driver unless it won’t 
 *        get used any more in application code. To uninstall Ethernet driver, 
 *        you have to make sure, all references to the driver are released. 
 *        Ethernet driver can only be uninstalled successfully when reference 
 *        counter equals to one.
 *    
 * @return ESP_OK if the ethernet connection was stoped.
 */
static esp_err_t netEthStop(void)
{
   printf("Closing Ethernet connection\n");
#ifdef CONFIG_ETH_ENABLED    
   if((eth_netif != NULL) && (s_eth_handle != NULL)) 
   {
      ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onNetEvent));
      ESP_ERROR_CHECK(esp_eth_stop(s_eth_handle));
      ESP_ERROR_CHECK(esp_eth_del_netif_glue(s_eth_glue));
      s_eth_glue = NULL;
       
      vTaskDelay(pdMS_TO_TICKS(100));
      ESP_ERROR_CHECK(esp_eth_driver_uninstall(s_eth_handle));
      s_eth_handle = NULL;
      ESP_ERROR_CHECK(s_phy->del(s_phy));
      ESP_ERROR_CHECK(s_mac->del(s_mac));

      esp_netif_destroy(eth_netif);
      eth_netif = NULL;
      
      spi_bus_free(spiHostId);  
   }
#endif   
   return ESP_OK;
}

#if CONFIG_ETH_USE_ESP32_EMAC
/**
 * @brief Initialize the Ethernet interface using RMII mode.
 *
 * @param mdcPin The GPIO pin number for the MDC (Management Data Clock) signal.
 * @param mdioPin The GPIO pin number for the MDIO (Management Data Input/Output) signal.
 */
static void netRmiiInit(int mdcPin, int mdioPin)
{
   eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
   esp32_emac_config.smi_mdc_gpio_num = mdcPin; 
   esp32_emac_config.smi_mdio_gpio_num = mdioPin; 
   s_mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
} 
#endif

#ifdef CONFIG_ETH_USE_SPI_ETHERNET
/**
 * @brief Initialize the Ethernet interface using SPI mode.
 *
 * @param spi Pointer to the SPI pin configurations.
 * @param spi_devcfg Pointer to the SPI device configurations.
 */
static void netSpiInit(netSpi_t* spi, spi_device_interface_config_t* spi_devcfg)
{
   spi_bus_config_t buscfg = {
      .miso_io_num = spi->miso, 
      .mosi_io_num = spi->mosi, 
      .sclk_io_num = spi->clk, 
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
   };
   
   spiHostId = spi->hostId;                                   
   ESP_ERROR_CHECK(spi_bus_initialize(spiHostId, &buscfg, SPI_DMA_CH_AUTO));
   
   spi_devcfg->mode = 0;
   spi_devcfg->clock_speed_hz = spi->freq; 
   spi_devcfg->spics_io_num = spi->cs;
   spi_devcfg->queue_size = 20;
}
#endif

#ifdef CONFIG_ETH_ENABLED
/**
 * @brief Sets the MAC address for the ESP32's internal Ethernet MAC.
 *
 * @return esp_err_t Returns ESP_OK if the MAC address is set successfully.
 */
static esp_err_t netSetMac(void)
{

uint8_t mac[6];  // Array to store the MAC address

   // Read the MAC address of the ESP32's internal Ethernet MAC
   esp_err_t err = esp_read_mac(mac, ESP_MAC_ETH);  
   if(err == ESP_OK) 
   {
      // If the MAC address is successfully obtained, set it using esp_eth_ioctl
      err = esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, mac);
   }

   return err;
}
#endif

/**
 * @brief Connect to a Wi-Fi network with the specified SSID and password.
 *        Disconnects from any currently connected Wi-Fi network 
 *        and attempts to connect to the provided network.
 * 
 * @param ssid Pointer to the string containing the SSID of the Wi-Fi network.
 * @param password Pointer to the string containing the password of the Wi-Fi network.
 * @return ESP_OK if the connection attempt is successful.
 */
esp_err_t netWifiConnect(char* ssid, char* password)
{
esp_err_t ret = ESP_OK;

   esp_wifi_disconnect();
   
   if(ssid)
   {
      // FIXME esp_wifi_set_country_code

      wifi_config_t cfg = {
         .sta = {
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = CONFIG_WIFI_SCAN_RSSI_THRESHOLD,
            .threshold.authmode = WIFI_AUTH_OPEN
         }
      };
      
      if(strlen(ssid) < sizeof(cfg.sta.ssid) &&
         strlen(password) < sizeof(cfg.sta.password))
      {
         printf("Connecting to: %s\n", ssid);
         
         strcpy((char*)cfg.sta.ssid, ssid);
         strcpy((char*)cfg.sta.password, password);

         ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
         ret = esp_wifi_connect();
         if(ESP_ERR_WIFI_NOT_STARTED == ret)
         {
            esp_wifi_start();
            ret = esp_wifi_connect();
         }
         
         if(ESP_OK != ret)
         {  
            ESP_LOGD(TAG, "WiFi connect failed: %x\n", ret);
            ret = ESP_ERR_WIFI_SSID;
         }
      } 
      else
      {
         esp_wifi_stop();
         ret = ESP_ERR_INVALID_ARG;
      }
   }
   
   return ret;
}

/**
 * @brief Connects the Ethernet interface.
 * This function starts the Ethernet interface if it has been initialized (s_eth_handle is not NULL).
 *
 * @return ESP_OK if the Ethernet interface is successfully started.
 */
esp_err_t netEthConnect(void)
{
#ifdef CONFIG_ETH_ENABLED
   if(s_eth_handle != NULL)
   {
      esp_eth_start(s_eth_handle);
      return ESP_OK;
   }
#endif
   
   return ESP_ERR_INVALID_ARG;
}

/**
 * @brief Starts the Ethernet network interface based on the provided configuration.
 *  
 * @param[in] cfg Pointer to the network configuration structure.
 * @return ESP_OK if the Ethernet network interface is successfully started.
 */
static esp_err_t netEthStart(netConfig_t* cfg)
{
#ifdef CONFIG_ETH_ENABLED
   esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
   esp_netif_config.if_desc = "Ethernet";
   esp_netif_config.route_prio = 64;
   esp_netif_config_t netif_config = {
     .base = &esp_netif_config,
     .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
   };

   eth_netif = esp_netif_new(&netif_config);
   assert(eth_netif);
   
   eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
   mac_config.rx_task_stack_size = CONFIG_ETHERNET_EMAC_TASK_STACK_SIZE;
   eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
   phy_config.phy_addr = CONFIG_ETHERNET_PHY_ADDR;
   phy_config.reset_gpio_num = cfg->phyRstPin; 

   
   if(!strcmp("IP101", cfg->adapter))
   {
#if CONFIG_ETH_USE_ESP32_EMAC
      netRmiiInit(cfg->phyMdcPin, cfg->phyMdioPin);
      s_phy = esp_eth_phy_new_ip101(&phy_config);
#else
   ESP_LOGD(TAG, "CONFIG_ETH_USE_ESP32_EMAC undef");
   return ESP_ERR_INVALID_ARG;  
#endif  
   }
   else if(!strcmp("RTL8201", cfg->adapter))
   {
#if CONFIG_ETH_USE_ESP32_EMAC
      netRmiiInit(cfg->phyMdcPin, cfg->phyMdioPin);
      s_phy = esp_eth_phy_new_rtl8201(&phy_config);
#else
   ESP_LOGD(TAG, "CONFIG_ETH_USE_ESP32_EMAC undef");
   return ESP_ERR_INVALID_ARG;  
#endif 
   }
   else if(!strcmp("LAN87XX", cfg->adapter))
   {
#if CONFIG_ETH_USE_ESP32_EMAC
      netRmiiInit(cfg->phyMdcPin, cfg->phyMdioPin);
      s_phy = esp_eth_phy_new_lan87xx(&phy_config);
#else
   ESP_LOGD(TAG, "CONFIG_ETH_USE_ESP32_EMAC undef");
   return ESP_ERR_INVALID_ARG;  
#endif 
   }
   else if(!strcmp("DP83848", cfg->adapter))
   {
#if CONFIG_ETH_USE_ESP32_EMAC
      netRmiiInit(cfg->phyMdcPin, cfg->phyMdioPin);
      s_phy = esp_eth_phy_new_dp83848(&phy_config);
#else
   ESP_LOGD(TAG, "CONFIG_ETH_USE_ESP32_EMAC undef");
   return ESP_ERR_INVALID_ARG;  
#endif 
   }
   else if(!strcmp("KSZ80XX", cfg->adapter))
   {
#if CONFIG_ETH_USE_ESP32_EMAC
      netRmiiInit(cfg->phyMdcPin, cfg->phyMdioPin);
      s_phy = esp_eth_phy_new_ksz80xx(&phy_config);
#else
   ESP_LOGD(TAG, "CONFIG_ETH_USE_ESP32_EMAC undef");
   return ESP_ERR_INVALID_ARG;  
#endif 
   }
   /* dm9051 ethernet driver is based on spi driver */
   else if(!strcmp("DM9051", cfg->adapter))
   {
#if CONFIG_ETH_SPI_ETHERNET_DM9051
      spi_device_interface_config_t spi_devcfg;
      netSpiInit(&cfg->spi, &spi_devcfg);
                                                                     
      eth_dm9051_config_t dm9051_config = ETH_DM9051_DEFAULT_CONFIG(cfg->spi.hostId, &spi_devcfg);
      dm9051_config.int_gpio_num = cfg->spi.irq; 
      s_mac = esp_eth_mac_new_dm9051(&dm9051_config, &mac_config);
      s_phy = esp_eth_phy_new_dm9051(&phy_config);
#else
   ESP_LOGD(TAG, "CONFIG_ETH_SPI_ETHERNET_DM9051 undef");
   return ESP_ERR_INVALID_ARG;  
#endif 
   }
   /* w5500 ethernet driver is based on spi driver */
   else if(!strcmp("W5500", cfg->adapter))
   {
#if CONFIG_ETH_SPI_ETHERNET_W5500
      spi_device_interface_config_t spi_devcfg = {0};
      netSpiInit(&cfg->spi, &spi_devcfg);
                                                                  
      eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(cfg->spi.hostId, &spi_devcfg);
      w5500_config.int_gpio_num = cfg->spi.irq; 
      s_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
      s_phy = esp_eth_phy_new_w5500(&phy_config);
#else
   ESP_LOGD(TAG, "CONFIG_ETH_SPI_ETHERNET_W5500 undef");
   return ESP_ERR_INVALID_ARG;  
#endif 
   }
  
   else 
   {
      return ESP_ERR_INVALID_ARG;
   }
   
   ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onNetEvent, NULL));
   
   // Install Ethernet driver
   esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
   esp_err_t err = esp_eth_driver_install(&config, &s_eth_handle);

   if(err != ESP_OK)
   {
      return err;
   }
      
   if(netIsAdapterSpi(cfg->adapter))
   {
      netSetMac();
   }
   
   // combine driver with netif
   s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
   esp_netif_attach(eth_netif, s_eth_glue);
#endif

   return ESP_OK; 
}
    
/**
 * @brief Waits for the acquisition of an IP address before continuing.
 *        This function is used to prevent the Barracuda App Server from 
 *        starting until a valid IP address is obtained.
 * @note This function assumes that the soDispMutex mutex is already 
 *       locked by the calling function.
 */
#if 0
void netWaitIP(void)   
{
   if(!gotIP)
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
#endif

/**
 * @brief Checks if the network adapter is a SPI-based interface.
 * @param cfg Pointer to the network configuration structure.
 * @return TRUE if the adapter is SPI-based.
 */
int netIsAdapterSpi(char* adapter)
{
   return (!strcmp("W5500", adapter) || !strcmp("DM9051", adapter));
}

/**
 * @brief Checks if the network adapter is a RMII-based interface.
 * @param cfg Pointer to the network configuration structure.
 * @return TRUE if the adapter is RMII-based.
 */
int netIsAdapterRmii(char* adapter)
{
   return (!strcmp("IP101", adapter) || !strcmp("RTL8201", adapter) ||
           !strcmp("LAN87XX", adapter) || !strcmp("DP83848", adapter) ||
           !strcmp("KSZ80XX", adapter));
}

/**
 * @brief Initialize and start the network interfaces.
 *        If it the configuration has a valid adapter starts the appropriate network interface.
 *        Sets up SNTP (Simple Network TimeProtocol) configuration for time synchronization.
 * @return false if there are no configured adapters, otherwise true.
 */   
bool netInit(void) 
{
netConfig_t cfg;
   
   ESP_ERROR_CHECK(esp_netif_init());
   ESP_ERROR_CHECK(esp_event_loop_create_default());
   
   gpio_install_isr_service(0);
    
   cfgGetNet(&cfg);
  
   if(!strcmp("wifi", cfg.adapter))
   {   
      netWifiStart();
   }
   else if(netIsAdapterSpi(cfg.adapter) || netIsAdapterRmii(cfg.adapter))
   {
      netEthStart(&cfg);      
   }
   
   netXedgeEventInit();
         
   esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
   esp_sntp_setservername(0, "pool.ntp.org");
   esp_sntp_init();
   sntp_set_time_sync_notification_cb(onSntpSync);
   
   return !strcmp("close", cfg.adapter) ? false : true;
}

/**
 * @brief Establishes a network connection using the provided network configuration.
 *
 * @param cfg Pointer to the network configuration structure.
 * @return esp_err_t Returns ESP_OK if the connection was successful.
 */
esp_err_t netConnect(netConfig_t* cfg)
{
esp_err_t ret = ESP_OK;

   // Set the state to revert to AP mode if the Wi-Fi network is not found or connection fails.
   apMode = AP_MODE_USER_REQUESTED;
   netWifiRetry = 0;

   if(!strcmp("close", cfg->adapter))
   {
      cfgEraseNet();  

      if(wifi_netif != NULL) 
      {
         ESP_ERROR_CHECK(netWifiStop(true));  
      }
      
      if(eth_netif != NULL) 
      {
         ESP_ERROR_CHECK(netEthStop());
      }   
   
      if(ap_netif == NULL)
      {
         ret = netWifiApStart(true);
      }
      
      return ret;   
   }
   
   if(!strcmp("wifi", cfg->adapter))
   {
      // Turn off AP when installed.
      if(ap_netif != NULL)
      {
         ESP_ERROR_CHECK(netWifiApStop());
      }
      
      // Turn off ethernet when installed.
      if(eth_netif != NULL) 
      {
         ESP_ERROR_CHECK(netEthStop());
      }
      
      // Start wifi when not already installed. 
      if(wifi_netif == NULL) 
      {
         ESP_ERROR_CHECK(netWifiStart()); 
      }
      
      ret = netWifiConnect(cfg->ssid, cfg->password);
   }
   // If the adapter is ethernet by SPI or RMII.
   else if(netIsAdapterSpi(cfg->adapter) || netIsAdapterRmii(cfg->adapter))
   {
      if(wifi_netif != NULL)
      {
         ESP_ERROR_CHECK(netWifiStop(true));   
      }
      else if(eth_netif != NULL)
      {
         ESP_ERROR_CHECK(netEthStop());
      } 
      else if(ap_netif != NULL)
      {
         ESP_ERROR_CHECK(netWifiApStop());
      }
      
      ret = netEthStart(cfg);  
      if(ret == ESP_OK)
      {
         ret = netEthConnect();
      } 
   }
   
   // Save config when the adapter started successfully. 
   if(ESP_OK == ret)
   {
      cfgSetNet(cfg);
   }
   else
   {
      apMode = AP_MODE_DISABLED;
   }
          
   return ret;
}

#else // end SOC_WIFI_SUPPORTED

//Fixme: Code below is just for test purposes

#include "esp_eth_driver.h"
#include "esp_check.h"
#include "sdkconfig.h"
#define SPI_ETHERNETS_NUM 1
#define INTERNAL_ETHERNETS_NUM      1


static esp_eth_handle_t eth_init_internal(esp_eth_mac_t **mac_out, esp_eth_phy_t **phy_out)
{
    esp_eth_handle_t ret = NULL;

    // Init common MAC and PHY configs to default
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    // Update PHY config based on board specific configuration
    phy_config.phy_addr = CONFIG_EXAMPLE_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_EXAMPLE_ETH_PHY_RST_GPIO;
    // Init vendor specific MAC config to default
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    // Update vendor specific MAC config based on board configuration
    esp32_emac_config.smi_gpio.mdc_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;
#if CONFIG_EXAMPLE_USE_SPI_ETHERNET
    // The DMA is shared resource between EMAC and the SPI. Therefore, adjust
    // EMAC DMA burst length when SPI Ethernet is used along with EMAC.
    esp32_emac_config.dma_burst_len = ETH_DMA_BURST_LEN_4;
#endif // CONFIG_EXAMPLE_USE_SPI_ETHERNET
    // Create new ESP32 Ethernet MAC instance
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    // Create new PHY instance based on board configuration
#if CONFIG_EXAMPLE_ETH_PHY_GENERIC
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_IP101
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_RTL8201
    esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_LAN87XX
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_DP83848
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_KSZ80XX
    esp_eth_phy_t *phy = esp_eth_phy_new_ksz80xx(&phy_config);
#endif
    // Init Ethernet driver to default and install it
    esp_eth_handle_t eth_handle = NULL;
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_GOTO_ON_FALSE(esp_eth_driver_install(&config, &eth_handle) == ESP_OK, NULL,
                        err, TAG, "Ethernet driver install failed");

    if (mac_out != NULL) {
        *mac_out = mac;
    }
    if (phy_out != NULL) {
        *phy_out = phy;
    }
    return eth_handle;
err:
    if (eth_handle != NULL) {
        esp_eth_driver_uninstall(eth_handle);
    }
    if (mac != NULL) {
        mac->del(mac);
    }
    if (phy != NULL) {
        phy->del(phy);
    }
    return ret;
}

esp_err_t example_eth_init(esp_eth_handle_t *eth_handles_out[], uint8_t *eth_cnt_out)
{
    esp_err_t ret = ESP_OK;
    esp_eth_handle_t *eth_handles = NULL;
    uint8_t eth_cnt = 0;

#if CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET || CONFIG_EXAMPLE_USE_SPI_ETHERNET
    ESP_GOTO_ON_FALSE(eth_handles_out != NULL && eth_cnt_out != NULL, ESP_ERR_INVALID_ARG,
                        err, TAG, "invalid arguments: initialized handles array or number of interfaces");
    eth_handles = calloc(SPI_ETHERNETS_NUM + INTERNAL_ETHERNETS_NUM, sizeof(esp_eth_handle_t));
    ESP_GOTO_ON_FALSE(eth_handles != NULL, ESP_ERR_NO_MEM, err, TAG, "no memory");

#if CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
    eth_handles[eth_cnt] = eth_init_internal(NULL, NULL);
    ESP_GOTO_ON_FALSE(eth_handles[eth_cnt], ESP_FAIL, err, TAG, "internal Ethernet init failed");
    eth_cnt++;
#endif //CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET

#if CONFIG_EXAMPLE_USE_SPI_ETHERNET
    ESP_GOTO_ON_ERROR(spi_bus_init(), err, TAG, "SPI bus init failed");
    // Init specific SPI Ethernet module configuration from Kconfig (CS GPIO, Interrupt GPIO, etc.)
    spi_eth_module_config_t spi_eth_module_config[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM] = { 0 };
    INIT_SPI_ETH_MODULE_CONFIG(spi_eth_module_config, 0);
    // The SPI Ethernet module(s) might not have a burned factory MAC address, hence use manually configured address(es).
    // In this example, Locally Administered MAC address derived from ESP32x base MAC address is used.
    // Note that Locally Administered OUI range should be used only when testing on a LAN under your control!
    uint8_t base_mac_addr[ETH_ADDR_LEN];
    ESP_GOTO_ON_ERROR(esp_efuse_mac_get_default(base_mac_addr), err, TAG, "get EFUSE MAC failed");
    uint8_t local_mac_1[ETH_ADDR_LEN];
    esp_derive_local_mac(local_mac_1, base_mac_addr);
    spi_eth_module_config[0].mac_addr = local_mac_1;
#if CONFIG_EXAMPLE_SPI_ETHERNETS_NUM > 1
    INIT_SPI_ETH_MODULE_CONFIG(spi_eth_module_config, 1);
    uint8_t local_mac_2[ETH_ADDR_LEN];
    base_mac_addr[ETH_ADDR_LEN - 1] += 1;
    esp_derive_local_mac(local_mac_2, base_mac_addr);
    spi_eth_module_config[1].mac_addr = local_mac_2;
#endif
#if CONFIG_EXAMPLE_SPI_ETHERNETS_NUM > 2
#error Maximum number of supported SPI Ethernet devices is currently limited to 2 by this example.
#endif
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        eth_handles[eth_cnt] = eth_init_spi(&spi_eth_module_config[i], NULL, NULL);
        ESP_GOTO_ON_FALSE(eth_handles[eth_cnt], ESP_FAIL, err, TAG, "SPI Ethernet init failed");
        eth_cnt++;
    }
#endif // CONFIG_EXAMPLE_USE_SPI_ETHERNET
#else
    ESP_LOGD(TAG, "no Ethernet device selected to init");
#endif // CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET || CONFIG_EXAMPLE_USE_SPI_ETHERNET
    *eth_handles_out = eth_handles;
    *eth_cnt_out = eth_cnt;

    return ret;
#if CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET || CONFIG_EXAMPLE_USE_SPI_ETHERNET
err:
    free(eth_handles);
    return ret;
#endif
}

void wifiScan(int print, lua_State* L,
              void (*cb)(lua_State* L, const uint8_t* ssid, int rssi,
                        const char* authmode,const char*  pchiper,
                        const char* gcipher, int channel))
{
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ipInfo = &event->ip_info;
    netEventGotIP(event_id, ipInfo);
}



esp_err_t netConnect(netConfig_t* cfg)
{
    return ESP_OK;
}


bool netInit(void) 
{
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;
    ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));

    // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *eth_netifs[eth_port_cnt];
    esp_eth_netif_glue_handle_t eth_netif_glues[eth_port_cnt];

    // Create instance(s) of esp-netif for Ethernet(s)
    if (eth_port_cnt == 1) {
        // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
        // default esp-netif configuration parameters.
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        eth_netifs[0] = esp_netif_new(&cfg);
        eth_netif_glues[0] = esp_eth_new_netif_glue(eth_handles[0]);
        // Attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[0], eth_netif_glues[0]));
    } else {
        // Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are used and so you need to modify
        // esp-netif configuration parameters for each interface (name, priority, etc.).
        esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
        esp_netif_config_t cfg_spi = {
            .base = &esp_netif_config,
            .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
        };
        char if_key_str[10];
        char if_desc_str[10];
        char num_str[3];
        for (int i = 0; i < eth_port_cnt; i++) {
            itoa(i, num_str, 10);
            strcat(strcpy(if_key_str, "ETH_"), num_str);
            strcat(strcpy(if_desc_str, "eth"), num_str);
            esp_netif_config.if_key = if_key_str;
            esp_netif_config.if_desc = if_desc_str;
            esp_netif_config.route_prio -= i*5;
            eth_netifs[i] = esp_netif_new(&cfg_spi);
            eth_netif_glues[i] = esp_eth_new_netif_glue(eth_handles[i]);
            // Attach Ethernet driver to TCP/IP stack
            ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[i], eth_netif_glues[i]));
        }
    }

    // Register user defined event handlers
    //ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // Start Ethernet driver state machine
    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }

   netXedgeEventInit();
         
   esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
   esp_sntp_setservername(0, "pool.ntp.org");
   esp_sntp_init();
   sntp_set_time_sync_notification_cb(onSntpSync);
   
   return true;
}


#endif // end not SOC_WIFI_SUPPORTED

