# 📚 Glosario (sin complicaciones)

Si una palabra técnica del proyecto no se entiende, búscala aquí. Están explicadas como se las explicaría a un amigo.

| Palabra | ¿Qué significa? |
| :--- | :--- |
| **Wake word** (palabra de activación) | La palabra que "despierta" al aparato, como "Alexa" o "Hey Siri". El aparato oye todo, pero solo reacciona con esa palabra. |
| **ESP32-S3** | Una computadora diminuta (microcontrolador) con Wi-Fi y Bluetooth. Es el "cerebro" del proyecto. |
| **Firmware** | El programa que se guarda dentro del ESP32-S3. Cuando le das *Upload*, lo estás "flasheando". |
| **PlatformIO** | Una extensión de VS Code que compila el programa y lo sube a la placa con un clic. |
| **Compilar** | Convertir el código que escribimos (texto) en el programa que entiende la placa. |
| **Monitor serial** | Una ventana en tu computadora donde la placa "te habla" (mensajes) y donde tú le "escribes" (comandos como `c` o `e`). |
| **Baudios (baud rate)** | La velocidad de la conversación entre la placa y la computadora. Los dos lados deben usar la **misma**, si no verás basura. En este proyecto casi siempre es **115200**. |
| **COM (puerto)** | El nombre que Windows le da al puerto USB de tu placa (`COM3`, `COM6`…). |
| **INMP441** | El micrófono digital que usamos. Es muy pequeño y entrega el sonido ya convertido en números. |
| **I2S** | La forma en que el micrófono le manda el audio a la placa, con 3 cables: `SD` (los datos), `SCK` (el reloj) y `WS` (indica si es el canal izquierdo o el derecho). |
| **Canal L/R** | Con dos micrófonos en los mismos cables, uno se declara "izquierdo" (pin `L/R` a GND) y el otro "derecho" (pin `L/R` a 3.3V). Así se turnan para hablar sin chocar. |
| **WS2812 / NeoPixel** | Los LEDs de colores del aro. Se controlan con un solo cable de datos (`DIN`). |
| **16 kHz** | Se toman 16,000 "fotos" del sonido cada segundo. Es suficiente para entender la voz. |
| **Muestra (sample)** | Cada una de esas "fotos" del sonido: un número que dice qué tan fuerte estaba el aire en ese instante. |
| **RMS** | Un número que resume "qué tan fuerte suena" un pedacito de audio. Más volumen = RMS más grande. |
| **VAD** | *Voice Activity Detection*: detectar si en este momento alguien está hablando (y no solo hay silencio o ruido). |
| **MFCC** | La "huella digital" del sonido: convierte el audio en una tabla pequeña de números que describe **cómo suena** una palabra (grave, aguda, qué letras tiene…). Es lo que se compara. |
| **DTW** | *Dynamic Time Warping*. Compara dos huellas aunque hayas hablado más rápido o más lento. Entre más chica la distancia, más parecidas son. |
| **Enrolar** | Registrar tu voz: decir la palabra 3 veces para que la placa guarde tu huella. |
| **NVS / Flash** | La memoria de la placa que **no se borra** al apagarla. Ahí se guarda tu huella de voz. |
| **Red neuronal** | Un programa que **aprende con ejemplos** en vez de seguir reglas. Le enseñas cientos de grabaciones y aprende a distinguir "Hey Bot" de lo demás. |
| **TinyML** | Machine Learning (aprendizaje automático) en versión miniatura, para que corra en un chip pequeño. |
| **Edge Impulse** | Un sitio web donde subes tus grabaciones, entrenas la red neuronal y la descargas como una librería. |
| **Dataset** | Tu colección de grabaciones para entrenar. |
| **Falso positivo** | Cuando la placa **cree** que dijiste la palabra, pero no la dijiste (por ejemplo, con un golpe o una palabra parecida). |
| **INT8 / cuantizar** | Guardar los números de la red neuronal con menos precisión (8 bits) para que ocupe cuatro veces menos espacio y corra más rápido. |
| **PSRAM** | Memoria extra, externa al chip. El Método 3 la necesita porque el modelo y el procesamiento de audio no caben en la memoria interna. |
| **ESP-SR** | La librería de Espressif (el fabricante del ESP32) para reconocimiento de voz. |
| **AFE** | *Audio Front-End*: la etapa de ESP-SR que "limpia" el audio de los dos micrófonos antes de buscar la palabra. |
| **WakeNet** | El modelo de ESP-SR que busca la palabra de activación en el audio ya limpio. |
| **ESP-IDF** | El kit de desarrollo oficial de Espressif. El Método 3 se compila con él. |
| **Núcleo (Core)** | El ESP32-S3 tiene dos "cerebros" (Core 0 y Core 1). En el Método 3, el Core 1 se dedica a la voz y el Core 0 queda libre para tu aplicación. |
