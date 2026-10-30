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
static bool uiResetAlarm = false;
static uint16_t driveStatus = 0;
static int16_t actualSpeed = 0;
static int16_t commandedSpeed = 0;

// Drive position state
static int32_t driveActualPosition = 0;
static int32_t driveCommandPosition = 0;

// One bus transaction at a time
static bool txnActive = false;
static unsigned long txnStartMs = 0;

// Polling timing
static unsigned long lastUiPoll = 0;
static unsigned long uiPollBackoffUntil = 0;
static unsigned long lastDrivePoll = 0;
static unsigned long drivePollBackoffUntil = 0;

// UI registers buffer
static uint16_t uiRegs[UI_REGISTERS_COUNT] = {0};

// Drive poll reads 2 separate blocks. Block A: speed+alarm+DI status (0x0B00..0x0B03+0x0B11)
// We poll speed separately (single reg at 0x0B00), then PR status block.
static uint16_t driveSpeedReg = 0;       // 0x0B00
static uint16_t driveAlarmReg = 0;       // 0x0B03
static uint16_t driveDIStatusReg = 0;    // 0x0B11

// Position block: 4 regs at 0x602A (cmd pos H, cmd pos L, motor pos H, motor pos L)
static uint16_t drivePosRegs[4] = {0};

// DI config read buffer (4 regs at 0x0401)
static uint16_t diConfigRegs[4] = {0};
static const uint16_t diConfigAddresses[4] = {
  REG_T6_PR400_SI1, REG_T6_PR401_SI2, REG_T6_PR402_SI3, REG_T6_PR403_SI4
};
static uint8_t diConfigReadIndex = 0;

// Drive poll alternates between: speed poll, position poll
static uint8_t drivePollPhase = 0;

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
static const uint8_t DRIVE_Q_SIZE = 16;
static uint16_t driveQReg[DRIVE_Q_SIZE];
static uint16_t driveQVal[DRIVE_Q_SIZE];
static uint8_t driveQHead = 0;
static uint8_t driveQCount = 0;
static bool driveWriteInFlight = false;

// On-demand read requests
static bool pendingDIRead = false;
static bool pendingAlarmRead = false;
static bool pendingPositionRead = false;
static bool pendingMotionDiagRead = false;
static uint8_t motionDiagReadIndex = 0;
static uint16_t motionDiagRegs[3] = {0};
static const uint16_t motionDiagAddresses[3] = {
  REG_T6_PR802_TRIGGER, REG_T6_PR_STATUS, REG_T6_DI_STATUS
};

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
    uiPollBackoffUntil = 0;
    uiSetpoint = uiRegs[0];
    uiRun = uiRegs[1];
    uiEstop = (uiRegs[UI_REG_UI_STATUS] & UI_STATUS_ESTOP) != 0;
    uiResetAlarm = uiRegs[UI_REG_RESET_ALARM];
    if (uiRegs[UI_REG_RESET_ALARM])
    {
        Serial.printf("[ModbusMaster] UI poll OK: sYEAH BOI\n");
    }
    // Serial.printf("[ModbusMaster] UI poll OK: setpoint=%u run=%u estop=%u, reset alarm=%u\n",
    //               uiSetpoint, uiRun, uiEstop ? 1 : 0,  uiResetAlarm);
  } else {
    // A missing UI response must not monopolize the shared bus or starve
    // explicit drive diagnostics such as `readdi`.
    uiPollBackoffUntil = millis() + 1000;
    Serial.printf("[ModbusMaster] UI poll timeout/error: 0x%02X; retry in 1 s\n", event);
  }
  return true;
}

// Drive speed poll (single register 0x0B00)
static bool onDriveSpeedPollComplete(Modbus::ResultCode event, uint16_t, void*) {
  txnFinished();
  if (event == Modbus::EX_SUCCESS) {
    actualSpeed = (int16_t)driveSpeedReg;
  } else {
    drivePollBackoffUntil = millis() + 3000;
    Serial.printf("[ModbusMaster] Drive speed poll failed: 0x%02X\n", event);
  }
  return true;
}

// Drive position poll (4 registers from 0x602A)
static bool onDrivePosPollComplete(Modbus::ResultCode event, uint16_t, void*) {
  txnFinished();
  if (event == Modbus::EX_SUCCESS) {
    driveCommandPosition = ((int32_t)(int16_t)drivePosRegs[0] << 16) | drivePosRegs[1];
    driveActualPosition  = ((int32_t)(int16_t)drivePosRegs[2] << 16) | drivePosRegs[3];
    if (pendingPositionRead) {
      // Explicitly requested — print result
      Serial.printf("[Drive Position]\n");
      Serial.printf("  Cmd  pos: %ld pulses (%.3f rev)\n",
        (long)driveCommandPosition, driveCommandPosition / 10000.0f);
      Serial.printf("  Actual   : %ld pulses (%.3f rev)\n",
        (long)driveActualPosition, driveActualPosition / 10000.0f);
      pendingPositionRead = false;
    }
  } else {
    // Position registers only valid in PR mode – don't backoff for this
    if (pendingPositionRead) {
      Serial.printf("[ModbusMaster] Position read failed: 0x%02X\n", event);
      pendingPositionRead = false;
    }
  }
  return true;
}

