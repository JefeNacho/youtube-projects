# 🛠️ Guía de Montaje: Antigravity Sidecar (SquareLine Studio)

Esta es la guía definitiva para construir tu interfaz. Sigue los pasos uno por uno. **No uses espacios ni guiones bajos en los nombres.**

---

## ⚙️ Paso 0: Configuración del Proyecto
Al abrir SquareLine Studio, crea un proyecto nuevo con estos valores:
- **Resolution:** 800 x 480
- **Color Depth:** 16-bit
- **LVGL Version:** v8.3.6
- **Board:** Custom (ESP32-S3)

---

## 🖼️ Paso 1: Importar Fotos y Letras (Assets)
1. Ve a la pestaña **Assets**.
2. Arrastra aquí las imágenes: `mty_amanecer`, `mty_dia`, `mty_atardecer`, `mty_noche`.
3. Arrastra el archivo de la letra **Inter-Bold.ttf** e **Inter-Medium.ttf**.
4. En SquareLine, crea 3 fuentes (Fonts):
   - **fontBig:** Inter-Bold, Size 56.
   - **fontMedium:** Inter-SemiBold, Size 16.
   - **fontSmall:** Inter-Medium, Size 14.

> [!IMPORTANT]
> **SIMBOLO DE GRADOS (°):**
> Para que el símbolo `°` no salga como un carácter raro (un cuadro o una X), tienes que añadirlo manualmente al crear la fuente:
> - En la pestaña **Fonts**, cuando estés configurando tu fuente, busca el campo que dice **"Symbols"** o **"Included Characters"**.
> - Escribe ahí el símbolo `°` (copia y pega este: `°`) o el código `0xB0`.
> - Dale a **Generate** o **Apply**. Si no lo haces, SquareLine no "dibuja" ese símbolo y no lo verás en la pantalla.

---

## 🎨 Paso 2: Crear el Estilo "Vidrio" (liquidGlass)

Los estilos son como "plantillas" de diseño. Los creas una vez y los aplicas a muchos botones o cuadros.

### 🔎 ¿Dónde están los Styles?
1. Mira la esquina **inferior derecha** de tu pantalla. Verás una pestaña que dice **Styles**.
2. Haz clic en el botón **`+`** (Plus) para crear uno nuevo. Ponle el nombre: `liquidGlass`.

### ✍️ ¿Cómo lo configuro?
> [!CAUTION]
> No uses la **Opacity** de la primera sección (MAIN), porque pondrá transparente el texto.
1. Ve a la sección **BACKGROUND**.
2. Cambia el **Background Color** a Blanco (`#FFFFFF`).
3. Cambia SOLO el **Background Opacity** a `102` (un 40%).

**Otras propiedades obligatorias:**
- **Border Width:** `2` | **Border Color:** Blanco (`#FFFFFF`).
- **Radius:** `24` (para esquinas redondas).

### 🔗 ¿Cómo se lo pongo a mis cuadros o botones?
Una vez creado el estilo, tienes que "pegárselo" a tus objetos:
1. Haz clic en tu cuadro (ej: `timeTempWidget`) en la pantalla central.
2. Mira arriba a la derecha, en el panel **Inspector** (donde cambias el tamaño y posición).
3. Baja hasta que veas una sección llamada **States / Styles**.
4. Haz clic en **`+ Add State`** y elige **MAIN** (el estado normal).
5. En la lista que aparece, busca y selecciona tu estilo: `liquidGlass`. 
   - *¡Magia! Tu cuadro ahora tiene el borde y fondo de cristal.*

---

## 🏗️ Paso 3: Construir la Pantalla Principal (screenDay)

### 1. El Fondo
- Crea una **Screen** y llámala `screenDay`.
- Crea una **Image** dentro, llámala `bgImg`.
- En `Source`, selecciona `mty_dia.png`.
- Tamaño: `800 x 480`.

### 2. El Cuadro del Reloj (timeTempWidget)
- Crea un **Panel** dentro de la pantalla.
- **Name:** `timeTempWidget`.
- **Styles:** Asígnale el estilo `liquidGlass`.
- **Size:** `Width: 240` | `Height: 140`.
- **Position:** `Align: BOTTOM_RIGHT`. `X: -32` | `Y: -100`.
- **Layout:** `Flex - Vertical`.

