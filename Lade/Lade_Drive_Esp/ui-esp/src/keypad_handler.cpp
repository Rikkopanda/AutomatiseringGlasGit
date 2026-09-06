/**
 * @file keypad_handler.cpp
 * @brief 4x4 Membrane keypad implementation for UI ESP
 */

#include "keypad_handler.h"
#include <Arduino.h>
#include "../../shared/modbus_common.h"

// ============================================================================
// KEYPAD CONFIGURATION
// ============================================================================

// Row and column pins (adjust to your wiring)
static const uint8_t KEYPAD_ROWS[4] = {13, 18, 19, 23};
static const uint8_t KEYPAD_COLS[4] = {25, 26, 32, 33};

// Character map for this keypad layout
static const char KEYPAD_MAP[4][4] = {
  {'D', 'C', 'B', 'A'},
  {'#', '9', '6', '3'},
  {'0', '8', '5', '2'},
  {'*', '7', '4', '1'}
};

// ============================================================================
// KEYPAD STATE
// ============================================================================

struct KeypadState {
  char lastKey = '\0';
  char currentKey = '\0';
  unsigned long lastPressTimeMs = 0;
  bool keyAvailable = false;
};

static KeypadState keypadState;
static const unsigned long KEYPAD_DEBOUNCE_MS = 20;

// ============================================================================
// INITIALIZATION
// ============================================================================

void keypadInit() {
  // Set up row pins as outputs
  for (int i = 0; i < 4; i++) {
    pinMode(KEYPAD_ROWS[i], OUTPUT);
    digitalWrite(KEYPAD_ROWS[i], HIGH); // Inactive state
  }
  
  // Set up column pins as inputs with pull-ups
  for (int i = 0; i < 4; i++) {
    pinMode(KEYPAD_COLS[i], INPUT_PULLUP);
  }
  
  Serial.println("[Keypad] Initialized 4x4 keypad");
}

// ============================================================================
// KEYPAD SCANNING
// ============================================================================

/**
 * Scan keypad matrix and return pressed key (if any)
 */
static char scanKeypad() {
  for (int row = 0; row < 4; row++) {
    // Drive this row LOW
    digitalWrite(KEYPAD_ROWS[row], LOW);
    delayMicroseconds(50);
    
    // Check each column
    for (int col = 0; col < 4; col++) {
      if (digitalRead(KEYPAD_COLS[col]) == LOW) {
        // Key pressed at (row, col)
        digitalWrite(KEYPAD_ROWS[row], HIGH); // Restore row
        return KEYPAD_MAP[row][col];
      }
    }
    
    // Restore row to HIGH
    digitalWrite(KEYPAD_ROWS[row], HIGH);
  }
  
  return '\0'; // No key pressed
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

char keypadGetKeyPress() {
  char key = '\0';
  if (keypadState.keyAvailable) {
    key = keypadState.currentKey;
    keypadState.keyAvailable = false;
  }
  return key;
}

char keypadGetCurrentKey() {
  return keypadState.currentKey;
}

bool keypadIsKeyPressed() {
  return keypadState.currentKey != '\0';
}

void keypadPrintMap() {
  Serial.println("Keypad layout:");
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      Serial.print(KEYPAD_MAP[r][c]);
      Serial.print(" ");
    }
    Serial.println();
  }
}

// ============================================================================
// BACKGROUND TASK
// ============================================================================

void keypadTask() {
  unsigned long now = millis();
  
  // Scan keypad
  char rawKey = scanKeypad();
  
  if (rawKey != keypadState.lastKey) {
    // Key changed (press or release)
    keypadState.lastPressTimeMs = now;
    keypadState.lastKey = rawKey;
  }
  
  // Debounce: after stable period, update current state
  if ((now - keypadState.lastPressTimeMs) > KEYPAD_DEBOUNCE_MS) {
    if (rawKey != keypadState.currentKey) {
      keypadState.currentKey = rawKey;
      
      // Signal that a new key was pressed
      if (rawKey != '\0') {
        keypadState.keyAvailable = true;
      }
    }
  }
}
