/*
 * ============================================
 * IoT Smart Room - ESP32-CAM
 * Camera Streaming + Door Lock via MQTT
 * ============================================
 * Board: ESP32-CAM (Ai-Thinker)
 * 
 * Features:
 * - MJPEG video streaming on port 81
 * - Capture endpoint for face recognition
 * - MQTT control for door lock relay
 * - Flash LED control
 * 
 * Required Libraries:
 * - PubSubClient
 * - ArduinoJson
 * - ESP32 Camera (built-in with ESP32 board)
 * ============================================
 */

#include "esp_camera.h"
#include "esp_http_server.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================
// Globals
// ============================================
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

unsigned long doorUnlockTime = 0;
bool doorLocked = true;

// ============================================
// Camera Initialization
// ============================================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  
  // Use PSRAM if available 
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;   // 640x480
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    Serial.println("[CAM] PSRAM found, using high quality");
  } else {
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 15;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    Serial.println("[CAM] No PSRAM, using low quality");
  }
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] ⚠ Init failed: 0x%x\n", err);
    return false;
  }
  
  // Adjust camera settings
  sensor_t *s = esp_camera_sensor_get();
  s->set_brightness(s, 1);     // Brightness
  s->set_contrast(s, 1);       // Contrast
  s->set_saturation(s, 0);     // Saturation
  s->set_vflip(s, 0);          // Vertical flip
  s->set_hmirror(s, 0);        // Horizontal mirror
  
  Serial.println("[CAM] Camera initialized ✓");
  return true;
}

// ============================================
// MJPEG Stream Handler
// ============================================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];
  
  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[STREAM] Frame capture failed");
      res = ESP_FAIL;
      break;
    }
    
    size_t hlen = snprintf(part_buf, 64, STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    
    esp_camera_fb_return(fb);
    
    if (res != ESP_OK) break;
    
    delay(30); // ~30 FPS cap
  }
  
  return res;
}

// ============================================
// Single Capture Handler (for face recognition)
// ============================================
esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  
  return res;
}

// ============================================
// Status Handler
// ============================================
esp_err_t status_handler(httpd_req_t *req) {
  StaticJsonDocument<256> doc;
  doc["device"] = MQTT_CLIENT_ID;
  doc["stream_url"] = "http://" + WiFi.localIP().toString() + ":81/stream";
  doc["capture_url"] = "http://" + WiFi.localIP().toString() + "/capture";
  doc["door_locked"] = doorLocked;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  
  char buffer[256];
  serializeJson(doc, buffer);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, buffer, strlen(buffer));
}

// ============================================
// Start HTTP Servers
// ============================================
void startStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = STREAM_PORT;
  config.ctrl_port = STREAM_PORT + 1;
  
  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };
  
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.printf("[HTTP] Stream server started on port %d\n", STREAM_PORT);
  }
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  
  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
  };
  
  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
  };
  
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    Serial.println("[HTTP] Camera server started on port 80");
  }
}

// ============================================
// Door Lock Control
// ============================================
void unlockDoor() {
  digitalWrite(PIN_DOOR_RELAY, HIGH);
  doorLocked = false;
  doorUnlockTime = millis();
  Serial.println("[DOOR] 🔓 Door UNLOCKED");
  
  // Brief flash to indicate unlock
  digitalWrite(PIN_FLASH_LED, HIGH);
  delay(100);
  digitalWrite(PIN_FLASH_LED, LOW);
}

void lockDoor() {
  digitalWrite(PIN_DOOR_RELAY, LOW);
  doorLocked = true;
  Serial.println("[DOOR] 🔒 Door LOCKED");
}

// ============================================
// MQTT Callback
// ============================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String payloadStr = "";
  for (unsigned int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }
  
  Serial.println("[MQTT] Received: " + topicStr + " => " + payloadStr);
  
  if (topicStr == TOPIC_DOOR_COMMAND) {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, payloadStr);
    
    String command = doc["command"] | "";
    String name = doc["name"] | "";
    
    if (command == "UNLOCK") {
      unlockDoor();
      
      // Publish event
      StaticJsonDocument<128> event;
      event["event"] = "access_granted";
      event["name"] = name;
      event["timestamp"] = millis();
      char buffer[128];
      serializeJson(event, buffer);
      mqttClient.publish(TOPIC_DOOR_STATUS, buffer);
      
    } else if (command == "LOCK") {
      lockDoor();
      
      StaticJsonDocument<128> event;
      event["event"] = "door_locked";
      event["timestamp"] = millis();
      char buffer[128];
      serializeJson(event, buffer);
      mqttClient.publish(TOPIC_DOOR_STATUS, buffer);
    }
  }
}

// ============================================
// MQTT Connection
// ============================================
void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Connecting...");
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println(" connected ✓");
      mqttClient.subscribe(TOPIC_DOOR_COMMAND);
      
      // Publish cam online status
      StaticJsonDocument<256> doc;
      doc["status"] = "online";
      doc["device"] = MQTT_CLIENT_ID;
      doc["stream"] = "http://" + WiFi.localIP().toString() + ":81/stream";
      doc["capture"] = "http://" + WiFi.localIP().toString() + "/capture";
      char buffer[256];
      serializeJson(doc, buffer);
      mqttClient.publish(TOPIC_CAM_STATUS, buffer, true);
    } else {
      Serial.print(" failed (rc=");
      Serial.print(mqttClient.state());
      Serial.println("), retrying in 5s...");
      delay(5000);
    }
  }
}

// ============================================
// WiFi Connection
// ============================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("[WiFi] Connected ✓ IP: " + WiFi.localIP().toString());
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("  IoT Smart Room - ESP32-CAM");
  Serial.println("========================================\n");
  
  // GPIO setup
  pinMode(PIN_FLASH_LED, OUTPUT);
  pinMode(PIN_DOOR_RELAY, OUTPUT);
  digitalWrite(PIN_FLASH_LED, LOW);
  digitalWrite(PIN_DOOR_RELAY, LOW);
  
  // Initialize camera
  if (!initCamera()) {
    Serial.println("[FATAL] Camera init failed! Restarting...");
    ESP.restart();
  }
  
  // Connect WiFi
  connectWiFi();
  
  // mDNS
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.println("[mDNS] Hostname: " + String(MDNS_HOSTNAME) + ".local");
  }
  
  // Start HTTP servers
  startCameraServer();
  startStreamServer();
  
  // MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  connectMQTT();
  
  Serial.println("\n========================================");
  Serial.printf("  Stream: http://%s:81/stream\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Capture: http://%s/capture\n", WiFi.localIP().toString().c_str());
  Serial.println("========================================\n");
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  // MQTT
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();
  
  // Auto-lock door after timeout
  if (!doorLocked && millis() - doorUnlockTime > DOOR_UNLOCK_TIME) {
    lockDoor();
    
    StaticJsonDocument<128> event;
    event["event"] = "auto_locked";
    event["timestamp"] = millis();
    char buffer[128];
    serializeJson(event, buffer);
    mqttClient.publish(TOPIC_DOOR_STATUS, buffer);
  }
  
  // Periodic status report
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 60000) {
    lastStatus = millis();
    
    StaticJsonDocument<256> doc;
    doc["status"] = "online";
    doc["device"] = MQTT_CLIENT_ID;
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["door_locked"] = doorLocked;
    char buffer[256];
    serializeJson(doc, buffer);
    mqttClient.publish(TOPIC_CAM_STATUS, buffer);
  }
  
  delay(10);
}
