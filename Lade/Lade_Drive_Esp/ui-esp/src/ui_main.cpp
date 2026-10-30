/**
 * @file ui_main.cpp
 * @brief Main application for UI ESP (Slave)
 * 
 * Responsibilities:
 * - Manage LCD display (4x2 I2C)
 * - Handle 2 rotary encoders (A/B + button)
 * - Handle 7 buttons with LEDs
 * - Handle 4x4 membrane keypad
 * - Expose user input via Modbus to control ESP
 * - Receive drive status updates via Modbus and display
 */

#include <Arduino.h>
#include "modbus_slave.h"
#include "lcd_display.h"
#include "rotary_encoder.h"
#include "button_handler.h"
#include "keypad_handler.h"
#include "../../shared/modbus_common.h"

// ============================================================================
// APPLICATION STATE
// ============================================================================

uint16_t currentSetpoint = 0;
uint16_t currentRun = 0;

unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL_MS = 300;

unsigned long lastSerialReport = 0;
const unsigned long SERIAL_REPORT_INTERVAL_MS = 1000;

// ============================================================================
// INPUT HANDLERS
// ============================================================================

static bool reset_alarm_status = false;

// Button 5 is the reset alarm button
// implemented as rising edge trigger
static void publishAlarmReset() {
  bool alarm_reset = buttonIsPressed(BUTTON_5);
  bool estop = buttonIsPressed(BUTTON_7);

  // Serial.println(alarm_reset ? "[UI] RESET" : "[UI] NO RESET");
  // Serial.println(reset_alarm_status ? "[UI] reset_alarm_status" : "[UI] NO reset_alarm_status");

  // if (estop)
  // {
  //   modbusSlaveSetResetAlarm(0);
  //   return;
  // }

  if (alarm_reset)
  {
      modbusSlaveSetResetAlarm(1);
  }
  else
  {
      modbusSlaveSetResetAlarm(0);
  }
  return;
  if (reset_alarm_status == false && alarm_reset == true)
  {
    modbusSlaveSetResetAlarm(1);
    reset_alarm_status = true;
    Serial.println("YEAH BOI");

  }
  else if (reset_alarm_status == true && alarm_reset == false)
  {
    modbusSlaveSetResetAlarm(0);
    reset_alarm_status = false;
  }
  else if ((reset_alarm_status == false && alarm_reset == false) || (reset_alarm_status == true && alarm_reset == true))
  {
    modbusSlaveSetResetAlarm(0);
  }
}

/**
 * Process rotary encoder input
 * Adjusts speed setpoint
 */
// Button 7 is the e-stop. A reading of 0 (pin low) stops and disables the drive.
static void publishEstop() {
  bool estop = buttonIsPressed(BUTTON_7);
  static bool wasEstop = false;

  if (estop && currentRun != 0) {
    currentRun = 0;
    modbusSlaveSetRun(0);
  }
  if (estop != wasEstop) {
    Serial.println(estop ? "[UI] EM STOP" : "[UI] EM STOP released");
    wasEstop = estop;
  }

  uint16_t uiStatus = estop ? UI_STATUS_ESTOP : 0;
  if (currentRun) uiStatus |= 1;
  modbusSlaveSetUIStatus(uiStatus);
}



static void handleEncoder() {
  int8_t delta = encoderGetDelta(ENCODER_MAIN);
  
  if (delta != 0) {
    // Encoder provides +/-1 per pulse; scale it up for user-friendly speed steps
    int16_t speedDelta = delta * ENCODER_STEP;
    int16_t newSpeed = (int16_t)currentSetpoint + speedDelta;
    
    // Clamp to valid range
    if (newSpeed < ENCODER_MIN_SPEED) newSpeed = ENCODER_MIN_SPEED;
    if (newSpeed > ENCODER_MAX_SPEED) newSpeed = ENCODER_MAX_SPEED;
    
    currentSetpoint = (uint16_t)newSpeed;
    modbusSlaveSetSetpoint(currentSetpoint);
    
    Serial.printf("[UI] Encoder: setpoint = %u r/min\n", currentSetpoint);
  }
  
  // Encoder button toggles run/stop. run = 0 is the stop message.
  if (encoderButtonWasPressed(ENCODER_MAIN)) {
    currentRun = currentRun ? 0 : 1;
    modbusSlaveSetRun(currentRun);
    if (currentRun == 0) Serial.println("[UI] STOP");
    else Serial.printf("[UI] Encoder button: run = %u\n", currentRun);
  }
}

