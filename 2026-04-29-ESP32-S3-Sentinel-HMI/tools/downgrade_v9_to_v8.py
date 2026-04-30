import os

replacements = {
    "lv_screen_load_anim_t": "lv_scr_load_anim_t",
    "lv_image_dsc_t": "lv_img_dsc_t",
    "lv_image_create": "lv_img_create",
    "lv_image_set_src": "lv_img_set_src",
    "lv_obj_remove_flag": "lv_obj_clear_flag",
    "lv_image_set_pivot": "lv_img_set_pivot",
    "lv_image_set_rotation": "lv_img_set_angle",
    ".header.magic = LV_IMAGE_HEADER_MAGIC,": ".header.always_zero = 0,",
    "LV_COLOR_FORMAT_ARGB8888": "LV_IMG_CF_TRUE_COLOR_ALPHA",
    "LV_COLOR_FORMAT_RGB565A8": "LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED",
    "LV_COLOR_FORMAT_RGB565": "LV_IMG_CF_TRUE_COLOR"
}

def downgrade():
    ui_dir = r"c:\wkspace\pantalla\sentinel_hmi\src\ui"
    files = []
    for root, dirs, filenames in os.walk(ui_dir):
        for f in filenames:
            if f.endswith(".c") or f.endswith(".h"):
                files.append(os.path.join(root, f))
                
    patched_count = 0
    for filepath in files:
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                content = f.read()
                
            modified = False
            for old, new in replacements.items():
                if old in content:
                    content = content.replace(old, new)
                    modified = True
                    
            if modified:
                with open(filepath, 'w', encoding='utf-8') as f:
                    f.write(content)
                print(f"Patched: {filepath}")
                patched_count += 1
        except Exception as e:
            print(f"Error reading {filepath}: {e}")
            
    print(f"\nTerminado: {patched_count} archivos actualizados a sintaxis LVGL 8.3")

if __name__ == "__main__":
    downgrade()
