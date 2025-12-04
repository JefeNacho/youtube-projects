/*
  ESP32-CAM Sender - Envía imágenes capturadas por la cámara al servidor Flask para detección de rostros.
  Proyecto basado en ESP32-CAM AI-Thinker.
  Autor: Ignacio Aguilera
  Fecha: Diciembre 2025
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>  // Librería para manejar JSON

// Configuración WiFi
const char* ssid = "TU_WIFI_SSID";           // Cambiar por tu SSID
const char* password = "TU_WIFI_PASSWORD";    // Cambiar por tu password

// Configuración del servidor Flask
const char* serverUrl = "http://<IP_DE_TU_PC>:5000/upload";  // IP de tu PC en la red local
// Configuración de pines para AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Variables globales
unsigned long lastCaptureTime = 0;
const int captureInterval = 100;  // Capturar cada 100ms (10 FPS)

// Función auxiliar: procesa el texto JSON y muestra datos útiles.
void parseJsonPayload(const String &payload) {
  if (payload.length() == 0) {
    Serial.println(" (Aviso) Respuesta vacía del servidor");
    return;
  }

  Serial.print(" JSON recibido: ");
  Serial.println(payload);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print(" Error al leer JSON: ");
    Serial.println(err.c_str());
    Serial.println(" Sugerencia: aumenta JSON_BUFFER_SIZE si es 'NoMemory'.");
    return;
  }

  // Estado del procesamiento
  const char* status = doc["status"];
  Serial.print(" Estado: "); Serial.println(status);

  // Número de rostros
  int facesDetected = doc["faces_detected"];
  Serial.print(" Rostros detectados: "); Serial.println(facesDetected);

  // Lista de posiciones
  JsonArray positions = doc["positions"].as<JsonArray>();

  int idx = 0;
  for (JsonObject pos : positions) {
    int x = pos["x"] | 0;
    int y = pos["y"] | 0;
    int w = pos["w"] | 0;
    int h = pos["h"] | 0;
    Serial.printf("  Cara %d -> x:%d y:%d w:%d h:%d\n", idx, x, y, w, h);
    idx++;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n ESP32-CAM iniciando...");
  
  // Configurar la cámara
  if (!initCamera()) {
    Serial.println(" Error al inicializar la cámara");
    return;
  }
  
  // Conectar a WiFi
  connectWiFi();
  
  Serial.println(" Sistema listo");
  Serial.print(" Enviando imágenes a: ");
  Serial.println(serverUrl);
}

void loop() {
  // Verificar conexión WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Reconectando...");
    connectWiFi();
    return;
  }
  
  // Capturar y enviar frame según el intervalo
  if (millis() - lastCaptureTime >= captureInterval) {
    captureAndSend();
    lastCaptureTime = millis();
  }
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;  // Formato JPEG
  
  // Configurar resolución 320x240 (QVGA) - óptimo para ESP32-CAM
  // Esta resolución es ideal para streaming y procesamiento en tiempo real
  config.frame_size = FRAMESIZE_QVGA;  // 320x240
  config.jpeg_quality = 10;             // 0-63 (menor = mejor calidad)
  
  // Ajustar buffer count según memoria disponible
  if(psramFound()) {
    config.fb_count = 2;  // Con PSRAM: usar doble buffer
  } else {
    config.fb_count = 1;  // Sin PSRAM: buffer único
  }
  
  // Inicializar la cámara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error 0x%x al inicializar cámara\n", err);
    return false;
  }
  
  Serial.println("Cámara inicializada correctamente");
  return true;
}

void connectWiFi() {
  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n WiFi conectado");
    Serial.print(" IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n Error al conectar WiFi");
  }
}

void captureAndSend() {
  // Capturar imagen
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println(" Error al capturar imagen");
    return;
  }
  
  // Enviar imagen al servidor
  HTTPClient http;
  http.begin(serverUrl);
  http.setTimeout(5000);  // Timeout de 5 segundos
  
  int httpResponseCode = http.POST(fb->buf, fb->len);
  
  if (httpResponseCode > 0) {
    Serial.printf(" Frame enviado - Código: %d - Tamaño: %d bytes\n", 
                  httpResponseCode, fb->len);
    // Procesar respuesta JSON en función separada para claridad
    parseJsonPayload(http.getString());
  } else {
    Serial.printf(" Error al enviar - Código: %d - Descripción: %s\n", 
                  httpResponseCode, http.errorToString(httpResponseCode).c_str());
  }
  
  http.end();
  
  // Liberar el buffer de la cámara
  esp_camera_fb_return(fb);
}

