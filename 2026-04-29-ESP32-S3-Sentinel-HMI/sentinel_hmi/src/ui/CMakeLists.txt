SET(SOURCES ui_comp_comcarg.c
    ui_comp.c
    ui_screenNight.c
    ui_screenMorning.c
    ui_screenDay.c
    ui_screenDown.c
    ui_screenDashboard.c
    ui_screenSerial.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_mty_noche_png.c
    ui_img_mty_amanecer_png.c
    ui_img_mty_dia_png.c
    ui_img_mty_atardecer_png.c
    fonts/ui_font_fontBig.c
    fonts/ui_font_fontMedium.c
    fonts/ui_font_fontSmall.c)

add_library(ui ${SOURCES})
