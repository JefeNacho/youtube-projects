/**
 * ==============================================================================
 * PROYECTO: Wake Word 100% Local en ESP32-S3
 * MÉTODO 1: Enrolamiento en vivo con MFCC + DTW (reconoce SOLO tu voz)
 *
 * ¿Qué hace este programa?
 *   1. Escribes 'c' en el monitor serial y guardas silencio 10 s (mide el ruido del cuarto).
 *   2. Escribes 'e' y dices tu palabra (ej. "Hey Bot") 3 veces: el ESP32 guarda tu huella de voz.
 *   3. Desde ese momento, cada vez que digas la palabra, el aro de LEDs se enciende en arcoíris.
 *
 * Hardware (ver README.md de la carpeta del proyecto):
 *   - 2 micrófonos INMP441 en IO11 (SD), IO12 (SCK), IO13 (WS)
 *   - Aro de 16 LEDs WS2812 en IO17
 *
 * Todos los números que puedes ajustar están en include/config.h
 * ==============================================================================
 */

#include <Arduino.h>
#include <driver/i2s.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "mfcc_extractor.h"
#include "dtw_matcher.h"
#include "log.h"

// ── Instancias Globales ───────────────────────────────────────────────────────
Preferences preferences;
MFCCExtractor mfccExtractor;
Adafruit_NeoPixel ledRing(LED_RING_COUNT, PIN_LED_OUT, NEO_GRB + NEO_KHZ800);

// ── Buffers en Memoria SRAM ───────────────────────────────────────────────────
// 24000 muestras de 16 bits = 48 KB.
int16_t audioRawBuffer[TOTAL_SAMPLES];

// Matriz MFCC del audio capturado en tiempo real [61 frames][13 coeficientes]
float liveMFCC[NUM_FRAMES][NUM_MFCC_COEFFS];

// Plantillas de voz enroladas almacenadas en memoria [3 repeticiones][61][13]
float enrolledMFCC[NUM_ENROLL_TEMPLATES][NUM_FRAMES][NUM_MFCC_COEFFS];
int enrolledCount = 0;
bool isEnrolled = false;

// ── Variables de Estado y Temporización ───────────────────────────────────────
enum SystemState {
    STATE_IDLE,
    STATE_LISTENING,
    STATE_ENROLLING,
    STATE_TRIGGERED
};

SystemState currentState = STATE_IDLE;
unsigned long triggerTimestamp = 0;
float ambientNoiseRMSLeft = 0.0f;
float ambientNoiseRMSRight = 0.0f;
float ambientNoiseStdDevLeft = 0.0f;
float ambientNoiseStdDevRight = 0.0f;
bool useLeftMicrophone = true;
bool stereoVoiceWindowNeedsReset = true;
bool noiseCalibrated = false;

static const uint8_t VAD_MIN_VOICE_CHUNKS =
    ((VAD_MIN_VOICE_DURATION_MS * AUDIO_SAMPLE_RATE / 1000 - FFT_SIZE + FRAME_STEP - 1) / FRAME_STEP) + 1;

// ── Prototipos de Función ─────────────────────────────────────────────────────
void setupI2S();
void readStereoChunk(int16_t* left, int16_t* right, int samples);
void readAudioChunk(int16_t* buffer, int samples);
void readStereoVoiceWindow(int16_t* left, int16_t* right);
void recordAudioWindow(int16_t* buffer, int totalSamples);
bool calibrateAmbientNoise();
bool loadCalibrationFromNVS();
bool saveCalibrationToNVS();
float getVoiceThreshold(bool leftChannel);
float getVoiceReleaseThreshold(bool leftChannel);
void appendVoiceWindow(const int16_t* chunk, uint16_t previousWindows, uint16_t& capturedSamples);
void finishVoiceCapture(uint16_t capturedSamples);
void loadTemplatesFromNVS();
void saveTemplatesToNVS();
void handleEnrollment();
void checkWakeWordInference();
void runMicrophoneTest();
void runAudioDump(uint32_t durationMs);
void runHardwareDiagnostic();
void printHelp();
void printAudioBar(const char* label, float rms, float scale);
void showLearningAnimation(uint8_t position);
void showWakeWordAnimation(uint8_t frame);
void clearLedRing();

