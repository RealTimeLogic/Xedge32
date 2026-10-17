/** 
 * Configuration parameter module for xedge32.c 
 *
 * Copyright (c) Real Time Logic
 *
 * This software may only be used by the terms and conditions
 * stipulated in the corresponding license agreement under which the
 * software has been supplied. All use of this code is subject to the
 *  terms and conditions of the included License Agreement.
 */
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <nvs_flash.h>
#include <sdmmc_cmd.h>
#include <esp_log.h>
#include "CfgESP32.h"

static nvs_handle_t nvsh;

static const char TAG[]={"X"};

/**
 * @brief Initialize the configuration parameters module.
 *        This function initializes the NVS (Non-Volatile Storage) Flash 
 *        and opens the NVS namespace for reading and writing configuration parameters. 
 *         
 * @note This function should be called at the start of the application.
 * @return ESP_OK if the configuration module is successfully initialized.
 */
esp_err_t cfgInit(void)
{
   esp_err_t err = nvs_flash_init();
   if((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
   {
      ESP_LOGE(TAG, "NVS init failed! Erasing memory.");
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
   }

   ESP_ERROR_CHECK(err);
   
   ESP_ERROR_CHECK(nvs_open("xedge", NVS_READWRITE, &nvsh));
    
   return err;
}


/* An empty saved name disables the default hostname; missing means Xedge32. */
#if CONFIG_mDNS_ENABLED
esp_err_t mDnsGet(char namebuf[80])
{
   size_t size=80;
   return nvs_get_str(nvsh,"mdns",namebuf,&size);
}

esp_err_t mDnsSet(const char* name)
{
   esp_err_t err = nvs_set_str(nvsh,"mdns",name);
   return err == ESP_OK ? nvs_commit(nvsh) : err;
}
#endif

/**
 * @brief Save the SD card parameters to the configuration storage.
 *
 * @param cfg Pointer to the SD card slot configuration structure.
 * @return ESP_OK after commit, or the first NVS error. Earlier writes are
 *         not rolled back on failure.
 */
esp_err_t cfgSetSdCard(sdmmc_slot_config_t* cfg)
{
   esp_err_t err;
   if((err = nvs_set_i32(nvsh, "SdWidth", (int32_t)cfg->width)) != ESP_OK)
      return err;
   
#ifdef SOC_SDMMC_USE_GPIO_MATRIX 
   if((err = nvs_set_i32(nvsh, "SdClk", (int32_t)cfg->clk)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "SdCmd", (int32_t)cfg->cmd)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "SdD0", (int32_t)cfg->d0)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "SdD1", (int32_t)cfg->d1)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "SdD2", (int32_t)cfg->d2)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "SdD3", (int32_t)cfg->d3)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "SdD4", (int32_t)cfg->d4)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "SdD5", (int32_t)cfg->d5)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "SdD6", (int32_t)cfg->d6)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "SdD7", (int32_t)cfg->d7)) != ESP_OK)
      return err;
#endif   

   return nvs_commit(nvsh);
}

/**
 * @brief Read the SD card parameters of the configuration storage.
 *
 * @param cfg Pointer to the SD card slot configuration structure.
 * @return ESP_OK if the SD card parameters are successfully read.
 */
esp_err_t cfgGetSdCard(sdmmc_slot_config_t* cfg)
{
int32_t width;

   if(ESP_OK == nvs_get_i32(nvsh, "SdWidth", &width))
   {
      cfg->width = width; 
#ifdef SOC_SDMMC_USE_GPIO_MATRIX 
      if(width >= 1)
      {
         int32_t clk;
         int32_t cmd;
         int32_t d0;

         if((ESP_OK == nvs_get_i32(nvsh, "SdClk", &clk)) &&
            (ESP_OK == nvs_get_i32(nvsh, "SdCmd", &cmd)) &&
            (ESP_OK == nvs_get_i32(nvsh, "SdD0", &d0)))
         {
            cfg->clk = clk;
            cfg->cmd = cmd;
            cfg->d0 = d0;
         }
         else
         {
            return ESP_FAIL;
         }
      }
      
      if(width >= 4)
      {
         int32_t d1;
         int32_t d2;
         int32_t d3;

         if((ESP_OK == nvs_get_i32(nvsh, "SdD1", &d1)) &&
            (ESP_OK == nvs_get_i32(nvsh, "SdD2", &d2)) &&
            (ESP_OK == nvs_get_i32(nvsh, "SdD3", &d3)))
         {
            cfg->d1 = d1;
            cfg->d2 = d2;
            cfg->d3 = d3;
         }
         else
         {
            return ESP_FAIL;
         }
      }
      
      if(width == 8)
      {
         int32_t d4;
         int32_t d5;
         int32_t d6;
         int32_t d7;
         
         if((ESP_OK == nvs_get_i32(nvsh, "SdD4", &d4)) &&
            (ESP_OK == nvs_get_i32(nvsh, "SdD5", &d5)) &&
            (ESP_OK == nvs_get_i32(nvsh, "SdD6", &d6)) &&
            (ESP_OK == nvs_get_i32(nvsh, "SdD7", &d7)))
         {
            cfg->d4 = d4;
            cfg->d5 = d5;
            cfg->d6 = d6;
            cfg->d7 = d7;
         }
         else
         {
            return ESP_FAIL;
         }
      }
#endif // SOC_SDMMC_USE_GPIO_MATRIX
      
      return ESP_OK;
   }

   return ESP_FAIL;
}

