/**
 * @file driver_control.h
 * @brief Servo drive control logic for Control ESP
 * 
 * Manages the servo drive state machine:
 * - Initialization sequence
 * - Speed setpoint management
 * - Enable/disable control
 * - Status monitoring
 */

#ifndef DRIVER_CONTROL_H
#define DRIVER_CONTROL_H

#include <stdint.h>

// ============================================================================
// DRIVE STATE MACHINE
// ============================================================================

enum DriveState {
  DRIVE_STATE_IDLE,           // Waiting to initialize
  DRIVE_STATE_INIT_SEQ,       // Running initialization sequence
  DRIVE_STATE_READY,          // Ready to accept commands
  DRIVE_STATE_ERROR,          // Error condition
  DRIVE_STATE_DISABLED        // Servo disabled
};

// ============================================================================
// INITIALIZATION & LIFECYCLE
// ============================================================================

/**
 * Initialize drive control system
 * Must be called once in setup()
 */
void driverInit();

/**
 * Main control loop task
 * Must be called frequently in loop()
 */
void driverTask();

/**
 * Get current drive state
 */
DriveState driverGetState();

/**
 * Reset drive state machine (e.g., after error)
 */
void driverReset();

// ============================================================================
// SPEED CONTROL
// ============================================================================

/**
 * Set target speed setpoint (r/min)
 * Actual communication to drive happens asynchronously via Modbus
 * 
 * @param speedRpm: Signed speed in r/min (-5000..5000 typical)
 */
void driverSetSpeed(int16_t speedRpm);

/**
 * Get last commanded speed
 */
int16_t driverGetCommandedSpeed();

/**
 * Get actual motor speed (from drive feedback)
 */
int16_t driverGetActualSpeed();

// ============================================================================
// ENABLE/DISABLE CONTROL
// ============================================================================

/**
 * Enable servo (SON = 1)
 */
void driverEnable();

/**
 * Disable servo (SON = 0)
 */
void driverDisable();

/**
 * Get servo enable state
 */
bool driverIsEnabled();

/**
 * Emergency stop (immediate disable)
 */
void driverEmergencyStop();

// ============================================================================
// STATUS & DIAGNOSTICS
// ============================================================================

/**
 * Get drive status bits (Dn-18)
 * Bits: bit0=Alarm, bit1=Ready, bit2=Emg, bit3=Preach, bit4=Sreach,
 *       bit5=Treach, bit6=ZeroSpeed, bit7=Run
 * (Note: bit=0 means that function is ON)
 */
uint16_t driverGetStatusBits();

/**
 * Check if drive is in alarm state
 */
bool driverIsAlarmed();

/**
 * Check if drive is ready
 */
bool driverIsReady();

/**
 * Check if motor is actually running
 */
bool driverIsRunning();

#endif // DRIVER_CONTROL_H
