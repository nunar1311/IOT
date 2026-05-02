/*
 * ============================================
 * IoT Smart Room - ESP32 Main Controller
 * ============================================
 * Board: ESP32 NodeMCU-32S (Ai-Thinker)
 *
 * Features:
 * - Read all sensors (DHT11, MQ-2, MQ-7, APM10, PIR, Piezo)
 * - Control actuators (Relays, Stepper, LEDs)
 * - Display data on 1.3" OLED
 * - Publish/Subscribe MQTT
 * - Automatic room automation
 * ============================================
 *
 * Required Libraries (install via Arduino Library Manager):
 * - DHT sensor library by Adafruit
 * - Adafruit Unified Sensor
 * - Adafruit SH110X
 * - Adafruit GFX Library
 * - PubSubClient by Nick O'Leary
 * - ArduinoJson by Benoit Blanchon
 * - Stepper (built-in)
 * - ESPmDNS (built-in with ESP32 board)
 * ============================================
 */

#include "actuators.h"
#include "config.h"
#include "display.h"
#include "mqtt_handler.h"
#include "sensors.h"
#include <ESPmDNS.h>
#include <WiFi.h>

// ============================================
// Global Objects
// ============================================
SensorManager sensors;
ActuatorManager actuators;
DisplayManager oled;
MQTTHandler mqtt;

// ============================================
// Timing Variables
// ============================================
unsigned long lastSensorRead = 0;
unsigned long lastMQTTPublish = 0;
unsigned long lastOLEDUpdate = 0;
unsigned long lastStatusReport = 0;
unsigned long lastAlertTime = 0;
// PIR Room 1 (KY-019 PWM light: dim → bright)
unsigned long motionLightTimer1 = 0;
bool motionLightActive1 = false;  // true = đang ở chế độ BRIGHT

// PIR Room 2 (Relay 4: OFF → ON)
unsigned long motionLightTimer2 = 0;
bool motionLightActive2 = false;  // true = đèn đang BẬT

// Both Rooms (Relay 4)
bool relay4Active = false; // true = Relay 4 đang bật do 2 PIR

// Previous alert states (avoid repeated alerts)
bool prevGasAlert = false;
bool prevCOAlert = false;
bool prevTempAlert = false;
bool prevVibrationAlert = false;

