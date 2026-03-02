#include "pins_config.h"
#include "LovyanGFX_Driver.h"
#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <TCA9534.h>
#include "ui.h"
#include "soc/rtc_cntl_reg.h"

extern "C" { extern const lv_img_dsc_t ui_img_logo_redondeado_png; }

// --- ESTRUCTURA DE DATOS 100% SINCRONIZADA ---
struct __attribute__((packed)) TelemetryPacket {
    uint8_t  magic;       // 0: 0xA5
    uint16_t speed;       // 1-2: MPH * 10
    uint16_t rpm;         // 3-4
    int8_t   gear;        // 5
    uint8_t  fuel;        // 6: 0-100%
    uint32_t dist;        // 7-10: Miles
    uint8_t  wear[5];     // 11-15: E, T, C, S, W
    uint8_t  cargoDamage; // 16
    uint8_t  adas[4];     // 17-20: L, H, W, Z
    uint32_t income;      // 21-24
    uint8_t  padding[6];  // 25-30
    uint8_t  checksum;    // 31
};

struct __attribute__((packed)) LogisticsPacket {
    uint8_t magic; // 0xB5
    char cargo[64];
    char city[62];
    uint8_t checksum;
};

TelemetryPacket tPkt;
LogisticsPacket lPkt;
TCA9534 ioex; LGFX gfx;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf; static lv_color_t *buf1;

lv_obj_t * boot_bg; lv_obj_t * boot_arc_outer; lv_obj_t * boot_arc_inner;
lv_obj_t * boot_label; lv_obj_t * boot_logo_clip; lv_obj_t * boot_logo;
lv_obj_t * boot_scanline; lv_obj_t * boot_glow_trail;

struct {
    float speed = 0; int rpm = 0; int gear = 0; int fuel_pct = 0; uint32_t dist_mi = 0;
    int wear[5] = {0,0,0,0,0}; int cargoDamage = 0;
    bool lightsBeam = false; bool lightsHigh = false; bool wipers = false; bool hazards = false;
    char cargo[64] = "---"; char city[64] = "---"; long income = 0;
} truckData;

