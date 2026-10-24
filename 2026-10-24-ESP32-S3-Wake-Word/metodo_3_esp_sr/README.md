# 🏭 Método 3: Espressif ESP-SR (AFE + WakeNet)

**El más robusto y el más difícil de configurar.** Usa **ESP-SR**, la librería oficial de reconocimiento de voz de Espressif (los fabricantes del ESP32). No entrenas nada: Espressif ya entrenó modelos para palabras como **"Hi ESP"**, **"Alexa"**, **"Jarvis"** o **"Sophia"**, y tú eliges cuál subir a la placa. En mis pruebas fue el que mejor funcionó y no tuvo falsos positivos.

> ⚠️ **Dos cosas que debes saber antes de empezar:**
> 1. **Necesita PSRAM** (la memoria extra de las placas N16R8). Sin ella no arranca.
> 2. **Solo puedes usar las palabras que trae Espressif.** Una palabra propia requiere pedirla a Espressif a través de su servicio de pago.

---

## 🧠 ¿Cómo funciona? (en palabras sencillas)

```text
 2 micrófonos ─► feed_task ─► AFE (limpia el audio) ─► detect_task ─► WakeNet: ¿es la palabra? ─► 💡 Aro azul
    (I2S)       lee el audio   Audio Front-End          pide el audio     modelo de Espressif
                                                          ya limpio
        Todo esto corre en el NÚCLEO 1  ·  El NÚCLEO 0 queda libre para tu aplicación (Wi-Fi, Bluetooth…)
```

- **AFE (Audio Front-End):** recibe el audio de los **dos micrófonos** y lo prepara (separa la voz del resto y detecta cuándo alguien habla) antes de buscar la palabra. Por eso es el que mejor aguanta el ruido.
- **WakeNet:** la red neuronal de Espressif que busca la palabra en ese audio ya limpio.
- **Dos tareas, un solo núcleo:** `feed_task` alimenta el AFE y `detect_task` revisa el resultado. Las dos viven en el núcleo 1 del ESP32-S3 y dejan el núcleo 0 libre.

*(¿No entiendes alguna palabra? Mira el [glosario](../docs/GLOSARIO.md).)*

---

## 🗣️ Palabras disponibles

Se elige **una** en el archivo [`sdkconfig.defaults`](./sdkconfig.defaults). Por defecto viene **"Hi ESP"**.

| Palabra | Línea que debes activar en `sdkconfig.defaults` |
| :--- | :--- |
| **Hi ESP** *(por defecto)* | `CONFIG_SR_WN_WN9_HIESP=y` |
| **Alexa** | `CONFIG_SR_WN_WN9_ALEXA=y` |
| **Jarvis** | `CONFIG_SR_WN_WN9_JARVIS_TTS=y` |
| **Sophia** | `CONFIG_SR_WN_WN9_SOPHIA_TTS=y` |
| **Computer** | `CONFIG_SR_WN_WN9_COMPUTER_TTS=y` |

