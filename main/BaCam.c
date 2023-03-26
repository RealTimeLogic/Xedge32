/*
 * Copyright (c) Real Time Logic
 * This software may only be used by the terms and conditions stipulated
 * in the corresponding license agreement under which the software has
 * been supplied. All use of this code is subject to the terms and
 * conditions of the included License Agreement.
 *
 * Lua binding for the ESP32 camera module using the ESP-IDF camera driver.
 * Provides functions for initializing the camera and capturing an image.
 * The lcam function accepts a Lua table with configuration options and returns a boolean indicating
 * whether the initialization was successful and a camera object, which is used by the LCAM_lread function.
 * The LCAM_lread function captures an image using the initialized camera object and returns a Lua string
 * containing the image data. If an error occurs, the function returns nil and an error message.
 * This binding also includes a function for closing the camera object.
 * Note that this binding assumes that the ESP-IDF camera driver is already configured and installed.
 */
#include "BaESP32.h"

#include "esp_camera.h"

#define BACAM "CAM"

typedef struct {
   camera_config_t* cfg;
   sensor_t* sensor;
} LCAM;


/**
 * Converts the pixel format selected in the KConfig menu to a string.
 * If no pixel format is defined, the function returns a dash ("-").
 */  
static const char* cfg_format_to_str(void) {
#if CONFIG_PIXFORMAT_RGB565
   return "RGB565"; 
#elif CONFIG_PIXFORMAT_YUV422
   return "YUV422";
#elif CONFIG_PIXFORMAT_GRAYSCALE
   return "GRAYSCALE";
#elif CONFIG_PIXFORMAT_JPEG
   return "JPEG";
#elif CONFIG_PIXFORMAT_RGB888
   return "RGB888";
#elif CONFIG_PIXFORMAT_RAW
   return "RAW";
#elif CONFIG_PIXFORMAT_RGB444
   return "RGB444";
#elif CONFIG_PIXFORMAT_RGB555
   return "RGB555";
#endif
   
   return "-";
}

/**
 * Converts the size frame selected in the KConfig menu to a string.
 * If no pixel size is defined, the function returns a dash ("-").
 */ 
static const char* cfg_frame_to_str(void) {
//QQVGA-QXGA Do not use sizes above QVGA when not JPEG
#if CONFIG_FRAMESIZE_96X96
   return "96X96";      
#elif CONFIG_FRAMESIZE_QQVGA
   return "QQVGA";
#elif CONFIG_FRAMESIZE_QCIF
   return "QCIF";
#elif CONFIG_FRAMESIZE_HQVGA
   return "HQVGA";
#elif CONFIG_FRAMESIZE_240X240
   return "240X240";
#elif CONFIG_FRAMESIZE_QVGA
   return "QVGA";
#elif CONFIG_FRAMESIZE_CIF
   return "CIF";
#elif CONFIG_FRAMESIZE_HVGA
   return "HVGA";
#elif CONFIG_FRAMESIZE_VGA
   return "VGA";
#elif CONFIG_FRAMESIZE_SVGA
   return "SVGA";
#elif CONFIG_FRAMESIZE_XGA
   return "XGA";
#elif CONFIG_FRAMESIZE_HD
   return "HD";
#endif

   return "-";
}

/**
 * Converts the vertical flip selected in the KConfig menu to a boolean.
 */
static int cfg_vflip_to_int(void) {
#if ENABLE_VERTICAL_FLIP
   return TRUE;
#endif

   return FALSE;
}

/**
 * Converts the vertical horizontal mirror in the KConfig menu to a boolean.
 */
static int cfg_hmirror_to_int(void) {
#if ENABLE_HORIZONTAL_MIRROR
   return TRUE;
#endif
   return FALSE;
}

static LCAM* LCAM_getUD(lua_State* L) {
   return (LCAM*)luaL_checkudata(L, 1, BACAM);
}

/*
 * Lua binding for close the ESP32 camera module.
 * Returns boolean indicating whether the close was successful:
 * Raises an error if the camera has not been initialized or if the close fails.
 */
static int LCAM_lclose(lua_State* L) {
   LCAM* o = LCAM_getUD(L);
   if (o->sensor == NULL) {
      return luaL_error(L, "camera not initialized");
   }
   
   // The sensor field at NULL indicates that the camera could not be initialized.
   o->sensor = NULL;
   
   // Free camera configuration memory.
   if (o->cfg) {
      free(o->cfg);
      o->cfg = NULL;
   }
   
   // Deinit camera
   esp_err_t err = esp_camera_deinit();
   if (err != ESP_OK) {
      return luaL_error(L, "failed to deinit camera (%d)", err);
   }
   
   return 1;
}

/*
 * Lua binding for capturing an image from ESP32 camera module.
 * Returns a table with the following fields:
 *   - frame: a Lua string containing the image data in selected format.
 * Raises an error if the camera has not been initialized or if the capture fails.
 */