// ==============================================================================
// SETUP
// ==============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    logLinea('=', 38);
    logInfo(F("Iniciando ESP32-S3 Wake Word (Metodo 1: MFCC + DTW)"));
    logLinea('=', 38);

    pinMode(PIN_LED_OUT, OUTPUT);
    digitalWrite(PIN_LED_OUT, LOW);
    ledRing.begin();
    clearLedRing();

    // Inicializar DSP y Extractor de características
    mfccExtractor.init();
    logInfo(F("DSP Hamming & Banco Mel 26 bandas inicializado."));

    // Inicializar periférico I2S
    setupI2S();
    logInfo(F("Bus I2S (IO11 SD, IO12 SCK, IO13 WS) a 16kHz configurado."));

    // Cargar plantillas de voz desde memoria Flash (NVS)
    loadTemplatesFromNVS();
    noiseCalibrated = loadCalibrationFromNVS();

    if (noiseCalibrated) {
        logInfo(F("Ultima calibracion cargada desde Flash."));
    } else {
        logInfo(F("Calibracion requerida: envia c para calibrar durante 10 segundos."));
    }
    if (!isEnrolled) {
        logAviso(F("No hay plantilla de voz guardada."));
    } else {
        logInfo(F("Plantilla guardada."));
    }
    printHelp();

    currentState = STATE_LISTENING;
}

// ==============================================================================
// LOOP PRINCIPAL
// ==============================================================================
void loop() {
    // 1. Iniciar enrolamiento sólo mediante el comando 'e' del monitor serie
    if (Serial.available() > 0) {
        char command = (char)Serial.read();
        if (command == 'e' || command == 'E') {
            if (!noiseCalibrated) {
                logAviso(F("Primero calibra el ruido: guarda silencio y escribe c."));
                return;
            }
            handleEnrollment();
            return;
        }
        if (command == 'c' || command == 'C') {
            noiseCalibrated = false;
            noiseCalibrated = calibrateAmbientNoise();
            currentState = STATE_LISTENING;
            if (noiseCalibrated) {
                if (saveCalibrationToNVS()) {
                    logInfo(F("Calibracion guardada. Ya puedes enrolar con e."));
                } else {
                    logError(F("Calibracion activa, pero no se pudo guardar en Flash."));
                }
            } else {
                logAviso(F("No se completo la calibracion."));
            }
            return;
        }
        if (command == 'h' || command == 'H' || command == '?') {
            printHelp();
            return;
        }
        if (command == 'm' || command == 'M') {
            runMicrophoneTest();
            return;
        }
        if (command == 'l') {
            // Prueba del aro sin la librería Adafruit: rutina RMT del core de Arduino (solo el primer LED).
            const uint8_t seq[4][3] = {{40, 0, 0}, {0, 40, 0}, {0, 0, 40}, {40, 40, 40}};
            Serial.println(F("Prueba LED #1 con neopixelWrite: rojo, verde, azul, blanco."));
            for (const auto& c : seq) {
                neopixelWrite(PIN_LED_OUT, c[0], c[1], c[2]);
                delay(1000);
            }
            neopixelWrite(PIN_LED_OUT, 0, 0, 0);
            Serial.println(F("Fin de la prueba."));
            return;
        }
        if (command == 'x') {
            runHardwareDiagnostic();
            return;
        }
        if (command == 'd') {
            runAudioDump(1500);
            return;
        }
        if (command == 'D') {
            runAudioDump(20000);
            return;
        }
    }

    // 2. Apagado del LED tras el tiempo de activación
    if (currentState == STATE_TRIGGERED) {
        if (millis() - triggerTimestamp >= 2000) { // 2 segundos activo
            clearLedRing();
            currentState = STATE_LISTENING;
            logInfo(F("Retornando a escucha continua..."));
        }
    }

    // 3. Proceso de inferencia continua
    if (currentState == STATE_LISTENING && isEnrolled && noiseCalibrated) {
        checkWakeWordInference();
    }
    delay(5);
}

// Muestra los comandos que puedes escribir en el monitor serial (y pulsar Enter).
void printHelp() {
    Serial.println(F("Comandos (escríbelos en el monitor serial y pulsa Enter):"));
    Serial.println(F("  c  = calibrar el ruido del cuarto (10 s en silencio)  <- primero esto"));
    Serial.println(F("  e  = enrolar tu palabra (la dices 3 veces)            <- luego esto"));
    Serial.println(F("  m  = probar los micrófonos (barras de volumen)"));
    Serial.println(F("  h  = mostrar esta ayuda"));
    Serial.println(F("Avanzados: d / D = volcar audio crudo 1.5 s / 20 s, x = diagnóstico, l = probar LED 1"));
}

