# ESP8266 Robot Arm Vision Control — সম্পূর্ণ Project Documentation

## ১. Project-এর উদ্দেশ্য

এই project-এ camera থেকে মানুষের হাত ও আঙুল শনাক্ত করে একটি ESP8266-controlled robotic arm চালানো হয়। একই web dashboard-এ control camera, ESP32-CAM monitoring, ESP8266 connection status এবং manual servo sliders রয়েছে। Website Render-এ HTTPS দিয়ে host করা এবং ESP8266-এর সঙ্গে secure MQTT broker দিয়ে যোগাযোগ করা হয়েছে।

## ২. প্রধান feature

- Device/mobile camera থেকে MediaPipe hand landmark detection
- Thumb, index, middle, ring ও pinky-এর আলাদা open/closed state
- Hand position থেকে `left`, `right`, `center` direction
- Gesture দিয়ে Base, Shoulder ও Elbow servo control
- Base, Shoulder ও Elbow-এর `0–180°` manual slider
- ESP32-CAM live monitoring panel
- MQTT দিয়ে hosted website থেকে ESP8266 control
- ESP8266 automatic online/offline detection
- ESP8266 Serial Monitor-এ Wi-Fi, MQTT, command ও servo status
- Local HTTP fallback mode
- Frontend এবং backend আলাদা Render service

## ৩. ব্যবহৃত hardware

| Hardware | কাজ |
|---|---|
| ESP8266 NodeMCU | Servo control, Wi-Fi ও MQTT communication |
| ESP32-CAM | Robot arm monitoring video |
| তিনটি servo | Base, Shoulder ও Elbow movement |
| External regulated 5V supply | Servo power |
| Mobile/computer camera | MediaPipe finger detection |
| Wi-Fi router | Devices-এর network connection |

### Servo wiring

| Servo | NodeMCU pin | GPIO |
|---|---:|---:|
| Base signal | D1 | GPIO5 |
| Shoulder signal | D2 | GPIO4 |
| Elbow signal | D5 | GPIO14 |

Servo-এর VCC external regulated 5V supply-তে এবং GND external supply-তে দিতে হবে। External supply GND ও ESP8266 GND common করতে হবে। Servo-কে NodeMCU-এর 3.3V pin থেকে power দেওয়া যাবে না।

## ৪. ব্যবহৃত software ও library

### Frontend

- React 19
- Vite 7
- `@mediapipe/tasks-vision`
- HTML Canvas
- Browser Camera API (`getUserMedia`)

### Backend

- Node.js
- Express 5
- MQTT.js
- Render Web Service

### ESP8266 firmware

- Arduino IDE
- ESP8266WiFi
- ESP8266WebServer
- Servo library
- PubSubClient
- BearSSL `WiFiClientSecure`

### Cloud services

- GitHub: source control
- Render Static Site: React frontend
- Render Web Service: Node backend
- HiveMQ Cloud Serverless: MQTT broker

## ৫. Project folder structure

```text
project-root/
├── frontend/
│   ├── src/
│   │   ├── App.jsx
│   │   ├── main.jsx
│   │   ├── styles.css
│   │   └── overrides.css
│   ├── package.json
│   ├── vite.config.js
│   └── .env.example
├── backend/
│   ├── server.js
│   ├── package.json
│   └── package-lock.json
├── firmware/
│   └── esp8266_robot_arm/
│       ├── esp8266_robot_arm.ino
│       ├── secrets.example.h
│       └── secrets.h (Git-ignored)
├── render.yaml
├── package.json
├── brain.md
├── README.md
└── PROJECT_DOCUMENTATION_BN.md
```

## ৬. সম্পূর্ণ system architecture

```text
Device Camera
    ↓
React + MediaPipe HandLandmarker
    ↓ finger pattern + direction
Render HTTPS Backend
    ↓ MQTT/TLS publish
HiveMQ Cloud Broker
    ↓ subscribed command
ESP8266
    ↓ PWM
Base / Shoulder / Elbow servos

ESP8266
    ↓ retained status + heartbeat
HiveMQ Cloud
    ↓ status subscription
Render Backend
    ↓ /api/device-status
React Dashboard
```

