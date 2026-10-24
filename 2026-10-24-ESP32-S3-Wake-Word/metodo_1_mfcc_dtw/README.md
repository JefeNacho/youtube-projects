# 🎙️ Método 1: Enrolamiento en vivo (MFCC + DTW)

**El más fácil de los tres.** La placa aprende **tu voz**: tú dices tu palabra 3 veces, ella guarda su "huella" y desde ese momento se enciende el aro de LEDs cada vez que la repites. Todo pasa dentro del ESP32-S3: sin internet, sin computadora entrenando, sin cuentas.

> ⚠️ **Limitaciones (para que no te sorprendas):** solo reconoce a **quien la enroló** y, como solo aprende de 3 ejemplos de tu voz, a veces se activa con sonidos parecidos (hay falsos positivos). Para algo más robusto mira el [Método 2](../metodo_2_edge_impulse) o el [Método 3](../metodo_3_esp_sr).

---

## 🧠 ¿Cómo funciona? (en palabras sencillas)

```text
 Micrófonos ─► ¿Alguien habla? ─► Graba 1.5 s ─► Saca la "huella" ─► Compara con tus 3 huellas ─► ¿Se parecen?
   (I2S)         (VAD)                              (MFCC)                  (DTW)                       │
                                                                                                        ▼
                                                                                                   💡 Aro de LEDs
```

1. **Escucha todo el tiempo.** Si el volumen sube por encima del ruido normal del cuarto, entiende que alguien habla (**VAD**).
2. **Graba 1.5 segundos** de esa voz.
3. **Saca su "huella digital"** (**MFCC**): una tabla pequeña de números que describe cómo suena.
4. **La compara con las 3 huellas que guardaste** (**DTW**), aunque hayas hablado un poco más rápido o más lento.
5. Si la **distancia** es menor al límite (`DTW_MAX_DISTANCE`), ¡es tu palabra! Se enciende el aro en arcoíris.

Tus 3 huellas se guardan en la memoria flash, así que **al reiniciar la placa no tienes que volver a enrolar**.

*(¿No entiendes alguna palabra? Mira el [glosario](../docs/GLOSARIO.md).)*

---

## ✅ Antes de empezar

