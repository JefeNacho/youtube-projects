#include <Arduino.h>
#include <esp_heap_caps.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include "secrets.h"
#include <lvgl.h>
#include <TCA9534.h>
#include "pins_config.h"
#include "LovyanGFX_Driver.h"
#include "audio_synth.h"
#include "mic_capture.h"
#include "lvgl_fs_driver.h"

/*
 * GUI GENERADO POR SQUARELINE STUDIO
 */
#include "src/ui/ui.h"

// --- CONFIGURACION DE PROTOCOLO BINARIO ---
#define MAGIC_ATMOS 0xA5
#define MAGIC_SYS   0xB5

struct __attribute__((packed)) AtmosPacket {
    uint8_t  magic;   // 0xA5
    float    temp;    // 4 bytes
    float    hum;     // 4 bytes
    int      aqi;     // 4 bytes
    uint8_t  weather; // 0=Clear, 1=Clouds, 2=Rain
    uint8_t  checksum;
};

struct __attribute__((packed)) SysPacket {
    uint8_t  magic;   // 0xB5
    uint8_t  cpu;     // %
    uint8_t  gpu;     // %
    uint16_t ram_mb;
    char     status[32];
    uint8_t  checksum;
};

#define MAGIC_COM        0xC5
#define MAGIC_AUDIO      0xD5
#define MAGIC_VOICE_STATE 0xF5   // PC → ESP32: 1 byte estado (0=IDLE 1=LISTEN 2=THINK 3=SPEAK)
#define AUDIO_CHUNK_BYTES 1024   // 512 samples × 2 bytes, 16-bit mono
struct __attribute__((packed)) ComPacket {
    uint8_t  magic;     // 0xC5
    uint8_t  count;     // number of ports
    uint8_t  pad;       // 0
    char     ports[255]; // comma separated string
    uint8_t  checksum;
};

// --- GLOBALES ---
TCA9534 ioex;
LGFX gfx;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;
static lv_color_t *buf1;
SemaphoreHandle_t i2c_mutex = NULL;

// Globales para Hilo Seguro LVGL
char global_ports_list[255] = "";
volatile bool new_ports_available = false;

// Ring buffer SPSC para mensajes del terminal
// Core 0 escribe (head), Core 1 lee (tail) — sin mutex necesario
#define TERM_QUEUE_DEPTH 64
static char            term_queue[TERM_QUEUE_DEPTH][256];
static uint8_t         term_queue_terminal[TERM_QUEUE_DEPTH]; // terminal index por mensaje
static volatile int    term_q_head = 0;
static volatile int    term_q_tail = 0;

// --- SENTINEL MONITOR: MULTI-TERMINAL ---
#define MAX_TERMINALS    4
#define TERM_TEXT_LIMIT  4000

lv_obj_t* terminal_areas[MAX_TERMINALS]  = {NULL};
lv_obj_t* terminal_texts[MAX_TERMINALS]  = {NULL};
int active_terminal_count = 1; // 1, 2, o 4

// Total line counter para la status bar
static uint32_t total_line_count = 0;

// --- SENTINEL: PAUSE / RESUME ---
volatile bool terminal_paused       = false;
volatile int  pending_while_paused  = 0;

// --- SENTINEL: RECORDING STATE ---
volatile bool is_recording = false;
static uint32_t rec_start_millis = 0;

// --- SENTINEL: PORT PICKER ---
static char     terminal_port[MAX_TERMINALS][32] = {{0}}; // puerto asignado a cada terminal
static int      picker_target_term = -1;  // terminal para el que estamos eligiendo puerto
static lv_obj_t* picker_modal     = NULL;
static int      picker_next_term  = -1;   // proximo terminal en cola (para 2→4 que agrega T2 y T3)

// --- SENTINEL: FILTER ---
// 0=ALL, 1=ERR, 2=WARN, 3=INFO
volatile uint8_t current_filter = 0;
static const char* filter_labels[] = {"ALL", "ERR", "WARN", "INFO"};
static const char* filter_patterns[] = {"", "[ERR", "[WARN", "[INFO"};

// --- SENTINEL: HEADER BUTTONS ---
static lv_obj_t* pause_btn      = NULL;
static lv_obj_t* pause_lbl      = NULL;
static lv_obj_t* rec_btn        = NULL;
static lv_obj_t* rec_lbl        = NULL;
static lv_obj_t* filter_btn     = NULL;
static lv_obj_t* filter_lbl     = NULL;
static lv_obj_t* split_btn      = NULL;
static lv_obj_t* split_lbl      = NULL;
static lv_obj_t* clear_btn      = NULL;
static lv_obj_t* usb_btn        = NULL;

// --- SENTINEL: STATUS BAR ---
static lv_obj_t* status_bar          = NULL;
static lv_obj_t* status_line_count_lbl = NULL;
static lv_obj_t* status_rec_indicator  = NULL;
static lv_obj_t* status_elapsed_lbl    = NULL;

// --- GESTION DE MEMORIA PSRAM ---
extern "C" {
    void ui_img_mty_noche_png_load();
    void ui_img_mty_amanecer_png_load();
    void ui_img_mty_dia_png_load();
    void ui_img_mty_atardecer_png_load();
}


// ── Voz: estado actual del pipeline (actualizado por 0xF5 desde el daemon) ──
volatile uint8_t voice_state = 0;   // 0=IDLE 1=LISTENING 2=THINKING 3=SPEAKING

// BLE UART — incluir DESPUÉS de todas las declaraciones globales para que
// BLETask pueda acceder a: TERM_QUEUE_DEPTH, term_queue, term_q_head/tail,
// rec_start_millis, is_recording, voice_state, global_ports_list, etc.
#include "ble_uart.h"

// Task Handles
TaskHandle_t TaskSerialHandle = NULL;

// --- FUNCIONES HARDWARE ---
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    if (gfx.getStartCount() == 0) return;
    gfx.pushImageDMA(area->x1, area->y1, (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1), (lgfx::rgb565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t * idrv, lv_indev_data_t * data) {
    uint16_t x, y;
    bool touched = false;

    // Adquirir Mutex antes de leer el tactil (GT911 comparte el bus I2C)
    if (i2c_mutex != NULL && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        touched = gfx.getTouch(&x, &y);
        xSemaphoreGive(i2c_mutex);
    }

    data->state = LV_INDEV_STATE_REL;
    if (touched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    }
}

// Sonido de sistema solo para botones LVGL
void my_buzzer_feedback(lv_indev_drv_t * idrv, uint8_t e) {
    if (e == LV_EVENT_CLICKED) {
        lv_obj_t * act = lv_indev_get_obj_act();
        if(act && (lv_obj_check_type(act, &lv_btn_class) || lv_obj_check_type(act, &lv_imgbtn_class))) {
            tone(BUZZER_PIN, 2000, 30);
        }
    }
}

// Forward declaration — necesaria porque my_dash_btn_event llama rebuild_terminal_layout
void rebuild_terminal_layout(int count);

// Dash Button (desde Serial): limpiar terminales dinamicos y volver sin animacion
static void my_dash_btn_event(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    // Desconectar todos los terminales activos (T1 en adelante) al salir
    for (int i = 1; i < active_terminal_count; i++) {
        if (terminal_port[i][0] != '\0') {
            Serial.printf("MONITOR:T%d:NONE\n", i);
            Serial.flush();
            if (ble_is_connected()) {
                char _ble_resp[32];
                snprintf(_ble_resp, sizeof(_ble_resp), "MONITOR:T%d:NONE\n", i);
                ble_uart_send(_ble_resp);
            }
            terminal_port[i][0] = '\0';
        }
    }
    // Cerrar picker si estaba abierto
    if (picker_modal) { lv_obj_del(picker_modal); picker_modal = NULL; }
    picker_target_term = -1;
    picker_next_term   = -1;
    rebuild_terminal_layout(1); // destruir terminales 1-3 antes de salir
    if (split_lbl) lv_label_set_text(split_lbl, "1x");
    _ui_screen_change(&ui_screenDashboard, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_screenDashboard_screen_init);
}

// Hub Button: navegar al screen correcto segun la hora actual
static void my_hub_btn_event(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    struct tm timeinfo;
    lv_obj_t** target = &ui_screenNight;
    void (*init_fn)(void) = ui_screenNight_screen_init;
    if (getLocalTime(&timeinfo)) {
        int h = timeinfo.tm_hour;
        if      (h >= 6  && h < 9)  { target = &ui_screenMorning; init_fn = ui_screenMorning_screen_init; }
        else if (h >= 9  && h < 18) { target = &ui_screenDay;     init_fn = ui_screenDay_screen_init; }
        else if (h >= 18 && h < 21) { target = &ui_screenDown;    init_fn = ui_screenDown_screen_init; }
    }
    _ui_screen_change(target, LV_SCR_LOAD_ANIM_NONE, 0, 0, init_fn);
}

// Liberar memoria al borrar una tarjeta
static void free_user_data_event(lv_event_t * e) {
    char * data = (char *)lv_event_get_user_data(e);
    if(data) free(data);
}

// Click Event para las Tarjetas COM dinamicas
static void my_com_card_event(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    char * port_meta = (char *)lv_event_get_user_data(e);

    if(code == LV_EVENT_CLICKED && port_meta) {
        tone(BUZZER_PIN, 2000, 30);
        Serial.printf("MONITOR:%s\n", port_meta);
        if (ble_is_connected()) {
            char _ble_resp[64];
            snprintf(_ble_resp, sizeof(_ble_resp), "MONITOR:%s\n", port_meta);
            ble_uart_send(_ble_resp);
        }

        // Registrar puerto de T0 para que sea visible en el label
        strncpy(terminal_port[0], port_meta, 31);
        terminal_port[0][31] = '\0';

        if(ui_monitorTitle) {
            lv_label_set_text_fmt(ui_monitorTitle, "SENTINEL [%s]", port_meta);
        }
        if(ui_terminalText) {
            if (strncmp(port_meta, "PY CH", 5) == 0) {
                int ch_num = atoi(port_meta + 5);
                lv_label_set_text_fmt(ui_terminalText,
                    "> T0: %s\n> [%s] Activo.\n> Usa logger.set_terminal(%d)\n",
                    port_meta, port_meta, ch_num);
            } else {
                lv_label_set_text_fmt(ui_terminalText, "> T0: %s\n> Conectando...\n", port_meta);
            }
        }
        _ui_screen_change(&ui_screenSerial, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_screenSerial_screen_init);
        Serial.flush();
    }
}

// =====================================================================
// SENTINEL MONITOR — HEADER BUTTON CALLBACKS
// =====================================================================

static void pause_btn_event(lv_event_t * e) {
    terminal_paused = !terminal_paused;
    lv_obj_t* lbl = lv_obj_get_child(lv_event_get_target(e), 0);
    if (terminal_paused) {
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLAY);
    } else {
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PAUSE);
        // Al reanudar, el loop() procesara los pendientes normalmente
        pending_while_paused = 0;
    }
}

