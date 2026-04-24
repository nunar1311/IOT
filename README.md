# 🏠 IoT Smart Room Project

Dự án **Hệ thống Phòng thông minh IoT** tích hợp AI — nhận diện khuôn mặt, phát hiện bất thường môi trường, cảnh báo Telegram, và dashboard real-time.

---

## 📊 Tính năng nổi bật

1. **Giám sát Môi trường Real-time**: Nhiệt độ, độ ẩm, nồng độ bụi PM1.0/PM2.5/PM10, khí gas (MQ-2), CO (MQ-7), và mức độ rung (Piezo).
2. **Dashboard Web (Next.js 16 + React 19)**: Giao diện hiện đại, cập nhật dữ liệu qua **WebSocket**. Hỗ trợ điều khiển relay, motor, và LED trực tiếp.
3. **AI Nhận diện khuôn mặt (ESP32-CAM + OpenCV SFace)**:
   - Tự động mở cửa khi nhận ra khuôn mặt trong database.
   - Lưu ảnh và cảnh báo Telegram + TTS khi phát hiện người lạ.
4. **AI Anomaly Detection**: Phát hiện bất thường bằng mô hình **TensorFlow Autoencoder** (fallback sang Z-score thống kê). Cảnh báo Telegram khi phát hiện sự cố.
5. **Text-to-Speech (TTS)**: Phát cảnh báo giọng nói qua `gTTS` + `pyttsx3` khi có sự kiện khẩn cấp.
6. **Telegram Bot Integration**: Gửi ảnh + văn bản cảnh báo về người lạ, nhiệt độ cao, gas, CO, v.v.
7. **Node-RED Flow Automation**: Cho phép cấu hình rule tự động linh hoạt (chạy qua Docker).
8. **Màn hình OLED 1.3"**: Hiển thị trạng thái môi trường và bảo mật trực tiếp tại board điều khiển.

---

## 🛠 Phần cứng yêu cầu

| Thiết bị | Số lượng | Ghi chú |
| :--- | :---: | :--- |
| ESP32 NodeMCU-32S | 1 | Vi điều khiển trung tâm |
| ESP32-CAM + OV2640 | 1 | Nhận diện khuôn mặt & mở cửa |
| DHT11 | 1 | Nhiệt độ & Độ ẩm |
| MQ-2 | 1 | Cảm biến khí Gas (5V) |
| MQ-7 | 1 | Cảm biến CO (5V) |
| APM10 (ASAIR) | 1 | Bụi PM1.0/PM2.5/PM10 (UART 1200bps) |
| PIR | 2+ | Cảm biến chuyển động (Phòng 1, 2, ...) |
| Piezo Vibration | 1 | Cảm biến rung/va chạm (ADC) |
| Module 4-Relay | 1 | Điều khiển đèn, quạt, còi |
| Module 1-Relay KY-019 | 1 | Đèn mờ (PWM) |
| Động cơ bước 28BYJ-48 + ULN2003 | 1 | Cơ cấu đóng/mở cửa |
| OLED 1.3" (I2C) | 1 | Màn hình hiển thị board |
| LED (Đỏ, Xanh, Vàng) | 3+ | Đèn báo trạng thái & cảnh báo |

> Chi tiết đấu nối chân xem tại **[PINOUT.md](./PINOUT.md)**.

---

## 📂 Cấu trúc dự án

```
IOT/
├── firmware/
│   ├── esp32_main/         # Arduino C++ — ESP32 NodeMCU-32S (Cảm biến & Điều khiển)
│   │   ├── config.h        # Cấu hình WiFi, MQTT, Pin definitions, Thresholds
│   │   ├── esp32_main.ino  # Entry point
│   │   ├── sensors.cpp/h   # Đọc DHT11, MQ-2, MQ-7, APM10, PIR, Piezo
│   │   ├── actuators.cpp/h # Điều khiển Relay, Motor, LED
│   │   ├── display.cpp/h   # Màn hình OLED
│   │   └── mqtt_handler.cpp/h # Publish/Subscribe MQTT
│   └── esp32_cam/          # Arduino C++ — ESP32-CAM (Camera & Mở cửa)
│       ├── config.h        # Cấu hình Camera, WiFi, MQTT
│       └── esp32_cam.ino   # Stream + MQTT door control
│
├── server/                 # Python FastAPI — Backend & AI Core
│   ├── main.py             # Entry point & lifespan management
│   ├── config.py           # Cấu hình tập trung từ .env
│   ├── database.py         # MongoDB client (pymongo)
│   ├── mqtt_client.py      # MQTT broker client (paho-mqtt)
│   ├── api/
│   │   └── app.py          # FastAPI routes, WebSocket manager
│   ├── models/
│   │   ├── face_recognizer.py          # OpenCV SFace nhận diện khuôn mặt
│   │   ├── anomaly_detector.py         # TF Autoencoder / Z-score
│   │   ├── face_detection_yunet_2023mar.onnx
│   │   └── face_recognition_sface_2021dec.onnx
│   ├── services/
│   │   ├── alert_service.py    # Orchestrator cảnh báo (MQTT + Telegram + WS)
│   │   ├── data_processor.py   # Xử lý dữ liệu cảm biến từ MQTT
│   │   ├── telegram_service.py # Telegram Bot (gửi ảnh + text)
│   │   └── tts_service.py      # Text-to-Speech (gTTS / pyttsx3)
│   ├── faces_db/           # Thư mục chứa ảnh khuôn mặt đã đăng ký
│   └── requirements.txt    # Python dependencies
│
├── web_app/                # Next.js 16 + React 19 — Dashboard Frontend
│   ├── src/
│   │   ├── app/            # App Router (Next.js)
│   │   ├── components/     # UI Components (Dashboard, Charts, Panels...)
│   │   ├── contexts/       # React Context (WebSocket, Fullscreen...)
│   │   └── lib/            # Utilities, hooks
│   ├── .env.local          # Cấu hình frontend (NEXT_PUBLIC_API_URL...)
│   └── package.json        # Node dependencies (Next.js, Recharts, shadcn...)
│
├── mongo-init/             # Scripts khởi tạo MongoDB (collections, indexes)
├── mosquitto/              # Cấu hình Mosquitto MQTT Broker
├── nodered/                # Export flows Node-RED
├── docker-compose.yml      # Docker: Mosquitto + MongoDB + Node-RED
├── .env                    # Biến môi trường chính (MQTT, Mongo, Telegram...)
├── PINOUT.md               # Sơ đồ đấu nối chân phần cứng chi tiết
└── README.md               # Tài liệu này
```

