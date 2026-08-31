# React Robot Arm Vision Control

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

## Render deployment

Root-এর `render.yaml` Blueprint দুইটি service বানায়:

- `frontend/`: Render Static Site
- `backend/`: Render Node Web Service

Render Dashboard-এ **New → Blueprint** থেকে GitHub repository connect করুন।

Render cloud local `192.168.x.x` mobile camera, ESP8266 বা ESP32-CAM address access করতে পারে না। Hosted physical control-এর জন্য public HTTPS tunnel, MQTT cloud, অথবা local bridge প্রয়োজন। Local hardware control-এর জন্য computer-এ backend চালানো বর্তমান কার্যকর mode।

Render configuration সম্পর্কে বিস্তারিত: [Blueprint spec](https://render.com/docs/blueprint-spec) এবং [monorepo support](https://render.com/docs/monorepo-support)।

## Manual control

Dashboard-এর Base, Shoulder ও Elbow slider দিয়ে `0–180°` angle পাঠানো যায়। ESP8266 connected থাকলেই `Move servos` button চালু হয়।
