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
  modbusTask(); // Keep Modbus polling alive

  driverTask(); // Updates state, communicates with drive and UI
  
  // ---- Get current inputs from UI ----
  uint16_t uiSetpoint = modbusGetUISetpoint();
  uint16_t uiRun = modbusGetUIRun();
  
  // Run rising edge starts one absolute move. Run = 0 (UI stop) e-stops it.
  static uint16_t lastRun = 0;
  if (uiRun != lastRun) {
    if (uiRun == 0) {
      driverStopMotion();
    } else if (driverGetState() == DRIVE_STATE_READY) {
      int32_t pulses = (int32_t)uiSetpoint;
      if (pulses > T6_MOVE_MAX_PULSES) pulses = T6_MOVE_MAX_PULSES;
      driverMoveAbsolute(pulses, T6_MOVE_SPEED_RPM);
    }
    lastRun = uiRun;
  }
  
  // ---- Relay drive status back to UI ----
  uint16_t driveStatus = driverGetStatusBits();
  modbusWriteStatusToUI(driveStatus);
  
  // ---- Periodic diagnostics ----
  if (millis() - lastStatusReport > STATUS_REPORT_INTERVAL_MS) {
    lastStatusReport = millis();
    // Serial.printf("[Control] UI: setpoint=%u run=%u\n",
    //               uiSetpoint, uiRun);
    Serial.printf("[Control] State=%d, UI: setpoint=%u run=%u, Drive: actual=%d status=0x%04X\n",
                  (int)driverGetState(),
                  uiSetpoint, uiRun,
                  driverGetActualSpeed(), driveStatus);
  }
  
  delay(10); // Small delay to avoid hogging CPU
}
