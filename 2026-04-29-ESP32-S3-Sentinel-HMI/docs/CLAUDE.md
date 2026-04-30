# Antigravity Sidecar — CLAUDE.md

## Qué es este proyecto

Panel HMI físico de monitoreo para desarrolladores. Una pantalla táctil **Elecrow CrowPanel 4.3" (ESP32-S3, 800×480)** recibe telemetría en tiempo real desde una PC Windows vía USB Serial. El objetivo es externalizar dashboards de sistema, clima y debugging a un panel dedicado para mantener la pantalla principal libre.

Sistema de dos partes que deben mantenerse sincronizadas:

| Parte | Lenguaje | Ubicación |
|---|---|---|
| **Sentinel Daemon** (PC) | Python | `sentinel_daemon.py` |
| **Firmware HMI** (ESP32-S3) | C++ / Arduino | `sentinel_hmi/sentinel_hmi.ino` |
| **UI generada** (SquareLine) | C (LVGL v8.3.x) | Exportar a `Monitor_UIFiles/`, copiar a `sentinel_hmi/src/ui/` |

---

## Agentes Disponibles

Invocar con `@agent-name` o dejar que Claude los seleccione automáticamente:

- **`firmware-hmi-expert`** — Firmware ESP32-S3, LVGL, FreeRTOS, PSRAM, LovyanGFX, coprocesador STC8H, bus I2C
- **`python-serial-expert`** — `sentinel_daemon.py`, pyserial, protocolos binarios, empaquetado con PyInstaller, arranque automático en Windows

---

## Arquitectura del Sistema

```
PC Windows
└── sentinel_daemon.py                    ← v2.0 — daemon activo
    ├── weather_loop    → Paquete 0xA5 cada 600s  (OpenWeatherMap)
    ├── sys_loop        → Paquete 0xB5 cada 1s    (psutil: CPU/RAM)
    ├── com_loop        → Paquete 0xC5 cada 3s    (lista de puertos COM)
    ├── proxy_loop      → Bridge transparente a COM secundario (10ms)
    ├── IPCServer       → Puerto 9090 (sentinel.logger)
    ├── REST API        → Puerto 8080
    └── WebSocket       → Puerto 8081 (streaming)
          │
          │ USB CDC Serial (115200 baud, COM4 por defecto)
          ▼
ESP32-S3 (sentinel_hmi.ino)
    ├── Core 0 — TaskSerial: recibe paquetes binarios, envía "MONITOR:PORTx"
    └── Core 1 — TaskLVGL:  lv_timer_handler, LovyanGFX, pantalla RGB 800×480
```

---

## Protocolo Binario (little-endian)

Los structs en C (`__attribute__((packed))`) y los `struct.pack` de Python **deben coincidir exactamente**:

| Magic | Nombre | Formato Python | Contenido |
|---|---|---|---|
| `0xA5` | AtmosPacket | `<BffibB` | magic, temp(f), hum(f), aqi(i), weather(b), checksum |
| `0xB5` | SysPacket | `<BBB H 32s B` | magic, cpu, gpu, ram_mb, status[32], checksum |
| `0xC5` | ComPacket | `<BBB 255s B` | magic, count, pad, ports[255], checksum |

El ESP32 responde con texto ASCII terminado en `\n`, ej: `MONITOR:COM5` o `MONITOR:Py_Console`.

---

## Mapa de Archivos Clave

```
sentinel_daemon.py         # Daemon principal PC — punto de entrada (v2.0)
data_fetchers.py           # WeatherFetcher (OpenWeatherMap) + SystemFetcher (psutil)
install_autostart.py       # Registrar SidecarDaemon.exe en Task Scheduler de Windows
build.bat                  # Build script PyInstaller → dist/SidecarDaemon.exe
requirements.txt           # pyserial, psutil, requests
test_logger.py             # Test librería sentinel.logger (requiere daemon activo)
test_socket.py             # Test IPC socket puerto 9090

src/
└── sentinel/              # Paquete Python — librería cliente para proyectos
    ├── __init__.py        # Exporta singleton `logger`
    └── logger.py          # SentinelLogger: envía logs por socket TCP 9090

sentinel_hmi/
├── sentinel_hmi.ino        # Firmware principal — setup() y tasks FreeRTOS
├── pins_config.h          # Pines GPIO, resolución, comandos I2C STC8H
├── LovyanGFX_Driver.h     # Configuración del driver de pantalla RGB paralela
├── audio_synth.h          # Síntesis de audio I2S
├── lvgl_fs_driver.h       # Driver filesystem para assets en LittleFS
├── secrets.h              # WiFi SSID/password (NO commitear — en .gitignore)
├── partitions.csv         # Esquema de particiones — Huge APP obligatorio
├── littlefs.bin           # Imagen LittleFS pre-compilada (flashear aparte)
├── data/assets/           # Assets binarios para LittleFS
└── src/ui/                # UI activa compilada ← EDITAR AQUÍ (no en Monitor_UIFiles/)
    ├── ui.c / ui.h
    ├── ui_screen*.c/h     # Pantallas: Dashboard, Serial, Morning, Day, Down, Night
    ├── ui_img_manager.c   # Loader PSRAM (modificado vs SquareLine export)
    └── ui_img_mty_*.c     # Imágenes compiladas (PSRAM)

Monitor_UIFiles/           # Salida de SquareLine Studio — NO editar manualmente
                           # Copiar a sentinel_hmi/src/ui/ después de exportar

squareline_project/
└── Monitor.spj            # Proyecto SquareLine Studio — fuente de verdad del diseño

images/                    # Fuentes PNG originales de los fondos
├── mty_amanecer.png / mty_dia.png / mty_atardecer.png / mty_noche.png
├── monitor_bg.png / Monitor_Serial.png   # Compilados en UI (ui_img_*.c)
└── (referencia — las imágenes compiladas viven en sentinel_hmi/src/ui/)

Inter/                     # Fuente tipográfica (variable fonts)
├── Inter-VariableFont_opsz,wght.ttf
└── Inter-Italic-VariableFont_opsz,wght.ttf

downgrade_v9_to_v8.py      # Convierte exportaciones LVGL v9 → v8.3.x (usar si necesario)
```

