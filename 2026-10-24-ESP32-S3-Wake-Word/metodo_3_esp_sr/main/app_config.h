#pragma once

/**
 * ==============================================================================
 * PINES Y PARÁMETROS: MÉTODO 3 (ESPRESSIF ESP-SR)
 * ==============================================================================
 */

#define PIN_I2S_SD          11    // Datos de los micrófonos (SD)
#define PIN_I2S_SCK         12    // Reloj de bits (SCK / BCLK)
#define PIN_I2S_WS          13    // Selección de canal (WS / LRCK)
#define PIN_LED_OUT         17    // Datos del aro de LEDs WS2812 (DIN)
#define LED_RING_COUNT      16    // Cantidad de LEDs del aro

#define AUDIO_SAMPLE_RATE   16000 // 16 kHz

#define AFE_TASK_CORE       1     // Núcleo 1: procesa la voz

// El INMP441 entrega 24 bits de audio dentro de una palabra de 32 bits. Con >> 14 se
// conserva más volumen que con >> 16 (que usan los Métodos 1 y 2). Con este valor se
// probó el Método 3 y detecta bien; si tu voz se oye "recortada" prueba con 16.
#define MIC_SAMPLE_SHIFT    14
