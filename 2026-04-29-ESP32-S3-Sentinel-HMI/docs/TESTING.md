# Guia de Pruebas — Sentinel Monitor
> Actualizado: 2026-04-01 (rev 3)
> Sigue los pasos en orden. Cada fase depende de la anterior.

---

## ¿Qué cambié? (sesión actual)

### `src/sentinel/logger.py` — nueva clase `SentinelFile` + método `open()`
- Agregada clase `SentinelFile` (líneas 72–139): encapsula un archivo CSV o JSONL abierto en el daemon
- Agregado método `SentinelLogger.open(filename, headers)` (línea ~362): retorna `SentinelFile`, formato determinado por extensión
- Faltaba salto de línea antes de `_DEFAULT_PORT`: corregido

### `sentinel_daemon.py` — filtro de puertos COM (solo placas de desarrollo)
- Agregado set `_DEV_BOARD_VIDS` con VIDs conocidos: WCH CH340, Espressif, FTDI, Silicon Labs, Arduino, STM32, Adafruit, etc.
- Agregada función `_is_dev_board(port_info)`: whitelist por VID + exclusión por keywords Bluetooth/audio en description/manufacturer/hwid
- Modificada `send_com_list_packet()`: filtra con `_is_dev_board()` antes de armar la lista

### `sentinel_hmi/audio_synth.h` — reescrito para streaming PCM
- Eliminado: tono senoidal 440 Hz, `AudioSynthTask`, `SAMPLE_RATE=44100`
- Agregado: `SAMPLE_RATE=16000`, pool de 8 buffers `DRAM_ATTR` de 512 samples c/u
- Agregado: `audio_filled_q` y `audio_free_q` (colas FreeRTOS de índices, no-static)
- Agregada `audio_init_queues()`: crea colas y pre-llena `audio_free_q` con índices 0..7
- Agregada `AudioTask()`: drena cola con timeout 40ms → expande mono→stereo → I2S. Silencio en underrun

### `sentinel_hmi/sentinel_hmi.ino` — 5 cambios puntuales
- `#define MAGIC_AUDIO 0xD5` y `AUDIO_CHUNK_BYTES 1024` después de MAGIC_COM
- `Serial.setRxBufferSize(1024 → 2048)` en setup
- Llamada `audio_init_queues()` antes del `xTaskCreatePinnedToCore` de audio
- Nombre de tarea `AudioSynthTask → AudioTask`
- Case `MAGIC_AUDIO` en `SerialBridgeTask`: recibe 1025 bytes (1 header + 1024 PCM), despacha al pool por índice de slot

### `audio_stream.py` — nuevo script de streaming de audio PC→ESP32
- Captura audio del sistema via WASAPI loopback del output por defecto
- Remuestrea a 16kHz mono con `scipy.signal.resample_poly`
- Envía paquetes `[0xD5][1024 bytes PCM]` por USB Serial
- Fix crítico en `connect()`: configura `dtr=False`, `rts=False` ANTES de `open()` para no resetear ESP32 via CH340
- Fix en `_find_device_and_rate()`: usa output device + `WasapiSettings(loopback=True)` directamente, sin buscar device "loopback" por nombre

### `sentinel_arduino/` — nueva librería Arduino (creada completa)
- `src/Sentinel.h`: `SentinelLogger` + `SentinelFile`, compatible con cualquier `Stream&`
- `src/Sentinel.cpp`: implementación completa, `_splitExt()`, `write()` overloads, `close()`
- `library.properties`: metadatos de librería Arduino
- `examples/BasicLogging/BasicLogging.ino`, `CSVSensor/CSVSensor.ino`, `MLPipeline/MLPipeline.ino`, `Demo/Demo.ino`

### Archivos nuevos de instalación y documentación
- `install.bat`: instalador todo-en-uno (pip + build exe + autostart UAC)
- `make_arduino_zip.bat`: genera `Sentinel.zip` para Arduino IDE
- `examples/demo_sentinel.py`: demo completo de la librería Python
- `SENTINEL_GUIDE.md`: guía detallada de usuario
- `requirements.txt`: agregados `sounddevice`, `numpy`, `scipy`
- `pyproject.toml`: descripción actualizada