> 💡 Los modelos que terminan en `_tts` fueron entrenados con voces generadas por computadora; los que no, con grabaciones de personas. ESP-SR tiene más palabras (y en otros idiomas): la lista completa está en el [repositorio de ESP-SR](https://github.com/espressif/esp-sr).

---

## ✅ Antes de empezar

- Circuito armado como en el [README principal](../README.md#-conexiones), con los **dos micrófonos separados 40–65 mm**.
- Una placa **con PSRAM** (ESP32-S3 N16R8).
- **ESP-IDF 5.3** instalado (ver el paso 1).
- El proyecto en una **ruta corta, sin espacios ni acentos** (por ejemplo `C:\proyectos\`).

---

## 🚀 Paso a paso

Este método **no se compila con PlatformIO**, sino con **ESP-IDF**, la herramienta oficial de Espressif. No te asustes: son solo 3 comandos.

### Paso 1 · Instala ESP-IDF (una sola vez)

1. Descarga el **ESP-IDF Tools Installer** para Windows desde la [página oficial de instalación](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/get-started/windows-setup.html) y elige la versión **5.3.x**. Instala todo lo que sugiere (incluye Python, Git y los drivers).
2. Al terminar tendrás en el menú de Inicio un acceso llamado **"ESP-IDF 5.3 PowerShell"** (o "CMD"). **Siempre que quieras compilar este método, ábrelo.**

### Paso 2 · Ve a la carpeta del método

En la ventana de ESP-IDF que abriste:

```bash
cd C:\proyectos\2026-10-24-ESP32-S3-Wake-Word\metodo_3_esp_sr
```

(Cambia la ruta por donde descomprimiste el proyecto.)

### Paso 3 · Compila

```bash
idf.py set-target esp32s3
idf.py build
```

- `set-target` solo se hace **la primera vez** (le dice que tu chip es un ESP32-S3).
- La primera compilación **descarga ESP-SR y sus modelos (cientos de MB)** y tarda varios minutos. Necesitas internet solo esta vez. Debe terminar con *"Project build complete"*.

### Paso 4 · Súbelo y mira el monitor

Conecta la placa por el puerto **UART** y averigua su puerto (por ejemplo `COM6`, lo ves en el Administrador de dispositivos):

```bash
idf.py -p COM6 flash monitor
```

Esto sube el programa **y el modelo de la palabra** a la placa y abre el monitor serial. Para salir del monitor pulsa **`Ctrl + ]`**. Deberías ver algo así:

```text
========================================
[INFO] ESP32-S3 Wake Word - Metodo 3: ESP-SR (AFE de 2 microfonos + WakeNet)
========================================
[INFO] Bus I2S Estereo (Canal L: Mic 1, Canal R: Mic 2) iniciado.
[INFO] Palabra de activación cargada: wn9_hiesp
[INFO] Audio Front-End (AFE) activo: procesa los 2 microfonos antes de WakeNet.
[INFO] Tareas de Audio ancladas a Core 1. Core 0 disponible para App.
```

### Paso 5 · ¡Pruébalo!

Dile **"Hi ESP"** a la placa (de frente y a menos de un metro). El aro se pone **azul 2 segundos**:

```text
****************************************
[INFO] >>> PALABRA DE ACTIVACION DETECTADA <<<
****************************************
[INFO] Sistema listo para nueva activacion.
```

---

## 🔄 Cambiar la palabra de activación

1. Abre [`sdkconfig.defaults`](./sdkconfig.defaults) y cambia la línea `CONFIG_SR_WN_WN9_HIESP=y` por la de la palabra que quieras (tabla de arriba). **Deja una sola línea activa.**
2. Borra la carpeta `build` y el archivo `sdkconfig` de esta carpeta.
3. Vuelve a compilar y subir:
   ```bash
   idf.py set-target esp32s3
   idf.py build
   idf.py -p COM6 flash monitor
   ```
4. En el monitor debe aparecer `Palabra de activación cargada: wn9_...` con la nueva palabra.

---

## 🎛️ Cosas que puedes ajustar

| Qué | Dónde | Detalle |
| :--- | :--- | :--- |
| Pines, cantidad de LEDs | [`main/app_config.h`](./main/app_config.h) | `PIN_I2S_*`, `PIN_LED_OUT`, `LED_RING_COUNT` |
| Volumen del micrófono | `MIC_SAMPLE_SHIFT` en [`main/app_config.h`](./main/app_config.h) | Ahora `14` (con el que lo probé). Un número **mayor** (16) baja el volumen que le llega al AFE |
| Sensibilidad de la detección | `afe_config->wakenet_mode` en [`main/main.c`](./main/main.c) | Ahora `DET_MODE_2CH_95`: más sensible (detecta más, con algo más de riesgo de falsos positivos). Las alternativas están en la documentación de ESP-SR |
| Color del aro al detectar | `led_strip_set_pixel(led_ring, i, 0, 0, 255)` en `main.c` | Los tres números son rojo, verde y azul (0–255) |

---

## 📁 ¿Qué hay en esta carpeta?

| Archivo | Para qué sirve |
| :--- | :--- |
| `main/main.c` | El programa: inicia micrófonos, AFE, WakeNet y las dos tareas |
| `main/app_config.h` | Pines y números ajustables |
| `main/log_simple.h` | Escribe mensajes `[INFO]`, `[AVISO]`, `[ERROR]` en el monitor serial |
| `main/idf_component.yml` | Lista de componentes que se descargan solos (ESP-SR, etc.) |
| `sdkconfig.defaults` | Configuración: PSRAM, memoria y **la palabra de activación** |
| `partitions.csv` | Cómo se reparte la memoria flash (incluye el espacio del modelo) |
| `dependencies.lock` | Versiones exactas de los componentes, para que a todos les compile igual |

---

## ❓ Problemas

Mira [**docs/SOLUCION_DE_PROBLEMAS.md**](../docs/SOLUCION_DE_PROBLEMAS.md).
