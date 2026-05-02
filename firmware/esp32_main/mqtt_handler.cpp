/*
 * ============================================
 * IoT Smart Room - MQTT Handler Implementation
 * ============================================
 */

#include "mqtt_handler.h"

// Static instance for callback
MQTTHandler* MQTTHandler::instance = nullptr;

extern ActuatorManager actuators;

// ============================================
// Constructor
// ============================================
MQTTHandler::MQTTHandler() : mqttClient(wifiClient) {
  instance = this;
  commandCallback = nullptr;
  lastReconnectAttempt = 0;
}

// ============================================
// Initialize MQTT
// ============================================
void MQTTHandler::begin() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);  // Larger buffer for JSON
  
  Serial.println("[MQTT] Connecting to broker: " + String(MQTT_SERVER) + ":" + String(MQTT_PORT));
  reconnect();
}

// ============================================
// Loop - maintain connection
// ============================================
void MQTTHandler::loop() {
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > MQTT_RETRY_DELAY) {
      lastReconnectAttempt = now;
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }
  mqttClient.loop();
}

bool MQTTHandler::isConnected() {
  return mqttClient.connected();
}

// ============================================
// Reconnect & Subscribe
// ============================================
bool MQTTHandler::reconnect() {
  Serial.print("[MQTT] Attempting connection...");
  
  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.println(" connected ✓");
    
    // Subscribe to control topics
    mqttClient.subscribe(TOPIC_CONTROL_RELAY);
    mqttClient.subscribe(TOPIC_CONTROL_MOTOR);
    mqttClient.subscribe(TOPIC_CONTROL_LED);
    mqttClient.subscribe(TOPIC_ALERT_TRIGGER);
    mqttClient.subscribe(TOPIC_DOOR_STATUS);
    mqttClient.subscribe(TOPIC_TTS_PLAY);
    
    Serial.println("[MQTT] Subscribed to control topics ✓");
    
    // Publish online status
    StaticJsonDocument<128> doc;
    doc["status"] = "online";
    doc["device"] = MQTT_CLIENT_ID;
    doc["ip"] = WiFi.localIP().toString();
    
    char buffer[128];
    serializeJson(doc, buffer);
    mqttClient.publish(TOPIC_SENSOR_STATUS, buffer, true);  // retained
    
    return true;
  } else {
    Serial.print(" failed, rc=");
    Serial.println(mqttClient.state());
    return false;
  }
}

// ============================================
// Static Callback (routes to instance)
// ============================================
void MQTTHandler::mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (instance) {
    String topicStr = String(topic);
    String payloadStr = "";
    for (unsigned int i = 0; i < length; i++) {
      payloadStr += (char)payload[i];
    }
    instance->handleMessage(topicStr, payloadStr);
  }
}

void MQTTHandler::handleMessage(String topic, String payload) {
  Serial.println("[MQTT] Received: " + topic + " => " + payload);
  
  if (commandCallback) {
    commandCallback(topic, payload);
  }
}

// ============================================
// Publish: Environment Data
// ============================================
void MQTTHandler::publishEnvironment(EnvironmentData env) {
  StaticJsonDocument<512> doc;
  
  doc["temperature"] = round(env.temperature * 10) / 10.0;
  doc["humidity"] = round(env.humidity * 10) / 10.0;
  doc["gas_raw"] = env.gasLevel;
  doc["gas_ppm"] = round(env.gasPPM * 10) / 10.0;
  doc["co_raw"] = env.coLevel;
  doc["co_ppm"] = round(env.coPPM * 10) / 10.0;
  doc["gas_alert"] = env.gasAlert;
  doc["co_alert"] = env.coAlert;
  doc["temp_alert"] = env.tempAlert;
  doc["humidity_alert"] = env.humidityAlert;
  doc["timestamp"] = millis();
  
  char buffer[512];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPIC_SENSOR_ENV, buffer);
}

// ============================================
// Publish: Air Quality Data
// ============================================
void MQTTHandler::publishAirQuality(AirQualityData air) {
  StaticJsonDocument<256> doc;
  
  // Always publish so the dashboard receives data.
  // When dataValid=false the sensor is not connected or frame is corrupted.
  doc["sensor_ok"] = air.dataValid;
  doc["pm1_0"]     = air.dataValid ? air.pm1_0 : 0;
  doc["pm2_5"]     = air.dataValid ? air.pm2_5 : 0;
  doc["pm10"]      = air.dataValid ? air.pm10  : 0;
  doc["pm25_alert"] = air.pm25Alert;
  doc["pm10_alert"] = air.pm10Alert;
  doc["timestamp"]  = millis();
  
  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPIC_SENSOR_AIR, buffer);
}

// ============================================
// Publish: Motion Data
// ============================================
void MQTTHandler::publishMotion(MotionData motion) {
  StaticJsonDocument<256> doc;
  
  doc["pir_room1"] = motion.pirRoom1;
  doc["pir_room2"] = motion.pirRoom2;
  doc["vibration"] = motion.vibrationLevel;
  doc["vibration_alert"] = motion.vibrationAlert;
  doc["last_motion_room1"] = motion.lastMotionRoom1;
  doc["last_motion_room2"] = motion.lastMotionRoom2;
  doc["timestamp"] = millis();
  
  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPIC_SENSOR_MOTION, buffer);
}

// ============================================
// Publish: System Status
// ============================================
void MQTTHandler::publishSystemStatus() {
  StaticJsonDocument<256> doc;
  
  doc["device"] = MQTT_CLIENT_ID;
  doc["status"] = "online";
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime_sec"] = millis() / 1000;
  
  // Send relay states
  doc["relay1"] = actuators.getRelayState(1);
  doc["relay2"] = actuators.getRelayState(2);
  doc["relay3"] = actuators.getRelayState(3);
  doc["relay4"] = actuators.getRelayState(4);
  
  // Door state
  doc["doorStatus"] = (actuators.getDoorState() == DOOR_OPEN || actuators.getDoorState() == DOOR_OPENING) ? "open" : "closed";
  
  doc["timestamp"] = millis();
  
  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPIC_SENSOR_STATUS, buffer, true);
}

// ============================================
// Publish: Alert Event
// ============================================
void MQTTHandler::publishAlert(String type, String message, String severity) {
  StaticJsonDocument<512> doc;
  
  doc["type"] = type;
  doc["message"] = message;
  doc["severity"] = severity;  // "warning", "danger", "critical"
  doc["device"] = MQTT_CLIENT_ID;
  doc["timestamp"] = millis();
  
  char buffer[512];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPIC_ALERT, buffer);
  
  Serial.println("[ALERT] Published: " + type + " - " + message);
}

// ============================================
// Publish: Door Event
// ============================================
void MQTTHandler::publishDoorEvent(String event, String name) {
  StaticJsonDocument<256> doc;
  
  doc["event"] = event;  // "opened", "closed", "unknown_face", "access_granted"
  doc["name"] = name;
  doc["timestamp"] = millis();
  
  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPIC_DOOR_STATUS, buffer);
}
