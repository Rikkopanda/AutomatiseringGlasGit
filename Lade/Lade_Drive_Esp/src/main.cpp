#include <Arduino.h>
#include "driver/rmt.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <vector>

// ================== PINS ==================
#define PIN_STEP      14
#define PIN_DIR       12
#define PIN_ENABLE    27
#define PIN_NAV_NEXT  4
#define PIN_NAV_PREV  5
#define PIN_HOME_LIMIT 15  // home switch, reached while moving positive
#define PIN_LEFTOVER 16  // RX2
#define PIN_END_LIMIT_1  17  // TX2: far-end switch, reached while moving positive
#define PIN_END_LIMIT_0  2   // D2: oven-side end switch, reached while moving negative

#define PIN_POT_SPEED 34
#define PIN_POT_ACCEL 35
#define DEBUG_INPUTS 1                 // set to 0 to silence keypad/pot/limit diagnostics
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
constexpr long MAX_STORED_POSITION = 10000000L;
constexpr long HOME_SEARCH_MAX_STEPS = 10000000L;
#define USE_ENABLE_PIN 1    // 1 = physical enable wire used, 0 = no enable pin on drive
#define ENABLE_ACTIVE_LOW 1   // 1: ENABLE low = ON (common), 0: ENABLE high = ON
#define USE_SERIAL_STEPS 1   // 1 = via serial, 0 = via potmeter

// 4x4 membrane keypad: connect its eight pins directly to these GPIOs.
// The usual keypad order is R1,R2,R3,R4,C1,C2,C3,C4.
const uint8_t KEYPAD_ROWS[4] = {13, 18, 19, 23};
const uint8_t KEYPAD_COLS[4] = {25, 26, 32, 33};
// This keypad's ribbon is transposed/reversed relative to the usual R1..R4,
// C1..C4 order.  Keep the wiring and use this calibrated key map.
const char KEYPAD_MAP[4][4] = {
  {'D', 'C', 'B', 'A'},
  {'#', '9', '6', '3'},
  {'0', '8', '5', '2'},
  {'*', '7', '4', '1'}
};

const float MIN_MAX_SPEED = 100.0f;
const float MAX_MAX_SPEED = 200000.0f;
const float MIN_ACCELERATION = 100.0f;
const float MAX_ACCELERATION = 500000.0f;

// ================== PARAMETERS ==================
const float DEFAULT_MAX_SPEED    = 142000.0f;   // steps/sec
const float DEFAULT_ACCELERATION = 102000.0f;  // steps/sec²
const uint32_t PULSE_US  = 5;         // pulse breedte (5 us is usually safer for servo/driver inputs)
float maxSpeedSetting = DEFAULT_MAX_SPEED;
float accelerationSetting = DEFAULT_ACCELERATION;

#define LCD_I2C_ADDR   0x27
#define LCD_COLS       16
#define LCD_ROWS       2
#define LCD_UPDATE_MS  300

// ================== RMT ==================
#define RMT_CH          RMT_CHANNEL_0
#define RMT_DIV         80            // 1 µs tick
constexpr size_t RMT_BUFFER_ITEMS = 16384;

rmt_item32_t *rmtItems = nullptr;
size_t rmtBufferItems = 0;

// ================== STATE ==================
volatile long currentPos = 0;
bool isMoving = false;
bool isContinuous = false;
bool driveEnabled = false;
bool lcdReady = false;
unsigned long lastLcdUpdate = 0;
volatile bool moveChunkDone = false;
bool moveActive = false;
long moveTotalSteps = 0;
long moveNextStep = 0;
bool moveDirPositive = true;
long activeMoveSteps = 0;
bool homingInProgress = false;
bool positionKnown = true;
TaskHandle_t moveServiceTaskHandle = nullptr;
String keypadNumber;
char lastKey = '\0';
char keypadCandidate = '\0';
unsigned long keypadCandidateSinceMs = 0;
unsigned long lastPotReadMs = 0;

struct DebouncedButton {
  bool stablePressed = false;
  bool candidatePressed = false;
  unsigned long candidateSinceMs = 0;
};
DebouncedButton nextButton;
DebouncedButton prevButton;

