/**
 * @file lcd_display.cpp
 * @brief LCD I2C display implementation for UI ESP
 */

#include "../../shared/modbus_common.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>

// ============================================================================
// PRIVATE STATE
// ============================================================================

static LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
static bool lcdReady = false;
static unsigned long lastUpdate = 0;
static char displayedLine0[17] = "";
static char displayedLine1[17] = "";

// ============================================================================
// INITIALIZATION
// ============================================================================

bool lcdInit() {
  // Initialize I2C on pins 21 (SDA), 22 (SCL) @ 100kHz clock
  Serial.printf("[LCD] Initializing I2C on SDA=GPIO%d, SCL=GPIO%d @ 100kHz...\n", LCD_SDA_PIN, LCD_SCL_PIN);
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN, 100000);
  delay(150); // Allow bus lines to stabilize and LCD power-on reset to finish

  // Test communication with expected LCD address
  Wire.beginTransmission(LCD_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.printf("[LCD] Expected LCD at 0x%02X did not respond! Scanning bus...\n", LCD_I2C_ADDR);
    bool foundAny = false;
    for (int addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("[LCD] I2C device found at 0x%02X\n", addr);
        foundAny = true;
      }
    }
    if (!foundAny) {
      Serial.printf("[LCD] No I2C devices found on GPIO%d/GPIO%d!\n", LCD_SDA_PIN, LCD_SCL_PIN);
      return false;
    }
  } else {
    Serial.printf("[LCD] Found LCD at expected address 0x%02X\n", LCD_I2C_ADDR);
  }

  // Initialize LCD hardware (exact sequence as minimal_check_lcd)
  delay(100);
  lcd.init();
  delay(50);
  lcd.backlight();
  delay(50);
  lcd.clear();
  delay(50);
  
  // Test initial write
  lcd.setCursor(0, 0);
  lcd.print("UI ESP Ready");
  lcd.setCursor(0, 1);
  lcd.print("Speed: 0 RPM");
  
  strncpy(displayedLine0, "UI ESP Ready", sizeof(displayedLine0));
  strncpy(displayedLine1, "Speed: 0 RPM", sizeof(displayedLine1));

  lcdReady = true;
  Serial.printf("[LCD] Initialized at 0x%02X\n", LCD_I2C_ADDR);
  
  return true;
}

// ============================================================================
// DISPLAY FUNCTIONS
// ============================================================================

void printlcd(const char *str)
{
  lcd.setCursor(0, 0);
  lcd.print(str);
  Serial.printf("[UI]: %s\n", str);

}

void lcdUpdateSpeed(uint16_t speedRpm, bool isRunning, uint16_t statusBits) {
  if (!lcdReady) return;
  
  unsigned long now = millis();
  if (now - lastUpdate < LCD_UPDATE_MS) {
    return; // Rate limit updates
  }
  lastUpdate = now;
  
  // Line 0: Speed and run status (16 chars max)
  char line0[17];
  const char* runStr = isRunning ? "RUN" : "STOP";
  snprintf(line0, sizeof(line0), "SPD:%4u %s    ", speedRpm, runStr);
  line0[16] = '\0';
  
  // Only write to I2C if line 0 actually changed
  if (strncmp(line0, displayedLine0, sizeof(line0)) != 0) {
    strncpy(displayedLine0, line0, sizeof(displayedLine0));
    lcd.setCursor(0, 0);
    lcd.print(line0);
  }
  
  // Line 1: Status bits (16 chars max)
  char line1[17];
  bool alarm = (statusBits & (1 << 0)) == 0;
  bool ready = (statusBits & (1 << 1)) == 0;
  bool running = (statusBits & (1 << 7)) == 0;
  
  snprintf(line1, sizeof(line1), "A:%c R:%c RUN:%c  ",
           alarm ? 'Y' : 'N',
           ready ? 'Y' : 'N',
           running ? 'Y' : 'N');
  line1[16] = '\0';
  
  // Only write to I2C if line 1 actually changed
  if (strncmp(line1, displayedLine1, sizeof(line1)) != 0) {
    strncpy(displayedLine1, line1, sizeof(displayedLine1));
    lcd.setCursor(0, 1);
    lcd.print(line1);
  }
}

void lcdPrintLine0(const char* msg) {
  if (!lcdReady) return;
  
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", msg);
  buf[16] = '\0';
  
  if (strncmp(buf, displayedLine0, sizeof(buf)) != 0) {
    strncpy(displayedLine0, buf, sizeof(displayedLine0));
    lcd.setCursor(0, 0);
    lcd.print(buf);
  }
}

void lcdPrintLine1(const char* msg) {
  if (!lcdReady) return;
  
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", msg);
  buf[16] = '\0';
  
  if (strncmp(buf, displayedLine1, sizeof(buf)) != 0) {
    strncpy(displayedLine1, buf, sizeof(displayedLine1));
    lcd.setCursor(0, 1);
    lcd.print(buf);
  }
}

void lcdClear() {
  if (!lcdReady) return;
  lcd.clear();
  displayedLine0[0] = '\0';
  displayedLine1[0] = '\0';
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