### 3. La Hora y Temperatura
- Dentro de `timeTempWidget`, crea un **Label**.
- **Name:** `labelHour`.
- **Text:** `16:08`.
- **Font:** `fontBig`.
- **Alineación:** Centro.

- Crea otro **Label** debajo.
- **Name:** `labelTemp`.
- **Text:** `32°C Monterrey, NL`.
- **Font:** `fontSmall`.
- **Color Opacity:** `200` (un poco traslúcido).

### 4. El Botón (monitorBtn)
- Crea un **Button** debajo del cuadro del reloj.
- **Name:** `monitorBtn`.
- **Size:** `Width: 240` | `Height: 60`.
- **Position:** `Align: BOTTOM_RIGHT`. `X: -32` | `Y: -32`.
- **Styles:** Asígnale el estilo `liquidGlass`.
- **Radius:** `100`.

---

## 📊 Paso 4: Selector de Dispositivos (Dashboard)
Esta pantalla convierte tu Sidecar en una terminal universal. Recibirá la lista viva de placas conectadas y te dejará elegir cuál monitorear.

### 1. Fondo y Estructura
- **Pantalla:** Crea una Screen llamada `screenDashboard`.
- **Imagen de Fondo:** Crea una **Image** (`bgDashImg`) con `mty_atardecer.png`. Tamaño `800 x 480`.

### 2. Creación del Componente (ComCard)
En lugar de dibujar tarjetas manualmente, crearemos un "componente reutilizable" que SquareLine exportará como una función (`ui_comcard_create()`) para multiplicarla por código:

1. **Crea el contenedor base:** En tu pantalla, crea un **Panel** y nómbralo `ComCard`.
2. **Ajustes:** Ponle `Width: 180` | `Height: 180`. En **Style (MAIN)**, aplícale tu estilo `liquidGlass`.
3. **Contenido Interno:** Añade un **Label** dentro del panel y nómbralo `lblComName`. Ponle "COMx", fuente `fontMedium` y alineación `CENTER`.
4. **Convertir a Componente:**
   - Haz clic derecho sobre el Panel principal (`ComCard`) en el árbol de jerarquía (Hierarchy) y selecciona **"Create Component"**.
   - El componente base se guardará en tu "librería" (pestaña inferior **"Components"**). 
   - *Nota:* El panel original que tenías en pantalla NO desaparecerá, sino que se convertirá automáticamente en la **primera instancia** (copia) de ese componente. Puedes borrarlo de la pantalla si solo estabas creando el molde, o dejarlo ahí si lo vas a usar.
5. **Añadir Parámetros (El truco mágico):**
   - Haz doble clic en tu nuevo `ComCard` dentro de la pestaña Components para entrar a editarlo.
   - Selecciona el Label `lblComName`. En el panel **Inspector** (derecha), ve a la propiedad de **Text**.
   - Notarás un pequeño ícono de dos eslabones (una cadena) junto a la palabra Text. Haz clic ahí para vincularlo a un nuevo parámetro. Nómbralo `PortName`.
   - *¡Listo! Al exportar a C, la función te pedirá el texto como argumento para que cada tarjeta diga COM3, COM5, etc., desde Arduino.*


### 3. El Contenedor Flex (deviceContainer)
Este es el "corral" donde aparecerán tus tarjetas dinámicas.
- Vuelve a la pantalla `screenDashboard`.
- Crea un **Panel** y nómbralo `deviceContainer`.
- **Size:** `Width: 700` | `Height: 350`.
- **Position:** `Align: BOTTOM_MID` con `Y: -20`. Esto es vital para dejar todo el espacio superior libre para el título y el botón Volver.
- **Fondo:** Hazlo totalmente transparente (Color Opacity: 0, Border Width: 0).
- **Layout:** Activa **Flex - Wrap** (Row Wrap) y pon `Space evenly` o `Center`. Esto hará que las tarjetas fluyan como un Grid.
- **V-Scroll:** Asegúrate de que tenga el scroll vertical encendido por si tienes más placas conectadas de las que caben.

### 4. Cabecera y Botón de Regreso (dbHeader / btnHome)
Para tener el control de navegación bien anclado arriba y aprovechar el canvas:
- Añade un contenedor transparente `dbHeader` con `Align: TOP_MID`.
- Pon allí tu título "Seleccionar Conexión".
- Añade un **Button** (`btnHome`) dentro del header (o directamente con `Align: TOP_LEFT` en la pantalla).
- **Size:** Pequeño, `Width: 100, Height: 40`.
- **Text/Icono:** "Volver" o un ícono de retorno.
- *(Nota: En `ui_events.c` se configurará para cargar `screenDay`/`screenHUD` al darle clic).*

