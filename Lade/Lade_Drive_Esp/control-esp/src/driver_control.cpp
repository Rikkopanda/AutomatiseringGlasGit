/**
 * @file driver_control.cpp
 * @brief Servo drive control implementation for Control ESP
 */

#include "driver_control.h"
#include "modbus_master.h"
#include "../../shared/modbus_common.h"
#include <Arduino.h>

// ============================================================================
// PRIVATE STATE
// ============================================================================

static DriveState driveState = DRIVE_STATE_IDLE;
static uint8_t initStep = 0;
static unsigned long lastInitTx = 0;
static unsigned long lastModbusTx = 0;

static int16_t commandedSpeed = 0;
static int16_t targetSpeed = 0;
static bool servoEnabled = false;
static uint16_t currentStatusBits = 0;

// ============================================================================
// INITIALIZATION SEQUENCE
// ============================================================================

/**
 * Run initialization sequence
 * Configures drive registers for Modbus control
 * 
 * NOTE: This assumes the drive's serial parameters are already set via keypad!
 * See master.cpp header comment for pre-configuration steps.
 */
static void runInitSequence() {
  const unsigned long INIT_DELAY_MS = 500;
  
  if (millis() - lastInitTx < INIT_DELAY_MS) {
    return; // Wait before next step
  }
  
  Serial.printf("[DriverControl] Init step %d\n", initStep);
  
  switch (initStep) {
    case 0:
      // Enable Modbus control of SON (servo on/off)
      modbusWriteSpeedToDrive(0);
      initStep++;
      break;
      
    case 1:
      // Set speed source to internal preset 1 (Pn168 = 1)
      // This is already set; we just verify via read in next poll
      initStep++;
      driveState = DRIVE_STATE_READY;
      Serial.println("[DriverControl] Initialization complete");
      break;
      
    default:
      driveState = DRIVE_STATE_READY;
      break;
  }
  
  lastInitTx = millis();
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void driverInit() {
  modbusInit(); // Initialize Modbus master first
  driveState = DRIVE_STATE_INIT_SEQ;
  commandedSpeed = 0;
  targetSpeed = 0;
  servoEnabled = false;
  
  Serial.println("[DriverControl] Initialized");
}

void driverTask() {
  modbusTask(); // Keep Modbus polling alive
  
  // Update status from drive feedback
  currentStatusBits = modbusDriveGetStatus();
  
  switch (driveState) {
    case DRIVE_STATE_IDLE:
      // Waiting to start
      break;
      
    case DRIVE_STATE_INIT_SEQ:
      runInitSequence();
      break;
      
    case DRIVE_STATE_READY:
    case DRIVE_STATE_DISABLED:
      // Normal operation
      if (commandedSpeed != targetSpeed) {
        commandedSpeed = targetSpeed;
        modbusWriteSpeedToDrive(commandedSpeed);
      }
      break;
      
    case DRIVE_STATE_ERROR:
      // In error state; wait for reset()
      break;
  }
}

DriveState driverGetState() {
  return driveState;
}

void driverReset() {
  driveState = DRIVE_STATE_INIT_SEQ;
  initStep = 0;
  lastInitTx = 0;
  commandedSpeed = 0;
  targetSpeed = 0;
}

void driverSetSpeed(int16_t speedRpm) {
  if (driveState == DRIVE_STATE_READY && servoEnabled) {
    targetSpeed = speedRpm;
  }
}

int16_t driverGetCommandedSpeed() {
  return commandedSpeed;
}

int16_t driverGetActualSpeed() {
  return modbusDriveGetActualSpeed();
}

void driverEnable() {
  if (driveState == DRIVE_STATE_READY) {
    servoEnabled = true;
    modbusSetServoEnable(true);
  }
}

void driverDisable() {
  servoEnabled = false;
  targetSpeed = 0;
  commandedSpeed = 0;
  modbusSetServoEnable(false);
}

bool driverIsEnabled() {
  return servoEnabled && driveState == DRIVE_STATE_READY;
}

void driverEmergencyStop() {
  targetSpeed = 0;
  commandedSpeed = 0;
  servoEnabled = false;
  modbusSetServoEnable(false);
  Serial.println("[DriverControl] EMERGENCY STOP");
}

uint16_t driverGetStatusBits() {
  return currentStatusBits;
}

bool driverIsAlarmed() {
  // Dn-18 bit0 = Alarm (bit=0 means ON)
  return (currentStatusBits & (1 << 0)) == 0;
}

bool driverIsReady() {
  // Dn-18 bit1 = Ready (bit=0 means ON)
  return (currentStatusBits & (1 << 1)) == 0;
}

bool driverIsRunning() {
  // Dn-18 bit7 = Run (bit=0 means ON)
  return (currentStatusBits & (1 << 7)) == 0;
}
