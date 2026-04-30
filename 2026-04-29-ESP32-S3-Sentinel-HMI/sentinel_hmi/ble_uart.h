// ble_uart.h — Nordic UART Service (NUS) para Sentinel HMI
// Autor: Ignacio Aguilera
//
// Implementa el canal BLE alternativo al USB CDC usando NimBLE-Arduino,
// que consume ~45KB de DRAM frente a los ~150KB del stack bluedroid por
// defecto. La diferencia de ~100KB es crítica en ESP32-S3 donde DRAM es
// compartida entre BLE, WiFi (~40KB) y las tareas FreeRTOS.
//
// Requisito: instalar "NimBLE-Arduino" by h2zero desde el Library Manager.
//
// Paquetes soportados (mismos magic bytes que SerialBridgeTask):
//   0xA5 AtmosPacket  — temperatura / humedad / clima
//   0xB5 SysPacket    — CPU / GPU / RAM / status
//   0xC5 ComPacket    — lista de puertos COM disponibles
//   0xF5 VoiceState   — estado del pipeline de voz (2 bytes)
//   0xD5 AudioChunk   — IGNORADO por BLE (ancho de banda insuficiente)

#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/semphr.h>

// ─── UUIDs Nordic UART Service ────────────────────────────────────────────────
#define NUS_SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_RX_UUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // PC→ESP32 WRITE
#define NUS_CHAR_TX_UUID        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // ESP32→PC NOTIFY

// ─── Tamaño del buffer RX circular ────────────────────────────────────────────
#define BLE_RX_BUF_SIZE  4096

// ─── Magic bytes (espejo de sentinel_hmi.ino — mantener sincronizados) ────────
#define BLE_MAGIC_ATMOS       0xA5
#define BLE_MAGIC_SYS         0xB5
#define BLE_MAGIC_COM         0xC5
#define BLE_MAGIC_AUDIO       0xD5   // ignorado en BLE
#define BLE_MAGIC_VOICE_STATE 0xF5
#define BLE_AUDIO_CHUNK_BYTES 1024

// ─── Estructuras de paquete (packed, igual que en sentinel_hmi.ino) ───────────
struct __attribute__((packed)) BLE_AtmosPacket {
    uint8_t  magic;
    float    temp;
    float    hum;
    int      aqi;
    uint8_t  weather;
    uint8_t  checksum;
};

struct __attribute__((packed)) BLE_SysPacket {
    uint8_t  magic;
    uint8_t  cpu;
    uint8_t  gpu;
    uint16_t ram_mb;
    char     status[32];
    uint8_t  checksum;
};

struct __attribute__((packed)) BLE_ComPacket {
    uint8_t  magic;
    uint8_t  count;
    uint8_t  pad;
    char     ports[255];
    uint8_t  checksum;
};

// ─── Variables globales exportadas ────────────────────────────────────────────

// Estado de conexión — actualizado desde callbacks BLE, leído desde cualquier core.
volatile bool ble_connected = false;

// Estado del advertising BLE para la UI del Dashboard:
//   0 = Sin Conexión  (no está haciendo advertising)
//   1 = Visible...    (advertising activo, esperando que el PC conecte)
//   2 = Conectado     (conexión BLE activa)
volatile uint8_t ble_advertising_state = 0;

// ─── Staging para AtmosPacket → LVGL (Core 0 escribe, Core 1 aplica) ─────────
volatile bool ble_atmos_pending = false;
volatile int  ble_atmos_t_int   = 0;
volatile int  ble_atmos_t_dec   = 0;

// ─── Estado interno ────────────────────────────────────────────────────────────
static NimBLEServer*         _ble_server  = nullptr;
static NimBLECharacteristic* _ble_tx_char = nullptr;   // ESP32→PC notify
static NimBLECharacteristic* _ble_rx_char = nullptr;   // PC→ESP32 write

// Buffer circular RX en PSRAM para no consumir DRAM escasa.
static uint8_t*          _rx_buf  = nullptr;
static volatile int      _rx_head = 0;
static volatile int      _rx_tail = 0;
static SemaphoreHandle_t _rx_mutex = nullptr;

// ─── Callbacks BLE ─────────────────────────────────────────────────────────────

