/*
 * BaCamV4L2.c
 * Lua camera binding for Xedge32 using V4L2 (Exclusive for ESP32-P4)
 * Maintains API compatibility with the legacy DVP/esp32-camera driver.
 */

#include "BaESP32.h"

// If the camera is on and it's a P4
#if defined(CONFIG_CAM_ENABLED) && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include "esp_heap_caps.h"

#include <linux/videodev2.h>
#include "esp_video_init.h"
#include "esp_cam_sensor_xclk.h"
#include "driver/i2c_master.h"
#include "driver/ppa.h"
#include "driver/jpeg_encode.h"

#include "lua.h"
#include "lauxlib.h"

#define BACAM "CAMV4L2"
#define P4_CAM_BUFFERS 2 // Number of V4L2 buffers to mmap

typedef struct {
    int fd;
    uint8_t *buffers[P4_CAM_BUFFERS];
    uint32_t buf_lengths[P4_CAM_BUFFERS];
    uint32_t width;
    uint32_t height;
    
    // --- HW JPEG ENCODER STATE ---
    jpeg_encoder_handle_t encoder_handle;
    jpeg_encode_cfg_t pic_cfg;     
    uint8_t *jpeg_out_buf;       
    uint32_t jpeg_out_buf_size;  
    
    // --- PPA SCALER STATE ---
    ppa_client_handle_t ppa_srm_handle; 
    uint8_t *ppa_out_buf;               
    uint32_t ppa_out_buf_size;          
    uint32_t requested_w;               
    uint32_t requested_h;               
} LCAM;

static LCAM* LCAM_getUD(lua_State* L) {
    return (LCAM*)luaL_checkudata(L, 1, BACAM);
}

static LCAM* LCAM_checkUD(lua_State* L) {
    LCAM* o = LCAM_getUD(L);
    if (o->fd < 0) {
        luaL_error(L, "Camera not initialized or already closed");
    }
    return o;
}

// =========================================================================
// 0. HARDWARE INITIALIZATION (Clock & I2C Bus)
// =========================================================================

static i2c_master_bus_handle_t s_i2cbus_handle = NULL;
static esp_cam_sensor_xclk_handle_t s_xclk_handle = NULL;

/**
 * @brief Initializes the physical ESP32-P4 hardware for the camera (I2C, XCLK, VFS).
 */
static esp_err_t p4_camera_hardware_init(int scl_pin, int sda_pin, int xclk_pin) {
    esp_err_t err;

    i2c_master_bus_config_t bus_config = {
        .i2c_port = 0,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_config, &s_i2cbus_handle);
    if (err != ESP_OK) return err;

    esp_cam_sensor_xclk_config_t xclk_config = {
        .esp_clock_router_cfg = {
            .xclk_pin = xclk_pin,
            .xclk_freq_hz = 24000000, 
        }
    };
    err = esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &s_xclk_handle);
    if (err == ESP_OK) {
        err = esp_cam_sensor_xclk_start(s_xclk_handle, &xclk_config);
    }
    if (err != ESP_OK) return err;

    esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .init_sccb = false, 
            .i2c_handle = s_i2cbus_handle,
            .freq = 400000,
        },
        .reset_pin = -1, 
        .pwdn_pin = -1,  
    };
    
    esp_video_init_config_t cam_config = {
        .csi = &csi_config,
    };

    return esp_video_init(&cam_config);
}

/**
 * @brief Deinitializes the physical ESP32-P4 hardware, freeing pins and buses.
 */
static void p4_camera_hardware_deinit(void) {
    esp_video_deinit();
    if (s_xclk_handle) {
        esp_cam_sensor_xclk_stop(s_xclk_handle);
        esp_cam_sensor_xclk_free(s_xclk_handle);
        s_xclk_handle = NULL;
    }
    if (s_i2cbus_handle) {
        i2c_del_master_bus(s_i2cbus_handle);
        s_i2cbus_handle = NULL;
    }
}

// =========================================================================
// 1. HARDWARE COMPRESSION ENGINE (JPEG)
// =========================================================================