void runMicrophoneTest() {
    const unsigned long reportIntervalMs = 250;
    unsigned long lastReport = millis();
    int16_t left[512];
    int16_t right[512];
    uint8_t colorIndex = 0;

    logInfo(F("Prueba de microfono activa. Pulsa cualquier tecla para terminar."));
    logInfo(F("Habla cuando quieras; las barras muestran ambos microfonos."));

    while (true) {
        bool stopRequested = false;
        while (Serial.available() > 0) {
            int key = Serial.read();
            if (key != '\r' && key != '\n') stopRequested = true;
        }
        if (stopRequested) break;
        readStereoChunk(left, right, 512);
        float leftRMS = MFCCExtractor::calculateRMS(left, 512);
        float rightRMS = MFCCExtractor::calculateRMS(right, 512);

        if (millis() - lastReport >= reportIntervalMs) {
            Serial.print(F("\r\033[2K"));
            printAudioBar("L", leftRMS, 5000.0f);
            Serial.print(F("  "));
            printAudioBar("R", rightRMS, 5000.0f);
            Serial.print(F("  (RMS "));
            Serial.print(leftRMS, 0);
            Serial.print(F(" / "));
            Serial.print(rightRMS, 0);
            Serial.print(')');
            ledRing.clear();
            ledRing.setPixelColor(colorIndex++ % LED_RING_COUNT, ledRing.Color(0, 30, 80));
            ledRing.show();
            lastReport = millis();
        }
    }

    Serial.println();
    logInfo(F("Prueba de microfono terminada."));
    ledRing.clear();
    ledRing.show();
    currentState = STATE_LISTENING;
}

// Captura audio crudo de ambos micrófonos (sin VAD ni selección de canal) y lo envía
// por serie para dump_audio_esp32.py. Se graba primero y se envía después, para que
// el tráfico serie no meta clics en la señal.
// Protocolo: "AUDIODUMP <sr> <muestras/canal> <canales> <baudios>\n", el script responde 'A',
// el ESP32 contesta "AUDIODUMP_FAST\n", cambia de velocidad y manda int16 LE entrelazado L,R.
void runAudioDump(uint32_t durationMs) {
    const uint32_t fastBaud = 460800;
    const uint32_t samplesPerChannel = (uint32_t)AUDIO_SAMPLE_RATE * durationMs / 1000;
    const size_t totalSamples = (size_t)samplesPerChannel * 2;
    int16_t* dump = (int16_t*)ps_malloc(totalSamples * sizeof(int16_t));
    if (!dump) {
        Serial.println(F("AUDIODUMP_ERROR,sin memoria PSRAM"));
        return;
    }

    int16_t left[512];
    int16_t right[512];
    i2s_zero_dma_buffer(I2S_NUM_0);
    for (uint32_t done = 0; done < samplesPerChannel; done += 512) {
        const int n = (int)min((uint32_t)512, samplesPerChannel - done);
        readStereoChunk(left, right, n);
        for (int i = 0; i < n; i++) {
            dump[(done + i) * 2] = left[i];
            dump[(done + i) * 2 + 1] = right[i];
        }
    }

    Serial.printf("AUDIODUMP %d %u 2 %u\n", AUDIO_SAMPLE_RATE, (unsigned)samplesPerChannel, (unsigned)fastBaud);
    Serial.flush();

    unsigned long waitStart = millis();
    bool fastRequested = false;
    while (millis() - waitStart < 3000) {
        if (Serial.available() > 0 && Serial.read() == 'A') {
            fastRequested = true;
            break;
        }
    }
    if (!fastRequested) {
        free(dump);
        Serial.println(F("AUDIODUMP_ERROR,sin respuesta del script"));
        return;
    }

    Serial.println(F("AUDIODUMP_FAST"));
    Serial.flush();
    Serial.updateBaudRate(fastBaud);
    delay(50);
    Serial.write((const uint8_t*)dump, totalSamples * sizeof(int16_t));
    Serial.flush();
    delay(50);
    Serial.updateBaudRate(115200);
    delay(50);
    Serial.println();
    Serial.println(F("AUDIODUMP_END"));
    free(dump);
    i2s_zero_dma_buffer(I2S_NUM_0);
    stereoVoiceWindowNeedsReset = true;
    currentState = STATE_LISTENING;
}