// DI config read (4 registers from 0x0401)
static bool onDIConfigReadComplete(Modbus::ResultCode event, uint16_t, void*) {
  txnFinished();
  if (event == Modbus::EX_SUCCESS) {
    diConfigReadIndex++;
    if (diConfigReadIndex < 4) return true;

    pendingDIRead = false;
    // Decode each DI function code
    auto diName = [](uint16_t code) -> const char* {
      uint8_t fn = code & 0x7F;
      switch (fn) {
        case 0x03: return "SRV-ON(servo enable)";
        case 0x22: return "STP(E-STOP)";
        case 0x14: return "STP(E-STOP) alt";
        case 0x26: return "NOT(neg limit)";
        case 0x25: return "POT(pos limit)";
        case 0x02: return "NOT(neg limit) alt";
        case 0x01: return "POT(pos limit) alt";
        case 0x0F: return "ORG(home sensor)";
        case 0x00: return "NOT ASSIGNED";
        default:   return "UNKNOWN";
      }
    };
    auto polarity = [](uint16_t code) -> const char* {
      return (code & 0x80) ? "NC" : "NO";
    };
    Serial.println("[Drive DI Config]");
    Serial.printf("  SI1 (Pr4.00 = 0x%04X) : %s [%s]\n", diConfigRegs[0], diName(diConfigRegs[0]), polarity(diConfigRegs[0]));
    Serial.printf("  SI2 (Pr4.01 = 0x%04X) : %s [%s]\n", diConfigRegs[1], diName(diConfigRegs[1]), polarity(diConfigRegs[1]));
    Serial.printf("  SI3 (Pr4.02 = 0x%04X) : %s [%s]\n", diConfigRegs[2], diName(diConfigRegs[2]), polarity(diConfigRegs[2]));
    Serial.printf("  SI4 (Pr4.03 = 0x%04X) : %s [%s]\n", diConfigRegs[3], diName(diConfigRegs[3]), polarity(diConfigRegs[3]));
  } else {
    pendingDIRead = false;
    Serial.printf("[ModbusMaster] DI config read failed at Pr4.%02u (register 0x%04X): 0x%02X\n",
                  diConfigReadIndex, diConfigAddresses[diConfigReadIndex], event);
  }
  return true;
}

// Alarm + DI status read (single alarm reg 0x0B03)
static bool onAlarmReadComplete(Modbus::ResultCode event, uint16_t, void*) {
  txnFinished();
  pendingAlarmRead = false;
  if (event == Modbus::EX_SUCCESS) {
    Serial.printf("[Drive Status] AlarmCode=0x%04X (%s)\n",
      driveAlarmReg,
      driveAlarmReg == 0 ? "OK (no alarm)" : "ALARM ACTIVE");
  } else {
    Serial.printf("[ModbusMaster] Alarm read failed: 0x%02X\n", event);
  }
  return true;
}

