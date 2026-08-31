#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>

const char* WIFI_SSID = "ZerOne BD";
const char* WIFI_PASSWORD = "zeronebd@2026";

ESP8266WebServer server(80);
Servo servoBase, servoShoulder, servoElbow;

const uint8_t BASE_PIN = D1;      // GPIO5
const uint8_t SHOULDER_PIN = D2;  // GPIO4
const uint8_t ELBOW_PIN = D5;     // GPIO14

int basePos = 90, shoulderPos = 90, elbowPos = 90;
bool wifiWasConnected = false;
unsigned long lastStatusPrint = 0;
unsigned long lastReconnect = 0;

void smoothMove(Servo &servo, int &currentPos, int targetPos) {
  targetPos = constrain(targetPos, 0, 180);
  if (currentPos == targetPos) return;
  int stepValue = targetPos > currentPos ? 1 : -1;
  while (currentPos != targetPos) {
    currentPos += stepValue;
    servo.write(currentPos);
    delay(10);
    yield();
  }
}

void applyGesture(const int f[5], const String &dir) {
  bool allOpen = f[0] && f[1] && f[2] && f[3] && f[4];
  bool allClosed = !f[0] && !f[1] && !f[2] && !f[3] && !f[4];

  if (allOpen) {
    Serial.println("[ACTION] All open -> HOME");
    smoothMove(servoBase, basePos, 90);
    smoothMove(servoShoulder, shoulderPos, 90);
    smoothMove(servoElbow, elbowPos, 90);
  } else if (allClosed && dir == "right") {
    Serial.println("[ACTION] Fist + right -> BASE 180");
    smoothMove(servoBase, basePos, 180);
  } else if (allClosed && dir == "left") {
    Serial.println("[ACTION] Fist + left -> BASE 0");
    smoothMove(servoBase, basePos, 0);
  } else if (!f[0] && f[1] && !f[2] && !f[3] && !f[4]) {
    Serial.println("[ACTION] Index -> SHOULDER 90");
    smoothMove(servoShoulder, shoulderPos, 90);
  } else if (!f[0] && f[1] && f[2] && !f[3] && !f[4]) {
    Serial.println("[ACTION] Index + middle -> SHOULDER 40");
    smoothMove(servoShoulder, shoulderPos, 40);
  } else if (!f[0] && !f[1] && !f[2] && f[3] && !f[4]) {
    Serial.println("[ACTION] Ring -> ELBOW 0");
    smoothMove(servoElbow, elbowPos, 0);
  } else if (!f[0] && !f[1] && !f[2] && !f[3] && f[4]) {
    Serial.println("[ACTION] Pinky -> ELBOW 140");
    smoothMove(servoElbow, elbowPos, 140);
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
  smoothMove(servoBase, basePos, targetBase);
  smoothMove(servoShoulder, shoulderPos, targetShoulder);
  smoothMove(servoElbow, elbowPos, targetElbow);
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
  Serial.println("[READY] Waiting for commands...");
}

void loop() {
  server.handleClient();
  handleSerial();
  printNetworkStatus();
}
