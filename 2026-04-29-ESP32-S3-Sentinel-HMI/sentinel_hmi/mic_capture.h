#pragma once
/*
 * mic_capture.h — Captura de audio I2S desde INMP441
 * Autor: Ignacio Aguilera
 *
 * Hardware confirmado por esquemático CrowPanel Advanced:
 *   INMP441 (U28): SCK=GPIO19, WS=GPIO2, SD=GPIO20
 *   Pin LR → GND (R26=0Ω) → mic transmite en canal izquierdo del frame I2S
 *   GPIO 19/20 son libres: el USB usa CH340K (sin conexión a los GPIO del ESP32-S3)
 *
 * Protocolo hacia PC: 0xE5 + 1024 bytes PCM int16 mono 8kHz
 * El ESP32 solo transmite chunks durante voz activa (VAD por RMS).
 */

#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_attr.h>
#include <math.h>
#include <string.h>

// ── Pines (por esquemático) ───────────────────────────────────────────────
#define MIC_SCK   19    // IO19_MIC_CLK — SCK del INMP441
#define MIC_WS     2    // IO2_MIC_WS   — WS/LR (R26=0Ω a GND → canal izq.)
#define MIC_SD    20    // IO20_MIC_SD  — SD (datos de audio del INMP441)
#define MIC_PORT  I2S_NUM_0   // I2S_NUM_1 reservado para speaker (audio_synth.h)

// ── Parámetros de captura ─────────────────────────────────────────────────
#define MIC_SAMPLE_RATE     8000
#define MIC_CHUNK_SAMPLES   512
#define MIC_CHUNK_BYTES     (MIC_CHUNK_SAMPLES * 2)   // 1024 bytes int16 mono

// ── Protocolo ─────────────────────────────────────────────────────────────
#define MAGIC_MIC  0xE5   // ESP32 → PC: 0xE5 + 1024 bytes PCM mic

// ── VAD (Voice Activity Detection) por RMS ───────────────────────────────
// RMS normalizado [0.0–1.0]. Ajustar si ambiente es muy ruidoso/silencioso.
#define VAD_THRESHOLD        0.015f   // umbral de activación de voz
#define VAD_VOICE_CHUNKS     2        // chunks > umbral para iniciar stream
#define VAD_SILENCE_CHUNKS   20       // chunks < umbral para cerrar utterance (~1.3s)
#define VAD_PREBUFFER_CHUNKS 3        // chunks previos enviados para no cortar el inicio

// ── Buffers en SRAM interna (DMA-accesibles) ─────────────────────────────
// DRAM_ATTR fuerza ubicación en IRAM/DRAM — el bus OPI de PSRAM no es apto para I2S DMA.
DRAM_ATTR static int16_t _mic_dma_buf[MIC_CHUNK_SAMPLES];
DRAM_ATTR static int16_t _mic_pre[VAD_PREBUFFER_CHUNKS][MIC_CHUNK_SAMPLES];

// Paquete de transmisión: [0xE5][1024 bytes] — escrito atómicamente en un Serial.write()
DRAM_ATTR static uint8_t _mic_tx_pkt[1 + MIC_CHUNK_BYTES];

static uint8_t _mic_pre_idx = 0;
static bool    _mic_i2s_ok  = false;

// ─────────────────────────────────────────────────────────────────────────
// mic_init_i2s() — llamar desde setup() antes de xTaskCreatePinnedToCore(MicTask)
// ─────────────────────────────────────────────────────────────────────────
void mic_init_i2s() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = MIC_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        // INMP441 LR=GND → datos en canal izquierdo. ONLY_LEFT entrega mono directo.
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = MIC_CHUNK_SAMPLES,   // 512 samples × 2B = 1024B por DMA buf
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };
    i2s_pin_config_t pins = {
        .bck_io_num   = MIC_SCK,
        .ws_io_num    = MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_SD
    };
    esp_err_t e1 = i2s_driver_install(MIC_PORT, &cfg, 0, NULL);
    esp_err_t e2 = i2s_set_pin(MIC_PORT, &pins);
    Serial.printf("[MIC] install=%s  set_pin=%s  SCK=%d WS=%d SD=%d port=NUM_%d\n",
                  esp_err_to_name(e1), esp_err_to_name(e2),
                  MIC_SCK, MIC_WS, MIC_SD, (int)MIC_PORT);
    _mic_i2s_ok = (e1 == ESP_OK && e2 == ESP_OK);
}

// ─────────────────────────────────────────────────────────────────────────
// Helpers internos
// ─────────────────────────────────────────────────────────────────────────

static float _mic_rms(const int16_t* buf, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float s = (float)buf[i] / 32768.0f;
        sum += s * s;
    }
    return sqrtf(sum / (float)n);
}

// Envío atómico: un solo Serial.write() garantiza que los bytes no se interleaven
// con otros writers (el ESP32-IDF protege uart_write_bytes() internamente).
static void _mic_send_chunk(const int16_t* samples) {
    _mic_tx_pkt[0] = MAGIC_MIC;
    memcpy(_mic_tx_pkt + 1, samples, MIC_CHUNK_BYTES);
    Serial.write(_mic_tx_pkt, sizeof(_mic_tx_pkt));
}

// ─────────────────────────────────────────────────────────────────────────
// MicTask — fijar en Core 0 junto al Serial y Audio
// ─────────────────────────────────────────────────────────────────────────
void MicTask(void* pv) {
    if (!_mic_i2s_ok) {
        Serial.println("[MIC] I2S no inicializado — MicTask cancelado");
        vTaskDelete(NULL);
        return;
    }
    Serial.println("[MIC] MicTask iniciado — 8kHz mono VAD activo");

    size_t bytes_read = 0;
    int voice_count   = 0;
    int silence_count = 0;
    bool streaming    = false;

    while (true) {
        i2s_read(MIC_PORT, _mic_dma_buf, MIC_CHUNK_BYTES, &bytes_read, portMAX_DELAY);
        if (bytes_read == 0) continue;

        float rms = _mic_rms(_mic_dma_buf, MIC_CHUNK_SAMPLES);

        if (!streaming) {
            // Rotar pre-buffer circular (guarda los últimos VAD_PREBUFFER_CHUNKS)
            memcpy(_mic_pre[_mic_pre_idx], _mic_dma_buf, MIC_CHUNK_BYTES);
            _mic_pre_idx = (_mic_pre_idx + 1) % VAD_PREBUFFER_CHUNKS;

            if (rms >= VAD_THRESHOLD) {
                if (++voice_count >= VAD_VOICE_CHUNKS) {
                    streaming     = true;
                    silence_count = 0;
                    // Enviar pre-buffer en orden cronológico (más antiguo primero)
                    for (int i = 0; i < VAD_PREBUFFER_CHUNKS; i++) {
                        _mic_send_chunk(_mic_pre[(_mic_pre_idx + i) % VAD_PREBUFFER_CHUNKS]);
                    }
                    Serial.println("[MIC] Voz detectada — stream ON");
                }
            } else {
                voice_count = 0;
            }
        } else {
            // Streaming activo: enviar chunk actual
            _mic_send_chunk(_mic_dma_buf);

            if (rms < VAD_THRESHOLD) {
                if (++silence_count >= VAD_SILENCE_CHUNKS) {
                    streaming     = false;
                    voice_count   = 0;
                    silence_count = 0;
                    Serial.println("[MIC] Silencio — stream OFF");
                }
            } else {
                silence_count = 0;
            }
        }
    }
}