### 5. Integración en C (Para tu código LVGL)
*(Nota mental: Cuando LVGL procese la pantalla, usaremos la función `ui_ComCard_create(ui_deviceContainer)` dentro de un loop for para generar automáticamente un `ComCard` por cada placa detectada en el JSON del Daemon).*

---

## ⌨️ Paso 5: Terminal Serial (screenMonitor)
Esta es la vista técnica para ver qué está pasando "detrás de cámaras".

### 1. El entorno
- **Pantalla:** `screenMonitor`.
- **Background:** Color Negro (`#050505`).

### 2. Panel de Cabecera (monitorHeader)
- **Panel:** `Width: 800` | `Height: 48`.
- **Position:** `Align: TOP_MID`. `X: 0, Y: 0`.
- **Bg Color:** Gris oscuro (`#1A1A1A`). No uses `liquidGlass` aquí, queremos algo sólido.
- **Layout:** `Flex - Horizontal`, `Justify: Space-between`.
- **Label (monitorTitle):** 
  - **Text:** `PROJ_MONITOR [COM3]`.
  - **Font:** `fontSmall`.
  - **Color:** Verde Neón (`#00FFCC`).
- **Botón de Regreso (btnBackToDash):**
  - **Estructura:** Un pequeño Frame/Button dentro del header.
  - **Estilo:** `liquidGlass` o fondo traslúcido (`#FFFFFF33`).
  - **Texto:** "Dashboard".
  - **Función:** Regresar a la pantalla de selección de COM (`screenDashboard`).

### 3. El Área de Texto (terminalArea)
- **Panel:** `terminalArea`.
- **Size:** `Width: 736` | `Height: 340`.
- **Position:** `Align: CENTER`. `X: 0, Y: 20`.
- **Bg Color:** Negro total (`#000000`) con Opacidad `180`.
- **Border:** `Width: 1` | `Color: #00FFCC`.
- **Scroll:** Asegúrate de que **Scrollable** esté activado (V-scroll).

### 4. Texto de la Terminal (terminalText)
- **Label** dentro de `terminalArea`.
- **Size:** `Width: 700` (Deja margen para el scroll).
- **Text:** `> Daemon: ACTIVE\n> [COMPILER] Waiting...\n> Ready for stream.`
- **Font:** `fontSmall`.
- **Color:** Verde Neón (`#00FFCC`).
- **Align:** `TOP_LEFT`. `X: 10, Y: 10`.

---

## 🚀 Resumen para no olvidar:
- **Nombres:** Siempre `LetraMayúscula` (ej: `myWidget`). NUNCA `mi_cuadro`.
- **Posición:** Todos los cuadros principales van abajo a la derecha (`X: -32, Y: -32`).
- **Grosor Borde:** Siempre `2`.

---

## 🛡️ Paso 6: Sentinel Monitor — Toolbar y Status Bar (screenSerial)

La pantalla `screenSerial` (antes `screenMonitor`) se actualiza con controles profesionales: botones de Pause, Recording, Filtro y Split view, más una barra de estado inferior.

> [!IMPORTANT]
> Todos estos elementos deben crearse en SquareLine Studio. El firmware los referencia por nombre (ej: `ui_pauseBtn`). **NO se crean dinámicamente en código.**

### 1. Actualizar el Header (monitorHeader)

El header existente de 800x48px se extiende para incluir 4 botones a la derecha del título.

**Actualizar el título:**
- Cambia el texto default de `monitorTitle` de `PROJ_MONITOR [COM3]` a `SENTINEL [COM3]`.

**Botones del header** — crear dentro de `monitorHeader`, alineados a la derecha:

#### Botón Pause/Resume (`pauseBtn`)
- **Tipo:** Button
- **Name:** `pauseBtn`
- **Size:** `Width: 52` | `Height: 32`
- **Position:** Dentro de `monitorHeader`. Para alineación manual: `X: 460, Y: 8`
- **Bg Color:** `#1A1A2E` | **Bg Opacity:** 255
- **Border:** `Width: 1` | `Color: #00FFCC`
- **Radius:** `4`
- **Padding:** `2`
- **Label interno (`pauseLbl`):**
  - **Text:** `||` (dos barras, simulando pause — LVGL tiene `LV_SYMBOL_PAUSE` pero en SquareLine usa texto)
  - **Font:** `fontSmall`
  - **Color:** `#00FFCC`
  - **Align:** `CENTER`