enum UiScreen { UI_HOME, UI_SELECT_POSITION, UI_EDIT_POSITION };
UiScreen uiScreen = UI_HOME;
long savedPositions[4] = {0, 0, 0, 0};
uint8_t selectedPosition = 0;
uint8_t editPosition = 0;
String editValue;
Preferences preferences;

LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

bool prepareMove(long steps);
float estimateMoveMs(long steps, long *plannedPulses = nullptr);
bool startContinuousTest(long signedHz);
void stopMotion();
void setDriveEnabled(bool enabled);
void updateLcdStatus(bool force = false);
void moveServiceTask(void *param);
void updateNavigationButtons();
bool updateOneButton(DebouncedButton &button, uint8_t pin);
void loadPositions();
void savePosition(uint8_t index, long value);
void goToSelectedPosition();
char readKeypad();
void handleKeypad();
void updatePotSettings();
bool isHomeLimitPressed();
bool isEndLimit0Pressed();
bool isEndLimit1Pressed();
void processLimitSwitches();
bool startHoming();
void debugLimitSwitches();

#if USE_SERIAL_STEPS
long manualSteps = 1000;
void printStatus();

// ================== SERIAL COMMANDS ==================
void printSerialHelp() {
  Serial.println("Keypad menu: Home A=edit/C=go; select A/B=position/C=edit; edit digits, *=clear, #=save, D=back.");
  Serial.println("GPIO4=next stored position, GPIO5=previous stored position.");
  Serial.println("Commands: steps=<300-60000>, speed=<100-200000>, accel=<100-500000>, move=<steps>, left/right, <n> r|l, home, time=<steps>, cont=<hz>, stop, enable, disable, status, menu, pos=<waarde>, help");
}

void printStatus() {
  Serial.printf("Status | pos=%ld (%s) moving=%s cont=%s drive=%s speed=%.1f accel=%.1f steps=%ld | end0=%s home=%s end1=%s\n",
                currentPos,
                positionKnown ? "known" : "unknown",
                isMoving ? "JA" : "nee",
                isContinuous ? "JA" : "nee",
                driveEnabled ? "AAN" : "uit",
                maxSpeedSetting,
                accelerationSetting,
                manualSteps,
                isEndLimit0Pressed() ? "PRESSED" : "open",
                isHomeLimitPressed() ? "PRESSED" : "open",
                isEndLimit1Pressed() ? "PRESSED" : "open");
}

