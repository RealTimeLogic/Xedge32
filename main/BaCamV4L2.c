/*
 * BaCamV4L2.c
 * Binding Lua de cámara para Xedge32 usando V4L2 (Exclusivo para ESP32-P4)
 * Mantiene compatibilidad de API con el driver DVP/esp32-camera anterior.
 */

#include "BaESP32.h"

#if CONFIG_IDF_TARGET_ESP32P4

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
#define P4_CAM_BUFFERS 2 // Cantidad de buffers V4L2 a mapear en memoria

typedef struct {
    int fd;
    uint8_t *buffers[2];
    uint32_t buf_lengths[2];
    uint32_t width;
    uint32_t height;
    
    // --- ESTADO DEL COMPRESOR HW ---
    jpeg_encoder_handle_t encoder_handle;
    jpeg_encode_cfg_t pic_cfg;     // <--- ¡Acá guardamos los datos de la foto!
    uint8_t *jpeg_out_buf;       
    uint32_t jpeg_out_buf_size;  
    // --- PPA para escalar la imagen a formatos no nativos del sensor.
    ppa_client_handle_t ppa_srm_handle; // El manejador del motor 2D
    uint8_t *ppa_out_buf;               // El buffer intermedio alineado a caché
    uint32_t ppa_out_buf_size;          // Tamaño de ese buffer intermedio
    uint32_t requested_w;               // El ancho final que pidió Lua
    uint32_t requested_h;               // El alto final que pidió Lua
} LCAM;

static LCAM* LCAM_getUD(lua_State* L)
{
   return (LCAM*)luaL_checkudata(L, 1, BACAM);
}

static LCAM* LCAM_checkUD(lua_State* L)
{
   LCAM* o = LCAM_getUD(L);
   if (o->fd == 0)
   {
      luaL_error(L, "Camera not initialized");
   }
   return o;
}

// =========================================================================
// 0. INICIALIZACIÓN FÍSICA DEL P4 (Reloj y Bus de Control)
// =========================================================================

// ¡ATENCIÓN! Ajustá estos pines según tu placa de desarrollo ESP32-P4
//#define P4_CAM_SCL_PIN   53 
//#define P4_CAM_SDA_PIN   54
//#define P4_CAM_XCLK_PIN  55
//#define P4_CAM_I2C_PORT  0
//#define P4_CAM_XCLK_FREQ 24000000 // 24 MHz es estándar para sensores MIPI

static i2c_master_bus_handle_t s_i2cbus_handle = NULL;
static esp_cam_sensor_xclk_handle_t s_xclk_handle = NULL;

static esp_err_t p4_camera_hardware_init(int scl_pin, int sda_pin, int xclk_pin) {
    esp_err_t err;

    // 1. Iniciar bus I2C (SCCB)
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

    // 2. Encender el Reloj Maestro (XCLK)
    esp_cam_sensor_xclk_config_t xclk_config = {
        .esp_clock_router_cfg = {
            .xclk_pin = xclk_pin,
            .xclk_freq_hz = 24000000, // 24MHz fijo
        }
    };
    err = esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &s_xclk_handle);
    if (err == ESP_OK) {
        err = esp_cam_sensor_xclk_start(s_xclk_handle, &xclk_config);
    }
    if (err != ESP_OK) return err;

    // 3. Registrar la cámara MIPI en el VFS (Crea el /dev/video0)
    esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .init_sccb = false, // Lo inicializamos nosotros arriba
            .i2c_handle = s_i2cbus_handle,
            .freq = 400000,
        },
        .reset_pin = -1, // Cambiar si tu placa usa un pin de reset
        .pwdn_pin = -1,  // Cambiar si tu placa usa un pin de power down
    };
    
    esp_video_init_config_t cam_config = {
        .csi = &csi_config,
    };

    return esp_video_init(&cam_config);
}

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
// 1. MOTOR DE COMPRESIÓN JPEG POR HARDWARE (Cero Copias)
// =========================================================================

