/**
 * @file keypad_handler.h
 * @brief 4x4 Membrane keypad handling for UI ESP
 * 
 * Manages standard 4x4 membrane keypad input:
 * - Key press detection
 * - Debouncing
 * - Character mapping
 */

#ifndef KEYPAD_HANDLER_H
#define KEYPAD_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize keypad GPIO pins
 * Must be called once in setup()
 */
void keypadInit();

// ============================================================================
// KEYPAD INPUT
// ============================================================================

/**
 * Check if a key was pressed since last check
 * 
 * @return Character of pressed key ('1'..'9', 'A'..'D', '#', '*', or '\0' if none)
 */
char keypadGetKeyPress();

/**
 * Get currently pressed key (if any)
 * 
 * @return Character of currently pressed key, or '\0' if none
 */
char keypadGetCurrentKey();

/**
 * Check if any key is currently being pressed
 */
bool keypadIsKeyPressed();

// ============================================================================
// DIAGNOSTIC
// ============================================================================

/**
 * Print keypad map to Serial (useful for verification)
 */
void keypadPrintMap();

// ============================================================================
// BACKGROUND TASK
// ============================================================================

/**
 * Background task for keypad scanning and debouncing
 * Must be called frequently in loop()
 */
void keypadTask();

#endif // KEYPAD_HANDLER_H
