/*
 * ============================================
 * IoT Smart Room - ESP32-CAM Configuration
 * Board: ESP32-CAM (Ai-Thinker)
 * ============================================
 */

#ifndef CAM_CONFIG_H
#define CAM_CONFIG_H

// WiFi
#define WIFI_SSID "Q"
#define WIFI_PASSWORD "131120031"

// mDNS
#define MDNS_HOSTNAME "esp32-cam"

// MQTT
#define MQTT_SERVER "172.20.10.2"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "esp32_cam"

// MQTT Topics
#define TOPIC_CAM_STATUS "iot/cam/status"
#define TOPIC_CAM_FRAME "iot/cam/frame"
#define TOPIC_DOOR_COMMAND "iot/door/command" // Subscribe: UNLOCK/LOCK
#define TOPIC_DOOR_STATUS "iot/door/status"   // Publish: door events
#define TOPIC_ALERT "iot/alert/event"

// Camera Pins (Ai-Thinker ESP32-CAM)
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

// Flash LED & Relay
#define PIN_FLASH_LED 4   // Onboard flash LED
#define PIN_DOOR_RELAY 12 // Door lock relay

// Door timing
#define DOOR_UNLOCK_TIME 5000 // Keep door unlocked for 5 seconds

// Stream settings
#define STREAM_PORT 81
#define FRAME_SIZE FRAMESIZE_VGA // 640x480

#endif // CAM_CONFIG_H