---

## ¿Por qué lo cambié?

| Cambio | Razón técnica |
|---|---|
| Filtro COM por VID | La pantalla mostraba puertos Bluetooth, audio y virtuales. Los VIDs de fabricantes de chips USB-Serial son únicos y estables — whitelist más fiable que parsear strings de descripción |
| `SentinelFile.open()` | API simétrica con Python nativo (`open()`). La extensión determina el formato sin necesidad de dos métodos distintos |
| Audio a 16kHz | 44100 Hz stereo = 176 KB/s; USB CDC del ESP32 puede manejarlo pero no deja ancho de banda para telemetría. 16kHz mono = 32 KB/s, deja 75% del canal libre |
| Pool estático `DRAM_ATTR` | Los buffers DMA de I2S deben estar en SRAM interna. `heap_caps_malloc` en ruta caliente genera fragmentación; pool estático es determinista |
| `dtr=False` antes de `open()` | Con `serial.Serial(port, ...)` pyserial sube DTR al abrir → CH340 detecta transición y resetea ESP32 → pantalla negra. La secuencia correcta es configurar DTR antes del `open()` |
| `WasapiSettings(loopback=True)` en output device | Buscar un device llamado "loopback" por nombre es frágil (varía por idioma y driver). WASAPI permite capturar el output como input pasando el índice del output + flag loopback |
| Discard loop en firmware | `for(i=0; i<1024; i++) Serial.read()` = 1024 llamadas de sistema → pausa el parser serial ~10ms. Loop de 64 bytes con `readBytes` = 16 llamadas |

---

## ¿Cómo funciona?

### Filtro de puertos COM

```
serial.tools.list_ports.comports()
    └── para cada puerto:
        _is_dev_board(p)
            ├── p.vid in _DEV_BOARD_VIDS  → True (incluir)
            ├── "bluetooth"/"bt" en description/manufacturer/hwid → False (excluir)
            └── p.vid is None → False (excluir: virtual, BT sin driver)
        → real_ports = [p.device para p que pasa el filtro]
→ available = ["PY CH0".."PY CH3"] + real_ports
→ ComPacket 0xC5 → ESP32 → Dashboard cards
```

### Audio streaming (nuevo flujo completo)

```
Windows WASAPI (speakers)
    └── sounddevice.InputStream(device=output_idx, loopback=True)
        └── raw float32 @ 48000 Hz stereo
            └── mean(axis=1) → mono
                └── resample_poly(up=16000//g, down=48000//g) → 16kHz mono
                    └── clip + astype(int16) → 1024 bytes por chunk
                        └── deque(maxlen=16) [thread-safe SPSC]
                            └── _send_loop() [hilo dedicado]
                                └── serial.write([0xD5] + 1024_bytes)

ESP32 SerialBridgeTask (Core 0):
    while Serial.available():
        header = Serial.peek()
        if header == 0xD5:
            if available >= 1025:
                Serial.read()  # consumir 0xD5
                xQueueReceive(audio_free_q, &slot)  # slot libre del pool
                Serial.readBytes(audio_pool[slot], 1024)
                xQueueSend(audio_filled_q, &slot)

AudioTask (Core 0):
    while true:
        xQueueReceive(audio_filled_q, &slot, 40ms)
        if got:
            para i en 0..511:
                _stereo_buf[i*2]   = audio_pool[slot][i]  # L
                _stereo_buf[i*2+1] = audio_pool[slot][i]  # R
            xQueueSend(audio_free_q, &slot)  # devolver slot
            i2s_write(I2S_NUM_0, _stereo_buf, 2048, ...)
        else:
            i2s_zero_dma_buffer()  # silencio en underrun
```

### API de archivos Python/Arduino

