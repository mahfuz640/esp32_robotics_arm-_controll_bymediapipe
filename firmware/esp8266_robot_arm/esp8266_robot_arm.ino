#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <WiFiClientSecureBearSSL.h>
#include <PubSubClient.h>
#include "secrets.h"

ESP8266WebServer server(80);
BearSSL::WiFiClientSecure mqttNetwork;
PubSubClient mqtt(mqttNetwork);
Servo servoBase, servoShoulder, servoElbow;

const uint8_t BASE_PIN = D1;      // GPIO5
const uint8_t SHOULDER_PIN = D2;  // GPIO4
const uint8_t ELBOW_PIN = D5;     // GPIO14

int basePos = 90, shoulderPos = 90, elbowPos = 90;
int baseTarget = 90, shoulderTarget = 90, elbowTarget = 90;
unsigned long lastServoStep = 0;
const uint8_t SERVO_STEP_MS = 12;
const uint8_t SERVO_DEADBAND = 2;
bool wifiWasConnected = false;
unsigned long lastStatusPrint = 0;
unsigned long lastReconnect = 0;
unsigned long lastMqttReconnect = 0;
unsigned long lastHeartbeat = 0;
String mqttCommandTopic;
String mqttStatusTopic;

void setServoTargets(int base, int shoulder, int elbow) {
  baseTarget = constrain(base, 0, 180);
  shoulderTarget = constrain(shoulder, 0, 180);
  elbowTarget = constrain(elbow, 0, 180);
}

void stepServo(Servo &servo, int &currentPos, int targetPos) {
  int difference = targetPos - currentPos;
  if (abs(difference) <= SERVO_DEADBAND) return;
  currentPos += difference > 0 ? 1 : -1;
  servo.write(currentPos);
}

void updateServos() {
  if (millis() - lastServoStep < SERVO_STEP_MS) return;
  lastServoStep = millis();
  stepServo(servoBase, basePos, baseTarget);
  stepServo(servoShoulder, shoulderPos, shoulderTarget);
  stepServo(servoElbow, elbowPos, elbowTarget);
}

void applyGesture(const int f[5], const String &dir) {
  bool allOpen = f[0] && f[1] && f[2] && f[3] && f[4];
  bool allClosed = !f[0] && !f[1] && !f[2] && !f[3] && !f[4];

  if (allOpen) {
    Serial.println("[ACTION] All open -> HOME");
    setServoTargets(90, 90, 90);
  } else if (allClosed && dir == "right") {
    Serial.println("[ACTION] Fist + right -> BASE 180");
    baseTarget = 180;
  } else if (allClosed && dir == "left") {
    Serial.println("[ACTION] Fist + left -> BASE 0");
    baseTarget = 0;
  } else if (!f[0] && f[1] && !f[2] && !f[3] && !f[4]) {
    Serial.println("[ACTION] Index -> SHOULDER 90");
    shoulderTarget = 90;
  } else if (!f[0] && f[1] && f[2] && !f[3] && !f[4]) {
    Serial.println("[ACTION] Index + middle -> SHOULDER 40");
    shoulderTarget = 40;
  } else if (!f[0] && !f[1] && !f[2] && f[3] && !f[4]) {
    Serial.println("[ACTION] Ring -> ELBOW 0");
    elbowTarget = 0;
  } else if (!f[0] && !f[1] && !f[2] && !f[3] && f[4]) {
    Serial.println("[ACTION] Pinky -> ELBOW 140");
    elbowTarget = 140;
  } else {
    Serial.println("[ACTION] Pattern has no assigned movement");
  }
}

bool parsePattern(String pattern, int fingers[5]) {
  for (int i = 0; i < 5; i++) {
    int comma = pattern.indexOf(',');
    String value = comma >= 0 ? pattern.substring(0, comma) : pattern;
    value.trim();
    if (value != "0" && value != "1") return false;
    fingers[i] = value.toInt();
    if (i < 4 && comma < 0) return false;
    pattern = comma >= 0 ? pattern.substring(comma + 1) : "";
  }
  return true;
}

