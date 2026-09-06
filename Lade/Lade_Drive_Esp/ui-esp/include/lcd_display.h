/**
 * @file lcd_display.h
 * @brief LCD I2C display management for UI ESP
 * 
 * Handles 16x2 LCD display over I2C:
 * - Initialization
 * - Update display with speed, status, etc.
 * - Custom characters
 */

#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <stdint.h>

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize I2C (pins 21/22) and LCD display
 * Must be called once in setup()
 * 
 * @return true if LCD found and initialized, false otherwise
 */
bool lcdInit();

// ============================================================================
// DISPLAY UPDATE
// ============================================================================

/**
 * Update display with current speed setpoint and status
 * 
 * @param speedRpm: Current speed setpoint (0..3000)
 * @param isRunning: Motor running state
 * @param statusBits: Drive status bits from Dn-18
 */
void lcdUpdateSpeed(uint16_t speedRpm, bool isRunning, uint16_t statusBits);

/**
 * Display a message on line 0 (row 0)
 */
void lcdPrintLine0(const char* msg);

/**
 * Display a message on line 1 (row 1)
 */
void lcdPrintLine1(const char* msg);

/**
 * Clear display
 */
void lcdClear();

/**
 * Display encoder mode/status
 */
void lcdShowEncoderMode(const char* mode);

/**
 * Display alarm/error message
 */
void lcdShowError(const char* errorMsg);

// ============================================================================
// HOUSEKEEPING
// ============================================================================

/**
 * Background task for LCD updates (debounce, refresh, etc.)
 * Must be called frequently in loop()
 */
void lcdTask();

/**
 * Check if LCD is connected and ready
 */
bool lcdIsReady();

#endif // LCD_DISPLAY_H
