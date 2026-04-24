/*
 * ============================================
 * IoT Smart Room - OLED Display Module Header
 * 1.3" OLED I2C (SH1106 or SSD1306)
 * ============================================
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "config.h"
#include "sensors.h"
#include "actuators.h"

// Display pages
enum DisplayPage {
  PAGE_SPLASH,
  PAGE_TEMP_HUMIDITY,
  PAGE_AIR_QUALITY,
  PAGE_GAS_CO,
  PAGE_MOTION_STATUS,
  PAGE_SYSTEM_INFO,
  PAGE_ALERT,
  PAGE_COUNT  // Total number of pages
};

class DisplayManager {
public:
  DisplayManager();
  void begin();
  void update(EnvironmentData env, AirQualityData air, MotionData motion, 
              bool wifiConnected, bool mqttConnected, DoorState doorState);
  void showAlert(String title, String message);
  void showSplash();
  void nextPage();
  void setPage(DisplayPage page);
  
private:
  Adafruit_SH1106G display;
  DisplayPage currentPage;
  unsigned long lastPageChange;
  bool alertActive;
  unsigned long alertStartTime;
  
  void drawHeader(String title);
  void drawFooter(bool wifi, bool mqtt);
  void drawProgressBar(int x, int y, int w, int h, int value, int maxVal);
  void drawPageTempHumidity(EnvironmentData env);
  void drawPageAirQuality(AirQualityData air);
  void drawPageGasCO(EnvironmentData env);
  void drawPageMotionStatus(MotionData motion, DoorState door);
  void drawPageSystemInfo(bool wifi, bool mqtt);
  void drawPageAlert(String title, String msg);
};

#endif // DISPLAY_H
