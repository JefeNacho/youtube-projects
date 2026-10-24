# 🧠 Método 2: Red neuronal con Edge Impulse (TinyML)

Aquí **no se programan reglas**: se **entrena una red neuronal** con grabaciones de voz y se sube a la placa. Como aprende de muchos ejemplos, puede reconocer tu palabra dicha por **distintas personas** y distinguirla de otras palabras y del ruido.

Hay **dos caminos**; elige el que te toque:

| | **Camino A · Probar el modelo que ya viene** | **Camino B · Entrenar tu propio modelo** |
| :--- | :--- | :--- |
| Para quién | Quieres ver funcionar el método ya | Quieres **tu** palabra |
| Palabra | **"Hey Bot"** | La que tú quieras |
| Necesitas | Solo VS Code y la placa | Cuenta en [Edge Impulse](https://edgeimpulse.com), Node.js 20 y tiempo para grabar |
| Dificultad | 🟢 Fácil (10 minutos) | 🟡 Tedioso, pero se puede |

---

## 🧠 ¿Cómo funciona? (en palabras sencillas)

```text
 Micrófonos ─► Guarda los últimos 2 s ─► Saca la "huella" (MFCC) ─► Red neuronal ─► ¿qué es?
   (I2S)         (buffer circular)         cada 500 ms                (CNN INT8)      wake_word 96 %
                                                                                       noise     3 %
                                                                                           │
                                                                      si wake_word ≥ 80 % ▼
                                                                                      💡 Aro de LEDs
```

1. Los dos micrófonos se **mezclan en uno solo** (mono) y se guardan los **últimos 2 segundos** de audio.
2. Cada **medio segundo** se le pregunta a la red neuronal: *"¿en estos 2 s dijeron la palabra?"*
3. La red responde con un porcentaje para cada clase: `wake_word`, `adios_bot`, `noise` y `silence`.
4. Si `wake_word` llega a **80 %** (o dos lecturas seguidas altas), se enciende el aro verde y blanco 2 segundos.

La red se "cuantizó a INT8": usa números pequeños de 8 bits en vez de 32, así ocupa cuatro veces menos espacio y corre más rápido en un chip diminuto.

*(¿No entiendes alguna palabra? Mira el [glosario](../docs/GLOSARIO.md).)*

---

## 🟢 Camino A · Probar el modelo que ya viene

El modelo entrenado ya está en [`lib/wake_word_inferencing`](./lib/wake_word_inferencing). Solo súbelo:

1. En VS Code: **Archivo ▸ Abrir carpeta…** y elige **`metodo_2_edge_impulse`** (no la subcarpeta `recolector_audio`).
2. Espera a que PlatformIO se configure y pulsa **→ (Upload)**. La primera compilación tarda varios minutos (compila todo el modelo); las siguientes son mucho más rápidas.
3. Abre el **monitor serial** (icono del enchufe). Verás esto, y una línea nueva cada medio segundo:

```text
ESP32-S3 TinyML Wake Word (Metodo 2: Edge Impulse INT8)
heap_free=... bytes; psram_size=(un número grande, mayor que 0) bytes; psram_free=...
Dos microfonos I2S L/R mezclados a mono. Modelo Edge Impulse wake_word cargado.
sample_rate=16072 Hz; ventana=32144 muestras (2 s); inferencia cada 8036 muestras (500 ms)
mejor=noise (97%) | adios_bot=0% noise=97% silence=2% wake_word=1% | volumen(rms)=25 pico=130
```

4. Di **"Hey Bot"**. Cuando `wake_word` pase del 80 %:

```text
mejor=wake_word (97%) | adios_bot=1% noise=1% silence=1% wake_word=97% | volumen(rms)=1450 pico=9800
>>> WAKE WORD DETECTADA POR RED NEURONAL INT8 <<< (una lectura, confianza=96.80%)
Listo para nueva deteccion.
```

> ✅ **¿Qué es `psram_size`?** Si dice `0`, tu placa no tiene PSRAM (o no está bien configurada) y el modelo puede fallar con el error `MFCC failed (-1002)`. Revisa [problemas](../docs/SOLUCION_DE_PROBLEMAS.md).

---

## 🟡 Camino B · Entrenar tu propio modelo

### Paso 1 · Prepara tu computadora

1. Crea una cuenta en [**studio.edgeimpulse.com**](https://studio.edgeimpulse.com) y un **proyecto nuevo**.
2. Instala **Node.js 20 LTS** desde [nodejs.org](https://nodejs.org/).
   > ⚠️ Usa la versión **20**, no la 24: la herramienta de Edge Impulse no compila en Node 24. Comprueba con `node --version` (debe decir `v20.x`). Si usas varias versiones, `nvm-windows` te permite cambiar fácil (`nvm install 20` y `nvm use 20`).
3. Instala la herramienta de Edge Impulse:
   ```bash
   npm install -g edge-impulse-cli --allow-scripts=serialport,sharp,@serialport/bindings
   ```
   Si falla mencionando `node-gyp`, instala las *Visual Studio Build Tools* con la carga de trabajo **Desktop development with C++** y repite el comando.

### Paso 2 · Sube el programa que graba (`recolector_audio`)

Este programa **no** detecta la palabra: solo manda el audio de tus micrófonos por el cable USB para que Edge Impulse lo grabe.

1. En VS Code abre la carpeta **`metodo_2_edge_impulse/recolector_audio`** y pulsa **→ (Upload)**.
2. Para comprobar que funciona, abre el monitor serial (a **2000000** baudios; ya viene configurado). Debe verse un flujo de números que **cambia mucho cuando hablas**. Ciérralo antes de seguir (el puerto no se puede compartir).

### Paso 3 · Conecta la placa con Edge Impulse

En una terminal:

```bash
edge-impulse-data-forwarder --frequency 16000 --baud-rate 2000000
```

- Te pedirá tu usuario y contraseña de Edge Impulse y a qué proyecto conectarte.
- Cuando pregunte el nombre del eje, escribe exactamente **`audio`**.
- Los **dos números de velocidad** (`16000` y `2000000`) deben ser exactamente esos y coincidir con los del programa; si no, las grabaciones salen cortas o llenas de basura.

### Paso 4 · Graba tu dataset

En Edge Impulse Studio ▸ **Data acquisition**, graba muestras de **2 a 3 segundos** cambiando la etiqueta (**Label**) para cada clase:

| Clase (Label) | Qué grabar | Cantidad recomendada |
| :--- | :--- | :---: |
| `wake_word` | Tu palabra ("Hey Bot"), dicha por ti y por otras personas, a distintas distancias | 60 – 80 |
| `adios_bot` | Frases o palabras **parecidas** a la tuya que **no** deben activar nada | 60 – 80 |
| `noise` | **Ruido real de tu cuarto**: ventilador, música, tele, voces lejanas, golpes | 80 – 120 |
| `silence` | El cuarto en silencio | unas decenas |

**Reglas que me ahorraron dolores de cabeza:**
- Graba el ruido **con estos mismos micrófonos y en el mismo cuarto** donde lo vas a usar, no con el celular.
- No grabes solo silencio: incluye ruido tranquilo, normal y el peor que esperas.
- En las muestras de `wake_word`, deja el ruido natural de fondo y cambia la distancia.
- Pon palabras parecidas en `adios_bot`: una frase que casi suena igual enseña más que una palabra totalmente distinta.
- Haz el **Train/Test split** (Edge Impulse tiene una opción automática) y verifica que *Test* no quede en 0 %.

> 🔎 **Antes de entrenar, escucha una de tus grabaciones.** Si se distingue tu voz del silencio, vas bien. Si todas suenan igual (estática), algo está mal con el audio y no sigas entrenando.

### Paso 5 · Crea y entrena el modelo

1. **Impulse design ▸ Create impulse**:
   - Ventana (*window size*): **2000 ms**.
   - Bloque de procesamiento: **Audio (MFCC)**. Bloque de aprendizaje: **Classification (Keras)**. Guarda.
2. En **MFCC**, usa estos valores y pulsa *Save parameters* y luego **Generate features**:

   | Parámetro | Valor |
   | :--- | :---: |
   | Number of coefficients | `13` |
   | Frame length | `0.032` |
   | Frame stride | `0.016` |
   | Number of filters | `26` |
   | FFT length | `512` |

3. Revisa el **Feature Explorer**: las clases deben verse en grupos razonablemente separados. Si están todas mezcladas como un anillo, vuelve a revisar tu audio.
4. En **Classifier** (red neuronal): ciclos de entrenamiento (*epochs*) entre `50` y `80`, *learning rate* `0.005`, y pulsa **Start training**.
5. Mira la **matriz de confusión**. Lo más importante no es el porcentaje total, sino que `noise`, `adios_bot` y `silence` **casi nunca** se confundan con `wake_word`: para un aparato que escucha todo el día, un falso positivo molesta más que una palabra no detectada.

### Paso 6 · Exporta el modelo y ponlo en el proyecto

1. En Studio ▸ **Deployment**, elige **Arduino library**, activa **Enable EON Compiler** y selecciona la versión **Quantized (int8)**. Pulsa **Build** y descarga el `.zip`.
2. Descomprímelo. Dentro hay una carpeta que se llama como tu proyecto + `_inferencing` (en el mío, `wake_word_inferencing`).
3. **Reemplaza** el contenido de [`lib/wake_word_inferencing`](./lib/wake_word_inferencing) por el de esa carpeta.
4. Si tus clases no se llaman exactamente como las mías, revisa [`include/config.h`](./include/config.h): `WAKEWORD_LABEL` debe ser el nombre de **tu** clase de la palabra (por defecto `"wake_word"`).
5. Sube el programa principal (**→ Upload** en la carpeta `metodo_2_edge_impulse`) y repite los pasos del Camino A.

> ⚠️ Si cambias algo del impulse (por ejemplo el tamaño de la ventana), debes **volver a generar las características, reentrenar y volver a exportar** la librería. El programa se adapta solo al tamaño de ventana del modelo que pongas (aunque el mensaje de arranque diga "2 s").

---

> 🤖 **¿Prefieres que una IA lo haga contigo?** Hay un prompt listo para *"integra mi modelo"* en la [guía con IA](../docs/GUIA_CON_IA.md#-integrar-mi-propio-modelo-de-edge-impulse-método-2).

---

## 🎛️ Ajustes (`include/config.h`)

| Problema | Qué cambiar | Cómo |
| :--- | :--- | :--- |
| Se activa sin que digas nada (falsos positivos) | `CONFIDENCE_THRESHOLD` (ahora `0.80`) | **Súbelo** (0.85, 0.90…) |
| No te detecta cuando hablas desde lejos o bajito | `CONFIDENCE_THRESHOLD` | **Bájalo** poco a poco |
| Quieres cambiar cuántas lecturas seguidas bastan | `SUSTAINED_MIN_PROB` y `SUSTAINED_MEAN_PROB` | La palabra suele aparecer en 3 o 4 lecturas seguidas y su probabilidad se reparte entre ellas; con esto se acepta una detección si **dos lecturas seguidas** son altas aunque ninguna llegue al 80 % |

**Cómo probarlo bien:** deja la placa varios minutos con ruido (ventilador, música, conversación, golpes) **sin decir la palabra** y comprueba que no se encienda. Luego di la palabra varias veces con ese mismo ruido. Elige el umbral con esos datos, no a ojo.

> 💡 **Si en tu cuarto hay mucho ruido** (golpes, voces) y ves falsos positivos, la mejor solución es **grabar más muestras de `noise` y de `wake_word` en ese mismo ambiente** y reentrenar.

---

## 🔧 Lo que debes saber (lecciones aprendidas)

| Detalle | Por qué importa |
| :--- | :--- |
| `-DARDUINO_USB_CDC_ON_BOOT=0` en `platformio.ini` | Hace que los mensajes salgan por el puerto **UART** (COM). Sin esto, el monitor serial no muestra lo que esperas. |
| `-DEI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=0` en `platformio.ini` | Desactiva unas instrucciones aceleradas que en esta placa daban resultados **distintos** a los del Studio (37 % en una toma que Studio calificaba con 100 %). **No lo quites.** |
| `MIC_SAMPLE_SHIFT 16` en `config.h` | El INMP441 manda 24 bits dentro de una palabra de 32; con `>> 16` los pasamos a 16 bits sin desbordes. Es el mismo valor del `recolector_audio` y del Método 1: **el audio que entrenas debe sonar igual al que la placa escucha**. |
| El pin `L/R` de los dos micrófonos | Si uno queda sin conectar, los dos chocan y todo el audio sale como ruido a máximo volumen. Revísalo **primero** si el modelo no clasifica bien. |
| Cambiar banderas de `platformio.ini` | Obliga a recompilar todo (unos 5 minutos). Es normal. |

---

## 📁 ¿Qué hay en esta carpeta?

| Archivo / carpeta | Para qué sirve |
| :--- | :--- |
| `src/main.cpp` | El programa: captura el audio y le pregunta a la red neuronal cada 500 ms |
| `include/config.h` | Pines, umbrales y el nombre de la etiqueta de la palabra |
| `lib/wake_word_inferencing/` | El modelo entrenado (librería exportada de Edge Impulse) |
| `recolector_audio/` | Programa aparte para grabar tu dataset (Camino B) |
| `platformio.ini` | Configuración de la placa para PlatformIO |

---

## ❓ Problemas

Mira [**docs/SOLUCION_DE_PROBLEMAS.md**](../docs/SOLUCION_DE_PROBLEMAS.md).
