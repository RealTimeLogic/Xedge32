/*
 * Copyright (c) Real Time Logic
 * This software may only be used by the terms and conditions stipulated
 * in the corresponding license agreement under which the software has
 * been supplied. All use of this code is subject to the terms and
 * conditions of the included License Agreement.
 *
 * This Lua binding interfaces with the ESP32 camera module, utilizing
 * the ESP-IDF camera driver. It offers functions for both
 * initializing the camera and capturing images. The 'lcam' function
 * takes a Lua table containing configuration options and returns a
 * camera object on success. This camera object is used by the
 * 'LCAM_lread' function. The 'LCAM_lread' function captures an image
 * with the initialized camera object and returns a Lua string with
 * the image data. Please note that this binding assumes the ESP-IDF
 * camera driver has already been configured and installed.
 */

#include "BaESP32.h"
#if CONFIG_CAM_ENABLED

#include "esp_camera.h"

#define BACAM "CAM"

typedef struct {
   sensor_t* sensor;
   int pins[16];
} LCAM;


static LCAM* LCAM_getUD(lua_State* L)
{
   return (LCAM*)luaL_checkudata(L, 1, BACAM);
}

static LCAM* LCAM_checkUD(lua_State* L)
{
   LCAM* o = LCAM_getUD(L);
   if (o->sensor == NULL)
   {
      luaL_error(L, "Camera not initialized");
   }
   return o;
}



static int LCAM_close(lua_State* L)
{
   LCAM* o = LCAM_getUD(L);
   if(o->sensor)
   {
      for(int i = 0 ; i < sizeof(o->pins)/sizeof(o->pins[0]) ; i++)
      {
         if(o->pins[i] >= 0)
         {
            activeGPOI[o->pins[i]] = NULL; // Release
         }
      }
      // The sensor field at NULL indicates that the camera could not
      // be initialized.
      o->sensor = NULL;
      // Free camera configuration memory.
      // Deinit camera.
      esp_err_t err = esp_camera_deinit();
      if (err != ESP_OK)
      {
         return pushEspRetVal(L, err, "Deinit camera failed", FALSE);
      }
      return 1;
   }
   return 0;
}


/*
 * Lua binding for close the ESP32 camera module.
 * Returns true on success and nil,err on failure.
 * Raises an error if the camera has not been initialized or has been closed.
 */
static int LCAM_lclose(lua_State* L)
{
   LCAM_checkUD(L);
   LCAM_close(L);
   return 0;
}

/*
 * Lua binding for capturing an image from ESP32 camera module.
 * Returns the binary cam data as a Lua string.
 * Raises an error if the camera has not been initialized.
 * returns nil, err if capture failed.
 */