void updateLcdStatus(bool force) {
  if (!lcdReady) return;
  if (!force && (millis() - lastLcdUpdate) < LCD_UPDATE_MS) return;
  lastLcdUpdate = millis();

  char line1[17];
  char line2[17];
  if (uiScreen == UI_HOME) {
    snprintf(line1, sizeof(line1), "P%u Cur:%ld", selectedPosition, currentPos);
    snprintf(line2, sizeof(line2), "A:Edit C:Go");
  } else if (uiScreen == UI_SELECT_POSITION) {
    snprintf(line1, sizeof(line1), "Select P%u=%ld", editPosition, savedPositions[editPosition]);
    snprintf(line2, sizeof(line2), "A/B Sel C:Edit");
  } else {
    snprintf(line1, sizeof(line1), "Edit P%u (steps)", editPosition);
    snprintf(line2, sizeof(line2), "%s #=Save D=Back", editValue.c_str());
  }

  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

uint32_t stepPeriodUsForIndex(long stepIndex, long totalSteps, float maxSpeed, float accel) {
  long accelSteps = (long)((maxSpeed * maxSpeed) / (2.0f * accel));
  if (accelSteps * 2 > totalSteps) {
    accelSteps = totalSteps / 2;
    maxSpeed = sqrtf(2.0f * accel * accelSteps);
  }

  long constSteps = totalSteps - 2 * accelSteps;
  float speed;

  if (stepIndex < accelSteps) {
    speed = sqrtf(2.0f * accel * (stepIndex + 1));
  } else if (stepIndex < (accelSteps + constSteps)) {
    speed = maxSpeed;
  } else {
    long decelIndex = totalSteps - stepIndex;
    speed = sqrtf(2.0f * accel * decelIndex);
  }

  if (speed < 50.0f) speed = 50.0f;
  uint32_t period = (uint32_t)(1000000.0f / speed);
  if (period <= PULSE_US) {
    period = PULSE_US + 1;
  }
  return period;
}

long fillMoveChunk(long startStep, long chunkSteps, long totalSteps, float maxSpeed, float accel) {
  long totalItems = 0;
  long endStep = startStep + chunkSteps;

  for (long stepIndex = startStep; stepIndex < endStep && totalItems < (long)rmtBufferItems - 2; ++stepIndex) {
    uint32_t period = stepPeriodUsForIndex(stepIndex, totalSteps, maxSpeed, accel);

    rmtItems[totalItems].level0 = 1;
    rmtItems[totalItems].duration0 = PULSE_US;
    rmtItems[totalItems].level1 = 0;
    rmtItems[totalItems].duration1 = period - PULSE_US;
    totalItems++;
  }

  if (totalItems > 0 && endStep >= totalSteps) {
    rmtItems[totalItems - 1].duration1 = 10;
  }

  return totalItems;
}

bool queueNextMoveChunk() {
  if (!moveActive) return false;

  long remaining = moveTotalSteps - moveNextStep;
  if (remaining <= 0) return false;

  long chunkSteps = remaining;
  if (chunkSteps > (long)rmtBufferItems - 2) {
    chunkSteps = (long)rmtBufferItems - 2;
  }

  long totalItems = fillMoveChunk(moveNextStep, chunkSteps, moveTotalSteps, maxSpeedSetting, accelerationSetting);
  if (totalItems <= 0) return false;

  moveNextStep += chunkSteps;
  rmt_write_items(RMT_CH, rmtItems, totalItems, false);
  return true;
}

void serviceMoveChunks() {
  if (!moveActive || isContinuous) return;
  if (!moveChunkDone) return;

  moveChunkDone = false;

  if (moveNextStep >= moveTotalSteps) {
    bool wasHoming = homingInProgress;
    moveActive = false;
    isMoving = false;
    setDriveEnabled(false);
    if (wasHoming) {
      homingInProgress = false;
      positionKnown = false;
      Serial.println("Home search failed: switch was not reached");
    } else {
      currentPos += activeMoveSteps;
      Serial.println("Move finished");
    }
    activeMoveSteps = 0;
    return;
  }

  if (!queueNextMoveChunk()) {
    moveActive = false;
    isMoving = false;
    setDriveEnabled(false);
    Serial.println("Move chunk failed");
  }
}

void moveServiceTask(void *param) {
  (void)param;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    serviceMoveChunks();
  }
}

void runMove(long steps, const char *label) {
  if (isMoving || isContinuous) {
    Serial.println("Motor is al bezig");
    return;
  }

  long plannedPulses = 0;
  float estMs = estimateMoveMs(steps, &plannedPulses);
  Serial.printf("Plan: %ld pulses, %.1f ms\n", plannedPulses, estMs);

  activeMoveSteps = steps;
  if (prepareMove(steps)) {
    Serial.printf("%s %ld steps\n", label, labs(steps));
  } else {
    activeMoveSteps = 0;
  }
}

