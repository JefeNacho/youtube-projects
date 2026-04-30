#include "ui.h"
#include "ui_img_manager.h"

// Imagen cargada desde LittleFS a PSRAM en runtime. NO embebida en flash.
// Llamar ui_img_mty_atardecer_png_load() desde setup() despues de montar LittleFS.

lv_img_dsc_t ui_img_mty_atardecer_png = {
    .header.always_zero = 0,
    .header.w           = 800,
    .header.h           = 480,
    .header.cf          = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .data_size          = 1152000,
    .data               = NULL,
};

void ui_img_mty_atardecer_png_load() {
    ui_img_mty_atardecer_png.data      = UI_LOAD_IMAGE("S:assets/ui_img_mty_atardecer_png.bin", 1152000);
    ui_img_mty_atardecer_png.data_size = 1152000;
}