/* Erase the count keys in the shared namespace and commit. Missing keys are
   already erased. Returns the first NVS error; prior erases are not rolled back. */
static esp_err_t cfgEraseKeys(const char* const* keys, size_t count)
{
   for(size_t i = 0; i < count; i++)
   {
      esp_err_t err = nvs_erase_key(nvsh, keys[i]);
      if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
         return err;
   }
   return nvs_commit(nvsh);
}

/* Erase saved SD parameters. No input; returns ESP_OK after commit or the
   first NVS error. Unset optional pins and an already-erased config are valid. */
esp_err_t cfgEraseSdCard(void)
{
   static const char* const keys[] = {
      "SdWidth",
#ifdef SOC_SDMMC_USE_GPIO_MATRIX
      "SdClk", "SdCmd", "SdD0", "SdD1", "SdD2", "SdD3",
      "SdD4", "SdD5", "SdD6", "SdD7",
#endif
   };
   return cfgEraseKeys(keys, sizeof(keys) / sizeof(keys[0]));
}

/**
 * @brief Read the Network parameters to the configuration storage.
 *
 * @param cfg Pointer to the network configuration structure.
 * @return ESP_OK if the network parameters are successfully read.
 */
esp_err_t cfgGetNet(netConfig_t* cfg)
{
   memset(cfg, 0, sizeof(*cfg));
   strcpy(cfg->adapter, "close");
   cfg->spi.hostId = 2;
   cfg->spi.clk = -1;
   cfg->spi.mosi = -1;
   cfg->spi.miso = -1;
   cfg->spi.cs = -1;
   cfg->spi.irq = -1;
   cfg->spi.freq = 40000000;
   cfg->phyRstPin = -1;
   cfg->phyMdioPin = -1;
   cfg->phyMdcPin = -1;

   size_t size = sizeof(cfg->adapter);
   nvs_get_str(nvsh, "netAdapter", cfg->adapter, &size);
   
   size = sizeof(cfg->ssid);
   nvs_get_str(nvsh, "netSsid", cfg->ssid, &size);
   
   size = sizeof(cfg->password);
   nvs_get_str(nvsh, "netPassword", cfg->password, &size);
   
   nvs_get_i32(nvsh, "netSpiHostId", &cfg->spi.hostId);
   nvs_get_i32(nvsh, "netSpiClk", &cfg->spi.clk);
   nvs_get_i32(nvsh, "netSpiMosi", &cfg->spi.mosi);
   nvs_get_i32(nvsh, "netSpiMiso", &cfg->spi.miso);
   nvs_get_i32(nvsh, "netSpiCs", &cfg->spi.cs);
   nvs_get_i32(nvsh, "netSpiIrq", &cfg->spi.irq);
   nvs_get_i32(nvsh, "netSpiFreq", &cfg->spi.freq);
   
   nvs_get_i32(nvsh, "netPhyRst", &cfg->phyRstPin);
   nvs_get_i32(nvsh, "netPhyMdio", &cfg->phyMdioPin);
   nvs_get_i32(nvsh, "netPhyMdc", &cfg->phyMdcPin);
   
   return ESP_OK;
}

/**
 * @brief Save the Network parameters to the configuration storage.
 *
 * @param cfg Pointer to the network configuration structure.
 * @return ESP_OK after commit, or the first NVS error. Earlier writes are
 *         not rolled back on failure.
 */
esp_err_t cfgSetNet(netConfig_t* cfg)
{
   esp_err_t err;
   if((err = nvs_set_str(nvsh, "netAdapter", cfg->adapter)) != ESP_OK)
      return err;
   if((err = nvs_set_str(nvsh, "netSsid", cfg->ssid)) != ESP_OK)
      return err;
   if((err = nvs_set_str(nvsh, "netPassword", cfg->password)) != ESP_OK)
      return err;
   
   if((err = nvs_set_i32(nvsh, "netSpiHostId", cfg->spi.hostId)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "netSpiClk", cfg->spi.clk)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "netSpiMosi", cfg->spi.mosi)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "netSpiMiso", cfg->spi.miso)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "netSpiCs", cfg->spi.cs)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "netSpiIrq", cfg->spi.irq)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "netSpiFreq", cfg->spi.freq)) != ESP_OK)
      return err;
   
   if((err = nvs_set_i32(nvsh, "netPhyRst", cfg->phyRstPin)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "netPhyMdio", cfg->phyMdioPin)) != ESP_OK)
      return err;
   if((err = nvs_set_i32(nvsh, "netPhyMdc", cfg->phyMdcPin)) != ESP_OK)
      return err;
   
   return nvs_commit(nvsh);
}

/* Erase saved network parameters. No input; returns ESP_OK after commit or
   the first NVS error. Missing keys count as already erased. */
esp_err_t cfgEraseNet(void)
{
   static const char* const keys[] = {
      "netAdapter", "netSsid", "netPassword", "netSpiHostId", "netSpiClk",
      "netSpiMosi", "netSpiMiso", "netSpiCs", "netSpiIrq", "netSpiFreq",
      "netPhyRst", "netPhyMdio", "netPhyMdc"
   };
   return cfgEraseKeys(keys, sizeof(keys) / sizeof(keys[0]));
}
  

