#pragma once
#include <lvgl.h>
#include <LittleFS.h>

// LVGL usa un sistema de archivos virtual. Esta clase enruta sus peticiones a la memoria LittleFS del ESP32.

static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode) {
    const char * flags = "";
    if(mode == LV_FS_MODE_WR) flags = "w";
    else if(mode == LV_FS_MODE_RD) flags = "r";
    else if(mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) flags = "r+";

    // SquareLine pedirá rutas como "L:assets/img.bin". Aquí le agregamos el "/" inicial para LittleFS: "/assets/img.bin"
    char buf[256];
    snprintf(buf, sizeof(buf), "/%s", path);
    
    File f = LittleFS.open(buf, flags);
    if(!f) {
        Serial.printf("[LVGL-FS] Error: No se pudo abrir el asset binario: %s\n", buf);
        return NULL;
    }
    
    File * fp = new File(f);
    return (void *)fp;
}

static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * file_p) {
    File * fp = (File *)file_p;
    fp->close();
    delete fp;
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br) {
    File * fp = (File *)file_p;
    *br = fp->read((uint8_t *)buf, btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence) {
    File * fp = (File *)file_p;
    SeekMode mode;
    if(whence == LV_FS_SEEK_SET) mode = SeekSet;
    else if(whence == LV_FS_SEEK_CUR) mode = SeekCur;
    else if(whence == LV_FS_SEEK_END) mode = SeekEnd;
    
    fp->seek(pos, mode);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p) {
    File * fp = (File *)file_p;
    *pos_p = fp->position();
    return LV_FS_RES_OK;
}

// Llama a esta función en el setup() de Arduino ANTES de ui_init()
void init_lvgl_fs() {
    if(!LittleFS.begin(true)) {
        Serial.println("[LVGL-FS] ERROR FATAL: LittleFS falló al montarse. ¡Las imágenes no cargarán!");
        return;
    }
    Serial.println("[LVGL-FS] LittleFS montado exitosamente. Preparando partición estática.");

    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);
    
    // Le asignamos la letra 'S' (SquareLine exportó esto por defecto)
    fs_drv.letter = 'S'; 
    fs_drv.open_cb = fs_open;
    fs_drv.close_cb = fs_close;
    fs_drv.read_cb = fs_read;
    fs_drv.seek_cb = fs_seek;
    fs_drv.tell_cb = fs_tell;
    
    lv_fs_drv_register(&fs_drv);
}
