/**
 * @file lcd_display.cpp
 * @brief LCD I2C display implementation for UI ESP
 */

#include "lcd_display.h"
#include "../../shared/modbus_common.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>

// ============================================================================
// PRIVATE STATE
// ============================================================================

static LiquidCrystal_I2C* lcd = nullptr;
static bool lcdReady = false;
static unsigned long lastUpdate = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

bool lcdInit() {
  // Initialize I2C on pins 25 (SDA), 26 (SCL)
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  Wire.setTimeOut(50);
  Wire.setClock(100000); // Standard I2C speed

  uint8_t detectedAddress = 0;
  Serial.println("[LCD] Scanning I2C...");
  for (uint8_t address = 1; address < 0x78; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[LCD] I2C device found at 0x%02X\n", address);
      if (detectedAddress == 0) {
        detectedAddress = address;
      }
    }
  }

  if (detectedAddress == 0) {
    Serial.println("[LCD] No I2C device found on GPIO25/GPIO26");
    return false;
  }

  lcd = new LiquidCrystal_I2C(detectedAddress, LCD_COLS, LCD_ROWS);
  
  // Try to initialize display
  delay(50);
  lcd->init();
  delay(50);
  lcd->backlight();
  lcd->clear();
  delay(10);
  
  // Test write to verify connection
  lcd->setCursor(0, 0);
  lcd->print("UI ESP Ready");
  lcd->setCursor(0, 1);
  lcd->print("Speed: 0 RPM");
  
  lcdReady = true;
  Serial.printf("[LCD] Initialized at 0x%02X\n", detectedAddress);
  
  return true;
}

// ============================================================================
// DISPLAY FUNCTIONS
// ============================================================================

void lcdUpdateSpeed(uint16_t speedRpm, bool isRunning, uint16_t statusBits) {
  if (!lcdReady) return;
  
  unsigned long now = millis();
  if (now - lastUpdate < LCD_UPDATE_MS) {
    return; // Rate limit updates
  }
  lastUpdate = now;
  
  // Line 0: Speed and run status
  char line0[17];
  const char* runStr = isRunning ? "RUN" : "STOP";
  snprintf(line0, sizeof(line0), "SPD:%4u %s  ", speedRpm, runStr);
  
  lcd->setCursor(0, 0);
  lcd->print(line0);
  
  // Line 1: Status bits
  char line1[17];
  bool alarm = (statusBits & (1 << 0)) == 0;
  bool ready = (statusBits & (1 << 1)) == 0;
  bool running = (statusBits & (1 << 7)) == 0;
  
  snprintf(line1, sizeof(line1), "ALM:%c RDY:%c RUN:%c",
           alarm ? 'Y' : 'N',
           ready ? 'Y' : 'N',
           running ? 'Y' : 'N');
  
  lcd->setCursor(0, 1);
  lcd->print(line1);
}

void lcdPrintLine0(const char* msg) {
  if (!lcdReady) return;
  
  lcd->setCursor(0, 0);
  lcd->print("                "); // Clear line
  lcd->setCursor(0, 0);
  lcd->print(msg);
}

void lcdPrintLine1(const char* msg) {
  if (!lcdReady) return;
  
  lcd->setCursor(0, 1);
  lcd->print("                "); // Clear line
  lcd->setCursor(0, 1);
  lcd->print(msg);
}

void lcdClear() {
  if (!lcdReady) return;
  lcd->clear();
}

void lcdShowEncoderMode(const char* mode) {
  if (!lcdReady) return;
  
  char line[17];
  snprintf(line, sizeof(line), "Mode: %-10s", mode);
  lcdPrintLine0(line);
}

void lcdShowError(const char* errorMsg) {
  if (!lcdReady) return;
  
  Serial.printf("[LCD] ERROR: %s\n", errorMsg);
  lcdClear();
  lcdPrintLine0("ERROR:");
  lcdPrintLine1(errorMsg);
}

// ============================================================================
// BACKGROUND TASK
// ============================================================================

void lcdTask() {
  if (!lcdReady) return;
  
  // Background tasks like I2C recovery can go here if needed
}

// ============================================================================
// STATUS
// ============================================================================

bool lcdIsReady() {
  return lcdReady;
}
