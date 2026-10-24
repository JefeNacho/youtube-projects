#pragma once
#include <Arduino.h>

/**
 * ==============================================================================
 * CONFIGURACIÓN DE PINES Y PARÁMETROS: MÉTODO 2 (EDGE IMPULSE TINYML)
 * ==============================================================================
 */

// ── Pines de Hardware ESP32-S3 ────────────────────────────────────────────────
#define PIN_I2S_SD          11    // Serial Data compartido por ambos INMP441
#define PIN_I2S_SCK         12    // Bit Clock (BCLK)
#define PIN_I2S_WS          13    // Word Select / LRCK
#define PIN_LED_OUT         17    // DIN del aro de LEDs WS2812B (Indicador de Wake Word)
#define LED_RING_COUNT      16    // Cantidad de LEDs del aro

// ── Umbrales de Inferencia TinyML ─────────────────────────────────────────────
#define CONFIDENCE_THRESHOLD 0.80f // Umbral mínimo de probabilidad para disparo (0.0 - 1.0)
#define SUSTAINED_MIN_PROB   0.55f // Disparo por dos lecturas seguidas: mínimo de cada una...
#define SUSTAINED_MEAN_PROB  0.65f // ...y promedio de ambas
#define WAKEWORD_LABEL       "wake_word" // Etiqueta exacta del modelo Edge Impulse

// ── Audio ─────────────────────────────────────────────────────────────────────
// El INMP441 entrega 24 bits de audio dentro de una palabra de 32 bits. Con >> 16 nos
// quedamos con los 16 bits más importantes (la conversión exacta a 16 bits, sin desbordes).
// Es el mismo valor que usan el Método 1 y la herramienta recolector_audio.
#define MIC_SAMPLE_SHIFT     16