```
# Python
with logger.open("datos.csv", ["temp","hum"]) as f:
    f.write([25.3, 60.1])
# → IPC → daemon → sentinel_logs/csv/datos.csv

# Arduino
SentinelFile f = sentinel.open("datos.csv", cols, 2);
float row[] = {25.3f, 60.1f};
f.write(row, 2);   # → Serial → "CSV>datos:25.3,60.1\n" → daemon
f.close();         # → Serial → "CSV!datos\n"
```

---

## Estado actual del sistema

| Componente | Estado |
|---|---|
| Daemon Python (`sentinel_daemon.py`) | v2.0 — multi-terminal T0-T3, filtro COM, CSV/JSONL/MET |
| Firmware ESP32-S3 (`sentinel_hmi.ino`) | LVGL 8.3.x, FreeRTOS, audio streaming 0xD5 |
| Librería Python (`src/sentinel/logger.py`) | `SentinelFile.open()`, CSV, JSONL, métricas, tabla, progreso |
| Librería Arduino (`sentinel_arduino/`) | `SentinelLogger` + `SentinelFile`, sin dependencias externas |
| Audio streaming (`audio_stream.py`) | WASAPI loopback → 16kHz mono → I2S, standalone |
| Protocolo binario | `0xA5` Atmos, `0xB5` Sys, `0xC5` Com, `0xD5` Audio |
| Protocolo ASCII | `CSV>`, `CSV!`, `KV>`, `MET>`, `JSON!`, `T{n}:msg` |
| Instalación | `install.bat` (doble-click), `make_arduino_zip.bat` |

---

## Prerequisitos de hardware

- CrowPanel 4.3" (ESP32-S3) conectado por USB
- Arduino IDE configurado: Flash=QIO OPI, PSRAM=OPI, Partition=Huge APP 3MB
- `sentinel_hmi/secrets.h` con SSID y password WiFi válidos
- API key de OpenWeatherMap en `sentinel_daemon.py` (línea `OWM_API_KEY`)
  - **Nota:** las claves nuevas de OWM tardan 1-3 horas en activarse

---

## BLOQUE A — Firmware HMI (ESP32-S3)

### A.1 Compilar y flashear

1. Abrir `sentinel_hmi/sentinel_hmi.ino` en Arduino IDE
2. Verificar settings:
   - Board: `ESP32S3 Dev Module`
   - Flash Mode: `QIO OPI`
   - PSRAM: `OPI PSRAM`
   - Partition Scheme: `Huge APP (3MB No OTA)`
   - USB Mode: `USB-OTG (CDC)`
3. Compilar y flashear
4. Abrir Monitor Serie a 115200 baud

**Resultado esperado en Monitor Serie:**
```
>>> TAMANO REAL DEL PSRAM DETECTADO: 8388608 bytes <<<
[CORE 0] Serial Bridge iniciado.
Conectando WiFi....
WiFi Conectado!
[SENTINEL] Monitor HMI iniciado.
```

Si el PSRAM muestra 192KB: Flash Mode no está en OPI.

### A.2 Boot y pantalla hub

Al encender: spinner + `SENTINEL MONITOR` ~5-10s, luego pantalla hub por hora:

| Hora | Pantalla |
|---|---|
| 06:00–08:59 | Amanecer |
| 09:00–17:59 | Día |
| 18:00–20:59 | Atardecer |
| 21:00–05:59 | Noche |

### A.3 Navegación hub → Dashboard → Hub

- Tocar hub → Dashboard instantáneo (sin fade)
- Tocar **HUB** en Dashboard → pantalla hub de la hora actual
- Tocar **HUB** a distintas horas → pantalla cambia según rango

### A.4 Buzzer en tarjetas del Dashboard

Tocar cualquier tarjeta (PY CH0, COM3, etc.) → tono breve del buzzer.

### A.5 Modal de selección de puerto — scroll y orden

Al hacer Split 2x, el modal muestra:
1. `— Ninguno (inactivo)` — siempre visible arriba (fuera del área scrollable)
2. `PY CH0..CH3` — primeros en la lista scrollable
3. Puertos COM reales (solo placas de desarrollo, sin Bluetooth)
4. `Cancelar` — siempre visible abajo (fuera del área scrollable)