static void handleEncoderAccel() {
  int8_t delta = encoderGetDelta(ENCODER_SECONDARY);
  
  if (delta != 0) {
    // Encoder provides +/-1 per pulse; scale it up for user-friendly speed steps
    int16_t speedDelta = delta * ENCODER_STEP;
    int16_t newSpeed = (int16_t)currentSetpoint + speedDelta;
    
    // Clamp to valid range
    if (newSpeed < ENCODER_MIN_SPEED) newSpeed = ENCODER_MIN_SPEED;
    if (newSpeed > ENCODER_MAX_SPEED) newSpeed = ENCODER_MAX_SPEED;
    
    currentSetpoint = (uint16_t)newSpeed;
    modbusSlaveSetSetpoint(currentSetpoint);
    
    Serial.printf("[UI] Encoder: setpoint = %u r/min\n", currentSetpoint);
  }
  
  // Encoder button toggles run/stop. run = 0 is the stop message.
  if (encoderButtonWasPressed(ENCODER_SECONDARY)) {
    currentRun = currentRun ? 0 : 1;
    modbusSlaveSetRun(currentRun);
    if (currentRun == 0) Serial.println("[UI] STOP");
    else Serial.printf("[UI] Encoder button: run = %u\n", currentRun);
  }
}

/**
 * Process 7-button panel input
 */
static void handleButtons() {
  // Panel wiring: Button 0 is START and Button 1 is STOP.
  if (buttonWasPressed(BUTTON_0)) {
    currentRun = 1;
    modbusSlaveSetRun(1);
    Serial.println("[UI] Button 0 (START) pressed");
  }
  
  if (buttonWasPressed(BUTTON_1)) {
    currentRun = 0;
    modbusSlaveSetRun(0);
    Serial.println("[UI] Button 1 (STOP) pressed");
  }
  
  // Buttons 2-6 reserved for future features
  for (int i = 2; i < NUM_BUTTONS; i++) {
    if (buttonWasPressed((ButtonID)i)) {
      Serial.printf("[UI] Button %d pressed (reserved for future)\n", i);
    }
  }
  
  // Note: Button LEDs are passive (integrated) - they light automatically
  // when buttons are pressed. No active LED control needed.
}

/**
 * Process keypad input
 */
static void handleKeypad() {
  char key = keypadGetKeyPress();
  
  if (key != '\0') {
    Serial.printf("[UI] Keypad: '%c'\n", key);
    
    // Example: Use keypad for direct speed entry or mode selection
    // (Implementation depends on your UI design)
    if (key >= '0' && key <= '9') {
      // Could implement numeric entry mode
    } else if (key == '*') {
      currentRun = 0;
      modbusSlaveSetRun(0);
      Serial.println("[UI] STOP");
    } else if (key == '#') {
      // Example: Run full speed
      currentSetpoint = ENCODER_MAX_SPEED;
      modbusSlaveSetSetpoint(currentSetpoint);
    }
  }
}

// ============================================================================
// DISPLAY UPDATE
// ============================================================================

static void updateDisplay() {
  if (millis() - lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL_MS) {
    return; // Throttle updates
  }
  lastDisplayUpdate = millis();
  
  // Get drive status from master (via Modbus)
  uint16_t driveStatus = modbusSlaveGetDriveStatus();
  
  // Update LCD with current speed and status
  lcdUpdateSpeed(currentSetpoint, currentRun ? true : false, driveStatus);
  
}

// ============================================================================
// SETUP
// ============================================================================
static long long best_lang;

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("  UI ESP32 (SLAVE)");
  Serial.println("  Version: 1.0");
  Serial.println("========================================");
  Serial.println();
  
  // Initialize all input devices
  Serial.println("[SETUP] Initializing devices...");
  
  Serial.println("[SETUP] LCD init...");
  lcdInit();
  Serial.println("[SETUP] LCD done");

  Serial.println("[SETUP] Button init...");
  buttonInit();
  Serial.println("[SETUP] Button done");
  
  encoderInit();
  
  // Initialize Modbus slave
  modbusSlaveInit();
  
  Serial.println("[SETUP] Setup complete!");
  Serial.println();
  best_lang = 0;
  // lcdClear();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

char str[32];
void loop() {
  // ---- Modbus slave communication ----
  modbusSlaveTask();
  
  // ---- Input processing ----
  encoderTask();
  buttonTask();
  
  // ---- Handle user inputs ----
  handleEncoder();
  handleEncoderAccel();
  handleButtons();
  publishEstop();
  publishAlarmReset();

  // ---- Update display ----
  lcdTask();
  updateDisplay();
  
  best_lang += 1;
  // ---- Periodic diagnostics ----
  if (millis() - lastSerialReport > 1000) {
    lastSerialReport = millis();
    
    // sprintf(str, "%d", best_lang);
    // printlcd(str);
    Serial.printf("[UI] State: setpoint=%u run=%u master=%s\n",
                  currentSetpoint, currentRun,
                  modbusSlaveHasActiveMaster() ? "OK" : "LOST");
  }
  
  delay(10); // Small delay to avoid hogging CPU
}
