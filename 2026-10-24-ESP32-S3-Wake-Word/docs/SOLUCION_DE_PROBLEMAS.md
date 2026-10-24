# ❓ Solución de problemas

Busca tu síntoma en la tabla. Están ordenados desde lo más común. **Casi siempre el problema es el cableado o el puerto, no el código**, así que revisa eso primero.

---

## 🔌 Conexión con la placa

| Síntoma | Causa probable | Solución |
| :--- | :--- | :--- |
| La placa **no aparece** como puerto COM | El cable solo carga (no transmite datos) o falta el driver | Prueba otro cable USB. Instala el driver del chip **CH343** (o CH340) desde el sitio de su fabricante y reconecta |
| `Upload` falla con *"Failed to connect"* o *"No serial data received"* | Puerto equivocado o la placa no entró en modo de carga | Usa el puerto USB-C rotulado **UART**. Si no sirve: mantén pulsado **BOOT**, pulsa y suelta **RESET**, suelta **BOOT** y vuelve a dar *Upload* |
| El monitor serial **no muestra nada** | Puerto, velocidad o tipo de USB | Velocidad **115200** (el Método 2 `recolector_audio` usa 2000000). Pulsa **RESET**. Si tu placa solo tiene el USB nativo, cambia `-DARDUINO_USB_CDC_ON_BOOT=0` a `=1` en `platformio.ini` |
| El monitor muestra **símbolos raros** | Velocidad (baudios) equivocada | Debe coincidir con `monitor_speed` del `platformio.ini` |
| *"Could not open COMx, the port is busy"* | Otro programa usa el puerto (el monitor serial, el forwarder…) | **Solo un programa a la vez** puede usar el puerto. Cierra el otro monitor |

---

## 🎤 Micrófonos y aro de LEDs

| Síntoma | Causa probable | Solución |
| :--- | :--- | :--- |
| Con el comando `m` (Método 1) las barras están **llenas todo el tiempo** o se escucha estática a volumen máximo | Un pin **`L/R`** está sin conectar, o los dos micrófonos están en el mismo canal | Conecta `L/R` del micrófono 1 a **GND** y el del micrófono 2 a **3.3V**. Los dos nunca al mismo lado |
| Una de las dos barras (`L` o `R`) **no se mueve** | Ese micrófono está mal cableado | Revisa `VDD`, `GND`, `SD`, `SCK`, `WS` y `L/R` de ese micrófono |
| Un micrófono se **calienta** o huele a quemado | Lo conectaste a **5V** | Los INMP441 van a **3.3V**. Cámbialo (probablemente ya se dañó) |
| El **aro de LEDs no enciende** | Sin alimentación, sin tierra común o `DIN` en otro pin | `VCC` a **5V**, `GND` al **GND del ESP32**, `DIN` a **GPIO 17**. En el Método 1 prueba con `l` y `x` |
| Los LEDs prenden de **colores raros** | El aro es de otro tipo (RGBW) o `DIN` está flojo | Revisa el cable de `DIN`; prueba con `x` para ver rojo, verde, azul y blanco |

---

## 💻 Compilación

| Síntoma | Causa probable | Solución |
| :--- | :--- | :--- |
| La **primera compilación tarda muchísimo** | PlatformIO está descargando herramientas y librerías | Es normal (varios minutos). Las siguientes son mucho más rápidas |
| PlatformIO no encuentra el proyecto | Abriste la carpeta equivocada | Abre **la carpeta del método** (por ejemplo `metodo_1_mfcc_dtw`), no la carpeta raíz del repositorio |
| Errores de compilación raros con rutas | La ruta tiene **espacios o acentos** | Mueve el proyecto a una ruta corta, como `C:\proyectos\` |
| El Método 3 no compila | No usaste ESP-IDF | El Método 3 se compila con **ESP-IDF 5.3**, no con PlatformIO. Mira su [README](../metodo_3_esp_sr/README.md) |

---

## 🟠 Método 1 (MFCC + DTW)

| Síntoma | Solución |
| :--- | :--- |
| Dice *"Primero calibra el ruido"* | Escribe `c` y quédate en silencio 10 s |
| *"No se detecto voz; enrolamiento cancelado"* | No hablaste en 10 s, o hablaste muy bajito. Repite con `e`, habla más cerca o baja `VAD_ENERGY_THRESHOLD` |
| Te rechaza a ti | Sube `DTW_MAX_DISTANCE` en `config.h` (mira la `Distancia DTW` que imprime) |
| Te acepta a ti **y a otros** o con otras palabras | Baja `DTW_MAX_DISTANCE` |
| Se activa solo con ruidos | Recalibra con `c` y sube `VAD_ENERGY_THRESHOLD` |

---

## 🔵 Método 2 (Edge Impulse)

| Síntoma | Solución |
| :--- | :--- |
| `npm install` falla con `node-gyp` o `serialport` | Usa **Node.js 20 LTS** (no 24). Si sigue fallando, instala las *Visual Studio Build Tools* (carga de trabajo "Desktop development with C++") y repite |
| `edge-impulse-data-forwarder` no detecta la placa o la reinicia | Sube `recolector_audio` y verifica `-DARDUINO_USB_CDC_ON_BOOT=0`. Cierra cualquier monitor serial abierto |
| Edge Impulse dice que la frecuencia es ~1700 Hz y no 16000 Hz | Faltó el `--baud-rate 2000000` en el comando (los **dos lados** deben usar `2000000`) |
| Las grabaciones salen **muy cortas** o con basura | Mismo caso: baudios distintos entre la placa y el forwarder |
| **Todas** las grabaciones suenan igual (estática) y el Feature Explorer muestra todo mezclado | Revisa el pin `L/R` de cada micrófono (ver arriba). No entrenes con ese audio |
| Error `MFCC failed (-1002)` | Falta memoria. Confirma en el monitor que `psram_size` sea **mayor que 0** (placa N16R8) |
| La placa da resultados distintos a los del Studio | Verifica que `-DEI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=0` siga en `platformio.ini` |
| Con ruido en el cuarto hay falsos positivos | Graba más muestras de `noise` y `wake_word` **en ese cuarto**, reentrena y vuelve a exportar. Como alternativa rápida, sube `CONFIDENCE_THRESHOLD` |

---

## 🟣 Método 3 (ESP-SR)

| Síntoma | Solución |
| :--- | :--- |
| Falla al iniciar mencionando `PSRAM` o `SPIRAM` | Este método **necesita PSRAM**. Usa una placa N16R8 (o similar con PSRAM octal) |
| *"No se encontro ningun modelo WakeNet en la particion 'model'"* | El modelo no se subió. Usa `idf.py -p COMx flash` completo (no solo la aplicación) y revisa que `sdkconfig.defaults` tenga **una** palabra activa |
| Cambié la palabra y sigue la anterior | Después de editar `sdkconfig.defaults`, borra la carpeta `build` y el archivo `sdkconfig`, y vuelve a compilar |
| No detecta tu voz | Habla de frente, a menos de 1 m. Separa los dos micrófonos **40–65 mm** |
| Compilar descarga mucho | La primera vez baja ESP-SR y sus modelos (cientos de MB). Es normal y solo ocurre una vez |