**Verificar scroll:** si hay más de 4 puertos COM, el área entre Ninguno y Cancelar debe ser scrollable con drag táctil.

### A.6 Terminales y split view

| Acción | Resultado esperado |
|---|---|
| Split 1x→2x | Área dividida, modal T1 |
| Split 2x→4x | Modal T2 luego T3 automático |
| DASH desde split | Vuelta a Dashboard, proxies desconectados |

### A.7 Scroll en terminales

Con datos fluyendo en un terminal:
- Drag hacia arriba → scroll manual funciona
- Drag hacia abajo hasta el fondo → auto-scroll se reactiva (sticky: activa solo si estás a <60px del fondo)
- El label `lv_label` NO debe absorber eventos táctiles (su flag CLICKABLE debe estar cleared)

---

## BLOQUE B — Daemon Python

### B.1 Instalar dependencias

```bash
pip install -r requirements.txt
```

Incluye: `pyserial`, `psutil`, `requests`, `sounddevice`, `numpy`, `scipy`

### B.2 Detectar CrowPanel por VID:PID

```bash
python -c "import serial.tools.list_ports; [print(f'{p.device}  VID:{p.vid:04X}  {p.description}') for p in serial.tools.list_ports.comports() if p.vid == 0x1A86]"
```

**Resultado esperado:**
```
COM8  VID:1A86  USB-SERIAL CH340
```

### B.3 Arrancar el daemon

```bash
python sentinel_daemon.py
```

**Resultado esperado:**
```
[IPC] Servidor escuchando en 127.0.0.1:9090
[API] REST server en http://127.0.0.1:8080
[WS] WebSocket server en ws://127.0.0.1:8081/stream
[DAEMON] HMI conectado en COM8.
```

### B.4 Verificar filtro de puertos COM en pantalla

Con el daemon corriendo, ir al Dashboard de la pantalla.

**Resultado esperado:** Solo se ven puertos de placas de desarrollo (Arduino, ESP32, STM32, etc.). Los puertos Bluetooth (ej. `COM3 - Dispositivo de entrada de audio Bluetooth`) no deben aparecer.

**Prueba de fallo:** Si aparece un puerto Bluetooth, verificar que su VID no está en `_DEV_BOARD_VIDS` y que la descripción contiene "bluetooth". Agregar el VID al set o revisar el string de descripción.

### B.5 Canal Python — activar y enviar mensaje

```bash
python -c "import socket; s=socket.create_connection(('127.0.0.1',9090),3); s.sendall(b'MONITOR:T1:PY CH2\n'); s.close()"
```

Luego:

```bash
python -c "from sentinel import logger; logger.set_terminal(2); logger.info('Mensaje en PY CH2')"
```

**Resultado esperado en pantalla (slot T1):** mensaje en azul.

### B.6 Proxy COM multi-terminal

```bash
python -c "import socket; s=socket.create_connection(('127.0.0.1',9090),3); s.sendall(b'MONITOR:T1:COM5\n'); s.close()"
```

**Resultado esperado:** `[PROXY] T1: Iniciando monitoreo de COM5`

### B.7 IPC — fallo silencioso con daemon apagado

```bash
# Con daemon cerrado:
python -c "from sentinel import logger; logger.print('Sin daemon'); print('Sin excepcion — OK')"
```

**Resultado esperado:** Sin excepción `ConnectionRefusedError`. La librería falla silenciosamente.

### B.8 Reconexión automática

1. Desconectar USB de la pantalla
2. `[DAEMON] Pantalla desconectada. Buscando reconexion...`
3. Reconectar → en <5s: `[DAEMON] HMI conectado en COM8.`

---

## BLOQUE C — Librería Python sentinel

### C.1 Instalar y verificar

```bash
pip install .
python -c "from sentinel import logger; print(type(logger).__name__)"
```

**Resultado esperado:** `SentinelLogger`

### C.2 Demo completo

Con el daemon corriendo:

```bash
python examples/demo_sentinel.py
```

