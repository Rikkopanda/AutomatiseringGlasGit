/**
 * @file modbus_master.cpp
 * @brief Modbus RTU Master implementation for Control ESP
 */

#include "modbus_master.h"
#include "../../shared/modbus_common.h"
#include <ModbusRTU.h>
#include <Arduino.h>

// ============================================================================
// PRIVATE STATE
// ============================================================================

static ModbusRTU mb;
static HardwareSerial* modbusSerial = &Serial2;  // UART1 on Control ESP
static bool initialized = false;

// Last polled values
static uint16_t uiSetpoint = 0;
static uint16_t uiRun = 0;
static uint16_t driveStatus = 0;
static int16_t actualSpeed = 0;
static int16_t commandedSpeed = 0;

// Polling timing
static unsigned long lastUiPoll = 0;
static unsigned long lastDrivePoll = 0;
static bool uiPollPending = false;
static bool drivePollPending = false;
static uint16_t driveRegs[2] = {0};

// UI registers buffer
static uint16_t uiRegs[UI_REGISTERS_COUNT] = {0};

// ============================================================================
// CALLBACKS
// ============================================================================

/**
 * Callback when UI poll completes
 */
static bool onUIPollComplete(Modbus::ResultCode event, uint16_t, void*) {
  uiPollPending = false;
  if (event == Modbus::EX_SUCCESS) {
    uiSetpoint = uiRegs[0];
    uiRun = uiRegs[1];
    Serial.printf("[ModbusMaster] UI poll OK: setpoint=%u run=%u\n", uiSetpoint, uiRun);
  } else {
    Serial.printf("[ModbusMaster] UI poll failed: 0x%02X\n", event);
  }
  return true;
}

/**
 * Callback when drive poll completes
 */
static bool onDrivePollComplete(Modbus::ResultCode event, uint16_t, void*) {
  drivePollPending = false;
  if (event == Modbus::EX_SUCCESS) {
    Serial.printf("[ModbusMaster] Drive poll OK: speed=%d status=0x%04X\n", actualSpeed, driveStatus);
  } else {
    Serial.printf("[ModbusMaster] Drive poll failed: 0x%02X\n", event);
  }
  return true;
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void modbusInit() {
  Serial2.begin(MODBUS_BAUDRATE, MODBUS_SERIAL_CFG, CONTROL_RXD, CONTROL_TXD);
  
  mb.begin(&Serial2, -1); // -1 = no direction pin, auto-direction on RS-485 adapter
  mb.master();
  
  initialized = true;
  Serial.println("[ModbusMaster] Initialized on Serial2 (GPIO16/17)");
}

void modbusPollUI() {
  if (!initialized || uiPollPending) 
  {
    Serial.printf("[ModbusMaster] UI poll skipped (init=%d pending=%d)\n", initialized, uiPollPending);
    return;
  }
  bool ok = mb.readHreg(UI_ESP_SLAVE_ID, UI_REG_SETPOINT, uiRegs, 3, onUIPollComplete);
  uiPollPending = ok;
  Serial.printf("[ModbusMaster] UI poll issued, readHreg returned %d\n", ok);
}

void modbusPollDrive() {
  if (!initialized || drivePollPending)
  {
    Serial.printf("[ModbusMaster] Drive poll skipped (init=%d pending=%d)\n", initialized, uiPollPending);
    return;
  }
  
  bool ok = mb.readHreg(DRIVE_SLAVE_ID, REG_DN08_ACTUAL_SPEED, driveRegs, 2, onDrivePollComplete) != 0;
  
  drivePollPending = ok;
  Serial.printf("[ModbusMaster] Drive poll issued, readHreg returned %d\n", ok);
}

void modbusWriteStatusToUI(uint16_t statusBits) {
  if (!initialized) return;
  
  mb.writeHreg(UI_ESP_SLAVE_ID, UI_REG_DRIVE_STATUS, statusBits, nullptr);
}

void modbusWriteSpeedToDrive(int16_t speedRpm) {
  if (!initialized) return;
  
  commandedSpeed = speedRpm;
  mb.writeHreg(DRIVE_SLAVE_ID, REG_PN169_SPEED_SET, (uint16_t)speedRpm, nullptr);
}

void modbusSetServoEnable(bool enable) {
  if (!initialized) return;
  
  uint16_t sonValue = enable ? (PN070_DEFAULT & ~1) : (PN070_DEFAULT | 1);
  mb.writeHreg(DRIVE_SLAVE_ID, REG_PN070_COMM_STATE, sonValue, nullptr);
}

uint16_t modbusGetUISetpoint() {
  return uiSetpoint;
}

uint16_t modbusGetUIRun() {
  return uiRun;
}

uint16_t modbusDriveGetStatus() {
  return driveStatus;
}

int16_t modbusDriveGetActualSpeed() {
  return actualSpeed;
}

bool modbusIsReady() {
  return initialized;
}

void modbusTask() {
  if (!initialized) return;
  
  mb.task();

  // Poll UI ESP at regular interval
  if (millis() - lastUiPoll > MODBUS_POLL_INTERVAL_MS) {
    lastUiPoll = millis();
    modbusPollUI();
  }
  
  // Poll drive at regular interval
  // if (millis() - lastDrivePoll > MODBUS_DRIVE_POLL_INTERVAL_MS) {
  //   lastDrivePoll = millis();
  //   modbusPollDrive();
  // }
}
