/*
 * ============================================
 * IoT Smart Room - OLED Display Implementation
 * ============================================
 */

#include "display.h"

// ============================================
// Constructor
// ============================================
DisplayManager::DisplayManager() : display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1) {
  currentPage = PAGE_SPLASH;
  lastPageChange = 0;
  alertActive = false;
  alertStartTime = 0;
}

// ============================================
// Initialize Display
// ============================================
void DisplayManager::begin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  if (!display.begin(OLED_ADDRESS, true)) {
    Serial.println("[DISPLAY] ⚠ OLED initialization failed!");
    return;
  }

  display.setTextColor(SH110X_WHITE);
  display.clearDisplay();
  showSplash();
  Serial.println("[DISPLAY] OLED 1.3\" initialized ✓");
}

// ============================================
// Update Display (auto-rotate pages)
// ============================================
void DisplayManager::update(EnvironmentData env, AirQualityData air,
                            MotionData motion, bool wifiConnected,
                            bool mqttConnected, DoorState doorState) {
  // Handle alert timeout
  if (alertActive && millis() - alertStartTime > 5000) {
    alertActive = false;
  }

  // Auto-rotate pages
  if (!alertActive && millis() - lastPageChange >= OLED_PAGE_DURATION) {
    lastPageChange = millis();
    currentPage = (DisplayPage)((currentPage + 1) % PAGE_COUNT);
    if (currentPage == PAGE_SPLASH)
      currentPage = PAGE_TEMP_HUMIDITY; // Skip splash
  }

  display.clearDisplay();

  if (alertActive) {
    // Alert overrides normal display
    return; // Alert already drawn
  }

  switch (currentPage) {
  case PAGE_TEMP_HUMIDITY:
    drawPageTempHumidity(env);
    break;
  case PAGE_AIR_QUALITY:
    drawPageAirQuality(air);
    break;
  case PAGE_GAS_CO:
    drawPageGasCO(env);
    break;
  case PAGE_MOTION_STATUS:
    drawPageMotionStatus(motion, doorState);
    break;
  case PAGE_SYSTEM_INFO:
    drawPageSystemInfo(wifiConnected, mqttConnected);
    break;
  default:
    drawPageTempHumidity(env);
    break;
  }

  drawFooter(wifiConnected, mqttConnected);
  display.display();
}

// ============================================
// Show Alert (interrupts normal display)
// ============================================
void DisplayManager::showAlert(String title, String message) {
// alertActive = true;
// alertStartTime = millis();
//
// display.clearDisplay();
// drawPageAlert(title, message);
// display.display();
}

// ============================================
// Splash Screen
// ============================================
void DisplayManager::showSplash() {
  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(10, 5);
  display.println("IoT Smart");
  display.setCursor(25, 25);
  display.println("Room");

  display.setTextSize(1);
  display.setCursor(15, 48);
  display.println("Initializing...");

  // Draw border
  display.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SH110X_WHITE);

  display.display();
  delay(2000);
}

// ============================================
// Page: Temperature & Humidity
// ============================================
void DisplayManager::drawPageTempHumidity(EnvironmentData env) {
  drawHeader("NHIET DO & DO AM");

  // Temperature
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.print("Nhiet do:");
  display.setTextSize(2);
  display.setCursor(5, 26);
  display.print(env.temperature, 1);
  display.setTextSize(1);
  display.print(" C");

  // Temperature bar
  int tempPercent =
      constrain(map((int)(env.temperature * 10), 0, 500, 0, 100), 0, 100);
  drawProgressBar(0, 42, 128, 6, tempPercent, 100);

  // Humidity
  display.setCursor(0, 52);
  display.print("Do am: ");
  display.print(env.humidity, 1);
  display.print("%");

  // Alert indicator
  if (env.tempAlert) {
    display.setCursor(100, 16);
    display.print("!! ");
  }
}

// ============================================
// Page: Air Quality (PM)
// ============================================
void DisplayManager::drawPageAirQuality(AirQualityData air) {
  drawHeader("CHAT LUONG K.KHI");

  if (!air.dataValid) {
    display.setTextSize(1);
    display.setCursor(20, 30);
    display.print("Dang doc...");
    return;
  }

  display.setTextSize(1);

  // PM1.0
  display.setCursor(0, 16);
  display.print("PM1.0: ");
  display.print(air.pm1_0);
  display.print(" ug/m3");

  // PM2.5
  display.setCursor(0, 28);
  display.print("PM2.5: ");
  display.setTextSize(2);
  display.print(air.pm2_5);
  display.setTextSize(1);
  display.print(" ug/m3");
  if (air.pm25Alert)
    display.print(" !!");

  // PM2.5 bar
  int pm25Pct = constrain(map(air.pm2_5, 0, 100, 0, 100), 0, 100);
  drawProgressBar(0, 44, 128, 5, pm25Pct, 100);

  // PM10
  display.setCursor(0, 53);
  display.print("PM10:  ");
  display.print(air.pm10);
  display.print(" ug/m3");
  if (air.pm10Alert)
    display.print(" !!");
}

