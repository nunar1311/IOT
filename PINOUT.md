# Sơ đồ kết nối phần cứng - IoT Smart Room

Tài liệu mô tả chi tiết sơ đồ đấu nối chân (pinout) giữa **ESP32 NodeMCU-32S** (board điều khiển trung tâm) và tất cả các cảm biến, actuator, thiết bị ngoại vi trong hệ thống.

> Mọi giá trị PIN đều được lấy từ file cấu hình tại `firmware/esp32_main/config.h`.

---

## 1. Màn hình OLED 1.3" (Giao tiếp I2C)

| Thiết bị | Chân Module | Chân ESP32 | Loại tín hiệu | Ghi chú |
| :--- | :--- | :--- | :--- | :--- |
| **OLED 1.3" (SSD1306/SH110X)** | VCC | 3.3V | Nguồn | Địa chỉ I2C: `0x3C` |
| | GND | GND | Đất | |
| | SCL | **GPIO 22** | I2C Clock | `PIN_OLED_SCL` |
| | SDA | **GPIO 21** | I2C Data | `PIN_OLED_SDA` |

---

## 2. Cảm biến môi trường (Sensors)

| Thiết bị | Chân Module | Chân ESP32 | Loại tín hiệu | Ghi chú |
| :--- | :--- | :--- | :--- | :--- |
| **Nhiệt độ & Độ ẩm (DHT11)** | DATA | **GPIO 15** | Digital (1-Wire) | `PIN_DHT11` |
| **Cảm biến khí Gas (MQ-2)** | A0 | **GPIO 34** | Analog (ADC1) | `PIN_MQ2_AO` — Cấp nguồn **5V** cho module |
| **Cảm biến CO (MQ-7)** | A0 | **GPIO 35** | Analog (ADC1) | `PIN_MQ7_AO` — Cấp nguồn **5V** cho module |
| **Cảm biến bụi APM10 (ASAIR)** | TX | **GPIO 16** | UART2 RX | `PIN_APM10_RX` — Baud 1200bps, protocol frame `0xFE` |
| | RX | **GPIO 17** | UART2 TX | `PIN_APM10_TX` |
| **PIR Motion — Phòng 1** | OUT | **GPIO 27** | Digital Input | `PIN_PIR_ROOM1` |
| **PIR Motion — Phòng 2** | OUT | **GPIO 26** | Digital Input | `PIN_PIR_ROOM2` |
| **Cảm biến Rung Piezo** | A0 | **GPIO 36** (VP) | Analog (ADC1) | `PIN_PIEZO` — Input-only pin, ngưỡng: 500 ADC |

---

## 3. Điều khiển Relay (Actuators)

| Thiết bị | Chân Module | Chân ESP32 | Loại tín hiệu | Chức năng |
| :--- | :--- | :--- | :--- | :--- |
| **Module 4-Relay** | IN1 | **GPIO 25** | Digital Output | `PIN_RELAY1` — Đèn chính Phòng 1 |
| | IN2 | **GPIO 33** | Digital Output | `PIN_RELAY2` — Quạt (làm mát / thông gió) |
| | IN3 | **GPIO 32** | Digital Output | `PIN_RELAY3` — Đèn chính Phòng 2 |
| | IN4 | **GPIO 14** | Digital Output | `PIN_RELAY4` — Dự trữ / Đèn Phòng 3 |
| **Module 1-Relay KY-019** | IN | **GPIO 13** | PWM Output | `PIN_RELAY_DIM` — Đèn mờ điều chỉnh độ sáng |

---

## 4. Động cơ bước (Stepper Motor 28BYJ-48 qua ULN2003)

| Thiết bị | Chân Module | Chân ESP32 | Loại tín hiệu | Ghi chú |
| :--- | :--- | :--- | :--- | :--- |
| **Module ULN2003** | IN1 | **GPIO 19** | Digital Output | `PIN_STEPPER_IN1` |
| | IN2 | **GPIO 18** | Digital Output | `PIN_STEPPER_IN2` |
| | IN3 | **GPIO 5** | Digital Output | `PIN_STEPPER_IN3` |
| | IN4 | **GPIO 4** | Digital Output | `PIN_STEPPER_IN4` |

> **Thông số động cơ**: 2048 bước/vòng (`STEPPER_STEPS_REV`), tốc độ 10 RPM (`STEPPER_SPEED_RPM`).

---

## 5. Đèn báo hiệu (LED Indicators)

| Thiết bị | Chân Module | Chân ESP32 | Loại tín hiệu | Ghi chú |
| :--- | :--- | :--- | :--- | :--- |
| **LED Đỏ — Phòng 1** | Anode (+) | **GPIO 2** | Digital Output | `PIN_LED_R1_RED` — Báo DANGER *(trùng với Built-in LED)* |
| **LED Xanh — Phòng 1** | Anode (+) | **GPIO 12** | Digital Output | `PIN_LED_R1_GREEN` — Trạng thái bình thường |
| **LED Vàng — Phòng 1** | Anode (+) | **GPIO 23** | Digital Output | `PIN_LED_R1_YELLOW` — Cảnh báo WARNING |

> ⚠️ Nối **điện trở 220–330 Ω** nối tiếp với cực Anode (+) của mỗi LED. Cực Cathode (−) nối GND.

---

## 6. ESP32-CAM — Nhận diện khuôn mặt & Mở cửa

ESP32-CAM hoạt động như một **node độc lập**, giao tiếp với server qua **MQTT** và kết nối trực tiếp với relay khóa cửa.

> Cấu hình pin từ `firmware/esp32_cam/config.h`.