#### Botón Recording (`recBtn`)
- **Tipo:** Button
- **Name:** `recBtn`
- **Size:** `Width: 52` | `Height: 32`
- **Position:** `X: 518, Y: 8` (58px después del anterior)
- **Bg Color:** `#1A1A2E` | **Bg Opacity:** 255
- **Border:** `Width: 1` | `Color: #00FFCC`
- **Radius:** `4`
- **Label interno (`recLbl`):**
  - **Text:** `REC`
  - **Font:** `fontSmall`
  - **Color:** `#888888` (gris cuando inactivo — el firmware lo cambia a `#FF0000` cuando graba)
  - **Align:** `CENTER`

#### Botón Filtro (`filterBtn`)
- **Tipo:** Button
- **Name:** `filterBtn`
- **Size:** `Width: 52` | `Height: 32`
- **Position:** `X: 576, Y: 8`
- **Bg Color:** `#1A1A2E` | **Bg Opacity:** 255
- **Border:** `Width: 1` | `Color: #00FFCC`
- **Radius:** `4`
- **Label interno (`filterLbl`):**
  - **Text:** `ALL` (el firmware ciclará: ALL → ERR → WARN → INFO → ALL)
  - **Font:** `fontSmall`
  - **Color:** `#00FFCC`
  - **Align:** `CENTER`

#### Botón Split View (`splitBtn`)
- **Tipo:** Button
- **Name:** `splitBtn`
- **Size:** `Width: 52` | `Height: 32`
- **Position:** `X: 634, Y: 8`
- **Bg Color:** `#1A1A2E` | **Bg Opacity:** 255
- **Border:** `Width: 1` | `Color: #00FFCC`
- **Radius:** `4`
- **Label interno (`splitLbl`):**
  - **Text:** `1x` (el firmware ciclará: 1x → 2x → 4x → 1x)
  - **Font:** `fontSmall`
  - **Color:** `#00FFCC`
  - **Align:** `CENTER`

> [!NOTE]
> **Event callbacks:** No configurar eventos en SquareLine para estos 4 botones. El firmware registra los callbacks en código con `lv_obj_add_event_cb()` después de `ui_init()`, porque las acciones son complejas (toggle states, enviar serial, etc.).

---

### 2. Status Bar inferior (`statusBar`)

Barra de información en la parte inferior de `screenSerial`.

- **Tipo:** Panel
- **Name:** `statusBar`
- **Size:** `Width: 800` | `Height: 24`
- **Position:** `Align: BOTTOM_MID`. `X: 0, Y: 0` (pegado al borde inferior)
- **Bg Color:** `#0A0A14` | **Bg Opacity:** 240
- **Border:** `Width: 0`
- **Radius:** `0`
- **Padding:** `2`
- **Scrollable:** Desactivado

#### Label conteo de líneas (`statusLineCount`)
- **Parent:** `statusBar`
- **Name:** `statusLineCount`
- **Text:** `0 lines`
- **Font:** `fontSmall`
- **Color:** `#888888`
- **Align:** `LEFT_MID` | `X: 8, Y: 0`

#### Label indicador REC (`statusRecIndicator`)
- **Parent:** `statusBar`
- **Name:** `statusRecIndicator`
- **Text:** (vacío — el firmware lo llena cuando está grabando)
- **Font:** `fontSmall`
- **Color:** `#888888`
- **Align:** `CENTER` | `X: 0, Y: 0`
- **Recolor:** Activado (para que el firmware use `#FF0000 ● REC#`)

#### Label tiempo transcurrido (`statusElapsed`)
- **Parent:** `statusBar`
- **Name:** `statusElapsed`
- **Text:** (vacío — el firmware muestra `HH:MM:SS` cuando graba)
- **Font:** `fontSmall`
- **Color:** `#888888`
- **Align:** `RIGHT_MID` | `X: -8, Y: 0`

---

### 3. Ajustar el Área de Terminal (`terminalArea`)