**Resultado esperado en pantalla:** Secuencia de mensajes de colores, métricas, barra de progreso, tabla ASCII, y al final dos líneas de archivos cerrados.

**Resultado esperado en disco:**
```
sentinel_logs/csv/sensores.csv     ← 20 filas con temp, hum, presion
sentinel_logs/json/entrenamiento.jsonl  ← 10 entradas JSON
sentinel_logs/metrics.jsonl        ← métricas capturadas
```

Verificar:
```bash
python -c "import pandas as pd; df=pd.read_csv('sentinel_logs/csv/sensores.csv'); print(df.describe())"
python -c "import pandas as pd; df=pd.read_json('sentinel_logs/json/entrenamiento.jsonl', lines=True); print(df[['epoch','loss','acc']])"
```

### C.3 SentinelFile — CSV con context manager

```bash
python -c "
from sentinel import logger
with logger.open('test.csv', ['x', 'y']) as f:
    f.write([1, 2])
    f.write([3, 4])
print('CSV cerrado')
"
```

**Resultado esperado:** `sentinel_logs/csv/test.csv` con 2 filas de datos + header.

### C.4 SentinelFile — JSONL

```bash
python -c "
from sentinel import logger
run = logger.open('test.jsonl')
run.write({'a': 1, 'b': True, 'c': 3.14})
run.close()
print('JSONL cerrado')
"
```

**Resultado esperado:** `sentinel_logs/json/test.jsonl` con 1 línea JSON.

### C.5 Métricas y progreso

```bash
python -c "
from sentinel import logger
logger.metric('cpu', 72.3, '%')
logger.metric('ram', 4096, 'MB')
logger.progress(7, 10, 'Progreso')
"
```

**Resultado esperado en pantalla:** Dos líneas de métrica y una barra `[==============>     ] 70% (7/10)`.

### C.6 Tabla ASCII

```bash
python -c "
from sentinel import logger
logger.table(['Modelo','Acc','ms'], [['YOLOv8n','87%',12],['YOLOv8s','91%',28]])
"
```

**Resultado esperado:** Tabla ASCII con bordes `+---+---+` en el terminal de la pantalla.

### C.7 Sesiones multi-terminal

```bash
python -c "
from sentinel import logger
s1 = logger.session('Training')
s2 = logger.session('Inference')
s1.info('Epoch 1/10')
s2.info('Modelo listo')
s1.metric('loss', 0.342)
s2.metric('fps', 47)
"
```

**Prerequisito:** pantalla en modo Split 4x con T1 y T2 activos.
**Resultado esperado:** T1 muestra logs de Training; T2 muestra logs de Inference.

### C.8 Grabación con control de formato

```bash
python -c "
from sentinel import logger
logger.start_recording('mi_test')            # solo .txt
logger.info('Línea grabada')
logger.stop_recording()
"
```

```bash
python -c "
from sentinel import logger
logger.start_recording('run_csv', csv_log=True)  # .txt + .csv
logger.warn('Con CSV')
logger.stop_recording()
"
```

**Resultado esperado:** archivos en `sentinel_logs/recordings/`.

---

## BLOQUE D — Librería Arduino

### D.1 Instalar librería

```bash
make_arduino_zip.bat
```

**Resultado esperado:** `Sentinel.zip` generado.

Luego en Arduino IDE: `Sketch > Include Library > Add .ZIP Library...` → seleccionar `Sentinel.zip`.

### D.2 Demo completo — flashear y verificar

1. Abrir `sentinel_arduino/examples/Demo/Demo.ino` en Arduino IDE
2. Asegurarse que el daemon está corriendo
3. En la pantalla: seleccionar el COM de la placa Arduino (en Split 2x, T1 por ejemplo)
4. Flashear la placa Arduino
5. Observar la pantalla durante ~40 segundos

**Resultado esperado en pantalla (T1):**
- Mensajes de colores (cyan/azul/ámbar/rojo) al inicio
- Barras de progreso `CSV` y `JSON` avanzando simultáneamente
- Al completar: `CSV cerrado` y `JSONL cerrado`
- Tabla final con rutas de archivos

