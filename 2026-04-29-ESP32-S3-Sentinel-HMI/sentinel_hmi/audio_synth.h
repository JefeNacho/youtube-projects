#pragma once
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_attr.h>

#define I2S_PORT  I2S_NUM_1
// CrowPanel 4.3" — pines I2S confirmados con ejemplos oficiales Elecrow:
// GPIO 1, 2, 3 NO usar: 3 = R3 del panel RGB, 1/2 = USB/JTAG del ESP32-S3
#define I2S_BCLK  5  // I2S_BCLK(IO5)
#define I2S_LRC   6  // I2S_LRCLK(IO6)
#define I2S_DOUT  4  // I2S_DOUT(IO4)
#define SAMPLE_RATE 16000

// Pool: 8 buffers de 512 samples mono × 2 bytes = 1024 bytes cada uno.
// DRAM_ATTR fuerza la ubicacion en SRAM interna — accesible por DMA.
// Jamas en PSRAM: el bus OPI saturado por LVGL vaciaria el buffer I2S.
#define AUDIO_POOL_SLOTS   8
#define AUDIO_CHUNK_SAMPLES 512
#define AUDIO_CHUNK_BYTES  (AUDIO_CHUNK_SAMPLES * sizeof(int16_t))  // 1024

// NO static — una sola instancia compartida entre SerialBridgeTask y AudioTask.
// Sin static, el linker fusiona las definiciones del .ino y el header.
DRAM_ATTR int16_t audio_pool[AUDIO_POOL_SLOTS][AUDIO_CHUNK_SAMPLES];

// Buffer de trabajo estereo (mono -> L+R interleaved): el doble de muestras.
// Tambien en SRAM interna para que el DMA I2S pueda alcanzarlo.
DRAM_ATTR int16_t _stereo_buf[AUDIO_CHUNK_SAMPLES * 2];

// Colas FreeRTOS de indices de slot (uint8_t).
// No-static: visibles desde sentinel_hmi.ino para el parser serial.
QueueHandle_t audio_filled_q = NULL;  // Slots con datos listos para reproducir
QueueHandle_t audio_free_q   = NULL;  // Slots disponibles para escribir

// Configuracion I2S: 16kHz mono, DMA en SRAM interna
static const i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 1024,
    .use_apll             = false,
    .tx_desc_auto_clear   = true
};

static const i2s_pin_config_t pin_config = {
    .bck_io_num    = I2S_BCLK,
    .ws_io_num     = I2S_LRC,
    .data_out_num  = I2S_DOUT,
    .data_in_num   = I2S_PIN_NO_CHANGE
};

// Crea las dos colas y pre-llena audio_free_q con todos los indices.
// Llamar desde setup(), antes de xTaskCreatePinnedToCore(AudioTask).
void audio_init_queues() {
    audio_filled_q = xQueueCreate(AUDIO_POOL_SLOTS, sizeof(uint8_t));
    audio_free_q   = xQueueCreate(AUDIO_POOL_SLOTS, sizeof(uint8_t));
    for (uint8_t i = 0; i < AUDIO_POOL_SLOTS; i++) {
        xQueueSend(audio_free_q, &i, portMAX_DELAY);
    }
}

// Tarea de reproduccion — fijar en Core 0 junto al Serial.
// Si no hay datos en 40ms escribe silencio para evitar crackling por underrun.
void AudioTask(void *pvParameters) {
    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    Serial.printf("[I2S] driver_install: %s\n", esp_err_to_name(err));
    err = i2s_set_pin(I2S_PORT, &pin_config);
    Serial.printf("[I2S] set_pin: %s (BCLK=%d, LRC=%d, DOUT=%d)\n",
                  esp_err_to_name(err), I2S_BCLK, I2S_LRC, I2S_DOUT);
    i2s_zero_dma_buffer(I2S_PORT);
    Serial.println("[I2S] AudioTask iniciado");

    size_t bytes_written;
    const TickType_t timeout = pdMS_TO_TICKS(40);
    uint32_t i2s_chunks = 0;

    while (true) {
        uint8_t slot;
        if (xQueueReceive(audio_filled_q, &slot, timeout) == pdTRUE) {
            // Expandir mono -> stereo interleaved (L+R identicos), 60% ganancia
            for (int i = 0; i < AUDIO_CHUNK_SAMPLES; i++) {
                int32_t s = (int32_t)audio_pool[slot][i] * 6 / 10;
                _stereo_buf[i * 2]     = (int16_t)s;
                _stereo_buf[i * 2 + 1] = (int16_t)s;
            }
            // Devolver el slot al pool antes de escribir a I2S
            xQueueSend(audio_free_q, &slot, 0);
            err = i2s_write(I2S_PORT, _stereo_buf, sizeof(_stereo_buf), &bytes_written, portMAX_DELAY);
            i2s_chunks++;
            if (i2s_chunks <= 3 || i2s_chunks % 100 == 0) {
                Serial.printf("[I2S] chunk #%d: wrote %d bytes, err=%s, sample[0]=%d\n",
                              i2s_chunks, bytes_written, esp_err_to_name(err), audio_pool[slot][0]);
            }
        } else {
            i2s_zero_dma_buffer(I2S_PORT);
        }
    }
}