// Diagnóstico de hardware (comando 'x'): estadísticas de las palabras I2S crudas y prueba del aro.
// Un INMP441 sano manda 24 bits alineados a la izquierda: los 8 bits bajos de cada palabra
// valen 0 y la señal en silencio es pequeña. Con la SD en pull-down, si nadie maneja la línea
// la lectura debe caer a ~0.
static void printRawStats(const char* title) {
    static int32_t raw[1024];
    size_t bytesRead = 0;
    i2s_zero_dma_buffer(I2S_NUM_0);
    for (int i = 0; i < 4; i++) i2s_read(I2S_NUM_0, raw, sizeof(raw), &bytesRead, portMAX_DELAY);
    for (int ch = 0; ch < 2; ch++) {
        int lowByteZero = 0;
        int64_t sum = 0;
        int32_t minV = INT32_MAX, maxV = INT32_MIN;
        for (int i = 0; i < 512; i++) {
            int32_t w = raw[i * 2 + ch];
            if ((w & 0xFF) == 0) lowByteZero++;
            int32_t v = w >> 8;
            sum += v;
            minV = min(minV, v);
            maxV = max(maxV, v);
        }
        Serial.printf("%s %c: bits bajos en 0 = %d%%, media(24b) = %d, min = %d, max = %d\n",
                      title, ch == 0 ? 'L' : 'R', lowByteZero * 100 / 512,
                      (int)(sum / 512), (int)minV, (int)maxV);
    }
}

void runHardwareDiagnostic() {
    Serial.println(F("--- Diagnostico de hardware ---"));
    printRawStats("SD normal   ");

    gpio_set_pull_mode((gpio_num_t)PIN_I2S_SD, GPIO_PULLDOWN_ONLY);
    delay(50);
    printRawStats("SD pulldown ");
    gpio_set_pull_mode((gpio_num_t)PIN_I2S_SD, GPIO_FLOATING);

    Serial.println(F("Aro de LEDs: rojo, verde, azul, blanco (1 s cada uno)..."));
    const uint8_t colors[4][3] = {{64, 0, 0}, {0, 64, 0}, {0, 0, 64}, {64, 64, 64}};
    for (const auto& color : colors) {
        for (uint8_t i = 0; i < LED_RING_COUNT; i++) ledRing.setPixelColor(i, ledRing.Color(color[0], color[1], color[2]));
        ledRing.show();
        delay(1000);
    }
    clearLedRing();
    i2s_zero_dma_buffer(I2S_NUM_0);
    stereoVoiceWindowNeedsReset = true;
    Serial.println(F("--- Fin del diagnostico ---"));
}

void printAudioBar(const char* label, float rms, float scale) {
    const uint8_t barWidth = 24;
    uint8_t filled = (uint8_t)constrain((int)(rms * barWidth / scale), 0, barWidth);
    Serial.print(label);
    Serial.print(F(" ["));
    for (uint8_t i = 0; i < barWidth; i++) {
        Serial.print(i < filled ? '#' : '.');
    }
    Serial.print(']');
}

void clearLedRing() {
    ledRing.clear();
    ledRing.show();
}

void showLearningAnimation(uint8_t position) {
    ledRing.clear();
    for (uint8_t i = 0; i < LED_RING_COUNT; i++) {
        uint8_t distance = (i + LED_RING_COUNT - position) % LED_RING_COUNT;
        uint8_t brightness = distance == 0 ? 180 : (distance == 1 ? 60 : 8);
        ledRing.setPixelColor(i, ledRing.Color(brightness, brightness / 3, 0));
    }
    ledRing.show();
}

void showWakeWordAnimation(uint8_t frame) {
    for (uint8_t i = 0; i < LED_RING_COUNT; i++) {
        uint8_t hue = (uint8_t)(frame * 8 + i * (256 / LED_RING_COUNT));
        ledRing.setPixelColor(i, ledRing.gamma32(ledRing.ColorHSV((uint16_t)hue * 256, 255, 180)));
    }
    ledRing.show();
}

// ==============================================================================
// CONFIGURACIÓN DEL DRIVER I2S
// ==============================================================================
void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Dos INMP441: uno L/R a GND y otro a 3.3V
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
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