// ============================================
// WiFi Connection
// ============================================
void connectWiFi() {
  Serial.println("\n[WiFi] Connecting to: " + String(WIFI_SSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < WIFI_MAX_RETRIES) {
    delay(WIFI_RETRY_DELAY / WIFI_MAX_RETRIES);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected ✓");
    Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
    Serial.println("[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
  } else {
    Serial.println("\n[WiFi] ⚠ Connection failed! Operating offline.");
  }
}

// ============================================
// mDNS Setup
// ============================================
void setupMDNS() {
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.println("[mDNS] Hostname: " + String(MDNS_HOSTNAME) + ".local");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("[mDNS] ⚠ mDNS setup failed");
  }
}

// ============================================
// MQTT Command Handler
// ============================================
void handleMQTTCommand(String topic, String payload) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.println("[CMD] JSON parse error: " + String(error.c_str()));
    return;
  }

  // Relay control
  if (topic == TOPIC_CONTROL_RELAY) {
    int relayId = doc["relay_id"] | 0;
    bool state = doc["state"] | false;
    actuators.setRelay(relayId, state);
    Serial.printf("[CMD] Relay %d → %s\n", relayId, state ? "ON" : "OFF");
  }

  // Motor control
  else if (topic == TOPIC_CONTROL_MOTOR) {
    String action = doc["action"] | "stop";
    if (action == "open") {
      actuators.openDoor();
    } else if (action == "close") {
      actuators.closeDoor();
    }
    Serial.println("[CMD] Motor → " + action);
  }

  // LED control
  else if (topic == TOPIC_CONTROL_LED) {
    String mode = doc["mode"] | "dim";
    if (mode == "off")
      actuators.setLightMode(LED_OFF);
    else if (mode == "dim")
      actuators.setLightMode(LED_DIM);
    else if (mode == "bright")
      actuators.setLightMode(LED_BRIGHT);
    else if (mode == "blink")
      actuators.setLightMode(LED_BLINK_FAST);
    else if (mode == "pulse")
      actuators.setLightMode(LED_PULSE);
    Serial.println("[CMD] LED mode → " + mode);
  }

  // Alert trigger (from server)
  else if (topic == TOPIC_ALERT_TRIGGER) {
    String type = doc["type"] | "info";
    String message = doc["message"] | "";
    oled.showAlert(type, message);

    if (type == "emergency") {
      actuators.activateEmergency();
    }
  }

  // Door status from ESP32-CAM
  else if (topic == TOPIC_DOOR_STATUS) {
    String event = doc["event"] | "";
    String name = doc["name"] | "";

    if (event == "access_granted") {
      actuators.openDoor();
      oled.showAlert("CUA MO", "Xin chao " + name);

      // Auto-close after 5 seconds
      // (handled in automation loop)
    } else if (event == "unknown_face") {
      mqtt.publishAlert("security", "Unknown face detected at door!", "danger");
      actuators.setAlarm(true);
      oled.showAlert("CANH BAO", "Nguoi la tai cua!");
      actuators.setLED(1, 0, true); // Red LED on
    }
  }

  // TTS playback command
  else if (topic == TOPIC_TTS_PLAY) {
    String text = doc["text"] | "";
    // Speaker relay is controlled - the actual TTS is handled by the Python
    // server sending audio to a connected speaker
    actuators.setAlarm(true); // Enable speaker
    Serial.println("[TTS] Playing: " + text);
  }
}