void update_damage_color(lv_obj_t* obj, int wear) {
    if(!obj) return;
    lv_color_t c = (wear < 10) ? lv_color_hex(0x00C14F) : (wear < 25) ? lv_color_hex(0xFFFF00) : lv_color_hex(0xFF0000);
    lv_obj_set_style_border_color(obj, c, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_opa(obj, 255, 0);
}

void format_currency(char* buf, long value) {
    if (value < 1000) sprintf(buf, "$%ld", value);
    else if (value < 1000000) sprintf(buf, "$%ld,%03ld", value / 1000, value % 1000);
    else sprintf(buf, "$%ld,%03ld,%03ld", value / 1000000, (value % 1000000) / 1000, value % 1000);
}

void sync_ui_forced() {
    if(ui_SpeedArc) lv_arc_set_value(ui_SpeedArc, (int)truckData.speed);
    if(ui_SpeedValueLabel) lv_label_set_text_fmt(ui_SpeedValueLabel, "%d", (int)truckData.speed);
    if(ui_RPMValueLabel) lv_label_set_text_fmt(ui_RPMValueLabel, "%d", truckData.rpm);
    if(ui_Label9) lv_label_set_text_fmt(ui_Label9, "%d%%", truckData.fuel_pct);
    if(ui_DistLabel) lv_label_set_text_fmt(ui_DistLabel, "%d mi", truckData.dist_mi);
    if(ui_Label10) {
        if (truckData.gear == 0) lv_label_set_text(ui_Label10, "N");
        else if (truckData.gear < 0) lv_label_set_text_fmt(ui_Label10, "R%d", abs(truckData.gear));
        else lv_label_set_text_fmt(ui_Label10, "%d", truckData.gear);
    }
    if(ui_ImgLights) lv_obj_set_style_img_recolor(ui_ImgLights, truckData.lightsBeam ? lv_color_hex(0x00D4FF) : lv_color_hex(0x333333), 0);
    if(ui_ImgBeam) lv_obj_set_style_img_recolor(ui_ImgBeam, truckData.lightsHigh ? lv_color_hex(0x00D4FF) : lv_color_hex(0x333333), 0);
    if(ui_ImgWipers) lv_obj_set_style_img_recolor(ui_ImgWipers, truckData.wipers ? lv_color_hex(0x00D4FF) : lv_color_hex(0x333333), 0);
    if(ui_ImgHazards) lv_obj_set_style_img_recolor(ui_ImgHazards, truckData.hazards ? lv_color_hex(0xFF3C3C) : lv_color_hex(0x333333), 0);

    if(ui_LabelR) {
        bool active = (truckData.gear < 0);
        lv_obj_set_style_text_color(ui_LabelR, active ? lv_color_hex(0x00D4FF) : lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_opa(ui_LabelR, active ? 255 : 150, 0);
        lv_obj_set_style_text_font(ui_LabelR, active ? &ui_font_FontSpeedHuge : &ui_font_FontDataMedium, 0);
    }
    if(ui_LabelN) {
        bool active = (truckData.gear == 0);
        lv_obj_set_style_text_color(ui_LabelN, active ? lv_color_hex(0x00D4FF) : lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_opa(ui_LabelN, active ? 255 : 150, 0);
        lv_obj_set_style_text_font(ui_LabelN, active ? &ui_font_FontSpeedHuge : &ui_font_FontDataMedium, 0);
    }
    if(ui_LabelD) {
        bool active = (truckData.gear > 0);
        lv_obj_set_style_text_color(ui_LabelD, active ? lv_color_hex(0x00D4FF) : lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_opa(ui_LabelD, active ? 255 : 150, 0);
        lv_obj_set_style_text_font(ui_LabelD, active ? &ui_font_FontSpeedHuge : &ui_font_FontDataMedium, 0);
    }

    if(ui_ScreenTruck && lv_scr_act() == ui_ScreenTruck) {
        if(ui_MotorValue) lv_label_set_text_fmt(ui_MotorValue, "%d%%", truckData.wear[0]);
        if(ui_TransValue) lv_label_set_text_fmt(ui_TransValue, "%d%%", truckData.wear[1]);
        if(ui_CabinValue) lv_label_set_text_fmt(ui_CabinValue, "%d%%", truckData.wear[2]);
        if(ui_ChassisValue) lv_label_set_text_fmt(ui_ChassisValue, "%d%%", truckData.wear[3]);
        if(ui_TrailerValue) lv_label_set_text_fmt(ui_TrailerValue, "%d%%", truckData.wear[4]);
        update_damage_color(ui_EngineBlock, truckData.wear[0]);
        update_damage_color(ui_TransBlock, truckData.wear[1]);
        update_damage_color(ui_CabinBody, truckData.wear[2]);
        update_damage_color(ui_ChassisBody, truckData.wear[3]);
    }

    if(ui_cargoValue) lv_label_set_text(ui_cargoValue, truckData.cargo);
    if(ui_destValue) lv_label_set_text(ui_destValue, truckData.city);
    if(ui_rewardValue) { char money[32]; format_currency(money, truckData.income); lv_label_set_text(ui_rewardValue, money); }
    if(ui_damageValue) lv_label_set_text_fmt(ui_damageValue, "%d%%", truckData.cargoDamage);
}

void loop() {
  lv_timer_handler(); 
  uint32_t start_read = millis();
  while (Serial1.available() && (millis() - start_read < 10)) {
    uint8_t m = Serial1.read();
    if (m == 0xA5) {
      uint8_t raw[31];
      size_t n = Serial1.readBytes(raw, 31);
      if (n == 31) {
        memcpy(((uint8_t*)&tPkt) + 1, raw, 31);
        truckData.speed = tPkt.speed / 10.0;
        truckData.rpm = tPkt.rpm;
        truckData.gear = tPkt.gear;
        truckData.fuel_pct = tPkt.fuel;
        truckData.dist_mi = tPkt.dist;
        for(int i=0; i<5; i++) truckData.wear[i] = tPkt.wear[i];
        truckData.cargoDamage = tPkt.cargoDamage;
        truckData.lightsBeam = (tPkt.adas[0] == 1);
        truckData.lightsHigh = (tPkt.adas[1] == 1);
        truckData.wipers = (tPkt.adas[2] == 1);
        truckData.hazards = (tPkt.adas[3] == 1);
        truckData.income = tPkt.income;
        sync_ui_forced();
        if (millis() % 500 < 20) Serial1.println("{\"dbg\":\"PKT_A5_OK\"}");
      }
    } else if (m == 0xB5) {
      uint8_t raw[127];
      size_t n = Serial1.readBytes(raw, 127);
      if (n == 127) {
        memcpy(((uint8_t*)&lPkt) + 1, raw, 127);
        char tmp_cargo[65]; memcpy(tmp_cargo, lPkt.cargo, 64); tmp_cargo[64] = '\0';
        char tmp_city[63]; memcpy(tmp_city, lPkt.city, 62); tmp_city[62] = '\0';
        strncpy(truckData.cargo, tmp_cargo, 63);
        strncpy(truckData.city, tmp_city, 61);
        sync_ui_forced();
        Serial1.println("{\"dbg\":\"PKT_B5_OK\"}");
      }
    }
  }
}

void boot_master_cb(void * var, int32_t v) {
    lv_arc_set_angles(boot_arc_outer, (v*2) % 360, ((v*2) + 120) % 360);
    lv_arc_set_angles(boot_arc_inner, (1000 - v*2) % 360, (1000 - v*2 + 180) % 360);
    int scan_y = (v * 480) / 1000;
    lv_obj_set_y(boot_scanline, scan_y);
    lv_obj_set_y(boot_glow_trail, scan_y - 8);
    int logo_top = 120;
    if (scan_y > logo_top) {
        int h = scan_y - logo_top;
        lv_obj_set_height(boot_logo_clip, (h > 200) ? 200 : h);
    }
}

void start_boot_animation() {
    boot_bg = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_bg, lv_color_hex(0x010203), 0);
    lv_disp_load_scr(boot_bg);
    boot_arc_outer = lv_arc_create(boot_bg); lv_obj_set_size(boot_arc_outer, 360, 360);
    lv_obj_align(boot_arc_outer, LV_ALIGN_CENTER, 0, -20); lv_arc_set_bg_angles(boot_arc_outer, 0, 360);
    lv_obj_set_style_arc_width(boot_arc_outer, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(boot_arc_outer, lv_color_hex(0x00D4FF), LV_PART_INDICATOR);
    lv_obj_remove_style(boot_arc_outer, NULL, LV_PART_KNOB);
    boot_arc_inner = lv_arc_create(boot_bg); lv_obj_set_size(boot_arc_inner, 310, 310);
    lv_obj_align(boot_arc_inner, LV_ALIGN_CENTER, 0, -20); lv_arc_set_bg_angles(boot_arc_inner, 0, 360);
    lv_obj_set_style_arc_width(boot_arc_inner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(boot_arc_inner, lv_color_hex(0x00D4FF), LV_PART_INDICATOR);
    lv_obj_remove_style(boot_arc_inner, NULL, LV_PART_KNOB);
    boot_logo_clip = lv_obj_create(boot_bg); lv_obj_set_size(boot_logo_clip, 200, 0); 
    lv_obj_set_pos(boot_logo_clip, 300, 120); lv_obj_set_style_bg_opa(boot_logo_clip, 0, 0);
    lv_obj_set_style_border_width(boot_logo_clip, 0, 0); lv_obj_clear_flag(boot_logo_clip, LV_OBJ_FLAG_SCROLLABLE);
    boot_logo = lv_img_create(boot_logo_clip); lv_img_set_src(boot_logo, &ui_img_logo_redondeado_png);
    lv_obj_set_pos(boot_logo, 0, 0); 
    boot_glow_trail = lv_obj_create(boot_bg); lv_obj_set_size(boot_glow_trail, 800, 20);
    lv_obj_set_style_bg_color(boot_glow_trail, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_opa(boot_glow_trail, 60, 0); lv_obj_set_style_border_width(boot_glow_trail, 0, 0);
    boot_scanline = lv_obj_create(boot_bg); lv_obj_set_size(boot_scanline, 800, 4);
    lv_obj_set_style_bg_color(boot_scanline, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_color(boot_scanline, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_shadow_width(boot_scanline, 20, 0); lv_obj_set_style_border_width(boot_scanline, 0, 0);
    boot_label = lv_label_create(boot_bg); lv_label_set_text(boot_label, "INICIANDO NUCLEO...");
    lv_obj_set_style_text_font(boot_label, &ui_font_FontUILarge, 0);
    lv_obj_set_style_text_color(boot_label, lv_color_hex(0x00D4FF), 0); lv_obj_align(boot_label, LV_ALIGN_CENTER, 0, 190);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, boot_bg); lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_time(&a, 4000); lv_anim_set_exec_cb(&a, boot_master_cb); lv_anim_start(&a);
    uint32_t st = millis();
    while(millis() - st < 4500) { lv_timer_handler(); while(Serial1.available()) Serial1.read(); delay(2); }
    lv_disp_load_scr(ui_ScreenDriving);
    lv_timer_handler(); delay(100); lv_obj_del(boot_bg); 
}

void setup() {
  // CONFIGURACIÓN UART1 FÍSICA (PUERTO HY2.0: RX=19, TX=20)
  Serial1.setRxBufferSize(1024);
  Serial1.begin(115200, SERIAL_8N1, 19, 20); 
  Serial1.setTimeout(10);

  Serial.begin(115200); 
  Serial.println("{\"dbg\":\"S3_CORE_READY\"}");

  Wire.begin(15, 16); 
 ioex.attach(Wire); ioex.setDeviceAddress(0x18);
  ioex.config(1, TCA9534::Config::OUT); ioex.output(1, TCA9534::Level::H); 
  pinMode(1, OUTPUT); digitalWrite(1, LOW);
  gfx.init(); gfx.initDMA(); gfx.startWrite(); lv_init();
  size_t bs = sizeof(lv_color_t) * LCD_H_RES * LCD_V_RES;
  buf = (lv_color_t *)heap_caps_malloc(bs, MALLOC_CAP_SPIRAM);
  buf1 = (lv_color_t *)heap_caps_malloc(bs, MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&draw_buf, buf, buf1, LCD_H_RES * LCD_V_RES);
  static lv_disp_drv_t drv; lv_disp_drv_init(&drv);
  drv.hor_res = LCD_H_RES; drv.ver_res = LCD_V_RES;
  drv.flush_cb = my_disp_flush; drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&drv);
  static lv_indev_drv_t idrv; lv_indev_drv_init(&idrv);
  idrv.type = LV_INDEV_TYPE_POINTER; idrv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&idrv);
  ui_init();
  if(ui_LabelR) { lv_obj_add_flag(ui_LabelR, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(ui_LabelR, ui_event_LabelR, LV_EVENT_CLICKED, NULL); }
  if(ui_LabelN) { lv_obj_add_flag(ui_LabelN, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(ui_LabelN, ui_event_LabelN, LV_EVENT_CLICKED, NULL); }
  if(ui_LabelD) { lv_obj_add_flag(ui_LabelD, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(ui_LabelD, ui_event_LabelD, LV_EVENT_CLICKED, NULL); }
  start_boot_animation();
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  gfx.pushImageDMA(area->x1, area->y1, (area->x2-area->x1+1), (area->y2-area->y1+1), (lgfx::rgb565_t *)&color_p->full);
  lv_disp_flush_ready(disp);
}
void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
  uint16_t x, y; data->state = LV_INDEV_STATE_REL;
  if (gfx.getTouch(&x, &y)) { data->state = LV_INDEV_STATE_PR; data->point.x = x; data->point.y = y; }
}

extern "C" {
void uiEventLights(lv_event_t * e) { Serial1.println("{\"cmd\":\"lights\"}"); }
void uiEventHighBeam(lv_event_t * e) { Serial1.println("{\"cmd\":\"highbeam\"}"); }
void uiEventWipers(lv_event_t * e) { Serial1.println("{\"cmd\":\"wipers\"}"); }
void uiEventHazards(lv_event_t * e) { Serial1.println("{\"cmd\":\"hazards\"}"); }
void uiEventGearR(lv_event_t * e) { Serial1.println("{\"cmd\":\"gear_R\"}"); }
void uiEventGearN(lv_event_t * e) { Serial1.println("{\"cmd\":\"gear_N\"}"); }
void uiEventGearD(lv_event_t * e) { Serial1.println("{\"cmd\":\"gear_D\"}"); }
}
