/**
 * @file rotary_encoder.h
 * @brief Rotary encoder input handling for UI ESP
 * 
 * Manages 2 rotary encoders:
 * - Main speed encoder (A/B quadrature + button)
 * - Secondary encoder (A/B quadrature + button) [reserved for future use]
 * 
 * Encoders provide fine control over speed setpoint
 */

#ifndef ROTARY_ENCODER_H
#define ROTARY_ENCODER_H

#include <stdint.h>

// ============================================================================
// ENCODER DEFINITIONS
// ============================================================================

enum EncoderID {
  ENCODER_MAIN = 0,      // Primary speed control encoder
  ENCODER_SECONDARY = 1  // Secondary encoder (future use)
};

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize rotary encoders
 * - Sets up GPIO pins for quadrature reading
 * - Configures debouncing
 * Must be called once in setup()
 */
void encoderInit();

// ============================================================================
// ENCODER INPUT
// ============================================================================

/**
 * Get encoder position delta since last call
 * Positive = clockwise, negative = counter-clockwise
 * 
 * @param encoderId: Which encoder (ENCODER_MAIN or ENCODER_SECONDARY)
 * @return Signed step count (-128..127 typical)
 */
int8_t encoderGetDelta(EncoderID encoderId);

/**
 * Get absolute position of encoder
 * Useful for multi-turn or relative position tracking
 * 
 * @param encoderId: Which encoder
 * @return Absolute position (arbitrary reference)
 */
int32_t encoderGetPosition(EncoderID encoderId);

/**
 * Reset encoder position to 0
 */
void encoderReset(EncoderID encoderId);

// ============================================================================
// ENCODER BUTTON (integrated into encoder shaft)
// ============================================================================

/**
 * Check if encoder button was pressed since last check
 * 
 * @param encoderId: Which encoder
 * @return true if button pressed and released (edge-triggered)
 */
bool encoderButtonWasPressed(EncoderID encoderId);

/**
 * Get current button state
 * 
 * @param encoderId: Which encoder
 * @return true if button currently pressed (level-triggered)
 */
bool encoderButtonIsPressed(EncoderID encoderId);

/**
 * Get button press count (resets when read)
 * Useful for counting rapid presses
 */
uint8_t encoderGetButtonPressCount(EncoderID encoderId);

// ============================================================================
// BACKGROUND TASK
// ============================================================================

/**
 * Background task for encoder debouncing and state update
 * Must be called frequently in loop()
 */
void encoderTask();

#endif // ROTARY_ENCODER_H