ESP32-CAM flow আলাদা:

```text
ESP32-CAM stream → React monitoring panel
```

## ৭. MediaPipe finger detection

MediaPipe ২১টি hand landmark শনাক্ত করে। Frontend নিচের landmark position দেখে finger open/closed নির্ধারণ করে:

- Thumb: landmark 4 ও 3-এর horizontal position
- Index: landmark 8 ও 6
- Middle: landmark 12 ও 10
- Ring: landmark 16 ও 14
- Pinky: landmark 20 ও 18

Pattern order:

```text
thumb,index,middle,ring,pinky
```

উদাহরণ:

```text
0,1,0,0,0 = শুধু index finger open
0,1,1,0,0 = index ও middle open
1,1,1,1,1 = সব finger open
0,0,0,0,0 = fist
```

Wrist landmark-এর X position:

- `< 0.4` হলে `left`
- `> 0.6` হলে `right`
- মাঝখানে হলে `center`

## ৮. Gesture command mapping

| Gesture | Servo action |
|---|---|
| সব finger open | Base, Shoulder, Elbow → 90° home |
| Fist + right | Base → 180° |
| Fist + left | Base → 0° |
| শুধু index | Shoulder → 90° |
| Index + middle | Shoulder → 40° |
| শুধু ring | Elbow → 0° |
| শুধু pinky | Elbow → 140° |

Servo movement `smoothMove()` দিয়ে প্রতি ধাপে ১° এবং ১০ ms delay-এ হয়। `yield()` ESP8266 watchdog ও Wi-Fi সচল রাখে।

## ৯. MQTT communication

Hosted Render backend local `192.168.x.x` ESP8266 address access করতে পারে না। তাই ESP8266 নিজে HiveMQ Cloud broker-এ outbound TLS connection তৈরি করে।

### MQTT topics

```text
Command: robot-arm/robot-arm-01/command
Status:  robot-arm/robot-arm-01/status
```

`robot-arm-01` হলো `MQTT_DEVICE_ID`। Firmware ও Render environment-এ এটি একই হতে হবে।

### Gesture payload

```text
gesture|0,1,0,0,0|center
```

### Manual servo payload

```text
servo|100|90|40
```

অর্থ:

```text
Base=100°, Shoulder=90°, Elbow=40°
```

### Status payload

```json
{
  "online": true,
  "base": 90,
  "shoulder": 90,
  "elbow": 90
}
```

ESP8266 retained online status ও heartbeat publish করে। ESP8266 broker থেকে সত্যিই disconnect হলে MQTT Last Will retained `offline` status publish করে। Heartbeat সাময়িক দেরি হলে website offline হয় না।

ESP32-CAM MQTT cloud preview bandwidth কম রাখতে QQVGA `160×120` JPEG এবং maximum-performance profile-এ `250 ms` frame interval ব্যবহার করে, অর্থাৎ network ও broker অনুকূলে target প্রায় 4 FPS। Camera frame ৩০ সেকেন্ড না এলে monitoring panel offline দেখায়। MQTT video পূর্ণ frame-rate stream নয়; এটি low-frame-rate monitoring preview। Connection অস্থিতিশীল হলে interval `500–1000 ms` করা উচিত।

## ১০. Backend API

Production backend:

```text
https://esp32-robotics-backend.onrender.com
```

### Health API

```http
GET /api/health
```

Expected response:

```json
{"ok":true,"service":"robot-arm-vision-api"}
```

### Device status

```http
GET /api/device-status
```

MQTT response example:

```json
{
  "online": true,
  "base": 90,
  "shoulder": 90,
  "elbow": 90,
  "broker": true,
  "transport": "mqtt"
}
```

### Gesture control

```http
GET /api/control?pattern=0,1,0,0,0&dir=center
```

### Manual servo control

```http
GET /api/servo?base=100&shoulder=90&elbow=40
```

MQTT configured থাকলে backend MQTT command publish করে। `MQTT_URL` না থাকলে local HTTP fallback ব্যবহার করে এবং `controller` URL প্রয়োজন হয়।

