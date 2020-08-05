/* Esptouch example

   This code example is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   Modified by RTL.
*/
#ifdef SMARTCONFIG

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "driver/gpio.h"

/* FreeRTOS event group to signal when we are connected & ready to
 * make a request
 */
static EventGroupHandle_t doneEventGrp;

static const int DONE_BIT = BIT1;
static const char *TAG = "SmartConfig";

static nvs_handle_t nvsh;


#define BLINK_GPIO1 GPIO_NUM_5
#define BLINK_GPIO2 GPIO_NUM_4


typedef struct {
   int msBlink;
   uint8_t pattern[4];   
} LED;

static LED* ledPtr;

#define milliSleep(ms) vTaskDelay(pdMS_TO_TICKS(ms))

static void setLedPattern(int msBlink, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
   ledPtr->msBlink=msBlink;
   ledPtr->pattern[0]=a;
   ledPtr->pattern[1]=b;
   ledPtr->pattern[2]=c;
   ledPtr->pattern[3]=d;
}

static void ledTask(void* param)
{
   gpio_pad_select_gpio(BLINK_GPIO1);
   gpio_set_direction(BLINK_GPIO1, GPIO_MODE_OUTPUT);
   gpio_pad_select_gpio(BLINK_GPIO2);
   gpio_set_direction(BLINK_GPIO2, GPIO_MODE_OUTPUT);
   for(;;)
   {
      for(int i= 0; i < sizeof(ledPtr->pattern) ; i++)
      {
         uint32_t level=ledPtr->pattern[i] ? 1 : 0;
         gpio_set_level(BLINK_GPIO1, level);
         gpio_set_level(BLINK_GPIO2, level);
         milliSleep(ledPtr->msBlink);
      }
   }
}

static void restart()
{
   ESP_LOGI(TAG, "Restarting...");
   setLedPattern(50,1,0,1,0);
   milliSleep(2500);
   esp_restart();
}

static void connectEvent(void* arg, esp_event_base_t event_base, 
                          int32_t event_id, void* event_data)
{
   if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
   {
      setLedPattern(400,1,0,0,0);
      ESP_LOGI(TAG, "WiFi started");
   }
   else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
   {
      ESP_LOGI(TAG, "WiFi disconnected");
      uint8_t rcounter; /* Reconnect counter */
      ESP_ERROR_CHECK(nvs_get_u8(nvsh, "rcounter", &rcounter));
      printf("rcounter %d\n",rcounter);
      if(rcounter > 0)
      {
         rcounter--;
         nvs_set_u8(nvsh, "rcounter", rcounter);
      }
      restart();
   }
   else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
   {
      ESP_LOGI(TAG, "WiFi Connected to ap");
      xEventGroupSetBits(doneEventGrp, DONE_BIT);
   }
}


static void smartConfigEvent(void* arg, esp_event_base_t event_base, 
                          int32_t event_id, void* event_data)
{
   if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
   {
      setLedPattern(400,1,0,0,0);
      ESP_LOGI(TAG, "WiFi started");
   }
   else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
   {
      ESP_LOGI(TAG, "WiFi disconnected");
      restart();
   }
   else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
   {
      setLedPattern(400,1,0,1,0);
      ESP_LOGI(TAG, "WiFi Connected to AP");
   }
   else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
   {
      ESP_LOGI(TAG, "Scan done");
   }
   else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
   {
      ESP_LOGI(TAG, "Found channel");
   }
   else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
   {
      setLedPattern(200,1,0,0,0);
      ESP_LOGI(TAG, "Got SSID and password");

      smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
      wifi_config_t wifi_config;

      bzero(&wifi_config, sizeof(wifi_config_t));
      memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
      memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
      wifi_config.sta.bssid_set = evt->bssid_set;
      if (wifi_config.sta.bssid_set == true) {
         memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
         ESP_ERROR_CHECK(nvs_set_blob(nvsh,"bssid",evt->bssid, sizeof(wifi_config.sta.bssid)));
      }
      ESP_ERROR_CHECK(nvs_set_str(nvsh,"ssid",(char*)evt->ssid));
      ESP_ERROR_CHECK(nvs_set_str(nvsh,"password",(char*)evt->password));
      ESP_LOGI(TAG, "SSID: %s", evt->ssid);
      ESP_LOGI(TAG, "PASSWORD: %s", evt->password);
      ESP_ERROR_CHECK(esp_wifi_disconnect() );
      ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
      ESP_ERROR_CHECK(esp_wifi_connect() );
   }
   else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
   {
      xEventGroupSetBits(doneEventGrp, DONE_BIT);
   }
}


