/*
 * ============================================
 * IoT Smart Room - MQTT Handler Header
 * ============================================
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sensors.h"
#include "actuators.h"

class MQTTHandler {
public:
  MQTTHandler();
  void begin();
  void loop();
  bool isConnected();
  
  // Publish sensor data
  void publishEnvironment(EnvironmentData env);
  void publishAirQuality(AirQualityData air);
  void publishMotion(MotionData motion);
  void publishSystemStatus();
  void publishAlert(String type, String message, String severity);
  void publishDoorEvent(String event, String name = "");
  
  // Callback setter
  typedef void (*CommandCallback)(String topic, String payload);
  void setCommandCallback(CommandCallback cb) { commandCallback = cb; }

private:
  WiFiClient wifiClient;
  PubSubClient mqttClient;
  CommandCallback commandCallback;
  
  unsigned long lastReconnectAttempt;
  
  bool reconnect();
  static void mqttCallback(char* topic, byte* payload, unsigned int length);
  static MQTTHandler* instance;
  void handleMessage(String topic, String payload);
};

#endif // MQTT_HANDLER_H
