/**
 * ==============================================================================
 * PROYECTO: Wake Word 100% Local en ESP32-S3
 * MÉTODO 2: Red neuronal (TinyML) entrenada en Edge Impulse
 *
 * ¿Qué hace este programa?
 *   - Escucha sin parar con 2 micrófonos y guarda los últimos 2 segundos de audio.
 *   - Cada 500 ms le pregunta a la red neuronal: "¿en estos 2 s dijeron la palabra?"
 *   - Si está lo bastante segura (ver CONFIDENCE_THRESHOLD en include/config.h),
 *     enciende el aro de LEDs 2 segundos.
 *
 * Funciona con la voz de CUALQUIER persona, siempre que el modelo se haya entrenado
 * con varias voces. El modelo vive en lib/wake_word_inferencing (lo exportas de Edge Impulse).
 *
 * Hardware (ver README.md de la carpeta del proyecto):
 *   - 2 micrófonos INMP441 en IO11 (SD), IO12 (SCK), IO13 (WS): uno en L y otro en R
 *   - Aro de 16 LEDs WS2812 en IO17
 * ==============================================================================
 */

#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

// ── Modelo Edge Impulse ───────────────────────────────────────────────────────
#include <wake_word_inferencing.h>

// ── Instancias y Buffers ──────────────────────────────────────────────────────
// Buffer circular de audio: la tarea de captura (Core 0) lo llena sin parar y
// el loop (Core 1) copia la ventana completa de 2 s en windowBuf para clasificarla.
static constexpr size_t WINDOW_SAMPLES = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
static int16_t ringBuf[WINDOW_SAMPLES];
static int16_t windowBuf[WINDOW_SAMPLES];
static volatile uint32_t ringTotalSamples = 0; // muestras escritas desde el arranque
static int32_t i2sBuffer[256];

Adafruit_NeoPixel ledRing(LED_RING_COUNT, PIN_LED_OUT, NEO_GRB + NEO_KHZ800);

unsigned long triggerTimestamp = 0;
bool isTriggered = false;
bool audioReady = false;
static bool eiAllocatorReported = false;

// Enciende el aro alternando verde y blanco.
static void ledRingShowWakeWord() {
    for (uint16_t i = 0; i < LED_RING_COUNT; i++) {
        uint32_t color = (i % 2 == 0)
            ? ledRing.Color(0, 255, 0)      // verde
            : ledRing.Color(255, 255, 255); // blanco
        ledRing.setPixelColor(i, color);
    }
    ledRing.show();
}

static void ledRingOff() {
    ledRing.clear();
    ledRing.show();
}

