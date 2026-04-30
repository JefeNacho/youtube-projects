/**
 * Demo — Sentinel Arduino Library
 * Autor: Ignacio Aguilera
 *
 * Ejemplo completo: logging, CSV, JSONL, métricas y barra de progreso.
 *
 * Requisitos:
 *   1. Librería Sentinel instalada (Sketch > Include Library > Add .ZIP Library)
 *   2. sentinel_daemon.py corriendo en la PC (o SidecarDaemon.exe)
 *   3. Pantalla CrowPanel conectada
 *   4. Conectar esta placa al COM elegido en la pantalla
 *
 * Leer resultados en Python después:
 *   import pandas as pd
 *   df_csv   = pd.read_csv("sentinel_logs/csv/sensores.csv")
 *   df_jsonl = pd.read_json("sentinel_logs/json/experimento.jsonl", lines=True)
 */

#include <Sentinel.h>

// ── Instancia del logger ───────────────────────────────────────────────────
// Pasa cualquier Stream: Serial, Serial1, Serial2, SoftwareSerial, WiFiClient...
SentinelLogger sentinel(Serial);

// ── Estado global del demo ─────────────────────────────────────────────────
static uint32_t frame      = 0;
static uint32_t csvSamples = 0;
static bool     csvDone    = false;
static bool     jsonDone   = false;

// Archivos de datos (creados en setup, cerrados cuando se completan)
static SentinelFile* csvFile  = nullptr;
static SentinelFile* jsonFile = nullptr;

// ── Columnas del CSV ───────────────────────────────────────────────────────
static const char* CSV_COLS[] = {"temp_c", "hum_pct", "presion_hpa", "vbat_v"};
static const uint8_t N_CSV    = 100;   // filas a capturar
static const uint8_t N_JSON   = 50;    // frames JSONL a grabar


// ── Funciones de lectura de sensor (simuladas) ─────────────────────────────
float readTemp()     { return 22.0f + random(-30,  80) * 0.1f; }
float readHumidity() { return 55.0f + random(-80,  80) * 0.1f; }
float readPressure() { return 1013.0f + random(-30, 30) * 0.1f; }
float readVBat()     { return 3.70f + random(-10,  10) * 0.01f; }
float readConfidence() { return 0.60f + random(0,  40) * 0.01f; }


void setup() {
    Serial.begin(115200);
    delay(1200);   // Espera a que el daemon abra el puerto

    // ── 1. Logging básico ──────────────────────────────────────────────────
    sentinel.separator('=', 32);
    sentinel.info(F("Sentinel Demo — Arduino"));
    sentinel.separator();

    sentinel.print(F("Mensaje generico (cyan)"));
    sentinel.info(F("Mensaje informativo (azul)"));
    sentinel.warn(F("Advertencia (ambar) — voltaje bajo"));
    sentinel.error(F("Error (rojo) — sensor sin respuesta"));
    sentinel.separator();

    // ── 2. Abrir archivo CSV ───────────────────────────────────────────────
    // Define columnas y abre el archivo en una sola llamada.
    // El daemon crea sentinel_logs/csv/sensores.csv al recibir el schema.
    static SentinelFile csv = sentinel.open("sensores.csv", CSV_COLS, 4);
    csvFile = &csv;

    // ── 3. Abrir archivo JSONL ─────────────────────────────────────────────
    // Sin schema — cada write() envía pares clave=valor.
    // El daemon los convierte a JSON Lines con timestamp automático.
    static SentinelFile json = sentinel.open("experimento.jsonl");
    jsonFile = &json;

    sentinel.info(F("Captura iniciada (100 CSV + 50 JSONL)..."));
    sentinel.separator();
}


void loop() {
    static uint32_t last = 0;
    if (millis() - last < 200) return;
    last = millis();

    frame++;

    // ── A. Métricas en tiempo real ─────────────────────────────────────────
    float temp = readTemp();
    float vbat = readVBat();
    sentinel.metric("temp",  temp, "C");
    sentinel.metric("vbat",  vbat, "V");

    // Advertencia automática si temperatura alta
    if (temp > 35.0f) sentinel.warn("Temperatura elevada!");

    // ── B. Escribir fila CSV ───────────────────────────────────────────────
    if (!csvDone && csvFile) {
        float row[] = {temp, readHumidity(), readPressure(), vbat};
        csvFile->write(row, 4);

        csvSamples++;
        sentinel.metric("CSV", csvSamples, N_CSV);   // barra de progreso

        if (csvSamples >= N_CSV) {
            csvFile->close();
            csvFile = nullptr;
            csvDone = true;
            sentinel.info(F("CSV cerrado: sentinel_logs/csv/sensores.csv"));
        }
    }

    // ── C. Escribir registro JSONL ─────────────────────────────────────────
    if (!jsonDone && jsonFile && frame <= N_JSON) {
        float    conf    = readConfidence();
        uint32_t lat_ms  = 10 + random(0, 20);
        bool     detect  = conf >= 0.80f;

        char kv[96];
        snprintf(kv, sizeof(kv),
                 "frame=%lu,conf=%.3f,lat_ms=%lu,temp=%.1f,detected=%s",
                 (unsigned long)frame, conf,
                 (unsigned long)lat_ms, temp,
                 detect ? "true" : "false");
        jsonFile->write(kv);

        sentinel.metric("JSON", frame, N_JSON);   // barra de progreso

        if (frame >= N_JSON) {
            jsonFile->close();
            jsonFile = nullptr;
            jsonDone = true;
            sentinel.info(F("JSONL cerrado: sentinel_logs/json/experimento.jsonl"));
        }
    }

    // ── D. Resumen final ───────────────────────────────────────────────────
    if (csvDone && jsonDone) {
        sentinel.separator('=', 32);
        sentinel.info(F("Demo completado."));
        sentinel.info(F("Leer en Python:"));
        sentinel.print(F("  pd.read_csv('sentinel_logs/csv/sensores.csv')"));
        sentinel.print(F("  pd.read_json('sentinel_logs/json/experimento.jsonl', lines=True)"));
        sentinel.separator('=', 32);

        // Detener — no seguir escribiendo
        while (true) delay(5000);
    }
}