// ==============================================================================
// CAPTURA DE AUDIO Y VAD (Voice Activity Detection)
// ==============================================================================
void readAudioChunk(int16_t* buffer, int samples) {
    static int16_t left[512];
    static int16_t right[512];
    readStereoChunk(left, right, samples);
    const int16_t* selectedChannel = useLeftMicrophone ? left : right;
    memcpy(buffer, selectedChannel, samples * sizeof(int16_t));
}

void readStereoVoiceWindow(int16_t* left, int16_t* right) {
    static int16_t leftOverlap[512];
    static int16_t rightOverlap[512];
    static bool initialized = false;

    if (!initialized || stereoVoiceWindowNeedsReset) {
        readStereoChunk(leftOverlap, rightOverlap, 512);
        initialized = true;
        stereoVoiceWindowNeedsReset = false;
    } else {
        memmove(leftOverlap, leftOverlap + 256, 256 * sizeof(int16_t));
        memmove(rightOverlap, rightOverlap + 256, 256 * sizeof(int16_t));
        readStereoChunk(leftOverlap + 256, rightOverlap + 256, 256);
    }
    memcpy(left, leftOverlap, 512 * sizeof(int16_t));
    memcpy(right, rightOverlap, 512 * sizeof(int16_t));
}

void readStereoChunk(int16_t* left, int16_t* right, int samples) {
    static int32_t rawSamples[1024];
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, (void*)rawSamples, samples * 2 * sizeof(int32_t), &bytesRead, portMAX_DELAY);

    // El INMP441 entrega audio firmado de 24 bits alineado a la izquierda en
    // cada palabra I2S de 32 bits. >>16 conserva sus 16 bits mas significativos;
    // usar >>14 y convertir a int16_t desborda y envuelve las muestras fuertes.
    for (int i = 0; i < samples; i++) {
        left[i] = (int16_t)(rawSamples[i * 2] >> 16);
        right[i] = (int16_t)(rawSamples[i * 2 + 1] >> 16);
    }
}

void recordAudioWindow(int16_t* buffer, int totalSamples) {
    for (int offset = 0; offset < totalSamples; offset += 512) {
        int samples = min(512, totalSamples - offset);
        readAudioChunk(buffer + offset, samples);
    }
}

bool calibrateAmbientNoise() {
    logInfo(F("Calibrando ruido ambiente durante 10 segundos; guarda silencio..."));
    stereoVoiceWindowNeedsReset = true;
    double leftMean = 0.0;
    double leftSquaredDeviation = 0.0;
    double rightMean = 0.0;
    double rightSquaredDeviation = 0.0;
    uint32_t chunkCount = 0;
    unsigned long startTime = millis();
    int16_t left[512];
    int16_t right[512];

    while (millis() - startTime < NOISE_CALIBRATION_MS) {
        readStereoChunk(left, right, 512);
        double leftRMS = MFCCExtractor::calculateRMS(left, 512);
        double rightRMS = MFCCExtractor::calculateRMS(right, 512);
        chunkCount++;
        double leftDelta = leftRMS - leftMean;
        leftMean += leftDelta / chunkCount;
        leftSquaredDeviation += leftDelta * (leftRMS - leftMean);
        double rightDelta = rightRMS - rightMean;
        rightMean += rightDelta / chunkCount;
        rightSquaredDeviation += rightDelta * (rightRMS - rightMean);
    }
    if (chunkCount == 0) {
        i2s_zero_dma_buffer(I2S_NUM_0);
        stereoVoiceWindowNeedsReset = true;
        return false;
    }
    ambientNoiseRMSLeft = (float)leftMean;
    ambientNoiseRMSRight = (float)rightMean;
    ambientNoiseStdDevLeft = chunkCount > 1
        ? (float)sqrt(leftSquaredDeviation / (chunkCount - 1))
        : 0.0f;
    ambientNoiseStdDevRight = chunkCount > 1
        ? (float)sqrt(rightSquaredDeviation / (chunkCount - 1))
        : 0.0f;
    i2s_zero_dma_buffer(I2S_NUM_0);
    stereoVoiceWindowNeedsReset = true;
    return true;
}