static void disconnectEvent(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data)
{
   if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
   {
      ESP_LOGI(TAG, "WiFi disconnected, trying to reconnect...");
      ESP_ERROR_CHECK(esp_wifi_connect());
   }
}


static int getWifiCfg(wifi_config_t* wcfg)
{
   size_t size;
   bzero(wcfg, sizeof(wifi_config_t));
   size=sizeof(wcfg->sta.ssid);
   if(ESP_OK != nvs_get_str(nvsh,"ssid",(char*)wcfg->sta.ssid, &size))
      return -1;
   size=sizeof(wcfg->sta.password);
   if(ESP_OK != nvs_get_str(nvsh,"password",(char*)wcfg->sta.password,&size))
      return -1;
   size=sizeof(wcfg->sta.bssid);
   if(ESP_OK == nvs_get_blob(nvsh,"bssid",wcfg->sta.bssid, &size))
      wcfg->sta.bssid_set = true;
   return 0;
}

void smartConfig(void)
{
   TaskHandle_t ledTaskH;
   LED led = {
      .msBlink=800,
      .pattern={1,0,1,0}
   };
   ledPtr=&led;
   ESP_ERROR_CHECK(nvs_open("WiFi", NVS_READWRITE, &nvsh));
   esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
   assert(sta_netif); 
   wifi_init_config_t wicfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&wicfg) );
   ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
   ESP_ERROR_CHECK(esp_wifi_start() );
   doneEventGrp = xEventGroupCreate();
   xTaskCreate(ledTask,"ledTask",2048,NULL,3,&ledTaskH);
   wifi_config_t wcfg;
   uint8_t rcounter = 255; /* Reconnect counter */
   if(ESP_OK ==nvs_get_u8(nvsh, "rcounter", &rcounter) && rcounter > 0 && ! getWifiCfg(&wcfg))
   {
      ESP_LOGI(TAG, "Connecting to saved SSID: %s", wcfg.sta.ssid);
      ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wcfg) );
      ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &connectEvent, NULL) );
      ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connectEvent, NULL) );
      ESP_ERROR_CHECK(esp_wifi_connect());
      xEventGroupWaitBits(doneEventGrp, DONE_BIT, true, false, portMAX_DELAY);
      ESP_ERROR_CHECK(nvs_set_u8(nvsh, "rcounter", 5));
      esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, connectEvent);
      esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, connectEvent);
   }
   else
   {
      setLedPattern(400,1,0,1,0);
      ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &smartConfigEvent, NULL) );
      ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &smartConfigEvent, NULL) );
      ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &smartConfigEvent, NULL) );
      ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
      smartconfig_start_config_t scfg = SMARTCONFIG_START_CONFIG_DEFAULT();
      ESP_ERROR_CHECK(esp_smartconfig_start(&scfg) );
      EventBits_t uxBits=xEventGroupWaitBits(doneEventGrp,DONE_BIT,true,false,pdMS_TO_TICKS(2*60*1000));
      ESP_ERROR_CHECK(nvs_set_u8(nvsh, "rcounter", 5));
      if(uxBits & DONE_BIT)
      {
         ESP_LOGI(TAG, "Completed successfully");
      }
      else
      {
         ESP_LOGI(TAG, "Waiting for SmartConfig app timed out.");
         restart();
      }
      esp_smartconfig_stop();
      esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, smartConfigEvent);
      esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, smartConfigEvent);
      esp_event_handler_unregister(SC_EVENT, ESP_EVENT_ANY_ID, smartConfigEvent);
   }
   vTaskDelete(ledTaskH);
   gpio_set_level(BLINK_GPIO1, 0);
   gpio_set_level(BLINK_GPIO2, 0);
   vEventGroupDelete(doneEventGrp);
   ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &disconnectEvent, NULL) );
}

#endif /* SMARTCONFIG */
