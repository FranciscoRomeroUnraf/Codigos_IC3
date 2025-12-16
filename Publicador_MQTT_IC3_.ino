#include <WiFi.h>
#include <ArduinoMqttClient.h>
#include <time.h>
#include <ArduinoJson.h>

// ===== CONFIGURACIÓN RED Y MQTT =====
const char* ssid       = "FT-ROMERO-2.4GHz";
const char* password   = "00417639883";
const char* mqtt_server = "192.168.0.16";
const char* tenant     = "UNRaf";

// ===== CONFIGURACIÓN LWT =====
const char* lwt_payload = "offline";
const bool  lwt_retain  = true;
const int   lwt_qos     = 1;

// ===== OBJETOS =====
WiFiClient espClient;
MqttClient client(espClient);

// ===== PINES DE SENSORES Y ACTUADORES =====
const int pinLow    = 13;
const int pinMid    = 12;
const int pinHigh   = 27;
const int pinBomba  = 15;

// ===== LED RGB =====
const int pinRed    = 33;
const int pinGreen  = 26;
const int pinBlue   = 14;

// ===== VARIABLES DE ESTADO =====
bool bombaEncendida   = false;
bool alarmaEncendida  = false;
String nivel          = "DESCONOCIDO";
String mac;

// Timers
unsigned long tiempoApagado        = 0;
unsigned long tiempoMaxBomba       = 0;
unsigned long tiempoDescansoBomba  = 10000;
unsigned long inicioBomba          = 0;
unsigned long bloqueoBomba         = 0;

// Variables de estado anterior
String prevNivel     = "";
bool prevBomba       = false;
bool prevAlarma      = false;

// Publicación periódica
unsigned long ultimoEnvioHora = 0;
const unsigned long intervaloHora = 3600000; // 1 hora

// ===== LED RGB CONTROL =====
void setColor(bool r, bool g, bool b) {
  digitalWrite(pinRed,   r ? HIGH : LOW);
  digitalWrite(pinGreen, g ? HIGH : LOW);
  digitalWrite(pinBlue,  b ? HIGH : LOW);
}

// ===== CONEXIÓN WIFI =====
void setup_wifi() {
  Serial.println("Conectando a WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  mac = WiFi.macAddress();
  mac.replace(":", "");

  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;

  while (!getLocalTime(&timeinfo)) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nHora sincronizada");
}

// ===== OBTENER FECHA YYYYMMDDTHHMMSS =====
String getDateCompact() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00000000T";

  char buffer[16];
  strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%S", &timeinfo);
  return String(buffer);
}

// ===== CALLBACK MQTT =====
void onMqttMessage(int messageSize) {
  Serial.println("\n📩 Mensaje recibido:");
  Serial.println("Tópico: " + String(client.messageTopic()));

  String msg;
  while (client.available()) {
    msg += (char)client.read();
  }
  Serial.println("Payload: " + msg);

  StaticJsonDocument<200> doc;
  if (deserializeJson(doc, msg)) {
    Serial.println("⚠ Error al parsear JSON");
    return;
  }

  String command = doc["command"];
  int tiempo     = doc["tiempo"] | 0;
  String ackTopic = String(tenant) + "/" + mac + "/ack";

  if (command == "encender" && tiempo > 0) {
    bombaEncendida = true;
    tiempoApagado  = millis() + (tiempo * 1000);
    inicioBomba    = millis();
    digitalWrite(pinBomba, HIGH);

    client.beginMessage(ackTopic, false, 1);
    client.print("Bomba encendida");
    client.endMessage();
  } 
  else if (command == "apagar") {
    bombaEncendida = false;
    digitalWrite(pinBomba, LOW);

    client.beginMessage(ackTopic, false, 1);
    client.print("Bomba apagada");
    client.endMessage();
  } 
  else if (command == "set_max" && tiempo > 0) {
    tiempoMaxBomba = tiempo * 1000;

    client.beginMessage(ackTopic, false, 1);
    client.print("Tiempo máximo actualizado");
    client.endMessage();
  } 
  else if (command == "set_descanso" && tiempo > 0) {
    tiempoDescansoBomba = tiempo * 1000;

    client.beginMessage(ackTopic, false, 1);
    client.print("Tiempo de descanso actualizado");
    client.endMessage();
  } 
  else {
    client.beginMessage(ackTopic, false, 1);
    client.print("Comando inválido");
    client.endMessage();
  }
}