void processSerialCommand(String command) {
  command.trim();
  if (command.length() == 0) return;

  command.toLowerCase();

  if (command == "help" || command == "?") {
    printSerialHelp();
    return;
  }

  if (command == "menu") {
    printSerialHelp();
    printStatus();
    return;
  }

  if (command == "status" || command == "tab") {
    printStatus();
    return;
  }

  if (command == "stop") {
    stopMotion();
    return;
  }

  if (command == "home") {
    startHoming();
    return;
  }

  if (command == "enable" || command == "on") {
    setDriveEnabled(true);
    Serial.println("Drive enabled");
    return;
  }

  if (command == "disable" || command == "off") {
    stopMotion();
    Serial.println("Drive disabled");
    return;
  }

  if (command.startsWith("steps=")) {
    long value = command.substring(6).toInt();
    manualSteps = constrain(value, 300, 60000);
    Serial.printf("Steps ingesteld op %ld\n", manualSteps);
    printStatus();
    return;
  }

  if (command.startsWith("speed=")) {
    long value = command.substring(6).toInt();
    if (value > 0) {
      maxSpeedSetting = constrain(value, 100, 200000);
      Serial.printf("Speed ingesteld op %.1f steps/s\n", maxSpeedSetting);
      printStatus();
      return;
    }
  }

  if (command.startsWith("accel=")) {
    long value = command.substring(6).toInt();
    if (value > 0) {
      accelerationSetting = constrain(value, 100, 500000);
      Serial.printf("Accel ingesteld op %.1f steps/s^2\n", accelerationSetting);
      printStatus();
      return;
    }
    return;
  }

  if (command.startsWith("pos=")) {
    currentPos = command.substring(4).toInt();
    Serial.printf("Positie handmatig ingesteld op %ld\n", currentPos);
    return;
  }

  if (command.startsWith("move=")) {
    long value = command.substring(5).toInt();
    if (value != 0) {
      runMove(value, "Move");
      return;
    }
  }

  if (command.startsWith("time=")) {
    long value = command.substring(5).toInt();
    if (value != 0) {
      long plannedPulses = 0;
      float estMs = estimateMoveMs(value, &plannedPulses);
      Serial.printf("Time: %ld pulses, %.1f ms (%.3f s)\n", plannedPulses, estMs, estMs / 1000.0f);
      return;
    }
  }

  if (command.startsWith("cont=")) {
    long hz = command.substring(5).toInt();
    if (hz != 0) {
      startContinuousTest(hz);
      return;
    }
  }

  if (command == "left" || command == "l") {
    runMove(-manualSteps, "Move LEFT");
    return;
  }

  if (command == "right" || command == "r") {
    runMove(manualSteps, "Move RIGHT");
    return;
  }

  int split = command.indexOf(' ');
  if (split > 0) {
    String first = command.substring(0, split);
    String second = command.substring(split + 1);
    second.trim();
    long value = first.toInt();

    if (value > 0 && (second == "r" || second == "right")) {
      runMove(value, "Move RIGHT");
      return;
    }
    if (value > 0 && (second == "l" || second == "left")) {
      runMove(-value, "Move LEFT");
      return;
    }
  }

  long value = command.toInt();
  if (value > 0) {
    manualSteps = constrain(value, 300, 60000);
    Serial.printf("Steps ingesteld op %ld\n", manualSteps);
    return;
  }

  Serial.println("Onbekend commando. Typ help.");
}

// Assemble a complete command without blocking motor/limit-switch processing.
void handleSerialCommand() {
  static String command;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      processSerialCommand(command);
      command = "";
    } else if (command.length() < 80) {
      command += c;
    } else {
      command = ""; // discard an overlong malformed command
      Serial.println("Command too long - discarded");
    }
  }
}
#endif

float estimateMoveMs(long steps, long *plannedPulses) {
  if (plannedPulses) *plannedPulses = 0;
  if (steps == 0) return 0.0f;

  long absSteps = labs(steps);
  float maxSpeed = maxSpeedSetting;
  float accel = accelerationSetting;
  long accelSteps = (long)((maxSpeed * maxSpeed) / (2.0f * accel));
  if (accelSteps * 2 > absSteps) {
    accelSteps = absSteps / 2;
    maxSpeed = sqrtf(2.0f * accel * accelSteps);
  }

  long constSteps = absSteps - 2 * accelSteps;
  double totalUs = 0.0;

  for (long i = 1; i <= accelSteps; i++) {
    float speed = sqrtf(2.0f * accel * i);
    if (speed < 50) speed = 50;
    totalUs += (1000000.0 / speed);
  }
  for (long i = 0; i < constSteps; i++) {
    totalUs += (1000000.0 / maxSpeed);
  }
  for (long i = accelSteps; i >= 1; i--) {
    float speed = sqrtf(2.0f * accel * i);
    if (speed < 50) speed = 50;
    totalUs += (1000000.0 / speed);
  }

  if (plannedPulses) *plannedPulses = absSteps;
  return (float)(totalUs / 1000.0);
}

