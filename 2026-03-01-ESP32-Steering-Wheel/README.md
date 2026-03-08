# ESP32 Volante (Steering Wheel) con Panel Elecrow para Euro Truck Simulator 2

Este proyecto contiene el firmware y los scripts necesarios para construir tu propio **volante interactivo** con Force Feedback simulado (Auto-centrado / Filtro anti-vibración) y soporte para telemetría directa usando un **ESP32-C3** y un **Panel Táctil Elecrow Advanced de 4.3"**.

Permite la comunicación bidireccional entre el volante físico y el juego (Euro Truck Simulator 2) a través de un puente programado en Python, emulando un joystick de Windows mediante vJoy.

---

## 🚀 Características Principales

- **Entrada Analógica Suavizada:** Lectura de potenciómetros para la dirección y pedales con filtrado anti-vibración y auto-centrado por software.
- **Control de Crucero Inteligente:** Integrado mediante un algoritmo PID en Python que mantiene la velocidad del camión basándose en la telemetría en tiempo real.
- **Telemetría Bidireccional:** El PC envía datos vitales (Velocidad, RPM, Marcha, Combustible, Daños del camión y luces) hacia la pantalla Elecrow.
- **Interfaz Gráfica Premium:** Dashboard compilado desde SquareLine Studio para el panel táctil Elecrow.
- **Comandos en Tiempo Real:** Botones para marchas (R, N, D), luces, limpiaparabrisas y luces de emergencia.

---

## � Diagrama de Conexiones

El siguiente diagrama muestra la arquitectura física del volante, cómo se conectan los periféricos usando las **PCBs Custom** (`Esp32-c3-middleware` y `pedal-pcb`) al ESP32-C3, y cómo este se comunica con el PC y la pantalla táctil Elecrow.

```mermaid
graph TD
    PC["💻 PC (Euro Truck Simulator 2)"] <-->|USB-C (vJoy / Telemetría)| ESP32["🧠 ESP32-C3 Super Mini<br>(Cerebro Principal)"]

    ESP32 -->|UART (TX: 21, RX: 20)| Pantalla["📺 Panel Elecrow Advanced 4.3\""]

    subgraph "Volante (Esp32-c3-middleware PCB)"
        ESP32
        PIN_STEER["🎛️ Eje Dirección<br>(PIN 3 - Analógico)"] --> ESP32
        PIN_HORN["🔘 Claxon<br>(PIN 4 - PullDown)"] --> ESP32
    end

    subgraph "Pedalera (pedal-pcb PCB)"
        PIN_GAS["⚡ Acelerador<br>(PIN 0 - PullUp)"] -.->|Conector a Middleware| ESP32
        PIN_BRAKE["🛑 Freno<br>(PIN 1 - PullUp)"] -.->|Conector a Middleware| ESP32
    end
```

---

## �📦 Estructura del Repositorio

- **`ets2_real_bridge.py`**: El "cerebro" en PC. Es un script de Python que lee la telemetría del juego, recibe lecturas físicas del ESP32 por USB (Serial), envía pulsaciones virtuales a vJoy, y manda datos de vuelta a la pantalla.
- **`esp32_c3_steering_wheel/`**: Firmware del ESP32-C3 (escrito en PlatformIO/C++ con FreeRTOS). Lee los valores analógicos y de los botones del volante, enviándolos al PC. **Además, actúa como puente físico**: recibe los paquetes de telemetría del PC (Velocidad, RPM, etc.) y se los reenvía por puerto Serial a la pantalla Elecrow para que esta los dibuje.
- **`elecrow_panel_advanced_4.3/`**: Firmware de la pantalla Elecrow encargado de mostrar los indicadores gráficos y botones táctiles.
- **`squareline_project/`**: Código fuente de diseño visual para modificar la interfaz del panel interactivo (requiere SquareLine Studio).
- **`3d_prints/`**: Archivos y modelos 3D necesarios para imprimir la carcasa, los soportes físicos del volante y/o pedales.

---

## 🛠️ Requisitos Previos

### Hardware

- 1x **ESP32-C3 Super Mini** (como controlador principal del volante/pedales).
- 1x **Elecrow Panel Advanced 4.3"** (como cuadro de instrumentos o _dashboard_).
- Potenciómetros (para volante, acelerador, freno) y Pulsadores (para controles).
- Cables USB-C de buena calidad para transferencia de datos.

### Software