// ===== MQTT RECONNECT =====
void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");

    String cmdTopic    = String(tenant) + "/" + mac + "/cmd";
    String statusTopic = String(tenant) + "/" + mac + "/status";

    client.beginWill(statusTopic.c_str(), strlen(lwt_payload), lwt_retain, lwt_qos);
    client.print(lwt_payload);
    client.endWill();

    if (client.connect(mqtt_server, 1883)) {
      Serial.println("\nConectado al broker");
      client.beginMessage(statusTopic.c_str(), true, 1);
      client.print("online");
      client.endMessage();

      client.subscribe(cmdTopic.c_str(), 1);
      Serial.println("📡 Suscrito a: " + cmdTopic);
    } 
    else {
      Serial.print("Error rc=");
      Serial.print(client.connectError());
      Serial.println(" Reintentando en 5s...");
      delay(5000);
    }
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  pinMode(pinLow,    INPUT_PULLUP);
  pinMode(pinMid,    INPUT_PULLUP);
  pinMode(pinHigh,   INPUT_PULLUP);
  pinMode(pinBomba,  OUTPUT);
  digitalWrite(pinBomba, LOW);

  pinMode(pinRed,   OUTPUT);
  pinMode(pinGreen, OUTPUT);
  pinMode(pinBlue,  OUTPUT);
  setColor(false, false, false);

  setup_wifi();
  client.onMessage(onMqttMessage);

  Serial.println("Sistema iniciado.");
}

// ===== LOOP PRINCIPAL =====
void loop() {
  if (!client.connected()) reconnect();
  client.poll();

  unsigned long ahora = millis();

  bool low   = !digitalRead(pinLow);
  bool mid   = !digitalRead(pinMid);
  bool high  = !digitalRead(pinHigh);

  if (low && !high) {
    nivel = "BAJO";
    alarmaEncendida = true;
  } 
  else if (mid && !high) {
    nivel = "MEDIO";
    alarmaEncendida = false;
  } 
  else if (high) {
    nivel = "ALTO";
    alarmaEncendida = false;
  } 
  else {
    nivel = "DESCONOCIDO";
    alarmaEncendida = false;
  }

  // Auto-apagado por tiempo máximo
  if (bombaEncendida && tiempoMaxBomba > 0 && (ahora - inicioBomba >= tiempoMaxBomba)) {
    bombaEncendida = false;
    digitalWrite(pinBomba, LOW);
    bloqueoBomba = ahora + tiempoDescansoBomba;

    client.beginMessage((String(tenant) + "/" + mac + "/ack").c_str(), false, 1);
    client.print("Bomba apagada por tiempo máximo");
    client.endMessage();
  }

  // Encendido automático por nivel MEDIO
  if (nivel == "MEDIO" && ahora > bloqueoBomba) {
    if (!bombaEncendida) {
      bombaEncendida = true;
      inicioBomba = ahora;
      digitalWrite(pinBomba, HIGH);
    }
  } else if (nivel != "MEDIO" && bombaEncendida) {
    bombaEncendida = false;
    digitalWrite(pinBomba, LOW);
  }

  // LEDs según estado
  if (alarmaEncendida) setColor(true, false, false);
  else if (bombaEncendida) setColor(false, true, false);
  else if (nivel == "ALTO") setColor(false, false, true);
  else setColor(false, false, false);

  // ===== Publicación por evento o por hora =====
  bool publicar = false;
  bool publicarHora = false;

  if (nivel != prevNivel || bombaEncendida != prevBomba || alarmaEncendida != prevAlarma)
    publicar = true;

  if (millis() - ultimoEnvioHora >= intervaloHora) {
    publicar = true;
    publicarHora = true;
    ultimoEnvioHora = millis();
  }

  if (publicar) {
    String topicBase = String(tenant) + "/" + mac;

    if (nivel != prevNivel || publicarHora) {
      client.beginMessage((topicBase + "/nivel").c_str(), true, 1);
      client.print(nivel);
      client.endMessage();
    }

    if (bombaEncendida != prevBomba || publicarHora) {
      client.beginMessage((topicBase + "/bomba").c_str(), true, 1);
      client.print(bombaEncendida ? "ON" : "OFF");
      client.endMessage();
    }

    if (alarmaEncendida != prevAlarma || publicarHora) {
      client.beginMessage((topicBase + "/alarma").c_str(), true, 1);
      client.print(alarmaEncendida ? "ON" : "OFF");
      client.endMessage();
    }

    if (publicarHora) {
      String datetime = getDateCompact();
      client.beginMessage((topicBase + "/datetime").c_str(), false, 1);
      client.print(datetime);
      client.endMessage();
    }

    prevNivel  = nivel;
    prevBomba  = bombaEncendida;
    prevAlarma = alarmaEncendida;

    Serial.println("📤 Estados publicados.");
  }

  delay(100);
}