void *ei_malloc(size_t size) {
    // El SDK de Edge Impulse a veces pide 0 bytes (p. ej. al cerrar la ventana
    // deslizante del clasificador continuo con 0 frames pendientes) y espera
    // recibir un puntero valido, como hace glibc. heap_caps_*alloc en ESP-IDF
    // devuelve NULL para una reserva de 0 bytes, lo que el SDK interpreta como
    // EIDSP_OUT_OF_MEM. Forzamos un minimo de 1 byte para evitar ese falso OOM.
    if (size == 0) {
        size = 1;
    }
    void *memory = heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        memory = heap_caps_aligned_alloc(16, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (memory && !eiAllocatorReported) {
        ei_printf("[EI_ALLOC] inicializado; heap=%u, psram=%u\n",
            (unsigned)ESP.getFreeHeap(),
            (unsigned)ESP.getFreePsram());
        eiAllocatorReported = true;
    }
    if (!memory) {
        ei_printf("[EI_ALLOC] fallo malloc %u bytes; heap=%u, psram=%u\n",
            (unsigned)size,
            (unsigned)ESP.getFreeHeap(),
            (unsigned)ESP.getFreePsram());
    }
    return memory;
}

void *ei_calloc(size_t nitems, size_t size) {
    if (size != 0 && nitems > SIZE_MAX / size) {
        ei_printf("[EI_ALLOC] overflow en calloc nitems=%u size=%u\n", (unsigned)nitems, (unsigned)size);
        return nullptr;
    }

    size_t totalSize = nitems * size;
    if (totalSize == 0) {
        totalSize = 1;
    }
    void *memory = heap_caps_calloc(1, totalSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        memory = heap_caps_calloc(1, totalSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (memory && !eiAllocatorReported) {
        ei_printf("[EI_ALLOC] inicializado; heap=%u, psram=%u\n",
            (unsigned)ESP.getFreeHeap(),
            (unsigned)ESP.getFreePsram());
        eiAllocatorReported = true;
    }
    if (!memory) {
        ei_printf("[EI_ALLOC] fallo calloc %u bytes; heap=%u, psram=%u\n",
            (unsigned)totalSize,
            (unsigned)ESP.getFreeHeap(),
            (unsigned)ESP.getFreePsram());
    }
    return memory;
}

void ei_free(void *memory) {
    free(memory);
}

// ── Prototipos de Función ─────────────────────────────────────────────────────
bool setupI2S();
void captureTask(void *param);

static int audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    if (offset + length > WINDOW_SAMPLES) {
        return -1;
    }
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = (float)windowBuf[offset + i];
    }
    return 0;
}

// ==============================================================================
// SETUP
// ==============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    ei_printf("ESP32-S3 TinyML Wake Word (Metodo 2: Edge Impulse INT8)\n");
    ei_printf("heap_free=%u bytes; psram_size=%u bytes; psram_free=%u bytes\n",
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
    if (ESP.getPsramSize() == 0) {
        ei_printf("WARN: No se detecto PSRAM; verifica la placa y memoria configuradas.\n");
    }

    ledRing.begin();
    ledRing.setBrightness(80); // limita el consumo de los 16 LEDs WS2812B
    ledRingOff();

    audioReady = setupI2S();
    if (!audioReady) {
        return;
    }
    ei_printf("Dos microfonos I2S L/R mezclados a mono. Modelo Edge Impulse wake_word cargado.\n");
    ei_printf("sample_rate=%d Hz; ventana=%d muestras (2 s); inferencia cada %d muestras (500 ms)\n",
        (int)EI_CLASSIFIER_FREQUENCY, (int)WINDOW_SAMPLES, (int)EI_CLASSIFIER_SLICE_SIZE);

    // Captura continua en el Core 0; el loop (Core 1) clasifica la ventana completa.
    xTaskCreatePinnedToCore(captureTask, "audio_capture", 4096, nullptr, 5, nullptr, 0);

    ei_printf("Iniciando bucle de inferencia (ventana completa de 2 s, buffer circular)...\n");
}

// ==============================================================================
// LOOP PRINCIPAL
// ==============================================================================
void loop() {
    if (!audioReady) {
        delay(1000);
        return;
    }

    // Apagado del aro de LEDs tras 2 segundos
    if (isTriggered && (millis() - triggerTimestamp >= 2000)) {
        ledRingOff();
        isTriggered = false;
        ei_printf("Listo para nueva deteccion.\n");
    }

    // Clasifica la ventana completa de 2 s cada vez que entran 500 ms de audio nuevo.
    static uint32_t lastClassifiedTotal = 0;
    const uint32_t total = ringTotalSamples;
    if (total < WINDOW_SAMPLES || total - lastClassifiedTotal < EI_CLASSIFIER_SLICE_SIZE) {
        delay(10);
        return;
    }
    lastClassifiedTotal = total;

    // Copia la ventana en orden cronologico: la muestra mas vieja esta en total % WINDOW_SAMPLES.
    const size_t oldest = total % WINDOW_SAMPLES;
    memcpy(windowBuf, ringBuf + oldest, (WINDOW_SAMPLES - oldest) * sizeof(int16_t));
    memcpy(windowBuf + (WINDOW_SAMPLES - oldest), ringBuf, oldest * sizeof(int16_t));

    signal_t signal;
    signal.total_length = WINDOW_SAMPLES;
    signal.get_data = &audio_signal_get_data;

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

    if (res == EI_IMPULSE_OK) {
        float maxConfidence = 0.0f;
        const char* bestLabel = "";

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            float val = result.classification[ix].value;
            if (val > maxConfidence) {
                maxConfidence = val;
                bestLabel = result.classification[ix].label;
            }
        }

        // Nivel de la ventana (RMS y pico) para compararlo con las muestras del Studio.
        double sumSquares = 0.0;
        int peak = 0;
        for (size_t i = 0; i < WINDOW_SAMPLES; i++) {
            const int v = windowBuf[i];
            sumSquares += (double)v * v;
            if (abs(v) > peak) peak = abs(v);
        }
        ei_printf("mejor=%s (%.0f%%) |", bestLabel, maxConfidence * 100.0f);
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            ei_printf(" %s=%.0f%%", result.classification[ix].label, result.classification[ix].value * 100.0f);
        }
        ei_printf(" | volumen(rms)=%.0f pico=%d\n", sqrt(sumSquares / WINDOW_SAMPLES), peak);

        // Evaluar si coincide con la Wake Word
        // La palabra aparece en 3-4 ventanas seguidas y su probabilidad se reparte entre ellas,
        // asi que ademas del umbral de una sola lectura se acepta que dos lecturas consecutivas
        // sean altas las dos (promedio sostenido).
        float wakeProb = 0.0f;
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            if (strcmp(result.classification[ix].label, WAKEWORD_LABEL) == 0) {
                wakeProb = result.classification[ix].value;
            }
        }
        static float previousWakeProb = 0.0f;
        const bool singleHit = wakeProb >= CONFIDENCE_THRESHOLD;
        const bool sustainedHit = wakeProb >= SUSTAINED_MIN_PROB && previousWakeProb >= SUSTAINED_MIN_PROB
            && (wakeProb + previousWakeProb) / 2.0f >= SUSTAINED_MEAN_PROB;
        previousWakeProb = wakeProb;

        if (singleHit || sustainedHit) {
            if (!isTriggered) {
                ei_printf(">>> WAKE WORD DETECTADA POR RED NEURONAL INT8 <<< (%s, confianza=%.2f%%)\n",
                    singleHit ? "una lectura" : "dos lecturas seguidas", wakeProb * 100.0f);

                ledRingShowWakeWord();
                triggerTimestamp = millis();
                isTriggered = true;
            }
        }
    } else {
        ei_printf("ERR: Fallo la inferencia del modelo Edge Impulse. Codigo de error: %d\n", res);
    }
}