static int LCAM_lread(lua_State* L)
{
   LCAM_checkUD(L);
   /* We capture the image with global mutex released since this
    * function may take a few milliseconds.
    */
   ThreadMutex_release(soDispMutex);
   camera_fb_t *fb = esp_camera_fb_get();
   ThreadMutex_set(soDispMutex);
   if(!fb)
   {
      return pushEspRetVal(L, ESP_FAIL, "Camera Capture Failed", FALSE);
   }
   luaL_Buffer lb;
   luaL_buffinit(L, &lb);
   uint8_t* buf = (uint8_t*)luaL_prepbuffsize(&lb, fb->len);
   memcpy(buf, fb->buf, fb->len);
   luaL_pushresultsize(&lb, fb->len);
   esp_camera_fb_return(fb);
   return 1;
}

    
static const luaL_Reg camObjLib[] = {
   {"read", LCAM_lread},
   {"close", LCAM_lclose},
   {"__close", LCAM_close},
   {"__gc", LCAM_close},
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
 * Returns the camera object, or nil,err on error.
 */
int lcam(lua_State* L)
{
   int pixel_format;
   int frame_size;
   camera_config_t cfg;
   
   lInitConfigTable(L, 1);
   lua_Integer d0 = balua_checkIntField(L, 1, "d0");
   lua_Integer d1 = balua_checkIntField(L, 1, "d1");
   lua_Integer d2 = balua_checkIntField(L, 1, "d2");
   lua_Integer d3 = balua_checkIntField(L, 1, "d3");
   lua_Integer d4 = balua_checkIntField(L, 1, "d4");
   lua_Integer d5 = balua_checkIntField(L, 1, "d5");
   lua_Integer d6 = balua_checkIntField(L, 1, "d6");
   lua_Integer d7 = balua_checkIntField(L, 1, "d7");
   lua_Integer xclk = balua_checkIntField(L, 1, "xclk");
   lua_Integer pclk = balua_checkIntField(L, 1, "pclk");
   lua_Integer vsync = balua_checkIntField(L, 1, "vsync");
   lua_Integer href = balua_checkIntField(L, 1, "href");
   lua_Integer sda = balua_checkIntField(L, 1, "sda");
   lua_Integer scl = balua_checkIntField(L, 1, "scl");
   lua_Integer reset = balua_checkIntField(L, 1, "reset");
   lua_Integer pwdn = balua_getIntField(L, 1, "pwdn", -1);
   lua_Integer freq = balua_checkIntField(L, 1, "freq");
   const char* format = balua_getStringField(L, 1, "format", "JPEG");
   const char* frame = balua_getStringField(L, 1, "frame", "QVGA");
   int vflip = balua_getBoolField(L, 1, "vflip", FALSE);
   int hmirror = balua_getBoolField(L, 1, "hmirror", FALSE);

   lua_Integer pins[] = {
      pwdn,reset,xclk,sda,scl,d7,d6,d5,d4,d3,d2,d1,d0,vsync,href,pclk
   };
   for(int i = 0 ; i < sizeof(pins)/sizeof(pins[0]) ; i++)
   {
      if(pins[i] >= 0)
      {
         if(pins[i] >= GPIO_NUM_MAX || activeGPOI[pins[i]])
            luaL_error(L,"Pin %d in use", pins[i]);
      }
   }

   // Check pixel format
   if (strcmp(format, "JPEG") == 0)
   {
      pixel_format = PIXFORMAT_JPEG;
   } else if (strcmp(format, "YUV422") == 0)
   {
      pixel_format = PIXFORMAT_YUV422;
   } else if (strcmp(format, "RGB565") == 0)
   {
      pixel_format = PIXFORMAT_RGB565;
   } else if (strcmp(format, "GRAYSCALE") == 0)
   {
      pixel_format = PIXFORMAT_GRAYSCALE;
   } else if (strcmp(format, "RGB888") == 0)
   {
      pixel_format = PIXFORMAT_RGB888;
   } else if (strcmp(format, "RAW") == 0)
   {
      pixel_format = PIXFORMAT_RAW;
   } else if (strcmp(format, "RGB444") == 0)
   {
      pixel_format = PIXFORMAT_RGB444;
   } else if (strcmp(format, "RGB555") == 0)
   {
      pixel_format = PIXFORMAT_RGB555;
   } else {
      return luaL_error(L, "invalid pixel format '%s'", format);
   }
   
   // Check frame size format
   if (strcmp(frame, "96X96") == 0)
   {
      frame_size = FRAMESIZE_96X96;
   } else if (strcmp(frame, "QQVGA") == 0)
   {
      frame_size = FRAMESIZE_QQVGA;
   } else if (strcmp(frame, "QCIF") == 0)
   {
      frame_size = FRAMESIZE_QCIF;
   } else if (strcmp(frame, "HQVGA") == 0)
   {
      frame_size = FRAMESIZE_HQVGA;
   } else if (strcmp(frame, "240X240") == 0)
   {
      frame_size = FRAMESIZE_240X240;
   } else if (strcmp(frame, "QVGA") == 0)
   {
      frame_size = FRAMESIZE_QVGA;
   } else if (strcmp(frame, "CIF") == 0)
   {
      frame_size = FRAMESIZE_CIF;
   } else if (strcmp(frame, "HVGA") == 0)
   {
      frame_size = FRAMESIZE_HVGA;
   } else if (strcmp(frame, "VGA") == 0)
   {
      frame_size = FRAMESIZE_VGA;
   } else if (strcmp(frame, "SVGA") == 0)
   {
      frame_size = FRAMESIZE_SVGA;
   } else if (strcmp(frame, "XGA") == 0)
   {
      frame_size = FRAMESIZE_XGA;
   } else if (strcmp(frame, "HD") == 0)
   {
      frame_size = FRAMESIZE_HD;
   } else {
      return luaL_error(L, "invalid frame size '%s'", frame);
   } 

   if (esp_camera_sensor_get() != NULL) 
   {
      return luaL_error(L, "Camera already initialized");
   }
   
   // Create the CAM object and copy the sensor and configuration information to it. 
   // For now it is only used in the close and read functions, but in the future it 
   // may be necessary to send commands to the camera.
   LCAM* cam = (LCAM*)lNewUdata(L, sizeof(LCAM), BACAM, camObjLib);
   
   // Init camera config struct
   cfg.pin_pwdn = pwdn;                       
   cfg.pin_reset = reset;              
   cfg.pin_xclk = xclk;
   cfg.pin_sccb_sda = sda;
   cfg.pin_sccb_scl = scl;

   cfg.pin_d7 = d7;
   cfg.pin_d6 = d6;
   cfg.pin_d5 = d5;
   cfg.pin_d4 = d4;
   cfg.pin_d3 = d3;
   cfg.pin_d2 = d2;
   cfg.pin_d1 = d1;
   cfg.pin_d0 = d0;
   cfg.pin_vsync = vsync;
   cfg.pin_href = href;
   cfg.pin_pclk = pclk;

   cfg.pixel_format = pixel_format;
   cfg.frame_size = frame_size;
   
   // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
   cfg.xclk_freq_hz = freq;
   cfg.ledc_timer = LEDC_TIMER_0;
   cfg.ledc_channel = LEDC_CHANNEL_0;

   cfg.jpeg_quality = 12;   // 0-63 lower number means higher quality

#if CONFIG_FB_ONE
   cfg.fb_count = 1;   
#elif CONFIG_FB_TWO
   cfg.fb_count = 2;       // if more than one, i2s runs in continuous mode. Use only with JPEG
#endif

#if CONFIG_GRAB_LATEST
   cfg.grab_mode = CAMERA_GRAB_LATEST; 
#elif CONFIG_GRAB_WHEN_EMPTY
   cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
#endif
   
   cfg.fb_location = CAMERA_FB_IN_PSRAM;
   
   esp_err_t err = esp_camera_init(&cfg);
   if (err != ESP_OK)
   {
      return pushEspRetVal(L, ESP_FAIL, "Camera Init Failed", TRUE);
   }

   cam->sensor = esp_camera_sensor_get();
   cam->sensor->set_vflip(cam->sensor, vflip);
   cam->sensor->set_hmirror(cam->sensor, hmirror);

   for(int i = 0 ; i < sizeof(pins)/sizeof(pins[0]) ; i++)
   {
      /* We assign the activeGPOI a non NULL value to indicate it is
       * in use; the pointer is only used for checking that it is non
       * NULL and it is not used as a valid pointer value.
       */
      if(pins[i] >= 0)
      {
         activeGPOI[pins[i]] = (struct LGPIO*)1;
      }
   }
   baAssert(sizeof(pins)/sizeof(pins[0]) == sizeof(cam->pins)/sizeof(cam->pins[0]));
   for(int i = 0 ; i < sizeof(pins)/sizeof(pins[0]) ; i++)
   {
      cam->pins[i]=pins[i];
   }
   return 1;
}
#endif // CONFIG_CAM_ENABLED

