/**
 * @file modbus_master.h
 * @brief Modbus RTU Master interface for Control ESP
 * 
 * Handles all Modbus master communication:
 * - Polls UI ESP for setpoint and run commands
 * - Polls servo drive for status
 * - Writes control commands to drive
 * - Writes status echoes back to UI ESP
 */

#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include <stdint.h>

// ============================================================================
// INITIALIZATION
// ============================================================================
/**
 * Initialize Modbus master on Serial2 (RS-485 bus)
 */
void modbusInit();

// ============================================================================
// MASTER POLL OPERATIONS
// ============================================================================

/**
 * Poll UI ESP for user input (setpoint, run command)
 * Non-blocking; result comes via callback or on next task() call
 */
void modbusPollUI();

/**
 * Poll drive for actual speed and status
 * Non-blocking; result comes via callback or on next task() call
 */
void modbusPollDrive();

/**
 * Write drive status echo back to UI ESP
 * Allows UI to display drive state (alarm, ready, etc.)
 */
void modbusWriteStatusToUI(uint16_t driveStatus);

/**
 * Write setpoint to drive
 * Speed value in signed r/min (-5000..5000 typical)
 */
void modbusWriteSpeedToDrive(int16_t speedRpm);

/**
 * Enable/disable servo via Modbus
 * Sets the SON (Servo ON) command bit
 */
void modbusSetServoEnable(bool enable);

// ============================================================================
// STATE ACCESSORS
// ============================================================================

/**
 * Get last polled UI setpoint (r/min)
 */
uint16_t modbusGetUISetpoint();

/**
 * Get last polled UI run command (0 = stop, 1 = run)
 */
uint16_t modbusGetUIRun();

/**
 * Get last polled drive status bits
 */
uint16_t modbusDriveGetStatus();

/**
 * Get last polled actual motor speed
 */
int16_t modbusDriveGetActualSpeed();

/**
 * Check if Modbus is ready (initialized and bus responsive)
 */
bool modbusIsReady();

// ============================================================================
// BACKGROUND TASK
// ============================================================================

/**
 * Must be called frequently in loop() to handle Modbus state machine
 * Non-blocking; performs one transaction per call if needed
 */
void modbusTask();

#endif // MODBUS_MASTER_H
