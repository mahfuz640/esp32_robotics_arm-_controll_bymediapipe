# React Robot Arm Vision Control

সম্পূর্ণ বাংলা setup, architecture, API, deployment ও troubleshooting guide: [PROJECT_DOCUMENTATION_BN.md](PROJECT_DOCUMENTATION_BN.md)

একই dashboard-এ device/mobile camera থেকে MediaPipe finger detection, ESP8266 robot-arm control এবং ESP32-CAM live monitoring দেখা যায়।

## Project layout

```text
frontend/   React, Vite ও MediaPipe UI
backend/    Node/Express camera ও ESP8266 proxy
firmware/   ESP8266 Arduino firmware
```

## Local setup

```powershell
npm run install:all
npm run build
npm start
```

Dashboard: `http://localhost:5000`

Development-এর জন্য দুই terminal-এ `npm run dev:backend` এবং `npm run dev:frontend` চালান।

## ESP8266 firmware

`firmware/esp8266_robot_arm/secrets.example.h` কপি করে `secrets.h` বানিয়ে Wi-Fi credentials বসান। তারপর `esp8266_robot_arm.ino` upload করুন। `secrets.h` Git-এ যায় না। Servo-তে আলাদা regulated 5V supply দিন এবং ESP8266-এর সঙ্গে common ground রাখুন।

## Device URLs

- Mobile IP Webcam: `http://PHONE_IP:8080/video`
- ESP8266: `http://ESP8266_IP`
- ESP32-CAM: `http://ESP32_IP:81/stream`

## ESP32-CAM online/Wi-Fi firmware

`firmware/esp32_cam_online/secrets.example.h` copy করে একই folder-এ `secrets.h` বানিয়ে
2.4 GHz Wi-Fi name/password দিন। Arduino IDE-তে **AI Thinker ESP32-CAM**, Partition Scheme
**Huge APP**, এবং upload speed **115200** নির্বাচন করে `esp32_cam_online.ino` upload করুন।
Upload-এর সময় GPIO0-কে GND-তে দিন; upload শেষে GPIO0 খুলে reset চাপুন। Serial Monitor-এ
দেখানো `http://ESP32_IP:81/stream` dashboard-এর ESP32-CAM URL field-এ দিন।

## Render deployment

Root-এর `render.yaml` Blueprint দুইটি service বানায়:

- `frontend/`: Render Static Site
- `backend/`: Render Node Web Service

Render Dashboard-এ **New → Blueprint** থেকে GitHub repository connect করুন।

Render cloud local `192.168.x.x` mobile camera, ESP8266 বা ESP32-CAM address access করতে পারে না। Hosted physical control-এর জন্য public HTTPS tunnel, MQTT cloud, অথবা local bridge প্রয়োজন। Local hardware control-এর জন্য computer-এ backend চালানো বর্তমান কার্যকর mode।

## Hosted control with MQTT

Hosted website থেকে ESP8266 control MQTT/TLS দিয়ে হয়:

```text
Render HTTPS API → MQTT broker → ESP8266 → servos
ESP8266 status → MQTT broker → Render API → website
```

1. HiveMQ Cloud/সমমানের MQTT broker তৈরি করে TLS host, port `8883`, username ও password নিন।
2. `secrets.example.h` কপি করে local `secrets.h`-এ `MQTT_HOST`, `MQTT_USERNAME`, `MQTT_PASSWORD` এবং একই `MQTT_DEVICE_ID` বসিয়ে firmware upload করুন। Arduino Library Manager থেকে `PubSubClient` install করুন।
3. Render backend service-এর Environment-এ `MQTT_URL=mqtts://BROKER_HOST:8883`, `MQTT_USERNAME`, `MQTT_PASSWORD`, এবং `MQTT_DEVICE_ID=robot-arm-01` বসান।
4. Backend redeploy করুন। Website status ESP8266 heartbeat পেলে connected দেখাবে।

`MQTT_DEVICE_ID` firmware ও Render-এ হুবহু একই হতে হবে। Firmware বর্তমানে encrypted TLS ব্যবহার করে কিন্তু certificate verification সহজ setup-এর জন্য disabled; production deployment-এ broker CA certificate pin করুন।

Hosted frontend-এ ESP8266 URL/IP দিতে হয় না। `VITE_API_URL` থাকলে UI `Automatic · MQTT Cloud` দেখায় এবং backend retained MQTT status দিয়ে device auto-detect করে। Heartbeat দেরি হলে offline হয় না; ESP8266 সত্যিই MQTT broker থেকে disconnect হলে Last Will status দিয়ে offline হয়। Local HTTP fallback mode-এই কেবল ESP8266 URL field দেখা যায়।

Render configuration সম্পর্কে বিস্তারিত: [Blueprint spec](https://render.com/docs/blueprint-spec) এবং [monorepo support](https://render.com/docs/monorepo-support)।

## Manual control

Dashboard-এর Base, Shoulder ও Elbow slider দিয়ে `0–180°` angle পাঠানো যায়। ESP8266 connected থাকলেই `Move servos` button চালু হয়।
