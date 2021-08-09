
/*

In the parent directory, run:
CPWD=$PWD
cd ..
swig -DCONFIG_IDF_TARGET_ESP32=1 -Iesp-idf/components/esp32/ -Iesp-idf/components/esp_common/include -Iesp-idf/components/hal/include -Iesp-idf/components/soc/include -Iesp-idf/components/soc/esp32/include -Iesp-idf/components/driver/include -Iesp-idf/components/esp_rom/include -Iesp-idf/components/driver/esp32/include -Iesp-idf/components/nvs_flash/include -I$CPWD/main -lua -o $CPWD/main/esp_wrap.c $CPWD/tools/esp.i

SWIG: http://www.swig.org/download.html

 */




%module esp


%{
#include "driver/gpio.h"
#include "hal/gpio_types.h"
#include "esp32/rom/gpio.h"
#include "driver/adc.h"
#include "driver/dac.h"
#include "driver/mcpwm.h"
#include "nvs_flash.h"
#include "swig-esp.h"
%}

#define CONFIG_IDF_TARGET_ESP32 1
//#define SOC_MCPWM_PERIPH_NUM MCPWM_UNIT_MAX
#define _Static_assert(x,m)
#define __attribute__(x)
#define SOC_MCPWM_SUPPORTED 1

%ignore gpio_init;
%ignore gpio_intr_ack;
%ignore gpio_intr_ack_high;
%ignore gpio_intr_handler_register;
%ignore gpio_intr_pending;
%ignore gpio_intr_pending_high;
%ignore gpio_pin_wakeup_disable;
%ignore gpio_pin_wakeup_enable;
%ignore nvs_flash_read_security_cfg;
%ignore nvs_flash_secure_init_partition;
%ignore nvs_flash_generate_keys;
%ignore nvs_flash_secure_init;

%include "stdint.i"
%include "swig-esp.h"
%include "driver/gpio.h"
%include "hal/gpio_types.h"
%include "esp32/rom/gpio.h"
%include "driver/adc.h"
%include "driver/dac.h"
%include "driver/mcpwm.h"
%include "hal/mcpwm_types.h"
%include "nvs_flash.h"
