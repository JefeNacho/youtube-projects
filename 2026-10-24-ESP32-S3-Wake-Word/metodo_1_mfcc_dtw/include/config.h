#pragma once
#include <Arduino.h>

/**
 * ==============================================================================
 * CONFIGURACIÓN DE PINES Y PARÁMETROS: MÉTODO 1 (MFCC + DTW)
 * ==============================================================================
 */

// ── Pines de Hardware ESP32-S3 ────────────────────────────────────────────────
#define PIN_I2S_SD          11    // Serial Data (Data Out del micrófono)
#define PIN_I2S_SCK         12    // Bit Clock (BCLK)
#define PIN_I2S_WS          13    // Word Select / LRCK
#define PIN_LED_OUT         17    // LED Indicador de Wake Word
#define LED_RING_COUNT      16    // Numero de LEDs WS2812B del aro

// ── Parámetros de Audio I2S ───────────────────────────────────────────────────
#define AUDIO_SAMPLE_RATE   16000 // 16 kHz (Estándar para reconocimiento de voz)
#define AUDIO_DURATION_MS   1500  // Duración de la ventana de captura (1,5 segundos)
#define TOTAL_SAMPLES       (AUDIO_SAMPLE_RATE * AUDIO_DURATION_MS / 1000) // 16000 muestras

// ── Parámetros DSP y MFCC ─────────────────────────────────────────────────────
#define FFT_SIZE            512   // Tamaño de ventana (32 ms a 16kHz)
#define FRAME_STEP          256   // Desplazamiento (16 ms, 50% de solapamiento)
#define NUM_FRAMES          (((TOTAL_SAMPLES - FFT_SIZE) / FRAME_STEP) + 1) // 61 frames
#define NUM_MEL_FILTERS     26    // Filtros triangulares en escala Mel (300Hz - 8000Hz)
#define NUM_MFCC_COEFFS     13    // Coeficientes cepstrales por frame (13 características)

// ── Calibración de Algoritmos (VAD y DTW) ─────────────────────────────────────
// I2S se convierte a PCM signed de 16 bits conservando los 16 bits superiores
// de cada palabra de 32 bits del INMP441.
#define VAD_ENERGY_THRESHOLD 162.5f  // Margen minimo adicional en PCM de 16 bits
#define VAD_NOISE_MARGIN      0.03f  // Margen adicional proporcional al ruido ambiente
#define VAD_STD_MULTIPLIER    1.25f  // Margen frente a variaciones del ruido
#define NOISE_CALIBRATION_MS  10000  // Duracion de la calibracion automatica
#define MFCC_TARGET_RMS       8000.0f // Normaliza volumen antes de extraer MFCC
#define VAD_REQUIRED_CHUNKS   4      // Ventanas solapadas consecutivas para confirmar voz
#define VAD_MIN_VOICE_DURATION_MS 300 // Duracion minima continua de voz antes de enrolar o calcular DTW
#define ENROLL_WAIT_TIMEOUT_MS 10000 // Tiempo máximo esperando cada muestra
#define INFERENCE_REQUIRED_CHUNKS 4   // Persistencia minima previa al limite de 300ms
#define DTW_MAX_DISTANCE     180.0f // Umbral de similitud MFCC-DTW
#define DTW_SAKOE_CHIBA_BAND 8      // Radio de restricción temporal (Sakoe-Chiba)

// ── Enrolamiento ──────────────────────────────────────────────────────────────
#define NUM_ENROLL_TEMPLATES 3      // Número de repeticiones para promediar la huella vocal
#define NVS_NAMESPACE        "wakeword_dtw_v2"
