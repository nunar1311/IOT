/*
 * ============================================
 * IoT Smart Room - Sensor Module
 * Handles all sensor readings
 * ============================================
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <DHT.h>
#include "config.h"

// ============================================
// Data Structures
// ============================================
struct EnvironmentData {
  float temperature;
  float humidity;
  int gasLevel;       // MQ-2 ADC raw value
  int coLevel;        // MQ-7 ADC raw value
  float gasPPM;       // Approximate PPM
  float coPPM;        // Approximate PPM
  bool gasAlert;
  bool coAlert;
  bool tempAlert;
  bool humidityAlert;
};

struct AirQualityData {
  uint16_t pm1_0;     // PM1.0 µg/m³
  uint16_t pm2_5;     // PM2.5 µg/m³
  uint16_t pm10;      // PM10 µg/m³
  bool pm25Alert;
  bool pm10Alert;
  bool dataValid;
};

struct MotionData {
  bool pirRoom1;
  bool pirRoom2;
  int vibrationLevel;
  bool vibrationAlert;
  unsigned long lastMotionRoom1;
  unsigned long lastMotionRoom2;
};

// ============================================
// Sensor Manager Class
// ============================================
class SensorManager {
public:
  SensorManager();
  void begin();
  void readAll();
  
  // Individual sensor reads
  void readDHT11();
  void readMQ2();
  void readMQ7();
  void readAPM10();
  void readPIR();
  void readPiezo();
  
  // Getters
  EnvironmentData getEnvironment() { return envData; }
  AirQualityData getAirQuality() { return airData; }
  MotionData getMotion() { return motionData; }
  
  // Alert checks
  bool hasAnyAlert();
  bool hasDangerAlert();
  String getAlertSummary();

private:
  DHT dht;
  EnvironmentData envData;
  AirQualityData airData;
  MotionData motionData;
  
  // APM10 parsing
  uint8_t apmBuffer[32];
  int apmIndex;
  bool parseAPM10Frame();
  
  // Calibration helpers
  float rawToGasPPM(int raw);
  float rawToCOPPM(int raw);
};

#endif // SENSORS_H