static void rec_btn_event(lv_event_t * e) {
    if (!is_recording) {
        Serial.println("SENTINEL:REC_START");
        Serial.flush();
        if (ble_is_connected()) ble_uart_send("SENTINEL:REC_START\n");
        // El estado real se actualiza al recibir S:REC del daemon
    } else {
        Serial.println("SENTINEL:REC_STOP");
        Serial.flush();
        if (ble_is_connected()) ble_uart_send("SENTINEL:REC_STOP\n");
        // El estado real se actualiza al recibir S:STOP del daemon
    }
}

static void filter_btn_event(lv_event_t * e) {
    current_filter = (current_filter + 1) % 4;
    lv_obj_t* lbl = lv_obj_get_child(lv_event_get_target(e), 0);
    if (lbl) lv_label_set_text(lbl, filter_labels[current_filter]);
    // Enviar comando al daemon
    Serial.printf("SENTINEL:FILTER:%s\n", filter_labels[current_filter]);
    Serial.flush();
    if (ble_is_connected()) {
        char _ble_resp[32];
        snprintf(_ble_resp, sizeof(_ble_resp), "SENTINEL:FILTER:%s\n", filter_labels[current_filter]);
        ble_uart_send(_ble_resp);
    }
}

// Forward declaration para que los callbacks puedan llamar show_port_picker
static void show_port_picker(int term_idx);

static void port_picker_item_event(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    char* port = (char*)lv_event_get_user_data(e);
    int   idx  = picker_target_term;
    if (port && idx >= 0 && idx < MAX_TERMINALS) {
        strncpy(terminal_port[idx], port, 31);
        terminal_port[idx][31] = '\0';
        Serial.printf("MONITOR:T%d:%s\n", idx, port);
        Serial.flush();
        if (ble_is_connected()) {
            char _ble_resp[64];
            snprintf(_ble_resp, sizeof(_ble_resp), "MONITOR:T%d:%s\n", idx, port);
            ble_uart_send(_ble_resp);
        }
        tone(BUZZER_PIN, 1500, 20);
        // Actualizar label del terminal para indicar el puerto asignado
        if (terminal_texts[idx]) {
            if (strncmp(port, "PY CH", 5) == 0) {
                int ch_num = atoi(port + 5);
                lv_label_set_text_fmt(terminal_texts[idx],
                    "> T%d: %s\n> [%s] Activo.\n> Usa logger.set_terminal(%d)\n",
                    idx, port, port, ch_num);
            } else {
                lv_label_set_text_fmt(terminal_texts[idx], "> T%d: %s\n> Conectando...\n", idx, port);
            }
        }
    }
    if (picker_modal) { lv_obj_del(picker_modal); picker_modal = NULL; }
    picker_target_term = -1;
    // Si hay otro terminal en cola (caso 2→4), mostrarlo ahora
    if (picker_next_term >= 0) {
        int next = picker_next_term;
        picker_next_term = -1;
        show_port_picker(next);
    }
}

// Ninguno: el terminal existe pero no escucha ningun puerto
static void port_picker_none_event(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = picker_target_term;
    if (idx >= 0 && idx < MAX_TERMINALS) {
        terminal_port[idx][0] = '\0';
        if (terminal_texts[idx]) {
            lv_label_set_text_fmt(terminal_texts[idx], "> T%d: — | Inactivo\n", idx);
        }
    }
    if (picker_modal) { lv_obj_del(picker_modal); picker_modal = NULL; }
    picker_target_term = -1;
    if (picker_next_term >= 0) {
        int next = picker_next_term;
        picker_next_term = -1;
        show_port_picker(next);
    }
}

static void port_picker_cancel_event(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (picker_modal) { lv_obj_del(picker_modal); picker_modal = NULL; }
    picker_target_term = -1;
    // Si hay otro terminal en cola, igualmente procesarlo
    if (picker_next_term >= 0) {
        int next = picker_next_term;
        picker_next_term = -1;
        show_port_picker(next);
    }
}