| Thiết bị | Tín hiệu / Chân Module | Chân ESP32-CAM | Loại tín hiệu | Ghi chú |
| :--- | :--- | :--- | :--- | :--- |
| **Relay Khóa cửa** | IN | **GPIO 12** | Digital Output | `PIN_DOOR_RELAY` — Mở chốt điện từ 5 giây |
| **Flash LED** | Onboard LED | **GPIO 4** | Digital Output | `PIN_FLASH_LED` — Trợ sáng camera |
| **Camera OV2640** | PWDN | **GPIO 32** | Control | `PWDN_GPIO_NUM` — Quản lý nguồn camera |
| | RESET | **NC (−1)** | Control | Không dùng |
| | XCLK | **GPIO 0** | External Clock | `XCLK_GPIO_NUM` |
| | SIOD | **GPIO 26** | I2C (SCCB) | `SIOD_GPIO_NUM` — Cấu hình camera |
| | SIOC | **GPIO 27** | I2C (SCCB) | `SIOC_GPIO_NUM` |
| | Y9 / Y8 / Y7 / Y6 | **GPIO 35 / 34 / 39 / 36** | Data Bus | Dữ liệu hình ảnh |
| | Y5 / Y4 / Y3 / Y2 | **GPIO 21 / 19 / 18 / 5** | Data Bus | Dữ liệu hình ảnh |
| | VSYNC | **GPIO 25** | Sync | `VSYNC_GPIO_NUM` |
| | HREF | **GPIO 23** | Sync | `HREF_GPIO_NUM` |
| | PCLK | **GPIO 22** | Pixel Clock | `PCLK_GPIO_NUM` |

> **Stream**: Cổng `:81` (VGA 640×480). **HTTP Capture**: Cổng `:80/capture`.

---

## 7. Tổng hợp bản đồ GPIO — ESP32 NodeMCU-32S

| GPIO | Chức năng | Loại | Ghi chú |
| :---: | :--- | :--- | :--- |
| 2 | LED Đỏ (Báo động) | Output | Trùng Built-in LED |
| 4 | Stepper IN4 | Output | |
| 5 | Stepper IN3 | Output | |
| 12 | LED Xanh (Status) | Output | ⚠️ Boot fail nếu HIGH lúc khởi động |
| 13 | Relay Dim (PWM) | PWM Output | |
| 14 | Relay 4 (Dự trữ) | Output | |
| 15 | DHT11 Data | Input | |
| 16 | APM10 RX (UART2) | UART RX | |
| 17 | APM10 TX (UART2) | UART TX | |
| 18 | Stepper IN2 | Output | |
| 19 | Stepper IN1 | Output | |
| 21 | OLED SDA (I2C) | I2C Data | |
| 22 | OLED SCL (I2C) | I2C Clock | |
| 23 | LED Vàng (Warning) | Output | |
| 25 | Relay 1 (Đèn Phòng 1) | Output | |
| 26 | PIR Phòng 2 | Input | |
| 27 | PIR Phòng 1 | Input | |
| 32 | Relay 3 (Đèn Phòng 2) | Output | |
| 33 | Relay 2 (Quạt) | Output | |
| 34 | MQ-2 Analog | ADC (Input only) | |
| 35 | MQ-7 Analog | ADC (Input only) | |
| 36 (VP) | Piezo Vibration | ADC (Input only) | |

---

## ⚡ Lưu ý về Nguồn điện

- **MQ-2, MQ-7**: Yêu cầu **5V** cho heater — sử dụng chân `VIN` của ESP32 (nếu cấp qua USB) hoặc nguồn 5V ngoài.
- **Module Relay 4 kênh**: Nên cấp nguồn **5V** riêng khi điều khiển tải lớn (đèn, quạt).
- **Động cơ bước 28BYJ-48**: Cấp **5V** từ nguồn riêng hoặc `VIN` — **không** lấy trực tiếp từ 3.3V.
- **ESP32 Logic Level**: Các chân GPIO hoạt động ở **3.3V** — không cấp trực tiếp tín hiệu 5V vào GPIO (cần level shifter nếu cần thiết).
- **GPIO 12**: Cẩn thận khi dùng — nếu HIGH lúc boot, ESP32 sẽ lỗi flash mode.

---

## 🔌 Sơ đồ giao tiếp tổng quan

```
┌─────────────────────────────────────────────────────────────────┐
│                     ESP32 NodeMCU-32S                           │
│                                                                 │
│  Cảm biến:                    Actuator:                         │
│  DHT11 ─── GPIO 15            Relay 1-4 ─── GPIO 25/33/32/14   │
│  MQ-2  ─── GPIO 34            Relay Dim  ─── GPIO 13 (PWM)     │
│  MQ-7  ─── GPIO 35            Stepper    ─── GPIO 19/18/5/4    │
│  APM10 ─── GPIO 16/17 (UART) LED R/G/Y  ─── GPIO 2/12/23      │
│  PIR1  ─── GPIO 27                                             │
│  PIR2  ─── GPIO 26            Display:                         │
│  Piezo ─── GPIO 36            OLED I2C   ─── GPIO 21/22        │
│                                                                 │
│  WiFi ──────────────────── MQTT Broker (Mosquitto :1883)        │
└─────────────────────────────────────────────────────────────────┘
                              │ MQTT
┌─────────────────────────────────────────────────────────────────┐
│                     ESP32-CAM (Node riêng)                      │
│  Camera OV2640 ─── Onboard  │  Relay cửa ─── GPIO 12           │
│  Flash LED     ─── GPIO 4   │  Stream    ─── :81 (HTTP)         │
│  WiFi ─────────────────────── MQTT Broker                       │
└─────────────────────────────────────────────────────────────────┘
```