bool startContinuousTest(long signedHz) {
  if (signedHz == 0) {
    Serial.println("cont=<hz>: hz mag niet 0 zijn");
    return false;
  }

  if (isMoving || isContinuous) {
    stopMotion();
  }

  setDriveEnabled(true);

  long hz = labs(signedHz);
  hz = constrain(hz, 10, 200000);
  bool dir = signedHz > 0;
  if ((dir && (isHomeLimitPressed() || isEndLimit1Pressed())) ||
      (!dir && isEndLimit0Pressed())) {
    Serial.println("Continuous motion blocked: relevant limit switch is pressed");
    return false;
  }
  moveDirPositive = dir;
  digitalWrite(PIN_DIR, dir ? HIGH : LOW);

  uint32_t periodUs = (uint32_t)(1000000UL / (uint32_t)hz);
  if (periodUs <= (PULSE_US + 1)) {
    periodUs = PULSE_US + 1;
  }

  rmtItems[0].level0 = 1;
  rmtItems[0].duration0 = PULSE_US;
  rmtItems[0].level1 = 0;
  rmtItems[0].duration1 = periodUs - PULSE_US;

  rmt_set_tx_loop_mode(RMT_CH, true);
  rmt_write_items(RMT_CH, rmtItems, 1, false);
  isContinuous = true;

  Serial.printf("Continuous ON: %ld Hz, DIR=%s\n", hz, dir ? "RIGHT" : "LEFT");
  return true;
}

void stopMotion() {
  rmt_tx_stop(RMT_CH);
  rmt_set_tx_loop_mode(RMT_CH, false);
  isMoving = false;
  isContinuous = false;
  moveActive = false;
  moveChunkDone = false;
  moveNextStep = 0;
  moveTotalSteps = 0;
  activeMoveSteps = 0;
  homingInProgress = false;
  setDriveEnabled(false);
  Serial.println("Motion stopped");
}

// Both switches are wired from GPIO to GND, so a closed switch reads LOW.
bool isHomeLimitPressed() {
  return digitalRead(PIN_HOME_LIMIT) == LOW;
}

bool isEndLimit0Pressed() {
  return digitalRead(PIN_END_LIMIT_0) == LOW;
}

bool isEndLimit1Pressed() {
  return digitalRead(PIN_END_LIMIT_1) == LOW;
}

void processLimitSwitches() {
  if (!isMoving && !isContinuous) return;

  bool hitHome = moveDirPositive && isHomeLimitPressed();
  bool hitEnd1 = moveDirPositive && isEndLimit1Pressed();
  bool hitEnd0 = !moveDirPositive && isEndLimit0Pressed();
  if (!hitHome && !hitEnd0 && !hitEnd1) return;

  bool wasHoming = homingInProgress;
  stopMotion();
  homingInProgress = false;
  if (hitHome) {
    currentPos = 0;
    positionKnown = true;
    Serial.println(wasHoming ? "Home reached: position set to 0" : "Home limit reached: motion stopped, position set to 0");
  } else {
    // A stop within an RMT pulse chunk has no exact pulse count, so do not
    // pretend the software position is still accurate. Run `home` to recover.
    positionKnown = false;
    Serial.println(hitEnd0 ? "End limit 0 reached: motion stopped; position unknown, run 'home'"
                           : "End limit 1 reached: motion stopped; position unknown, run 'home'");
  }
}

void debugLimitSwitches() {
#if DEBUG_INPUTS
  static int lastEnd0 = -1;
  static int lastHome = -1;
  static int lastEnd1 = -1;
  int end0 = isEndLimit0Pressed();
  int home = isHomeLimitPressed();
  int end1 = isEndLimit1Pressed();
  if (end0 != lastEnd0 || home != lastHome || end1 != lastEnd1) {
    lastEnd0 = end0;
    lastHome = home;
    lastEnd1 = end1;
    Serial.printf("Limits | end0(GPIO2)=%s home(GPIO15)=%s end1(GPIO17)=%s\n",
                  end0 ? "PRESSED" : "open",
                  home ? "PRESSED" : "open",
                  end1 ? "PRESSED" : "open");
  }
#endif
}

bool startHoming() {
  if (isMoving || isContinuous) {
    Serial.println("Cannot home while motion is active");
    return false;
  }
  if (isHomeLimitPressed()) {
    currentPos = 0;
    positionKnown = true;
    Serial.println("Already at home: position set to 0");
    return true;
  }

  homingInProgress = true;
  activeMoveSteps = HOME_SEARCH_MAX_STEPS;
  if (!prepareMove(HOME_SEARCH_MAX_STEPS)) {
    homingInProgress = false;
    activeMoveSteps = 0;
    return false;
  }
  Serial.println("Homing positive toward GPIO16/RX2; send 'stop' to abort");
  return true;
}