static void show_port_picker(int term_idx) {
    // Eliminar picker previo si existe
    if (picker_modal) { lv_obj_del(picker_modal); picker_modal = NULL; }

    // --- Contenedor externo: columna fija, no scrollable ---
    picker_modal = lv_obj_create(ui_screenSerial);
    lv_obj_set_size(picker_modal, 380, 390);
    lv_obj_center(picker_modal);
    lv_obj_set_style_bg_color(picker_modal, lv_color_hex(0x0D1636), 0);
    lv_obj_set_style_bg_opa(picker_modal, 255, 0);
    lv_obj_set_style_border_color(picker_modal, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_border_width(picker_modal, 1, 0);
    lv_obj_set_style_border_opa(picker_modal, 255, 0);
    lv_obj_set_style_radius(picker_modal, 8, 0);
    lv_obj_set_style_pad_all(picker_modal, 10, 0);
    lv_obj_set_style_pad_row(picker_modal, 6, 0);
    lv_obj_set_flex_flow(picker_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(picker_modal, LV_OBJ_FLAG_SCROLLABLE);

    // Titulo
    lv_obj_t* title = lv_label_create(picker_modal);
    lv_label_set_text_fmt(title, "Terminal T%d — Seleccionar puerto", term_idx);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(title, &ui_font_fontSmall, 0);

    // Separador
    lv_obj_t* sep = lv_obj_create(picker_modal);
    lv_obj_set_size(sep, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_bg_opa(sep, 60, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

    // --- Area scrollable para la lista de puertos (ocupa el espacio restante) ---
    lv_obj_t* scroll_area = lv_obj_create(picker_modal);
    lv_obj_set_size(scroll_area, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(scroll_area, 1);   // toma todo el espacio libre entre titulo y cancel
    lv_obj_set_style_bg_opa(scroll_area, 0, 0);
    lv_obj_set_style_border_width(scroll_area, 0, 0);
    lv_obj_set_style_pad_all(scroll_area, 0, 0);
    lv_obj_set_style_pad_row(scroll_area, 6, 0);
    lv_obj_set_flex_flow(scroll_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(scroll_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll_area, LV_SCROLLBAR_MODE_AUTO);

    // Boton Ninguno — siempre al tope de la lista scrollable
    lv_obj_t* none_btn = lv_btn_create(scroll_area);
    lv_obj_set_size(none_btn, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(none_btn, lv_color_hex(0x0A0A14), 0);
    lv_obj_set_style_bg_opa(none_btn, 255, 0);
    lv_obj_set_style_border_color(none_btn, lv_color_hex(0x444466), 0);
    lv_obj_set_style_border_width(none_btn, 1, 0);
    lv_obj_set_style_radius(none_btn, 4, 0);
    lv_obj_t* none_lbl = lv_label_create(none_btn);
    lv_label_set_text(none_lbl, "— Ninguno (inactivo)");
    lv_obj_set_style_text_color(none_lbl, lv_color_hex(0x888899), 0);
    lv_obj_set_style_text_font(none_lbl, &ui_font_fontSmall, 0);
    lv_obj_center(none_lbl);
    lv_obj_add_event_cb(none_btn, port_picker_none_event, LV_EVENT_CLICKED, NULL);

    // Botones de puertos disponibles (dentro del scroll_area)
    char temp_list[255];
    strncpy(temp_list, global_ports_list, 254);
    temp_list[254] = '\0';
    char* token = strtok(temp_list, ",");
    while (token != NULL) {
        char* port_copy = (char*)malloc(strlen(token) + 1);
        if (port_copy) {
            strcpy(port_copy, token);
            lv_obj_t* btn = lv_btn_create(scroll_area);
            lv_obj_set_size(btn, LV_PCT(100), 40);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
            lv_obj_set_style_bg_opa(btn, 255, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x00FFCC), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_opa(btn, 80, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, port_copy);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(lbl, &ui_font_fontSmall, 0);
            lv_obj_center(lbl);
            lv_obj_set_user_data(btn, port_copy);
            lv_obj_add_event_cb(btn, port_picker_item_event,  LV_EVENT_CLICKED, port_copy);
            lv_obj_add_event_cb(btn, free_user_data_event,    LV_EVENT_DELETE,  port_copy);
        }
        token = strtok(NULL, ",");
    }

    // --- Boton Cancelar: fuera del scroll, siempre visible al fondo ---
    lv_obj_t* cancel_btn = lv_btn_create(picker_modal);
    lv_obj_set_size(cancel_btn, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x200808), 0);
    lv_obj_set_style_bg_opa(cancel_btn, 255, 0);
    lv_obj_set_style_border_color(cancel_btn, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_border_width(cancel_btn, 1, 0);
    lv_obj_set_style_radius(cancel_btn, 4, 0);
    lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancelar");
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_font(cancel_lbl, &ui_font_fontSmall, 0);
    lv_obj_center(cancel_lbl);
    lv_obj_add_event_cb(cancel_btn, port_picker_cancel_event, LV_EVENT_CLICKED, NULL);

    picker_target_term = term_idx;
}

static void split_btn_event(lv_event_t * e) {
    int next_count;
    if (active_terminal_count == 1)      next_count = 2;
    else if (active_terminal_count == 2) next_count = 4;
    else                                 next_count = 1;

    int prev_count = active_terminal_count;
    rebuild_terminal_layout(next_count);

    lv_obj_t* lbl = lv_obj_get_child(lv_event_get_target(e), 0);
    if (lbl) {
        if (next_count == 1)      lv_label_set_text(lbl, "1x");
        else if (next_count == 2) lv_label_set_text(lbl, "2x");
        else                      lv_label_set_text(lbl, "4x");
    }

    // Si estamos expandiendo, mostrar picker para los nuevos slots
    if (next_count > prev_count) {
        picker_next_term = -1;
        if (next_count == 2) {
            // 1→2: un solo terminal nuevo (T1)
            show_port_picker(1);
        } else if (next_count == 4) {
            // 2→4: dos terminales nuevos (T2 y T3), mostrar en secuencia
            picker_next_term = 3; // T3 se mostrara despues de que T2 sea seleccionado
            show_port_picker(2);
        }
    }
}

// =====================================================================
// SENTINEL MONITOR — MULTI-TERMINAL LAYOUT
// =====================================================================

static void clear_btn_event(lv_event_t * e) {
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminal_texts[i]) lv_label_set_text(terminal_texts[i], "");
    }
    total_line_count = 0;
    if (status_line_count_lbl) lv_label_set_text(status_line_count_lbl, "0 lines");
}

static bool usb_port_released = false;

static void usb_btn_event(lv_event_t * e) {
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);

    if (!usb_port_released) {
        // Primer press: liberar puerto.
        // Enviar por USB CDC y por BLE si está conectado — el daemon escucha
        // por el canal activo (uno u otro, no ambos simultáneamente).
        Serial.println("SENTINEL:RELEASE_PORT");
        if (ble_is_connected()) ble_uart_send("SENTINEL:RELEASE_PORT\n");
        usb_port_released = true;
        lv_obj_set_style_border_color(btn, lv_color_hex(0x555555), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
    } else {
        // Segundo press: reconectar.
        Serial.println("SENTINEL:RECONNECT_PORT");
        Serial.flush();
        if (ble_is_connected()) ble_uart_send("SENTINEL:RECONNECT_PORT\n");
        usb_port_released = false;
        lv_obj_set_style_border_color(btn, lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFAA00), 0);
    }
}

void rebuild_terminal_layout(int count) {
    // Destruir terminales dinamicos (1-3), nunca el 0 (SquareLine)
    for (int i = 1; i < MAX_TERMINALS; i++) {
        if (terminal_areas[i]) {
            lv_obj_del(terminal_areas[i]);
            terminal_areas[i] = NULL;
            terminal_texts[i] = NULL;
        }
    }
    active_terminal_count = count;

    // Dimensiones base del area de terminales
    // screenSerial es 800x480, header ~40px top, status bar 24px bottom
    // Area disponible: 800 wide, ~416 tall (y empieza en ~40)
    // Terminal 0 original tiene padding, asi que usamos coordenadas absolutas

    int area_w, area_h;
    int x0, y0; // posicion del terminal 0

    if (count == 1) {
        area_w = 736; area_h = 370;
        x0 = 32; y0 = 56;
    } else if (count == 2) {
        area_w = 736; area_h = 180;
        x0 = 32; y0 = 56;
    } else { // 4
        area_w = 364; area_h = 180;
        x0 = 16; y0 = 56;
    }

    // Resize terminal 0 (creado por SquareLine)
    if (ui_terminalArea) {
        lv_obj_set_align(ui_terminalArea, LV_ALIGN_TOP_LEFT);
        // CLICKABLE es obligatorio: sin el flag el input device nunca despacha
        // eventos de drag a este objeto (SquareLine lo elimina en la exportacion)
        lv_obj_add_flag(ui_terminalArea, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_scroll_dir(ui_terminalArea, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(ui_terminalArea, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_size(ui_terminalArea, area_w, area_h);
        lv_obj_set_pos(ui_terminalArea, x0, y0);
    }
    if (ui_terminalText) {
        lv_obj_set_width(ui_terminalText, area_w - 20);
        // El label no debe capturar el hit-test; el scroll lo maneja el contenedor
        lv_obj_clear_flag(ui_terminalText, LV_OBJ_FLAG_CLICKABLE);
    }

    // Crear terminales 1..(count-1) con el mismo estilo
    for (int i = 1; i < count; i++) {
        int tx, ty;
        if (count == 2) {
            // Stacked: terminal 1 debajo de terminal 0
            tx = x0;
            ty = y0 + area_h + 6;
        } else {
            // 2x2 grid
            // i=1: top-right, i=2: bottom-left, i=3: bottom-right
            int col = i % 2;
            int row = i / 2;
            tx = x0 + col * (area_w + 8);
            ty = y0 + row * (area_h + 6);
        }

        terminal_areas[i] = lv_obj_create(ui_screenSerial);
        lv_obj_set_size(terminal_areas[i], area_w, area_h);
        lv_obj_set_pos(terminal_areas[i], tx, ty);
        lv_obj_set_style_bg_color(terminal_areas[i], lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(terminal_areas[i], 100, 0);
        lv_obj_set_style_border_color(terminal_areas[i], lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_border_width(terminal_areas[i], 1, 0);
        lv_obj_set_style_border_opa(terminal_areas[i], 80, 0);
        lv_obj_set_style_radius(terminal_areas[i], 4, 0);
        lv_obj_set_style_pad_all(terminal_areas[i], 4, 0);
        lv_obj_add_flag(terminal_areas[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(terminal_areas[i], LV_DIR_VER);
        lv_obj_set_scrollbar_mode(terminal_areas[i], LV_SCROLLBAR_MODE_AUTO);

        terminal_texts[i] = lv_label_create(terminal_areas[i]);
        lv_obj_set_width(terminal_texts[i], area_w - 20);
        lv_label_set_long_mode(terminal_texts[i], LV_LABEL_LONG_WRAP);
        lv_obj_clear_flag(terminal_texts[i], LV_OBJ_FLAG_CLICKABLE);
        // Mostrar puerto asignado si ya tenia uno, o indicar que esta sin asignar
        if (terminal_port[i][0] != '\0') {
            if (strncmp(terminal_port[i], "PY CH", 5) == 0) {
                int ch_num = atoi(terminal_port[i] + 5);
                lv_label_set_text_fmt(terminal_texts[i],
                    "> T%d: %s\n> [%s] Activo.\n> Usa logger.set_terminal(%d)\n",
                    i, terminal_port[i], terminal_port[i], ch_num);
            } else {
                lv_label_set_text_fmt(terminal_texts[i], "> T%d: %s\n> Conectando...\n", i, terminal_port[i]);
            }
        } else {
            lv_label_set_text_fmt(terminal_texts[i], "> T%d: — | Sin asignar\n", i);
        }
        lv_label_set_recolor(terminal_texts[i], true);
        lv_obj_set_style_text_color(terminal_texts[i], lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_text_font(terminal_texts[i], &ui_font_fontSmall, 0);
    }
}

// =====================================================================
// SENTINEL MONITOR — HELPER: append text to a terminal (newest at bottom)
// =====================================================================

static void append_to_terminal(int term_idx, const char* msg) {
    if (term_idx < 0 || term_idx >= MAX_TERMINALS) term_idx = 0;

    lv_obj_t* text_obj = terminal_texts[term_idx];
    lv_obj_t* area_obj = terminal_areas[term_idx];
    if (!text_obj || !area_obj) return;

    // Aplicar filtro: si current_filter != 0 y la linea no contiene el patron, ignorar
    if (current_filter != 0) {
        if (strstr(msg, filter_patterns[current_filter]) == NULL) {
            return; // Linea filtrada — no mostrar
        }
    }

    const char* current = lv_label_get_text(text_obj);

    // Newest at bottom: append al final
    String updated = String(current) + String(msg) + "\n";

    // Si excede el limite, recortar desde el INICIO (drop oldest lines)
    if (updated.length() > TERM_TEXT_LIMIT) {
        int excess = updated.length() - TERM_TEXT_LIMIT;
        // Buscar el primer '\n' despues del exceso para cortar en linea completa
        int cut = updated.indexOf('\n', excess);
        if (cut >= 0) {
            updated = updated.substring(cut + 1);
        } else {
            updated = updated.substring(excess);
        }
    }

    lv_label_set_text(text_obj, updated.c_str());

    // Forzar recalculo de layout para que LVGL conozca la nueva altura
    lv_obj_update_layout(area_obj);

    // Sticky auto-scroll: solo bajar al fondo si el usuario ya estaba cerca del fondo.
    // Si scrolleo hacia arriba para leer, respetar su posicion.
    lv_coord_t child_h      = lv_obj_get_height(text_obj);
    lv_coord_t area_h       = lv_obj_get_content_height(area_obj);
    lv_coord_t max_scroll   = child_h - area_h;
    if (max_scroll > 0) {
        lv_coord_t cur_scroll = lv_obj_get_scroll_y(area_obj);
        // Auto-scroll solo si el usuario esta a 60px o menos del fondo
        if (max_scroll - cur_scroll <= 60) {
            lv_obj_scroll_to_y(area_obj, max_scroll, LV_ANIM_OFF);
        }
    }

    total_line_count++;
}

// =====================================================================
// SENTINEL MONITOR — CREAR HEADER BUTTONS Y STATUS BAR
// =====================================================================
// ESTRATEGIA: Si los widgets existen (creados en SquareLine y exportados
// como ui_pauseBtn, ui_recBtn, etc.), usamos esos. Si NO existen
// (aún no se han diseñado en SquareLine), los creamos dinámicamente
// como fallback temporal. Una vez creados en SquareLine, el fallback
// se puede eliminar.

static lv_obj_t* create_header_btn_fallback(lv_obj_t* parent, const char* text,
                                             lv_event_cb_t cb, int x_pos) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 52, 32);
    lv_obj_set_pos(btn, x_pos, 4);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(btn, 255, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 2, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(lbl, &ui_font_fontSmall, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void setup_sentinel_buttons() {
    if (!ui_topContainer) return;

    // --- BOTONES: Usar SquareLine si existen (check runtime), si no crear fallback ---

    // Pause button
    if (ui_pauseBtn) {
        pause_btn = ui_pauseBtn;
        pause_lbl = ui_lblPauseBtn;
        lv_obj_add_event_cb(pause_btn, pause_btn_event, LV_EVENT_CLICKED, NULL);
    } else {
        int base_x = 460;
        pause_btn = create_header_btn_fallback(ui_topContainer, LV_SYMBOL_PAUSE, pause_btn_event, base_x);
        pause_lbl = lv_obj_get_child(pause_btn, 0);
    }

    // REC button
    if (ui_recBtn) {
        rec_btn = ui_recBtn;
        rec_lbl = ui_lblRecBtn;
        lv_obj_add_event_cb(rec_btn, rec_btn_event, LV_EVENT_CLICKED, NULL);
    } else {
        rec_btn = create_header_btn_fallback(ui_topContainer, "REC", rec_btn_event, 518);
        rec_lbl = lv_obj_get_child(rec_btn, 0);
        lv_obj_set_style_text_color(rec_lbl, lv_color_hex(0x888888), 0);
    }

    // Filter button
    if (ui_filterBtn) {
        filter_btn = ui_filterBtn;
        filter_lbl = ui_lblFilterBtn;
        lv_obj_add_event_cb(filter_btn, filter_btn_event, LV_EVENT_CLICKED, NULL);
    } else {
        filter_btn = create_header_btn_fallback(ui_topContainer, "ALL", filter_btn_event, 576);
        filter_lbl = lv_obj_get_child(filter_btn, 0);
    }

    // Split button
    if (ui_splitBtn) {
        split_btn = ui_splitBtn;
        split_lbl = ui_lblSplitBtn;
        lv_obj_add_event_cb(split_btn, split_btn_event, LV_EVENT_CLICKED, NULL);
    } else {
        split_btn = create_header_btn_fallback(ui_topContainer, "1x", split_btn_event, 634);
        split_lbl = lv_obj_get_child(split_btn, 0);
    }

    // CLR button — limpiar todos los terminales
    if (ui_clearBtn) {
        clear_btn = ui_clearBtn;
        lv_obj_add_event_cb(clear_btn, clear_btn_event, LV_EVENT_CLICKED, NULL);
    } else {
        clear_btn = create_header_btn_fallback(ui_topContainer, "CLR", clear_btn_event, 692);
    }

    // USB button — liberar puerto para flashear
    // Estado inicial: conectado (borde cyan, texto naranja)
    usb_port_released = false;
    if (ui_usbBtn) {
        usb_btn = ui_usbBtn;
        lv_obj_add_event_cb(usb_btn, usb_btn_event, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_border_color(usb_btn, lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_text_color(ui_lblUsbBtn, lv_color_hex(0xFFAA00), 0);
    } else {
        usb_btn = create_header_btn_fallback(ui_topContainer, "USB", usb_btn_event, 746);
        lv_obj_set_style_border_color(usb_btn, lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(usb_btn, 0), lv_color_hex(0xFFAA00), 0);
    }
}

static void setup_sentinel_status_bar() {
    // --- STATUS BAR: Usar SquareLine si existe (check runtime), si no crear fallback ---
    if (ui_stsBar) {
        status_bar             = ui_stsBar;
        status_line_count_lbl  = ui_statusLineCount;
        status_rec_indicator   = ui_statusRecIndicator;
        status_elapsed_lbl     = ui_statusElapsed;
        lv_label_set_recolor(status_rec_indicator, true);
    } else {
        if (!ui_screenSerial) return;

        status_bar = lv_obj_create(ui_screenSerial);
        lv_obj_set_size(status_bar, 800, 24);
        lv_obj_set_pos(status_bar, 0, 456);
        lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x0A0A14), 0);
        lv_obj_set_style_bg_opa(status_bar, 240, 0);
        lv_obj_set_style_border_width(status_bar, 0, 0);
        lv_obj_set_style_radius(status_bar, 0, 0);
        lv_obj_set_style_pad_all(status_bar, 2, 0);
        lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

        status_line_count_lbl = lv_label_create(status_bar);
        lv_label_set_text(status_line_count_lbl, "0 lines");
        lv_obj_set_style_text_color(status_line_count_lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(status_line_count_lbl, &ui_font_fontSmall, 0);
        lv_obj_align(status_line_count_lbl, LV_ALIGN_LEFT_MID, 8, 0);

        status_rec_indicator = lv_label_create(status_bar);
        lv_label_set_text(status_rec_indicator, "");
        lv_label_set_recolor(status_rec_indicator, true);
        lv_obj_set_style_text_color(status_rec_indicator, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(status_rec_indicator, &ui_font_fontSmall, 0);
        lv_obj_align(status_rec_indicator, LV_ALIGN_CENTER, 0, 0);

        status_elapsed_lbl = lv_label_create(status_bar);
        lv_label_set_text(status_elapsed_lbl, "");
        lv_obj_set_style_text_color(status_elapsed_lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(status_elapsed_lbl, &ui_font_fontSmall, 0);
        lv_obj_align(status_elapsed_lbl, LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

// =====================================================================
// TAREA NUCLEO 0: PROCESAMIENTO SERIAL
// =====================================================================

void SerialBridgeTask(void * pvParameters) {
    Serial.println("[CORE 0] Serial Bridge iniciado.");
    AtmosPacket aPkt;
    SysPacket sPkt;

    while(true) {
        while (Serial.available()) {
            uint8_t header = Serial.peek();

            // --- DETECCION DE PAQUETES BINARIOS ---
            // Usamos break si vemos un MAGIC pero el paquete esta incompleto,
            // asi el MAGIC se queda en el buffer para el siguiente ciclo.

            if (header == MAGIC_ATMOS) {
                if (Serial.available() >= (int)sizeof(AtmosPacket)) {
                    Serial.read(); // Consumir header
                    Serial.readBytes((uint8_t*)&aPkt + 1, sizeof(AtmosPacket) - 1);
                    // lv_snprintf no soporta %f — convertir a enteros
                    int t_int = (int)aPkt.temp;
                    int t_dec = abs((int)(aPkt.temp * 10) % 10);
                    if(ui_labelTemp)  lv_label_set_text_fmt(ui_labelTemp,  "%d.%d\xC2\xB0""C Monterrey, NL", t_int, t_dec);
                    if(ui_labelTemp1) lv_label_set_text_fmt(ui_labelTemp1, "%d.%d\xC2\xB0""C Monterrey, NL", t_int, t_dec);
                    if(ui_labelTemp2) lv_label_set_text_fmt(ui_labelTemp2, "%d.%d\xC2\xB0""C Monterrey, NL", t_int, t_dec);
                    if(ui_labelTemp3) lv_label_set_text_fmt(ui_labelTemp3, "%d.%d\xC2\xB0""C Monterrey, NL", t_int, t_dec);
                } else break;
            }
            else if (header == MAGIC_SYS) {
                if (Serial.available() >= (int)sizeof(SysPacket)) {
                    Serial.read(); // Consumir header
                    Serial.readBytes((uint8_t*)&sPkt + 1, sizeof(SysPacket) - 1);
                } else break;
            }
            else if (header == MAGIC_COM) {
                if (Serial.available() >= (int)sizeof(ComPacket)) {
                    ComPacket cPkt;
                    Serial.read(); // Consumir 0xC5
                    Serial.readBytes((uint8_t*)&cPkt + 1, sizeof(ComPacket) - 1);
                    cPkt.ports[254] = '\0';
                    strncpy(global_ports_list, cPkt.ports, 254);
                    new_ports_available = true;
                } else break;
            }
            else if (header == MAGIC_AUDIO) {
                // Paquete fijo: [0xD5][1024 bytes PCM mono 16-bit 16kHz]
                if (Serial.available() >= AUDIO_CHUNK_BYTES + 1) {
                    Serial.read(); // consumir 0xD5
                    // Encender amplificador al recibir el primer chunk de audio
                    static bool amp_on = false;
                    static uint32_t audio_chunks_rx = 0;
                    if (!amp_on) {
                        // Timeout largo — Core 1 puede tener el bus I2C por el touch
                        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                            ioex.output(4, TCA9534::Level::L); // IO4=L: Q9 off → SHUT alto → normal
                            ioex.output(3, TCA9534::Level::L); // IO3=L: MUTE pin bajo → NS4268 unmuted
                            xSemaphoreGive(i2c_mutex);
                            amp_on = true;
                            Serial.println("[AUDIO] Amplificador ON (IO3=L unmute, IO4=L no-shutdown)");
                        } else {
                            Serial.println("[AUDIO] ERROR: no pude tomar i2c_mutex");
                        }
                    }
                    uint8_t slot;
                    if (xQueueReceive(audio_free_q, &slot, 0) == pdTRUE) {
                        int actually_read = Serial.readBytes((uint8_t*)audio_pool[slot], AUDIO_CHUNK_BYTES);
                        // Debug: verificar datos JUSTO después de leerlos
                        if (audio_chunks_rx < 5) {
                            Serial.printf("[RX] slot=%d read=%d s[0]=%d s[1]=%d s[255]=%d\n",
                                slot, actually_read,
                                audio_pool[slot][0], audio_pool[slot][1], audio_pool[slot][255]);
                        }
                        xQueueSend(audio_filled_q, &slot, 0);
                        audio_chunks_rx++;
                        // Reportar cada 50 chunks (~1.6s) para no saturar serial
                        if (audio_chunks_rx % 50 == 0) {
                            Serial.printf("[AUDIO] %d chunks procesados\n", audio_chunks_rx);
                        }
                    } else {
                        // Cola llena — descartar chunk
                        uint8_t _discard[64];
                        for (int left = AUDIO_CHUNK_BYTES; left > 0; ) {
                            int n = (left < 64) ? left : 64;
                            Serial.readBytes(_discard, n);
                            left -= n;
                        }
                        Serial.println("[AUDIO] Cola llena, chunk descartado");
                    }
                } else break;
            }
            else if (header == MAGIC_VOICE_STATE) {
                // Paquete fijo: [0xF5][1 byte estado]
                if (Serial.available() >= 2) {
                    Serial.read();                    // consumir 0xF5
                    voice_state = (uint8_t)Serial.read();
                } else break;
            }
            // --- TRATAMIENTO DE TEXTO PLANO (LOGGER / SENTINEL PROTOCOL) ---
            else {
                uint8_t c = Serial.read();
                static String logLine = "";
                if (c == '\n' || logLine.length() > 250) {
                    if (logLine.length() > 0) {
                        // Parsear comandos de estado del daemon
                        if (logLine == "S:REC") {
                            is_recording = true;
                            rec_start_millis = millis();
                            logLine = "";
                            continue;
                        } else if (logLine == "S:STOP") {
                            is_recording = false;
                            logLine = "";
                            continue;
                        }

                        // Parsear terminal ID: T0:, T1:, T2:, T3:
                        int target_terminal = 0;
                        const char* msg = logLine.c_str();
                        if (logLine.length() > 2 && msg[0] == 'T' &&
                            msg[1] >= '0' && msg[1] <= '3' && msg[2] == ':') {
                            target_terminal = msg[1] - '0';
                            msg += 3; // Skip prefix
                        }

                        int next = (term_q_head + 1) % TERM_QUEUE_DEPTH;
                        if (next != term_q_tail) { // descarta si el buffer esta lleno
                            strncpy(term_queue[term_q_head], msg, 255);
                            term_queue[term_q_head][255] = '\0';
                            term_queue_terminal[term_q_head] = (uint8_t)target_terminal;
                            term_q_head = next;
                        }
                    }
                    logLine = "";
                } else if (c >= 32 && c <= 126) {
                    // Incluye '#' (ASCII 35) para recolor LVGL: #RRGGBB texto#
                    logLine += (char)c;
                }
            }

            // Visual Pulse: Cambiar color de un elemento para saber que hay trafico
            if(ui_monitorTitle) {
                static uint32_t last_pulse = 0;
                if(millis() - last_pulse > 100) {
                    lv_obj_set_style_text_color(ui_monitorTitle, lv_color_hex(0x00FFCC), 0);
                    last_pulse = millis();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.setTxBufferSize(8192);  // Buffer TX grande para bursts de audio mic (0xE5 chunks)
    Serial.setRxBufferSize(4096); // Buffer grande para paquetes de audio (1025 bytes c/u)
    Serial.begin(460800);
    delay(500);

    // --- PRIORIDAD MAXIMA: Arrancar el puente serial de inmediato ---
    xTaskCreatePinnedToCore(SerialBridgeTask, "SerialBridge", 8192, NULL, 1, &TaskSerialHandle, 0);

    Serial.printf("\n\n>>> TAMANO REAL DEL PSRAM DETECTADO: %d bytes <<<\n\n", ESP.getPsramSize());

    // Inicializar I2C Mutex
    i2c_mutex = xSemaphoreCreateMutex();

    // Inicializacion I2C y Expansor de Pantalla Probada (ETS2)
    Wire.begin(15, 16);
    ioex.attach(Wire);
    ioex.setDeviceAddress(0x18);

    // Expansor Pin 1 = LCD Backlight (Activo Alto)
    ioex.config(1, TCA9534::Config::OUT);
    ioex.output(1, TCA9534::Level::H);

    // Expansor Pin 4 = Power-Gate del Amplificador de Audio I2S (Activo Bajo)
    // Arranca APAGADO (HIGH) para evitar ruido de pines I2S flotando durante boot.
    // Encender solo cuando haya audio real listo para reproducir.
    ioex.config(4, TCA9534::Config::OUT);
    ioex.output(4, TCA9534::Level::H);

    // Buzzer nativo en GPIO 8
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);


    // Inicializar Pantalla
    gfx.init();
    gfx.initDMA();
    gfx.startWrite();
    lv_init();

    // Reserva en PSRAM reducida a 1/4 (25%) para liberar >1.1MB extra y alojar las 6 imagenes gigantes
    size_t bs = sizeof(lv_color_t) * LCD_H_RES * (LCD_V_RES / 4);
    buf = (lv_color_t *)heap_caps_malloc(bs, MALLOC_CAP_SPIRAM);
    buf1 = (lv_color_t *)heap_caps_malloc(bs, MALLOC_CAP_SPIRAM);

    if (buf && buf1) {
        lv_disp_draw_buf_init(&draw_buf, buf, buf1, LCD_H_RES * (LCD_V_RES / 4));
    } else {
        Serial.println("[ERR] Fallo reserva de PSRAM, usando buffer simple interno.");
        buf = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * LCD_H_RES * 40, MALLOC_CAP_INTERNAL);
        lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_H_RES * 40);
    }

    // Registro de Drivers
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = LCD_H_RES; drv.ver_res = LCD_V_RES;
    drv.flush_cb = my_disp_flush; drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&drv);

    static lv_indev_drv_t idrv;
    lv_indev_drv_init(&idrv);
    idrv.type = LV_INDEV_TYPE_POINTER; idrv.read_cb = my_touchpad_read;
    idrv.feedback_cb = my_buzzer_feedback; // Habilitar feedback de sonido global en LVGL
    lv_indev_drv_register(&idrv);

    // INICIALIZAR LITTLEFS (Sistema de archivos virtual S:)
    init_lvgl_fs();

    // CARGAR IMAGENES DE FONDO EN PSRAM ANTES DE ui_init()
    // ui_init() llama lv_img_set_src() — los datos deben existir antes de que LVGL
    // intente renderizar. Con data=NULL el decoder crashea (null ptr en Xtensa).
    ui_img_mty_noche_png_load();
    ui_img_mty_amanecer_png_load();
    ui_img_mty_dia_png_load();
    ui_img_mty_atardecer_png_load();

    // INICIALIZAR GUI DE SQUARELINE
    ui_init();

    // Configurar terminal 0 (SquareLine) para que se comporte igual que los dinamicos:
    // - Recolor para colores LVGL (#RRGGBB texto#)
    // - LONG_WRAP para word-wrap (evitar scroll horizontal)
    // - Scrollbar auto en el area contenedora
    if (ui_terminalText) {
        lv_label_set_recolor(ui_terminalText, true);
        lv_label_set_long_mode(ui_terminalText, LV_LABEL_LONG_WRAP);
        lv_obj_clear_flag(ui_terminalText, LV_OBJ_FLAG_CLICKABLE);
    }
    if (ui_terminalArea) {
        lv_obj_set_scrollbar_mode(ui_terminalArea, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_add_flag(ui_terminalArea, LV_OBJ_FLAG_CLICKABLE);
    }

    // Reemplazar el callback fijo de SquareLine en el boton HUB con logica horaria
    if (ui_hubBtn) {
        lv_obj_remove_event_cb(ui_hubBtn, ui_event_hubBtn);
        lv_obj_add_event_cb(ui_hubBtn, my_hub_btn_event, LV_EVENT_ALL, NULL);
    }

    // Reemplazar el callback de SquareLine en el boton BLE — en el disenador
    // quedó conectado a ui_screenDay por error; debe llamar a ble_ui_toggle()
    if (ui_btConnectionSts) {
        lv_obj_remove_event_cb(ui_btConnectionSts, ui_event_btConnectionSts);
        lv_obj_add_event_cb(ui_btConnectionSts, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) ble_ui_toggle();
        }, LV_EVENT_ALL, NULL);
    }

    // Reemplazar dashBtn (Serial→Dashboard): sin animacion + cleanup de terminales dinamicos
    if (ui_dashBtn) {
        lv_obj_remove_event_cb(ui_dashBtn, ui_event_dashBtn);
        lv_obj_add_event_cb(ui_dashBtn, my_dash_btn_event, LV_EVENT_ALL, NULL);
    }

    // --- SENTINEL MONITOR: Registrar terminal 0 con los objetos de SquareLine ---
    terminal_areas[0] = ui_terminalArea;
    terminal_texts[0] = ui_terminalText;

    // --- SENTINEL MONITOR: Setup botones (SquareLine si existen, fallback si no) ---
    setup_sentinel_buttons();

    // Forzar estado visual correcto del botón USB después del primer render.
    // lv_obj_set_style_* programa el cambio pero LVGL lo aplica en el siguiente ciclo.
    // El evento SCREEN_LOADED garantiza que ya se renderizó.
    if (ui_screenSerial) {
        lv_obj_add_event_cb(ui_screenSerial, [](lv_event_t* e) {
            if (usb_btn) {
                lv_obj_set_style_border_color(usb_btn, lv_color_hex(0x00FFCC), 0);
                lv_obj_set_style_text_color(lv_obj_get_child(usb_btn, 0),
                    usb_port_released ? lv_color_hex(0x555555) : lv_color_hex(0xFFAA00), 0);
                lv_obj_set_style_border_color(usb_btn,
                    usb_port_released ? lv_color_hex(0x555555) : lv_color_hex(0x00FFCC), 0);
            }
        }, LV_EVENT_SCREEN_LOADED, NULL);
    }

    // --- SENTINEL MONITOR: Setup status bar (SquareLine si existe, fallback si no) ---
    setup_sentinel_status_bar();

    // --- ANIMACION DE CARGA DE BOOT: "Breathing Waves" ---
    lv_obj_t * boot_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(boot_overlay, 800, 480);
    lv_obj_set_style_bg_color(boot_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(boot_overlay, 255, 0);
    lv_obj_set_style_border_width(boot_overlay, 0, 0);
    lv_obj_clear_flag(boot_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Titulo central
    lv_obj_t * title = lv_label_create(boot_overlay);
    lv_label_set_text(title, "SENTINEL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Subtitulo
    lv_obj_t * subtitle = lv_label_create(boot_overlay);
    lv_label_set_text(subtitle, "M O N I T O R");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_opa(subtitle, 140, 0);
    lv_obj_set_style_text_font(subtitle, &ui_font_fontSmall, 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 30);

    // --- Anillos de puntos concéntricos ---
    // Cada anillo es un conjunto de labels "·" colocados en círculo.
    // La animación pulsa la opacidad de cada anillo con desfase temporal,
    // creando el efecto de onda expansiva desde el centro.
    #define BOOT_NUM_RINGS   4
    #define BOOT_DOTS_RING0  8
    #define BOOT_DOTS_RING1  14
    #define BOOT_DOTS_RING2  20
    #define BOOT_DOTS_RING3  28
    static const int dots_per_ring[BOOT_NUM_RINGS] = {
        BOOT_DOTS_RING0, BOOT_DOTS_RING1, BOOT_DOTS_RING2, BOOT_DOTS_RING3
    };
    static const int ring_radius[BOOT_NUM_RINGS] = { 60, 100, 145, 195 };
    // Opacidad base decreciente: anillos exteriores más tenues
    static const int ring_opa_max[BOOT_NUM_RINGS] = { 220, 180, 130, 80 };

    // Almacenar referencias para animar
    lv_obj_t* ring_containers[BOOT_NUM_RINGS];

    const int cx = 400, cy = 225; // Centro ligeramente arriba del medio

    for (int r = 0; r < BOOT_NUM_RINGS; r++) {
        // Contenedor invisible por anillo (para animar opacidad de grupo)
        ring_containers[r] = lv_obj_create(boot_overlay);
        lv_obj_set_size(ring_containers[r], 800, 480);
        lv_obj_set_pos(ring_containers[r], 0, 0);
        lv_obj_set_style_bg_opa(ring_containers[r], 0, 0);
        lv_obj_set_style_border_width(ring_containers[r], 0, 0);
        lv_obj_set_style_pad_all(ring_containers[r], 0, 0);
        lv_obj_clear_flag(ring_containers[r], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(ring_containers[r], 0, 0); // Empieza invisible

        int n = dots_per_ring[r];
        int rad = ring_radius[r];
        for (int d = 0; d < n; d++) {
            float angle = (2.0f * 3.14159f * d) / n;
            int dx = (int)(cosf(angle) * rad);
            int dy = (int)(sinf(angle) * rad);

            lv_obj_t * dot = lv_label_create(ring_containers[r]);
            // Alternar entre puntos y números para el efecto "matrix/digital"
            if (d % 3 == 0) {
                char num_str[4];
                snprintf(num_str, sizeof(num_str), "%d", (r * n + d) % 10);
                lv_label_set_text(dot, num_str);
            } else {
                lv_label_set_text(dot, "·");
            }
            lv_obj_set_style_text_color(dot, lv_color_hex(0x00FFCC), 0);
            lv_obj_set_style_text_font(dot, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(dot, cx + dx - 4, cy + dy - 7);
        }

        // Animación breathing: opacidad 0 → max → 0, desfasada por anillo
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ring_containers[r]);
        lv_anim_set_exec_cb(&a, [](void * obj, int32_t v) {
            lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
        });
        lv_anim_set_values(&a, 0, ring_opa_max[r]);
        lv_anim_set_time(&a, 900);
        lv_anim_set_delay(&a, r * 250);  // Cada anillo arranca 250ms después
        lv_anim_set_playback_time(&a, 900);
        lv_anim_set_playback_delay(&a, 0);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_repeat_delay(&a, 200);
        lv_anim_start(&a);
    }

    // Animación breathing del título principal
    lv_anim_t title_anim;
    lv_anim_init(&title_anim);
    lv_anim_set_var(&title_anim, title);
    lv_anim_set_exec_cb(&title_anim, [](void * obj, int32_t v) {
        lv_obj_set_style_text_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
    });
    lv_anim_set_values(&title_anim, 120, 255);
    lv_anim_set_time(&title_anim, 1500);
    lv_anim_set_playback_time(&title_anim, 1500);
    lv_anim_set_repeat_count(&title_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&title_anim);

    // (La tarea Serial ya fue creada arriba para ganar milisegundos)

    // --- WIFI Y TIEMPO (NTP) ---
    Serial.print("Conectando WiFi");
    WiFi.disconnect(true);   // Limpiar estado previo (puede quedar en modo AP+STA)
    delay(100);
    WiFi.mode(WIFI_STA);     // Forzar modo Station puro — conexión más rápida
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 80) {
        for(int i=0; i<5; i++) {
            delay(50);
            lv_timer_handler(); // Mantiene la animacion breathing activa
        }
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Conectado!");
        configTime(-21600, 0, "pool.ntp.org");

        // Esperar sincronizacion NTP con TIMEOUT de 5 segundos
        struct tm timeinfo;
        uint32_t ntp_start = millis();
        while (!getLocalTime(&timeinfo) && (millis() - ntp_start < 5000)) {
            for(int i=0; i<5; i++) {
                delay(10);
                lv_timer_handler();
            }
        }
    } else {
        Serial.println("\nError WiFi o Timeout");
        // Apagar WiFi completamente para liberar CPU/interrupts de Core 0.
        // Sin esto, la pila WiFi sigue en background intentando reconectarse
        // y degrada severamente el rendimiento de lv_timer_handler().
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_OFF);
    }

    if(boot_overlay) lv_obj_del(boot_overlay); // Forzar eliminacion para liberar la pantalla

    // SELECCIONAR PANTALLA SEGUN HORA DEL DIA (Monterrey, NL)
    // Rangos calibrados para latitud ~25.7°N:
    //   06:00 – 08:59  → Amanecer   (cielo naranja/rosa, sol saliendo)
    //   09:00 – 17:59  → Día pleno  (sol alto, cielo azul)
    //   18:00 – 20:59  → Atardecer  (tonos cálidos, sol bajando)
    //   21:00 – 05:59  → Noche      (oscuro, ciudad iluminada)
    {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            int h = timeinfo.tm_hour;
            if      (h >= 6  && h < 9)  { _ui_screen_change(&ui_screenMorning, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_screenMorning_screen_init); }
            else if (h >= 9  && h < 18) { _ui_screen_change(&ui_screenDay,     LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_screenDay_screen_init); }
            else if (h >= 18 && h < 21) { _ui_screen_change(&ui_screenDown,    LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_screenDown_screen_init); }
            // h >= 21 || h < 6: Noche — ui_screenNight ya activo por ui_init()
        }
        // NTP fallo: ui_screenNight ya activo — no hacer nada
    }


    // --- INICIAR TAREA DE AUDIO ---
    // Aislada en el Nucleo 0 (junto al Serial) para evitar que las rafagas
    // de DMA-RGB de LVGL en el Nucleo 1 ahoguen el bus SPI y vacien el buffer I2S.
    audio_init_queues();
    xTaskCreatePinnedToCore(AudioTask, "AudioTask", 4096, NULL, 1, NULL, 0);

    // ── Micrófono INMP441: I2S_NUM_0, GPIO 19/20/2, 8kHz mono ─────────────
    mic_init_i2s();
    xTaskCreatePinnedToCore(MicTask, "MicTask", 4096, NULL, 2, NULL, 0);

    // ── BLE Nordic UART Service ────────────────────────────────────────────
    // Inicializar ANTES de crear BLETask; BLEDevice::init() no debe llamarse
    // desde una tarea de usuario, sino desde el contexto de setup().
    // COEXISTENCIA WiFi+BLE: habilitar MODEM_SLEEP para ceder slots de radio.
    // Sin esto el advertising BLE puede perder ventanas cuando WiFi está activo.
    WiFi.setSleep(true);
    ble_uart_init("Sentinel-HMI");
    // Core 0 — junto a SerialBridgeTask, AudioTask y MicTask.
    // Stack 8192: parser de paquetes (~2KB) + overhead de la tarea BLE.
    xTaskCreatePinnedToCore(BLETask, "BLETask", 8192, NULL, 1, NULL, 0);

    // ── Función puente C→C++ para el botón BT del Dashboard ───────────────
    // El callback de LVGL (en ui_screenDashboard.c, compilado como C) necesita
    // disparar el advertising BLE. Como ble_uart.h usa C++, se expone con
    // extern "C" para que el linker resuelva el símbolo desde el .c file.

    Serial.println("[SENTINEL] Monitor HMI iniciado.");
}

// ─── Botón BT: toggle advertising ─────────────────────────────────────────────
// Declarado extern "C" para poder llamarse desde ui_screenDashboard.c (C puro).
// Solo actúa si BLE no está conectado:
//   estado 0 → start advertising → estado 1  (Sin Conexión → Visible...)
//   estado 1 → stop advertising  → estado 0  (Visible... → Sin Conexión)
//   estado 2 → ignorar (conexión activa, el daemon gestiona la desconexión)
extern "C" void ble_ui_toggle(void) {
    if (ble_advertising_state == 0) {
        // Desconectar WiFi antes de activar BLE para dar el radio 2.4GHz
        // exclusivamente a BLE. Con WiFi activo, cada round-trip ATT (service
        // discovery, MTU exchange, CCCD write) se retrasa y el stack de Windows
        // supera su timeout interno (~5s) → disconnect reason 0x13.
        // Los datos de clima quedan con el último valor hasta que BLE se detenga.
        WiFi.disconnect(false);   // desconecta del AP pero mantiene el stack
        delay(100);               // dar tiempo al radio para limpiar estado WiFi
        NimBLEDevice::startAdvertising();
        ble_advertising_state = 1;
        Serial.println("[BLE] Advertising activado — WiFi suspendido");
    } else if (ble_advertising_state == 1) {
        NimBLEDevice::stopAdvertising();
        ble_advertising_state = 0;
        // Reconectar WiFi al detener BLE para reanudar clima / NTP
        WiFi.reconnect();
        Serial.println("[BLE] Advertising detenido — WiFi reconectando");
    }
    // estado 2 (conectado): no hacer nada — BLE gestiona la desconexión
}

void loop() {
    // Reloj: Actualizar SquareLine cada 1 segundo (Asincrono)
    static uint32_t last_time_update = 0;
    if (millis() - last_time_update > 1000) {
        last_time_update = millis();
        struct tm timeinfo;
        if(getLocalTime(&timeinfo)) {
            // Formato 12h sin cero inicial (ej: "9:04", "12:30")
            int hour12 = timeinfo.tm_hour % 12;
            if (hour12 == 0) hour12 = 12;
            char time_str[16];
            snprintf(time_str, sizeof(time_str), "%d:%02d", hour12, timeinfo.tm_min);
            const char* ampm = (timeinfo.tm_hour < 12) ? "am" : "pm";

            if(ui_labelHour)  lv_label_set_text(ui_labelHour,  time_str);
            if(ui_labelHour1) lv_label_set_text(ui_labelHour1, time_str);
            if(ui_labelHour2) lv_label_set_text(ui_labelHour2, time_str);
            if(ui_labelHour3) lv_label_set_text(ui_labelHour3, time_str);

            if(ui_lblTime)  lv_label_set_text(ui_lblTime,  ampm);
            if(ui_lblTime1) lv_label_set_text(ui_lblTime1, ampm);
            if(ui_lblTime2) lv_label_set_text(ui_lblTime2, ampm);
            if(ui_lblTime3) lv_label_set_text(ui_lblTime3, ampm);
        }
    }

    // --- AUTO-CAMBIO DE PANTALLA HUB SEGUN HORA ---
    // Verifica cada 30s si el rango horario cambió y el usuario sigue en un hub screen.
    static int  last_hub_hour  = -1;
    static uint32_t last_hub_check = 0;
    if (millis() - last_hub_check > 30000) {
        last_hub_check = millis();
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            int h = timeinfo.tm_hour;
            lv_obj_t* act = lv_scr_act();
            bool on_hub = (act == ui_screenNight   || act == ui_screenMorning ||
                           act == ui_screenDay      || act == ui_screenDown);
            if (on_hub && h != last_hub_hour) {
                last_hub_hour = h;
                lv_obj_t** target  = &ui_screenNight;
                void (*init_fn)(void) = ui_screenNight_screen_init;
                if      (h >= 6  && h < 9)  { target = &ui_screenMorning; init_fn = ui_screenMorning_screen_init; }
                else if (h >= 9  && h < 18) { target = &ui_screenDay;     init_fn = ui_screenDay_screen_init; }
                else if (h >= 18 && h < 21) { target = &ui_screenDown;    init_fn = ui_screenDown_screen_init; }
                if (act != *target) {
                    _ui_screen_change(target, LV_SCR_LOAD_ANIM_NONE, 0, 0, init_fn);
                }
            }
        }
    }

    // --- MANEJO SEGURO DE LA UI (CORE 1) ---
    if (new_ports_available) {
        new_ports_available = false;
        if(ui_comContainer) {
            lv_obj_clean(ui_comContainer);
            char temp_list[255];
            strncpy(temp_list, global_ports_list, 255);
            char* token = strtok(temp_list, ",");
            while(token != NULL) {
                // Prevenir desbordamiento de UI acortando el nombre si es inmenso
                String safe_name = String(token);
                if (safe_name.length() > 14) {
                    safe_name = safe_name.substring(0, 11) + "...";
                }

                lv_obj_t* card = ui_comCarg_create(ui_comContainer);
                lv_obj_t* lbl = lv_obj_get_child(card, 0);
                if(lbl) lv_label_set_text(lbl, safe_name.c_str());

                // Guardamos el nombre real (largo) como user_data para que el evento sepa que pedir a Python
                char* port_meta = (char*)malloc(strlen(token) + 1);
                strcpy(port_meta, token);

                lv_obj_add_event_cb(card, my_com_card_event, LV_EVENT_CLICKED, port_meta);
                lv_obj_add_event_cb(card, free_user_data_event, LV_EVENT_DELETE, port_meta);

                token = strtok(NULL, ",");
            }
        }
    }

    // --- BLE: ACTUALIZAR TEXTO DEL BOTÓN BT EN DASHBOARD (CORE 1) ---
    // Solo actualiza el label de texto — el diseño visual (colores, animaciones)
    // lo controla SquareLine Studio. Detecta cambios en ble_advertising_state
    // escrito desde Core 0 (callbacks onConnect/onDisconnect) o desde este Core 1
    // (ble_ui_toggle vía evento LVGL).
    static uint8_t last_ble_ui_state = 0xFF;  // forzar actualización inicial
    if (ble_advertising_state != last_ble_ui_state && ui_btConnectionSts) {
        last_ble_ui_state = ble_advertising_state;
        // Actualizar texto del label
        if (ui_btLbl) {
            switch (ble_advertising_state) {
                case 0: lv_label_set_text(ui_btLbl, "Sin Conexión   "); break;
                case 1: lv_label_set_text(ui_btLbl, "Visible...   ");   break;
                case 2: lv_label_set_text(ui_btLbl, "Conectado   ");    break;
            }
        }
        // Actualizar color del círculo de estado (ui_btSts = hijo índice 1).
        // ui_btSts no está exportado en el header — acceder via lv_obj_get_child.
        lv_obj_t* dot = lv_obj_get_child(ui_btConnectionSts, 1);
        if (dot) {
            lv_color_t dot_color;
            switch (ble_advertising_state) {
                case 0: dot_color = lv_color_hex(0x5E5E5E); break;  // gris — sin conexión
                case 1: dot_color = lv_color_hex(0x5DADE2); break;  // azul — visible
                case 2: dot_color = lv_color_hex(0x2ECC71); break;  // verde — conectado
                default: dot_color = lv_color_hex(0x5E5E5E); break;
            }
            lv_obj_set_style_bg_color(dot, dot_color, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // --- SENTINEL: MANEJO SEGURO DE TERMINAL (CORE 1) ---
    // Procesa hasta 8 mensajes por tick para vaciar el buffer mas rapido
    int processed = 0;
    while (term_q_tail != term_q_head && processed < 8) {
        if (!terminal_paused) {
            if (lv_scr_act() == ui_screenSerial) {
                int term_idx = term_queue_terminal[term_q_tail];
                if (term_idx >= active_terminal_count) term_idx = 0;
                append_to_terminal(term_idx, term_queue[term_q_tail]);
            }
        } else {
            // Paused: no actualizar UI pero seguir contando
            pending_while_paused++;
        }
        term_q_tail = (term_q_tail + 1) % TERM_QUEUE_DEPTH;
        processed++;
    }

    // --- SENTINEL: ACTUALIZAR STATUS BAR (cada 500ms) ---
    static uint32_t last_status_update = 0;
    if (millis() - last_status_update > 500) {
        last_status_update = millis();

        if (status_line_count_lbl) {
            if (terminal_paused && pending_while_paused > 0) {
                lv_label_set_text_fmt(status_line_count_lbl, "%lu lines | %d pending",
                                      total_line_count, pending_while_paused);
            } else {
                lv_label_set_text_fmt(status_line_count_lbl, "%lu lines", total_line_count);
            }
        }

        // REC indicator
        if (status_rec_indicator) {
            if (is_recording) {
                // Blink effect: alternate between visible and dim
                bool blink = ((millis() / 500) % 2) == 0;
                if (blink) {
                    lv_label_set_text(status_rec_indicator, "#FF0000 " LV_SYMBOL_STOP " REC#");
                } else {
                    lv_label_set_text(status_rec_indicator, "#880000 " LV_SYMBOL_STOP " REC#");
                }
            } else {
                lv_label_set_text(status_rec_indicator, "");
            }
        }

        // REC button color update
        if (rec_lbl) {
            if (is_recording) {
                lv_obj_set_style_text_color(rec_lbl, lv_color_hex(0xFF0000), 0);
                lv_label_set_text(rec_lbl, LV_SYMBOL_STOP " REC");
            } else {
                lv_obj_set_style_text_color(rec_lbl, lv_color_hex(0x888888), 0);
                lv_label_set_text(rec_lbl, "REC");
            }
        }

        // Elapsed time (only when recording)
        if (status_elapsed_lbl) {
            if (is_recording) {
                uint32_t elapsed_s = (millis() - rec_start_millis) / 1000;
                uint32_t h = elapsed_s / 3600;
                uint32_t m = (elapsed_s % 3600) / 60;
                uint32_t s = elapsed_s % 60;
                lv_label_set_text_fmt(status_elapsed_lbl, "%02lu:%02lu:%02lu", h, m, s);
            } else {
                lv_label_set_text(status_elapsed_lbl, "");
            }
        }

        // Pause button badge update
        if (pause_lbl && terminal_paused && pending_while_paused > 0) {
            lv_label_set_text_fmt(pause_lbl, LV_SYMBOL_PLAY " %d", pending_while_paused);
        }
    }

    // ── BLE AtmosPacket staging → aplicar labels en Core 1 ──────────────────
    // BLETask (Core 0) no puede llamar LVGL directamente. Escribe en
    // ble_atmos_pending/t_int/t_dec y loop() (Core 1) los aplica aquí.
    if (ble_atmos_pending) {
        ble_atmos_pending = false;  // leer primero (fence implícito en Xtensa)
        int ti = ble_atmos_t_int;
        int td = ble_atmos_t_dec;
        if (ui_labelTemp)  lv_label_set_text_fmt(ui_labelTemp,  "%d.%d\xC2\xB0""C Monterrey, NL", ti, td);
        if (ui_labelTemp1) lv_label_set_text_fmt(ui_labelTemp1, "%d.%d\xC2\xB0""C Monterrey, NL", ti, td);
        if (ui_labelTemp2) lv_label_set_text_fmt(ui_labelTemp2, "%d.%d\xC2\xB0""C Monterrey, NL", ti, td);
        if (ui_labelTemp3) lv_label_set_text_fmt(ui_labelTemp3, "%d.%d\xC2\xB0""C Monterrey, NL", ti, td);
    }

    // El nucleo 1 (por defecto) corre el bucle de LVGL
    lv_timer_handler();
    delay(5);
}
