/* Console init

   This example code is in the Public Domain (or CC0 licensed, at your option.)
*/

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "linenoise/linenoise.h"
#include "esp_vfs_usb_serial_jtag.h"

void manageConsole(bool start)
{
   static TaskHandle_t mainTaskHandle;
   if(false == start)
   {
      /* Called by Lua binding esp32.execute"killmain" */
      vTaskDelete(mainTaskHandle);
      printf("deinit %d\n",esp_console_deinit());
      return;
   }
   /* called by main() */
   mainTaskHandle = xTaskGetCurrentTaskHandle();

   /* Drain stdout before reconfiguring it */
   fflush(stdout);
   fsync(fileno(stdout));
   /* Disable buffering on stdin */
   setvbuf(stdin, NULL, _IONBF, 0);

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
   /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
   esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
   /* Move the caret to the beginning of the next line on '\n' */
   esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
   /* Configure UART. Note that REF_TICK is used so that the baud rate remains
    * correct while APB frequency is changing in light sleep mode.
    */
   const uart_config_t uart_config = {
      .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
#if SOC_UART_SUPPORT_REF_TICK
      .source_clk = UART_SCLK_REF_TICK,
#elif SOC_UART_SUPPORT_XTAL_CLK
      .source_clk = UART_SCLK_XTAL,
#endif
   };
   /* Install UART driver for interrupt-driven reads and writes */
   ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                        256, 0, 0, NULL, 0) );
   ESP_ERROR_CHECK( uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config) );
   /* Tell VFS to use UART driver */
   esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
   /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
   esp_vfs_dev_usb_serial_jtag_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
   /* Move the caret to the beginning of the next line on '\n' */
   esp_vfs_dev_usb_serial_jtag_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

   usb_serial_jtag_driver_config_t usb_serial_jtag_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();

   /* Install USB-SERIAL-JTAG driver for interrupt-driven reads and writes */
   ESP_ERROR_CHECK( usb_serial_jtag_driver_install(&usb_serial_jtag_config) );
   
   /* Tell vfs to use usb-serial-jtag driver */
   esp_vfs_usb_serial_jtag_use_driver();
#else
#error Unsupported console type
#endif
  
   /* Initialize the console */
   esp_console_config_t console_config = {
      .max_cmdline_args = 8,
      .max_cmdline_length = 256
   };
   ESP_ERROR_CHECK( esp_console_init(&console_config) );
   
   /* Configure linenoise line completion library */
   /* Enable multiline editing. If not set, long commands will scroll within
    * single line.
    */
   linenoiseSetMultiLine(1);
   /* Set command history size */
   linenoiseHistorySetMaxLen(10);
   /* Set command maximum length */
   linenoiseSetMaxLineLen(console_config.max_cmdline_length); //console_config.max_cmdline_length);
   /* Don't return empty lines */
   linenoiseAllowEmpty(false);
}
