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
#pragma once

#include <driver/sdmmc_host.h>
#include "NetESP32.h"

esp_err_t cfgInit(void);
esp_err_t cfgSetSdCard(sdmmc_slot_config_t* cfg);
esp_err_t cfgGetSdCard(sdmmc_slot_config_t* cfg);
esp_err_t cfgEraseSdCard(void);

esp_err_t cfgGetNet(netConfig_t* cfg);
esp_err_t cfgSetNet(netConfig_t* cfg);
esp_err_t cfgEraseNet(void);
