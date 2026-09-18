/**
 * @file button_handler.h
 * @brief Button and LED management for UI ESP
 * 
 * Manages:
 * - 7 momentary buttons (with integrated LEDs)
 * - Individual LED control (with breathing/pulse effects optional)
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// BUTTON DEFINITIONS
// ============================================================================

enum ButtonID {
  BUTTON_0 = 0,  // Button + LED 0
  BUTTON_1 = 1,  // Button + LED 1
  BUTTON_2 = 2,
  BUTTON_3 = 3,
  BUTTON_4 = 4,
  BUTTON_5 = 5,
  BUTTON_6 = 6,
  BUTTON_7 = 7,
  BUTTON_8 = 8,
  BUTTON_9 = 9,
  NUM_BUTTONS = 10
};

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize button and LED pins
 * Must be called once in setup()
 */
void buttonInit();

// ============================================================================
// BUTTON INPUT
// ============================================================================

/**
 * Check if button was pressed since last check
 * 
 * @param buttonId: Button ID (0..6)
 * @return true if button was pressed (edge-triggered, debounced)
 */
bool buttonWasPressed(ButtonID buttonId);

/**
 * Get current button state
 * 
 * @param buttonId: Button ID
 * @return true if button currently pressed (level-triggered)
 */
bool buttonIsPressed(ButtonID buttonId);

/**
 * Get press count for button (auto-resets)
 * Useful for detecting multi-click patterns
 */
uint8_t buttonGetPressCount(ButtonID buttonId);

// ============================================================================
// LED CONTROL (on the button)
// ============================================================================

/**
 * Set LED on/off
 * 
 * @param buttonId: Button ID (0..6)
 * @param on: true = LED on, false = LED off
 */
void ledSet(ButtonID buttonId, bool on);

/**
 * Get LED state
 */
bool ledGetState(ButtonID buttonId);

/**
 * Toggle LED
 */
void ledToggle(ButtonID buttonId);

/**
 * Pulse/breathe LED (start effect)
 * LED fades in and out continuously
 */
void ledPulse(ButtonID buttonId, uint16_t periodMs);

/**
 * Stop pulse effect
 */
void ledStopPulse(ButtonID buttonId);

/**
 * Blink LED N times
 * 
 * @param buttonId: Button ID
 * @param count: Number of blinks
 * @param onMs: LED on duration (ms)
 * @param offMs: LED off duration (ms)
 */
void ledBlink(ButtonID buttonId, uint8_t count, uint16_t onMs, uint16_t offMs);

// ============================================================================
// BACKGROUND TASK
// ============================================================================

/**
 * Background task for debouncing and LED effects
 * Must be called frequently in loop()
 */
void buttonTask();

#endif // BUTTON_HANDLER_H