// NimBLE-Arduino v2.x cambió las firmas de los callbacks para incluir
// NimBLEConnInfo& (info de la conexión) y reason en onDisconnect.
class _BLEServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* srv, NimBLEConnInfo& connInfo) override {
        ble_connected = true;
        ble_advertising_state = 2;
        Serial.printf("[BLE] Cliente conectado  handle=0x%04X  interval=%d\n",
                      connInfo.getConnHandle(),
                      connInfo.getConnInterval());
    }

    void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& connInfo, int reason) override {
        ble_connected = false;
        // reason es el codigo HCI:
        //   0x08 = Supervision Timeout (radio interferido / WiFi coexistence)
        //   0x13 = Remote User Terminated (el daemon/PC desconecto a proposito)
        //   0x16 = Local Host Terminated
        //   0x22 = Connection parameter update rejected
        Serial.printf("[BLE] Desconectado  reason=0x%02X\n", reason);
        NimBLEDevice::startAdvertising();
        ble_advertising_state = 1;
    }
};

// Callbacks para el TX characteristic (ESP32→PC, el que tiene NOTIFY).
// El CCCD está aquí — onSubscribe se dispara cuando el Central escribe 0x0001.
class _BLETxCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& connInfo, uint16_t subValue) override {
        Serial.printf("[BLE] CCCD TX: 0x%04X (%s)\n",
                      subValue,
                      subValue == 1 ? "notify ON" : subValue == 0 ? "notify OFF" : "indicate");
    }
};
static _BLETxCallbacks _tx_cbs;

class _BLERxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
        const NimBLEAttValue& val = c->getValue();
        size_t len = val.size();
        if (len == 0 || _rx_mutex == nullptr || _rx_buf == nullptr) return;

        if (xSemaphoreTake(_rx_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

        const uint8_t* data = (const uint8_t*)val.data();
        for (size_t i = 0; i < len; i++) {
            int next = (_rx_head + 1) % BLE_RX_BUF_SIZE;
            if (next == _rx_tail) break;   // buffer lleno — descartar resto
            _rx_buf[_rx_head] = data[i];
            _rx_head = next;
        }

        xSemaphoreGive(_rx_mutex);
    }
};

static _BLEServerCallbacks _srv_cbs;
static _BLERxCallbacks     _rx_cbs;

// ─── API pública ───────────────────────────────────────────────────────────────

/**
 * ble_uart_init — Inicializar NimBLE stack, crear servicio NUS y configurar
 * advertising en standby. El advertising se activa desde ble_ui_toggle().
 *
 * Llamar desde setup() ANTES de crear BLETask.
 */
