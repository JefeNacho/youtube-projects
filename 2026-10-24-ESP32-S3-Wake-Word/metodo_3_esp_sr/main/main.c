/**
 * ==============================================================================
 * PROYECTO: Wake Word 100% Local en ESP32-S3
 * MÉTODO 3: Espressif ESP-SR (WakeNet + AFE con 2 micrófonos)
 *
 * ¿Qué hace este programa?
 *   - Dos tareas corren en el núcleo 1 del ESP32-S3:
 *       feed_task   -> lee los 2 micrófonos y le pasa el audio al AFE
 *       detect_task -> recibe el audio ya limpio y le pregunta a WakeNet si dijeron la palabra
 *   - Si la detecta, el aro de LEDs se pone azul 2 segundos.
 *   - El núcleo 0 queda libre para tu aplicación (Wi-Fi, Bluetooth, etc.).
 *
 * La palabra que escucha NO se entrena: es un modelo ya hecho por Espressif
 * (Hi ESP, Jarvis, Sophia...). Cámbiala en sdkconfig.defaults (mira el README.md).
 *
 * Framework: ESP-IDF 5.x (compilado con PlatformIO). Requiere PSRAM.
 * ==============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

// Librerías de Espressif ESP-SR
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

#include "app_config.h"
#include "log_simple.h"

static const char *TAG = "WAKENET_AFE";

// ── Punteros y Handles Globales del AFE ────────────────────────────────────────
static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;
static led_strip_handle_t led_ring = NULL;
static volatile bool is_led_active = false;
static int64_t led_trigger_time = 0;

// ── Prototipos ────────────────────────────────────────────────────────────────
static void init_hardware(void);
static void init_i2s(void);
static void feed_task(void *pvParameters);
static void detect_task(void *pvParameters);

// ==============================================================================
// APP MAIN (Punto de Entrada ESP-IDF)
// ==============================================================================
void app_main(void) {
    // 1. Inicializar NVS (Requerido para configuraciones internas)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    log_linea('=');
    log_info("ESP32-S3 Wake Word - Metodo 3: ESP-SR (AFE de 2 microfonos + WakeNet)");
    log_linea('=');

    // 2. Configurar el anillo de LEDs (WS2812) actuador
    init_hardware();

    // 3. Configurar I2S en modo Estéreo Entrelazado (2 Micrófonos)
    init_i2s();
    log_info("Bus I2S Estereo (Canal L: Mic 1, Canal R: Mic 2) iniciado.");

    // 4. Cargar modelos de voz desde la particion "model" y localizar WakeNet
    srmodel_list_t *models = esp_srmodel_init("model");
    char *wn_name = models ? esp_srmodel_filter(models, ESP_WN_PREFIX, NULL) : NULL;
    if (wn_name == NULL) {
        log_error("No se encontro ningun modelo WakeNet en la particion 'model'.");
        return;
    }

    printf("[INFO] Palabra de activación cargada: %s\n", wn_name);

    // 5. Configurar el Audio Front-End (AFE) de Espressif (API >= esp-sr 2.x)
    afe_config_t *afe_config = afe_config_init("MM", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_config->wakenet_model_name = wn_name;
    afe_config->afe_ringbuf_size = 50;
    // Modo "agresivo" para 2 canales: mas sensible (mayor tasa de deteccion, algo mas de falsos positivos).
    afe_config->wakenet_mode = DET_MODE_2CH_95;
    afe_config_check(afe_config);

    // Obtener interfaz del AFE para SR (Speech Recognition)
    afe_handle = (esp_afe_sr_iface_t*)esp_afe_handle_from_config(afe_config);
    afe_data = afe_handle->create_from_config(afe_config);

    if (afe_data == NULL) {
        log_error("Fallo al instanciar modulo AFE de ESP-SR.");
        return;
    }

    log_info("Audio Front-End (AFE) activo: procesa los 2 microfonos antes de WakeNet.");

    // 6. Crear Tareas FreeRTOS ancladas al Core 1
    // Task 1: Alimentar el AFE con datos I2S
    xTaskCreatePinnedToCore(feed_task, "afe_feed", 4096, NULL, 5, NULL, AFE_TASK_CORE);

    // Task 2: Detectar Wake Word desde la salida del AFE
    xTaskCreatePinnedToCore(detect_task, "afe_detect", 8192, NULL, 5, NULL, AFE_TASK_CORE);

    log_info("Tareas de Audio ancladas a Core 1. Core 0 disponible para App.");
}

// ==============================================================================
// CONFIGURACIÓN DE HARDWARE Y DRIVER I2S
// ==============================================================================
static void init_hardware(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = PIN_LED_OUT,
        .max_leds = LED_RING_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_ring));
    led_strip_clear(led_ring);
}

static void init_i2s(void) {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        // Leemos la palabra completa de 32 bits del INMP441 y la recortamos a 16 bits en feed_task.
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // 2 canales físicos entrelazados
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_SCK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_SD
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pin_config));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_NUM_0));
}

// ==============================================================================
// TAREA FEED: Lectura de I2S hacia el AFE (Core 1)
// ==============================================================================
static void feed_task(void *pvParameters) {
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int feed_channel = afe_handle->get_channel_num(afe_data);
    int32_t *raw_buff = malloc(audio_chunksize * sizeof(int32_t) * feed_channel);
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    size_t bytes_read = 0;

    while (1) {
        i2s_read(I2S_NUM_0, (void*)raw_buff, audio_chunksize * sizeof(int32_t) * feed_channel, &bytes_read, portMAX_DELAY);

        // Convertimos cada muestra de 32 bits a 16 bits (ver MIC_SAMPLE_SHIFT en app_config.h).
        int samples_read = bytes_read / sizeof(int32_t);
        for (int i = 0; i < samples_read; i++) {
            i2s_buff[i] = (int16_t)(raw_buff[i] >> MIC_SAMPLE_SHIFT);
        }

        // Alimentar el procesador de señal acústica (AFE)
        afe_handle->feed(afe_data, i2s_buff);
    }

    free(raw_buff);
    free(i2s_buff);
    vTaskDelete(NULL);
}

// ==============================================================================
// TAREA DETECT: Procesamiento AFE + Inferencia WakeNet (Core 1)
// ==============================================================================
static void detect_task(void *pvParameters) {
    while (1) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            continue;
        }

        // 1. Apagado temporizado del anillo de LEDs
        if (is_led_active) {
            if ((esp_timer_get_time() - led_trigger_time) >= 2000000) { // 2 segundos
                led_strip_clear(led_ring);
                is_led_active = false;
                log_info("Sistema listo para nueva activacion.");
            }
        }

        // 2. Detección de la palabra clave por WakeNet
        if (res->wakeup_state == WAKENET_DETECTED) {
            log_linea('*');
            log_info(">>> PALABRA DE ACTIVACION DETECTADA <<<");
            log_linea('*');

            for (int i = 0; i < LED_RING_COUNT; i++) {
                led_strip_set_pixel(led_ring, i, 0, 0, 255); // Azul solido (R,G,B)
            }
            led_strip_refresh(led_ring);
            led_trigger_time = esp_timer_get_time();
            is_led_active = true;
        }
    }
    vTaskDelete(NULL);
}
