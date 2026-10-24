/**
 * ==============================================================================
 * RECOLECTOR DE AUDIO para grabar tu dataset (Método 2)
 *
 * Este programa NO detecta la wake word: solo manda el audio de tus micrófonos por
 * el cable USB para que `edge-impulse-data-forwarder` lo suba a Edge Impulse Studio.
 *
 * Manda UNA muestra mono por línea (mezcla (L+R)/2 de los dos INMP441) a 16 kHz,
 * como texto y a 2,000,000 baudios. Usa el mismo >>16 que la inferencia del
 * Método 2 (MIC_SAMPLE_SHIFT en ../include/config.h) y el Método 1, para que el
 * audio con el que entrenas suene igual al que la placa escuchará después.
 *
 * Uso:
 *   edge-impulse-data-forwarder --frequency 16000 --baud-rate 2000000
 * ==============================================================================
 */

#include <Arduino.h>
#include <driver/i2s.h>

static constexpr int PIN_I2S_SD  = 11;
static constexpr int PIN_I2S_SCK = 12;
static constexpr int PIN_I2S_WS  = 13;

static constexpr uint32_t SAMPLE_RATE_HZ = 16000;
static constexpr uint32_t SERIAL_BAUD = 2000000;
static constexpr size_t FRAMES_PER_READ = 256;
static constexpr int MIC_SAMPLE_SHIFT = 16; // igual que la inferencia del Metodo 2 y el Metodo 1

static int32_t i2sBuffer[FRAMES_PER_READ * 2];
static char textBuffer[FRAMES_PER_READ * 8 + 16]; // "-32768\r\n" = 8 bytes max

static bool setupI2S() {
    const i2s_config_t config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE_HZ,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = FRAMES_PER_READ,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    const i2s_pin_config_t pins = {
        .bck_io_num = PIN_I2S_SCK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_SD
    };
    if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;
    i2s_zero_dma_buffer(I2S_NUM_0);
    return true;
}

// Escribe un entero con signo en texto; devuelve cuantos bytes escribio.
static size_t writeInt(char* out, int value) {
    char tmp[8];
    size_t n = 0, len = 0;
    unsigned v = value < 0 ? -(long)value : value;
    if (value < 0) out[len++] = '-';
    do { tmp[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) out[len++] = tmp[--n];
    return len;
}

void setup() {
    Serial.setTxBufferSize(16384);
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    if (!setupI2S()) {
        while (true) delay(1000);
    }
}

void loop() {
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, i2sBuffer, sizeof(i2sBuffer), &bytesRead, portMAX_DELAY);
    const size_t frames = bytesRead / (2 * sizeof(int32_t));
    size_t len = 0;
    for (size_t i = 0; i < frames; i++) {
        const int left = static_cast<int16_t>(i2sBuffer[i * 2] >> MIC_SAMPLE_SHIFT);
        const int right = static_cast<int16_t>(i2sBuffer[i * 2 + 1] >> MIC_SAMPLE_SHIFT);
        len += writeInt(textBuffer + len, (left + right) / 2);
        textBuffer[len++] = '\r';
        textBuffer[len++] = '\n';
    }
    Serial.write(reinterpret_cast<const uint8_t*>(textBuffer), len);
}
