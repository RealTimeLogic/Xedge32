/* Console init

   This example code is in the Public Domain (or CC0 licensed, at your option.)
*/

#include <stdio.h>
#include <fcntl.h>
#include <sys/cdefs.h>
#include <string.h>
#include "esp_system.h"
#include "esp_vfs_dev.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "linenoise/linenoise.h"
#include "esp_console.h"

void manageConsole(bool start)
{
   static TaskHandle_t mainTaskHandle;
   if(false == start)
   {
      /* Called by Lua binding esp32.execute"killmain" */
      vTaskDelete(mainTaskHandle);
      printf("deinit\n"); 
      return;
   }
   /* called by main() */
   mainTaskHandle = xTaskGetCurrentTaskHandle();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
   esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
   /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
   uart_vfs_dev_port_set_rx_line_endings(hw_config.channel, ESP_LINE_ENDINGS_CR);
   /* Move the caret to the beginning of the next line on '\n' */
   uart_vfs_dev_port_set_tx_line_endings(hw_config.channel, ESP_LINE_ENDINGS_CRLF);
   /* Configure UART. Note that REF_TICK is used so that the baud rate remains
    * correct while APB frequency is changing in light sleep mode.
    */
   const uart_config_t uart_config = {
      .baud_rate = hw_config.baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
#if SOC_UART_SUPPORT_REF_TICK
      .source_clk = UART_SCLK_REF_TICK,
#elif SOC_UART_SUPPORT_XTAL_CLK
      .source_clk = UART_SCLK_XTAL,
#endif
   };
   /* Drain stdout before reconfiguring it */
   fflush(stdout);
   fsync(fileno(stdout));

   /* Install UART driver for interrupt-driven reads and writes */
   ESP_ERROR_CHECK( uart_driver_install(hw_config.channel, 256, 0, 0, NULL, 0) );
   ESP_ERROR_CHECK(uart_param_config(hw_config.channel, &uart_config));
   ESP_ERROR_CHECK(uart_set_pin(hw_config.channel, hw_config.tx_gpio_num, hw_config.rx_gpio_num, -1, -1));
   
   /* Tell VFS to use UART driver */
   uart_vfs_dev_use_driver(hw_config.channel);
   
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
   /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
   usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
   
   /* Move the caret to the beginning of the next line on '\n' */
   usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

   /* Enable blocking mode on stdin and stdout */
   fcntl(fileno(stdout), F_SETFL, 0);
   fcntl(fileno(stdin), F_SETFL, 0);

   usb_serial_jtag_driver_config_t usb_serial_jtag_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();

   /* Install USB-SERIAL-JTAG driver for interrupt-driven reads and writes */
   ESP_ERROR_CHECK( usb_serial_jtag_driver_install(&usb_serial_jtag_config) );
   
   /* Tell vfs to use usb-serial-jtag driver */
   usb_serial_jtag_vfs_use_driver();
#else
   #error Unsupported console type
#endif
   
   /* Configure linenoise line completion library */
   /* Enable multiline editing. If not set, long commands will scroll within
    * single line.
    */
   linenoiseSetMultiLine(1);
   /* Set command history size */
   linenoiseHistorySetMaxLen(10);
   /* Set command maximum length */
   linenoiseSetMaxLineLen(256); 
   /* Don't return empty lines */
   linenoiseAllowEmpty(false);
   
   /* Disable buffering on stdin of the current task.
    * If the console is ran on a different UART than the default one,
    * buffering shall only be disabled for the current one. */
   setvbuf(stdin, NULL, _IONBF, 0);    
}
