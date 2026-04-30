# Sentinel Monitor — Guía de Usuario
**Autor: Ignacio Aguilera**

Panel HMI de monitoreo en tiempo real para desarrolladores.
Tu pantalla CrowPanel ESP32-S3 actúa como terminal externo para logs, métricas, archivos CSV/JSONL y más, sin ocupar pantalla en tu PC.

---

## Índice

1. [Requisitos y configuración inicial](#1-requisitos-y-configuración-inicial)
2. [Instalación (2 pasos)](#2-instalación-2-pasos)
3. [Arquitectura del sistema](#3-arquitectura-del-sistema)
4. [Uso desde Python](#4-uso-desde-python)
5. [Uso desde Arduino / microcontrolador](#5-uso-desde-arduino--microcontrolador)
6. [Referencia de métodos](#6-referencia-de-métodos)
7. [Archivos CSV y JSONL](#7-archivos-csv-y-jsonl)
8. [Métricas y barra de progreso](#8-métricas-y-barra-de-progreso)
9. [Múltiples terminales (sesiones)](#9-múltiples-terminales-sesiones)
10. [Grabación de sesiones](#10-grabación-de-sesiones)
11. [Leer los datos desde Python](#11-leer-los-datos-desde-python)
12. [Solución de problemas](#12-solución-de-problemas)

---

## 1. Requisitos y configuración inicial

### Hardware
| Componente | Detalle |
|---|---|
| Pantalla HMI | Elecrow CrowPanel 4.3" (ESP32-S3, 800×480) |
| Conexión | Cable USB-C al PC |
| PC | Windows 10/11 |

### Software
| Herramienta | Versión mínima |
|---|---|
| Python | 3.10+ |
| Arduino IDE | 2.x |
| Board package | esp32 by Espressif 2.x |

### Configuración obligatoria en Arduino IDE

> Sin esto el firmware no compila o no arranca correctamente.

| Opción | Valor |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Mode | **QIO OPI** |
| PSRAM | **OPI PSRAM** |
| Flash Size | 16 MB |
| Partition Scheme | **Huge APP (3MB No OTA)** |
| CPU Frequency | 240 MHz |
| USB Mode | USB-OTG (CDC) |

---

## 2. Instalación (2 pasos)

### Paso 1 — Daemon + librería Python

Doble-click en **`install.bat`** desde la raíz del proyecto.

El instalador hace automáticamente:
- Verifica Python
- Instala `pyserial`, `psutil`, `requests`
- Instala el paquete `sentinel` (`from sentinel import logger`)
- Genera `dist/SidecarDaemon.exe`
- Pregunta si registrar autostart con Windows (UAC automático)

### Paso 2 — Librería Arduino

1. Doble-click en **`make_arduino_zip.bat`** → genera `Sentinel.zip`
2. En Arduino IDE: `Sketch > Include Library > Add .ZIP Library...`
3. Seleccionar `Sentinel.zip`
4. Listo — `#include <Sentinel.h>` disponible

### Paso 3 — Flashear el firmware

1. Abrir `sentinel_hmi/sentinel_hmi.ino` en Arduino IDE
2. Verificar configuración de la tabla anterior
3. Compilar y subir (`Ctrl+U`)
4. La pantalla muestra el dashboard al iniciar

---

## 3. Arquitectura del sistema

```
Tu script Python / Arduino
        │
        │  TCP 9090 (Python)  /  USB Serial (Arduino)
        ▼
sentinel_daemon.py  ←───────────────────────────────────────┐
    ├─ Recibe tus logs y métricas                           │
    ├─ Guarda archivos en sentinel_logs/                    │
    ├─ Reenvía datos a la pantalla vía USB Serial           │
    └─ Expone REST API (8080) y WebSocket (8081)            │
                                                            │
ESP32-S3 (CrowPanel)  ──────────────────────────────────────┘
    ├─ Muestra logs en terminales con colores
    ├─ Dashboard de sistema (CPU, RAM, clima)
    └─ Selector de puerto COM para placas Arduino
```

**El daemon siempre debe estar corriendo.** Inícialo con:
```
dist\SidecarDaemon.exe
```
o:
```
python sentinel_daemon.py
```

---

## 4. Uso desde Python

### Instalación del paquete

```bash
pip install .          # desde la raíz del proyecto
```

### Importar

```python
from sentinel import logger   # singleton listo para usar
```

### Ejemplo mínimo

```python
from sentinel import logger

logger.info("Sistema iniciado")
logger.warn("Voltaje bajo: 3.1 V")
logger.error("Sensor sin respuesta")
```

### Ejemplo completo

Ver `examples/demo_sentinel.py` — ejecutar con:
```bash
python examples/demo_sentinel.py
```

---

## 5. Uso desde Arduino / microcontrolador

### Requisitos

- Librería Sentinel instalada (ver [Instalación](#2-instalación-2-pasos))
- Placa conectada al PC
- En la pantalla: seleccionar el COM de tu placa (botón en pantalla)
- Daemon corriendo

### Ejemplo mínimo

```cpp
#include <Sentinel.h>

SentinelLogger sentinel(Serial);

void setup() {
    Serial.begin(115200);
    delay(1000);
    sentinel.info(F("Sistema iniciado"));
}

void loop() {
    sentinel.metric("temp", readTemp(), "C");
    delay(500);
}
```

### Ejemplo completo

Ver `sentinel_arduino/examples/Demo/Demo.ino` — abrir en Arduino IDE y subir.

### Sin la librería (protocolo directo)

Si no quieres instalar la librería, puedes usar el protocolo ASCII directamente:

```cpp
// En cualquier Arduino/ESP sin librerías extras:
Serial.println("CSV>sensores:25.3,60.1,1013.0");  // fila CSV
Serial.println("CSV!sensores");                     // cerrar CSV
Serial.println("KV>run:epoch=1,loss=0.34,ok=true");// registro JSONL
Serial.println("MET>temp:25.3:C");                  // métrica
```

---

## 6. Referencia de métodos

### Python — `SentinelLogger`

| Método | Descripción |
|---|---|
| `logger.print(msg)` | Mensaje genérico (cyan) |
| `logger.info(msg)` | Informativo (azul) |
| `logger.warn(msg)` | Advertencia (ámbar) |
| `logger.error(msg)` | Error (rojo) |
| `logger.separator(char, len)` | Línea separadora (gris) |
| `logger.metric(name, value, unit)` | Métrica con unidad |
| `logger.progress(current, total, label)` | Barra de progreso |
| `logger.table(headers, rows)` | Tabla ASCII |
| `logger.open(filename, headers)` | Abre archivo CSV o JSONL |
| `logger.start_recording(name, txt, csv_log)` | Inicia grabación |
| `logger.stop_recording()` | Detiene grabación |
| `logger.session(name)` | Nueva instancia en terminal separado |

### Arduino — `SentinelLogger`

| Método | Descripción |
|---|---|
| `sentinel.print(msg)` | Mensaje genérico (cyan) |
| `sentinel.info(msg)` | Informativo (azul) |
| `sentinel.warn(msg)` | Advertencia (ámbar) |
| `sentinel.error(msg)` | Error (rojo) |
| `sentinel.separator(char, len)` | Línea separadora (gris) |
| `sentinel.metric(name, value, unit)` | Métrica con unidad |
| `sentinel.metric(label, current, total)` | Barra de progreso |
| `sentinel.open(filename, headers, count)` | Abre archivo, retorna `SentinelFile` |
| `sentinel.record(name)` | Inicia grabación |
| `sentinel.stop()` | Detiene grabación |
| `sentinel.sendRaw(line)` | Envía línea cruda sin formato |

### `SentinelFile` (Python y Arduino)

| Método | Descripción |
|---|---|
| `f.write(data)` | Escribe fila (CSV) o entrada (JSONL) |
| `f.close()` | Cierra el archivo en el daemon |

---

## 7. Archivos CSV y JSONL

Los archivos se guardan en `sentinel_logs/` con la siguiente estructura:

```
sentinel_logs/
├── csv/
│   └── sensores.csv       ← abierto con logger.open("sensores.csv", [...])
├── json/
│   └── run.jsonl          ← abierto con logger.open("run.jsonl")
├── metrics.jsonl          ← todas las métricas (logger.metric)
└── recordings/
    └── sesion_2026-04-01.txt
```

### CSV — Python

```python
# Abrir, escribir y cerrar
with logger.open("sensores.csv", ["temp", "hum", "presion"]) as f:
    for muestra in datos:
        f.write([muestra.temp, muestra.hum, muestra.presion])

# Leer después
import pandas as pd
df = pd.read_csv("sentinel_logs/csv/sensores.csv")
print(df.describe())
```

### CSV — Arduino

```cpp
const char* cols[] = {"temp_c", "hum_pct", "presion_hpa"};
SentinelFile data = sentinel.open("sensores.csv", cols, 3);

float row[] = {25.3f, 60.1f, 1013.0f};
data.write(row, 3);
data.close();
```

### JSONL — Python

```python
run = logger.open("experimento.jsonl")
run.write({"epoch": 1, "loss": 0.342, "acc": 0.87, "ok": True})
run.write({"epoch": 2, "loss": 0.261, "acc": 0.91, "ok": True})
run.close()

# Leer después
df = pd.read_json("sentinel_logs/json/experimento.jsonl", lines=True)
print(df[["epoch", "loss", "acc"]])
```

### JSONL — Arduino

```cpp
SentinelFile run = sentinel.open("experimento.jsonl");
run.write("epoch=1,loss=0.342,acc=0.87,ok=true");
run.write("epoch=2,loss=0.261,acc=0.91,ok=true");
run.close();
```

> **Tipos inferidos en JSONL**: `true`/`false` → bool, `1`/`42` → int, `0.34` → float, texto → string.

---

## 8. Métricas y barra de progreso

Las métricas aparecen en la pantalla y se guardan en `sentinel_logs/metrics.jsonl`.

### Python

```python
# Métrica simple
logger.metric("temperatura", 25.3, "°C")
logger.metric("accuracy",    91.4, "%")
logger.metric("latencia",    12,   "ms")

# Barra de progreso (mismo método, distinta firma)
for i, batch in enumerate(train_data):
    logger.progress(i + 1, len(train_data), "Entrenando")
```

### Arduino

```cpp
// Métrica simple
sentinel.metric("temp",    25.3f, "C");
sentinel.metric("accuracy", 91,   "%");

// Barra de progreso — misma firma pero con uint32_t
sentinel.metric("Calibrando", step, TOTAL);
// → [=========>          ] 45% (45/100)
```

---

## 9. Múltiples terminales (sesiones)

La pantalla tiene 4 terminales (T0-T3). Puedes asignar cada parte de tu sistema a uno distinto.

### Python

```python
from sentinel import logger

# logger usa T0 por defecto
entrenamiento = logger.session("Training")    # T1
inferencia    = logger.session("Inference")   # T2

entrenamiento.info("Epoch 1/10 iniciado")
inferencia.info("Modelo cargado en GPU")

entrenamiento.metric("loss", 0.342)
inferencia.metric("fps",   47)
```

### Arduino

```cpp
SentinelLogger sentinel0(Serial);          // T0 por defecto
SentinelLogger sentinel1(Serial, 1);       // T1 explícito

sentinel0.info(F("Sensor loop"));
sentinel1.info(F("Comunicacion loop"));
```

---

## 10. Grabación de sesiones

Graba todo lo que aparece en los terminales en archivos de texto.

### Python

```python
# Solo archivo de texto (por defecto)
logger.start_recording("mi_experimento")

# Texto + CSV de sesión (timestamp, nivel, mensaje por fila)
logger.start_recording("run_01", csv_log=True)

# ... tus logs ...

logger.stop_recording()
# → sentinel_logs/recordings/mi_experimento_2026-04-01.txt
```

### Desde la pantalla

Toca el botón **REC** en la pantalla para iniciar/detener grabación sin código.

---

## 11. Leer los datos desde Python

```python
import pandas as pd

# CSV de sensores
df = pd.read_csv("sentinel_logs/csv/sensores.csv")
print(df.describe())
df.plot(x="timestamp", y="temp_c")

# JSONL de entrenamiento
df = pd.read_json("sentinel_logs/json/entrenamiento.jsonl", lines=True)
print(df[["epoch", "loss", "acc"]].tail(5))
df.plot(x="epoch", y=["loss", "acc"])

# Historial de métricas
df = pd.read_json("sentinel_logs/metrics.jsonl", lines=True)
df_temp = df[df["name"] == "temp"]
```

---

## 12. Solución de problemas

### Mi placa no aparece en la pantalla

La pantalla filtra los puertos COM para mostrar solo placas de desarrollo (Arduino, ESP32, STM32, etc.). Si tu placa no aparece:

1. Verifica que el driver USB-Serial esté instalado (CH340, CP210x, FTDI)
2. Confirma que la placa aparece en el Administrador de Dispositivos de Windows bajo **Puertos (COM y LPT)**
3. Si sigue sin aparecer, el VID de tu chip puede no estar en la whitelist — revisar `_DEV_BOARD_VIDS` en `sentinel_daemon.py`

### El daemon no conecta a la pantalla

```
[ERROR] No se encontró el CrowPanel
```

- Verifica que la pantalla esté conectada por USB
- Comprueba en el Administrador de Dispositivos el VID/PID del CrowPanel
- El VID esperado es `0x1A86` (CH340), PID `0x7522`

### Los mensajes no llegan a la pantalla desde Python

- Confirma que el daemon está corriendo: `python sentinel_daemon.py`
- El puerto IPC es `127.0.0.1:9090` — verifica que no esté bloqueado
- Ejecutar `python test_socket.py` para diagnosticar

### La pantalla muestra texto corrupto

- El firmware espera mensajes ASCII. Evitar acentos en mensajes de Arduino (usar `F("texto")`)
- En Python la librería filtra caracteres no-ASCII automáticamente

### El scroll del terminal no funciona

- Requiere firmware actualizado (v2.0+). Flashear de nuevo desde Arduino IDE.

### Los archivos CSV/JSONL están vacíos

- El daemon debe estar corriendo durante toda la captura
- Llamar `f.close()` o salir del bloque `with` para forzar el cierre del archivo

---

*Sentinel Monitor — Ignacio Aguilera*