Con la status bar nueva (24px abajo) y los botones en el header (48px arriba), el área de terminal se reduce ligeramente:

- **Size actualizado:** `Width: 736` | `Height: 370`
- **Position actualizada:** `Align: CENTER` | `X: 0, Y: 20`
- **Scrollbar Mode:** `AUTO` (aparece al hacer scroll)
- Verificar que `terminalText` dentro tenga `Width: 700` y `Long Mode: WRAP`
- Verificar que `terminalText` tenga **Recolor: Activado** (para colores LVGL `#RRGGBB texto#`)

---

### 4. Terminales adicionales para Split View (T1, T2, T3)

> [!CAUTION]
> Los terminales 1-3 se crean **dinámicamente en código** porque su existencia y tamaño dependen del modo split activo (1x/2x/4x). NO crearlos en SquareLine. Solo el terminal 0 (`terminalArea`/`terminalText`) se define en SquareLine.

El firmware redimensiona `terminalArea` y crea/destruye paneles y labels adicionales según el modo:
- **1x:** Terminal 0 ocupa todo (736x370)
- **2x:** Dos terminales stacked (736x180 cada uno, separados 6px)
- **4x:** Grid 2x2 (364x180 cada uno)

Los terminales dinámicos copian el estilo visual de `terminalArea`:
- Bg Negro, Opacity 100, Border `#00FFCC` Width 1, Radius 4, Pad 4
- Label con `fontSmall`, Color `#00FFCC`, Recolor activado, Long Mode WRAP

---

### 5. Resumen de nombres para el firmware (Toolbar y Status Bar)

| Nombre SquareLine | Variable en C | Tipo | Proposito |
|---|---|---|---|
| `pauseBtn` | `ui_pauseBtn` | Button | Toggle pause/resume del scroll |
| `pauseLbl` | `ui_pauseLbl` | Label | Texto del boton (||/>/> 12) |
| `recBtn` | `ui_recBtn` | Button | Toggle inicio/detener grabacion |
| `recLbl` | `ui_recLbl` | Label | Texto (REC / STOP) |
| `filterBtn` | `ui_filterBtn` | Button | Ciclar filtro ALL/ERR/WARN/INFO |
| `filterLbl` | `ui_filterLbl` | Label | Texto del filtro activo |
| `splitBtn` | `ui_splitBtn` | Button | Ciclar layout 1x/2x/4x |
| `splitLbl` | `ui_splitLbl` | Label | Texto del modo split activo |
| `statusBar` | `ui_statusBar` | Panel | Barra inferior de estado |
| `statusLineCount` | `ui_statusLineCount` | Label | "127 lines" |
| `statusRecIndicator` | `ui_statusRecIndicator` | Label | "REC" con recolor rojo |
| `statusElapsed` | `ui_statusElapsed` | Label | "00:03:42" |

> [!TIP]
> Cuando exportes desde SquareLine, los nombres como `pauseBtn` se convierten en variables `ui_pauseBtn` en el codigo C generado. El firmware las referencia directamente.

---

> [!TIP]
> **Ahorra tiempo:** Una vez termines la pantalla de `screenDay`, hazle clic derecho y elige **Duplicate**. Luego solo cambia la imagen de fondo por `mty_noche.png` y guarda como `screenNight`.

---

## Inventario Completo de Widgets por Pantalla

Tabla maestra de **todos** los elementos graficos que el firmware referencia. Organizada por pantalla, con estado actual de cada widget.

> [!IMPORTANT]
> **Estado "Fallback"** significa que el firmware crea el widget dinamicamente en codigo si no existe en el export de SquareLine. Funciona, pero el posicionamiento y estilo son aproximados. Para resultado optimo, crear en SquareLine y re-exportar.

### screenNight

| Nombre SquareLine | Variable en C | Tipo | Estado | Notas |
|---|---|---|---|---|
| `screenNight` | `ui_screenNight` | Screen | Exportado | Pantalla nocturna (00:00-06:59) |
| `bgImg` | `ui_bgImg` | Image | Exportado | `mty_noche.png` |
| `timeTempWidget` | `ui_timeTempWidget` | Panel | Exportado | Contenedor reloj+temp |
| `labelHour` | `ui_labelHour` | Label | Exportado | Hora "16:08", fontBig |
| `labelTemp` | `ui_labelTemp` | Label | Exportado | "32C Monterrey, NL", fontSmall |
| `monitorBtn` | `ui_monitorBtn` | Button | Exportado | Navega a screenDashboard |
| `labelMonitor` | `ui_labelMonitor` | Label | Exportado | Texto del boton |

