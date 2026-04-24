/*
 * ============================================
 * IoT Smart Room - Sensor Module Implementation
 * ============================================
 */

#include "sensors.h"

// ============================================
// Constructor
// ============================================
SensorManager::SensorManager() : dht(PIN_DHT11, DHT11) {
  memset(&envData, 0, sizeof(envData));
  memset(&airData, 0, sizeof(airData));
  memset(&motionData, 0, sizeof(motionData));
  apmIndex = 0;
}

// ============================================
// Initialize all sensors
// ============================================
void SensorManager::begin() {
  // DHT11
  dht.begin();
  Serial.println("[SENSOR] DHT11 initialized on GPIO " + String(PIN_DHT11));
  
  // MQ-2 & MQ-7 (Analog)
  pinMode(PIN_MQ2_AO, INPUT);
  pinMode(PIN_MQ7_AO, INPUT);
  analogSetAttenuation(ADC_11db); // Full range 0-3.3V
  Serial.println("[SENSOR] MQ-2 initialized on GPIO " + String(PIN_MQ2_AO));
  Serial.println("[SENSOR] MQ-7 initialized on GPIO " + String(PIN_MQ7_AO));
  
  // APM10 (UART2)
  Serial2.begin(APM10_BAUD_RATE, SERIAL_8N1, PIN_APM10_RX, PIN_APM10_TX);
  Serial.println("[SENSOR] APM10 initialized on UART2 (RX:" + String(PIN_APM10_RX) + " TX:" + String(PIN_APM10_TX) + ")");
  
  // PIR sensors
  pinMode(PIN_PIR_ROOM1, INPUT);
  pinMode(PIN_PIR_ROOM2, INPUT);
  Serial.println("[SENSOR] PIR Room1 on GPIO " + String(PIN_PIR_ROOM1));
  Serial.println("[SENSOR] PIR Room2 on GPIO " + String(PIN_PIR_ROOM2));
  
  // Piezo vibration
  pinMode(PIN_PIEZO, INPUT);
  Serial.println("[SENSOR] Piezo on GPIO " + String(PIN_PIEZO));
  
  Serial.println("[SENSOR] All sensors initialized ✓");
}

// ============================================
// Read all sensors
// ============================================
void SensorManager::readAll() {
  readDHT11();
  readMQ2();
  readMQ7();
  readAPM10();
  readPIR();
  readPiezo();
}

// ============================================
// DHT11 - Temperature & Humidity
// ============================================
void SensorManager::readDHT11() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  
  if (!isnan(t) && !isnan(h)) {
    envData.temperature = t;
    envData.humidity = h;
    envData.tempAlert = (t >= TEMP_DANGER);
    envData.humidityAlert = (h >= HUMIDITY_HIGH || h <= HUMIDITY_LOW);
    
    Serial.printf("[DHT11] Temp: %.1f°C  Humidity: %.1f%%\n", t, h);
  } else {
    Serial.println("[DHT11] ⚠ Read failed!");
  }
}

// ============================================
// MQ-2 - Gas/Smoke Sensor
// ============================================
void SensorManager::readMQ2() {
  // Average multiple readings for stability
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(PIN_MQ2_AO);
    delayMicroseconds(100);
  }
  envData.gasLevel = sum / 10;
  envData.gasPPM = rawToGasPPM(envData.gasLevel);
  envData.gasAlert = (envData.gasLevel >= GAS_DANGER);
  
  Serial.printf("[MQ-2] Gas Level: %d (%.1f PPM) %s\n", 
    envData.gasLevel, envData.gasPPM,
    envData.gasAlert ? "⚠ ALERT!" : "OK");
}

// ============================================
// MQ-7 - CO Sensor
// ============================================
void SensorManager::readMQ7() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(PIN_MQ7_AO);
    delayMicroseconds(100);
  }
  envData.coLevel = sum / 10;
  envData.coPPM = rawToCOPPM(envData.coLevel);
  envData.coAlert = (envData.coLevel >= CO_DANGER);
  
  Serial.printf("[MQ-7] CO Level: %d (%.1f PPM) %s\n", 
    envData.coLevel, envData.coPPM,
    envData.coAlert ? "⚠ ALERT!" : "OK");
}

// ============================================
// APM10 - PM Dust Sensor (PMS protocol)
// ============================================
void SensorManager::readAPM10() {
  // 1. Send measurement request if needed (every 2 seconds max)
  static unsigned long lastRequest = 0;
  if (millis() - lastRequest > 2000) {
    uint8_t cmd[] = {0xFE, 0xA5, 0x00, 0x01, 0xA6};
    Serial2.write(cmd, 5);
    lastRequest = millis();
    // Do not reset apmIndex here immediately, wait for bytes to process
  }

  // 2. Read incoming bytes
  while (Serial2.available()) {
    uint8_t byte = Serial2.read();
    
    // Look for ASAIR frame header (0xFE 0xA5)
    if (apmIndex == 0 && byte != 0xFE) continue;
    if (apmIndex == 1 && byte != 0xA5) { 
      apmIndex = (byte == 0xFE) ? 1 : 0; 
      continue; 
    }
    
    apmBuffer[apmIndex++] = byte;
    
    // Full ASAIR frame received (11 bytes: FE A5 02 00 DF11 DF12 DF21 DF22 DF31 DF32 [CS])
    if (apmIndex >= 11) {
      if (parseAPM10Frame()) {
        Serial.printf("[APM10] PM1.0: %d  PM2.5: %d  PM10: %d µg/m³\n",
          airData.pm1_0, airData.pm2_5, airData.pm10);
      }
      apmIndex = 0;
    }
  }
}

