#include "ui.h"
#include <esp_heap_caps.h>

uint8_t* _ui_load_binary(char* fname, const uint32_t size)
{
    lv_fs_file_t f;
    lv_fs_res_t res;
    res = lv_fs_open(&f, fname, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) return NULL; // file not found
    uint32_t read_num;
    
    // Forzar lectura directamente a la PSRAM externa (Evita limites estrangulados por LVGL Heap)
    uint8_t* buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) return NULL; // Si falla por falta de PSRAM, proteger sistema.

    res = lv_fs_read(&f, buf, size, &read_num);
    if (res != LV_FS_RES_OK || read_num != size)
    {
        heap_caps_free(buf);
        return NULL;
    }
    lv_fs_close(&f);
    return buf;
}

