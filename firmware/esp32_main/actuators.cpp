/*
 * ============================================
 * IoT Smart Room - Actuator Module Implementation
 * ============================================
 */

#include "actuators.h"

// ============================================
// Constructor
// ============================================
ActuatorManager::ActuatorManager()
    : stepper(STEPPER_STEPS_REV, PIN_STEPPER_IN1, PIN_STEPPER_IN3,
              PIN_STEPPER_IN2, PIN_STEPPER_IN4) {
  memset(relayStates, 0, sizeof(relayStates));
  fanState = false;
  alarmState = false;
  doorState = DOOR_CLOSED;
  currentLEDMode = LED_DIM;
  dimLevel = DIM_LEVEL;
  emergencyMode = false;
  lastBlinkTime = 0;
  blinkState = false;
  blinkEnabled = false;
  stepsRemaining = 0;
  stepDirection = 1;
  lastStepTime = 0;
}

// ============================================
// Initialize all actuators
// ============================================
void ActuatorManager::begin() {
  // Initialize relay pins (Active LOW for most relay modules)
  pinMode(PIN_RELAY1, OUTPUT);
  pinMode(PIN_RELAY2, OUTPUT);
  pinMode(PIN_RELAY3, OUTPUT);
  pinMode(PIN_RELAY4, OUTPUT);

  // Set all relays OFF (HIGH = OFF for active-low modules)
  digitalWrite(PIN_RELAY1, HIGH);
  digitalWrite(PIN_RELAY2, HIGH);
  digitalWrite(PIN_RELAY3, HIGH);
  digitalWrite(PIN_RELAY4, HIGH);

  Serial.println("[ACTUATOR] 4-Relay module initialized");

  // KY-019 Relay using standard Digital Write (NO PWM for mechanical relay)
  pinMode(PIN_RELAY_DIM, OUTPUT);
  digitalWrite(PIN_RELAY_DIM, HIGH); // Start with light OFF (Active LOW)
  Serial.println("[ACTUATOR] KY-019 light initialized (Digital)");

  // Stepper motor
  stepper.setSpeed(STEPPER_SPEED_RPM);
  Serial.println("[ACTUATOR] Stepper motor 28BYJ-48 initialized");

  // LED indicators
  pinMode(PIN_LED_R1_RED, OUTPUT);
  pinMode(PIN_LED_R1_GREEN, OUTPUT);
  pinMode(PIN_LED_R1_YELLOW, OUTPUT);

  // Initial state: green LED on (system OK)
  digitalWrite(PIN_LED_R1_RED, LOW);
  digitalWrite(PIN_LED_R1_GREEN, HIGH);
  digitalWrite(PIN_LED_R1_YELLOW, LOW);

  Serial.println("[ACTUATOR] LED indicators initialized");
  Serial.println("[ACTUATOR] All actuators ready ✓");
}

// ============================================
// Non-blocking update loop
// ============================================
void ActuatorManager::update() {
  updateBlink();
  updateStepper();
  updatePWM();
}

// ============================================
// Relay Control
// ============================================
void ActuatorManager::setRelay(int relayNum, bool state) {
  if (relayNum < 1 || relayNum > 5)
    return;

  relayStates[relayNum - 1] = state;
  int pin;

  switch (relayNum) {
  case 1:
    pin = PIN_RELAY1;
    break;
  case 2:
    pin = PIN_RELAY2;
    break;
  case 3:
    pin = PIN_RELAY3;
    break;
  case 4:
    pin = PIN_RELAY4;
    break;
  case 5: // KY-019
    if (state) {
      digitalWrite(PIN_RELAY_DIM, LOW); // ON (Active LOW)
    } else {
      digitalWrite(PIN_RELAY_DIM, HIGH); // OFF
    }
    Serial.printf("[RELAY] KY-019 = %s\n", state ? "BRIGHT" : "OFF");
    return;
  default:
    return;
  }

  // Active LOW logic for relay modules
  digitalWrite(pin, state ? LOW : HIGH);
  Serial.printf("[RELAY] Relay %d = %s\n", relayNum, state ? "ON" : "OFF");
}

bool ActuatorManager::getRelayState(int relayNum) {
  if (relayNum < 1 || relayNum > 5)
    return false;
  return relayStates[relayNum - 1];
}

