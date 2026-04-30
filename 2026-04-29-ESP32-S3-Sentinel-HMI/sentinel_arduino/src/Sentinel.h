#pragma once
/**
 * Sentinel Arduino Library v1.0
 * Autor: Ignacio Aguilera
 *
 * ── Logging ──────────────────────────────────────────────────────────────
 *
 *   SentinelLogger sentinel(Serial);
 *
 *   sentinel.info("Sistema iniciado");
 *   sentinel.warn("Voltaje bajo: 3.1V");
 *   sentinel.error("Sensor sin respuesta");
 *   sentinel.metric("temp", 25.3f, "C");       // valor en pantalla
 *   sentinel.metric("Cargando", 45u, 100u);    // barra de progreso
 *
 * ── Archivos (CSV o JSONL) ────────────────────────────────────────────────
 *
 *   El formato se determina por la extension del nombre de archivo.
 *
 *   CSV:
 *     const char* cols[] = {"temp", "hum", "presion"};
 *     SentinelFile data = sentinel.open("datos.csv", cols, 3);
 *
 *     float row[] = {25.3f, 60.1f, 1013.0f};
 *     data.write(row, 3);
 *     data.close();
 *
 *   JSONL:
 *     SentinelFile run = sentinel.open("run.jsonl");
 *     run.write("epoch=1,loss=0.342,ok=true");
 *     run.close();
 *
 *   Leer en Python:
 *     pd.read_csv("sentinel_logs/csv/datos.csv")
 *     pd.read_json("sentinel_logs/json/run.jsonl", lines=True)
 *
 * ── Ajustar buffer (antes del include) ───────────────────────────────────
 *
 *   #define SENTINEL_BUF 256
 *   #include <Sentinel.h>
 */

#include <Arduino.h>

#ifndef SENTINEL_BUF
#  define SENTINEL_BUF 192
#endif
#ifndef SENTINEL_NAME_LEN
#  define SENTINEL_NAME_LEN 28
#endif

// Colores LVGL recolor — usados internamente para el terminal HMI
#define SL_COL_NORMAL  "00FFCC"
#define SL_COL_INFO    "5DADE2"
#define SL_COL_WARN    "FFAA00"
#define SL_COL_ERROR   "FF4444"
#define SL_COL_SEP     "555555"


// ===========================================================================
// SentinelFile — objeto de archivo retornado por sentinel.open()
// ===========================================================================
class SentinelFile {
public:
    /**
     * Escribe una fila de valores numericos (solo archivos .csv).
     *
     *   float row[] = {25.3f, 60.1f, 1013.0f};
     *   data.write(row, 3);
     *
     * @param decimals Precision decimal (default 2)
     */
    void write(float   values[], uint8_t count, uint8_t decimals = 2);
    void write(int     values[], uint8_t count);
    void write(int32_t values[], uint8_t count);

    /**
     * Escribe un registro de pares clave=valor (solo archivos .jsonl).
     * El daemon convierte a JSON Lines con timestamp automatico.
     *
     *   run.write("epoch=1,loss=0.342,ok=true");
     *
     * Tipos inferidos: true/false=bool, 1/42=int, 0.342=float, texto=string
     */
    void write(const char* kv);

    /**
     * Cierra el archivo. El daemon libera el file handle.
     * El archivo queda listo para leer desde Python.
     */
    void close();

private:
    Stream* _port;
    char    _base[SENTINEL_NAME_LEN];   // nombre sin extension
    bool    _isCSV;                     // true=CSV, false=JSONL

    // Solo SentinelLogger puede construir SentinelFile
    SentinelFile(Stream& port, const char* base, bool isCSV);
    friend class SentinelLogger;
};


// ===========================================================================
// SentinelLogger — clase principal
// ===========================================================================
class SentinelLogger {
public:
    /**
     * @param port     Cualquier Stream: Serial, Serial1, SoftwareSerial, WiFiClient...
     * @param terminal Indice de terminal HMI (0-3).
     */
    explicit SentinelLogger(Stream& port, uint8_t terminal = 0);

    // ── Routing ──────────────────────────────────────────────────────────

    /** Cambia el terminal activo. Retorna *this para encadenamiento. */
    SentinelLogger& terminal(uint8_t t);

    // ── Logging ──────────────────────────────────────────────────────────

    void print(const char* msg);
    void print(const __FlashStringHelper* msg);

    void info(const char* msg);
    void info(const __FlashStringHelper* msg);

    void warn(const char* msg);
    void warn(const __FlashStringHelper* msg);

    void error(const char* msg);
    void error(const __FlashStringHelper* msg);

    /** Linea separadora en gris. Caracter por defecto '-', longitud max 40. */
    void separator(char c = '-', uint8_t len = 30);

    // ── Archivos ──────────────────────────────────────────────────────────

    /**
     * Abre un archivo de datos. El formato se determina por la extension:
     *   .csv   → requiere headers (schema)
     *   .jsonl → sin schema, escribe pares clave=valor
     *
     * CSV — definir columnas al abrir:
     *   const char* cols[] = {"temp", "hum"};
     *   SentinelFile data = sentinel.open("datos.csv", cols, 2);
     *   float row[] = {25.3f, 60.1f};
     *   data.write(row, 2);
     *   data.close();
     *
     * JSONL — sin schema:
     *   SentinelFile run = sentinel.open("run.jsonl");
     *   run.write("epoch=1,loss=0.342,ok=true");
     *   run.close();
     *
     * @param filename  Nombre con extension: "datos.csv" o "run.jsonl"
     * @param headers   Nombres de columnas (solo CSV)
     * @param count     Numero de columnas (solo CSV)
     */
    SentinelFile open(const char* filename,
                      const char* headers[] = nullptr, uint8_t count = 0);

    // ── Metricas y progreso ───────────────────────────────────────────────

    /**
     * Valor nombrado — aparece en pantalla y se guarda en metrics.jsonl:
     *   sentinel.metric("temp",    25.3f, "C");
     *   sentinel.metric("latency", 12,    "ms");
     *
     * Barra de progreso — mismo metodo, distinta firma:
     *   sentinel.metric("Calibrando", 45u, 100u);
     *   // → [=========>          ] 45% (45/100)
     */
    void metric(const char* name,  float    value,   const char* unit = "");
    void metric(const char* name,  int32_t  value,   const char* unit = "");
    void metric(const char* name,  int      value,   const char* unit = "");
    void metric(const char* label, uint32_t current, uint32_t total);

    // ── Avanzado ──────────────────────────────────────────────────────────

    /** Envia una linea cruda al puerto sin formato. */
    void sendRaw(const char* line);

private:
    Stream&  _port;
    uint8_t  _terminal;

    void _prefix();
    void _colored(const char* color, const char* msg);
    void _coloredF(const char* color, const __FlashStringHelper* msg);

    static const char* _splitExt(const char* filename,
                                  char* dst, uint8_t dstLen);
};