// ==============================================================================
// CONFIGURACIÓN I2S
// ==============================================================================
bool setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = EI_CLASSIFIER_FREQUENCY,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
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

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        ei_printf("ERR: No se pudo instalar el driver I2S. Codigo de error: %d\n", err);
        return false;
    }

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) {
        ei_printf("ERR: No se pudieron configurar los pines I2S. Codigo de error: %d\n", err);
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }

    i2s_zero_dma_buffer(I2S_NUM_0);
    return true;
}

// 
// Tarea de captura (Core 0): lee I2S sin parar, convierte a 16 bits, mezcla L+R
// a mono y lo escribe en el buffer circular.
void captureTask(void *param) {
    // I2S entrega las muestras intercaladas: L0, R0, L1, R1, ...
    // Cada muestra llega en una palabra de 32 bits (ver MIC_SAMPLE_SHIFT en config.h).
    // La convertimos a 16 bits y mezclamos los dos canales en uno solo (mono).
    const size_t maxFrames = sizeof(i2sBuffer) / (2 * sizeof(int32_t));
    uint32_t writeTotal = 0;
    while (true) {
        size_t bytesRead = 0;
        esp_err_t err = i2s_read(I2S_NUM_0, i2sBuffer, maxFrames * 2 * sizeof(int32_t), &bytesRead, portMAX_DELAY);
        if (err != ESP_OK || bytesRead == 0 || bytesRead % (2 * sizeof(int32_t)) != 0) {
            ei_printf("ERR: Error al capturar audio I2S.\n");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const size_t framesRead = bytesRead / (2 * sizeof(int32_t));
        for (size_t i = 0; i < framesRead; i++) {
            int16_t left  = (int16_t)(i2sBuffer[i * 2] >> MIC_SAMPLE_SHIFT);
            int16_t right = (int16_t)(i2sBuffer[i * 2 + 1] >> MIC_SAMPLE_SHIFT);
            ringBuf[writeTotal % WINDOW_SAMPLES] = (int16_t)(((int32_t)left + right) / 2);
            writeTotal++;
        }
        ringTotalSamples = writeTotal;
    }
}
