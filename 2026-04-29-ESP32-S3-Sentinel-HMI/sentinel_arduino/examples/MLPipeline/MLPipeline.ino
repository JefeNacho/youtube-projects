/**
 * MLPipeline — Sentinel Arduino Library
 * Autor: Ignacio Aguilera
 *
 * Pipeline de inferencia con logging estructurado.
 *
 *   sentinel.metric("conf", 0.91f)               →  valor en pantalla + metrics.jsonl
 *   sentinel.write("run.jsonl", "k=v,k2=v2")     →  registro JSONL por frame
 *   sentinel.metric("Evaluando", frame, N_FRAMES) →  barra de progreso
 *   sentinel.close("run.jsonl")                   →  cerrar archivo
 *
 * Leer resultado con Python:
 *   df = pd.read_json("sentinel_logs/json/run.jsonl", lines=True)
 */
#include <Sentinel.h>

SentinelLogger sentinel(Serial);

static const uint32_t N_FRAMES   = 200;
static uint32_t       frame      = 0;
static uint32_t       detecciones = 0;
static bool           done       = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    sentinel.separator('=', 34);
    sentinel.info(F("MLPipeline — Evaluacion de modelo"));
    sentinel.separator();
}

void loop() {
    if (done) { delay(5000); return; }

    static uint32_t last = 0;
    if (millis() - last < 150) return;
    last = millis();

    float    conf   = 0.65f + random(0, 35) * 0.01f;
    uint32_t lat_ms = 8 + random(0, 12);
    int      label  = random(0, 5);
    bool     detect = conf >= 0.85f;
    if (detect) detecciones++;

    // Metricas en tiempo real
    sentinel.metric("conf",   conf,           "");
    sentinel.metric("lat_ms", (int32_t)lat_ms, "ms");

    // Registro estructurado JSONL
    char kv[80];
    snprintf(kv, sizeof(kv),
             "frame=%lu,label=%d,conf=%.3f,lat_ms=%lu,detected=%s",
             (unsigned long)++frame, label, conf,
             (unsigned long)lat_ms, detect ? "true" : "false");
    sentinel.write("run.jsonl", kv);

    // Barra de progreso
    sentinel.metric("Evaluando", frame, N_FRAMES);

    if (frame >= N_FRAMES) {
        sentinel.close("run.jsonl");

        float acc = 100.0f * detecciones / N_FRAMES;
        sentinel.separator('=', 34);
        sentinel.info(F("Evaluacion completa"));

        char res[56];
        snprintf(res, sizeof(res), "Accuracy: %.1f%%  (%lu/%lu)",
                 acc, (unsigned long)detecciones, (unsigned long)N_FRAMES);
        sentinel.print(res);

        done = true;
    }
}