void appendVoiceWindow(const int16_t* chunk, uint16_t previousWindows, uint16_t& capturedSamples) {
    const uint16_t offset = previousWindows == 0 ? 0 : FFT_SIZE + (previousWindows - 1) * FRAME_STEP;
    const uint16_t sourceOffset = previousWindows == 0 ? 0 : FFT_SIZE - FRAME_STEP;
    const uint16_t samplesToCopy = previousWindows == 0 ? FFT_SIZE : FRAME_STEP;
    if (offset >= TOTAL_SAMPLES) return;
    const uint16_t boundedSamples = min((uint16_t)(TOTAL_SAMPLES - offset), samplesToCopy);
    memcpy(audioRawBuffer + offset, chunk + sourceOffset, boundedSamples * sizeof(int16_t));
    capturedSamples = offset + boundedSamples;
}

void finishVoiceCapture(uint16_t capturedSamples) {
    if (capturedSamples < TOTAL_SAMPLES) {
        recordAudioWindow(audioRawBuffer + capturedSamples, TOTAL_SAMPLES - capturedSamples);
    }
}

float getVoiceThreshold(bool leftChannel) {
    float noiseRMS = leftChannel ? ambientNoiseRMSLeft : ambientNoiseRMSRight;
    float noiseStdDev = leftChannel ? ambientNoiseStdDevLeft : ambientNoiseStdDevRight;
    if (noiseRMS < 200.0f) noiseRMS = 200.0f;
    float baselineThreshold = noiseRMS + fmaxf(VAD_ENERGY_THRESHOLD,
                                                fmaxf(noiseRMS * VAD_NOISE_MARGIN,
                                                      noiseStdDev * VAD_STD_MULTIPLIER));
    return fmaxf(baselineThreshold, noiseRMS * 1.8f);
}

float getVoiceReleaseThreshold(bool leftChannel) {
    return getVoiceThreshold(leftChannel) * 0.8f;
}

// ==============================================================================
// RUTINA DE ENROLAMIENTO (3 REPETICIONES)
// ==============================================================================
void handleEnrollment() {
    currentState = STATE_ENROLLING;
    logLinea('*', 38);
    logAviso(F("MODO ENROLAMIENTO ACTIVADO"));
    logInfo(F("Di tu Wake Word ('Hey Bot') claramente 3 veces."));
    logLinea('*', 38);

    enrolledCount = 0;

    for (int sample = 0; sample < NUM_ENROLL_TEMPLATES; sample++) {
        logInfo(F("Esperando que hables... (Di la palabra ya)"));
        bool voiceDetected = false;
        unsigned long waitStart = millis();
        unsigned long lastAnimation = 0;
        uint8_t animationPosition = 0;
        uint8_t consecutiveVoiceChunks = 0;
        uint16_t capturedVoiceWindows = 0;
        uint16_t capturedVoiceSamples = 0;
        bool selectedChannelLocked = false;
        bool selectedLeftChannel = true;
        int16_t leftChunk[512];
        int16_t rightChunk[512];

        while (!voiceDetected && (millis() - waitStart < ENROLL_WAIT_TIMEOUT_MS)) {
            readStereoVoiceWindow(leftChunk, rightChunk);
            if (millis() - lastAnimation >= 80) {
                showLearningAnimation(animationPosition++ % LED_RING_COUNT);
                lastAnimation = millis();
            }
            float leftRMS = MFCCExtractor::calculateRMS(leftChunk, FFT_SIZE);
            float rightRMS = MFCCExtractor::calculateRMS(rightChunk, FFT_SIZE);
            const bool sustaining = consecutiveVoiceChunks >= VAD_REQUIRED_CHUNKS;
            float leftThreshold = sustaining ? getVoiceReleaseThreshold(true) : getVoiceThreshold(true);
            float rightThreshold = sustaining ? getVoiceReleaseThreshold(false) : getVoiceThreshold(false);
            float leftExcess = leftRMS - leftThreshold;
            float rightExcess = rightRMS - rightThreshold;
            if (leftExcess > 0.0f || rightExcess > 0.0f) {
                if (!selectedChannelLocked) {
                    selectedLeftChannel = leftRMS - getVoiceThreshold(true)
                        >= rightRMS - getVoiceThreshold(false);
                    selectedChannelLocked = true;
                    useLeftMicrophone = selectedLeftChannel;
                }
                const int16_t* selectedChunk = selectedLeftChannel ? leftChunk : rightChunk;
                appendVoiceWindow(selectedChunk, capturedVoiceWindows, capturedVoiceSamples);
                capturedVoiceWindows++;
                consecutiveVoiceChunks++;
                voiceDetected = consecutiveVoiceChunks >= VAD_MIN_VOICE_CHUNKS
                    && consecutiveVoiceChunks >= VAD_REQUIRED_CHUNKS;
            } else {
                consecutiveVoiceChunks = 0;
                capturedVoiceWindows = 0;
                capturedVoiceSamples = 0;
                selectedChannelLocked = false;
            }
            delay(5);
        }

        if (!voiceDetected) {
            i2s_zero_dma_buffer(I2S_NUM_0);
            stereoVoiceWindowNeedsReset = true;
            clearLedRing();
            logAviso(F("No se detecto voz; enrolamiento cancelado. Repite enviando e."));
            currentState = STATE_LISTENING;
            return;
        }

        finishVoiceCapture(capturedVoiceSamples);
        i2s_zero_dma_buffer(I2S_NUM_0);
        stereoVoiceWindowNeedsReset = true;
        clearLedRing();

        // Extraer características MFCC
        mfccExtractor.extract(audioRawBuffer, enrolledMFCC[sample]);
        enrolledCount++;

        char progressMessage[32];
        snprintf(progressMessage, sizeof(progressMessage), "Muestra %d de %d capturada.",
                 sample + 1, NUM_ENROLL_TEMPLATES);
        logInfo(progressMessage);
        for (uint8_t frame = 0; frame < 8; frame++) {
            showWakeWordAnimation(frame);
            delay(35);
        }
        delay(600);
    }

    isEnrolled = true;
    saveTemplatesToNVS();

    logInfo(F("¡Enrolamiento completado con exito!"));
    logLinea('*', 38);
    for (uint8_t frame = 0; frame < 12; frame++) {
        showWakeWordAnimation(frame);
        delay(35);
    }
    clearLedRing();
    currentState = STATE_LISTENING;
}

