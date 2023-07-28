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

// TODO: Found the value for no init.
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

/**
 * @brief Save the SD card parameters to the configuration storage.
 *
 * @param cfg Pointer to the SD card slot configuration structure.
 * @return ESP_OK if the SD card parameters are successfully saved.
 */
esp_err_t cfgSetSdCard(sdmmc_slot_config_t* cfg)
{
   nvs_set_i32(nvsh, "SdWidth", (int32_t)cfg->width);
   
#ifdef SOC_SDMMC_USE_GPIO_MATRIX 
   nvs_set_i32(nvsh, "SdClk", (int32_t)cfg->clk);
   nvs_set_i32(nvsh, "SdCmd", (int32_t)cfg->cmd);
   nvs_set_i32(nvsh, "SdD0", (int32_t)cfg->d0);
   nvs_set_i32(nvsh, "SdD1", (int32_t)cfg->d1);
   nvs_set_i32(nvsh, "SdD2", (int32_t)cfg->d2);
   nvs_set_i32(nvsh, "SdD3", (int32_t)cfg->d3);
   nvs_set_i32(nvsh, "SdD4", (int32_t)cfg->d4);
   nvs_set_i32(nvsh, "SdD5", (int32_t)cfg->d5);
   nvs_set_i32(nvsh, "SdD6", (int32_t)cfg->d6);
   nvs_set_i32(nvsh, "SdD7", (int32_t)cfg->d7);
#endif   

   return ESP_OK;
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
            cfg->d6 = d7;
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

/**
 * @brief Erase all the SD card parameters from the configuration storage.
 *
 * @param cfg Pointer to the SD card slot configuration structure.
 * @return ESP_OK if the SD card parameters are successfully saved.
 */
esp_err_t cfgEraseSdCard(void)
{
   esp_err_t errWidth = nvs_erase_key(nvsh, "SdWidth");
#ifdef SOC_SDMMC_USE_GPIO_MATRIX
   esp_err_t errClk = nvs_erase_key(nvsh, "SdClk");
   esp_err_t errCmd = nvs_erase_key(nvsh, "SdCmd");
   esp_err_t errD0 = nvs_erase_key(nvsh, "SdD0");
   esp_err_t errD1 = nvs_erase_key(nvsh, "SdD1");
   esp_err_t errD2 = nvs_erase_key(nvsh, "SdD2"); 
   esp_err_t errD3 = nvs_erase_key(nvsh, "SdD3");
   esp_err_t errD4 = nvs_erase_key(nvsh, "SdD4");
   esp_err_t errD5 = nvs_erase_key(nvsh, "SdD5");
   esp_err_t errD6 = nvs_erase_key(nvsh, "SdD6"); 
   esp_err_t errD7 = nvs_erase_key(nvsh, "SdD7");
#endif
      
   if(ESP_OK == errWidth) 
   {
#ifdef SOC_SDMMC_USE_GPIO_MATRIX
      if((ESP_OK == errClk) && (ESP_OK == errCmd) &&
         (ESP_OK == errD0) && (ESP_OK == errD1) &&
         (ESP_OK == errD2) && (ESP_OK == errD3) &&
         (ESP_OK == errD4) && (ESP_OK == errD5) &&
         (ESP_OK == errD6) && (ESP_OK == errD7))
      {   
         return ESP_OK;
      }
#else
     return ESP_OK; 
#endif
   }
   
   return ESP_FAIL;
}

/**
 * @brief Read the Network parameters to the configuration storage.
 *
 * @param cfg Pointer to the network configuration structure.
 * @return ESP_OK if the network parameters are successfully read.
 */
esp_err_t cfgGetNet(netConfig_t* cfg)
{
   size_t size = sizeof(cfg->adapter);
   if(nvs_get_str(nvsh, "netAdapter", cfg->adapter, &size) != ESP_OK)
   {
      strcpy(cfg->adapter, "close");     
   }
   
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
 * @return ESP_OK if the network parameters are successfully saved.
 */
esp_err_t cfgSetNet(netConfig_t* cfg)
{
   nvs_set_str(nvsh, "netAdapter", cfg->adapter);
   nvs_set_str(nvsh, "netSsid", cfg->ssid);
   nvs_set_str(nvsh, "netPassword", cfg->password);
   
   nvs_set_i32(nvsh, "netSpiHostId", cfg->spi.hostId);
   nvs_set_i32(nvsh, "netSpiClk", cfg->spi.clk);
   nvs_set_i32(nvsh, "netSpiMosi", cfg->spi.mosi);
   nvs_set_i32(nvsh, "netSpiMiso", cfg->spi.miso);
   nvs_set_i32(nvsh, "netSpiCs", cfg->spi.cs);
   nvs_set_i32(nvsh, "netSpiIrq", cfg->spi.irq);
   nvs_set_i32(nvsh, "netSpiFreq", cfg->spi.freq);
   
   nvs_set_i32(nvsh, "netPhyRst", cfg->phyRstPin);
   nvs_set_i32(nvsh, "netPhyMdio", cfg->phyMdioPin);
   nvs_set_i32(nvsh, "netPhyMdc", cfg->phyMdcPin);
   
   return ESP_OK;
}

/**
 * @brief Erase all the Network parameters from the configuration storage.
 *
 * @param cfg Pointer to the network configuration structure.
 * @return ESP_OK if the network parameters are successfully erased.
 */
esp_err_t cfgEraseNet(void)
{
   nvs_erase_key(nvsh, "netAdapter");
   nvs_erase_key(nvsh, "netSsid");
   nvs_erase_key(nvsh, "netPassword");
   nvs_erase_key(nvsh, "netSpiHostId");
   nvs_erase_key(nvsh, "netSpiClk");
   nvs_erase_key(nvsh, "netSpiMosi");
   nvs_erase_key(nvsh, "netSpiMiso");
   nvs_erase_key(nvsh, "netSpiCs");
   nvs_erase_key(nvsh, "netSpiIrq");
   nvs_erase_key(nvsh, "netSpiFreq");
   
   nvs_erase_key(nvsh, "netPhyRst");
   nvs_erase_key(nvsh, "netPhyMdio");
   nvs_erase_key(nvsh, "netPhyMdc");
   
   return ESP_OK;
}
  

