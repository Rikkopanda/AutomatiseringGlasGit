/**
 * @file control_main.cpp
 * @brief Main application for Control ESP (Master)
 * 
 * Responsibilities:
 * - Poll UI ESP for user commands (setpoint, run)
 * - Poll servo drive for feedback (speed, status)
 * - Control servo drive based on UI commands
 * - Relay drive status back to UI for display
 * 
 * Serial commands (115200 baud):
 *   help          - show available commands
 *   status        - print drive speed, position, enable state
 *   readdi        - read and print DI function config from drive
 *   writedi       - write SI1=enable, SI2=E-stop, SI3=low limit, SI4=high limit (NC)
 *   alarm         - read alarm code from drive
 *   motion        - read PR motion and physical input status from drive
 *   pos           - read and print actual + command position
 *   enable        - assert GPIO13 to enable drive (SRV-ON)
 *   disable       - deassert GPIO13 (servo off)
 *   invert        - toggle enable pin active level
 *   home          - trigger homing routine on drive
 *   move <n>      - absolute move to n pulses (e.g. move 10000)
 *   movel <n>     - relative move by n pulses (e.g. movel -5000)
 *   speed <n>     - set move speed in r/min for next move (default 60)
 *   accel <n>     - set accel/decel time ms/1000rpm for next move (default 500)
 *   stop          - e-stop current motion
 *   reset         - send alarm reset to drive
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

// Serial command state
static uint16_t serialMoveSpeed = T6_MOVE_SPEED_RPM;   // default 60 rpm
static uint16_t serialMoveAccel = T6_MOVE_ACCEL_MS;    // default 500 ms/1000rpm
static bool enableActiveHigh = !(ENABLE_ACTIVE_LOW);   // from shared header

// ============================================================================
// SERIAL COMMAND HANDLER
// ============================================================================

static void printHelp() {
  Serial.println("========================================");
  Serial.println("  Control ESP serial commands");
  Serial.println("========================================");
  Serial.println("  help           - this message");
  Serial.println("  status         - print live drive state");
  Serial.println("  readdi         - read DI function config (Pr4.00-03)");
  Serial.println("  writedi        - SI1=external enable, SI2=ESTOP, SI3=low, SI4=high");
  Serial.println("  alarm          - read alarm code from drive");
  Serial.println("  motion         - read motion state and SI1-SI4 input levels");
  Serial.println("  pos            - read actual + command position");
  Serial.println("  enable         - assert GPIO13 (servo enable)");
  Serial.println("  disable        - deassert GPIO13 (servo off)");
  Serial.println("  invert         - toggle GPIO13 active level (HIGH/LOW)");
  Serial.println("  home           - trigger homing routine");
  Serial.println("  move <pulses>  - absolute move (e.g. move 10000)");
  Serial.println("  movel <pulses> - relative move (e.g. movel -5000)");
  Serial.println("  speed <rpm>    - set move speed for next move (def 60)");
  Serial.println("  accel <ms>     - set accel time ms/1000rpm (def 500)");
  Serial.println("  stop           - immediate e-stop");
  Serial.println("  reset          - send alarm reset");
  Serial.println("========================================");
}

static void printStatus() {
  Serial.println("[Status]");
  Serial.printf("  Drive state   : %d (%s)\n",
    (int)driverGetState(),
    driverGetState() == DRIVE_STATE_READY ? "READY" :
    driverGetState() == DRIVE_STATE_INIT_SEQ ? "INIT" :
    driverGetState() == DRIVE_STATE_ERROR ? "ERROR" :
    driverGetState() == DRIVE_STATE_DISABLED ? "DISABLED" : "IDLE");
  Serial.printf("  Servo enabled : %s (GPIO13=%s)\n",
    driverIsEnabled() ? "YES" : "NO",
    enableActiveHigh ? "HIGH=ON" : "LOW=ON");
  Serial.printf("  Actual speed  : %d r/min\n", driverGetActualSpeed());
  Serial.printf("  Actual pos    : %ld pulses (%.3f rev)\n",
    (long)modbusDriveGetActualPosition(),
    modbusDriveGetActualPosition() / 10000.0f);
  Serial.printf("  Command pos   : %ld pulses (%.3f rev)\n",
    (long)modbusDriveGetCommandPosition(),
    modbusDriveGetCommandPosition() / 10000.0f);
  Serial.printf("  Status bits   : 0x%04X\n", driverGetStatusBits());
  Serial.printf("    Alarmed     : %s\n", driverIsAlarmed() ? "YES" : "no");
  Serial.printf("    Ready       : %s\n", driverIsReady() ? "YES" : "no");
  Serial.printf("    Running     : %s\n", driverIsRunning() ? "YES" : "no");
  Serial.printf("  Move speed    : %u r/min\n", serialMoveSpeed);
  Serial.printf("  Move accel    : %u ms/1000rpm\n", serialMoveAccel);
  Serial.printf("  UI setpoint   : %u\n", modbusGetUISetpoint());
  Serial.printf("  UI run        : %u\n", modbusGetUIRun());
  Serial.printf("  UI estop      : %s\n", modbusGetUIEstop() ? "YES" : "no");
}

static void handleSerialCommands() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  Serial.printf("> %s\n", line.c_str());

  // Parse command and optional argument
  int spaceIdx = line.indexOf(' ');
  String cmd = (spaceIdx < 0) ? line : line.substring(0, spaceIdx);
  String arg = (spaceIdx < 0) ? "" : line.substring(spaceIdx + 1);
  cmd.toLowerCase();
  arg.trim();

  if (cmd == "help") {
    printHelp();

  } else if (cmd == "status") {
    printStatus();

  } else if (cmd == "readdi") {
    modbusReadDIConfig();

  } else if (cmd == "writedi") {
    modbusWriteDILimitSwitches();

  } else if (cmd == "alarm") {
    modbusReadAlarmAndDI();

  } else if (cmd == "motion") {
    modbusReadMotionDiagnostics();

  } else if (cmd == "pos") {
    modbusReadPositionStatus();

  } else if (cmd == "enable") {
    setDriveEnabled(true);
    Serial.printf("[Cmd] Enable pin GPIO%d -> %s\n",
      CONTROL_ENABLE_PIN, enableActiveHigh ? "HIGH" : "LOW");

  } else if (cmd == "disable") {
    setDriveEnabled(false);
    Serial.printf("[Cmd] Enable pin GPIO%d -> %s\n",
      CONTROL_ENABLE_PIN, enableActiveHigh ? "LOW" : "HIGH");

  } else if (cmd == "invert") {
    enableActiveHigh = !enableActiveHigh;
    Serial.printf("[Cmd] Enable active level now: %s\n", enableActiveHigh ? "HIGH" : "LOW");

  } else if (cmd == "home") {
    modbusStartHoming();

  } else if (cmd == "move") {
    if (arg.length() == 0) {
      Serial.println("[Cmd] Usage: move <pulses>  (e.g. move 10000)");
    } else {
      int32_t pulses = arg.toInt();
      modbusMoveAbsolute(pulses, serialMoveSpeed, serialMoveAccel);
    }

  } else if (cmd == "movel") {
    if (arg.length() == 0) {
      Serial.println("[Cmd] Usage: movel <pulses>  (e.g. movel -5000)");
    } else {
      int32_t pulses = arg.toInt();
      modbusMoveRelative(pulses, serialMoveSpeed, serialMoveAccel);
    }

  } else if (cmd == "speed") {
    if (arg.length() == 0) {
      Serial.printf("[Cmd] Current move speed: %u r/min\n", serialMoveSpeed);
    } else {
      serialMoveSpeed = (uint16_t)constrain(arg.toInt(), 1, 3000);
      Serial.printf("[Cmd] Move speed set to %u r/min\n", serialMoveSpeed);
    }

  } else if (cmd == "accel") {
    if (arg.length() == 0) {
      Serial.printf("[Cmd] Current accel: %u ms/1000rpm\n", serialMoveAccel);
    } else {
      serialMoveAccel = (uint16_t)constrain(arg.toInt(), 1, 5000);
      Serial.printf("[Cmd] Accel set to %u ms/1000rpm\n", serialMoveAccel);
    }

  } else if (cmd == "stop") {
    modbusStopMotion();

  } else if (cmd == "reset") {
    modbusResetAlarm();
    Serial.println("[Cmd] Alarm reset queued");

  } else {
    Serial.printf("[Cmd] Unknown command: '%s'. Type 'help'.\n", cmd.c_str());
  }
}

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

  #if USE_ENABLE_PIN
    pinMode(CONTROL_ENABLE_PIN, OUTPUT);
    setDriveEnabled(false);
  #endif


  Serial.println("Setup complete. Waiting for drive...");
  printHelp();
  Serial.println();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  // ---- Core tasks ----
  modbusTask(); // Keep Modbus polling alive

  driverTask(); // Updates state, communicates with drive and UI
  
  // ---- Serial command interface ----
  handleSerialCommands();

  // ---- Get current inputs from UI ----
  uint16_t uiSetpoint = modbusGetUISetpoint();
  uint16_t uiRun = modbusGetUIRun();
  
  // Button 7 == 0 is e-stop: stop the path and command servo-off.
  // Releasing it does not start a move; run has to be pressed again.
  static uint16_t lastRun = 0;
  static bool estopActive = false;
  static unsigned long lastEstopCmd = 0;

  setDriveEnabled(uiRun);
  // digitalWrite(13, HIGH);
  static bool resetHeld = false;
  bool resetNow = modbusGetUI_Reset_Alarm();
  if (resetNow && !resetHeld) {
    modbusResetAlarm();
  }
  resetHeld = resetNow;

  if (modbusGetUIEstop()) {
    if (!estopActive || millis() - lastEstopCmd > 1000) {
      driverEmergencyStop();
      lastEstopCmd = millis();
      estopActive = true;
    }
    lastRun = 0;
  } else {
    estopActive = false;
    if (uiRun != lastRun) {
      if (uiRun == 0) {
        driverStopMotion();
      } else if (driverGetState() == DRIVE_STATE_READY) {
        int32_t pulses = (int32_t)uiSetpoint;
        if (pulses > T6_MOVE_MAX_PULSES) pulses = T6_MOVE_MAX_PULSES;
        driverMoveAbsolute(pulses, serialMoveSpeed, serialMoveAccel);
      }
      lastRun = uiRun;
    }
  }
  
  // ---- Relay drive status back to UI ----
  uint16_t driveStatus = driverGetStatusBits();
  modbusWriteStatusToUI(driveStatus);
  
  // ---- Periodic diagnostics ----
  if (millis() - lastStatusReport > STATUS_REPORT_INTERVAL_MS) {
    lastStatusReport = millis();
    Serial.printf("[Control] State=%d UI: sp=%u run=%u | Drive: spd=%d pos=%ld cmd=%ld status=0x%04X\n",
                  (int)driverGetState(),
                  uiSetpoint, uiRun,
                  driverGetActualSpeed(),
                  (long)modbusDriveGetActualPosition(),
                  (long)modbusDriveGetCommandPosition(),
                  driveStatus);
  }
  
  delay(10); // Small delay to avoid hogging CPU
}
