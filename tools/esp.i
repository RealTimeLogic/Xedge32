
/*

Navigate to ~/esp32 and run:

swig -DCONFIG_IDF_TARGET_ESP32=1 -Iesp-idf/components/esp32/ -Iesp-idf/components/esp_common/include -Iesp-idf/components/soc/include -Iesp-idf/components/soc/esp32/include -Iesp-idf/components/driver/include -Iesp-idf/components/esp_rom/include -lua esp.i

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
#include "swig-esp.h"
%}

#define CONFIG_IDF_TARGET_ESP32 1
//#define SOC_MCPWM_PERIPH_NUM MCPWM_UNIT_MAX
#define _Static_assert(x,m)
#define SOC_MCPWM_SUPPORTED

%ignore gpio_init;
%ignore gpio_intr_ack;
%ignore gpio_intr_ack_high;
%ignore gpio_intr_handler_register;
%ignore gpio_intr_pending;
%ignore gpio_intr_pending_high;
%ignore gpio_pin_wakeup_disable;
%ignore gpio_pin_wakeup_enable;

%include "stdint.i"
%include "swig-esp.h"
%include "driver/gpio.h"
%include "hal/gpio_types.h"
%include "esp32/rom/gpio.h"
%include "driver/adc.h"
%include "driver/dac.h"
%include "driver/mcpwm.h"