### Mobile camera frame proxy

```http
GET /api/mobile-frame?url=http://PHONE_IP:8080/video
```

Backend IP Webcam-এর `/video` URL-কে `/shot.jpg`-এ পরিবর্তন করে browser CORS সমস্যা এড়ায়। Render cloud local mobile IP access করতে পারে না, তাই এটি local backend mode-এ কার্যকর।

## ১১. Manual control panel

Dashboard-এ তিনটি slider রয়েছে:

- Base: 0–180°
- Shoulder: 0–180°
- Elbow: 0–180°

`Move servos` চাপলে frontend `/api/servo` call করে। Backend command MQTT broker-এ publish করে। ESP8266 command পেয়ে smooth movement চালায় এবং নতুন position status publish করে। ESP8266 connected না থাকলে button disabled থাকে।

## ১২. ESP8266 automatic detection

Hosted MQTT mode-এ ESP8266 IP বা URL দিতে হয় না। Website backend-এর MQTT status দিয়ে device চিনে নেয় এবং Settings-এ দেখায়:

```text
Automatic · MQTT Cloud
```

Local HTTP fallback mode-এ ESP8266 URL manually দিতে হয়।

## ১৩. ESP8266 secrets configuration

`secrets.example.h` কপি করে `secrets.h` বানাতে হবে:

```cpp
#pragma once

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "YOUR_CLUSTER.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT = 8883;
const char* MQTT_USERNAME = "YOUR_MQTT_USERNAME";
const char* MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";
const char* MQTT_DEVICE_ID = "robot-arm-01";
```

`secrets.h` `.gitignore`-এ আছে। আসল credentials GitHub-এ push করা যাবে না।

## ১৪. Arduino firmware upload

1. Arduino IDE-তে ESP8266 board package install করুন।
2. Library Manager থেকে `PubSubClient` ও compatible `Servo` library install করুন।
3. Board নির্বাচন করুন: `NodeMCU 1.0 (ESP-12E Module)`।
4. সঠিক COM port নির্বাচন করুন।
5. `esp8266_robot_arm.ino` upload করুন।
6. Serial Monitor baud `115200` করুন।

Expected output:

```text
[WIFI] ONLINE
[WIFI] IP: http://192.168.x.x
[SERVER] HTTP SERVER ONLINE: port 80
[MQTT] ONLINE
[MQTT] Subscribed: robot-arm/robot-arm-01/command
```

Command এলে:

```text
[MQTT] Command: servo|100|90|90
[MQTT MANUAL] Base=100 Shoulder=90 Elbow=90
```

## ১৫. HiveMQ Cloud configuration

1. HiveMQ Cloud-এ Serverless broker তৈরি করুন।
2. Access Management-এ credential তৈরি করুন।
3. Permission `Publish and Subscribe` দিন।
4. TLS hostname এবং port `8883` সংগ্রহ করুন।
5. Credentials firmware `secrets.h` ও Render Environment-এ দিন।

Password, API key বা license key screenshot/chat/GitHub-এ প্রকাশ করা যাবে না। প্রকাশ হলে credential delete করে নতুনটি তৈরি করতে হবে।

## ১৬. Render configuration

### Frontend Static Site

```text
Root Directory: frontend
Build Command: npm ci && npm run build
Publish Directory: dist
```

Environment:

```text
VITE_API_URL=https://esp32-robotics-backend.onrender.com
```

### Backend Web Service

```text
Root Directory: backend
Build Command: npm ci
Start Command: npm start
Health Check: /api/health
```

Environment:

```text
MQTT_URL=mqtts://YOUR_BROKER_HOST:8883
MQTT_USERNAME=YOUR_MQTT_USERNAME
MQTT_PASSWORD=YOUR_MQTT_PASSWORD
MQTT_DEVICE_ID=robot-arm-01
FRONTEND_ORIGIN=https://YOUR_FRONTEND.onrender.com
```

Backend service অবশ্যই `Web Service`; `Static Site` হিসেবে তৈরি করলে `Publish directory ./dist does not exist` error হবে।

