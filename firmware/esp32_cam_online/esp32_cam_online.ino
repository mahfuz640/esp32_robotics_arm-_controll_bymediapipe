#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "secrets.h"

// AI-Thinker ESP32-CAM pin map.
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

httpd_handle_t cameraServer = nullptr;
httpd_handle_t streamServer = nullptr;
unsigned long lastReconnectAttempt = 0;

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width">
<title>ESP32-CAM</title></head><body style="font-family:sans-serif;text-align:center">
<h2>ESP32-CAM Online</h2><img src="http://%IP%:81/stream" style="max-width:100%;height:auto">
<p><a href="/capture">Capture photo</a></p></body></html>
)HTML";

esp_err_t indexHandler(httpd_req_t *req) {
  String page = FPSTR(INDEX_HTML);
  page.replace("%IP%", WiFi.localIP().toString());
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, page.c_str(), page.length());
}

esp_err_t captureHandler(httpd_req_t *req) {
  camera_fb_t *frame = esp_camera_fb_get();
  if (!frame) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t result = httpd_resp_send(req, (const char *)frame->buf, frame->len);
  esp_camera_fb_return(frame);
  return result;
}

esp_err_t streamHandler(httpd_req_t *req) {
  static const char *CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
  static const char *BOUNDARY = "\r\n--frame\r\n";
  static const char *HEADER = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

  httpd_resp_set_type(req, CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  while (true) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (!frame) return ESP_FAIL;
    char header[64];
    size_t headerLength = snprintf(header, sizeof(header), HEADER, frame->len);
    esp_err_t result = httpd_resp_send_chunk(req, BOUNDARY, strlen(BOUNDARY));
    if (result == ESP_OK) result = httpd_resp_send_chunk(req, header, headerLength);
    if (result == ESP_OK) result = httpd_resp_send_chunk(req, (const char *)frame->buf, frame->len);
    esp_camera_fb_return(frame);
    if (result != ESP_OK) break;
  }
  return ESP_OK;
}

void startCameraServers() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  httpd_uri_t indexUri = { .uri = "/", .method = HTTP_GET, .handler = indexHandler, .user_ctx = nullptr };
  httpd_uri_t captureUri = { .uri = "/capture", .method = HTTP_GET, .handler = captureHandler, .user_ctx = nullptr };
  if (httpd_start(&cameraServer, &config) == ESP_OK) {
    httpd_register_uri_handler(cameraServer, &indexUri);
    httpd_register_uri_handler(cameraServer, &captureUri);
  }

  config.server_port = 81;
  config.ctrl_port += 1;
  httpd_uri_t streamUri = { .uri = "/stream", .method = HTTP_GET, .handler = streamHandler, .user_ctx = nullptr };
  if (httpd_start(&streamServer, &config) == ESP_OK) {
    httpd_register_uri_handler(streamServer, &streamUri);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.frame_size = psramFound() ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
  config.jpeg_quality = psramFound() ? 10 : 14;
  config.fb_count = psramFound() ? 2 : 1;

  esp_err_t cameraResult = esp_camera_init(&config);
  if (cameraResult != ESP_OK) {
    Serial.printf("[CAMERA] Init failed: 0x%x\n", cameraResult);
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
  Serial.println();

  startCameraServers();
  Serial.printf("[READY] Page:    http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.printf("[READY] Stream:  http://%s:81/stream\n", WiFi.localIP().toString().c_str());
  Serial.printf("[READY] Capture: http://%s/capture\n", WiFi.localIP().toString().c_str());
}

void loop() {
  if (WiFi.status() != WL_CONNECTED && millis() - lastReconnectAttempt > 10000) {
    lastReconnectAttempt = millis();
    Serial.println("[WIFI] Reconnecting...");
    WiFi.reconnect();
  }
  delay(100);
}
