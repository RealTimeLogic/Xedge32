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
#include <linux/videodev2.h>
#include "example_video_common.h" // Cabeceras de Espressif para el encoder HW

#define BACAM "CAM"
#define P4_CAM_BUFFERS 2 // Cantidad de buffers V4L2 a mapear en memoria

/*
 * Estructura de nuestro userdata de Lua.
 * Lua ve un objeto "CAM", pero nosotros manejamos descriptores POSIX.
 */
typedef struct {
    int fd;
    void* buffers[P4_CAM_BUFFERS];
    size_t buffer_size;
    example_encoder_handle_t encoder_handle;
    uint8_t* jpeg_out_buf;
    uint32_t jpeg_out_size;
} LCAM;

// Obtiene el objeto desde la pila de Lua
static LCAM* LCAM_getUD(lua_State* L) {
    return (LCAM*)luaL_checkudata(L, 1, BACAM);
}

// cam:close() / __gc
static int LCAM_close(lua_State* L) {
    LCAM* cam = LCAM_getUD(L);
    
    if (cam->fd >= 0) {
        // 1. Detener el flujo de video (Stream OFF)
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(cam->fd, VIDIOC_STREAMOFF, &type);

        // 2. Liberar memoria mapeada (mmap)
        for (int i = 0; i < P4_CAM_BUFFERS; i++) {
            if (cam->buffers[i] != MAP_FAILED && cam->buffers[i] != NULL) {
                munmap(cam->buffers[i], cam->buffer_size);
            }
        }

        // 3. Cerrar dispositivo
        close(cam->fd);
        cam->fd = -1;

        // 4. Apagar y liberar el codificador JPEG por hardware
        if (cam->encoder_handle) {
            example_encoder_free_output_buffer(cam->encoder_handle, cam->jpeg_out_buf);
            example_encoder_deinit(cam->encoder_handle);
            cam->encoder_handle = NULL;
            cam->jpeg_out_buf = NULL;
        }
    }
    return 0;
}

// img = cam:read()
static int LCAM_lread(lua_State* L) {
    LCAM* cam = LCAM_getUD(L);
    if (cam->fd < 0) {
        return luaL_error(L, "Camera is closed or uninitialized");
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    /*
     * ZONA CRÍTICA: Liberamos el mutex de Xedge32.
     * VIDIOC_DQBUF bloquea el hilo hasta que el hardware termina de exponer 
     * el fotograma. Si no soltamos el mutex, Xedge32 entero se congela.
     */
    ThreadMutex_release(soDispMutex);
    
    int res = ioctl(cam->fd, VIDIOC_DQBUF, &buf);
    
    if (res < 0 || !(buf.flags & V4L2_BUF_FLAG_DONE)) {
        ThreadMutex_set(soDispMutex);
        return pushEspRetVal(L, ESP_FAIL, "Failed to capture raw frame from ISP", FALSE);
    }

    // 2. El ISP nos entregó un frame crudo. Ahora usamos el acelerador JPEG.
    uint32_t jpeg_encoded_size = 0;
    esp_err_t enc_res = example_encoder_process(cam->encoder_handle, 
                                                cam->buffers[buf.index], 
                                                cam->buffer_size, 
                                                cam->jpeg_out_buf, 
                                                cam->jpeg_out_size, 
                                                &jpeg_encoded_size);

    /* * Recuperamos el mutex ANTES de volver a interactuar con la API de Lua.
     */
    ThreadMutex_set(soDispMutex);

    // 3. Devolvemos el buffer YUV420 a la cola de V4L2 para que el sensor siga trabajando
    ioctl(cam->fd, VIDIOC_QBUF, &buf);

    if (enc_res != ESP_OK) {
        return pushEspRetVal(L, ESP_FAIL, "Hardware JPEG encoding failed", FALSE);
    }

    // 4. Copiamos el JPEG al string inmutable de Lua
    lua_pushlstring(L, (const char*)cam->jpeg_out_buf, jpeg_encoded_size);
    return 1; 
}

static const luaL_Reg camObjLib[] = {
    {"read", LCAM_lread},
    {"close", LCAM_close},
    {"__close", LCAM_close},
    {"__gc", LCAM_close},
    {NULL, NULL}
};

// Constructor: cam = esp32.cam({ format="JPEG", frame="VGA", ... })
int lcam(lua_State* L) {
    // 1. Verificamos que el argumento 1 sea una tabla
    luaL_checktype(L, 1, LUA_TTABLE);

    // --- Inicialización del subsistema de video ---
    static bool video_subsystem_initialized = false;
    if (!video_subsystem_initialized) {
        if (example_video_init() != ESP_OK) {
            return luaL_error(L, "Hardware video init failed. Is the MIPI camera physically connected?");
        }
        video_subsystem_initialized = true;
    }

    // 2. Abrir dispositivo
    int fd = open("/dev/video0", O_RDWR);
    if (fd < 0) {
        return luaL_error(L, "Failed to open /dev/video0.");
    }

    // 3. CONSULTAR LA RESOLUCIÓN Y FORMATO REAL DEL SENSOR (Get Format)
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (ioctl(fd, VIDIOC_G_FMT, &format) < 0) {
        close(fd);
        return luaL_error(L, "VIDIOC_G_FMT failed to get sensor resolution");
    }
    
    // Guardamos las 3 variables clave dictadas por el hardware
    uint32_t width = format.fmt.pix.width;
    uint32_t height = format.fmt.pix.height;
    uint32_t pixel_format = format.fmt.pix.pixelformat; // <-- AGREGAMOS ESTO
    uint8_t jpeg_quality = 12;

    // 4. Crear el userdata en la RAM de Lua
    LCAM* cam = (LCAM*)lNewUdata(L, sizeof(LCAM), BACAM, camObjLib);
    memset(cam, 0, sizeof(LCAM));
    cam->fd = fd;

    // 5. Configurar el ISP para YUV420 manteniendo la resolución que dictó el hardware
    //format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420; 

    //if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
    //    return luaL_error(L, "ISP failed to accept YUV420 format");
    //}

    // 6. Pedir memoria al Kernel (REQBUFS)
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = P4_CAM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        return luaL_error(L, "V4L2 REQBUFS failed");
    }

    // 7. Mapear los buffers a espacio de usuario (mmap)
    for (int i = 0; i < P4_CAM_BUFFERS; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            return luaL_error(L, "V4L2 QUERYBUF failed");
        }

        cam->buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (cam->buffers[i] == MAP_FAILED) {
            return luaL_error(L, "mmap failed for V4L2 buffer");
        }
        cam->buffer_size = buf.length;

        // Encolar buffer para empezar a recibir frames
        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    // 8. Inicializar el Acelerador JPEG por hardware
    example_encoder_config_t encoder_config = {0};
    encoder_config.width = width;
    encoder_config.height = height;
    encoder_config.pixel_format = pixel_format; // <-- USAMOS EL FORMATO REAL
    encoder_config.quality = jpeg_quality;
    
    if (example_encoder_init(&encoder_config, &cam->encoder_handle) != ESP_OK) {
        return luaL_error(L, "Failed to initialize Hardware JPEG Encoder");
    }

    if (example_encoder_alloc_output_buffer(cam->encoder_handle, &cam->jpeg_out_buf, &cam->jpeg_out_size) != ESP_OK) {
        return luaL_error(L, "Failed to allocate JPEG output buffer");
    }

    // 9. Encender la cámara
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        return luaL_error(L, "VIDIOC_STREAMON failed");
    }

    return 1; // Retorna el userdata (cam) a Lua
}

#endif // CONFIG_IDF_TARGET_ESP32P4
