#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include "secrets.h"

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

WiFiClientSecure mqttNetwork;
PubSubClient mqtt(mqttNetwork);
String frameTopic, statusTopic;
unsigned long lastWifiAttempt = 0, lastMqttAttempt = 0, lastFrameAt = 0;

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED || millis() - lastWifiAttempt < 10000) return;
  lastWifiAttempt = millis();
  Serial.printf("[WIFI] Connecting to %s\n", WIFI_SSID);
  WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected() || millis() - lastMqttAttempt < 5000) return;
  lastMqttAttempt = millis();
  String clientId = String("esp32cam-") + MQTT_DEVICE_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, statusTopic.c_str(), 1, true, "offline")) {
    mqtt.publish(statusTopic.c_str(), "online", true);
    Serial.println("[MQTT] Camera online");
  } else Serial.printf("[MQTT] Failed, state=%d\n", mqtt.state());
}

void publishFrame() {
  if (!mqtt.connected() || millis() - lastFrameAt < FRAME_INTERVAL_MS) return;
  lastFrameAt = millis();
  camera_fb_t *frame = esp_camera_fb_get();
  if (!frame) { Serial.println("[CAMERA] Capture failed"); return; }
  bool sent = mqtt.beginPublish(frameTopic.c_str(), frame->len, false);
  if (sent) {
    for (size_t offset = 0; offset < frame->len; offset += 1024) {
      size_t length = min((size_t)1024, frame->len - offset);
      if (mqtt.write(frame->buf + offset, length) != length) { sent = false; break; }
      delay(0);
    }
    if (sent) sent = mqtt.endPublish();
  }
  Serial.printf("[FRAME] %u bytes: %s\n", frame->len, sent ? "sent" : "failed");
  esp_camera_fb_return(frame);
}

void setup() {
  Serial.begin(115200);
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST; config.fb_location = CAMERA_FB_IN_PSRAM;
  config.frame_size = psramFound() ? FRAMESIZE_QVGA : FRAMESIZE_QQVGA;
  config.jpeg_quality = psramFound() ? 12 : 16; config.fb_count = psramFound() ? 2 : 1;
  if (esp_camera_init(&config) != ESP_OK) { Serial.println("[CAMERA] Init failed"); return; }

  frameTopic = String("robot-arm/") + MQTT_DEVICE_ID + "/camera/frame";
  statusTopic = String("robot-arm/") + MQTT_DEVICE_ID + "/camera/status";
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.persistent(false);
  mqttNetwork.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORT); mqtt.setKeepAlive(30); mqtt.setSocketTimeout(15);
  connectWifi();
  Serial.println("[READY] ESP32-CAM MQTT cloud mode");
}

void loop() {
  connectWifi(); connectMqtt();
  if (mqtt.connected()) { mqtt.loop(); publishFrame(); }
  delay(10);
}