// ==============================================================================
// INFERENCIA CONTINUA CON DTW (Speaker Verification)
// ==============================================================================
void checkWakeWordInference() {
    // 1. Leer bloque corto para Voice Activity Detection
    int16_t leftChunk[512];
    int16_t rightChunk[512];
    uint8_t consecutiveVoiceChunks = 0;
    uint16_t capturedVoiceWindows = 0;
    uint16_t capturedVoiceSamples = 0;
    bool selectedChannelLocked = false;

    for (uint8_t chunkIndex = 0; chunkIndex < VAD_MIN_VOICE_CHUNKS; chunkIndex++) {
        readStereoVoiceWindow(leftChunk, rightChunk);
        float leftRMS = MFCCExtractor::calculateRMS(leftChunk, FFT_SIZE);
        float rightRMS = MFCCExtractor::calculateRMS(rightChunk, FFT_SIZE);
        const bool sustaining = consecutiveVoiceChunks >= VAD_REQUIRED_CHUNKS;
        float leftThreshold = sustaining ? getVoiceReleaseThreshold(true) : getVoiceThreshold(true);
        float rightThreshold = sustaining ? getVoiceReleaseThreshold(false) : getVoiceThreshold(false);
        float leftExcess = leftRMS - leftThreshold;
        float rightExcess = rightRMS - rightThreshold;
        if (leftExcess > 0.0f || rightExcess > 0.0f) {
            if (!selectedChannelLocked) {
                useLeftMicrophone = leftRMS - getVoiceThreshold(true)
                    >= rightRMS - getVoiceThreshold(false);
                selectedChannelLocked = true;
            }
            const int16_t* selectedChunk = useLeftMicrophone ? leftChunk : rightChunk;
            appendVoiceWindow(selectedChunk, capturedVoiceWindows, capturedVoiceSamples);
            capturedVoiceWindows++;
            consecutiveVoiceChunks++;
        }
        if (leftExcess <= 0.0f && rightExcess <= 0.0f) {
            consecutiveVoiceChunks = 0;
            capturedVoiceWindows = 0;
            capturedVoiceSamples = 0;
            selectedChannelLocked = false;
            break;
        }
    }

    // Varios bloques consecutivos reducen disparos por golpes o ruido aislado.
    if (consecutiveVoiceChunks >= VAD_MIN_VOICE_CHUNKS
        && consecutiveVoiceChunks >= INFERENCE_REQUIRED_CHUNKS) {
        finishVoiceCapture(capturedVoiceSamples);

        // Extraer huella MFCC de la voz capturada
        mfccExtractor.extract(audioRawBuffer, liveMFCC);

        // Calcular distancia DTW contra las plantillas enroladas
        float bestDistance = DTWMatcher::matchAgainstTemplates(liveMFCC, enrolledMFCC, enrolledCount);

        i2s_zero_dma_buffer(I2S_NUM_0);
        stereoVoiceWindowNeedsReset = true;

        // Evaluación del umbral (distancia pequeña = suena parecido a tu voz enrolada)
        Serial.printf("Distancia DTW = %.1f (se acepta si es menor o igual a %.1f)\n", bestDistance, (float)DTW_MAX_DISTANCE);
        if (bestDistance <= DTW_MAX_DISTANCE) {
            // ¡WAKE WORD DETECTADA CON ÉXITO!
            logLinea('-', 38);
            logInfo(F(">>> WAKE WORD DETECTADA (Acceso Concedido) <<<"));
            logLinea('-', 38);

            for (uint8_t frame = 0; frame < 18; frame++) {
                showWakeWordAnimation(frame);
                delay(35);
            }
            triggerTimestamp = millis();
            currentState = STATE_TRIGGERED;
        } else {
            // PRUEBA DEL IMPOSTOR: Palabra no coincide o voz de otra persona
            logAviso(F("Rechazado: Huella acustica no coincide con usuario"));
        }
    }
}