static int LCAM_lread(lua_State* L) {
   LCAM* o = LCAM_getUD(L);
   if (o->sensor == NULL) {
      return luaL_error(L, "camera not initialized");
   }
   
   // Capture an image
   camera_fb_t *fb = esp_camera_fb_get();
   if (!fb) {
      return luaL_error(L, "failed to capture image");
   }

   // Create a buffer for the image, so the camera driver can reuse the memory
   size_t size = fb->len;
   uint8_t *buf = (uint8_t*)malloc(fb->len);
   
   if (!buf) {
      esp_camera_fb_return(fb);
      return luaL_error(L, "could not allocate memory for image buffer");
   }
   
   memcpy(buf, fb->buf, size);
   esp_camera_fb_return(fb);

   // Push output buffer on Lua stack
   lua_pushlstring(L, (const char*)buf, size);
   free(buf);
   
   return 1;
}
    
static const luaL_Reg camObjLib[] = {
   {"read", LCAM_lread},
   {"close", LCAM_lclose},
   {"__close", LCAM_lclose},
   {"__gc", LCAM_lclose},
   {NULL, NULL}
};

/*
 * Lua binding for initializing the ESP32 camera module.
 * Accepts a table with the following optional fields:
 *   - d0-d7: camera data pins, default 5-12.
 *   - xclk: the GPIO pin used for XCLK, default is 4.
 *   - pclk: the GPIO pin used for PCLK, default is 5.
 *   - vsync: the GPIO pin used for VSYNC, default is 18.
 *   - href: the GPIO pin used for HREF, default is 23.
 *   - sda: the GPIO pin used for SDA, default is 19.
 *   - scl: the GPIO pin used for SCL, default is 18.
 *   - reset: the GPIO pin used for RESET, default is -1 (no reset pin).
 *   - pwdn: the GPIO pin used for PWDN, default is -1 (no power down pin).
 *   - freq: the frequency of XCLK in Hz, default is 100,000,000.
 *   - format: the pixel format of the image captured (JPEG, RGB565 or YUV422), default is JPEG.
 *   - frame: the image frame size (QQVGA, QVGA, SVGA ...), default is QVGA.
 *   - vflip: vertical flip on camera output, default false.
 *   - hmirror: horizontal mirror on camera output, default false.
 * Returns the camera object, or nill.
 * Note: Use the idf to set the default parameters of the CAM object.
 */
