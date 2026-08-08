#include <Arduino.h>
#include "driver/rmt.h"
#include <vector>

// ================== PINS ==================
#define PIN_STEP      14
#define PIN_DIR       12
#define PIN_ENABLE    21
#define PIN_LIMIT_L   4
#define PIN_LIMIT_R   5
#define PIN_BTN_LEFT  12
#define PIN_BTN_RIGHT 13
#define PIN_BTN_HOME  14
#define PIN_POT       34
#define USE_ENABLE_PIN 0    // 1 = physical enable wire used, 0 = no enable pin on drive
#define ENABLE_ACTIVE_LOW 1   // 1: ENABLE low = ON (common), 0: ENABLE high = ON
#define USE_BUTTONS   0   // 1 = buttons installed, 0 = serial only
#define USE_SERIAL_STEPS 1   // 1 = via serial, 0 = via potmeter

// ================== PARAMETERS ==================
const float MAX_SPEED    = 5000.0f;   // steps/sec
const float ACCELERATION = 12000.0f;  // steps/sec²
const uint32_t PULSE_US  = 5;         // pulse breedte (5 us is usually safer for servo/driver inputs)

// ================== RMT ==================
#define RMT_CH          RMT_CHANNEL_0
#define RMT_DIV         80            // 1 µs tick
#define MAX_RMT_ITEMS   512           // max items per buffer (pas aan indien nodig)

rmt_item32_t rmtItems[MAX_RMT_ITEMS];

// ================== STATE ==================
volatile long currentPos = 0;
bool isMoving = false;
bool isContinuous = false;

bool prepareMove(long steps);
float estimateMoveMs(long steps, long *plannedPulses = nullptr);
bool startContinuousTest(long signedHz);
void stopMotion();
void setDriveEnabled(bool enabled);

#if USE_SERIAL_STEPS
long manualSteps = 1000;

// ================== SERIAL COMMANDS ==================
void printSerialHelp() {
  Serial.println("Commands: steps=<300-6000>, move=<steps>, left/right, <n> r|l, time=<steps>, cont=<hz>, stop, pos=<waarde>, help");
}

void runMove(long steps, const char *label) {
  if (isMoving || isContinuous) {
    Serial.println("Motor is al bezig");
    return;
  }

  setDriveEnabled(true);

  long plannedPulses = 0;
  float estMs = estimateMoveMs(steps, &plannedPulses);
  Serial.printf("Plan: %ld pulses, %.1f ms\n", plannedPulses, estMs);
  if (plannedPulses < labs(steps)) {
    Serial.printf("Let op: gevraagd=%ld, buffer kan max %ld pulses per move\n", labs(steps), plannedPulses);
  }

  if (prepareMove(steps)) {
    currentPos += steps;
    Serial.printf("%s %ld steps\n", label, labs(steps));
  }
}

void handleSerialCommand() {
  if (!Serial.available()) return;

  String command = Serial.readStringUntil('\n');
  command.trim();
  if (command.length() == 0) return;

  command.toLowerCase();

  if (command == "help" || command == "?") {
    printSerialHelp();
    return;
  }

  if (command == "stop") {
    stopMotion();
    return;
  }

  if (command.startsWith("steps=")) {
    long value = command.substring(6).toInt();
    manualSteps = constrain(value, 300, 6000);
    Serial.printf("Steps ingesteld op %ld\n", manualSteps);
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
    manualSteps = constrain(value, 300, 6000);
    Serial.printf("Steps ingesteld op %ld\n", manualSteps);
    return;
  }

  Serial.println("Onbekend commando. Typ help.");
}
#endif

float estimateMoveMs(long steps, long *plannedPulses) {
  if (plannedPulses) *plannedPulses = 0;
  if (steps == 0) return 0.0f;

  long absSteps = labs(steps);
  float maxSpeed = MAX_SPEED;
  float accel = ACCELERATION;
  long accelSteps = (long)((maxSpeed * maxSpeed) / (2.0f * accel));
  if (accelSteps * 2 > absSteps) {
    accelSteps = absSteps / 2;
    maxSpeed = sqrtf(2.0f * accel * accelSteps);
  }

  long constSteps = absSteps - 2 * accelSteps;
  long maxItems = MAX_RMT_ITEMS - 2;
  long totalItems = 0;
  double totalUs = 0.0;

  for (long i = 1; i <= accelSteps && totalItems < maxItems; i++) {
    float speed = sqrtf(2.0f * accel * i);
    if (speed < 50) speed = 50;
    totalUs += (1000000.0 / speed);
    totalItems++;
  }
  for (long i = 0; i < constSteps && totalItems < maxItems; i++) {
    totalUs += (1000000.0 / maxSpeed);
    totalItems++;
  }
  for (long i = accelSteps; i >= 1 && totalItems < maxItems; i--) {
    float speed = sqrtf(2.0f * accel * i);
    if (speed < 50) speed = 50;
    totalUs += (1000000.0 / speed);
    totalItems++;
  }

  if (plannedPulses) *plannedPulses = totalItems;
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
  setDriveEnabled(false);
  Serial.println("Motion stopped");
}