// ============================================
// Automation Logic
// ============================================
void runAutomation() {
  EnvironmentData env = sensors.getEnvironment();
  AirQualityData air = sensors.getAirQuality();
  MotionData motion = sensors.getMotion();
  unsigned long now = millis();

  // ============================================================
  // MOTION-BASED LIGHTING (PIR)
  // Room 1: KY-019 (GPIO 13, PWM)  →  DIM khi không có người, BRIGHT khi có người
  // Room 2: Relay 4 (GPIO 14)      →  TẮT khi không có người, BẬT khi có người
  // ============================================================

  // --- PHÒNG 1: PIR Room1 → Relay 1 + KY-019 + Relay 4 ---
  if (motion.pirRoom1) {
    // Phát hiện người → bật sáng rõ (BRIGHT) + Relay 4
    if (!motionLightActive1) {
      actuators.setPIRLight(1, true);   // KY-019 → BRIGHT_LEVEL + Relay 1 ON
      actuators.setRelay(4, true);      // Relay 4 → ON
      motionLightActive1 = true;
      relay4Active = true;
      mqtt.publishAlert("motion", "Phong 1: Phat hien nguoi - Den sang ro + Relay 4 bat", "info");
      mqtt.publishSystemStatus();
      Serial.println("[PIR] Phong 1: Co nguoi → Relay 1 ON + Relay 4 ON");
    }
    motionLightTimer1 = now;  // Reset cooldown timer
  }
  // Hết cooldown → tắt đèn phòng 1
  if (motionLightActive1 && (now - motionLightTimer1 > PIR_COOLDOWN)) {
    actuators.setPIRLight(1, false);    // KY-019 → TẮT + Relay 1 OFF
    motionLightActive1 = false;
    // Relay 4: chỉ tắt nếu phòng 2 cũng không có người
    if (!motionLightActive2) {
      actuators.setRelay(4, false);
      relay4Active = false;
    }
    mqtt.publishAlert("motion", "Phong 1: Khong co nguoi - Den tat", "info");
    mqtt.publishSystemStatus();
    Serial.println("[PIR] Phong 1: Khong co nguoi → Relay 1 OFF (timeout)");
  }

  // --- PHÒNG 2: PIR Room2 → Relay 3 + Relay 4 ---
  if (motion.pirRoom2) {
    // Phát hiện người → bật đèn + Relay 4
    if (!motionLightActive2) {
      actuators.setPIRLight(2, true);   // Relay 3 → ON
      actuators.setRelay(4, true);      // Relay 4 → ON
      motionLightActive2 = true;
      relay4Active = true;
      mqtt.publishAlert("motion", "Phong 2: Phat hien nguoi - Den bat + Relay 4 bat", "info");
      mqtt.publishSystemStatus();
      Serial.println("[PIR] Phong 2: Co nguoi → Relay 3 ON + Relay 4 ON");
    }
    motionLightTimer2 = now;  // Reset cooldown timer
  }
  // Hết cooldown → tắt đèn phòng 2
  if (motionLightActive2 && (now - motionLightTimer2 > PIR_COOLDOWN)) {
    actuators.setPIRLight(2, false);    // Relay 3 → OFF
    motionLightActive2 = false;
    // Relay 4: chỉ tắt nếu phòng 1 cũng không có người
    if (!motionLightActive1) {
      actuators.setRelay(4, false);
      relay4Active = false;
    }
    mqtt.publishAlert("motion", "Phong 2: Khong co nguoi - Den tat", "info");
    mqtt.publishSystemStatus();
    Serial.println("[PIR] Phong 2: Khong co nguoi → Relay 3 OFF (timeout)");
  }

  // ---- TEMPERATURE-BASED FAN CONTROL ----
  if (env.temperature >= TEMP_WARNING && !actuators.isFanOn()) {
    actuators.setFan(true);
    mqtt.publishAlert("temperature",
                      "Nhiet do cao: " + String(env.temperature, 1) +
                          "°C - Quat da bat",
                      "warning");
    oled.showAlert("NHIET DO CAO", String(env.temperature, 1) + " °C");
  } else if (env.temperature < TEMP_WARNING - 2.0 && actuators.isFanOn() &&
             !actuators.isEmergencyActive()) {
    // Hysteresis: turn off fan 2°C below threshold
    actuators.setFan(false);
  }

  // ---- GAS/CO DANGER → EMERGENCY ----
  if (env.gasAlert && !prevGasAlert) {
    mqtt.publishAlert(
        "gas", "Phat hien khi gas nguy hiem! Muc: " + String(env.gasLevel),
        "critical");
    oled.showAlert("KHI GAS!", "Muc: " + String(env.gasLevel));
    actuators.activateEmergency();
    prevGasAlert = true;
  } else if (!env.gasAlert && prevGasAlert) {
    prevGasAlert = false;
    if (!env.coAlert)
      actuators.deactivateEmergency();
  }

  if (env.coAlert && !prevCOAlert) {
    mqtt.publishAlert("co",
                      "Phat hien khi CO nguy hiem! Muc: " + String(env.coLevel),
                      "critical");
    oled.showAlert("KHI CO!", "Muc: " + String(env.coLevel));
    actuators.activateEmergency();
    prevCOAlert = true;
  } else if (!env.coAlert && prevCOAlert) {
    prevCOAlert = false;
    if (!env.gasAlert)
      actuators.deactivateEmergency();
  }

  // ---- TEMPERATURE DANGER → EMERGENCY ----
  if (env.tempAlert && !prevTempAlert) {
    mqtt.publishAlert(
        "temperature",
        "Nhiet do nguy hiem: " + String(env.temperature, 1) + "°C", "critical");
    actuators.activateEmergency();
    prevTempAlert = true;
  } else if (!env.tempAlert && prevTempAlert) {
    prevTempAlert = false;
    if (!env.gasAlert && !env.coAlert)
      actuators.deactivateEmergency();
  }

  // ---- VIBRATION ALERT ----
  if (motion.vibrationAlert && !prevVibrationAlert) {
    mqtt.publishAlert("vibration",
                      "Phat hien rung dong bat thuong! Muc: " +
                          String(motion.vibrationLevel),
                      "warning");
    oled.showAlert("RUNG DONG!", "Muc: " + String(motion.vibrationLevel));
    prevVibrationAlert = true;
  } else if (!motion.vibrationAlert) {
    prevVibrationAlert = false;
  }

  // ---- AIR QUALITY ALERTS ----
  if (air.dataValid && air.pm25Alert) {
    if (now - lastAlertTime > ALERT_REPEAT_INTERVAL) {
      mqtt.publishAlert("air_quality",
                        "PM2.5 cao: " + String(air.pm2_5) + " ug/m3",
                        "warning");
      oled.showAlert("BUI MIN CAO", "PM2.5: " + String(air.pm2_5));
      lastAlertTime = now;
    }
  }

  // ---- AUTO DOOR CLOSE ----
  if (actuators.getDoorState() == DOOR_OPEN) {
    static unsigned long doorOpenTime = 0;
    if (doorOpenTime == 0)
      doorOpenTime = now;

    if (now - doorOpenTime > 5000) { // Close after 5 seconds
      actuators.closeDoor();
      doorOpenTime = 0;
      Serial.println("[AUTO] Door auto-closing after 5s");
    }
  }

  // ---- LED STATUS INDICATORS ----
  if (!actuators.isEmergencyActive()) {
    if (sensors.hasAnyAlert()) {
      actuators.setLED(1, 2, true);  // Yellow warning
      actuators.setLED(1, 1, false); // Green off
    } else {
      actuators.setLED(1, 2, false); // Yellow off
      actuators.setLED(1, 1, true);  // Green on (all OK)
      actuators.setLED(1, 0, false); // Red off
    }
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n========================================");
  Serial.println("  IoT Smart Room - ESP32 Main Controller");
  Serial.println("========================================\n");

  // 1. Initialize WiFi
  connectWiFi();

  // 2. Setup mDNS
  setupMDNS();

  // 3. Initialize sensors
  sensors.begin();

  // 4. Initialize actuators
  actuators.begin();

  // 5. Initialize OLED display
  oled.begin();

  // 6. Initialize MQTT
  mqtt.begin();
  mqtt.setCommandCallback(handleMQTTCommand);

  Serial.println("\n========================================");
  Serial.println("  System Ready! ✓");
  Serial.println("========================================\n");
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  unsigned long now = millis();

  // Maintain MQTT connection
  mqtt.loop();

  // Update actuators (non-blocking operations)
  actuators.update();

  // ---- Read Sensors ----
  if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead = now;
    sensors.readAll();

    // Run automation rules
    runAutomation();
  }

  // ---- Publish to MQTT ----
  if (now - lastMQTTPublish >= MQTT_PUBLISH_INTERVAL) {
    lastMQTTPublish = now;

    if (mqtt.isConnected()) {
      mqtt.publishEnvironment(sensors.getEnvironment());
      mqtt.publishAirQuality(sensors.getAirQuality());
      mqtt.publishMotion(sensors.getMotion());
    }
  }

  // ---- Update OLED ----
  if (now - lastOLEDUpdate >= OLED_UPDATE_INTERVAL) {
    lastOLEDUpdate = now;
    oled.update(sensors.getEnvironment(), sensors.getAirQuality(),
                sensors.getMotion(), WiFi.status() == WL_CONNECTED,
                mqtt.isConnected(), actuators.getDoorState());
  }

  // ---- System Status Report ----
  if (now - lastStatusReport >= STATUS_REPORT_INTERVAL) {
    lastStatusReport = now;
    if (mqtt.isConnected()) {
      mqtt.publishSystemStatus();
    }
  }

  // WiFi reconnect
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWiFiRetry = 0;
    if (now - lastWiFiRetry > 30000) {
      lastWiFiRetry = now;
      Serial.println("[WiFi] Reconnecting...");
      connectWiFi();
    }
  }

  // Small delay for system stability
  delay(10);
}