### screenMorning

| Nombre SquareLine | Variable en C | Tipo | Estado | Notas |
|---|---|---|---|---|
| `screenMorning` | `ui_screenMorning` | Screen | Exportado | Pantalla amanecer (07:00-11:59) |
| `bgImg1` | `ui_bgImg1` | Image | Exportado | `mty_amanecer.png` |
| `timeTempWidget1` | `ui_timeTempWidget1` | Panel | Exportado | Contenedor reloj+temp |
| `labelHour1` | `ui_labelHour1` | Label | Exportado | Hora, fontBig |
| `labelTemp1` | `ui_labelTemp1` | Label | Exportado | Temperatura, fontSmall |
| `monitorBtn1` | `ui_monitorBtn1` | Button | Exportado | Navega a screenDashboard |
| `labelMonitor1` | `ui_labelMonitor1` | Label | Exportado | Texto del boton |

### screenDay

| Nombre SquareLine | Variable en C | Tipo | Estado | Notas |
|---|---|---|---|---|
| `screenDay` | `ui_screenDay` | Screen | Exportado | Pantalla dia (12:00-17:59) |
| `bgImg2` | `ui_bgImg2` | Image | Exportado | `mty_dia.png` |
| `timeTempWidget2` | `ui_timeTempWidget2` | Panel | Exportado | Contenedor reloj+temp |
| `labelHour2` | `ui_labelHour2` | Label | Exportado | Hora, fontBig |
| `labelTemp2` | `ui_labelTemp2` | Label | Exportado | Temperatura, fontSmall |
| `monitorBtn2` | `ui_monitorBtn2` | Button | Exportado | Navega a screenDashboard |
| `labelMonitor2` | `ui_labelMonitor2` | Label | Exportado | Texto del boton |

### screenDown (Atardecer)

| Nombre SquareLine | Variable en C | Tipo | Estado | Notas |
|---|---|---|---|---|
| `screenDown` | `ui_screenDown` | Screen | Exportado | Pantalla atardecer (18:00-23:59) |
| `bgImg3` | `ui_bgImg3` | Image | Exportado | `mty_atardecer.png` |
| `timeTempWidget3` | `ui_timeTempWidget3` | Panel | Exportado | Contenedor reloj+temp |
| `labelHour3` | `ui_labelHour3` | Label | Exportado | Hora, fontBig |
| `labelTemp3` | `ui_labelTemp3` | Label | Exportado | Temperatura, fontSmall |
| `monitorBtn3` | `ui_monitorBtn3` | Button | Exportado | Navega a screenDashboard |
| `labelMonitor3` | `ui_labelMonitor3` | Label | Exportado | Texto del boton |

### screenDashboard

| Nombre SquareLine | Variable en C | Tipo | Estado | Notas |
|---|---|---|---|---|
| `screenDashboard` | `ui_screenDashboard` | Screen | Exportado | Selector de puertos COM |
| `bgDashImg` | `ui_bgImg4` | Image | Exportado | `mty_atardecer.png` |
| `comContainer` | `ui_comContainer` | Panel | Exportado | Contenedor Flex para tarjetas COM |
| `lblTitleDash` | `ui_lblTitleDash` | Label | Exportado | Titulo "Seleccionar Conexion" |
| `hubBtn` | `ui_hubBtn` | Button | Exportado | Boton volver al HUD |
| `lblBtn` | `ui_lblBtn` | Label | Exportado | Texto del boton hubBtn |

**Componente reutilizable:**

| Nombre SquareLine | Funcion en C | Tipo | Estado | Notas |
|---|---|---|---|---|
| `comCarg` | `ui_comCarg_create()` | Component | Exportado | Tarjeta de puerto COM, parametro `PortName` |

### screenSerial (Terminal Sentinel)