---

## 🚀 Hướng dẫn triển khai

### Điều kiện tiên quyết
- **Docker & Docker Compose** (cho Mosquitto, MongoDB, Node-RED)
- **Python 3.10+** (cho backend AI)
- **Node.js 18+ & pnpm/npm** (cho web dashboard)
- **Arduino IDE 2.x** (để nạp firmware ESP32)

---

### 1. Cấu hình biến môi trường

Sao chép và chỉnh sửa file `.env` tại thư mục gốc:

```bash
# Telegram Bot (lấy token tại @BotFather, chat_id tại @userinfobot)
TELEGRAM_BOT_TOKEN=your_bot_token_here
TELEGRAM_CHAT_ID=your_chat_id_here

# IP của máy chạy server (ESP32 cần kết nối đến đây)
MQTT_BROKER_HOST=192.168.1.x
```

---

### 2. Khởi động các Docker Service

```bash
# Khởi động MQTT Broker, MongoDB, và Node-RED
docker-compose up -d

# Kiểm tra trạng thái
docker-compose ps
```

- **Mosquitto MQTT**: `localhost:1883`
- **MongoDB**: `localhost:27017`
- **Node-RED UI**: `http://localhost:1880`

---

### 3. Khởi động Python Backend

```bash
cd server

# Tạo và kích hoạt môi trường ảo
python -m venv venv
venv\Scripts\activate          # Windows
# source venv/bin/activate     # Linux/macOS

# Cài đặt dependencies
pip install -r requirements.txt

# Khởi chạy server
python main.py
```

Server sẽ chạy tại: `http://localhost:8000`
- **API Docs**: `http://localhost:8000/docs`
- **WebSocket**: `ws://localhost:8000/ws`

---

### 4. Khởi động Web Dashboard (Frontend)

```bash
cd web_app

# Cài đặt dependencies
pnpm install        # hoặc npm install

# Cấu hình kết nối API (tùy chỉnh nếu cần)
# Xem file .env.local

# Chạy development server
pnpm dev            # hoặc npm run dev
```

Dashboard sẽ chạy tại: `http://localhost:3000`

---

### 5. Nạp Firmware lên ESP32

Sử dụng [Arduino IDE 2.x](https://www.arduino.cc/en/software):

1. **Cài đặt Board**: Thêm ESP32 board manager URL vào Arduino IDE.
2. **Cài đặt Libraries**:
   - `DHT sensor library` (Adafruit)
   - `Adafruit SSD1306` + `Adafruit SH110X` (OLED)
   - `PubSubClient` (MQTT)
   - `ArduinoJson`
3. **Cấu hình**: Chỉnh sửa `config.h` trong mỗi firmware:
   - `WIFI_SSID` / `WIFI_PASSWORD`
   - `MQTT_SERVER` → IP của máy đang chạy server
4. **Nạp code**:
   - Nạp `firmware/esp32_main/` vào **ESP32 NodeMCU-32S**
   - Nạp `firmware/esp32_cam/` vào **ESP32-CAM** (dùng FTDI adapter)

---

## 🔔 Cấu hình Telegram Bot

1. Nhắn tin với `@BotFather` trên Telegram → tạo bot mới → lấy **Bot Token**.
2. Nhắn tin với bot của bạn, sau đó chạy:
   ```bash
   cd server
   python get_chat_id.py   # Script tự động lấy Chat ID
   ```
3. Điền `TELEGRAM_BOT_TOKEN` và `TELEGRAM_CHAT_ID` vào file `.env`.

---

## 📡 MQTT Topics

| Topic | Hướng | Mô tả |
| :--- | :---: | :--- |
| `iot/sensors/env` | ESP32 → Server | Nhiệt độ, độ ẩm |
| `iot/sensors/air` | ESP32 → Server | Chất lượng không khí (PM, Gas, CO) |
| `iot/sensors/motion` | ESP32 → Server | Phát hiện chuyển động PIR |
| `iot/alert/event` | ESP32 → Server | Sự kiện cảnh báo từ firmware |
| `iot/alert/trigger` | Server → ESP32 | Kích hoạt còi/LED báo động |
| `iot/control/relay` | Server → ESP32 | Điều khiển relay |
| `iot/control/motor` | Server → ESP32 | Điều khiển stepper motor |
| `iot/control/led` | Server → ESP32 | Điều khiển LED |
| `iot/door/command` | Server → CAM | Lệnh LOCK/UNLOCK cửa |
| `iot/door/status` | CAM → Server | Trạng thái cửa |
| `iot/tts/play` | Server → ESP32 | Phát thông báo TTS |

