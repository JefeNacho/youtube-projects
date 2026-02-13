# Comunicación MQTT con ESP32

Este proyecto demuestra cómo implementar una comunicación básica MQTT utilizando un ESP32. El dispositivo se conecta a un broker MQTT público, se suscribe a un tema para recibir comandos y publica datos de sensores en otro tema.

## Funcionalidades

- **Conexión WiFi:** Se conecta a una red WiFi definida.
- **Cliente MQTT:** Utiliza la librería `PubSubClient` para conectarse al broker `broker.emqx.io`.
- **Suscripción (Entrada):** Escucha el tema `Entrada/01` para controlar un LED conectado al pin 26.
  - Enviar `0` apaga el LED.
  - Enviar `1` enciende el LED.
- **Publicación (Salida):** Lee el valor de un fotorresistor (LDR) conectado al pin 33 y publica el valor en el tema `Salida/01` cada 5 segundos.

## Requisitos

### Hardware

- Módulo de desarrollo ESP32.
- LED.
- Resistencia para el LED (220Ω o 330Ω).
- Fotorresistor (LDR).
- Resistencia de pull-down o pull-up para el LDR (10kΩ).
- Cables puente (Jumpers).
- Protoboard.

### Software

- Arduino IDE con el soporte para tarjetas ESP32 instalado.
- Librería `PubSubClient` instalada en Arduino IDE.

## Configuración y Uso

1. Abre el archivo `Prueba_MQTT.ino` en Arduino IDE.
2. Edita las credenciales WiFi:
   ```cpp
   const char* ssid     = "Nombre WiFi";
   const char* password = "Contraseña";
   ```
3. Verifica los pines definidos:
   ```cpp
   int ledpin= 26;
   int fotopin=33;
   ```
4. Carga el código en tu ESP32.
5. Abre el Monitor Serie (115200 baudios) para verificar la conexión.
6. Utiliza un cliente MQTT (como MQTT.fx o una app móvil) para interactuar:
   - Publica `1` o `0` en `Entrada/01` para controlar el LED.
   - Suscríbete a `Salida/01` para ver los valores del fotorresistor.

## Video Tutorial

Puedes ver el tutorial completo en YouTube: [https://youtu.be/x5GML1FqcTQ?si=v4PxW8fVUTJBntMr](https://youtu.be/x5GML1FqcTQ?si=v4PxW8fVUTJBntMr)