// ==============================================================================
// ALMACENAMIENTO EN MEMORIA NO VOLÁTIL (NVS / FLASH)
// ==============================================================================
void saveTemplatesToNVS() {
    preferences.begin(NVS_NAMESPACE, false);
    preferences.putBytes("templates", enrolledMFCC, sizeof(enrolledMFCC));
    preferences.putInt("count", enrolledCount);
    preferences.putBool("enrolled", true);
    preferences.end();
    logInfo(F("Plantillas MFCC guardadas en Flash NVS."));
}

bool saveCalibrationToNVS() {
    if (!preferences.begin(NVS_NAMESPACE, false)) return false;
    bool saved = preferences.putBool("calibrated", false) == sizeof(uint8_t);
    saved = preferences.putFloat("noise_left_rms", ambientNoiseRMSLeft) == sizeof(float) && saved;
    saved = preferences.putFloat("noise_right_rms", ambientNoiseRMSRight) == sizeof(float) && saved;
    saved = preferences.putFloat("noise_left_std", ambientNoiseStdDevLeft) == sizeof(float) && saved;
    saved = preferences.putFloat("noise_right_std", ambientNoiseStdDevRight) == sizeof(float) && saved;
    if (saved) saved = preferences.putBool("calibrated", true) == sizeof(uint8_t);
    preferences.end();
    return saved;
}

bool loadCalibrationFromNVS() {
    if (!preferences.begin(NVS_NAMESPACE, true)) return false;
    bool hasCalibration = preferences.getBool("calibrated", false)
        && preferences.isKey("noise_left_rms")
        && preferences.isKey("noise_right_rms")
        && preferences.isKey("noise_left_std")
        && preferences.isKey("noise_right_std");
    if (hasCalibration) {
        float leftRMS = preferences.getFloat("noise_left_rms", -1.0f);
        float rightRMS = preferences.getFloat("noise_right_rms", -1.0f);
        float leftStdDev = preferences.getFloat("noise_left_std", -1.0f);
        float rightStdDev = preferences.getFloat("noise_right_std", -1.0f);
        hasCalibration = isfinite(leftRMS) && leftRMS >= 0.0f
            && isfinite(rightRMS) && rightRMS >= 0.0f
            && isfinite(leftStdDev) && leftStdDev >= 0.0f
            && isfinite(rightStdDev) && rightStdDev >= 0.0f;
        if (hasCalibration) {
            ambientNoiseRMSLeft = leftRMS;
            ambientNoiseRMSRight = rightRMS;
            ambientNoiseStdDevLeft = leftStdDev;
            ambientNoiseStdDevRight = rightStdDev;
        }
    }
    preferences.end();
    return hasCalibration;
}

void loadTemplatesFromNVS() {
    preferences.begin(NVS_NAMESPACE, true);
    isEnrolled = preferences.getBool("enrolled", false);
    if (isEnrolled) {
        enrolledCount = preferences.getInt("count", 0);
        preferences.getBytes("templates", enrolledMFCC, sizeof(enrolledMFCC));
        logInfo(F("Plantillas MFCC cargadas exitosamente desde Flash NVS."));
    }
    preferences.end();
}
