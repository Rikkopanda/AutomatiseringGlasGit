/**
 * @file control_main.cpp
 * @brief Main application for Control ESP (Master)
 * 
 * Responsibilities:
 * - Poll UI ESP for user commands (setpoint, run)
 * - Poll servo drive for feedback (speed, status)
 * - Control servo drive based on UI commands
 * - Relay drive status back to UI for display
 */

#include <Arduino.h>
#include "driver_control.h"
#include "modbus_master.h"
#include "../../shared/modbus_common.h"

// ============================================================================
// APPLICATION STATE
// ============================================================================

unsigned long lastStatusReport = 0;
const unsigned long STATUS_REPORT_INTERVAL_MS = 2000;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("  CONTROL ESP32 (MASTER)");
  Serial.println("  Version: 1.0");
  Serial.println("========================================");
  Serial.println();
  
  // Initialize driver control (which initializes Modbus inside)
  driverInit();
  
  Serial.println("Setup complete. Waiting for drive...");
  Serial.println();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  // ---- Core tasks ----
  driverTask(); // Updates state, communicates with drive and UI
  
  // ---- Get current inputs from UI ----
  uint16_t uiSetpoint = modbusGetUISetpoint();
  uint16_t uiRun = modbusGetUIRun();
  
  // ---- Control logic ----
  if (driverGetState() == DRIVE_STATE_READY) {
    if (uiRun) {
      driverEnable();
      driverSetSpeed((int16_t)uiSetpoint);
    } else {
      driverDisable();
    }
  }
  
  // ---- Relay drive status back to UI ----
  uint16_t driveStatus = driverGetStatusBits();
  modbusWriteStatusToUI(driveStatus);
  
  // ---- Periodic diagnostics ----
  if (millis() - lastStatusReport > STATUS_REPORT_INTERVAL_MS) {
    lastStatusReport = millis();
    
    Serial.printf("[Control] State=%d, UI: setpoint=%u run=%u, Drive: actual=%d status=0x%04X\n",
                  (int)driverGetState(),
                  uiSetpoint, uiRun,
                  driverGetActualSpeed(), driveStatus);
  }
  
  delay(10); // Small delay to avoid hogging CPU
}
