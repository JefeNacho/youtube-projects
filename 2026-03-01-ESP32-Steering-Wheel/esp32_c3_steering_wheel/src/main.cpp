#include <Arduino.h>

#define S3_TX 21
#define S3_RX 20

// --- PINES DE SENSORES CORREGIDOS ---
const int PIN_GAS   = 0; // Se cambia de 1 a 0
const int PIN_BRAKE = 1; // Se cambia de 0 a 1
const int PIN_STEER = 3;
const int PIN_HORN  = 4;

SemaphoreHandle_t serialMutex;

void setup() {
  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
  Serial.setTimeout(0);
  
  Serial1.setRxBufferSize(4096);
  Serial1.begin(115200, SERIAL_8N1, S3_RX, S3_TX);
  Serial1.setTimeout(0);

  pinMode(PIN_HORN, INPUT_PULLDOWN);
  pinMode(PIN_GAS, INPUT_PULLUP);
  pinMode(PIN_BRAKE, INPUT_PULLUP);
  pinMode(PIN_STEER, INPUT);

  serialMutex = xSemaphoreCreateMutex();
}

void loop() {
  static uint8_t buf[512];
  
  // 1. PC -> S3 (Telemetría)
  int nPC = Serial.available();
  if (nPC > 0) {
    int read = Serial.readBytes(buf, min(nPC, (int)sizeof(buf)));
    Serial1.write(buf, read);
  }
  
  // 2. S3 -> PC (Comandos JSON)
  int nS3 = Serial1.available();
  if (nS3 > 0) {
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(5))) {
      int read = Serial1.readBytes(buf, min(nS3, (int)sizeof(buf)));
      Serial.write(buf, read);
      xSemaphoreGive(serialMutex);
    }
  }

  // 3. LECTURA DE SENSORES (50Hz)
  static unsigned long lastSensors = 0;
  if (millis() - lastSensors > 20) {
    int s = analogRead(PIN_STEER);
    int g = digitalRead(PIN_GAS);
    int b = digitalRead(PIN_BRAKE);
    int h = digitalRead(PIN_HORN);

    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(5))) {
      // Enviamos en orden: Steer, Gas, Brake, Horn
      Serial.printf("RAW:%d,%d,%d,%d\n", s, g, b, h);
      xSemaphoreGive(serialMutex);
    }
    lastSensors = millis();
  }
}
