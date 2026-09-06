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
const unsigned long SERIAL_REPORT_INTERVAL_MS = 5000;

// ============================================================================
// INPUT HANDLERS
// ============================================================================

/**
 * Process rotary encoder input
 * Adjusts speed setpoint
 */
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
  
  // Encoder button toggles run/stop
  if (encoderButtonWasPressed(ENCODER_MAIN)) {
    currentRun = currentRun ? 0 : 1;
    modbusSlaveSetRun(currentRun);
    
    Serial.printf("[UI] Encoder button: run = %u\n", currentRun);
  }
}

/**
 * Process 7-button panel input
 */
static void handleButtons() {
  // Example: Button 0 = Run, Button 1 = Stop, others for future use
  if (buttonWasPressed(BUTTON_0)) {
    currentRun = 1;
    modbusSlaveSetRun(1);
    Serial.println("[UI] Button 0 (Run) pressed");
  }
  
  if (buttonWasPressed(BUTTON_1)) {
    currentRun = 0;
    modbusSlaveSetRun(0);
    Serial.println("[UI] Button 1 (Stop) pressed");
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
      // Example: Emergency stop
      currentRun = 0;
      modbusSlaveSetRun(0);
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
  
  // Update UI status in Modbus
  uint16_t uiStatus = 0;
  if (currentRun) uiStatus |= (1 << 0);
  modbusSlaveSetUIStatus(uiStatus);
}

// ============================================================================
// SETUP
// ============================================================================

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
  Serial.println("Initializing devices...");
  
  lcdInit();
  // encoderInit();
  buttonInit();
  
  // Initialize Modbus slave
  // modbusSlaveInit();
  
  Serial.println("Setup complete!");
  Serial.println();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  // ---- Modbus slave communication ----
  modbusSlaveTask();
  
  // ---- Input processing ----
  encoderTask();
  buttonTask();
  
  // ---- Handle user inputs ----
  handleEncoder();
  handleButtons();
  
  // ---- Update display ----
  lcdTask();
  updateDisplay();
  
  // ---- Periodic diagnostics ----
  if (millis() - lastSerialReport > SERIAL_REPORT_INTERVAL_MS) {
    lastSerialReport = millis();
    
    Serial.printf("[UI] State: setpoint=%u run=%u master=%s\n",
                  currentSetpoint, currentRun,
                  modbusSlaveHasActiveMaster() ? "OK" : "LOST");
  }
  
  delay(5); // Small delay to avoid hogging CPU
}