## ১৭. Local development

প্রথমবার:

```powershell
npm run install:all
npm run build
```

Production-style local server:

```powershell
npm start
```

Dashboard:

```text
http://localhost:5000
```

Development mode:

```powershell
# Terminal 1
npm run dev:backend

# Terminal 2
npm run dev:frontend
```

Vite dashboard:

```text
http://localhost:5173
```

## ১৮. Camera modes

### This device camera

Dashboard যে device-এ খোলা, তার camera `getUserMedia()` দিয়ে ব্যবহার হয়। Camera permission প্রয়োজন। `localhost` অথবা HTTPS secure context দরকার।

### Mobile IP Camera

Local backend mode-এ Android IP Webcam-এর URL ব্যবহার করা যায়:

```text
http://PHONE_IP:8080/video
```

### ESP32-CAM

Arduino `CameraWebServer` example-এর সাধারণ stream URL:

```text
http://ESP32_IP:81/stream
```

HTTPS hosted page থেকে HTTP ESP32-CAM stream browser mixed-content policy-তে block হতে পারে। Public HTTPS proxy/tunnel ছাড়া Render-hosted monitoring নির্ভরযোগ্য নয়।

## ১৯. Troubleshooting

### MQTT `state=5`

অর্থ: authentication failed। পরীক্ষা করুন:

- Placeholder `YOUR_CLUSTER` পরিবর্তন হয়েছে কি না
- Host-এ `mqtts://` বা `:8883` দেওয়া হয়নি তো
- Username/password সঠিক কি না
- HiveMQ permission `Publish and Subscribe` কি না

### Website ESP8266 offline দেখায়

- ESP Serial-এ `[MQTT] ONLINE` আছে কি না
- Render backend-এ MQTT environment variables আছে কি না
- Firmware ও Render-এর `MQTT_DEVICE_ID` একই কি না
- Backend `/api/device-status` response দেখুন

### Command আসে, servo নড়ে না

Serial-এ command দেখা গেলে software/MQTT ঠিক। তখন পরীক্ষা করুন:

- External 5V servo supply
- Common ground
- Signal pin D1/D2/D5
- Servo connector polarity
- Supply current capacity
- অন্য servo দিয়ে test

### Button চাপলেও Serial-এ MQTT command নেই

- Frontend `VITE_API_URL` সঠিক কি না
- Backend `/api/servo` response
- Render backend MQTT broker-connected log
- Browser developer console/network error

### Render `Not Found`

- Backend latest Git commit deploy হয়েছে কি না
- Backend service type Web Service কি না
- Root Directory `backend` কি না
- `/api/health` endpoint পরীক্ষা করুন

## ২০. Security

- Wi-Fi/MQTT password কখনো GitHub-এ commit করবেন না।
- প্রকাশিত password সঙ্গে সঙ্গে rotate করুন।
- `secrets.h` Git-ignored রাখুন।
- Render Environment-এ secret value রাখুন।
- Firmware বর্তমানে TLS encryption-এর জন্য `setInsecure()` ব্যবহার করে, অর্থাৎ server certificate যাচাই করে না। Production-এর জন্য HiveMQ broker CA certificate দিয়ে `setTrustAnchors()` ব্যবহার করা উচিত।
- ESP8266 HTTP endpoint local network-এ খোলা; untrusted network-এ ব্যবহার করবেন না।

## ২১. Live services

Frontend:

```text
https://esp32-robotics-arm-controll-bymediapipe.onrender.com
```

Backend:

```text
https://esp32-robotics-backend.onrender.com
```

Backend health:

```text
https://esp32-robotics-backend.onrender.com/api/health
```

## ২২. বর্তমান verified state

শেষ live check-এ পাওয়া গেছে:

```text
Backend health: ONLINE
MQTT broker: ONLINE
ESP8266: ONLINE
Transport: MQTT
Servo positions: Base=90, Shoulder=90, Elbow=90
```

এই document-এ কোনো বাস্তব password বা secret রাখা হয়নি।
