# 🤖 Usa una IA como ayudante (Claude Code, Gemini CLI, Codex, ChatGPT…)

No tienes que hacerlo todo solo. Una IA puede **leer este proyecto, integrar tu propio modelo, cambiar la palabra, ajustar números y ayudarte a entender un error**. Tú sigues mandando: la IA propone y hace los cambios, y **tú pruebas con la placa**.

> 🧭 **Regla de oro:** la IA no puede ver tu placa ni tus cables. Cuando algo falle, **pégale la salida del monitor serial** y cuéntale cómo está conectado. Con eso puede ayudarte de verdad.

---

## 1️⃣ Elige cómo vas a trabajar

| Modo | Herramientas | Ventaja | Cuándo usarlo |
| :--- | :--- | :--- | :--- |
| **A · Agente en la carpeta** *(recomendado)* | **Claude Code**, **Gemini CLI**, **Codex CLI**, **Cursor**, **GitHub Copilot (modo agente)** | Lee todos los archivos, **edita el código y compila por ti**. Este proyecto trae un archivo [`AGENTS.md`](../AGENTS.md) que ellos leen solos y que ya les explica el proyecto | Integrar tu modelo, cambiar la palabra, ajustar valores, corregir errores de compilación |
| **B · Chat normal** | Claude.ai, ChatGPT, Gemini (web o app) | No instalas nada | Entender conceptos, interpretar el monitor serial, revisar tu cableado |

Puedes combinarlos: el agente hace los cambios y el chat te explica lo que no entiendes.

---

## 2️⃣ Modo A: con un agente en la carpeta

### Preparación (una sola vez)