void setDriveEnabled(bool enabled) {
  driveEnabled = enabled;
#if USE_ENABLE_PIN
#if ENABLE_ACTIVE_LOW
  digitalWrite(PIN_ENABLE, enabled ? HIGH : LOW);
#else
  digitalWrite(PIN_ENABLE, enabled ? HIGH : LOW);
#endif
#else
  (void)enabled;
#endif
}

bool updateOneButton(DebouncedButton &button, uint8_t pin) {
  bool rawPressed = digitalRead(pin) == LOW; // button wired GPIO to GND
  if (rawPressed != button.candidatePressed) {
    button.candidatePressed = rawPressed;
    button.candidateSinceMs = millis();
    return false;
  }
  if (button.stablePressed != button.candidatePressed &&
      millis() - button.candidateSinceMs >= BUTTON_DEBOUNCE_MS) {
    button.stablePressed = button.candidatePressed;
    return button.stablePressed; // only true once: on the press edge
  }
  return false;
}

void goToSelectedPosition() {
  if (isMoving || isContinuous) return;
  if (!positionKnown) {
    Serial.println("Position is unknown; run 'home' before using stored positions");
    return;
  }
  long steps = savedPositions[selectedPosition] - currentPos;
  if (steps == 0) {
    Serial.printf("Already at position P%u\n", selectedPosition);
    return;
  }
  runMove(steps, "Move to stored position");
}

void updateNavigationButtons() {
  if (uiScreen != UI_HOME || isMoving || isContinuous) return;
  if (updateOneButton(nextButton, PIN_NAV_NEXT)) {
    if (selectedPosition < 3) {
      selectedPosition++;
      Serial.printf("Next position: P%u\n", selectedPosition);
      goToSelectedPosition();
    } else {
      Serial.println("Already at highest position P3");
    }
  }
  if (updateOneButton(prevButton, PIN_NAV_PREV)) {
    if (selectedPosition > 0) {
      selectedPosition--;
      Serial.printf("Previous position: P%u\n", selectedPosition);
      goToSelectedPosition();
    } else {
      Serial.println("Already at lowest position P0");
    }
  }
}

void loadPositions() {
  preferences.begin("drawer-pos", false);
  for (uint8_t i = 0; i < 4; ++i) {
    char key[5];
    snprintf(key, sizeof(key), "p%u", i);
    savedPositions[i] = preferences.getLong(key, 0);
  }
  // A position tracker has no physical feedback.  Start from the known P0.
  currentPos = savedPositions[0];
}

void savePosition(uint8_t index, long value) {
  char key[5];
  snprintf(key, sizeof(key), "p%u", index);
  savedPositions[index] = value;
  preferences.putLong(key, value);
  Serial.printf("Saved P%u = %ld steps\n", index, value);
}

char readKeypad() {
  // One row at a time is driven LOW; a pressed key pulls its column LOW.
  for (uint8_t row = 0; row < 4; ++row) {
    for (uint8_t r = 0; r < 4; ++r) digitalWrite(KEYPAD_ROWS[r], HIGH);
    digitalWrite(KEYPAD_ROWS[row], LOW);
    delayMicroseconds(3);
    for (uint8_t col = 0; col < 4; ++col) {
      if (digitalRead(KEYPAD_COLS[col]) == LOW) return KEYPAD_MAP[row][col];
    }
  }
  return '\0';
}

