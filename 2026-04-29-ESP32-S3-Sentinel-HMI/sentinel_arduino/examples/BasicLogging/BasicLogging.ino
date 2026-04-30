/**
 * BasicLogging — Sentinel Arduino Library
 * Autor: Ignacio Aguilera
 *
 * Logging basico y barra de progreso.
 */
#include <Sentinel.h>

SentinelLogger sentinel(Serial);

void setup() {
    Serial.begin(115200);
    delay(1000);

    sentinel.separator('=', 32);
    sentinel.info(F("Sistema iniciado"));
    sentinel.warn(F("Voltaje de bateria: 3.1V (bajo)"));
    sentinel.error(F("Sensor DHT22 sin respuesta"));
    sentinel.separator();
    sentinel.info(F("Iniciando loop..."));
}

void loop() {
    static uint32_t last  = 0;
    static uint32_t step  = 0;
    const  uint32_t TOTAL = 60;

    if (millis() - last < 1000) return;
    last = millis();
    step = (step % TOTAL) + 1;

    float temp = 20.0f + random(-50, 80) * 0.1f;
    float vbat = 3.6f  + random(-10, 10) * 0.01f;

    char msg[52];
    snprintf(msg, sizeof(msg), "Temp: %.1f C  Vbat: %.2f V", temp, vbat);
    sentinel.print(msg);

    if (temp > 35.0f) sentinel.warn("Temperatura elevada!");

    // Barra de progreso — mismo metodo que metric()
    sentinel.metric("Ciclo", step, TOTAL);
}
