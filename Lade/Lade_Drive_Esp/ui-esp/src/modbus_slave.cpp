/**
 * @file modbus_slave.cpp
 * @brief Modbus RTU Slave implementation for UI ESP
 */

#include "modbus_slave.h"
#include "../../shared/modbus_common.h"
#include <ModbusRTU.h>
#include <Arduino.h>

// ============================================================================
// PRIVATE STATE
// ============================================================================

static ModbusRTU mb;
static bool initialized = false;
static unsigned long lastMasterContact = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void modbusSlaveInit() {
  // Use Serial2 (UART2) on dedicated pins so USB Serial (UART0) remains for 115200 debug
  Serial2.begin(MODBUS_BAUDRATE, MODBUS_SERIAL_CFG, UI_SERIAL_RXD, UI_SERIAL_TXD);
  
  mb.begin(&Serial2, -1); // -1 = no direction pin, auto-direction on RS-485 adapter
  mb.slave(UI_ESP_SLAVE_ID);
  
  // Add holding registers
  mb.addHreg(UI_REG_SETPOINT, 0);       // Speed setpoint from UI
  mb.addHreg(UI_REG_RUN, 0);            // Run command from UI
  mb.addHreg(UI_REG_CONTROL_MODE, 0);   // Control mode flags
  mb.addHreg(UI_REG_DRIVE_STATUS, 0);   // Drive status from master
  mb.addHreg(UI_REG_ACTUAL_SPEED, 0);   // Actual speed feedback
  mb.addHreg(UI_REG_UI_STATUS, 0);      // UI status bits
  
  initialized = true;
  lastMasterContact = millis();
  
  Serial.printf("[ModbusSlave] Initialized as slave ID %d on Serial2 (RX=GPIO%d, TX=GPIO%d)\n",
                UI_ESP_SLAVE_ID, UI_SERIAL_RXD, UI_SERIAL_TXD);
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void modbusSlaveSetSetpoint(uint16_t speedRpm) {
  if (!initialized) return;
  mb.Hreg(UI_REG_SETPOINT, speedRpm);
}

void modbusSlaveSetRun(uint16_t runCmd) {
  if (!initialized) return;
  mb.Hreg(UI_REG_RUN, runCmd);
}

uint16_t modbusSlaveGetDriveStatus() {
  if (!initialized) return 0;
  return mb.Hreg(UI_REG_DRIVE_STATUS);
}

void modbusSlaveSetUIStatus(uint16_t status) {
  if (!initialized) return;
  mb.Hreg(UI_REG_UI_STATUS, status);
}

bool modbusSlaveIsReady() {
  return initialized;
}

bool modbusSlaveHasActiveMaster() {
  // Check if we've heard from master in last 1 second
  return (millis() - lastMasterContact) < 1000;
}

void modbusSlaveTask() {
  if (!initialized) return;
  
  mb.task();
  
  // Track last master contact (any read/write)
  static uint16_t lastSetpoint = 0xFFFF;
  uint16_t currentSetpoint = mb.Hreg(UI_REG_SETPOINT);
  
  if (currentSetpoint != lastSetpoint) {
    lastMasterContact = millis();
    lastSetpoint = currentSetpoint;
  }
}
