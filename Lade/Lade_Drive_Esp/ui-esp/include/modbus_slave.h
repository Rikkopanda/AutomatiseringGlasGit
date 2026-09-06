/**
 * @file modbus_slave.h
 * @brief Modbus RTU Slave interface for UI ESP
 * 
 * UI ESP acts as a Modbus slave, responding to master queries:
 * - Exposes user input (setpoint, run command)
 * - Receives drive status updates
 * - Provides UI status feedback
 */

#ifndef MODBUS_SLAVE_H
#define MODBUS_SLAVE_H

#include <stdint.h>

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize Modbus slave on Serial2 (RS-485 bus)
 * Must be called once in setup()
 */
void modbusSlaveInit();

// ============================================================================
// REGISTER ACCESS (for internal use)
// ============================================================================

/**
 * Set local setpoint value (updated by encoders/buttons)
 * Automatically exposed via Modbus for master to read
 */
void modbusSlaveSetSetpoint(uint16_t speedRpm);

/**
 * Set local run command (updated by UI controls)
 * Automatically exposed via Modbus for master to read
 */
void modbusSlaveSetRun(uint16_t runCmd);

/**
 * Get last command from master (drive status bits)
 * Received via Modbus write from control ESP
 */
uint16_t modbusSlaveGetDriveStatus();

/**
 * Update UI status bits (LED states, encoder feedback, etc.)
 * Expose to master for diagnostics
 */
void modbusSlaveSetUIStatus(uint16_t status);

// ============================================================================
// STATE CHECK
// ============================================================================

/**
 * Check if Modbus slave is initialized and responding
 */
bool modbusSlaveIsReady();

/**
 * Check if master has polled us recently (communication alive)
 */
bool modbusSlaveHasActiveMaster();

// ============================================================================
// BACKGROUND TASK
// ============================================================================

/**
 * Must be called frequently in loop() to handle Modbus communication
 * Non-blocking; handles one transaction per call if needed
 */
void modbusSlaveTask();

#endif // MODBUS_SLAVE_H
