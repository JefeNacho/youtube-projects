#pragma once
#include <stdio.h>

/**
 * Mensajes sencillos por el monitor serial:
 *   log_info("...")   -> [INFO]   información normal
 *   log_aviso("...")  -> [AVISO]  algo que debes saber
 *   log_error("...")  -> [ERROR]  algo falló
 *   log_linea('=')    -> dibuja una línea para separar secciones
 */
static inline void log_info(const char *mensaje)  { printf("[INFO] %s" "\n", mensaje);  fflush(stdout); }
static inline void log_aviso(const char *mensaje) { printf("[AVISO] %s" "\n", mensaje); fflush(stdout); }
static inline void log_error(const char *mensaje) { printf("[ERROR] %s" "\n", mensaje); fflush(stdout); }

static inline void log_linea(char caracter) {
    for (int i = 0; i < 40; i++) putchar(caracter);
    putchar('\n');
    fflush(stdout);
}
