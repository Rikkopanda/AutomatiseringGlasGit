/**
 * @file rotary_encoder.cpp
 * @brief Rotary encoder implementation for UI ESP
 */

#include "rotary_encoder.h"
#include <Arduino.h>
#include "../../shared/modbus_common.h"

// ============================================================================
// ENCODER STRUCTURE
// ============================================================================

struct EncoderState {
  uint8_t pinA;
  uint8_t pinB;
  uint8_t pinButton;

  EncoderState(uint8_t a, uint8_t b, uint8_t button)
    : pinA(a), pinB(b), pinButton(button) {}
  
  int32_t position = 0;
  int8_t delta = 0;
  
  uint8_t lastStateA = 0;
  uint8_t lastStateB = 0;
  
  bool lastButton = false;
  bool buttonPressed = false;
  bool buttonEdge = false;
  uint8_t pressCount = 0;
  
  unsigned long lastDebounceTime = 0;
};

static EncoderState encoders[2] = {
  // ENCODER_MAIN: CLK=17, DT=4, Button=15
  {ENCODER1_CLK, ENCODER1_DT, ENCODER1_SW},
  
  // ENCODER_SECONDARY: CLK=16, DT=18, Button=5
  {ENCODER2_CLK, ENCODER2_DT, ENCODER2_SW}
};

// ============================================================================
// INITIALIZATION
// ============================================================================

void encoderInit() {
  for (int i = 0; i < 2; i++) {
    pinMode(encoders[i].pinA, INPUT);
    pinMode(encoders[i].pinB, INPUT);
    pinMode(encoders[i].pinButton, INPUT_PULLUP);
    
    encoders[i].lastStateA = digitalRead(encoders[i].pinA);
    encoders[i].lastStateB = digitalRead(encoders[i].pinB);
    encoders[i].lastButton = digitalRead(encoders[i].pinButton) == LOW;
  }
  
  Serial.println("[Encoder] Initialized");
}

// ============================================================================
// ENCODER READING
// ============================================================================

int8_t encoderGetDelta(EncoderID encoderId) {
  if (encoderId >= 2) return 0;
  
  int8_t delta = encoders[encoderId].delta;
  encoders[encoderId].delta = 0; // Clear delta after read
  return delta;
}

int32_t encoderGetPosition(EncoderID encoderId) {
  if (encoderId >= 2) return 0;
  return encoders[encoderId].position;
}

void encoderReset(EncoderID encoderId) {
  if (encoderId >= 2) return;
  encoders[encoderId].position = 0;
}

// ============================================================================
// BUTTON READING
// ============================================================================

bool encoderButtonWasPressed(EncoderID encoderId) {
  if (encoderId >= 2) return false;
  
  bool edge = encoders[encoderId].buttonEdge;
  encoders[encoderId].buttonEdge = false; // Clear edge after read
  return edge;
}

bool encoderButtonIsPressed(EncoderID encoderId) {
  if (encoderId >= 2) return false;
  return encoders[encoderId].buttonPressed;
}

uint8_t encoderGetButtonPressCount(EncoderID encoderId) {
  if (encoderId >= 2) return 0;
  
  uint8_t count = encoders[encoderId].pressCount;
  encoders[encoderId].pressCount = 0; // Clear count after read
  return count;
}

// ============================================================================
// BACKGROUND TASK
// ============================================================================

void encoderTask() {
  for (int i = 0; i < 2; i++) {
    EncoderState& enc = encoders[i];
    
    // Read quadrature A and B
    uint8_t a = digitalRead(enc.pinA);
    uint8_t b = digitalRead(enc.pinB);
    
    // Detect transitions (simplified Gray code decoder)
    if (a != enc.lastStateA) {
      if (b == enc.lastStateA) {
        // Clockwise rotation
        enc.position++;
        enc.delta = 1;
      } else {
        // Counter-clockwise rotation
        enc.position--;
        enc.delta = -1;
      }
      enc.lastStateA = a;
      enc.lastStateB = b;
    } else if (b != enc.lastStateB) {
      if (a != enc.lastStateB) {
        // Clockwise rotation
        enc.position++;
        enc.delta = 1;
      } else {
        // Counter-clockwise rotation
        enc.position--;
        enc.delta = -1;
      }
      enc.lastStateA = a;
      enc.lastStateB = b;
    }
    
    // Read and debounce button
    bool buttonNow = digitalRead(enc.pinButton) == LOW; // Active-low
    
    if (buttonNow != enc.lastButton) {
      enc.lastDebounceTime = millis();
      enc.lastButton = buttonNow;
    }
    
    if ((millis() - enc.lastDebounceTime) > ENCODER_DEBOUNCE_MS) {
      if (buttonNow && !enc.buttonPressed) {
        // Button just pressed
        enc.buttonPressed = true;
        enc.buttonEdge = true;
        enc.pressCount++;
      } else if (!buttonNow && enc.buttonPressed) {
        // Button just released
        enc.buttonPressed = false;
      }
    }
  }
}
