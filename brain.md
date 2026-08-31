# Project Brain

## উদ্দেশ্য

একই dashboard-এ mobile IP camera দিয়ে MediaPipe finger detection, ESP8266 robot arm control এবং ESP32-CAM monitoring।

## Folder ownership

- `frontend/`: React components, MediaPipe processing, UI ও Vite configuration
- `backend/`: Express server, camera proxy, ESP8266 proxy ও production hosting
- `firmware/`: ESP8266 Arduino sketch

## Data flow

```text
Android IP Webcam (/video বা /shot.jpg)
        ↓
Node proxy: /api/mobile-frame
        ↓
React + MediaPipe HandLandmarker
        ↓ finger count 0–5
Node proxy: /api/control?pattern=1,0,0,0,0&dir=right
        ↓
ESP8266 /control?pattern=...&dir=... → servos

ESP32-CAM :81/stream → React monitoring panel
```

## কেন backend proxy আছে

Browser অন্য IP-এর MJPEG feed থেকে MediaPipe চালালে CORS ও canvas security সমস্যা হয়। `server.js` mobile camera থেকে JPEG নিয়ে same-origin `/api/mobile-frame` endpoint দেয়। IP Webcam-এর `/video` URL দিলে server সেটিকে স্বয়ংক্রিয়ভাবে `/shot.jpg` করে। ESP8266 command-ও proxy দিয়ে যায়।

## দরকারি URL

- Mobile IP Webcam: `http://PHONE_IP:8080/video`
- ESP8266: `http://ESP8266_IP`
- ESP32-CAM: `http://ESP32_IP:81/stream`
- Dashboard: `http://COMPUTER_IP:5000`

সব device একই Wi-Fi-তে থাকতে হবে। Guest Wi-Fi/AP isolation চালু থাকলে device একে অন্যকে দেখতে পারবে না।

## Gesture protocol

Pattern-এর order হলো `thumb,index,middle,ring,pinky`; `1` মানে open এবং `0` মানে closed। Wrist landmark screen-এর 40%-এর বাঁ পাশে থাকলে `left`, 60%-এর ডান পাশে থাকলে `right`, মাঝখানে থাকলে `center`। ESP8266 HTTP এবং USB Serial—দুইভাবেই একই gesture গ্রহণ করে। Serial example: `0,0,0,0,0,right`।

## Device health check

Frontend প্রতি ৩ সেকেন্ডে backend-এর `/api/device-status?controller=...` endpoint call করে। Backend ESP8266-এর `/` endpoint সর্বোচ্চ ১.৫ সেকেন্ড অপেক্ষা করে পরীক্ষা করে। Response body-তে `Robot arm controller online` পাওয়া গেলে dashboard `CONNECTED`, অন্যথায় `NOT CONNECTED` দেখায়।

## Manual servo API

Dashboard-এর Base, Shoulder ও Elbow slider `0–180°` value নেয়। `Move servos` backend-এর `/api/servo` call করে এবং backend ESP8266-এর `/servo?base=90&shoulder=90&elbow=90` endpoint-এ পাঠায়। ESP8266 smooth movement শেষে JSON response দেয়। Button কেবল device connected থাকলে enabled হয়।

## MQTT cloud transport

Backend-এ `MQTT_URL` থাকলে HTTP LAN transport-এর বদলে MQTT ব্যবহার হয়। Gesture payload `gesture|0,1,0,0,0|right` এবং manual payload `servo|90|90|90` হিসেবে `robot-arm/<device-id>/command` topic-এ QoS 1-এ publish হয়। ESP8266 `robot-arm/<device-id>/status` topic-এ retained online state ও heartbeat publish করে। Heartbeat দেরি হলে offline হয় না; ESP8266 সত্যিই disconnect হলে broker-এর retained Last Will status offline দেখায়। `MQTT_URL` না থাকলে local HTTP fallback আগের মতো কাজ করে।

## চালানোর নিয়ম

```powershell
npm run install:all
npm run build
npm start
```

Dashboard Settings-এ URL বসিয়ে camera source নির্বাচন করে `Start camera` চাপুন। Development-এর সময় আলাদা terminal-এ `npm run dev:backend` এবং `npm run dev:frontend` চালাতে হবে।

## সমস্যা পরীক্ষা

Computer browser-এ আগে `http://PHONE_IP:8080/shot.jpg` খুলুন। ছবি না এলে IP Webcam app-এর `Start server`, একই network এবং Windows firewall পরীক্ষা করুন।

## Hardware safety

Servo-তে external regulated 5V supply দিন। ESP8266 3.3V pin থেকে servo চালাবেন না। Servo supply এবং ESP8266-এর ground common রাখুন।