| Nombre SquareLine | Variable en C | Tipo | Estado | Notas |
|---|---|---|---|---|
| `screenSerial` | `ui_screenSerial` | Screen | Exportado | Pantalla terminal serial |
| `monitorHeader` | `ui_monitorHeader` | Panel | Exportado | Header 800x48, fondo #1A1A1A |
| `monitorTitle` | `ui_monitorTitle` | Label | Exportado | "SENTINEL [COM3]", fontSmall |
| `dashBtn` | `ui_dashBtn` | Button | Exportado | Boton "Dashboard" en header |
| `dashBtn1` | `ui_dashBtn1` | Button | Exportado | Boton alternativo dashboard |
| `terminalArea` | `ui_terminalArea` | Panel | Exportado | Area scrollable del terminal (T0) |
| `terminalText` | `ui_terminalText` | Label | Exportado | Texto del terminal, fontSmall, Recolor ON |
| `pauseBtn` | `ui_pauseBtn` | Button | **FALLBACK** | Toggle pause/resume scroll |
| `pauseLbl` | `ui_pauseLbl` | Label | **FALLBACK** | Texto "||" / ">" |
| `recBtn` | `ui_recBtn` | Button | **FALLBACK** | Toggle grabacion REC/STOP |
| `recLbl` | `ui_recLbl` | Label | **FALLBACK** | Texto "REC", gris->rojo al grabar |
| `filterBtn` | `ui_filterBtn` | Button | **FALLBACK** | Ciclar filtro ALL/ERR/WARN/INFO |
| `filterLbl` | `ui_filterLbl` | Label | **FALLBACK** | Texto del filtro activo |
| `splitBtn` | `ui_splitBtn` | Button | **FALLBACK** | Ciclar split 1x/2x/4x |
| `splitLbl` | `ui_splitLbl` | Label | **FALLBACK** | Texto del modo split |
| `statusBar` | `ui_statusBar` | Panel | **FALLBACK** | Barra inferior 800x24 |
| `statusLineCount` | `ui_statusLineCount` | Label | **FALLBACK** | "127 lines" |
| `statusRecIndicator` | `ui_statusRecIndicator` | Label | **FALLBACK** | "REC" con recolor rojo |
| `statusElapsed` | `ui_statusElapsed` | Label | **FALLBACK** | "00:03:42" |

### Widgets creados dinamicamente (NO crear en SquareLine)

Estos widgets los crea el firmware en runtime segun el contexto:

| Variable | Tipo | Creado cuando | Notas |
|---|---|---|---|
| `terminal_areas[1..3]` | Panel | Split 2x o 4x activo | Copian estilo de terminalArea |
| `terminal_texts[1..3]` | Label | Split 2x o 4x activo | Copian estilo de terminalText |
| Instancias de `comCarg` | Component | Al recibir paquete 0xC5 | Una por puerto COM detectado |

### Fuentes

| Nombre SquareLine | Variable en C | Tipo | Estado |
|---|---|---|---|
| `fontBig` | `ui_font_fontBig` | Font | Exportado | Inter-Bold 56px |
| `fontMedium` | `ui_font_fontMedium` | Font | Exportado | Inter-SemiBold 16px |
| `fontSmall` | `ui_font_fontSmall` | Font | Exportado | Inter-Medium 14px |

### Assets de imagen

| Nombre SquareLine | Variable en C | Loader | Estado |
|---|---|---|---|
| `mty_noche` | `ui_img_mty_noche_png` | `ui_img_mty_noche_png_load()` | Exportado |
| `mty_amanecer` | `ui_img_mty_amanecer_png` | `ui_img_mty_amanecer_png_load()` | Exportado |
| `mty_dia` | `ui_img_mty_dia_png` | `ui_img_mty_dia_png_load()` | Exportado |
| `mty_atardecer` | `ui_img_mty_atardecer_png` | `ui_img_mty_atardecer_png_load()` | Exportado |
| `monitor_bg` | `ui_img_monitor_bg_png` | - | Exportado |
| `monitor_serial` | `ui_img_monitor_serial_png` | - | Exportado |

---

## Checklist de Exportacion

Antes de compilar el firmware despues de re-exportar desde SquareLine:

1. Exportar UI a `Monitor_UIFiles/`
2. Copiar a `sentinel_hmi/src/ui/`
3. Verificar que `ui.h` declara todas las pantallas y variables `extern`
4. Si agregaste los botones de toolbar en SquareLine, verificar que `ui_screenSerial.c` los crea — el firmware detectara los `#ifdef` y dejara de usar fallbacks
5. Si hay imagenes nuevas: ejecutar `downgrade_v9_to_v8.py` si se exporto con LVGL v9
6. Compilar y flashear desde Arduino IDE