- Tienes el circuito armado como en el [README principal](../README.md#-conexiones).
- Tienes instalado **VS Code + PlatformIO** (ver [Instalación](../README.md#-instalación-una-sola-vez)).
- La placa está conectada por el puerto USB **UART**.

---

## 🚀 Paso a paso

### Paso 1 · Abre la carpeta correcta
En VS Code: **Archivo ▸ Abrir carpeta…** y elige **`metodo_1_mfcc_dtw`**.
Espera a que PlatformIO termine de configurarse (barra azul de abajo; la primera vez descarga cosas y tarda unos minutos).

### Paso 2 · Sube el programa
En la barra azul de abajo pulsa **→ (Upload)**. Compila y sube el programa a la placa. Debe terminar con **`SUCCESS`** en verde.

> 💡 Si dice que no encuentra la placa: revisa el cable (que transmita datos), el puerto **UART** y la sección de [problemas](../docs/SOLUCION_DE_PROBLEMAS.md).

### Paso 3 · Abre el monitor serial
Pulsa el icono del **enchufe (Serial Monitor)**. Ahí es donde la placa te habla y donde tú le escribes. Si no aparece nada, pulsa el botón **RESET** de la placa. Deberías ver algo así:

```text
======================================
[INFO] Iniciando ESP32-S3 Wake Word (Metodo 1: MFCC + DTW)
======================================
[INFO] DSP Hamming & Banco Mel 26 bandas inicializado.
[INFO] Bus I2S (IO11 SD, IO12 SCK, IO13 WS) a 16kHz configurado.
[INFO] Calibracion requerida: envia c para calibrar durante 10 segundos.
[AVISO] No hay plantilla de voz guardada.
Comandos (escríbelos en el monitor serial y pulsa Enter):
  c  = calibrar el ruido del cuarto (10 s en silencio)  <- primero esto
  e  = enrolar tu palabra (la dices 3 veces)            <- luego esto
  ...
```

> ⌨️ **Cómo "escribirle" a la placa:** haz clic dentro de la terminal del monitor, escribe la letra (por ejemplo `c`) y, si no reacciona, pulsa **Enter**.

### Paso 4 · Prueba tus micrófonos (opcional, pero recomendado)
Escribe **`m`**. Verás dos barras, `L` y `R`, que suben cuando hablas, y un LED azul que va dando vueltas por el aro:

```text
L [########................]  R [######..................]  (RMS 1830 / 1420)
```

Si **las dos barras se mueven al hablar**, tus dos micrófonos y el aro funcionan. Pulsa cualquier tecla para salir.

- Una barra se queda quieta → revisa el cableado de ese micrófono (sobre todo su pin `L/R`).
- Las dos barras están llenas todo el tiempo → un pin `L/R` está al aire o los dos micrófonos están en el mismo canal.

### Paso 5 · Calibra el ruido del cuarto
Escribe **`c`** y **quédate en silencio 10 segundos** (sin hablar ni mover cosas). La placa mide cuánto ruido hay normalmente para saber después qué es "voz" y qué es "silencio".

```text
[INFO] Calibrando ruido ambiente durante 10 segundos; guarda silencio...
[INFO] Calibracion guardada. Ya puedes enrolar con e.
```

La calibración se guarda. Solo repítela si cambias de cuarto o hay mucho más (o menos) ruido que antes.

### Paso 6 · Enrola tu palabra
Escribe **`e`**. El aro se pone **naranja y gira** esperándote. Di tu palabra (por ejemplo **"Hey Bot"**) con claridad. Lo harás **3 veces**; después de cada una, el aro hace un destello de colores:

```text
**************************************
[AVISO] MODO ENROLAMIENTO ACTIVADO
[INFO] Di tu Wake Word ('Hey Bot') claramente 3 veces.
**************************************
[INFO] Esperando que hables... (Di la palabra ya)
[INFO] Muestra 1 de 3 capturada.
[INFO] Esperando que hables... (Di la palabra ya)
[INFO] Muestra 2 de 3 capturada.
[INFO] Esperando que hables... (Di la palabra ya)
[INFO] Muestra 3 de 3 capturada.
[INFO] Plantillas MFCC guardadas en Flash NVS.
[INFO] ¡Enrolamiento completado con exito!
```

> 💡 Dila siempre **parecido**: misma distancia al micrófono, mismo volumen, mismo ritmo. Si pasan 10 segundos sin que hables, el enrolamiento se cancela y solo repites con `e`.
> ¿Quieres otra palabra? Vuelve a escribir `e` y habla: **reemplaza** la anterior.

### Paso 7 · ¡Pruébalo!
Di tu palabra. Si la placa la reconoce, el aro se enciende en **arcoíris** unos 2 segundos:

```text
Distancia DTW = 142.3 (se acepta si es menor o igual a 180.0)
--------------------------------------
[INFO] >>> WAKE WORD DETECTADA (Acceso Concedido) <<<
--------------------------------------
[INFO] Retornando a escucha continua...
```

Y si dices otra cosa (o lo dice otra persona) y no se parece lo suficiente:

```text
Distancia DTW = 410.8 (se acepta si es menor o igual a 180.0)
[AVISO] Rechazado: Huella acustica no coincide con usuario
```

**Pídele a un amigo que diga tu palabra:** lo normal es que sea rechazado, porque este método aprende *tu* voz.

---

## 🎛️ Ajustes (si no te funciona bien)

Todo se ajusta en [`include/config.h`](./include/config.h). Cambia **un número a la vez**, vuelve a subir el programa (**→**) y prueba.

| Problema | Qué cambiar | Cómo |
| :--- | :--- | :--- |
| Te rechaza a **ti** (la distancia sale un poco arriba de 180) | `DTW_MAX_DISTANCE` (ahora `180.0f`) | **Súbelo** de poco en poco (200, 220…). Fíjate en la `Distancia DTW` que imprime la placa cuando dices bien la palabra |
| Acepta a **otras personas** o palabras parecidas | `DTW_MAX_DISTANCE` | **Bájalo** (160, 140…) |
| No detecta cuando hablas **bajito** | `VAD_ENERGY_THRESHOLD` (ahora `162.5f`) | **Bájalo** un poco |
| Se dispara con **ruidos** (golpes, ventilador) | `VAD_ENERGY_THRESHOLD` | **Súbelo** un poco; y recalibra con `c` |

---

## ⌨️ Todos los comandos

| Letra | Qué hace |
| :---: | :--- |
| `c` | Calibra el ruido del cuarto (10 s en silencio) |
| `e` | Enrola tu palabra (3 repeticiones) |
| `m` | Prueba de micrófonos: barras de volumen `L` y `R` |
| `h` | Muestra la ayuda con los comandos |
| `l` | *(avanzado)* Prueba solo el primer LED del aro: rojo, verde, azul, blanco |
| `x` | *(avanzado)* Diagnóstico del hardware: estadísticas de los micrófonos y prueba de colores del aro |
| `d` / `D` | *(avanzado)* Manda por USB el audio crudo de los 2 micrófonos (1.5 s / 20 s) para guardarlo como `.wav` (ver abajo) |

---

## 🔬 Herramienta opcional: escucha lo que oye la placa

¿Tus micrófonos suenan raro y quieres **escucharlos**? La carpeta [`herramientas/`](./herramientas) trae un script que guarda lo que oye cada micrófono como `.wav`:

```bash
pip install -r herramientas/requirements.txt
python herramientas/dump_audio_esp32.py --puerto COM6
```

Cambia `COM6` por tu puerto, **cierra antes el monitor serial** (no pueden usar el puerto los dos a la vez) y habla durante la captura. Los `.wav` quedan en `herramientas/grabaciones_esp32/`.

---

## 📁 ¿Qué hay en esta carpeta?

| Archivo | Para qué sirve |
| :--- | :--- |
| `src/main.cpp` | El programa principal: comandos, calibración, enrolamiento y detección |
| `include/config.h` | **Todos los números que puedes ajustar** (pines, umbrales, tiempos) |
| `include/mfcc_extractor.h` | Convierte audio en la "huella" MFCC |
| `include/dtw_matcher.h` | Compara dos huellas (DTW) |
| `include/log.h` | Escribe mensajes `[INFO]`, `[AVISO]`, `[ERROR]` en el monitor serial |
| `platformio.ini` | Configuración de la placa para PlatformIO |
| `herramientas/` | Script opcional para guardar el audio crudo |

---

## ❓ Problemas

Mira [**docs/SOLUCION_DE_PROBLEMAS.md**](../docs/SOLUCION_DE_PROBLEMAS.md).
