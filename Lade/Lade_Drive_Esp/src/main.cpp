#include <Arduino.h>
#include "driver/rmt.h"

// ================== PIN DEFINITIONS ==================
#define PIN_STEP      18
#define PIN_DIR       19
#define PIN_ENABLE    21

#define PIN_LIMIT_L   4
#define PIN_LIMIT_R   5
#define PIN_BTN_LEFT  12
#define PIN_BTN_RIGHT 13
#define PIN_BTN_HOME  14
#define PIN_POT       34

// ================== MOTION PARAMETERS ==================
const float MAX_SPEED     = 6000.0f;   // steps/sec
const float ACCELERATION  = 15000.0f;  // steps/sec²
const float MIN_SPEED     = 100.0f;

// ================== RMT CONFIG ==================
#define RMT_TX_CHANNEL    RMT_CHANNEL_0
#define RMT_CLK_DIV       80          // 80 MHz / 80 = 1 µs tick
#define PULSE_HIGH_US     2           // pulse width (2 µs is veilig)

rmt_config_t rmt_tx_config;
rmt_item32_t rmt_item;

// ================== MOTION STATE ==================
volatile long currentPos = 0;
long targetPos = 0;
bool isRunning = false;
bool currentDir = true;

float currentSpeed = 0.0f;
unsigned long lastUpdate = 0;

// ================== RMT INITIALISATIE ==================
void setupRMT() {
  rmt_tx_config.rmt_mode = RMT_MODE_TX;
  rmt_tx_config.channel = RMT_TX_CHANNEL;
  rmt_tx_config.gpio_num = (gpio_num_t)PIN_STEP;
  rmt_tx_config.mem_block_num = 1;
  rmt_tx_config.clk_div = RMT_CLK_DIV;
  rmt_tx_config.tx_config.loop_en = false;
  rmt_tx_config.tx_config.carrier_en = false;
  rmt_tx_config.tx_config.idle_output_en = true;
  rmt_tx_config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

  rmt_config(&rmt_tx_config);
  rmt_driver_install(rmt_tx_config.channel, 0, 0);

  // Eén pulse item (high + low)
  rmt_item.level0 = 1;
  rmt_item.duration0 = PULSE_HIGH_US;
  rmt_item.level1 = 0;
  rmt_item.duration1 = 2;   // minimale low tijd
}

// Stuur één schone step-pulse via RMT
void rmtStepOnce() {
  rmt_write_items(RMT_TX_CHANNEL, &rmt_item, 1, true);  // wait_tx_done = true
  if (currentDir) currentPos++;
  else            currentPos--;
}

// ================== MOTION CONTROL ==================
void setDirection(bool dir) {
  if (dir == currentDir) return;
  currentDir = dir;
  digitalWrite(PIN_DIR, dir ? HIGH : LOW);
  delayMicroseconds(5);   // kleine setup-tijd voor DIR
}

void moveTo(long target) {
  targetPos = target;
  isRunning = true;
}

void stopMotor() {
  isRunning = false;
  currentSpeed = 0;
}

void updateMotion() {
  if (!isRunning) return;

  // Hard limits
  if (digitalRead(PIN_LIMIT_L) == LOW && !currentDir) {
    stopMotor();
    Serial.println("Limit LEFT hit");
    return;
  }
  if (digitalRead(PIN_LIMIT_R) == LOW && currentDir) {
    stopMotor();
    Serial.println("Limit RIGHT hit");
    return;
  }

  long distance = targetPos - currentPos;
  if (distance == 0) {
    stopMotor();
    return;
  }

  // Richting instellen
  bool wantDir = (distance > 0);
  if (wantDir != currentDir) {
    setDirection(wantDir);
    currentSpeed = 0;          // soft stop bij omkering
  }

  // --- Eenvoudige trapezium / accel-decel berekening ---
  float absDist = abs(distance);
  float targetSpeed = MAX_SPEED;

  // Remafstand
  float stopDist = (currentSpeed * currentSpeed) / (2.0f * ACCELERATION);
  if (absDist <= stopDist) {
    targetSpeed = sqrtf(2.0f * ACCELERATION * absDist);
  }

  // Accel / Decel
  unsigned long now = micros();
  float dt = (now - lastUpdate) / 1000000.0f;
  if (dt > 0.02f) dt = 0.02f;   // beveiliging
  lastUpdate = now;

  if (currentSpeed < targetSpeed) {
    currentSpeed += ACCELERATION * dt;
    if (currentSpeed > targetSpeed) currentSpeed = targetSpeed;
  } else {
    currentSpeed -= ACCELERATION * dt;
    if (currentSpeed < targetSpeed) currentSpeed = targetSpeed;
  }

  if (currentSpeed < MIN_SPEED) currentSpeed = MIN_SPEED;

  // Interval tussen stappen
  float interval_us = 1000000.0f / currentSpeed;

  // RMT pulse sturen als het tijd is
  static unsigned long lastStep = 0;
  if (now - lastStep >= interval_us) {
    rmtStepOnce();
    lastStep = now;
  }
}

// ================== SETUP & LOOP ==================
void setup() {
  Serial.begin(115200);
  delay(500);

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

  Serial.println("T6 Servo – RMT versie gestart");
}

void loop() {
  // Knoppen (met eenvoudige debounce)
  static unsigned long lastBtnCheck = 0;
  if (millis() - lastBtnCheck > 40) {
    lastBtnCheck = millis();

    int pot = analogRead(PIN_POT);
    long steps = map(pot, 0, 4095, 200, 8000);   // aantal stappen afhankelijk van pot

    if (digitalRead(PIN_BTN_LEFT) == LOW) {
      moveTo(currentPos - steps);
    }
    if (digitalRead(PIN_BTN_RIGHT) == LOW) {
      moveTo(currentPos + steps);
    }
    if (digitalRead(PIN_BTN_HOME) == LOW) {
      moveTo(0);
    }
  }

  updateMotion();

  // Debug output
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 250) {
    lastPrint = millis();
    Serial.printf("Pos:%6ld  Target:%6ld  Speed:%6.0f\n",
                  currentPos, targetPos, currentSpeed);
  }
}