void handleKeypad() {
  char key = readKeypad();
  if (key != keypadCandidate) {
    keypadCandidate = key;
    keypadCandidateSinceMs = millis();
    return;
  }
  if (key == lastKey) return;
  // A key must be present for 40 ms; release must be stable for 40 ms too.
  // This prevents a floating/noisy keypad cable from creating key events.
  if (millis() - keypadCandidateSinceMs < 40) return;

  lastKey = key;
  if (key == '\0') return; // act only once, when a key is pressed

#if DEBUG_INPUTS
  Serial.printf("Keypad pressed: %c\n", key);
#endif

  if (key == 'D') {
    if (uiScreen == UI_HOME) stopMotion();
    else uiScreen = UI_HOME;
    updateLcdStatus(true);
    return;
  }

  if (uiScreen == UI_HOME) {
    if (key == 'A') {
      editPosition = selectedPosition;
      uiScreen = UI_SELECT_POSITION;
    } else if (key == 'C') {
      goToSelectedPosition();
    }
  } else if (uiScreen == UI_SELECT_POSITION) {
    if (key == 'A' && editPosition < 3) editPosition++;
    else if (key == 'B' && editPosition > 0) editPosition--;
    else if (key == 'C') {
      editValue = "";
      uiScreen = UI_EDIT_POSITION;
    }
  } else { // UI_EDIT_POSITION
    if (key >= '0' && key <= '9') {
      if (editValue.length() < 8) editValue += key;
    } else if (key == '*') {
      editValue = "";
    } else if (key == '#') {
      if (editValue.length()) {
        long value = constrain(editValue.toInt(), 0L, MAX_STORED_POSITION);
        savePosition(editPosition, value);
        selectedPosition = editPosition;
        uiScreen = UI_HOME;
      }
    }
  }
  updateLcdStatus(true);
}

void updatePotSettings() {
  if (millis() - lastPotReadMs < 100) return;
  lastPotReadMs = millis();

  // ADC1 pins are used deliberately; ADC2 pins cannot be read while Wi-Fi is active.
  int speedSum = 0;
  int accelSum = 0;
  for (uint8_t sample = 0; sample < 8; ++sample) {
    speedSum += analogRead(PIN_POT_SPEED);
    accelSum += analogRead(PIN_POT_ACCEL);
  }
  int speedRaw = speedSum / 8;
  int accelRaw = accelSum / 8;
  maxSpeedSetting = MIN_MAX_SPEED +
                    (MAX_MAX_SPEED - MIN_MAX_SPEED) * speedRaw / 4095.0f;
  accelerationSetting = MIN_ACCELERATION +
                        (MAX_ACCELERATION - MIN_ACCELERATION) * accelRaw / 4095.0f;

#if DEBUG_INPUTS
  static int lastSpeedRaw = -10000;
  static int lastAccelRaw = -10000;
  // Ignore ADC jitter, but report the first read and each meaningful turn.
  if (abs(speedRaw - lastSpeedRaw) >= 25 || abs(accelRaw - lastAccelRaw) >= 25) {
    lastSpeedRaw = speedRaw;
    lastAccelRaw = accelRaw;
    Serial.printf("Pots: speed raw=%d -> %.0f steps/s, accel raw=%d -> %.0f steps/s^2\n",
                  speedRaw, maxSpeedSetting, accelRaw, accelerationSetting);
  }
#endif
}

// ================== RMT INIT ==================
void setupRMT() {
  rmt_config_t config = {};
  config.rmt_mode = RMT_MODE_TX;
  config.channel = RMT_CH;
  config.gpio_num = (gpio_num_t)PIN_STEP;
  config.mem_block_num = 4;               // meer geheugen
  config.clk_div = RMT_DIV;
  config.tx_config.loop_en = false;
  config.tx_config.carrier_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

  rmt_config(&config);
  rmt_driver_install(RMT_CH, 0, 0);
}

// ================== BEWEGING BEREKENEN + BUFFER VULLEN ==================
bool prepareMove(long steps) {
  if (steps == 0) return false;
  if (isMoving || isContinuous) return false;

  moveDirPositive = steps > 0;
  if ((moveDirPositive && (isHomeLimitPressed() || isEndLimit1Pressed())) ||
      (!moveDirPositive && isEndLimit0Pressed())) {
    Serial.println("Move blocked: relevant limit switch is pressed");
    return false;
  }
  moveTotalSteps = labs(steps);
  moveNextStep = 0;
  moveChunkDone = false;
  moveActive = true;
  isMoving = true;

  digitalWrite(PIN_DIR, moveDirPositive ? HIGH : LOW);
  delayMicroseconds(10);
  setDriveEnabled(true);

  if (!queueNextMoveChunk()) {
    moveActive = false;
    isMoving = false;
    setDriveEnabled(false);
    return false;
  }

  Serial.printf("isMoving!\n");
  return true;
}

