# Comunicación P2P LoRa con ESP32 y RYLR998

En este proyecto encontrarás un sistema de comunicación Punto a Punto (P2P) vía LoRa utilizando dos módulos ESP32 y dos módulos transceptores LoRa RYLR998. El sistema consta de un transmisor que envía datos aleatorios y un receptor que los decodifica y muestra en el monitor serial con información de intensidad de señal (RSSI) y relación señal/ruido (SNR).

## Estructura del Proyecto

- **`rylr998_tx/`**: Firmware para el ESP32 Transmisor.
  - `rylr998_tx.ino`: Configura el módulo con dirección 1 y red 5, genera 3 valores aleatorios y los envía cada 5 segundos a la dirección 2.
- **`rylr998_rx/`**: Firmware para el ESP32 Receptor.
  - `rylr998_rx.ino`: Configura el módulo con dirección 2 y red 5, recibe los paquetes, "parsea" la trama de datos separada por comas y muestra los valores individuales.

## Requisitos Previos

### Hardware

- 2 x Módulos de desarrollo ESP32.
- 2 x Módulos LoRa RYLR998.
- Cables puente (Jumpers).

### Conexiones

La conexión entre el ESP32 y el RYLR998 utiliza el puerto Serial 2:

| ESP32    | RYLR998 |
| -------- | ------- |
| 3.3V     | VDD     |
| GND      | GND     |
| 17 (TX2) | RXD     |
| 16 (RX2) | TXD     |

_Nota: Recuerda que la conexión es cruzada (TX del ESP al RX del módulo y viceversa)._

### Software

- **Arduino IDE** con el soporte para tarjetas ESP32 instalado.

## Instrucciones de Configuración

### 1. Configuración del Transmisor (TX)

1.  Abre `rylr998_tx/rylr998_tx.ino` en el Arduino IDE.
2.  Verifica los pines definidos: `#define RXD2 16`, `#define TXD2 17`.
3.  Conecta el primer ESP32.
4.  Selecciona tu placa ESP32 y el puerto COM correspondiente.
5.  Sube el código.
    - Este dispositivo se configurará con **Address = 1** y **Network ID = 5**.

### 2. Configuración del Receptor (RX)

1.  Abre `rylr998_rx/rylr998_rx.ino` en el Arduino IDE.
2.  Verifica los pines definidos: `#define RXD2 16`, `#define TXD2 17`.
3.  Conecta el segundo ESP32.
4.  Selecciona tu placa ESP32 y el puerto COM correspondiente.
5.  Sube el código.
    - Este dispositivo se configurará con **Address = 2** y **Network ID = 5**.

## Uso

1.  Alimenta ambos dispositivos (TX y RX).
2.  Mantén el ESP32 Receptor conectado al PC para ver los datos.
3.  Abre el **Monitor Serie** en el Arduino IDE para el puerto del Receptor.
4.  Configura la velocidad a **115200 baudios**.
5.  Verás la configuración inicial de los parámetros LoRa.
6.  Una vez recibiendo datos, verás la trama cruda y los valores desglosados:
    - TxId (ID del emisor)
    - Length (Longitud de datos)
    - Value 1, 2, 3 (Datos aleatorios)
    - RSSI (Intensidad de señal)
    - SNR (Relación Señal/Ruido)

## Solución de Problemas

- **No hay comunicación**:
  - Verifica que ambos módulos tengan el mismo `NETWORKID` (5 en este código).
  - Asegúrate de que el TX envía a la dirección del RX (`AT+SEND=2...`) y el RX tiene esa dirección (`AT+ADDRESS=2`).
  - Revisa el cableado TX/RX. A veces están etiquetados confusamente; intenta invertirlos si no funciona (TX del ESP a RX del LoRa).
- **Caracteres extraños**: Asegúrate de que el Monitor Serie esté a 115200 baudios.
- **Error en comandos AT**: Si el módulo no responde a la configuración inicial, verifica que esté bien alimentado y que los pines 16/17 sean correctos para tu placa.
