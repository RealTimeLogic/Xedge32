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
#pragma once

#include <esp_wifi_types.h>
#include <hal/spi_types.h>
#include <barracuda.h>

/**< Network adapter string options. */
/**< "close" the network connection. */
/**< "wifi" use the internal ESP32 radio. */
/**< "IP101" use RMII IP101 of 10/100M Ethernet from IC Plus. */
/**< "RTL8201" use RMII RTL82xx of 10/100M Ethernet from Realtek. */
/**< "LAN87XX" use RMII LAN87xx of 10/100M Ethernet from Microchip. */
/**< "DP83848" use RMII DP83848, 10/100M Ethernet from TI. */
/**< "KSZ80XX" use RMII KSZ8051, 10/100M Ethernet from Microchip. */
/**< "W5500" use SPI PHY W5500, 10/100M Ethernet from Wiznet. */
/**< "DM9051" use SPI PHY DM9051, 10/100M Ethernet from Dacom. */

/**< SPI interface configuration. */
typedef struct {
   int32_t hostId;            /**< SPI host device ID. */
   int32_t clk;               /**< Clock pin. */
   int32_t mosi;              /**< MOSI (Master Output Slave Input) pin. */
   int32_t miso;              /**< MISO (Master Input Slave Output) pin. */
   int32_t cs;                /**< Chip select pin. */
   int32_t irq;               /**< Interrupt request pin. */
   int32_t freq;              /**< SPI frequency. */
} netSpi_t;

/**< Network configuration parameters. */
typedef struct 
{
   char adapter[32];          /**< Network adapter (wifi/eth) selection. */
                              /**< SSID (Service Set Identifier) of the target access point. */
   char ssid[sizeof(((wifi_sta_config_t*)0)->ssid)];          
                              /**< Password of the target access point. */
   char password[sizeof(((wifi_sta_config_t*)0)->password)];      
   netSpi_t spi;              /**< SPI configuration for ethernet. */
   int32_t phyRstPin;         /**< Pin to reset the PHY by default disabled (-1). */
   int32_t phyMdioPin;        /**< Mdio pin of the internal PHY. */
   int32_t phyMdcPin;         /**< Mdc pin of the internal PHY. */
} netConfig_t;

bool netInit(void); 
esp_err_t netConnect(netConfig_t* cfg);
esp_err_t netWifiConnect(char* ssid, char* password);
esp_err_t netEthConnect(void);

const char* wifiAuthMode(int authmode, int print);
const char* wifiCipherType(int pcipher, int gcipher, int print, const char** pciphers);
void wifiScan(int print, lua_State* L,
              void (*cb)(lua_State* L, const uint8_t* ssid, int rssi,
                        const char* authmode,const char*  pchiper,
                        const char* gcipher, int channel));

/**
 * @brief Enumeration for AP mode states.
 * 
 * This enumeration defines the possible states for the Access Point (AP) mode.
 */
typedef enum {
    AP_MODE_DISABLED,        /**< AP mode is disabled, by default. */
    AP_MODE_ENABLED,         /**< AP mode is enabled. */
    AP_MODE_USER_REQUESTED   /**< AP mode is explicitly requested by the user. */
} ap_mode_t;

void netWaitIP(void);   
int netGotIP(void);
esp_err_t netWifiApStart(bool regHandler);

int netIsAdapterSpi(char* adapter);
int netIsAdapterRmii(char* adapter);

void* netCheckAlloc(void* mem);