inline void ble_uart_init(const char* device_name) {
    Serial.printf("[BLE] DRAM libre antes de init: %d bytes\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    _rx_mutex = xSemaphoreCreateMutex();
    if (_rx_mutex == nullptr) {
        Serial.println("[BLE] ERROR: no se pudo crear mutex RX");
        return;
    }

    _rx_buf = (uint8_t*)heap_caps_malloc(BLE_RX_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (_rx_buf == nullptr) {
        Serial.println("[BLE] ERROR: no se pudo alojar _rx_buf en PSRAM");
        return;
    }

    NimBLEDevice::init(device_name);

    // Borrar bonds almacenados en NVS al arrancar.
    // Cuando el firmware se reflashea, el ESP32 pierde sus LTK pero Windows
    // los conserva. Al reconectar, Windows intenta cifrar con el LTK viejo,
    // el ESP32 no lo reconoce → falla de cifrado → Windows desconecta
    // (reason=0x213 = BLE_HS_ERR_HCI_BASE + HCI_ERR_REM_USER_TERM_CONN).
    // Borrar aquí garantiza que siempre se hace pairing fresco.
    NimBLEDevice::deleteAllBonds();

    // Habilitar "Just Works" pairing con bonding.
    // Windows inicia un SM_PAIRING_REQ al conectar un dispositivo BLE por
    // primera vez. Sin esta configuración, NimBLE responde "Pairing Not
    // Supported" y Windows desconecta (reason=0x13) antes de que el CCCD
    // write pueda completarse.
    // Just Works = sin PIN, sin confirmación manual, bonding habilitado.
    NimBLEDevice::setSecurityAuth(true, false, false);  // bonding, no MITM, no SC
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    _ble_server = NimBLEDevice::createServer();
    _ble_server->setCallbacks(&_srv_cbs);

    NimBLEService* svc = _ble_server->createService(NUS_SERVICE_UUID);

    // TX characteristic: ESP32 notifica al PC
    // NimBLE maneja el CCCD internamente. Se asignan callbacks para loguear
    // cuando el Central (PC) activa/desactiva notificaciones (onSubscribe).
    _ble_tx_char = svc->createCharacteristic(
        NUS_CHAR_TX_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    _ble_tx_char->setCallbacks(&_tx_cbs);

    // RX characteristic: PC escribe al ESP32
    _ble_rx_char = svc->createCharacteristic(
        NUS_CHAR_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _ble_rx_char->setCallbacks(&_rx_cbs);

    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    // Habilitar scan response para incluir el device name completo.
    // El paquete primario de advertising (max 31 bytes) se llena con el UUID
    // de 128 bits del NUS (18 bytes) + flags (3 bytes) = 21 bytes, dejando
    // solo 10 bytes libres — insuficiente para "Sentinel-HMI" (12 chars).
    // Con setScanResponseData, NimBLE envía el nombre completo en el SCAN_RSP
    // cuando el escáner lo solicita, y bleak lo recibe correctamente en Windows.
    NimBLEAdvertisementData scanResp;
    scanResp.setName(device_name);
    adv->setScanResponseData(scanResp);
    // NO iniciar advertising aquí — el usuario lo activa desde el Dashboard.

    Serial.printf("[BLE] DRAM libre despues de init: %d bytes\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    Serial.println("[BLE] NUS listo — advertising en standby. Pulsar boton en Dashboard para activar.");
}

/**
 * ble_uart_send — Enviar datos por TX (notify al PC). Fragmenta en 200 bytes.
 */
inline void ble_uart_send(const uint8_t* data, size_t len) {
    if (!ble_connected || _ble_tx_char == nullptr || data == nullptr || len == 0) return;

    const size_t FRAG = 200;
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = (len - offset > FRAG) ? FRAG : (len - offset);
        _ble_tx_char->setValue(data + offset, chunk);
        _ble_tx_char->notify();
        offset += chunk;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

inline void ble_uart_send(const char* str) {
    if (str) ble_uart_send((const uint8_t*)str, strlen(str));
}

inline int ble_uart_available() {
    if (_rx_mutex == nullptr) return 0;
    if (xSemaphoreTake(_rx_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return 0;
    int avail = (_rx_head - _rx_tail + BLE_RX_BUF_SIZE) % BLE_RX_BUF_SIZE;
    xSemaphoreGive(_rx_mutex);
    return avail;
}

inline int ble_uart_read(uint8_t* buf, size_t len) {
    if (_rx_mutex == nullptr || buf == nullptr || len == 0) return 0;
    if (xSemaphoreTake(_rx_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return 0;

    int read = 0;
    while (read < (int)len && _rx_tail != _rx_head) {
        buf[read++] = _rx_buf[_rx_tail];
        _rx_tail = (_rx_tail + 1) % BLE_RX_BUF_SIZE;
    }

    xSemaphoreGive(_rx_mutex);
    return read;
}

inline bool ble_is_connected() { return ble_connected; }

// ─── BLETask ───────────────────────────────────────────────────────────────────
// Tarea FreeRTOS que parsea el protocolo binario leyendo del buffer RX BLE.
// NÚCLEO: Core 0. STACK: 8192 bytes.

inline void BLETask(void* pv) {
    Serial.println("[CORE 0] BLE UART Task iniciado.");

    BLE_AtmosPacket  aPkt;
    BLE_SysPacket    sPkt;
    BLE_ComPacket    cPkt;

    // Buffer de ensamblaje en PSRAM — libera DRAM para el stack BLE.
    uint8_t* assem_buf = (uint8_t*)heap_caps_malloc(BLE_RX_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (assem_buf == nullptr) {
        Serial.println("[BLE] ERROR: no se pudo alojar assem_buf en PSRAM");
        vTaskDelete(NULL);
        return;
    }
    int assem_len = 0;

    while (true) {
        // ── Leer bytes disponibles ─────────────────────────────────────────
        int avail = ble_uart_available();
        if (avail > 0) {
            int space   = BLE_RX_BUF_SIZE - assem_len;
            int to_read = (avail < space) ? avail : space;
            assem_len += ble_uart_read(assem_buf + assem_len, to_read);
        }

        // ── Parsear buffer de ensamblaje ───────────────────────────────────
        int consumed = 0;

        while (consumed < assem_len) {
            uint8_t header = assem_buf[consumed];
            int remaining  = assem_len - consumed;

            if (header == BLE_MAGIC_ATMOS) {
                if (remaining < (int)sizeof(BLE_AtmosPacket)) break;
                memcpy(&aPkt, assem_buf + consumed, sizeof(BLE_AtmosPacket));
                consumed += sizeof(BLE_AtmosPacket);

                // Staging → loop() (Core 1) aplica los labels de forma segura.
                ble_atmos_t_int   = (int)aPkt.temp;
                ble_atmos_t_dec   = abs((int)(aPkt.temp * 10) % 10);
                ble_atmos_pending = true;
            }
            else if (header == BLE_MAGIC_SYS) {
                if (remaining < (int)sizeof(BLE_SysPacket)) break;
                memcpy(&sPkt, assem_buf + consumed, sizeof(BLE_SysPacket));
                consumed += sizeof(BLE_SysPacket);
            }
            else if (header == BLE_MAGIC_COM) {
                if (remaining < (int)sizeof(BLE_ComPacket)) break;
                memcpy(&cPkt, assem_buf + consumed, sizeof(BLE_ComPacket));
                consumed += sizeof(BLE_ComPacket);
                cPkt.ports[254] = '\0';
                strncpy(global_ports_list, cPkt.ports, 254);
                new_ports_available = true;
            }
            else if (header == BLE_MAGIC_AUDIO) {
                if (remaining < BLE_AUDIO_CHUNK_BYTES + 1) break;
                consumed += BLE_AUDIO_CHUNK_BYTES + 1;   // descartar
            }
            else if (header == BLE_MAGIC_VOICE_STATE) {
                if (remaining < 2) break;
                voice_state = assem_buf[consumed + 1];
                consumed += 2;
            }
            else {
                // Texto ASCII plano
                uint8_t c = assem_buf[consumed++];
                static String bleLine = "";

                if (c == '\n' || bleLine.length() > 250) {
                    if (bleLine.length() > 0) {
                        if (bleLine == "S:REC") {
                            is_recording     = true;
                            rec_start_millis = millis();
                            bleLine = "";
                            continue;
                        } else if (bleLine == "S:STOP") {
                            is_recording = false;
                            bleLine = "";
                            continue;
                        }

                        int target_terminal = 0;
                        const char* msg = bleLine.c_str();
                        if (bleLine.length() > 2 && msg[0] == 'T' &&
                            msg[1] >= '0' && msg[1] <= '3' && msg[2] == ':') {
                            target_terminal = msg[1] - '0';
                            msg += 3;
                        }

                        int next = (term_q_head + 1) % TERM_QUEUE_DEPTH;
                        if (next != term_q_tail) {
                            strncpy(term_queue[term_q_head], msg, 255);
                            term_queue[term_q_head][255] = '\0';
                            term_queue_terminal[term_q_head] = (uint8_t)target_terminal;
                            term_q_head = next;
                        }
                    }
                    bleLine = "";
                } else if (c >= 32 && c <= 126) {
                    bleLine += (char)c;
                }
            }
        }

        // ── Compactar buffer de ensamblaje ─────────────────────────────────
        if (consumed > 0 && consumed <= assem_len) {
            assem_len -= consumed;
            if (assem_len > 0) {
                memmove(assem_buf, assem_buf + consumed, assem_len);
            }
        } else if (consumed == 0 && assem_len == BLE_RX_BUF_SIZE) {
            assem_len = 0;
            Serial.println("[BLE] Parser: buffer lleno sin magic valido — descartando");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