bool SensorManager::parseAPM10Frame() {
  // ASAIR Protocol frame structure (11 bytes expected here for reading PM1, PM2.5, PM10):
  // [0]   : Header 0xFE
  // [1]   : Fixed Code 0xA5
  // [2]   : Length = 0x02
  // [3]   : Command / status = 0x00
  // [4-5] : PM1.0 (High, Low) µg/m³
  // [6-7] : PM2.5 (High, Low) µg/m³
  // [8-9] : PM10  (High, Low) µg/m³
  // [10]  : Checksum (low byte of sum of bytes [1..9])

  // Verify checksum
  uint16_t checksum = 0;
  // sum of Fixed code + Length + Command + Data
  // APMBuffer[1] through APMBuffer[9]
  for (int i = 1; i < 10; i++) {
    checksum += apmBuffer[i];
  }
  uint8_t expectedCS = checksum & 0xFF;
  uint8_t receivedCS = apmBuffer[10];
  
  if (expectedCS != receivedCS) {
    Serial.printf("[APM10] ⚠ Checksum error! Calc=0x%02X Recv=0x%02X\n", expectedCS, receivedCS);
    airData.dataValid = false;
    return false;
  }
  
  // Parse PM values
  airData.pm1_0 = (apmBuffer[4] << 8) | apmBuffer[5];
  airData.pm2_5 = (apmBuffer[6] << 8) | apmBuffer[7];
  airData.pm10  = (apmBuffer[8] << 8) | apmBuffer[9];
  
  airData.pm25Alert = (airData.pm2_5 >= PM25_DANGER);
  airData.pm10Alert = (airData.pm10 >= PM10_DANGER);
  airData.dataValid = true;
  
  return true;
}

// ============================================
// PIR - Motion Detection
// ============================================
void SensorManager::readPIR() {
  bool room1 = digitalRead(PIN_PIR_ROOM1) == HIGH;
  bool room2 = digitalRead(PIN_PIR_ROOM2) == HIGH;
  
  if (room1 && !motionData.pirRoom1) {
    motionData.lastMotionRoom1 = millis();
    Serial.println("[PIR] 🚶 Motion detected in Room 1!");
  }
  if (room2 && !motionData.pirRoom2) {
    motionData.lastMotionRoom2 = millis();
    Serial.println("[PIR] 🚶 Motion detected in Room 2!");
  }
  
  motionData.pirRoom1 = room1;
  motionData.pirRoom2 = room2;
}

// ============================================
// Piezo - Vibration Sensor
// ============================================
void SensorManager::readPiezo() {
  motionData.vibrationLevel = analogRead(PIN_PIEZO);
  motionData.vibrationAlert = (motionData.vibrationLevel >= VIBRATION_THRESHOLD);
  
  if (motionData.vibrationAlert) {
    Serial.printf("[PIEZO] ⚠ Vibration alert! Level: %d\n", motionData.vibrationLevel);
  }
}

// ============================================
// Alert Helpers
// ============================================
bool SensorManager::hasAnyAlert() {
  return envData.gasAlert || envData.coAlert || envData.tempAlert || 
         envData.humidityAlert || airData.pm25Alert || airData.pm10Alert ||
         motionData.vibrationAlert;
}

bool SensorManager::hasDangerAlert() {
  return envData.gasAlert || envData.coAlert || 
         (envData.temperature >= TEMP_DANGER);
}

String SensorManager::getAlertSummary() {
  String summary = "";
  if (envData.tempAlert) summary += "HIGH TEMP(" + String(envData.temperature, 1) + "°C) ";
  if (envData.gasAlert) summary += "GAS(" + String(envData.gasLevel) + ") ";
  if (envData.coAlert) summary += "CO(" + String(envData.coLevel) + ") ";
  if (envData.humidityAlert) summary += "HUMIDITY(" + String(envData.humidity, 1) + "%) ";
  if (airData.pm25Alert) summary += "PM2.5(" + String(airData.pm2_5) + ") ";
  if (airData.pm10Alert) summary += "PM10(" + String(airData.pm10) + ") ";
  if (motionData.vibrationAlert) summary += "VIBRATION ";
  return summary;
}

// ============================================
// Calibration Helpers
// Approximate conversions - adjust based on
// actual sensor calibration curves
// ============================================
float SensorManager::rawToGasPPM(int raw) {
  // MQ-2: Logarithmic curve approximation
  // This is approximate - calibrate with known gas concentrations
  if (raw < 100) return 0;
  float voltage = raw * (3.3 / 4095.0);
  float rs_ro = (3.3 - voltage) / voltage;
  float ppm = 613.9 * pow(rs_ro, -2.074); // LPG approximation
  return constrain(ppm, 0, 10000);
}

float SensorManager::rawToCOPPM(int raw) {
  // MQ-7: Logarithmic curve approximation
  if (raw < 100) return 0;
  float voltage = raw * (3.3 / 4095.0);
  float rs_ro = (3.3 - voltage) / voltage;
  float ppm = 98.322 * pow(rs_ro, -1.458); // CO approximation
  return constrain(ppm, 0, 5000);
}
