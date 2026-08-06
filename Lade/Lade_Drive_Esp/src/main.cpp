#include <Arduino.h>
#include "driver/rmt.h"
#include <vector>

// ================== PINS ==================
#define PIN_STEP      18
#define PIN_DIR       19
#define PIN_ENABLE    21
#define PIN_LIMIT_L   4
#define PIN_LIMIT_R   5
#define PIN_BTN_LEFT  12
#define PIN_BTN_RIGHT 13
#define PIN_BTN_HOME  14
#define PIN_POT       34

// ================== PARAMETERS ==================
const float MAX_SPEED    = 5000.0f;   // steps/sec
const float ACCELERATION = 12000.0f;  // steps/sec²
const uint32_t PULSE_US  = 2;         // pulse breedte

// ================== RMT ==================
#define RMT_CH          RMT_CHANNEL_0
#define RMT_DIV         80            // 1 µs tick
#define MAX_RMT_ITEMS   512           // max items per buffer (pas aan indien nodig)

rmt_item32_t rmtItems[MAX_RMT_ITEMS];

// ================== STATE ==================
volatile long currentPos = 0;
bool isMoving = false;

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

  isMoving = true;
  return true;
}

// Wordt aangeroepen als de RMT klaar is
void IRAM_ATTR rmt_tx_end_callback(rmt_channel_t channel, void *arg) {
  isMoving = false;
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_ENABLE, OUTPUT);
  digitalWrite(PIN_ENABLE, HIGH);

  pinMode(PIN_LIMIT_L, INPUT_PULLUP);
  pinMode(PIN_LIMIT_R, INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BTN_HOME, INPUT_PULLUP);

  analogReadResolution(12);

  setupRMT();

  // Callback als beweging klaar is
  rmt_register_tx_end_callback(rmt_tx_end_callback, NULL);

  Serial.println("T6 – Volledige RMT buffer versie klaar");
}

// ================== LOOP ==================
void loop() {
  // Limits controleren tijdens beweging
  if (isMoving) {
    if (digitalRead(PIN_LIMIT_L) == LOW || digitalRead(PIN_LIMIT_R) == LOW) {
      rmt_tx_stop(RMT_CH);
      isMoving = false;
      Serial.println("Limit geraakt – gestopt");
    }
  }

  // Knoppen
  static unsigned long lastBtn = 0;
  if (!isMoving && millis() - lastBtn > 50) {
    lastBtn = millis();

    int pot = analogRead(PIN_POT);
    long steps = map(pot, 0, 4095, 300, 6000);

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
      Serial.println("Home nog niet geïmplementeerd in deze versie");
    }
  }

  // Debug
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    Serial.printf("Pos: %ld  Moving: %s\n", currentPos, isMoving ? "JA" : "nee");
  }
}