**Resultado esperado en disco:**
```
sentinel_logs/csv/sensores.csv        ← 100 filas, 4 columnas
sentinel_logs/json/experimento.jsonl  ← 50 entradas JSON
```

### D.3 Protocolo directo sin librería (verificar compatibilidad)

En cualquier Arduino conectado al COM seleccionado:

```cpp
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("CSV>test:25.3,60.1");  // fila CSV
  Serial.println("CSV!test");             // cerrar
  Serial.println("MET>temp:25.3:C");     // métrica
}
```

**Resultado esperado:** `sentinel_logs/csv/test.csv` con 1 fila y métrica visible en pantalla.

---

## BLOQUE E — Audio streaming

> **Prerequisito:** firmware flasheado (incluye `AudioTask` y soporte `0xD5`).
> **Importante:** cerrar `sentinel_daemon.py` antes de usar `audio_stream.py` — ambos necesitan el mismo COM.

### E.1 Listar dispositivos de audio disponibles

```bash
python audio_stream.py --list-devices
```

**Resultado esperado:** tabla con índice, nombre, canales in/out, Hz. El output por defecto marcado con `←`.

### E.2 Streaming con WASAPI loopback (captura audio del sistema)

Reproducir cualquier audio en Windows (Spotify, YouTube, etc.) y luego:

```bash
python audio_stream.py
```

**Resultado esperado:**
```
  [OK] Puerto COM8 abierto (sin reset al ESP32)
  [OK] Audio: Altavoces (Realtek High Definition Audio)
       Modo: WASAPI loopback (audio del sistema)
       48000 Hz 2ch → remuestreo a 16000 Hz mono
       Chunk entrada: 1536 frames  |  salida: 512 samples  |  32 ms

  Transmitiendo... (Ctrl+C para detener)

  Chunks:    312  |   9.8 KB/s  |  Cola:  0  |  10s
```

- **Cola cerca de 0**: envío al día con captura. Si está en 16: algo bloquea el serial.
- **Audio audible en la pantalla** a los 1-2 segundos de iniciar.

**Prueba de fallo — reset del ESP32:**
Si la pantalla se pone negra al abrir el puerto, el DTR no se configuró antes del `open()`. Verificar en `connect()` que el orden es: `self._ser = serial.Serial()` → `self._ser.dtr = False` → `self._ser.rts = False` → `self._ser.open()`.

**Prueba de fallo — WASAPI no disponible:**
Si el output dice `input directo (micrófono)` en vez de `WASAPI loopback`: la API WASAPI no está disponible en este sistema. Opciones:
```bash
# Forzar dispositivo específico:
python audio_stream.py --list-devices
python audio_stream.py --device <índice_del_output>
```
O habilitar "Mezcla Estéreo" en Panel de Control → Sonido → Grabación → clic derecho → Mostrar deshabilitados.

### E.3 Calidad del audio

Métricas de referencia:
- **Cola consistentemente 0**: buffer al día, sin pérdida de chunks
- **>9 KB/s**: throughput correcto para 16kHz mono (teórico: ~32 KB/s incluyendo header)
- **Sin crackling notable**: el AudioTask envía silencio en underrun, no zeros aleatorios

---

## BLOQUE F — REST API y WebSocket

### F.1 Status del daemon

```bash
curl http://localhost:8080/api/status
```

**Resultado esperado:**
```json
{"status": "running", "hmi_connected": true, "hmi_port": "COM8", ...}
```

### F.2 Listar terminales

```bash
curl http://localhost:8080/api/terminals
```

### F.3 Enviar log via REST

```bash
curl -X POST http://localhost:8080/api/send -H "Content-Type: application/json" -d "{\"message\": \"Log via REST\", \"terminal\": 0}"
```

### F.4 WebSocket streaming

Guardar como `test_ws.py`:
```python
import asyncio, websockets, json

async def test():
    async with websockets.connect('ws://localhost:8081/stream') as ws:
        print('Conectado. Esperando...')
        msg = await asyncio.wait_for(ws.recv(), timeout=15)
        print(f'Recibido: {json.loads(msg)}')

asyncio.run(test())
```