// ============================================
// Page: Gas & CO
// ============================================
void DisplayManager::drawPageGasCO(EnvironmentData env) {
  drawHeader("KHI GAS & CO");

  display.setTextSize(1);

  // Gas (MQ-2)
  display.setCursor(0, 16);
  display.print("Gas (MQ-2):");
  display.setTextSize(2);
  display.setCursor(5, 26);
  display.print(env.gasPPM, 0);
  display.setTextSize(1);
  display.print(" PPM");
  if (env.gasAlert)
    display.print(" NGUY!");

  // Gas bar
  int gasPct = constrain(map(env.gasLevel, 0, 4095, 0, 100), 0, 100);
  drawProgressBar(0, 41, 128, 5, gasPct, 100);

  // CO (MQ-7)
  display.setCursor(0, 50);
  display.print("CO (MQ-7): ");
  display.print(env.coPPM, 0);
  display.print(" PPM");
  if (env.coAlert)
    display.print(" NGUY!");
}

// ============================================
// Page: Motion & Door Status
// ============================================
void DisplayManager::drawPageMotionStatus(MotionData motion, DoorState door) {
  drawHeader("CHUYEN DONG & CUA");

  display.setTextSize(1);

  // PIR Room 1
  display.setCursor(0, 16);
  display.print("Phong 1: ");
  display.print(motion.pirRoom1 ? "CO NGUOI" : "Trong");

  // PIR Room 2
  display.setCursor(0, 28);
  display.print("Phong 2: ");
  display.print(motion.pirRoom2 ? "CO NGUOI" : "Trong");

  // Vibration
  display.setCursor(0, 40);
  display.print("Rung: ");
  display.print(motion.vibrationLevel);
  if (motion.vibrationAlert)
    display.print(" !! CANH BAO");

  // Door status
  display.setCursor(0, 52);
  display.print("Cua: ");
  switch (door) {
  case DOOR_CLOSED:
    display.print("DONG");
    break;
  case DOOR_OPENING:
    display.print("DANG MO...");
    break;
  case DOOR_OPEN:
    display.print("MO");
    break;
  case DOOR_CLOSING:
    display.print("DANG DONG...");
    break;
  }
}

// ============================================
// Page: System Info
// ============================================
void DisplayManager::drawPageSystemInfo(bool wifi, bool mqtt) {
  drawHeader("HE THONG");

  display.setTextSize(1);

  display.setCursor(0, 16);
  display.print("WiFi: ");
  display.print(wifi ? "Da Ket noi" : "Mat ket noi!");

  display.setCursor(0, 28);
  display.print("MQTT: ");
  display.print(mqtt ? "Da Ket noi" : "Mat ket noi!");

  display.setCursor(0, 40);
  display.print("Uptime: ");
  unsigned long s = millis() / 1000;
  display.print(s / 3600);
  display.print("h ");
  display.print((s % 3600) / 60);
  display.print("m");

  display.setCursor(0, 52);
  display.print("RAM: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.print(" KB free");
}

// ============================================
// Alert Page
// ============================================
void DisplayManager::drawPageAlert(String title, String msg) {
  // Draw attention border
  display.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SH110X_WHITE);
  display.drawRect(2, 2, OLED_WIDTH - 4, OLED_HEIGHT - 4, SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(10, 5);
  display.print("!! CANH BAO !!");

  display.setTextSize(1);
  display.setCursor(5, 20);
  display.print(title);

  display.setCursor(5, 35);
  // Word wrap for message
  int x = 5;
  for (unsigned int i = 0; i < msg.length(); i++) {
    if (x > 120) {
      display.setCursor(5, display.getCursorY() + 10);
      x = 5;
    }
    display.print(msg[i]);
    x += 6;
  }
}

// ============================================
// Helper: Draw Header
// ============================================
void DisplayManager::drawHeader(String title) {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(title);
  display.drawLine(0, 10, OLED_WIDTH, 10, SH110X_WHITE);
}

// ============================================
// Helper: Draw Footer
// ============================================
void DisplayManager::drawFooter(bool wifi, bool mqtt) {
  display.drawLine(0, 56, OLED_WIDTH, 56, SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 58);
  display.print(wifi ? "W" : "!");
  display.print(" ");
  display.print(mqtt ? "M" : "!");

  // Page indicator
  int totalPages = PAGE_COUNT - 1; // Exclude splash
  int dotWidth = 6;
  int startX = OLED_WIDTH - (totalPages * dotWidth);
  for (int i = 1; i < PAGE_COUNT; i++) {
    if (i == currentPage) {
      display.fillCircle(startX + (i - 1) * dotWidth, 60, 2, SH110X_WHITE);
    } else {
      display.drawCircle(startX + (i - 1) * dotWidth, 60, 2, SH110X_WHITE);
    }
  }
}

// ============================================
// Helper: Draw Progress Bar
// ============================================
void DisplayManager::drawProgressBar(int x, int y, int w, int h, int value,
                                     int maxVal) {
  display.drawRect(x, y, w, h, SH110X_WHITE);
  int fillWidth = map(constrain(value, 0, maxVal), 0, maxVal, 0, w - 2);
  display.fillRect(x + 1, y + 1, fillWidth, h - 2, SH110X_WHITE);
}

// ============================================
// Navigation
// ============================================
void DisplayManager::nextPage() {
  currentPage = (DisplayPage)((currentPage + 1) % PAGE_COUNT);
  if (currentPage == PAGE_SPLASH)
    currentPage = PAGE_TEMP_HUMIDITY;
  lastPageChange = millis();
}

void DisplayManager::setPage(DisplayPage page) {
  currentPage = page;
  lastPageChange = millis();
}
