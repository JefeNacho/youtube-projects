# Detección de Rostros con ESP32-CAM, Flask y OpenCV

Este proyecto encontrarás un sistema de detección de rostros utilizando un módulo ESP32-CAM y un servidor Python Flask. El ESP32-CAM captura imágenes y las envía al servidor, el cual las procesa utilizando OpenCV para detectar rostros y devuelve los resultados (rostros detectados y coordenadas) al ESP32.

## Estructura del Proyecto

- **`server_detector.py`**: Servidor Python Flask que recibe las imágenes, realiza la detección de rostros con OpenCV, muestra la transmisión de video y devuelve datos JSON.
- **`esp32_cam_sender/`**: Sketch de Arduino para el ESP32-CAM.
  - `esp32_cam_sender.ino`: Firmware principal que maneja la conexión Wi-Fi, la captura de imágenes y las peticiones HTTP POST.

## Requisitos Previos

### Hardware
- Módulo ESP32-CAM.
- Fuente de alimentación de 5V (o vía USB).

### Software
- **Python 3.x** instalado en tu computadora.
- **Arduino IDE** con el soporte para tarjetas ESP32 instalado.

## Instrucciones de Configuración

### 1. Configuración del Servidor Python

1.  Navega a la carpeta del proyecto.
2.  Instala las librerías de Python requeridas:
    ```bash
    pip install flask opencv-python numpy
    ```
3.  Ejecuta el servidor:
    ```bash
    python server_detector.py
    ```
    *El servidor iniciará en el puerto 5000 y abrirá una ventana mostrando la transmisión de video una vez que se reciban imágenes.*

### 2. Configuración del ESP32-CAM

1.  Abre `esp32_cam_sender/esp32_cam_sender.ino` en el Arduino IDE.
2.  Instala la librería **ArduinoJson** a través del Gestor de Librerías.
3.  Modifica las siguientes líneas en el código con tus credenciales de red e IP del servidor:
    ```cpp
    const char* ssid = "TU_WIFI_SSID";
    const char* password = "TU_WIFI_PASSWORD";
    const char* serverUrl = "http://<IP_DE_TU_PC>:5000/upload"; 
    ```
    *Nota: Reemplaza `<IP_DE_TU_PC>` con la dirección IP local de la computadora que ejecuta el servidor Python (ej. `192.168.1.5`).*
4.  Conecta el ESP32-CAM a tu computadora y selecciona el puerto correcto en Arduino IDE.
5.  Selecciona **AI Thinker ESP32-CAM** como la placa en Arduino IDE.
6.  Sube el código.

## Uso

1.  Inicia primero el servidor Python.
2.  Enciende el ESP32-CAM.
3.  Abre el Monitor Serie en Arduino IDE (velocidad 115200).
4.  El ESP32 comenzará a capturar imágenes y enviarlas al servidor.
5.  La ventana del servidor mostrará la transmisión de video con rectángulos verdes alrededor de los rostros detectados.
6.  El Monitor Serie mostrará la respuesta JSON del servidor, incluyendo el número de rostros detectados y sus coordenadas.

## Solución de Problemas

- **Conexión Rechazada/Timeout**: Asegúrate de que ambos dispositivos estén en la misma red Wi-Fi. Verifica si el firewall de tu computadora está bloqueando el puerto 5000.
- **Fallo al Iniciar Cámara**: Revisa la fuente de alimentación. El ESP32-CAM puede ser sensible a fluctuaciones de energía.
- **No se Detectan Rostros**: Asegúrate de que la iluminación sea buena y la cámara esté enfocada. El servidor rota la imagen 90 grados en sentido antihorario por defecto; ajusta `cv2.rotate` en `server_detector.py` si la orientación de tu cámara es diferente.
