#pragma once
#include <Arduino.h>

/**
 * ==============================================================================
 * MENSAJES POR EL MONITOR SERIAL
 *
 * Tres funciones muy simples para escribir en el monitor serial con una etiqueta:
 *   logInfo("...")   ->  [INFO]  información normal
 *   logAviso("...")  ->  [AVISO] algo que debes saber o hacer
 *   logError("...")  ->  [ERROR] algo falló
 *   logLinea('=')    ->  dibuja una línea para separar secciones
 * ==============================================================================
 */

inline void logInfo(const char* mensaje)  { Serial.print(F("[INFO] "));  Serial.println(mensaje); }
inline void logAviso(const char* mensaje) { Serial.print(F("[AVISO] ")); Serial.println(mensaje); }
inline void logError(const char* mensaje) { Serial.print(F("[ERROR] ")); Serial.println(mensaje); }

inline void logInfo(const __FlashStringHelper* mensaje)  { Serial.print(F("[INFO] "));  Serial.println(mensaje); }
inline void logAviso(const __FlashStringHelper* mensaje) { Serial.print(F("[AVISO] ")); Serial.println(mensaje); }
inline void logError(const __FlashStringHelper* mensaje) { Serial.print(F("[ERROR] ")); Serial.println(mensaje); }

inline void logLinea(char caracter = '-', uint8_t largo = 38) {
    for (uint8_t i = 0; i < largo; i++) Serial.print(caracter);
    Serial.println();
}