int lcam(lua_State* L) {
int pixel_format;
int frame_size;

   lInitConfigTable(L, 1);
   lua_Integer d0 = balua_getIntField(L, 1, "d0", CONFIG_CAM_D0);
   lua_Integer d1 = balua_getIntField(L, 1, "d1", CONFIG_CAM_D1);
   lua_Integer d2 = balua_getIntField(L, 1, "d2", CONFIG_CAM_D2);
   lua_Integer d3 = balua_getIntField(L, 1, "d3", CONFIG_CAM_D3);
   lua_Integer d4 = balua_getIntField(L, 1, "d4", CONFIG_CAM_D4);
   lua_Integer d5 = balua_getIntField(L, 1, "d5", CONFIG_CAM_D5);
   lua_Integer d6 = balua_getIntField(L, 1, "d6", CONFIG_CAM_D6);
   lua_Integer d7 = balua_getIntField(L, 1, "d7", CONFIG_CAM_D7);
   lua_Integer xclk = balua_getIntField(L, 1, "xclk", CONFIG_CAM_XCLK);
   lua_Integer pclk = balua_getIntField(L, 1, "pclk", CONFIG_CAM_PCLK);
   lua_Integer vsync = balua_getIntField(L, 1, "vsync", CONFIG_CAM_VSYNC);
   lua_Integer href = balua_getIntField(L, 1, "href", CONFIG_CAM_HREF);
   lua_Integer sda = balua_getIntField(L, 1, "sda", CONFIG_CAM_SDA);
   lua_Integer scl = balua_getIntField(L, 1, "scl", CONFIG_CAM_SCL);
   lua_Integer reset = balua_getIntField(L, 1, "reset", CONFIG_CAM_RESET);
   lua_Integer pwdn = balua_getIntField(L, 1, "pwdn", -1);
   lua_Integer freq = balua_getIntField(L, 1, "freq", CONFIG_CAM_XCLK_FREQ);
   const char* format = balua_getStringField(L, 1, "format", cfg_format_to_str());
   const char* frame = balua_getStringField(L, 1, "frame", cfg_frame_to_str());
   int vflip = balua_getBoolField(L, 1, "vflip", cfg_vflip_to_int());
   int hmirror = balua_getBoolField(L, 1, "hmirror", cfg_hmirror_to_int());

   // Check pixel format
   if (strcmp(format, "JPEG") == 0) {
      pixel_format = PIXFORMAT_JPEG;
   } else if (strcmp(format, "YUV422") == 0) {
      pixel_format = PIXFORMAT_YUV422;
   } else if (strcmp(format, "RGB565") == 0) {
      pixel_format = PIXFORMAT_RGB565;
   } else if (strcmp(format, "GRAYSCALE") == 0) {
      pixel_format = PIXFORMAT_GRAYSCALE;
   } else if (strcmp(format, "RGB888") == 0) {
      pixel_format = PIXFORMAT_RGB888;
   } else if (strcmp(format, "RAW") == 0) {
      pixel_format = PIXFORMAT_RAW;
   } else if (strcmp(format, "RGB444") == 0) {
      pixel_format = PIXFORMAT_RGB444;
   } else if (strcmp(format, "RGB555") == 0) {
      pixel_format = PIXFORMAT_RGB555;
   } else {
      return luaL_error(L, "invalid pixel format '%s'", format);
   }
   
   // Check frame size format
   if (strcmp(frame, "96X96") == 0) {
      frame_size = FRAMESIZE_96X96;
   } else if (strcmp(frame, "QQVGA") == 0) {
      frame_size = FRAMESIZE_QQVGA;
   } else if (strcmp(frame, "QCIF") == 0) {
      frame_size = FRAMESIZE_QCIF;
   } else if (strcmp(frame, "HQVGA") == 0) {
      frame_size = FRAMESIZE_HQVGA;
   } else if (strcmp(frame, "240X240") == 0) {
      frame_size = FRAMESIZE_240X240;
   } else if (strcmp(frame, "QVGA") == 0) {
      frame_size = FRAMESIZE_QVGA;
   } else if (strcmp(frame, "CIF") == 0) {
      frame_size = FRAMESIZE_CIF;
   } else if (strcmp(frame, "HVGA") == 0) {
      frame_size = FRAMESIZE_HVGA;
   } else if (strcmp(frame, "VGA") == 0) {
      frame_size = FRAMESIZE_VGA;
   } else if (strcmp(frame, "SVGA") == 0) {
      frame_size = FRAMESIZE_SVGA;
   } else if (strcmp(frame, "XGA") == 0) {
      frame_size = FRAMESIZE_XGA;
   } else if (strcmp(frame, "HD") == 0) {
      frame_size = FRAMESIZE_HD;
   } else {
      return luaL_error(L, "invalid frame size '%s'", frame);
   } 
     
   // Since configuration information is required prior to creating the CAM object 
   // and cannot be local, it is created in dynamic memory (heap) to be copied to 
   // the object when the camera can be successfully initialized.
   camera_config_t *cfg = (camera_config_t*) malloc(sizeof(camera_config_t));
   
   if (!cfg) {
      return luaL_error(L, "could not allocate memory for configuration");
   }
          
   // Init camera config struct
   cfg->pin_pwdn = pwdn;                       
   cfg->pin_reset = reset;              
   cfg->pin_xclk = xclk;
   cfg->pin_sccb_sda = sda;
   cfg->pin_sccb_scl = scl;

   cfg->pin_d7 = d7;
   cfg->pin_d6 = d6;
   cfg->pin_d5 = d5;
   cfg->pin_d4 = d4;
   cfg->pin_d3 = d3;
   cfg->pin_d2 = d2;
   cfg->pin_d1 = d1;
   cfg->pin_d0 = d0;
   cfg->pin_vsync = vsync;
   cfg->pin_href = href;
   cfg->pin_pclk = pclk;

   cfg->pixel_format = pixel_format;
   cfg->frame_size = frame_size;
   
   // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
   cfg->xclk_freq_hz = freq;
   cfg->ledc_timer = LEDC_TIMER_0;
   cfg->ledc_channel = LEDC_CHANNEL_0;

   cfg->jpeg_quality = 12,   // 0-63 lower number means higher quality
   cfg->fb_count = 1;        // if more than one, i2s runs in continuous mode. Use only with JPEG
        

   // When the camera failed to initialize, it releases the configuration memory 
   // and throws an error on the LUA stack.
   esp_err_t err = esp_camera_init(cfg);
   if (err != ESP_OK) {
      free(cfg);
      return luaL_error(L, "failed to init camera (%d)", err);
   }

   // Create the CAM object and copy the sensor and configuration information to it. 
   // For now it is only used in the close and read functions, but in the future it 
   // may be necessary to send commands to the camera.
   LCAM* cam = (LCAM*)lNewUdata(L, sizeof(LCAM), BACAM, camObjLib);

   cam->cfg = cfg;
   cam->sensor = esp_camera_sensor_get();
   cam->sensor->set_vflip(cam->sensor, vflip);
   cam->sensor->set_hmirror(cam->sensor, hmirror);
   
   return 1;
}