/**
 * @brief Initializes the hardware JPEG encoder with the target dimensions.
 */
static esp_err_t hw_jpeg_encoder_init(LCAM *cam, uint32_t pixel_format, uint8_t quality) {
    jpeg_encode_engine_cfg_t eng_cfg = {
        .timeout_ms = 1000,
    };
    
    esp_err_t ret = jpeg_new_encoder_engine(&eng_cfg, &cam->encoder_handle);
    if (ret != ESP_OK) return ret;

    cam->pic_cfg.width = cam->requested_w;
    cam->pic_cfg.height = cam->requested_h;
    cam->pic_cfg.image_quality = quality;

    uint32_t raw_size = 0;

    switch (pixel_format) {
        case V4L2_PIX_FMT_RGB565:
            cam->pic_cfg.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
            cam->pic_cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
            raw_size = cam->requested_w * cam->requested_h * 2;
            break;
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_YUYV:
            cam->pic_cfg.src_type = JPEG_ENCODE_IN_FORMAT_YUV422;
            cam->pic_cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
            raw_size = cam->requested_w * cam->requested_h * 2;
            break;
#if CONFIG_ESP32P4_REV_MIN_FULL >= 300
        case V4L2_PIX_FMT_YUV420:
            cam->pic_cfg.src_type = JPEG_ENCODE_IN_FORMAT_YUV420;
            cam->pic_cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV420;
            raw_size = cam->requested_w * cam->requested_h * 3 / 2;
            break;
#endif
        default:
            jpeg_del_encoder_engine(cam->encoder_handle);
            cam->encoder_handle = NULL;
            return ESP_ERR_NOT_SUPPORTED;
    }

    cam->jpeg_out_buf_size = raw_size * 3 / 4; 
    jpeg_encode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    
    cam->jpeg_out_buf = (uint8_t *)jpeg_alloc_encoder_mem(cam->jpeg_out_buf_size, &mem_cfg, (size_t*)&cam->jpeg_out_buf_size);
    if (!cam->jpeg_out_buf) {
        jpeg_del_encoder_engine(cam->encoder_handle);
        cam->encoder_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void hw_jpeg_encoder_deinit(LCAM *cam) {
    if (cam->encoder_handle) {
        jpeg_del_encoder_engine(cam->encoder_handle);
        cam->encoder_handle = NULL;
    }
    if (cam->jpeg_out_buf) {
        free(cam->jpeg_out_buf);
        cam->jpeg_out_buf = NULL;
    }
}

// =========================================================================
// 2. V4L2 & PPA HELPER FUNCTIONS
// =========================================================================

/**
 * @brief Auto-negotiates the best available physical resolution from the sensor.
 */
static esp_err_t setup_v4l2_resolution(int fd, uint32_t target_w, uint32_t target_h, uint32_t format, uint32_t *out_w, uint32_t *out_h) {
    uint32_t best_w = 0, best_h = 0;
    uint32_t fallbacks[][2] = {
        {1920, 1080}, {1280, 960}, {1280, 720}, 
        {1024, 768}, {800, 800}, {800, 640}, {640, 480}, {320, 240}
    };
    int num_fallbacks = sizeof(fallbacks) / sizeof(fallbacks[0]);

    struct v4l2_format clean_fmt = {0};
    clean_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    // 1. Try to find a resolution suitable for downscaling
    for (int i = num_fallbacks - 1; i >= 0; i--) {
        uint32_t try_w = fallbacks[i][0];
        uint32_t try_h = fallbacks[i][1];
        
        if (try_w >= target_w && try_h >= target_h) {
            clean_fmt.fmt.pix.width = try_w;
            clean_fmt.fmt.pix.height = try_h;
            clean_fmt.fmt.pix.pixelformat = format;
            
            if (ioctl(fd, VIDIOC_S_FMT, &clean_fmt) == 0) {
                best_w = clean_fmt.fmt.pix.width;
                best_h = clean_fmt.fmt.pix.height;
                break;
            }
        }
    }

    // 2. Panic Mode (Upscale)
    if (best_w == 0) {
        for (int i = 0; i < num_fallbacks; i++) {
            clean_fmt.fmt.pix.width = fallbacks[i][0];
            clean_fmt.fmt.pix.height = fallbacks[i][1];
            clean_fmt.fmt.pix.pixelformat = format;
            
            if (ioctl(fd, VIDIOC_S_FMT, &clean_fmt) == 0) {
                best_w = clean_fmt.fmt.pix.width;
                best_h = clean_fmt.fmt.pix.height;
                break; 
            }
        }
    }

    if (best_w == 0) return ESP_FAIL;

    *out_w = best_w;
    *out_h = best_h;
    return ESP_OK;
}

/**
 * @brief Requests and memory-maps V4L2 capture buffers.
 */
static esp_err_t setup_v4l2_buffers(LCAM *cam) {
    struct v4l2_requestbuffers req = {
        .count = P4_CAM_BUFFERS,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(cam->fd, VIDIOC_REQBUFS, &req) < 0) return ESP_FAIL;

    for (int i = 0; i < P4_CAM_BUFFERS; i++) {
        struct v4l2_buffer buf = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i };
        if (ioctl(cam->fd, VIDIOC_QUERYBUF, &buf) < 0) return ESP_FAIL;
        
        cam->buf_lengths[i] = buf.length;
        cam->buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam->fd, buf.m.offset);
        if (cam->buffers[i] == MAP_FAILED) return ESP_FAIL;
        
        ioctl(cam->fd, VIDIOC_QBUF, &buf);
    }
    return ESP_OK;
}