static bool onMotionDiagReadComplete(Modbus::ResultCode event, uint16_t, void*) {
  txnFinished();
  if (event != Modbus::EX_SUCCESS) {
    pendingMotionDiagRead = false;
    Serial.printf("[Drive Motion] Read failed at register 0x%04X: 0x%02X\n",
                  motionDiagAddresses[motionDiagReadIndex], event);
    return true;
  }

  motionDiagReadIndex++;
  if (motionDiagReadIndex < 3) return true;

  pendingMotionDiagRead = false;
  uint16_t di = motionDiagRegs[2];
  Serial.printf("[Drive Motion] Pr8.02 state=0x%04X, Pr8.46 PR IO=0x%04X, PrB.17 DI=0x%04X\n",
                motionDiagRegs[0], motionDiagRegs[1], di);
  Serial.printf("  SI1=%s SI2=%s SI3=%s SI4=%s (PrB.17 bit=1 means input HIGH)\n",
                (di & 0x0001) ? "HIGH" : "LOW",
                (di & 0x0002) ? "HIGH" : "LOW",
                (di & 0x0004) ? "HIGH" : "LOW",
                (di & 0x0008) ? "HIGH" : "LOW");
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
  // Alternate between speed poll and position poll
  if (drivePollPhase == 0) {
    if (beginRead(DRIVE_SLAVE_ID, REG_T6_ACTUAL_SPEED, &driveSpeedReg, 1, onDriveSpeedPollComplete)) {
      drivePollPhase = 1;
    }
  } else {
    if (beginRead(DRIVE_SLAVE_ID, REG_T6_CMD_POS_H, drivePosRegs, 4, onDrivePosPollComplete)) {
      drivePollPhase = 0;
    }
  }
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

void modbusResetAlarm() {
  enqueueDrive(REG_T6_RESET_ALARM, VALUE_T6_RESET_ALARM);
}

void modbusConfigurePrMode() {
  enqueueDrive(REG_T6_PR001_MODE, T6_MODE_PR);
  enqueueDrive(REG_T6_PR400_SI1, T6_SI1_SERVO_ON);
  enqueueDrive(REG_T6_PR025_AUX, T6_AUX_SAVE);
  Serial.println("[ModbusMaster] PR mode queued (Pr0.01=6, servo on). Power-cycle the drive once.");
}

void modbusMoveAbsolute(int32_t pulses, uint16_t speedRpm, uint16_t accelMsPer1000Rpm) {
  if (pulses > T6_MOVE_MAX_PULSES) pulses = T6_MOVE_MAX_PULSES;
  if (pulses < 0) pulses = 0;
  if (speedRpm > T6_MOVE_MAX_SPEED_RPM) speedRpm = T6_MOVE_MAX_SPEED_RPM;
  if (accelMsPer1000Rpm < 1) accelMsPer1000Rpm = 1;
  if (accelMsPer1000Rpm > 5000) accelMsPer1000Rpm = 5000;

  enqueueDrive(REG_T6_PATH1_MODE, T6_PATH_ABSOLUTE);
  enqueueDrive(REG_T6_PATH1_POS_H, (uint16_t)((pulses >> 16) & 0xFFFF));
  enqueueDrive(REG_T6_PATH1_POS_L, (uint16_t)(pulses & 0xFFFF));
  enqueueDrive(REG_T6_PATH1_SPEED, speedRpm);
  enqueueDrive(REG_T6_PATH1_ACCEL, accelMsPer1000Rpm);
  enqueueDrive(REG_T6_PATH1_DECEL, accelMsPer1000Rpm);
  enqueueDrive(REG_T6_PR802_TRIGGER, T6_TRIGGER_PATH1);
  Serial.printf("[ModbusMaster] Absolute move queued: %ld pulses at %u r/min, accel/decel %u ms/1000rpm\n",
                (long)pulses, speedRpm, accelMsPer1000Rpm);
}

void modbusMoveRelative(int32_t pulses, uint16_t speedRpm, uint16_t accelMsPer1000Rpm) {
  if (pulses > T6_MOVE_MAX_PULSES)  pulses = T6_MOVE_MAX_PULSES;
  if (pulses < -T6_MOVE_MAX_PULSES) pulses = -T6_MOVE_MAX_PULSES;
  if (accelMsPer1000Rpm < 1) accelMsPer1000Rpm = 1;
  if (accelMsPer1000Rpm > 5000) accelMsPer1000Rpm = 5000;

  enqueueDrive(REG_T6_PATH1_MODE, T6_PATH_RELATIVE);
  enqueueDrive(REG_T6_PATH1_POS_H, (uint16_t)((pulses >> 16) & 0xFFFF));
  enqueueDrive(REG_T6_PATH1_POS_L, (uint16_t)(pulses & 0xFFFF));
  enqueueDrive(REG_T6_PATH1_SPEED, speedRpm);
  enqueueDrive(REG_T6_PATH1_ACCEL, accelMsPer1000Rpm);
  enqueueDrive(REG_T6_PATH1_DECEL, accelMsPer1000Rpm);
  enqueueDrive(REG_T6_PR802_TRIGGER, T6_TRIGGER_PATH1);
  Serial.printf("[ModbusMaster] Relative move queued: %+ld pulses at %u r/min, accel/decel %u ms/1000rpm\n",
                (long)pulses, speedRpm, accelMsPer1000Rpm);
}

void modbusStartHoming() {
  dropWaitingDriveWrites();
  enqueueDrive(REG_T6_PR802_TRIGGER, T6_TRIGGER_HOME);
  Serial.println("[ModbusMaster] Homing queued (Pr8.02 = 0x0020)");
}

void modbusStopMotion() {
  dropWaitingDriveWrites();
  enqueueDrive(REG_T6_PR802_TRIGGER, T6_TRIGGER_ESTOP);
  Serial.println("[ModbusMaster] STOP queued");
}

void modbusEmergencyStop() {
  dropWaitingDriveWrites();
  enqueueDrive(REG_T6_PR802_TRIGGER, T6_TRIGGER_ESTOP);
  // Do not change Pr4.00 here: SI1 is the physical servo-enable input and its
  // configured polarity must remain stable when an emergency stop occurs.
  Serial.println("[ModbusMaster] EM STOP queued (DI configuration unchanged)");
}

void modbusReadDIConfig() {
  diConfigReadIndex = 0;
  pendingDIRead = true;
  Serial.println("[ModbusMaster] DI config read requested (Pr4.00-03, spaced register addresses)");
}

void modbusWriteDILimitSwitches() {
  // SI1 stays as SRV-ON (NC, internal – no wire needed)
  // SI1 is physically wired as external servo enable; keep Pr4.00 = 0x0003.
  enqueueDrive(REG_T6_PR400_SI1, T6_DI_SRV_ON_NO);
  // SI2 = E-STOP (STP), Normally Closed (fail-safe: open = stop)
  enqueueDrive(REG_T6_PR401_SI2, T6_DI_ESTOP_NC); // Pr4.01 = 0x00A2
  // SI3 = Negative Limit (NOT), Normally Closed
  enqueueDrive(REG_T6_PR402_SI3, T6_DI_NL_NC);    // Pr4.02 = 0x00A6
  // SI4 = Positive Limit (POT), Normally Closed
  enqueueDrive(REG_T6_PR403_SI4, T6_DI_PL_NC);    // Pr4.03 = 0x00A5
  // Save parameters (Pr0.25 = 0x2211)
  enqueueDrive(REG_T6_PR025_AUX, T6_AUX_SAVE);
  // Soft-reset to apply DI changes without power cycling
  enqueueDrive(REG_T6_SOFT_RESET, VALUE_T6_SOFT_RESET);
  Serial.println("[ModbusMaster] DI config queued:");
  Serial.println("  SI1 = SRV-ON (NC, internal)");
  Serial.println("  SI2 = E-STOP/STP        (NC) -- wire emergency stop");
  Serial.println("  SI3 = NL/NOT neg limit  (NC) -- wire limit switch low end");
  Serial.println("  SI4 = PL/POT pos limit  (NC) -- wire limit switch high end");
  Serial.println("  Save + soft-reset queued");
}

void modbusReadAlarmAndDI() {
  pendingAlarmRead = true;
  Serial.println("[ModbusMaster] Alarm read requested");
}

void modbusReadPositionStatus() {
  pendingPositionRead = true;
  Serial.println("[ModbusMaster] Position read requested");
}

void modbusReadMotionDiagnostics() {
  motionDiagReadIndex = 0;
  pendingMotionDiagRead = true;
  Serial.println("[ModbusMaster] Reading Pr8.02 motion, Pr8.46 PR IO, and PrB.17 SI input levels");
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

bool modbusGetUI_Reset_Alarm() {
  return uiResetAlarm;
}

uint16_t modbusDriveGetStatus() {
  return driveStatus;
}

int16_t modbusDriveGetActualSpeed() {
  return actualSpeed;
}

int32_t modbusDriveGetActualPosition() {
  return driveActualPosition;
}

int32_t modbusDriveGetCommandPosition() {
  return driveCommandPosition;
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

  // Explicit diagnostic reads take priority over periodic traffic.
  if (pendingDIRead) {
    beginRead(DRIVE_SLAVE_ID, diConfigAddresses[diConfigReadIndex],
              &diConfigRegs[diConfigReadIndex], 1, onDIConfigReadComplete);
    return;
  }

  if (pendingAlarmRead) {
    if (beginRead(DRIVE_SLAVE_ID, REG_T6_ALARM_CODE, &driveAlarmReg, 1, onAlarmReadComplete)) {
      // cleared in callback
    }
    return;
  }

  if (pendingPositionRead) {
    if (beginRead(DRIVE_SLAVE_ID, REG_T6_CMD_POS_H, drivePosRegs, 4, onDrivePosPollComplete)) {
      pendingPositionRead = false;
    }
    return;
  }

  if (pendingMotionDiagRead) {
    beginRead(DRIVE_SLAVE_ID, motionDiagAddresses[motionDiagReadIndex],
              &motionDiagRegs[motionDiagReadIndex], 1, onMotionDiagReadComplete);
    return;
  }

  if (flushWrite()) return;

  // Poll the UI regularly, but back off after a timeout so a missing UI node
  // cannot continually occupy the shared bus while the drive is healthy.
  if (now >= uiPollBackoffUntil &&
      now - lastUiPoll > MODBUS_POLL_INTERVAL_MS) {
    if (beginRead(UI_ESP_SLAVE_ID, UI_REG_SETPOINT, uiRegs, UI_REGISTERS_COUNT, onUIPollComplete)) {
      lastUiPoll = now;
    }
    return;
  }

  // Background drive poll (speed + position, alternating)
  if (now >= drivePollBackoffUntil &&
      now - lastDrivePoll > MODBUS_DRIVE_POLL_INTERVAL_MS) {
    modbusPollDrive();
    lastDrivePoll = now;
  }
}