---

## Reglas Críticas — Leer Antes de Modificar

### Firmware ESP32-S3

1. **OPI obligatorio**: Flash Mode y PSRAM deben estar en **OPI (Octal SPI)** en Arduino IDE. Si el serial reporta 192KB de RAM en lugar de 8MB, es este error.
2. **Partición Huge APP**: El firmware con LVGL + WiFi no cabe en la partición default.
3. **Mutex I2C**: `i2c_mutex` (definido en `sentinel_hmi.ino`) DEBE envolver toda operación con `Wire`. El táctil GT911 (`0x5D`) y el coprocesador STC8H (`0x30`) comparten el bus.
4. **Audio en SRAM interna**: Los buffers DMA de I2S van con `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`. Nunca en PSRAM.
5. **Imágenes de fondo**: Al cambiar de pantalla, llamar `lv_img_cache_invalidate_src()` ANTES de `free()` o habrá Core Panic.
6. **`delay(150)` post Wire.begin()**: Siempre en `setup()` antes del primer comando al STC8H (`0x30`) para evitar race condition con el 8051 interno.

### Python Daemon

7. **`serial_lock` siempre**: Todo `ser.write()` debe ir dentro de `with self.serial_lock`. Sin excepción.
8. **DTR/RTS manual**: La secuencia baja/sube de DTR+RTS con `sleep(1)` entre cada paso evita que Python resetee el ESP32 al abrir el puerto.
9. **`timeout=0.1` en Serial**: Nunca `None`. Si el ESP32 se desconecta, `readline()` sin timeout bloquea el hilo para siempre.
10. **Detección de puerto por VID/PID**: `find_hmi_port()` (línea ~94) busca el CrowPanel por `HMI_VID=0x1A86 / HMI_PID=0x7522` (CH340). No hardcodear `COM4` — usar esa función.
11. **API keys en env vars**: `OWM_API_KEY` lee de `SENTINEL_OWM_API_KEY` con fallback al valor en código. Para setear en Windows: `setx SENTINEL_OWM_API_KEY "tu_key"`. Nunca agregar nuevas keys hardcodeadas sin env var.

---

## Comandos I2C al Coprocesador STC8H (dirección `0x30`)

| Comando | Efecto |
|---|---|
| `0` | Backlight máximo brillo |
| `244` | Backlight mínimo |
| `245` | Backlight apagado |
| `246` | Buzzer ON |
| `247` | Buzzer OFF |
| `248` | Amplificador audio (I2S) ON |
| `249` | Amplificador audio (I2S) OFF |

> Nota: `pins_config.h` usa `STC8_I2C_ADDR 0x18`. La dirección real documentada por Elecrow es `0x30`. Verificar con el hardware físico si hay discrepancia.

---

## Configuración Arduino IDE (Obligatoria)

| Setting | Valor |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Mode | **QIO OPI** |
| PSRAM | **OPI PSRAM** |
| Flash Size | 16MB |
| Partition Scheme | **Huge APP (3MB No OTA)** |
| CPU Frequency | 240MHz |
| USB Mode | USB-OTG (CDC) |

---

## Flujo de Trabajo UI

1. Editar diseño en **SquareLine Studio** (`squareline_project/Monitor.spj`)
2. Exportar UI → directorio `Monitor_UIFiles/`
3. Copiar archivos exportados a `sentinel_hmi/src/ui/`
4. Si hay imágenes nuevas: ejecutar `downgrade_v9_to_v8.py` si se exportó con LVGL v9
5. Compilar y flashear desde Arduino IDE

---

## Dependencias Python

```bash
pip install -r requirements.txt
# pyserial, psutil, requests

# Para instalar la librería sentinel en modo editable:
pip install -e .

# Para empaquetar ejecutable:
pip install pyinstaller
build.bat   # → dist/SidecarDaemon.exe
```

## Ejecutar el Daemon

```bash
# 1. Setear la API key (gratis en https://home.openweathermap.org/api_keys)
setx SENTINEL_OWM_API_KEY "tu_key"   # Windows — reiniciar terminal después

# 2. Ejecutar
python sentinel_daemon.py
# Detección automática por VID/PID — no hace falta elegir COM.
# Puertos: IPC=9090, REST=8080, WebSocket=8081
```