- **Python 3.8+** en tu PC.
- **vJoy** (Virtual Joystick Driver para Windows).
- **PlatformIO** (Extensión en VS Code) recomendado para compilar el firmware del ESP32.
- **Euro Truck Simulator 2** (con soporte para plugins de telemetría).

---

## ⚙️ Guía de Instalación Paso a Paso

### Paso 1: Configurar el Joystick Virtual (vJoy)

El script de Python se hace pasar por un control de Xbox o Genérico inyectando datos a **vJoy**.

1. Descarga e instala [vJoy](https://sourceforge.net/projects/vjoystick/).
2. Abre la aplicación "Configure vJoy" en Windows.
3. Asegúrate de habilitar al menos el "Device 1". Configura al menos **3 ejes** (X, Y, Z para Dirección, Acelerador y Freno) y unos **10 botones**.
4. Aplica los cambios.

### Paso 2: Preparar el Entorno en Python

Este script es la pasarela mágica entre tu volante físico y el juego.

1. Abre una terminal en la raíz de este proyecto.
2. Instala las librerías necesarias:
   ```bash
   pip install pyserial pyvjoy
   ```
   _(Nota: Asegúrate de tener los archivos o módulos requeridos de telemetría del camión instalados o disponibles en el PATH, e.g. `truck_telemetry`)_.
3. Abre `ets2_real_bridge.py` y edita la constante `SERIAL_PORT` para que coincida con el puerto COM de tu ESP32-C3 (Ejemplo: `SERIAL_PORT = 'COM10'`).

### Paso 3: Subir código al ESP32-C3 (Volante)

1. Abre la carpeta `esp32_c3_steering_wheel` usando **PlatformIO** (VS Code).
2. Conecta tu ESP32-C3 a la PC.
3. Haz clic en el botón de compilación (Build) y luego en Subir (Upload).

### Paso 4: Subir código a la pantalla Elecrow

1. Abre la carpeta `elecrow_panel_advanced_4.3` con Arduino IDE o PlatformIO.
2. Compila y descarga el firmware asegurándote de usar los parámetros de placa correctos recomendados por Elecrow.
3. (Opcional): Si deseas modificar los gráficos antes, abre `squareline_project/` en **SquareLine Studio**, exporta los archivos de UI y pégalos en las carpetas correspondientes de tu firmware.

---

## 🎮 Uso y Compilación en Juego

Cada vez que quieras jugar, sigue este flujo exacto:

1. **Conectar Periféricos:** Conecta tu ESP32-C3 y la pantalla Elecrow a los USB de tu ordenador.
2. **Ejecutar el Puente en Python:**
   ```bash
   python ets2_real_bridge.py
   ```
3. **Calibración Guiada:** El script de Python detectará tu volante y te pedirá seguir 3 pasos sencillos en la consola:
   - _Paso 1_: Pon el volante en el CENTRO y presiona ENTER.
   - _Paso 2_: Gira al MÁXIMO a la IZQUIERDA y presiona ENTER.
   - _Paso 3_: Gira al MÁXIMO a la DERECHA y presiona ENTER.
4. **Abrir el Juego:** Inicia Euro Truck Simulator 2. Ve a las **Opciones > Controles** y selecciona tu dispositivo **vJoy** como el control principal. Asigna el giro, el acelerador y el freno utilizando los pedales de tu sistema físico.
5. **¡A conducir!**: Mira en tiempo real la velocidad de tu panel Elecrow responder al camión mientras interactúas con los cruceros (Control Cruise Inteligente).

---

## ❓ Solución de Problemas Frecuentes

- **Python lanza `serial.serialutil.SerialException:`**: El puerto COM es incorrecto o está ocupado. Cierra el Monitor Serial de Arduino/PlatformIO y asegúrate de haber puesto el COM correcto en `ets2_real_bridge.py`.
- **vJoy no detecta movimientos**: Asegúrate de haber instalado correctamente vJoy y la librería `pyvjoy`.
- **El volante es muy sensible o vibra**: Ajusta la variable `STEER_SMOOTHING` en el archivo de Python. Un valor cercano a `0.05` es muy suave, mientras que `0.50` reacciona rapidísimo pero puede tener ruido del potenciómetro.
- **El panel Elecrow no muestra la velocidad a pesar de estar el script andando**: Verifica que los puertos RX/TX estén correctamente cableados si el puente los está mandando directo a la pantalla, o que el plugin de Telemetría de ETS2 está corriendo activo.

---

_Hecho para aprender y disfrutar del ecosistema ESP32._
