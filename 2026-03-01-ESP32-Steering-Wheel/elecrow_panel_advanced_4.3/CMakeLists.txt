SET(SOURCES ui_ScreenDriving.c
    ui_ScreenControls.c
    ui_ScreenTruck.c
    ui_ScreenJob.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    ui_img_headlights_png.c
    ui_img_headlights_beam_png.c
    ui_img_wipers_png.c
    ui_img_alert_triangle_png.c
    fonts/ui_font_FontDataMedium.c
    fonts/ui_font_FontSpeedHuge.c
    fonts/ui_font_FontUILarge.c
    fonts/ui_font_FontUIRegular.c
    fonts/ui_font_FontUISmall.c
    fonts/ui_font_FontUITiny.c)

add_library(ui ${SOURCES})
