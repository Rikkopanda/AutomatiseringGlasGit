/**
 * @file modbus_master.cpp
 * @brief Modbus RTU Master implementation for Control ESP
 *
 * modbus-esp8266 holds one transaction. readHreg/writeHreg while one is
 * open returns false and still clears the library's receive length, so the
 * open transaction never completes. Every request goes out from modbusTask()
 * and only when the previous callback has run.
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
static bool uiEstop = false;
static uint16_t driveStatus = 0;
static int16_t actualSpeed = 0;
static int16_t commandedSpeed = 0;

// One bus transaction at a time
static bool txnActive = false;
static unsigned long txnStartMs = 0;

// Polling timing
static unsigned long lastUiPoll = 0;
static unsigned long lastDrivePoll = 0;
static uint16_t driveReg = 0;
static unsigned long drivePollBackoffUntil = 0;

// UI registers buffer
static uint16_t uiRegs[UI_REGISTERS_COUNT] = {0};

// Writes are latched and sent when the bus is idle.
static bool speedDirty = false;
static bool enableDirty = false;
static bool enableArmed = false;
static bool statusDirty = false;
static bool statusSent = false;
static uint16_t speedToWrite = 0;
static uint16_t enableToWrite = 0;
static uint16_t enableArmedValue = 0;
static uint16_t statusToWrite = 0;
static uint16_t statusLastSent = 0;
static uint16_t writeValueInFlight = 0;

enum WriteKind : uint8_t { WRITE_NONE, WRITE_SPEED, WRITE_ENABLE, WRITE_STATUS, WRITE_DRIVE };
static WriteKind writeInFlight = WRITE_NONE;

// Drive register writes leave one at a time. Stop drops anything still waiting.
static const uint8_t DRIVE_Q_SIZE = 12;
static uint16_t driveQReg[DRIVE_Q_SIZE];
static uint16_t driveQVal[DRIVE_Q_SIZE];
static uint8_t driveQHead = 0;
static uint8_t driveQCount = 0;
static bool driveWriteInFlight = false;

// ============================================================================
// CALLBACKS
// ============================================================================

static void txnFinished() {
  txnActive = false;
  writeInFlight = WRITE_NONE;
}

static bool onUIPollComplete(Modbus::ResultCode event, uint16_t, void*) {
  txnFinished();
  if (event == Modbus::EX_SUCCESS) {
    uiSetpoint = uiRegs[0];
    uiRun = uiRegs[1];
    uiEstop = (uiRegs[UI_REG_UI_STATUS] & UI_STATUS_ESTOP) != 0;
    Serial.printf("[ModbusMaster] UI poll OK: setpoint=%u run=%u estop=%u\n",
                  uiSetpoint, uiRun, uiEstop ? 1 : 0);
  } else {
    Serial.printf("[ModbusMaster] UI poll failed: 0x%02X\n", event);
  }
  return true;
}

// Pr5.31 is the station id. A read of it is the link check that already worked.
// Pr0.01 (0x0003) and Pr8.02 (0x6002) stayed silent on this drive.
static bool onDrivePollComplete(Modbus::ResultCode event, uint16_t, void*) {
  txnFinished();

  if (event == Modbus::EX_SUCCESS) {
    Serial.printf("[ModbusMaster] Drive link OK: slave=%u Pr5.31=%u\n",
                  DRIVE_SLAVE_ID, driveReg);
  } else {
    drivePollBackoffUntil = millis() + 5000;
    Serial.printf("[ModbusMaster] Drive link failed: slave=%u reg=0x%04X 0x%02X\n",
                  DRIVE_SLAVE_ID, REG_T6_PR531_SLAVE_ID, event);
  }
  return true;
}

static const char* writeKindName(WriteKind kind) {
  switch (kind) {
    case WRITE_SPEED: return "speed";
    case WRITE_ENABLE: return "enable";
    case WRITE_STATUS: return "status";
    case WRITE_DRIVE: return "drive";
    default: return "none";
  }
}

static void driveQueuePop() {
  if (driveQCount == 0) return;
  driveQHead = (driveQHead + 1) % DRIVE_Q_SIZE;
  driveQCount--;
}

static bool onWriteComplete(Modbus::ResultCode event, uint16_t, void*) {
  WriteKind kind = writeInFlight;
  uint16_t value = writeValueInFlight;
  txnFinished();
  if (kind == WRITE_DRIVE) {
    driveWriteInFlight = false;
    driveQueuePop();
  }
  if (event == Modbus::EX_SUCCESS) {
    if (kind == WRITE_STATUS) {
      statusSent = true;
      statusLastSent = statusToWrite;
    }
  } else {
    Serial.printf("[ModbusMaster] %s write failed: value=0x%04X 0x%02X\n",
                  writeKindName(kind), value, event);
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

static bool beginRead(uint8_t slave, uint16_t reg, uint16_t* dest, uint16_t count,
                      bool (*cb)(Modbus::ResultCode, uint16_t, void*)) {
  if (!initialized || txnActive) return false;
  if (mb.readHreg(slave, reg, dest, count, cb) == 0) return false;
  txnActive = true;
  txnStartMs = millis();
  return true;
}

static bool beginWrite(uint8_t slave, uint16_t reg, uint16_t value, WriteKind kind) {
  if (!initialized || txnActive) return false;
  if (mb.writeHreg(slave, reg, value, onWriteComplete) == 0) return false;
  writeInFlight = kind;
  writeValueInFlight = value;
  txnActive = true;
  txnStartMs = millis();
  return true;
}

void modbusPollUI() {
  beginRead(UI_ESP_SLAVE_ID, UI_REG_SETPOINT, uiRegs, UI_REGISTERS_COUNT, onUIPollComplete);
}

void modbusPollDrive() {
  driveReg = 0;
  beginRead(DRIVE_SLAVE_ID, REG_T6_PR531_SLAVE_ID, &driveReg, 1, onDrivePollComplete);
}

static bool enqueueDrive(uint16_t reg, uint16_t value) {
  if (driveQCount >= DRIVE_Q_SIZE) {
    Serial.println("[ModbusMaster] drive write queue full");
    return false;
  }
  uint8_t index = (driveQHead + driveQCount) % DRIVE_Q_SIZE;
  driveQReg[index] = reg;
  driveQVal[index] = value;
  driveQCount++;
  return true;
}

static void dropWaitingDriveWrites() {
  if (driveWriteInFlight && driveQCount > 0) {
    driveQCount = 1;
    return;
  }
  driveQHead = 0;
  driveQCount = 0;
}

void modbusConfigurePrMode() {
  enqueueDrive(REG_T6_PR001_MODE, T6_MODE_PR);
  enqueueDrive(REG_T6_PR400_SI1, T6_SI1_SERVO_ON);
  enqueueDrive(REG_T6_PR025_AUX, T6_AUX_SAVE);
  Serial.println("[ModbusMaster] PR mode queued (Pr0.01=6, servo on). Power-cycle the drive once.");
}

void modbusMoveAbsolute(int32_t pulses, uint16_t speedRpm) {
  if (pulses > T6_MOVE_MAX_PULSES) pulses = T6_MOVE_MAX_PULSES;
  if (pulses < 0) pulses = 0;
  if (speedRpm > T6_MOVE_SPEED_RPM) speedRpm = T6_MOVE_SPEED_RPM;

  enqueueDrive(REG_T6_PATH1_MODE, T6_PATH_ABSOLUTE);
  enqueueDrive(REG_T6_PATH1_POS_H, (uint16_t)((pulses >> 16) & 0xFFFF));
  enqueueDrive(REG_T6_PATH1_POS_L, (uint16_t)(pulses & 0xFFFF));
  enqueueDrive(REG_T6_PATH1_SPEED, speedRpm);
  enqueueDrive(REG_T6_PATH1_ACCEL, T6_MOVE_ACCEL_MS);
  enqueueDrive(REG_T6_PATH1_DECEL, T6_MOVE_ACCEL_MS);
  enqueueDrive(REG_T6_PR802_TRIGGER, T6_TRIGGER_PATH1);
  Serial.printf("[ModbusMaster] Move queued: %ld pulses at %u r/min\n",
                (long)pulses, speedRpm);
}

void modbusStopMotion() {
  dropWaitingDriveWrites();
  enqueueDrive(REG_T6_PR802_TRIGGER, T6_TRIGGER_ESTOP);
  Serial.println("[ModbusMaster] STOP queued");
}

void modbusEmergencyStop() {
  dropWaitingDriveWrites();
  enqueueDrive(REG_T6_PR802_TRIGGER, T6_TRIGGER_ESTOP);
  // 0x03 = SRV-ON normally open. With SI1 unwired the servo turns off.
  // The manual applies Pr4.00 after a drive power cycle.
  enqueueDrive(REG_T6_PR400_SI1, 0x0003);
  Serial.println("[ModbusMaster] EM STOP queued");
}

void modbusWriteStatusToUI(uint16_t statusBits) {
  statusToWrite = statusBits;
  if (statusSent && statusLastSent == statusBits) return;
  statusDirty = true;
}

void modbusWriteSpeedToDrive(int16_t speedRpm) {
  commandedSpeed = speedRpm;
  speedToWrite = (uint16_t)speedRpm;
  speedDirty = true;
}

void modbusSetServoEnable(bool enable) {
  uint16_t sonValue = enable ? (PN070_DEFAULT & ~1) : (PN070_DEFAULT | 1);
  // run==0 calls this every loop. One SON value is one bus transaction.
  if (enableArmed && enableArmedValue == sonValue) return;
  enableArmed = true;
  enableArmedValue = sonValue;
  enableToWrite = sonValue;
  enableDirty = true;
}

static bool flushWrite() {
  if (driveQCount > 0 && !driveWriteInFlight) {
    if (!beginWrite(DRIVE_SLAVE_ID, driveQReg[driveQHead], driveQVal[driveQHead], WRITE_DRIVE)) {
      return false;
    }
    driveWriteInFlight = true;
    Serial.printf("[ModbusMaster] drive 0x%04X <= 0x%04X\n",
                  driveQReg[driveQHead], driveQVal[driveQHead]);
    return true;
  }
  if (statusDirty) {
    if (!beginWrite(UI_ESP_SLAVE_ID, UI_REG_DRIVE_STATUS, statusToWrite, WRITE_STATUS)) return false;
    statusDirty = false;
    return true;
  }
  return false;
}

uint16_t modbusGetUISetpoint() {
  return uiSetpoint;
}

uint16_t modbusGetUIRun() {
  return uiRun;
}

bool modbusGetUIEstop() {
  return uiEstop;
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

  // Callback should have cleared txnActive. If the library is idle and our
  // flag is still set, the callback was lost; don't stay stuck.
  if (txnActive && mb.slave() == 0 && millis() - txnStartMs > 50) {
    txnActive = false;
    driveWriteInFlight = false;
    writeInFlight = WRITE_NONE;
  }
  if (txnActive) return;

  unsigned long now = millis();

  // UI first, so a drive that never answers cannot hold the bus every loop.
  if (now - lastUiPoll > MODBUS_POLL_INTERVAL_MS) {
    if (beginRead(UI_ESP_SLAVE_ID, UI_REG_SETPOINT, uiRegs, UI_REGISTERS_COUNT, onUIPollComplete)) {
      lastUiPoll = now;
    }
    return;
  }

  if (flushWrite()) return;

  if (now >= drivePollBackoffUntil &&
      now - lastDrivePoll > MODBUS_DRIVE_POLL_INTERVAL_MS) {
    if (beginRead(DRIVE_SLAVE_ID, REG_T6_PR531_SLAVE_ID, &driveReg, 1, onDrivePollComplete)) {
      lastDrivePoll = now;
    }
  }
}