// Wordt aangeroepen als de RMT klaar is
void IRAM_ATTR rmt_tx_end_callback(rmt_channel_t channel, void *arg) {
  (void)channel;
  (void)arg;
  if (moveActive && !isContinuous) {
    moveChunkDone = true;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (moveServiceTaskHandle != nullptr) {
      vTaskNotifyGiveFromISR(moveServiceTaskHandle, &higherPriorityTaskWoken);
    }
    if (higherPriorityTaskWoken == pdTRUE) {
      portYIELD_FROM_ISR();
    }
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("APP: setup start");
  Serial.printf("APP: reset reason=%d\n", (int)esp_reset_reason());
  Serial.flush();

  rmtItems = (rmt_item32_t *)heap_caps_malloc(RMT_BUFFER_ITEMS * sizeof(rmt_item32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (rmtItems == nullptr) {
    rmtItems = (rmt_item32_t *)malloc(RMT_BUFFER_ITEMS * sizeof(rmt_item32_t));
  }
  if (rmtItems == nullptr) {
    Serial.println("RMT buffer alloc failed");
    while (true) {
      delay(1000);
    }
  }
  rmtBufferItems = RMT_BUFFER_ITEMS;
  Serial.println("APP: RMT buffer ready");
  Serial.flush();

  Serial.println("APP: Wire begin");
  Serial.flush();
  Wire.begin();
  Serial.println("APP: Wire ready");
  Serial.flush();
  Serial.println("APP: LCD init");
  Serial.flush();
  lcd.init();
  Serial.println("APP: LCD ready");
  Serial.flush();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("T6 Drive Ready");
  lcd.setCursor(0, 1);
  lcd.print("I2C:0x27");
  lcdReady = true;

  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_NAV_NEXT, INPUT_PULLUP);
  pinMode(PIN_NAV_PREV, INPUT_PULLUP);
  pinMode(PIN_HOME_LIMIT, INPUT_PULLUP);
  pinMode(PIN_END_LIMIT_0, INPUT_PULLUP);
  pinMode(PIN_END_LIMIT_1, INPUT_PULLUP);

  for (uint8_t row = 0; row < 4; ++row) {
    pinMode(KEYPAD_ROWS[row], OUTPUT);
    digitalWrite(KEYPAD_ROWS[row], HIGH);
  }
  for (uint8_t col = 0; col < 4; ++col) {
    pinMode(KEYPAD_COLS[col], INPUT_PULLUP);
  }
  
#if USE_ENABLE_PIN
  pinMode(PIN_ENABLE, OUTPUT);
  setDriveEnabled(false);
#endif

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT_SPEED, ADC_11db);
  analogSetPinAttenuation(PIN_POT_ACCEL, ADC_11db);
  loadPositions();

  setupRMT();

  xTaskCreatePinnedToCore(
    moveServiceTask,
    "moveServiceTask",
    4096,
    nullptr,
    3,
    &moveServiceTaskHandle,
    1
  );

  // Callback als beweging klaar is
  rmt_register_tx_end_callback(rmt_tx_end_callback, NULL);

  Serial.println("T6 – Volledige RMT buffer versie klaar");
#if DEBUG_INPUTS
  Serial.println("Input debug: GPIO2=end0(-), GPIO15=home(+), GPIO17/TX2=end1(+); switches use INPUT_PULLUP.");
#endif
#if USE_SERIAL_STEPS
  printSerialHelp();
  printStatus();
  Serial.printf("Enable pin: %s on GPIO %d\n", USE_ENABLE_PIN ? "enabled" : "not used", PIN_ENABLE);
#if USE_ENABLE_PIN
  Serial.printf("Enable polarity: active-%s\n", ENABLE_ACTIVE_LOW ? "LOW" : "HIGH");
  Serial.println("Drive starts disabled. Use 'enable' before testing motion, 'stop' to stop and disable.");
#endif
#endif
}

// ================== LOOP ==================
void loop() {
  processLimitSwitches();
  debugLimitSwitches();
  updatePotSettings();
  updateNavigationButtons();
  handleKeypad();

#if USE_SERIAL_STEPS
  handleSerialCommand();
#endif

  updateLcdStatus(false);

  // Debug
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    Serial.printf("Pos: %ld  Moving: %s  Cont: %s\n", currentPos, isMoving ? "JA" : "nee", isContinuous ? "JA" : "nee");
  }
}
