/*
 * ============================================
 * IoT Smart Room - ESP32 Main Controller
 * Configuration Header
 * ============================================
 * Board: ESP32 NodeMCU-32S (Ai-Thinker)
 * ============================================
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// WiFi Configuration
// ============================================
#define WIFI_SSID "Q"
#define WIFI_PASSWORD "131120031"
#define WIFI_RETRY_DELAY 5000
#define WIFI_MAX_RETRIES 20

// ============================================
// mDNS Configuration
// ============================================
#define MDNS_HOSTNAME "esp32-main"

// ============================================
// MQTT Configuration
// ============================================
#define MQTT_SERVER "172.20.10.2" // IP of Python server
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "esp32_main"
#define MQTT_USERNAME "" // Set if auth enabled
#define MQTT_PASSWORD "" // Set if auth enabled
#define MQTT_RETRY_DELAY 5000
#define MQTT_KEEPALIVE 60

// MQTT Topics - Publish
#define TOPIC_SENSOR_ENV "iot/sensors/env"
#define TOPIC_SENSOR_MOTION "iot/sensors/motion"
#define TOPIC_SENSOR_AIR "iot/sensors/air"
#define TOPIC_SENSOR_STATUS "iot/sensors/status"
#define TOPIC_ALERT "iot/alert/event"

// MQTT Topics - Subscribe
#define TOPIC_CONTROL_RELAY "iot/control/relay"
#define TOPIC_CONTROL_MOTOR "iot/control/motor"
#define TOPIC_CONTROL_LED "iot/control/led"
#define TOPIC_ALERT_TRIGGER "iot/alert/trigger"
#define TOPIC_DOOR_STATUS "iot/door/status"
#define TOPIC_TTS_PLAY "iot/tts/play"

// ============================================
// Pin Definitions - Sensors
// ============================================
#define PIN_DHT11 15     // DHT11 data pin
#define PIN_MQ2_AO 34    // MQ-2 gas sensor (ADC)
#define PIN_MQ7_AO 35    // MQ-7 CO sensor (ADC)
#define PIN_APM10_RX 16  // APM10 UART2 RX (sensor TX)
#define PIN_APM10_TX 17  // APM10 UART2 TX (sensor RX)
#define PIN_PIR_ROOM1 27 // PIR sensor room 1
#define PIN_PIR_ROOM2 26 // PIR sensor room 2 (was Piezo)
#define PIN_PIEZO 36     // Piezo vibration sensor (ADC, input only)

// ============================================
// Pin Definitions - OLED Display (I2C)
// ============================================
#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDRESS 0x3C // Default I2C address for 1.3" OLED

// ============================================
// Pin Definitions - Relay Module (4-Relay)
// ============================================
#define PIN_RELAY1 25 // Room 1 main light
#define PIN_RELAY2 33 // Fan (cooling / ventilation)
#define PIN_RELAY3 32 // Speaker/Alarm
#define PIN_RELAY4 14 // Reserved / Room 3 light

// Pin Definitions - 1-Relay KY-019
#define PIN_RELAY_DIM 13 // Dim light (PWM capable)

// ============================================
// Pin Definitions - Stepper Motor 28BYJ-48
// ============================================
#define PIN_STEPPER_IN1 19
#define PIN_STEPPER_IN2 18
#define PIN_STEPPER_IN3 5
#define PIN_STEPPER_IN4 4
#define STEPPER_STEPS_REV 2048 // Steps per revolution (64:1 gear)
#define STEPPER_SPEED_RPM 10   // Rotation speed (Increased from 15)

// ============================================
// Pin Definitions - LED Indicators
// (3 rooms × 3 LEDs = 9 LEDs, using shift register or multiplexing)
// For direct connection, we use available GPIOs
// ============================================
#define PIN_LED_R1_RED 2     // Room 1 Red (alert) - Built-in LED
#define PIN_LED_R1_GREEN 12  // Room 1 Green (status)
#define PIN_LED_R1_YELLOW 23 // Room 1 Yellow (warning)

// Room 2 & 3 LEDs managed via shift register (74HC595) or I2C expander
// Or use the relay module outputs
#define USE_LED_DIRECT true // true = direct GPIO, false = shift register

// ============================================
// Sensor Thresholds
// ============================================
#define TEMP_WARNING 30.0  // °C - Start fan
#define TEMP_DANGER 35.0   // °C - Alert + blink
#define HUMIDITY_HIGH 80.0 // % - Warning
#define HUMIDITY_LOW 20.0  // % - Warning

#define GAS_WARNING 1000 // ADC value - Warning
#define GAS_DANGER 1200  // ADC value - Alert + blink + fan
#define CO_WARNING 200   // ADC value - Warning
#define CO_DANGER 300    // ADC value - Alert + blink + fan

#define PM25_WARNING 25 // µg/m³ - Warning
#define PM25_DANGER 35  // µg/m³ - Alert
#define PM10_WARNING 40 // µg/m³ - Warning
#define PM10_DANGER 50  // µg/m³ - Alert

#define VIBRATION_THRESHOLD 1000 // ADC value - Alert (increased to avoid false positives)

// ============================================
// Timing Configuration (milliseconds)
// ============================================
#define SENSOR_READ_INTERVAL 2000 // Read sensors every 2s
#define MQTT_PUBLISH_INTERVAL                                                  \
  2000 // Publish to MQTT every 2s (improved real-time)
#define OLED_UPDATE_INTERVAL 2000 // Update OLED every 2s (improved real-time)
#define OLED_PAGE_DURATION 3000   // Each OLED page shown for 3s
#define PIR_COOLDOWN                                                           \
  10000 // Motion light stays 10s (changed from 5 min for easy testing)
#define LED_BLINK_INTERVAL 500       // Blink speed for alerts
#define ALERT_REPEAT_INTERVAL 30000  // Re-alert every 30s
#define STATUS_REPORT_INTERVAL 60000 // System status every 60s

// ============================================
// PWM Configuration (Dim Light)
// ============================================
#define PWM_CHANNEL 0
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8 // 8-bit (0-255)
#define DIM_LEVEL 50     // ~20% brightness
#define BRIGHT_LEVEL 255 // 100% brightness

// ============================================
// APM10 UART Configuration
// ============================================
#define APM10_BAUD_RATE 1200  // ASAIR APM10 operates at 1200 bps
#define APM10_FRAME_HEAD 0xFE // ASAIR protocol frame header

#endif // CONFIG_H