En terminal 1:
```bash
python test_ws.py
```
En terminal 2:
```bash
python -c "from sentinel import logger; logger.info('Test WS')"
```

---

## BLOQUE G — Instalación

### G.1 Instalador completo

```bash
# Doble-click o desde terminal (sin admin):
install.bat
```

**Resultado esperado:** pip instala deps → instala sentinel → build exe → pregunta autostart.

### G.2 Generar ZIP de librería Arduino

```bash
make_arduino_zip.bat
```

**Resultado esperado:** `Sentinel.zip` en la raíz del proyecto.

### G.3 Verificar librería Python instalada

```bash
pip show sentinel
```

**Resultado esperado:**
```
Name: sentinel
Version: 2.0.0
```

---

## Referencia rápida de problemas comunes

| Síntoma | Causa probable | Solución |
|---|---|---|
| PSRAM: 192KB en lugar de 8MB | Flash Mode no es OPI | Cambiar a `QIO OPI` en Arduino IDE |
| Pantalla negra al abrir audio_stream.py | DTR sube al abrir el puerto → CH340 resetea ESP32 | Verificar que `dtr=False` se configura ANTES de `open()` |
| Audio con crackling severo | Underrun en el buffer de I2S (cola llena, firmware lento) | Verificar que la pantalla no crasheó; el AudioTask escribe silencio en underrun |
| Cola siempre en 16 en audio_stream.py | El firmware dejó de consumir (crash o daemon interfiriendo) | Cerrar el daemon, verificar que el firmware esté flasheado con soporte 0xD5 |
| WASAPI loopback dice "micrófono" | No se detectó output device loopback | Usar `--device <idx>` con el índice del output device de `--list-devices` |
| Puerto Bluetooth sigue en pantalla | VID de su chipset no está en blacklist ni en whitelist | El `_is_dev_board` lo debería excluir si VID es None o descripción tiene "bluetooth" |
| Puerto real no aparece en pantalla | VID del chip USB-Serial no está en `_DEV_BOARD_VIDS` | Agregar el VID al set en `sentinel_daemon.py` línea ~37 |
| `[ERR] Fallo reserva PSRAM` al boot | Buffer LVGL no cabe en PSRAM | Normal si PSRAM < 2MB; el firmware cae al buffer interno automáticamente |
| Pantalla blanca permanente al boot | Imágenes cargadas después de `ui_init()` | `_load()` va ANTES de `ui_init()` en `setup()` |
| UI lenta / trabada | WiFi stack activo sin conexión | Verificar `WiFi.disconnect(true)` en el else del timeout |
| Crash al salir de 2x/4x | FADE_ON en Serial→Dashboard | Verificar `LV_SCR_LOAD_ANIM_NONE` en `my_dash_btn_event` |
| Modal picker sin scroll | `scroll_area` no tiene flex_grow o CLICKABLE cleared | Verificar `lv_obj_set_flex_grow(scroll_area, 1)` y `LV_OBJ_FLAG_SCROLLABLE` |
| Scroll del terminal no funciona | `LV_OBJ_FLAG_CLICKABLE` cleared en `terminalArea` o label absorbe táctil | `add_flag(terminalArea, CLICKABLE)` + `clear_flag(terminalText, CLICKABLE)` |
| `ConnectionRefusedError` al usar logger | Daemon no está corriendo | `python sentinel_daemon.py` |
| `ModuleNotFoundError: sentinel` | Librería no instalada | `pip install .` desde raíz del repo |
| Weather API 401 | Clave OWM nueva no activada aún | Esperar 1-3h desde creación |
| `SentinelFile.write()` no genera CSV | No se llamó `define_csv()` / `open()` con headers | Usar `logger.open("file.csv", ["col1","col2"])` |
| Arduino: datos en pantalla pero sin CSV | `CSV!` no se envió → daemon no cerró el file handle | Llamar `data.close()` o `sentinel.stop()` |