void handleControl() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  Serial.print("[HTTP] Client: "); Serial.println(server.client().remoteIP());
  if (!server.hasArg("pattern") || !server.hasArg("dir")) {
    Serial.println("[HTTP] ERROR: pattern/dir missing");
    server.send(400, "application/json", "{\"error\":\"pattern and dir required\"}");
    return;
  }

  int fingers[5];
  String pattern = server.arg("pattern");
  String dir = server.arg("dir");
  dir.toLowerCase();
  if (!parsePattern(pattern, fingers) || (dir != "left" && dir != "right" && dir != "center")) {
    Serial.println("[HTTP] ERROR: invalid gesture");
    server.send(400, "application/json", "{\"error\":\"invalid gesture\"}");
    return;
  }

  Serial.print("[GESTURE] pattern="); Serial.print(pattern);
  Serial.print(" dir="); Serial.println(dir);
  applyGesture(fingers, dir);
  server.send(200, "application/json", String("{\"ok\":true,\"pattern\":\"") + pattern + "\",\"dir\":\"" + dir + "\"}");
  Serial.println("[HTTP] Response: 200 OK");
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

void handleServo() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("base") || !server.hasArg("shoulder") || !server.hasArg("elbow")) {
    server.send(400, "application/json", "{\"error\":\"base, shoulder and elbow required\"}");
    return;
  }
  int targetBase = constrain(server.arg("base").toInt(), 0, 180);
  int targetShoulder = constrain(server.arg("shoulder").toInt(), 0, 180);
  int targetElbow = constrain(server.arg("elbow").toInt(), 0, 180);
  Serial.print("[MANUAL] Base="); Serial.print(targetBase);
  Serial.print(" Shoulder="); Serial.print(targetShoulder);
  Serial.print(" Elbow="); Serial.println(targetElbow);
  setServoTargets(targetBase, targetShoulder, targetElbow);
  server.send(200, "application/json", String("{\"ok\":true,\"base\":") + basePos + ",\"shoulder\":" + shoulderPos + ",\"elbow\":" + elbowPos + "}");
  Serial.println("[MANUAL] Movement complete");
}

void handleSerial() {
  if (!Serial.available()) return;
  String input = Serial.readStringUntil('\n');
  input.trim();
  int split = input.lastIndexOf(',');
  if (split < 0) { Serial.println("[SERIAL] Invalid format"); return; }
  String pattern = input.substring(0, split);
  String dir = input.substring(split + 1);
  dir.trim(); dir.toLowerCase();
  int fingers[5];
  if (parsePattern(pattern, fingers)) {
    Serial.print("[SERIAL] pattern="); Serial.print(pattern);
    Serial.print(" dir="); Serial.println(dir);
    applyGesture(fingers, dir);
  } else Serial.println("[SERIAL] Invalid pattern");
}

void publishMqttStatus(bool online) {
  if (!mqtt.connected()) return;
  String payload = String("{\"online\":") + (online ? "true" : "false") +
    ",\"base\":" + basePos + ",\"shoulder\":" + shoulderPos + ",\"elbow\":" + elbowPos + "}";
  mqtt.publish(mqttStatusTopic.c_str(), payload.c_str(), true);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String command;
  for (unsigned int i = 0; i < length; i++) command += (char)payload[i];
  Serial.print("[MQTT] Command: "); Serial.println(command);

  if (command.startsWith("gesture|")) {
    int separator = command.lastIndexOf('|');
    String pattern = command.substring(8, separator);
    String dir = command.substring(separator + 1);
    int fingers[5];
    if (separator > 8 && parsePattern(pattern, fingers)) applyGesture(fingers, dir);
    else Serial.println("[MQTT] Invalid gesture command");
  } else if (command.startsWith("servo|")) {
    int first = command.indexOf('|', 6);
    int second = command.indexOf('|', first + 1);
    if (first > 6 && second > first) {
      int targetBase = constrain(command.substring(6, first).toInt(), 0, 180);
      int targetShoulder = constrain(command.substring(first + 1, second).toInt(), 0, 180);
      int targetElbow = constrain(command.substring(second + 1).toInt(), 0, 180);
      Serial.printf("[MQTT MANUAL] Base=%d Shoulder=%d Elbow=%d\n", targetBase, targetShoulder, targetElbow);
      setServoTargets(targetBase, targetShoulder, targetElbow);
    } else Serial.println("[MQTT] Invalid servo command");
  } else Serial.println("[MQTT] Unknown command");
  publishMqttStatus(true);
}

void maintainMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    if (millis() - lastMqttReconnect < 5000) return;
    lastMqttReconnect = millis();
    String clientId = String("esp8266-") + MQTT_DEVICE_ID + "-" + String(ESP.getChipId(), HEX);
    Serial.print("[MQTT] Connecting to "); Serial.println(MQTT_HOST);
    if (mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                     mqttStatusTopic.c_str(), 1, true, "{\"online\":false}")) {
      Serial.println("[MQTT] ONLINE");
      mqtt.subscribe(mqttCommandTopic.c_str(), 1);
      Serial.print("[MQTT] Subscribed: "); Serial.println(mqttCommandTopic);
      publishMqttStatus(true);
    } else {
      Serial.print("[MQTT] Failed, state="); Serial.println(mqtt.state());
    }
    return;
  }
  mqtt.loop();
  if (millis() - lastHeartbeat >= 5000) {
    lastHeartbeat = millis();
    publishMqttStatus(true);
  }
}

void printNetworkStatus() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifiWasConnected) {
    wifiWasConnected = connected;
    if (connected) {
      Serial.println("[WIFI] ONLINE / RECONNECTED");
      Serial.print("[WIFI] IP: http://"); Serial.println(WiFi.localIP());
    } else Serial.println("[WIFI] OFFLINE");
  }

  if (millis() - lastStatusPrint >= 5000) {
    lastStatusPrint = millis();
    Serial.print("[STATUS] WiFi="); Serial.print(connected ? "ONLINE" : "OFFLINE");
    Serial.print(" | Server="); Serial.print(connected ? "ONLINE" : "OFFLINE");
    if (connected) { Serial.print(" | IP="); Serial.print(WiFi.localIP()); Serial.print(" | RSSI="); Serial.print(WiFi.RSSI()); Serial.print("dBm"); }
    Serial.print(" | Servo="); Serial.print(basePos); Serial.print(','); Serial.print(shoulderPos); Serial.print(','); Serial.println(elbowPos);
  }

  if (!connected && millis() - lastReconnect >= 10000) {
    lastReconnect = millis();
    Serial.println("[WIFI] Reconnect attempt...");
    WiFi.reconnect();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  Serial.println(); Serial.println("================================");
  Serial.println(" ESP8266 ROBOT ARM CONTROLLER");
  Serial.println("================================");

  servoBase.attach(BASE_PIN); servoShoulder.attach(SHOULDER_PIN); servoElbow.attach(ELBOW_PIN);
  servoBase.write(basePos); servoShoulder.write(shoulderPos); servoElbow.write(elbowPos);
  Serial.println("[SERVO] READY: Base=D1 Shoulder=D2 Elbow=D5");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  Serial.print("[WIFI] Connecting to: "); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) { delay(400); Serial.print('.'); }
  Serial.println();
  wifiWasConnected = WiFi.status() == WL_CONNECTED;
  if (wifiWasConnected) {
    Serial.println("[WIFI] ONLINE");
    Serial.print("[WIFI] IP: http://"); Serial.println(WiFi.localIP());
    Serial.print("[WIFI] Signal: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  } else Serial.println("[WIFI] Connection timed out; retrying in background");

  server.on("/control", HTTP_GET, handleControl);
  server.on("/control", HTTP_OPTIONS, handleOptions);
  server.on("/servo", HTTP_GET, handleServo);
  server.on("/servo", HTTP_OPTIONS, handleOptions);
  server.on("/", HTTP_GET, []() { server.send(200, "text/plain", "Robot arm controller online"); });
  server.begin();
  Serial.println("[SERVER] HTTP SERVER ONLINE: port 80");
  mqttCommandTopic = String("robot-arm/") + MQTT_DEVICE_ID + "/command";
  mqttStatusTopic = String("robot-arm/") + MQTT_DEVICE_ID + "/status";
  mqttNetwork.setInsecure(); // TLS encrypted; install broker CA cert for strict verification.
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(384);
  Serial.println("[READY] Waiting for HTTP/MQTT commands...");
}

void loop() {
  server.handleClient();
  handleSerial();
  updateServos();
  printNetworkStatus();
  maintainMqtt();
}
