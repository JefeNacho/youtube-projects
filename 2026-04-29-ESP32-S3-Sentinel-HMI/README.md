# Sentinel HMI 🚀

**El panel físico de monitoreo que todo desarrollador necesita.**

¿Cansado de llenar tu pantalla principal con terminales de logs, gráficas de CPU o ventanas de debugging? **Sentinel HMI** es un monitor físico que vive fuera de tu PC. Utiliza una pantalla táctil CrowPanel de 4.3\" potenciada por un ESP32-S3 para mostrarte en tiempo real lo que está pasando en tu sistema y en tus scripts.

![Monitor Serial](images/Monitor_Serial.png)

---

## 📺 Video Tutorial
Si prefieres ver el proceso paso a paso, aquí tienes el video completo del proyecto:
[![Ver Video](https://img.youtube.com/vi/xG3ZTKhvTWI/0.jpg)](https://youtu.be/xG3ZTKhvTWI)

---

## 🚀 Guía de Inicio Rápido

Para que el proyecto funcione, solo necesitas seguir estos dos pasos:

### 1. Flashear la Pantalla (HMI)
Entra en la carpeta `sentinel_hmi/` y abre el archivo `sentinel_hmi.ino` con tu Arduino IDE. Asegúrate de configurar la placa como **ESP32S3 Dev Module** con **OPI PSRAM** habilitado.

### 2. Instalar el Daemon en tu PC
En la raíz de esta carpeta, ejecuta el archivo:
```bash
install.bat
```
Este script preparará todo el entorno de Python, las librerías necesarias y te preguntará si quieres activar el arranque automático con Windows.

---

## 📂 Estructura Organizada

Para que no te pierdas entre tantos archivos, he organizado el proyecto así:

*   **`sentinel_hmi/`**: Código fuente para la pantalla (Arduino).
*   **`sentinel_daemon.py`**: El motor que corre en tu PC (Python).
*   **`sentinel_arduino/`**: Librería para usar en tus otros proyectos de Arduino.
*   **`examples/`**: Scripts de ejemplo para probar el monitor.
*   **`docs/`**: Guías detalladas, esquemáticos y manuales de usuario.
*   **`tools/`**: Scripts avanzados (compilación, reconexión de puertos, utilidades).

---

## 🐍 Uso Rápido desde Python

```python
from sentinel import logger

logger.info(\"¡Sentinel HMI conectado!\")
logger.metric(\"CPU Temp\", 42.5, \"°C\")
```

---

## 📄 Licencia y Créditos
Proyecto creado por **Ignacio Aguilera**.
Si te sirvió, ¡no olvides compartirlo y etiquetarme!

---
*Para más detalles técnicos, revisa la carpeta [docs/](docs/)*