// ============================================
// Light Control
// ============================================
void ActuatorManager::setLightMode(LEDMode mode) {
  currentLEDMode = mode;

  switch (mode) {
  case LED_OFF:
    digitalWrite(PIN_RELAY_DIM, HIGH); // OFF
    blinkEnabled = false;
    break;
  case LED_DIM:
    digitalWrite(PIN_RELAY_DIM, LOW); // Mechanical relay can't dim, so just turn ON
    blinkEnabled = false;
    break;
  case LED_BRIGHT:
    digitalWrite(PIN_RELAY_DIM, LOW); // ON
    blinkEnabled = false;
    break;
  case LED_BLINK_SLOW:
    blinkEnabled = true;
    break;
  case LED_BLINK_FAST:
    blinkEnabled = true;
    break;
  case LED_PULSE:
    // Handled in updatePWM
    break;
  }

  Serial.printf("[LIGHT] Mode changed to: %d\n", mode);
}

void ActuatorManager::setDimLevel(uint8_t level) {
  dimLevel = level;
  if (currentLEDMode == LED_DIM) {
    // Cannot dim mechanical relay
    digitalWrite(PIN_RELAY_DIM, level > 0 ? LOW : HIGH);
  }
}

void ActuatorManager::setRoomLight(int room, bool on) {
  switch (room) {
  case 1:
    setRelay(1, on);
    break;
  case 2:
    setRelay(3, on);
    break; // Using Relay3 for room 2
  case 3:  // Room 3 could use KY-019
    setRelay(5, on);
    break;
  }
}

// ============================================
// PIR-Driven Light Control (độc lập từng phòng)
// ============================================
// Phòng 1: Relay 1
//   - motionDetected = true  → BẬT (ON)
//   - motionDetected = false → TẮT (OFF)
// Phòng 2: Relay 3
//   - motionDetected = true  → BẬT (ON)
//   - motionDetected = false → TẮT (OFF)
// ============================================
void ActuatorManager::setPIRLight(int room, bool motionDetected) {
  if (room == 1) {
    // --- PHÒNG 1: Relay 1 ---
    if (motionDetected) {
      setRelay(1, true);
      Serial.println("[PIR-LIGHT] Phong 1 → Relay1 BAT (ON)");
    } else {
      setRelay(1, false);
      Serial.println("[PIR-LIGHT] Phong 1 → Relay1 TAT (OFF)");
    }
  } else if (room == 2) {
    // --- PHÒNG 2: Relay 3 ---
    if (motionDetected) {
      setRelay(3, true);
      Serial.println("[PIR-LIGHT] Phong 2 → Relay3 BAT (ON)");
    } else {
      setRelay(3, false);
      Serial.println("[PIR-LIGHT] Phong 2 → Relay3 TAT (OFF)");
    }
  }
}

// ============================================
// Fan Control
// ============================================
void ActuatorManager::setFan(bool on) {
  fanState = on;
  setRelay(2, on);
  Serial.printf("[FAN] Fan %s\n", on ? "ON 🌀" : "OFF");
}

// ============================================
// Speaker/Alarm Control
// ============================================
void ActuatorManager::setAlarm(bool on) {
  alarmState = on;
  // Loa/còi báo động bị gỡ bỏ, Relay 3 giờ đây chỉ dành cho Đèn phòng 2
  Serial.printf("[ALARM] Alarm logic triggered (Hardware removed) %s\n", on ? "ON 🔊" : "OFF");
}

// ============================================
// Stepper Motor (Door) - Non-blocking
// ============================================
void ActuatorManager::openDoor() {
  if (doorState == DOOR_OPEN || doorState == DOOR_OPENING)
    return;

  doorState = DOOR_OPENING;
  stepsRemaining = STEPPER_STEPS_REV * 5; // 3 revolutions
  stepDirection = 1;
  Serial.println("[DOOR] Opening... 🚪");
}

void ActuatorManager::closeDoor() {
  if (doorState == DOOR_CLOSED || doorState == DOOR_CLOSING)
    return;

  doorState = DOOR_CLOSING;
  stepsRemaining = STEPPER_STEPS_REV * 5; // 3 revolutions
  stepDirection = -1;
  Serial.println("[DOOR] Closing... 🚪");
}