static esp_err_t hw_jpeg_encoder_init(LCAM *cam, uint32_t pixel_format, uint8_t quality) {
    // 1. CONFIGURAR EL MOTOR (Hardware físico)
    jpeg_encode_engine_cfg_t eng_cfg = {
        .timeout_ms = 1000,
    };
    
    esp_err_t ret = jpeg_new_encoder_engine(&eng_cfg, &cam->encoder_handle);
    if (ret != ESP_OK) return ret;

    // 2. CONFIGURAR LA FOTO (La guardamos en la estructura de la cámara)
    cam->pic_cfg.width = cam->width;
    cam->pic_cfg.height = cam->height;
    cam->pic_cfg.image_quality = quality;

    uint32_t raw_size = 0;

    // Traductor V4L2 -> ESP JPEG
    switch (pixel_format) {
        case V4L2_PIX_FMT_RGB565:
            cam->pic_cfg.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
            cam->pic_cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
            raw_size = cam->width * cam->height * 2;
            break;
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_YUYV:
            cam->pic_cfg.src_type = JPEG_ENCODE_IN_FORMAT_YUV422;
            cam->pic_cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
            raw_size = cam->width * cam->height * 2;
            break;
#if CONFIG_ESP32P4_REV_MIN_FULL >= 300
        case V4L2_PIX_FMT_YUV420:
            cam->pic_cfg.src_type = JPEG_ENCODE_IN_FORMAT_YUV420;
            cam->pic_cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV420;
            raw_size = cam->width * cam->height * 3 / 2;
            break;
#endif
        default:
            jpeg_del_encoder_engine(cam->encoder_handle);
            return ESP_ERR_NOT_SUPPORTED;
    }

    // 3. ALLOC PERSISTENTE DMA
    cam->jpeg_out_buf_size = raw_size * 3 / 4; 
    jpeg_encode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    
    cam->jpeg_out_buf = (uint8_t *)jpeg_alloc_encoder_mem(cam->jpeg_out_buf_size, &mem_cfg, (size_t*)&cam->jpeg_out_buf_size);
    if (!cam->jpeg_out_buf) {
        jpeg_del_encoder_engine(cam->encoder_handle);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void hw_jpeg_encoder_deinit(LCAM *cam) {
    if (cam->encoder_handle) {
        jpeg_del_encoder_engine(cam->encoder_handle);
    }
    if (cam->jpeg_out_buf) {
        free(cam->jpeg_out_buf); // Liberamos la RAM persistente
    }
}

// =========================================================================
// 2. BINDINGS DE LUA
// =========================================================================

// Función auxiliar para aplicar controles al vuelo (Flips, Brillo, AWB, etc.)
static int LCAM_lset_control(lua_State *L) {
    LCAM *cam =  LCAM_checkUD(L);
    const char *ctrl_name = luaL_checkstring(L, 2);
    int value = luaL_checkinteger(L, 3);

    struct v4l2_ext_controls ctrls = {0};
    struct v4l2_ext_control ctrl = {0};
    ctrls.ctrl_class = V4L2_CID_USER_CLASS;
    ctrls.count = 1;
    ctrls.controls = &ctrl;

    // Mapeo simple de strings a CIDs de Linux
    if (strcmp(ctrl_name, "vflip") == 0) ctrl.id = V4L2_CID_VFLIP;
    else if (strcmp(ctrl_name, "hflip") == 0) ctrl.id = V4L2_CID_HFLIP;
    else if (strcmp(ctrl_name, "brightness") == 0) ctrl.id = V4L2_CID_BRIGHTNESS;
    else if (strcmp(ctrl_name, "contrast") == 0) ctrl.id = V4L2_CID_CONTRAST;
    else if (strcmp(ctrl_name, "red_balance") == 0) ctrl.id = V4L2_CID_RED_BALANCE;
    else if (strcmp(ctrl_name, "blue_balance") == 0) ctrl.id = V4L2_CID_BLUE_BALANCE;
    else {
        return luaL_error(L, "Control no soportado: %s", ctrl_name);
    }

    ctrl.value = value;
    
    // Ejecutamos el IOCTL (Degradación elegante: si falla, devolvemos false a Lua)
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
        // ==========================================================
        // 1. EL ESCALADOR HARDWARE (PPA SRM)
        // ==========================================================
        ppa_srm_oper_config_t srm_config = {
            .in = {
                .buffer = cam->buffers[buf.index], // Lo que escupe el sensor
                .pic_w = cam->width,
                .pic_h = cam->height,
                .block_w = cam->width,
                .block_h = cam->height,
                .block_offset_x = 0,
                .block_offset_y = 0,
                //.srm_cm = (ppa_srm_color_mode_t)ESP_COLOR_FOURCC_UYVY,
                //.srm_cm = PPA_SRM_COLOR_MODE_YUV422, // UYVY nativo
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .out = {
                .buffer = cam->ppa_out_buf,        // Nuestro buffer alineado DMA
                .buffer_size = cam->ppa_out_buf_size,
                .pic_w = cam->requested_w,
                .pic_h = cam->requested_h,
                .block_offset_x = 0,
                .block_offset_y = 0,
                //.srm_cm = (ppa_srm_color_mode_t)ESP_COLOR_FOURCC_UYVY,
                //.srm_cm = PPA_SRM_COLOR_MODE_YUV422,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            // Calculamos cuánto hay que achicar dinámicamente
            .scale_x = (float)cam->requested_w / (float)cam->width,
            .scale_y = (float)cam->requested_h / (float)cam->height,
            .rgb_swap = 0,
            .byte_swap = 0,
            .mode = PPA_TRANS_MODE_BLOCKING, // Esperamos que termine el hardware
        };

        // Ejecutamos el motor 2D
        if (ppa_do_scale_rotate_mirror(cam->ppa_srm_handle, &srm_config) == ESP_OK) {
            
            // ==========================================================
            // 2. EL BISTURÍ V2 (Inyectado sobre la salida del PPA)
            // Curamos el color intercambiando U (1) y V (3)
            // ==========================================================
           /* uint8_t *ptr = (uint8_t *)cam->ppa_out_buf;
            uint32_t len = cam->requested_w * cam->requested_h * 2;
            
            for (uint32_t i = 0; i < len; i += 4) {
                uint8_t temp_u = ptr[i + 1];
                ptr[i + 1] = ptr[i + 3];
                ptr[i + 3] = temp_u;
            }
            */
            // ==========================================================
            // 3. COMPRESIÓN JPEG (Le damos de comer el buffer PPA)
            // ==========================================================
            esp_err_t enc_res = jpeg_encoder_process(
                cam->encoder_handle, 
                &cam->pic_cfg, 
                cam->ppa_out_buf, // <--- ¡La imagen ya recortada y curada!
                //len, 
                cam->ppa_out_buf_size,             
                cam->jpeg_out_buf, 
                cam->jpeg_out_buf_size, 
                &final_jpeg_size
            );

            if (enc_res == ESP_OK && final_jpeg_size > 0) {
                lua_pushlstring(L, (const char *)cam->jpeg_out_buf, final_jpeg_size);
            } else { lua_pushnil(L); }
            
        } else {
            // Falló el PPA
            lua_pushnil(L); 
        }
    } else { 
        lua_pushnil(L); 
    }

    ioctl(cam->fd, VIDIOC_QBUF, &buf);
    return 1;
}

/*
static int LCAM_close(lua_State *L) {
    LCAM *cam = LCAM_getUD(L);
    if (cam->fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(cam->fd, VIDIOC_STREAMOFF, &type);
        
        // Apagamos el motor y liberamos nuestro buffer de DMA
        hw_jpeg_encoder_deinit(cam);

        for (int i = 0; i < 2; i++) {
            if (cam->buffers[i]) munmap(cam->buffers[i], cam->buf_lengths[i]);
        }
        close(cam->fd);
        cam->fd = -1;
        p4_camera_hardware_deinit(); // Liberamos I2C y reloj
    }
    return 0;
}
*/
static int LCAM_close(lua_State *L) {
    LCAM *cam = LCAM_getUD(L);
    
    if (cam->fd >= 0) {
        // 1. FRENAR EL STREAM (Fundamental para liberar el hardware del ISP)
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(cam->fd, VIDIOC_STREAMOFF, &type);
        
        // 2. DEINIT DEL ENCODER (Lo que ya tenías, perfecto)
        hw_jpeg_encoder_deinit(cam);

        // 3. LIBERAR PPA (El culpable del "unsupported color mode" o bloqueos de hardware)
        if (cam->ppa_srm_handle) {
            ppa_unregister_client(cam->ppa_srm_handle);
            cam->ppa_srm_handle = NULL;
        }

        // 4. LIBERAR BUFFER DE ESCALADO (El culpable del "Failed to create buffer")
        if (cam->ppa_out_buf) {
            heap_caps_free(cam->ppa_out_buf);
            cam->ppa_out_buf = NULL;
        }

        // 5. UNMAP DE BUFFERS V4L2 (Usamos la constante correcta)
        for (int i = 0; i < P4_CAM_BUFFERS; i++) {
            if (cam->buffers[i]) {
                munmap(cam->buffers[i], cam->buf_lengths[i]);
                cam->buffers[i] = NULL;
            }
        }

        // 6. CIERRE DE DESCRIPTOR Y HARDWARE GLOBAL
        close(cam->fd);
        cam->fd = -1;
        
        // Esta función está bien si solo manejas una cámara a la vez.
        // Si pensás en un sistema con múltiples cámaras, habría que tener cuidado.
        p4_camera_hardware_deinit(); 
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

static const luaL_Reg camObjLib[] = {
   {"read", LCAM_lread},
   {"set_control", LCAM_lset_control}, // <-- ¡Nuestra nueva súper función!
   {"close", LCAM_lclose},
   {"__close", LCAM_close},
   {"__gc", LCAM_close},
   {NULL, NULL}
};

/**
 * Constructor de la cámara en Lua: esp32.cam({table})
 */
int lcam(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    // ==========================================================
    // 1. LECTURA DE TABLA CON BAS API (Limpio y con Defaults)
    // ==========================================================
    int pin_scl  = balua_getIntField(L, 1, "scl", 8);
    int pin_sda  = balua_getIntField(L, 1, "sda", 7);
    int pin_xclk = balua_getIntField(L, 1, "xclk", 22);
    int quality  = balua_getIntField(L, 1, "quality", 80);
    
    const char *format_str = balua_getStringField(L, 1, "format", "JPEG");

    // VARIABLE MAESTRA: Lo que físicamente le pediremos al kernel de Linux
    uint32_t sensor_pixel_format; 
    bool use_hardware_jpeg = false;

    if (strcmp(format_str, "JPEG") == 0) {
        // Lua pide JPEG. Forzamos UYVY (el YUV soportado por el P4) y prendemos el motor.
        //sensor_pixel_format = V4L2_PIX_FMT_UYVY; 
        // El PPA del P4 rev 1.0 no escala YUV422. 
        // Pasamos toda la tubería a RGB565.
        sensor_pixel_format = V4L2_PIX_FMT_RGB565;
        use_hardware_jpeg = true;
    } 
    else if (strcmp(format_str, "YUV422") == 0) {
        sensor_pixel_format = V4L2_PIX_FMT_UYVY; // O UYVY si lo preferís
        use_hardware_jpeg = false;
    }
    else if (strcmp(format_str, "RGB565") == 0) {
        sensor_pixel_format = V4L2_PIX_FMT_RGB565;
        use_hardware_jpeg = false;
    }
    else {
        // Fallback de seguridad
        sensor_pixel_format = V4L2_PIX_FMT_UYVY;
    }

    // LÓGICA HÍBRIDA DE RESOLUCIÓN
    uint32_t width = 640;
    uint32_t height = 480;

    lua_getfield(L, 1, "width");
    if (lua_isnumber(L, -1)) width = lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "height");
    if (lua_isnumber(L, -1)) height = lua_tointeger(L, -1);
    lua_pop(L, 1);

    // Si el usuario no pasó width/height, buscamos la etiqueta clásica "frame"
    if (width == 640 && height == 480) {
        const char *frame_str = balua_getStringField(L, 1, "frame", "VGA");
        if (strcmp(frame_str, "HD") == 0)        { width = 1280; height = 720; }
        else if (strcmp(frame_str, "SVGA") == 0) { width = 800; height = 600; }
        else if (strcmp(frame_str, "VGA") == 0)  { width = 640; height = 480; }
        else if (strcmp(frame_str, "QVGA") == 0) { width = 320; height = 240; }
    }

    // ==========================================================
    // 1.5 INICIALIZACIÓN DE HARDWARE FÍSICO
    // ==========================================================
    if (p4_camera_hardware_init(pin_scl, pin_sda, pin_xclk) != ESP_OK) {
        return luaL_error(L, "Fallo al inicializar hardware (revisá los pines I2C/XCLK)");
    }

    int fd = open("/dev/video0", O_RDWR);
    if (fd < 0) return luaL_error(L, "No se pudo abrir /dev/video0");
    
    // ==========================================================
    // AUTO-NEGOCIACIÓN (Fuerza Bruta V4L2)
    // El driver esp_video 2.1.0 no soporta ENUM_FRAMESIZES.
    // Iteramos e intentamos setear la resolución directamente.
    // ==========================================================
    uint32_t best_w = 0;
    uint32_t best_h = 0;

    // Tabla de resoluciones nativas típicas del OV5647
    uint32_t fallbacks[][2] = {
        {1920, 1080}, {1280, 960}, {1280, 720}, 
        {1024, 768}, {800, 800}, {800, 640}, {640, 480}, {320, 240}
    };
    int num_fallbacks = sizeof(fallbacks) / sizeof(fallbacks[0]);

    struct v4l2_format clean_fmt = {0};
    clean_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    // Buscamos la resolución óptima para que el PPA achique (Downscale)
    for (int i = num_fallbacks - 1; i >= 0; i--) {
        uint32_t try_w = fallbacks[i][0];
        uint32_t try_h = fallbacks[i][1];
        
        if (try_w >= width && try_h >= height) {
            clean_fmt.fmt.pix.width = try_w;
            clean_fmt.fmt.pix.height = try_h;
            clean_fmt.fmt.pix.pixelformat = sensor_pixel_format;
            
            if (ioctl(fd, VIDIOC_S_FMT, &clean_fmt) == 0) {
                best_w = clean_fmt.fmt.pix.width;
                best_h = clean_fmt.fmt.pix.height;
                printf("[DEBUG] Exito (Upscale) seteando: %ldx%ld\n", best_w, best_h);
                break;
            }
        }
    }

    // Modo Pánico (Upscale): si el sensor no tiene nada mayor a lo pedido
    if (best_w == 0) {
        for (int i = 0; i < num_fallbacks; i++) {
            clean_fmt.fmt.pix.width = fallbacks[i][0];
            clean_fmt.fmt.pix.height = fallbacks[i][1];
            clean_fmt.fmt.pix.pixelformat = sensor_pixel_format;
            
            if (ioctl(fd, VIDIOC_S_FMT, &clean_fmt) == 0) {
                best_w = clean_fmt.fmt.pix.width;
                best_h = clean_fmt.fmt.pix.height;
                break; 
            }
        }
    }

    if (best_w == 0) {
        close(fd); return luaL_error(L, "El driver rechazo todas las resoluciones conocidas");
    }

    // ==========================================================
    // SET FORMAT: Acá ocurre la magia del ISP de Espressif.
    // Le pedimos la resolución física que encontramos, pero le exigimos
    // nuestro querido sensor_pixel_format (UYVY) para el motor JPEG.
    // ==========================================================
   // struct v4l2_format clean_fmt = {0};
    clean_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    clean_fmt.fmt.pix.width = best_w;
    clean_fmt.fmt.pix.height = best_h;
    clean_fmt.fmt.pix.pixelformat = sensor_pixel_format; // Forzamos UYVY

    if (ioctl(fd, VIDIOC_S_FMT, &clean_fmt) < 0) {
        close(fd); return luaL_error(L, "El driver rechazo la combinacion de tamaño y formato UYVY");
    }
    
    // Guardamos la resolución física elegida para que el PPA sepa de dónde partir
    //cam->width = best_w;
    //cam->height = best_h;
    // (Nota: el hardware ya quedó seteado correctamente gracias al ioctl del bucle)
    // ==========================================================

    // Ahora sí, le exigimos al hardware de Linux esa resolución exacta
    //struct v4l2_format clean_fmt = {0};
    clean_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    clean_fmt.fmt.pix.width = best_w;
    clean_fmt.fmt.pix.height = best_h;
    clean_fmt.fmt.pix.pixelformat = sensor_pixel_format; 

    if (ioctl(fd, VIDIOC_S_FMT, &clean_fmt) < 0) {
        close(fd); return luaL_error(L, "Error en VIDIOC_S_FMT al setear Best Fit");
    }
    // ==========================================================

    // 3. PEDIR BUFFERS A LINUX
    struct v4l2_requestbuffers req = {
        .count = P4_CAM_BUFFERS,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    ioctl(fd, VIDIOC_REQBUFS, &req);

    // 4. CREAR EL OBJETO CÁMARA (Acá nace 'cam')
    LCAM* cam = (LCAM*)lNewUdata(L, sizeof(LCAM), BACAM, camObjLib);
    memset(cam, 0, sizeof(LCAM));
    cam->fd = fd;

    // 1. Guardamos lo que el usuario de Lua pidió como destino final
    cam->requested_w = width;  
    cam->requested_h = height;

    // Leemos la resolución final que el sensor aceptó (por si ajustó los valores)
    cam->width = clean_fmt.fmt.pix.width;  
    cam->height = clean_fmt.fmt.pix.height;

    // ==========================================================
    // INICIALIZACIÓN DEL PPA (PIXEL-PROCESSING ACCELERATOR)
    // ==========================================================
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    
    if (ppa_register_client(&ppa_srm_config, &cam->ppa_srm_handle) != ESP_OK) {
        close(fd); return luaL_error(L, "Error al registrar cliente PPA SRM");
    }

    // 3. Reservar memoria intermedia ALINEADA A CACHÉ para la salida del PPA
    // Calculamos el tamaño para el formato YUV422 (2 bytes por píxel) del tamaño FINAL
    cam->ppa_out_buf_size = cam->requested_w * cam->requested_h * 2; 
    
    // Usamos el flag mágico de Espressif que garantiza la alineación para DMA
    cam->ppa_out_buf = heap_caps_calloc(1, cam->ppa_out_buf_size, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    
    if (!cam->ppa_out_buf) {
        ppa_unregister_client(cam->ppa_srm_handle);
        close(fd); return luaL_error(L, "Error: Sin RAM para el buffer del PPA");
    }
    // ==========================================================

    // 5. MAPEAR MEMORIA (DMA)
    for (int i = 0; i < P4_CAM_BUFFERS; i++) {
        struct v4l2_buffer buf = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i };
        ioctl(fd, VIDIOC_QUERYBUF, &buf);
        cam->buf_lengths[i] = buf.length;
        cam->buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    // 6. INICIALIZAR EL MOTOR JPEG
    // Ahora sí es el lugar correcto: 'cam' existe y tiene su width/height definidos.
    if (use_hardware_jpeg) {
        // ¡OJO ACÁ! Temporariamente pisamos el width/height de la cámara 
        // para engañar al inicializador del JPEG con las dimensiones finales
        uint32_t sensor_w = cam->width;
        uint32_t sensor_h = cam->height;
        cam->width = cam->requested_w;
        cam->height = cam->requested_h;
        
        if (hw_jpeg_encoder_init(cam, sensor_pixel_format, quality) != ESP_OK) {
            // Faltaría liberar ppa acá si falla, pero para probar estamos bien
            close(fd); return luaL_error(L, "Error HW JPEG Init");
        }
        // Restauramos el tamaño original para saber cuánto leemos de Linux
        cam->width = sensor_w;
        cam->height = sensor_h;
    }

    // 7. ARRANCAR EL STREAM DE VIDEO
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);

    // --- EL WARM-UP MÁGICO ---
    // Descartamos los primeros 5 frames para que el sensor ajuste la luz (AE)
    for(int i=0; i<5; i++) {
        struct v4l2_buffer dump = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
        if(ioctl(fd, VIDIOC_DQBUF, &dump) == 0) {
            ioctl(fd, VIDIOC_QBUF, &dump);
        }
    }
    // -------------------------

    return 1;
}

#endif // CONFIG_IDF_TARGET_ESP32P4
