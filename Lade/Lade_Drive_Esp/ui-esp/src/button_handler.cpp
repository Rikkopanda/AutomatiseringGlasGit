/**
 * @file button_handler.cpp
 * @brief Button and LED implementation for UI ESP
 */

#include "button_handler.h"
#include <Arduino.h>
#include "../../shared/modbus_common.h"

// ============================================================================
// BUTTON/LED STRUCTURE
// ============================================================================
// LEDs are passive/integrated: 3.3V -> resistor -> switch -> GPIO (active-low)
// When button is pressed, GPIO goes LOW and LED lights up automatically
// No separate LED control needed

struct ButtonState {
  uint8_t pinButton;  // Input pin (active-low)

  ButtonState(uint8_t pin) : pinButton(pin) {}
  
  bool stablePressed = false;
  bool candidatePressed = false;
  unsigned long candidateSinceMs = 0;
  bool edge = false;
  uint8_t pressCount = 0;
};

// Button pin mapping (7 buttons, LEDs are integrated)
static ButtonState buttons[NUM_BUTTONS] = {
  {BUTTON_GPIO_0},  // Button 0 (GPIO 23)
  {BUTTON_GPIO_1},  // Button 1 (GPIO 22)
  {BUTTON_GPIO_2},  // Button 2 (GPIO 19)
  {BUTTON_GPIO_3},  // Button 3 (GPIO 34)
  {BUTTON_GPIO_4},  // Button 4 (GPIO 35)
  {BUTTON_GPIO_5},  // Button 5 (GPIO 32)
  {BUTTON_GPIO_6},  // Button 6 (GPIO 33)
};

// ============================================================================
// INITIALIZATION
// ============================================================================

void buttonInit() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttons[i].pinButton, INPUT_PULLUP);
    // Note: LEDs are integrated into button circuit (passive)
    // No separate LED pin control needed
  }
  
  Serial.println("[Button] Initialized 7 buttons (LEDs integrated)");
}

// ============================================================================
// BUTTON INPUT
// ============================================================================

bool buttonWasPressed(ButtonID buttonId) {
  if (buttonId >= NUM_BUTTONS) return false;
  
  bool edge = buttons[buttonId].edge;
  buttons[buttonId].edge = false; // Clear after read
  return edge;
}

bool buttonIsPressed(ButtonID buttonId) {
  if (buttonId >= NUM_BUTTONS) return false;
  return buttons[buttonId].stablePressed;
}

uint8_t buttonGetPressCount(ButtonID buttonId) {
  if (buttonId >= NUM_BUTTONS) return 0;
  
  uint8_t count = buttons[buttonId].pressCount;
  buttons[buttonId].pressCount = 0; // Clear after read
  return count;
}

// ============================================================================
// LED CONTROL (Passive - LEDs light automatically when button pressed)
// ============================================================================
// These functions are stubs since LEDs are integrated into button circuit

void ledSet(ButtonID buttonId, bool on) {
  // LEDs are passive - they light when button is pressed
  // No active control needed
  (void)buttonId;
  (void)on;
}

bool ledGetState(ButtonID buttonId) {
  // Return button state as LED state (they're the same)
  if (buttonId >= NUM_BUTTONS) return false;
  return buttons[buttonId].stablePressed;
}

void ledToggle(ButtonID buttonId) {
  // Not applicable for passive LEDs
  (void)buttonId;
}

void ledPulse(ButtonID buttonId, uint16_t periodMs) {
  // Not applicable for passive LEDs
  (void)buttonId;
  (void)periodMs;
}

void ledStopPulse(ButtonID buttonId) {
  // Not applicable for passive LEDs
  (void)buttonId;
}

void ledBlink(ButtonID buttonId, uint8_t count, uint16_t onMs, uint16_t offMs) {
  // Not applicable for passive LEDs
  (void)buttonId;
  (void)count;
  (void)onMs;
  (void)offMs;
}

// ============================================================================
// BACKGROUND TASK
// ============================================================================

void buttonTask() {
  unsigned long now = millis();
  
  for (int i = 0; i < NUM_BUTTONS; i++) {
    ButtonState& btn = buttons[i];
    
    // ---- Debounce button input ----
    bool rawPressed = digitalRead(btn.pinButton) == LOW; // Active-low
    
    if (rawPressed != btn.candidatePressed) {
      btn.candidateSinceMs = now;
      btn.candidatePressed = rawPressed;
    }
    
    if ((now - btn.candidateSinceMs) > BUTTON_DEBOUNCE_MS) {
      if (rawPressed && !btn.stablePressed) {
        // Button just pressed
        btn.stablePressed = true;
        btn.edge = true;
        btn.pressCount++;
      } else if (!rawPressed && btn.stablePressed) {
        // Button just released
        btn.stablePressed = false;
      }
    }
    
    // Note: LEDs are passive (integrated into button circuit)
    // They light automatically when button is pressed
    // No active LED control needed here
  }
}