// ============================================
// LED Indicators
// ============================================
void ActuatorManager::setLED(int room, int color, bool state) {
  if (room == 1) {
    // If turning on a specific LED, turn off the others to ensure only one is on
    if (state) {
      if (color != 0) digitalWrite(PIN_LED_R1_RED, LOW);
      if (color != 1) digitalWrite(PIN_LED_R1_GREEN, LOW);
      if (color != 2) digitalWrite(PIN_LED_R1_YELLOW, LOW);
    }
    
    switch (color) {
    case 0:
      digitalWrite(PIN_LED_R1_RED, state ? HIGH : LOW);
      break;
    case 1:
      digitalWrite(PIN_LED_R1_GREEN, state ? HIGH : LOW);
      break;
    case 2:
      digitalWrite(PIN_LED_R1_YELLOW, state ? HIGH : LOW);
      break;
    }
  }
  // Room 2 & 3 LEDs would need I2C expander or shift register
  // For now, managed via relay module
}

void ActuatorManager::blinkAllLEDs(bool enable) {
  blinkEnabled = enable;
  if (!enable) {
    // Reset LEDs to normal state
    digitalWrite(PIN_LED_R1_RED, LOW);
    digitalWrite(PIN_LED_R1_YELLOW, LOW);
    digitalWrite(PIN_LED_R1_GREEN, HIGH);
  }
}

// ============================================
// Emergency Mode
// ============================================
void ActuatorManager::activateEmergency() {
  if (emergencyMode)
    return;

  emergencyMode = true;
  Serial.println("[EMERGENCY] ⚠⚠⚠ EMERGENCY MODE ACTIVATED ⚠⚠⚠");

  // Turn on fan for ventilation
  setFan(true);

  // Turn on alarm/speaker
  setAlarm(true);

  // Blink all LEDs
  blinkAllLEDs(true);

  // Set light to blink mode
  setLightMode(LED_BLINK_FAST);
}

void ActuatorManager::deactivateEmergency() {
  emergencyMode = false;
  Serial.println("[EMERGENCY] Emergency mode deactivated");

  // Reset to normal state
  setFan(false);
  setAlarm(false);
  blinkAllLEDs(false);
  setLightMode(LED_DIM);
}

// ============================================
// Non-blocking blink update
// ============================================
void ActuatorManager::updateBlink() {
  if (!blinkEnabled)
    return;

  unsigned long interval =
      (currentLEDMode == LED_BLINK_FAST) ? 250 : LED_BLINK_INTERVAL;

  if (millis() - lastBlinkTime >= interval) {
    lastBlinkTime = millis();
    blinkState = !blinkState;

    // Blink red LED
    digitalWrite(PIN_LED_R1_RED, blinkState ? HIGH : LOW);

    // Also blink the main light in emergency
    if (emergencyMode) {
      digitalWrite(PIN_RELAY_DIM, blinkState ? LOW : HIGH); // ON/OFF
    }
  }
}

// ============================================
// Non-blocking stepper update
// ============================================
void ActuatorManager::updateStepper() {
  if (stepsRemaining <= 0) {
    if (doorState == DOOR_OPENING) {
      doorState = DOOR_OPEN;
      Serial.println("[DOOR] Door is OPEN ✓");
    } else if (doorState == DOOR_CLOSING) {
      doorState = DOOR_CLOSED;
      Serial.println("[DOOR] Door is CLOSED ✓");
      // De-energize stepper to save power
      digitalWrite(PIN_STEPPER_IN1, LOW);
      digitalWrite(PIN_STEPPER_IN2, LOW);
      digitalWrite(PIN_STEPPER_IN3, LOW);
      digitalWrite(PIN_STEPPER_IN4, LOW);
    }
    return;
  }

  unsigned long stepDelay = 60000UL / (STEPPER_SPEED_RPM * STEPPER_STEPS_REV);

  if (millis() - lastStepTime >= stepDelay) {
    lastStepTime = millis();
    stepper.step(stepDirection);
    stepsRemaining--;
  }
}

// ============================================
// PWM pulse effect update
// ============================================
void ActuatorManager::updatePWM() {
  if (currentLEDMode != LED_PULSE)
    return;

  // Breathing effect
  static int pwmValue = 0;
  static int pwmDirection = 5;
  static unsigned long lastPulse = 0;

  if (millis() - lastPulse >= 30) {
    lastPulse = millis();
    pwmValue += pwmDirection;
    if (pwmValue >= 255 || pwmValue <= 10) {
      pwmDirection = -pwmDirection;
    }
    // Cannot pulse a mechanical relay, do nothing or force ON.
    digitalWrite(PIN_RELAY_DIM, LOW);
  }
}
