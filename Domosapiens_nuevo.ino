#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

const char* ssid = "Taberna de Moe";
const char* password = "demasiadolarga";
const long TIMEOUT = 1000000;
const unsigned long tiempoEntreIntentos = 5;
int numeroVuelta = 0;
unsigned long horaUltimoIntento = 0;
TaskHandle_t t;

/*
   Defino el servidor leyendo en el puerto 1996
*/
WiFiServer serverTCP(1996);
String buffer = "";
String bufferLive = "";
IPAddress ipLuz1 = "192.168.1.94";
IPAddress ipRiego = "192.168.1.98";
IPAddress ipClima = "192.168.1.96";
// Configuración NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600);

bool debug = true;

void setup(void) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  delay(500); //tiempo conexión wifi
  if (debug) {
    Serial.begin(115200);
    delay (1000); //tiempo para arrancar Serial monitor
    Serial.printf("\nEstado de la conexión: %d\n", WiFi.status());
    Serial.printf("Conectando a %s\n", ssid);
  }
  while (WiFi.status() != WL_CONNECTED) {
    if (debug) Serial.printf("Estado de la conexión: %d\n", WiFi.status());
    reconnectWiFi();
    delay(2000);
  }
  if (debug) valores_conexion();
  //Arranco el servidor
  serverTCP.begin();
  if (debug) Serial.printf("Server started, open %s in port 1996\n", WiFi.localIP().toString().c_str());
  // Inicialización del cliente NTP
  timeClient.begin();
  while (!timeClient.update()) {
    timeClient.forceUpdate();
    delay(1000);
  }
  xTaskCreatePinnedToCore(
    checkLiveness,
    "Check live task",
    8192,
    NULL,
    1,
    &t,
    0);
}

void loop() {
  reconnectWiFi();
  tratarClienteEntrate();
} 

void tratarClienteEntrate() {
  WiFiClient clienteTCP = serverTCP.available(); // Escucha a los clientes entrantes
  String comandoTCP, respuesta = "";

  while (clienteTCP && clienteTCP.connected()) {
    //clienteTCP.setTimeout(5000);
    if (clienteTCP.available()) { // si hay bytes para leer desde el cliente
      if (debug) Serial.println("[Cliente available]"); 
      buffer = clienteTCP.readStringUntil('\n'); //Lee mensaje hasta un salto

      //procesa el mensaje y prepara una respuesta:
      String respuesta = "DS: Recibido mensaje: " + buffer + "Reenviando petición a actuador....";
      if (debug) Serial.println("Enviando respuesta a cliente: " + respuesta);
      //Envía respuesta al cliente:
      clienteTCP.println(respuesta);
      clienteTCP.stop();
      if (buffer.indexOf("<DEVICE>LUZ1<DEVICE/>") != -1) {
        buffer = mandarMensajeAIp(buffer, ipLuz1);
      } else if (buffer.indexOf("<DEVICE>RIEGO<DEVICE/>") != -1) {
        buffer = mandarMensajeAIp(buffer, ipRiego);
      } else if (buffer.indexOf("<DEVICE>CLIMA<DEVICE/>") != -1) {
        buffer = mandarMensajeAIp(buffer, ipClima);
      }
      clienteTCP.println("Respuesta recibida desde actuador: " + buffer);
    }   
  }
}

String mandarMensajeAIp(String mensaje, IPAddress ipActuador) {
  WiFiClient clienteActuador;
  int numberOfRetries = 0; 
  String bufferActuador;
  horaUltimoIntento = timeClient.getEpochTime();
  while (!reintentarEnvioMensaje(clienteActuador, ipLuz1, horaUltimoIntento,  tiempoEntreIntentos)) {
    numberOfRetries++;
    if (numberOfRetries >= 10) {
      ESP.restart();
    }
  }
  clienteActuador.println(mensaje);
  if (clienteActuador.available() == 0 && clienteActuador.connected()) {
    if (debug) Serial.println("[Esperando respuesta actuador...]");
  }
  if (clienteActuador.available()) {
    if (debug) Serial.println("clienteActuador available");
      bufferActuador = clienteActuador.readStringUntil('\n');
      if (debug) Serial.println("Mensaje recibido de actuador: " + bufferActuador);
      clienteActuador.stop();
  }
  clienteActuador.stop();
  return bufferActuador;
}

