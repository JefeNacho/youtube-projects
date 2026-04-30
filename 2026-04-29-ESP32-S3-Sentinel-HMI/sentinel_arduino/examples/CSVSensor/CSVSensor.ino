/**
 * CSVSensor — Sentinel Arduino Library
 * Autor: Ignacio Aguilera
 *
 * Registra lecturas de sensor en CSV.
 *
 *   sentinel.schema("datos.csv", cols, n)  →  define columnas (una vez)
 *   sentinel.write("datos.csv",  row,  n)  →  escribe fila
 *   sentinel.close("datos.csv")            →  cierra archivo
 *
 * Leer resultado con Python:
 *   df = pd.read_csv("sentinel_logs/csv/datos.csv")
 */
#include <Sentinel.h>

SentinelLogger sentinel(Serial);

static const uint32_t MAX_SAMPLES = 100;
static uint32_t       samples     = 0;
static bool           done        = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    sentinel.separator('=', 30);
    sentinel.info(F("CSVSensor — Iniciando captura"));

    const char* cols[] = {"temp_c", "hum_pct", "presion_hpa", "vbat_v"};
    sentinel.schema("datos.csv", cols, 4);

    sentinel.info(F("100 muestras a 1 Hz..."));
    sentinel.separator();
}

void loop() {
    if (done) return;

    static uint32_t last = 0;
    if (millis() - last < 1000) return;
    last = millis();

    float row[] = {readTemp(), readHumidity(), readPressure(), readVBat()};
    sentinel.write("datos.csv", row, 4);

    char msg[60];
    snprintf(msg, sizeof(msg), "T=%.1fC  H=%.0f%%  P=%.1fhPa  Vbat=%.2fV",
             row[0], row[1], row[2], row[3]);
    sentinel.print(msg);

    sentinel.metric("Captura", ++samples, MAX_SAMPLES);

    if (samples >= MAX_SAMPLES) {
        sentinel.close("datos.csv");
        sentinel.separator('=', 30);
        sentinel.info(F("Captura completa. Archivo cerrado."));
        done = true;
    }
}

float readTemp()     { return 20.0f + random(-30, 80)   * 0.1f; }
float readHumidity() { return 50.0f + random(-100, 100) * 0.1f; }
float readPressure() { return 1013.0f + random(-50, 50) * 0.1f; }
float readVBat()     { return 3.7f + random(-5, 5)      * 0.01f; }
