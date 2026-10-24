# AGENTS.md — Instrucciones para asistentes de IA (Claude, ChatGPT/Codex, Gemini, Cursor, Copilot…)

Este archivo lo leen automáticamente los agentes de código cuando abres esta carpeta. Si eres una IA, **léelo completo antes de tocar nada**. Si eres una persona, no necesitas leerlo: mira la [guía para usar una IA como ayudante](./docs/GUIA_CON_IA.md).

## Qué es este proyecto

Detección de **wake word** (palabra de activación) 100 % local en un **ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM octal) con **dos micrófonos INMP441** (I2S) y un **aro de 16 LEDs WS2812**. Tiene tres implementaciones independientes, una por carpeta:

| Carpeta | Método | Herramienta de compilación |
| :--- | :--- | :--- |
| `metodo_1_mfcc_dtw/` | MFCC + DTW, enrolas tu voz en la placa (comandos `c`, `e`, `m`, `h`, `d`, `D`, `x`, `l` por el monitor serial) | PlatformIO (Arduino) |
| `metodo_2_edge_impulse/` | Red neuronal INT8 exportada de Edge Impulse (`lib/wake_word_inferencing`); `recolector_audio/` es un programa aparte para grabar el dataset | PlatformIO (Arduino) |
| `metodo_3_esp_sr/` | Espressif ESP-SR (AFE de 2 micrófonos + WakeNet); la palabra se elige en `sdkconfig.defaults` | **ESP-IDF 5.3 (`idf.py`)**, **no** PlatformIO |

Lee el `README.md` de la carpeta del método antes de modificarlo. Documentación general: `README.md`, `docs/SOLUCION_DE_PROBLEMAS.md`, `docs/GLOSARIO.md`.

## Cómo compilar (verifica SIEMPRE que compile después de cambiar algo)

```bash
# Métodos 1 y 2 (y recolector_audio): dentro de la carpeta del método
pio run                  # compilar
pio run -t upload        # subir
pio device monitor -b 115200

# Método 3: desde una terminal "ESP-IDF 5.3" (Windows: ESP-IDF 5.3 PowerShell)
cd metodo_3_esp_sr
idf.py set-target esp32s3    # solo la primera vez, o tras borrar sdkconfig
idf.py build
idf.py -p COMx flash monitor # Ctrl+] para salir
```

- El proyecto debe estar en una **ruta sin espacios ni acentos**.
- La primera compilación es lenta (descargas): es normal.
- Tú (la IA) normalmente **no puedes** probar con la placa: pide a la persona que suba el programa y te pegue la salida del monitor serial.

## Cosas que NO debes cambiar sin un motivo claro (cada una costó mucho depurar)

1. **Cableado compartido por los 3 métodos:** SD=GPIO 11, SCK=GPIO 12, WS=GPIO 13, aro de LEDs DIN=GPIO 17. Micrófono 1 con `L/R` a GND, micrófono 2 con `L/R` a 3.3 V. Un pin `L/R` al aire produce ruido a escala completa y parece un bug de software: **revisa el cableado antes de culpar al código.**
2. **Método 2, `platformio.ini`:** `-DEI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=0` es imprescindible (con ESP-NN la placa daba resultados distintos a Edge Impulse Studio). `-DARDUINO_USB_CDC_ON_BOOT=0` hace que `Serial` salga por el puerto UART (COM) y no por el USB nativo.
3. **Desplazamiento de bits del micrófono:** el INMP441 entrega 24 bits en una palabra de 32. Métodos 1 y 2 y `recolector_audio` usan `>> 16` (`MIC_SAMPLE_SHIFT` en `metodo_2_edge_impulse/include/config.h`); **deben coincidir** para que el audio de entrenamiento suene igual al de inferencia. El Método 3 usa `>> 14` (`MIC_SAMPLE_SHIFT` en `main/app_config.h`) porque así se probó.
4. **Método 3:** requiere PSRAM (`CONFIG_SPIRAM` en `sdkconfig.defaults`). Debe haber **una sola** línea `CONFIG_SR_WN_WN9_*=y` activa. Tras cambiarla hay que borrar `build/` y `sdkconfig` y correr `idf.py set-target esp32s3` otra vez. `dependencies.lock` fija las versiones: no lo actualices sin pedirlo.
5. **Método 1:** el enrolamiento y la calibración se hacen **solo por el monitor serial** (el botón BOOT no se usa). Las huellas y la calibración viven en NVS; la partición NVS es pequeña, así que no aumentes `NUM_ENROLL_TEMPLATES` sin revisar el tamaño.

## Tareas frecuentes

### Integrar el modelo de Edge Impulse de otra persona (Método 2)
1. Pide la librería exportada: Edge Impulse Studio ▸ Deployment ▸ **Arduino library** (EON Compiler activado, versión **Quantized (int8)**) y que la descomprima.
2. Reemplaza **todo** el contenido de `metodo_2_edge_impulse/lib/wake_word_inferencing/` por el de la librería nueva. Si la carpeta o el `.h` se llaman distinto (`<nombre_proyecto>_inferencing.h`), ajusta el `#include <wake_word_inferencing.h>` en `src/main.cpp` y borra el modelo viejo para que no haya dos.
3. Lee las etiquetas en `lib/.../src/model-parameters/model_variables.h` y pon en `include/config.h` el `WAKEWORD_LABEL` con el **nombre exacto** de la clase de la palabra. El resto del código lee las etiquetas del modelo automáticamente y se adapta al tamaño de ventana (`EI_CLASSIFIER_RAW_SAMPLE_COUNT`).
4. Conserva las banderas de `platformio.ini` (ver punto 2 de arriba) y compila con `pio run`.
5. Pide a la persona que suba el programa, diga la palabra y te pegue la salida: cada 500 ms imprime el porcentaje de cada clase.

### Cambiar la palabra del Método 3
Edita `metodo_3_esp_sr/sdkconfig.defaults` (opciones en el README del método), borra `build/` y `sdkconfig`, `idf.py set-target esp32s3`, `idf.py build`. Los nombres válidos son los `CONFIG_SR_WN_*` del componente `espressif/esp-sr` (se ven en su `Kconfig.projbuild`).

### Ajustar sensibilidad
M1: `include/config.h` (`DTW_MAX_DISTANCE`, `VAD_ENERGY_THRESHOLD`). M2: `include/config.h` (`CONFIDENCE_THRESHOLD` y `SUSTAINED_*`). M3: `afe_config->wakenet_mode` en `main/main.c`. Cambia **un valor a la vez** y explica qué esperar.

### Hacer que algo pase al detectar la palabra
Busca en el código el mensaje de detección (`WAKE WORD DETECTADA` / `PALABRA DE ACTIVACION DETECTADA`): ahí se enciende el aro. Añade la acción justo ahí (relevador, motor, etc.) sin bloquear la tarea de audio con `delay()` largos.

## Cómo ayudar a la persona (importante)

- Quien te usa puede ser un **principiante** (incluso de secundaria): responde en **español**, con palabras sencillas, pasos cortos y numerados, y explica el porqué de cada cambio.
- **Pide datos antes de diagnosticar:** qué método, qué placa, la salida completa del monitor serial y cómo está cableado. No adivines.
- Haz **un cambio a la vez** y di exactamente qué probar y qué debería verse.
- No inventes cifras de rendimiento ni funciones que el código no tiene: si no sabes, dilo y propón cómo medirlo.
- Nunca pidas ni pegues contraseñas, claves API o *tokens* (por ejemplo los de Edge Impulse).
- Si algo contradice a un README, revisa el código: el código manda.