// Función que verifica si se ha superado el tiempo de inactividad
bool reintentarEnvioMensaje(WiFiClient clienteActuador, IPAddress ipActuador, unsigned long horaUltimoIntento, unsigned long tiempoEntreReintentos) {
  unsigned long tiempoTranscurridoUltimoIntento = timeClient.getEpochTime() - horaUltimoIntento;

  if (debug) Serial.println("Tiempo transcurrido desde ultimo intento: " + String(tiempoTranscurridoUltimoIntento) + " segundos");

  if (tiempoTranscurridoUltimoIntento >= tiempoEntreReintentos) {
    horaUltimoIntento = timeClient.getEpochTime();
    if (!clienteActuador.connect(ipActuador, 1996)) {
            if (debug) Serial.println("connection failed, retrying in 1 minute");
            return false;
    } else {
        return true;
    }
  }
}

// Función que verifica si se ha superado el tiempo de inactividad
void checkLiveness(void * args) {
  Serial.println("Checking liveness actuadores on core 2");
  while(true) {
    WiFiClient actuadorRiego;
    WiFiClient actuadorClima;
    WiFiClient actuadorLuz1;
    bool estadoRiego = false;
    bool estadoLuz1 = false;
    bool estadoClima = true;
    if (!actuadorLuz1.connect(ipLuz1, 1998)) {
      if (debug) Serial.println("connection failed with luz_terraza_1");
      estadoLuz1 = false;
    } else {
      estadoLuz1 = true;
      actuadorLuz1.println("live?");
    }
    actuadorLuz1.stop();
    if (!actuadorRiego.connect(ipRiego, 1998)) {
      if (debug) Serial.println("connection failed with riego_terraza");
      estadoRiego = false;
    } else {
      estadoRiego = true;
      actuadorRiego.println("live?");
    }
    actuadorRiego.stop();
    /*
    if (!actuadorClima.connect(ipClima, 1998)) { 
      if (debug) Serial.println("connection failed with clima_terraza");
      estadoClima = false;
    } else {
      estadoClima = true;
      actuadorClima.println("live?");
    }
    actuadorClima.stop();
    */
    if (!estadoRiego && !estadoLuz1) {
      if (debug) Serial.println("Inactividad detectada en los 3 actuadores. Reiniciando domosapiens...");
      ESP.restart(); // Reinicia el microcontrolador
    } else if (debug) Serial.println("Hay actuadores vivos!");
    vTaskDelay(1000);
  }
}

void reconnectWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        if (debug) Serial.println("WiFi desconectado. Intentando reconectar...");
        WiFi.begin(ssid, password); // Reemplaza con tu SSID y contraseña
        while (WiFi.status() != WL_CONNECTED) {
            delay(1000);
            if (debug) Serial.println("Conectando a WiFi...");
        }
        if (debug) Serial.println("Reconectado a WiFi");
    }
}

void valores_conexion() {
  /*
    Estado de la conexión y obtencion de parámetros
      0 : WL_IDLE_STATUS cuando el Wi-Fi está en proceso de cambiar de estado
      1 : WL_NO_SSID_AVAIL en caso de que el SSID configurado no pueda ser alcanzado
      3 : WL_CONNECTED después de establecer una conexión satisfactoriamente
      4 : WL_CONNECT_FAILED si la contraseña es incorrecta
      6 : WL_DISCONNECTED si el módulo no está configurado en el modo de estación
  */
  Serial.println("******** VALORES CONEXION ********");
  Serial.printf("Estado de la conexión: %d\n", WiFi.status());
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("Password: %s\n", WiFi.psk().c_str());
  Serial.printf("BSSID: %s\n", WiFi.BSSIDstr().c_str());
  Serial.print("Conectado, dirección IP: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Getaway IP: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.print("Máscara de subred: ");
  Serial.println(WiFi.subnetMask());
  Serial.printf("Conectado, dirección MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  Serial.println("**********************************");
}