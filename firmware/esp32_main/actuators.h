/*
 * ============================================
 * IoT Smart Room - Actuator Module Header
 * Controls relays, stepper motor, LEDs
 * ============================================
 */

#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <Arduino.h>
#include <Stepper.h>
#include "config.h"

// LED modes
enum LEDMode {
  LED_OFF,
  LED_DIM,
  LED_BRIGHT,
  LED_BLINK_SLOW,
  LED_BLINK_FAST,
  LED_PULSE
};

// Door states
enum DoorState {
  DOOR_CLOSED,
  DOOR_OPENING,
  DOOR_OPEN,
  DOOR_CLOSING
};

class ActuatorManager {
public:
  ActuatorManager();
  void begin();
  void update();  // Call in loop for non-blocking operations
  
  // Relay control
  void setRelay(int relayNum, bool state);  // 1-4 for main, 5 for KY-019
  bool getRelayState(int relayNum);
  
  // Light control
  void setLightMode(LEDMode mode);
  void setDimLevel(uint8_t level);  // 0-255
  void setRoomLight(int room, bool on);

  // PIR-driven light control (tách biệt từng phòng)
  // Room 1: KY-019 PWM → DIM (no motion) / BRIGHT (motion)
  // Room 2: Relay 4   → OFF  (no motion) / ON    (motion)
  void setPIRLight(int room, bool motionDetected);
  
  // Fan control
  void setFan(bool on);
  bool isFanOn() { return fanState; }
  
  // Speaker/Alarm control
  void setAlarm(bool on);
  bool isAlarmOn() { return alarmState; }
  
  // Stepper motor (door)
  void openDoor();
  void closeDoor();
  DoorState getDoorState() { return doorState; }
  
  // LED indicators
  void setLED(int room, int color, bool state);  // room 1-3, color 0=R,1=G,2=Y
  void blinkAllLEDs(bool enable);
  
  // Emergency mode
  void activateEmergency();
  void deactivateEmergency();
  bool isEmergencyActive() { return emergencyMode; }

private:
  Stepper stepper;
  
  bool relayStates[5];    // 4-relay module + KY-019
  bool fanState;
  bool alarmState;
  DoorState doorState;
  LEDMode currentLEDMode;
  uint8_t dimLevel;
  bool emergencyMode;
  
  // Non-blocking blink
  unsigned long lastBlinkTime;
  bool blinkState;
  bool blinkEnabled;
  
  // Non-blocking stepper
  int stepsRemaining;
  int stepDirection;
  unsigned long lastStepTime;
  
  void updateBlink();
  void updateStepper();
  void updatePWM();
};

#endif // ACTUATORS_H
