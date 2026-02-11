// Se definen los pines para utilizar el Serial2
#define RXD2 16
#define TXD2 17

// Valores que se transmitirán
int value1, value2, value3;

// Función para mandar comandos AT al módulo LoRa
void sendCmd(String cmd)
{
  Serial2.println(cmd);  // Enviar string cmd a módulo LoRa
  delay(500);  // Esperar 500ms a que el módulo reciba el comando
  while (Serial2.available()) {
    Serial.print(char(Serial2.read()));  // Si hay respuesta imprimirla en el monitor serial 
  }
}

void setup() {
  Serial.begin(115200);  // Iniciar el monitor serial a 115200 baudios
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);  // Iniciar el Serial2 a 115200
  delay(3000);
  Serial.println("Configurando parámetros antena LoRa");
  delay(1000);
  sendCmd("AT+ADDRESS=1");  // Configurar el address a 1
  delay(1000);
  sendCmd("AT+NETWORKID=5");  // Configurar el Network ID a 5
  delay(1000);
  sendCmd("AT+BAND?");  // Leer la frecuencia configurada
  delay(1000);
  sendCmd("AT+PARAMETER?");  // Leer los parámetros configurados
  delay(1000);
  sendCmd("AT+MODE?");  // Leer el modo configurado
  delay(1000);
}

void loop() {
  // Generar los valores que transmitiremos
  value1 = random(-100,100);
  value2 = random(0,4000);
  value3 = random(1,9);

  String data = String(value1) + "," + String(value2) + "," + String(value3);  // Contruir el mensaje que enviaremos en un solo string
  String datalen = String(data.length());  // Obtener el tamaño del mensaje

  Serial.println("Enviando: " + data);
  sendCmd("AT+SEND=2," + datalen + "," + data);  // Construir el comando AT y enviar al módulo LoRa

  delay(5000);
}