1. Descarga o clona el proyecto en una ruta **sin espacios ni acentos** (por ejemplo `C:\proyectos\`). Instala lo de la [instalación del README](../README.md#-instalación-una-sola-vez) para poder compilar.
2. Instala **una** de estas herramientas (los comandos pueden cambiar con el tiempo; la página oficial de cada una tiene siempre el paso actual):

   | Herramienta | Instalación (referencia) | Para empezar |
   | :--- | :--- | :--- |
   | **Claude Code** | [docs.claude.com/claude-code](https://docs.claude.com/en/docs/claude-code/overview) | `claude` |
   | **Gemini CLI** | `npm install -g @google/gemini-cli` | `gemini` |
   | **Codex CLI** (OpenAI) | `npm install -g @openai/codex` | `codex` |
   | **Cursor** / **VS Code + Copilot** | Se instalan como un editor / extensión | Abre la carpeta y usa el chat en **modo agente** |

3. Abre una terminal **dentro de la carpeta del proyecto** (`2026-10-24-ESP32-S3-Wake-Word`) y escribe el comando de la tabla. El agente leerá `AGENTS.md` solo (Claude Code y Gemini CLI usan `CLAUDE.md` / `GEMINI.md`, que apuntan al mismo archivo).

> 💡 Si el agente te pide permiso para ejecutar comandos o editar archivos, **lee qué quiere hacer** antes de aceptar. Puedes preguntarle "¿qué hace este comando?".

### Primer mensaje recomendado

Pégalo para que la IA se ubique y te explique el proyecto:

```text
Soy principiante. Lee AGENTS.md y el README.md de este proyecto y explícame en palabras
sencillas: qué hace cada uno de los 3 métodos y cuál me conviene para empezar.
Mi placa es: [ESP32-S3 N16R8 / otra: ______].
No cambies ningún archivo todavía.
```

---

## 3️⃣ Prompts listos para copiar

Cambia lo que está entre `[corchetes]` y pega el mensaje. Si prefieres el modo chat, adjunta los archivos que menciona el prompt (o pega su contenido).

### 🧠 Integrar **mi propio modelo** de Edge Impulse (Método 2)

Antes: entrena tu modelo ([guía](../metodo_2_edge_impulse/README.md#-camino-b--entrenar-tu-propio-modelo)) y en Edge Impulse Studio ▸ **Deployment** descarga la **Arduino library** (EON Compiler activado, versión *Quantized int8*). Descomprime el `.zip` en cualquier carpeta.

```text
Quiero usar MI modelo de Edge Impulse en el Método 2 (carpeta metodo_2_edge_impulse).
Descomprimí mi librería exportada (Arduino library) en: [RUTA DE LA CARPETA DESCOMPRIMIDA].
Mi palabra de activación se llama [NOMBRE DE TU CLASE, por ejemplo hola_robot] en el modelo.

Por favor:
1. Reemplaza el contenido de lib/wake_word_inferencing con mi librería. Si el nombre del
   archivo .h cambió, ajusta el #include en src/main.cpp.
2. Lee las etiquetas de mi modelo y pon en include/config.h el WAKEWORD_LABEL correcto.
3. Conserva las banderas de platformio.ini (sobre todo ESP_NN=0 y USB_CDC_ON_BOOT=0).
4. Compila con pio run y dime si hubo errores.
5. Explícame en palabras sencillas qué cambiaste y qué debo ver en el monitor serial.
```

### 🗣️ Cambiar la palabra del Método 3

```text
En metodo_3_esp_sr quiero que la palabra de activación sea [Jarvis / Alexa / Sophia / Computer / Hi ESP].
Cambia sdkconfig.defaults, dime qué carpeta/archivos debo borrar y qué comandos de ESP-IDF
correr, uno por uno. Explícame cómo sé que el cambio funcionó.
```

### 🎚️ Ajustar la sensibilidad

```text
Estoy usando el Método [1 / 2 / 3] y mi problema es: [se activa solo con ruidos /
no me detecta / rechaza mi voz / detecta a otras personas].
Te pego la salida del monitor serial de las últimas pruebas:
[PEGA AQUÍ LA SALIDA]
Dime qué valor cambiar, de cuánto a cuánto, y por qué. Cambia solo UN valor para que yo
pueda probarlo y comparar.
```

### 🔧 Mi código no compila

```text
Al compilar el Método [1 / 2 / 3] me sale este error:
[PEGA AQUÍ EL ERROR COMPLETO]
Mi sistema es [Windows / Mac / Linux] y el proyecto está en: [RUTA].
Explícame qué significa el error y arréglalo. Si necesitas más información, pregúntame
antes de adivinar.
```

### 🔌 No funciona y creo que es el cableado

```text
Mi circuito: ESP32-S3 [modelo], 2 micrófonos INMP441, aro de [16] LEDs WS2812.
Así los conecté: [DESCRIBE CADA CABLE o adjunta una foto clara].
Síntoma: [las barras del comando m están llenas siempre / una barra no se mueve /
el aro no enciende / ...].
Compara mi cableado con la tabla del README principal y dime, paso por paso,
qué revisar primero.
```

### 💡 Que haga algo cuando oiga la palabra

```text
Quiero que cuando se detecte la wake word en el Método [1 / 2 / 3], además de encender el
aro se active [un relevador / un motor / un buzzer] conectado al GPIO [__].
Muéstrame dónde agregar el código, cómo conectarlo de forma segura y qué precauciones
tomar (voltaje, corriente, tierra común). No bloquees la lectura de audio con delay() largos.
```

### 🔄 Usar otra placa u otros pines

```text
Quiero adaptar el proyecto a la placa [NOMBRE EXACTO] con estos pines: [SD=__, SCK=__, WS=__, LEDs=__].
Dime qué archivos cambiar (platformio.ini, config.h, app_config.h…), qué riesgos hay
(PSRAM, tamaño de flash, USB nativo vs UART) y haz los cambios. Compila para confirmar.
```

### 📖 Entender algo

```text
Explícame como si tuviera 15 años qué hace [esta función / este archivo / esta palabra:
MFCC, DTW, I2S, AFE…]. Usa una analogía de la vida diaria y luego enséñame dónde se ve en el código.
```

---

## 4️⃣ Consejos para que la IA te ayude mejor

- ✅ **Dile siempre qué método usas** y qué placa tienes.
- ✅ **Pega el error o la salida completa**, no una parte ni una descripción ("me salió algo en rojo").
- ✅ **Un cambio a la vez**: pide que cambie una cosa, pruébala y luego sigues. Así sabes qué funcionó.
- ✅ **Pregúntale el porqué**: "explícame qué cambiaste". Así aprendes y detectas si se equivocó.
- ✅ **Revisa con los README**: si la IA contradice una tabla de este proyecto, pídele que revise el código y te muestre en qué archivo y línea se basa.
- ⚠️ **La IA puede equivocarse** (inventar un comando, un pin o una cifra). Si algo no cuadra, compila y prueba antes de seguir, o pregúntale "¿cómo puedo comprobarlo?".
- 🔒 **Nunca pegues contraseñas, claves API ni *tokens*** (por ejemplo, los de tu cuenta de Edge Impulse). Si la IA te pide uno, no se lo des.
- 💾 **Haz una copia de la carpeta** antes de pedir cambios grandes, o usa `git` para poder volver atrás.
- 🔬 **Recuerda quién prueba:** la IA puede compilar, pero solo tú puedes comprobar que la placa *suena, escucha y enciende*. Cuéntale lo que viste y oíste.