/**
 * @brief Configures the Pixel Processing Accelerator and allocates its DMA-aligned buffer.
 */
static esp_err_t setup_ppa_scaler(LCAM *cam) {
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    
    if (ppa_register_client(&ppa_srm_config, &cam->ppa_srm_handle) != ESP_OK) return ESP_FAIL;

    cam->ppa_out_buf_size = cam->requested_w * cam->requested_h * 2; 
    cam->ppa_out_buf = heap_caps_calloc(1, cam->ppa_out_buf_size, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    
    if (!cam->ppa_out_buf) {
        ppa_unregister_client(cam->ppa_srm_handle);
        cam->ppa_srm_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// =========================================================================
// 3. LUA BINDINGS
// =========================================================================

static int LCAM_lset_control(lua_State *L) {
    LCAM *cam =  LCAM_checkUD(L);
    const char *ctrl_name = luaL_checkstring(L, 2);
    int value = luaL_checkinteger(L, 3);

    struct v4l2_ext_controls ctrls = {0};
    struct v4l2_ext_control ctrl = {0};
    ctrls.ctrl_class = V4L2_CID_USER_CLASS;
    ctrls.count = 1;
    ctrls.controls = &ctrl;

    if (strcmp(ctrl_name, "vflip") == 0) ctrl.id = V4L2_CID_VFLIP;
    else if (strcmp(ctrl_name, "hflip") == 0) ctrl.id = V4L2_CID_HFLIP;
    else if (strcmp(ctrl_name, "brightness") == 0) ctrl.id = V4L2_CID_BRIGHTNESS;
    else if (strcmp(ctrl_name, "contrast") == 0) ctrl.id = V4L2_CID_CONTRAST;
    else if (strcmp(ctrl_name, "red_balance") == 0) ctrl.id = V4L2_CID_RED_BALANCE;
    else if (strcmp(ctrl_name, "blue_balance") == 0) ctrl.id = V4L2_CID_BLUE_BALANCE;
    else {
        return luaL_error(L, "Unsupported control: %s", ctrl_name);
    }

    ctrl.value = value;
    
    if (ioctl(cam->fd, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
        lua_pushboolean(L, 0); 
    } else {
        lua_pushboolean(L, 1);
    }
    return 1;
}

static int LCAM_lread(lua_State *L) {
    LCAM *cam =  LCAM_checkUD(L);

    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam->fd, VIDIOC_DQBUF, &buf) != 0) {
        lua_pushnil(L); return 1;
    }

    uint32_t final_jpeg_size = 0;

    if (buf.bytesused > 0) {
        ppa_srm_oper_config_t srm_config = {
            .in = {
                .buffer = cam->buffers[buf.index], 
                .pic_w = cam->width,
                .pic_h = cam->height,
                .block_w = cam->width,
                .block_h = cam->height,
                .block_offset_x = 0,
                .block_offset_y = 0,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .out = {
                .buffer = cam->ppa_out_buf,        
                .buffer_size = cam->ppa_out_buf_size,
                .pic_w = cam->requested_w,
                .pic_h = cam->requested_h,
                .block_offset_x = 0,
                .block_offset_y = 0,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = (float)cam->requested_w / (float)cam->width,
            .scale_y = (float)cam->requested_h / (float)cam->height,
            .rgb_swap = 0,
            .byte_swap = 0,
            .mode = PPA_TRANS_MODE_BLOCKING, 
        };

        if (ppa_do_scale_rotate_mirror(cam->ppa_srm_handle, &srm_config) == ESP_OK) {
            esp_err_t enc_res = jpeg_encoder_process(
                cam->encoder_handle, 
                &cam->pic_cfg, 
                cam->ppa_out_buf, 
                cam->ppa_out_buf_size,             
                cam->jpeg_out_buf, 
                cam->jpeg_out_buf_size, 
                &final_jpeg_size
            );

            if (enc_res == ESP_OK && final_jpeg_size > 0) {
                lua_pushlstring(L, (const char *)cam->jpeg_out_buf, final_jpeg_size);
            } else { lua_pushnil(L); }
            
        } else {
            lua_pushnil(L); 
        }
    } else { 
        lua_pushnil(L); 
    }

    ioctl(cam->fd, VIDIOC_QBUF, &buf);
    return 1;
}

static int LCAM_close(lua_State *L) {
    LCAM *cam = LCAM_getUD(L);
    
    if (cam->fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(cam->fd, VIDIOC_STREAMOFF, &type);
        
        hw_jpeg_encoder_deinit(cam);

        if (cam->ppa_srm_handle) {
            ppa_unregister_client(cam->ppa_srm_handle);
            cam->ppa_srm_handle = NULL;
        }

        if (cam->ppa_out_buf) {
            heap_caps_free(cam->ppa_out_buf);
            cam->ppa_out_buf = NULL;
        }

        for (int i = 0; i < P4_CAM_BUFFERS; i++) {
            if (cam->buffers[i]) {
                munmap(cam->buffers[i], cam->buf_lengths[i]);
                cam->buffers[i] = NULL;
            }
        }

        close(cam->fd);
        cam->fd = -1;
        
        p4_camera_hardware_deinit(); 
    }
    return 0;
}

static int LCAM_lclose(lua_State* L) {
    LCAM_checkUD(L);
    LCAM_close(L);
    return 0;
}

static const luaL_Reg camObjLib[] = {
    {"read", LCAM_lread},
    {"set_control", LCAM_lset_control}, 
    {"close", LCAM_lclose},
    {"__close", LCAM_close},
    {"__gc", LCAM_close},
    {NULL, NULL}
};

/**
 * @brief Lua Constructor: esp32.cam({table})
 */
int lcam(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    int pin_scl  = balua_getIntField(L, 1, "scl", 8);
    int pin_sda  = balua_getIntField(L, 1, "sda", 7);
    int pin_xclk = balua_getIntField(L, 1, "xclk", 22);
    int quality  = balua_getIntField(L, 1, "quality", 80);
    
    const char *format_str = balua_getStringField(L, 1, "format", "JPEG");
    uint32_t sensor_pixel_format; 
    bool use_hardware_jpeg = false;

    if (strcmp(format_str, "JPEG") == 0) {
        sensor_pixel_format = V4L2_PIX_FMT_RGB565;
        use_hardware_jpeg = true;
    } 
    else if (strcmp(format_str, "YUV422") == 0) {
        sensor_pixel_format = V4L2_PIX_FMT_UYVY; 
        use_hardware_jpeg = false;
    }
    else if (strcmp(format_str, "RGB565") == 0) {
        sensor_pixel_format = V4L2_PIX_FMT_RGB565;
        use_hardware_jpeg = false;
    }
    else {
        sensor_pixel_format = V4L2_PIX_FMT_UYVY;
    }

    uint32_t req_w = 640;
    uint32_t req_h = 480;

    lua_getfield(L, 1, "width");
    if (lua_isnumber(L, -1)) req_w = lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "height");
    if (lua_isnumber(L, -1)) req_h = lua_tointeger(L, -1);
    lua_pop(L, 1);

    if (req_w == 640 && req_h == 480) {
        const char *frame_str = balua_getStringField(L, 1, "frame", "VGA");
        if (strcmp(frame_str, "HD") == 0)        { req_w = 1280; req_h = 720; }
        else if (strcmp(frame_str, "SVGA") == 0) { req_w = 800;  req_h = 600; }
        else if (strcmp(frame_str, "VGA") == 0)  { req_w = 640;  req_h = 480; }
        else if (strcmp(frame_str, "QVGA") == 0) { req_w = 320;  req_h = 240; }
    }

    // --- PHASE 1: HARDWARE INIT ---
    if (p4_camera_hardware_init(pin_scl, pin_sda, pin_xclk) != ESP_OK) {
        return luaL_error(L, "Hardware init failed (check I2C/XCLK pins)");
    }

    int fd = open("/dev/video0", O_RDWR);
    if (fd < 0) {
        p4_camera_hardware_deinit();
        return luaL_error(L, "Failed to open /dev/video0");
    }

    // --- PHASE 2: RESOLUTION NEGOTIATION ---
    uint32_t best_w, best_h;
    if (setup_v4l2_resolution(fd, req_w, req_h, sensor_pixel_format, &best_w, &best_h) != ESP_OK) {
        close(fd);
        p4_camera_hardware_deinit();
        return luaL_error(L, "Driver rejected all known resolutions");
    }

    // --- PHASE 3: ALLOCATE LUA OBJECT ---
    LCAM* cam = (LCAM*)lNewUdata(L, sizeof(LCAM), BACAM, camObjLib);
    memset(cam, 0, sizeof(LCAM));
    cam->fd = fd;
    cam->requested_w = req_w;  
    cam->requested_h = req_h;
    cam->width = best_w;  
    cam->height = best_h;

    // --- PHASE 4: V4L2 BUFFERS & PPA & JPEG ---
    // If any of these fail, we use the cleanup labels at the bottom.
    if (setup_v4l2_buffers(cam) != ESP_OK) {
        goto err_buffers;
    }

    if (setup_ppa_scaler(cam) != ESP_OK) {
        goto err_ppa;
    }

    if (use_hardware_jpeg) {
        if (hw_jpeg_encoder_init(cam, sensor_pixel_format, quality) != ESP_OK) {
            goto err_jpeg;
        }
    }

    // --- PHASE 5: START STREAM ---
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        goto err_jpeg; // Stream failed, clean everything
    }

    // Warm-up loop (Auto Exposure adjustment)
    for(int i=0; i<5; i++) {
        struct v4l2_buffer dump = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
        if(ioctl(fd, VIDIOC_DQBUF, &dump) == 0) {
            ioctl(fd, VIDIOC_QBUF, &dump);
        }
    }

    return 1; // Success!

// --- CLEANUP LABELS (Executed in reverse order on failure) ---
err_jpeg:
    if (cam->ppa_srm_handle) { ppa_unregister_client(cam->ppa_srm_handle); cam->ppa_srm_handle = NULL; }
    if (cam->ppa_out_buf) { heap_caps_free(cam->ppa_out_buf); cam->ppa_out_buf = NULL; }
err_ppa:
err_buffers:
    for (int i = 0; i < P4_CAM_BUFFERS; i++) {
        if (cam->buffers[i]) { munmap(cam->buffers[i], cam->buf_lengths[i]); cam->buffers[i] = NULL; }
    }
    close(fd);
    cam->fd = -1;
    p4_camera_hardware_deinit();
    return luaL_error(L, "Failed to initialize video pipeline components");
}

#endif // CONFIG_IDF_TARGET_ESP32P4