void setDriveEnabled(bool enabled) {
#if USE_ENABLE_PIN
#if ENABLE_ACTIVE_LOW
  digitalWrite(PIN_ENABLE, enabled ? LOW : HIGH);
#else
  digitalWrite(PIN_ENABLE, enabled ? HIGH : LOW);
#endif
#else
  (void)enabled;
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

  bool dir = steps > 0;
  long absSteps = abs(steps);

  digitalWrite(PIN_DIR, dir ? HIGH : LOW);
  delayMicroseconds(10);

  // Bereken aantal stappen voor accel/decel
  float maxSpeed = MAX_SPEED;
  float accel = ACCELERATION;

  // s = v² / (2a)
  long accelSteps = (long)((maxSpeed * maxSpeed) / (2.0f * accel));
  if (accelSteps * 2 > absSteps) {
    // Korte beweging → driehoek-profiel
    accelSteps = absSteps / 2;
    maxSpeed = sqrtf(2.0f * accel * accelSteps);
  }

  long constSteps = absSteps - 2 * accelSteps;
  long totalItems = 0;

  // --- Accel fase ---
  for (long i = 1; i <= accelSteps && totalItems < MAX_RMT_ITEMS - 2; i++) {
    float speed = sqrtf(2.0f * accel * i);
    if (speed < 50) speed = 50;
    uint32_t period = (uint32_t)(1000000.0f / speed);

    rmtItems[totalItems].level0 = 1;
    rmtItems[totalItems].duration0 = PULSE_US;
    rmtItems[totalItems].level1 = 0;
    rmtItems[totalItems].duration1 = period - PULSE_US;
    totalItems++;
  }

  // --- Constante snelheid ---
  uint32_t constPeriod = (uint32_t)(1000000.0f / maxSpeed);
  for (long i = 0; i < constSteps && totalItems < MAX_RMT_ITEMS - 2; i++) {
    rmtItems[totalItems].level0 = 1;
    rmtItems[totalItems].duration0 = PULSE_US;
    rmtItems[totalItems].level1 = 0;
    rmtItems[totalItems].duration1 = constPeriod - PULSE_US;
    totalItems++;
  }

  // --- Decel fase ---
  for (long i = accelSteps; i >= 1 && totalItems < MAX_RMT_ITEMS - 2; i--) {
    float speed = sqrtf(2.0f * accel * i);
    if (speed < 50) speed = 50;
    uint32_t period = (uint32_t)(1000000.0f / speed);

    rmtItems[totalItems].level0 = 1;
    rmtItems[totalItems].duration0 = PULSE_US;
    rmtItems[totalItems].level1 = 0;
    rmtItems[totalItems].duration1 = period - PULSE_US;
    totalItems++;
  }

  if (totalItems == 0) return false;

  // Laatste item afsluiten
  rmtItems[totalItems - 1].duration1 = 10;

  // Buffer naar RMT sturen
  rmt_write_items(RMT_CH, rmtItems, totalItems, false);  // non-blocking
  Serial.printf("isMoving!\n");
  isMoving = true;
  return true;
}

// Wordt aangeroepen als de RMT klaar is
void IRAM_ATTR rmt_tx_end_callback(rmt_channel_t channel, void *arg) {
  (void)channel;
  (void)arg;
  isMoving = false;
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_DIR, OUTPUT);
  
#if USE_ENABLE_PIN
  pinMode(PIN_ENABLE, OUTPUT);
  setDriveEnabled(false);
#endif

#if USE_BUTTONS
  pinMode(PIN_LIMIT_L, INPUT_PULLUP);
  pinMode(PIN_LIMIT_R, INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BTN_HOME, INPUT_PULLUP);
#endif

#if !USE_SERIAL_STEPS
  analogReadResolution(12);
#endif

  setupRMT();

  // Callback als beweging klaar is
  rmt_register_tx_end_callback(rmt_tx_end_callback, NULL);

  Serial.println("T6 – Volledige RMT buffer versie klaar");
#if USE_SERIAL_STEPS
  printSerialHelp();
  Serial.printf("Enable pin: %s\n", USE_ENABLE_PIN ? "enabled" : "not used");
#if USE_ENABLE_PIN
  Serial.printf("Enable polarity: active-%s\n", ENABLE_ACTIVE_LOW ? "LOW" : "HIGH");
#endif
#endif
}

// ================== LOOP ==================
void loop() {
#if USE_SERIAL_STEPS
  handleSerialCommand();
#endif

#if USE_BUTTONS
  // Limits controleren tijdens beweging
  // if (isMoving) {
  //   if (digitalRead(PIN_LIMIT_L) == LOW || digitalRead(PIN_LIMIT_R) == LOW) {
  //     rmt_tx_stop(RMT_CH);
  //     isMoving = false;
  //     Serial.println("Limit geraakt – gestopt");
  //   }
  // }

  // Knoppen
  static unsigned long lastBtn = 0;
  if (!isMoving && millis() - lastBtn > 50) {
    lastBtn = millis();

#if USE_SERIAL_STEPS
    long steps = manualSteps;
#else
    int pot = analogRead(PIN_POT);
    long steps = map(pot, 0, 4095, 300, 6000);
#endif

    if (digitalRead(PIN_BTN_LEFT) == LOW) {
      if (prepareMove(-steps)) {
        currentPos -= steps;   // we tellen vooruit (of gebruik encoder feedback later)
        Serial.printf("Move LEFT %ld steps\n", steps);
      }
    }
    if (digitalRead(PIN_BTN_RIGHT) == LOW) {
      if (prepareMove(steps)) {
        currentPos += steps;
        Serial.printf("Move RIGHT %ld steps\n", steps);
      }
    }
    if (digitalRead(PIN_BTN_HOME) == LOW) {
      // Eenvoudige home – later uitbreiden
      // Serial.println("Home nog niet geïmplementeerd in deze versie");
    }
  }
#endif

  // Debug
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    Serial.printf("Pos: %ld  Moving: %s  Cont: %s\n", currentPos, isMoving ? "JA" : "nee", isContinuous ? "JA" : "nee");
  }
}