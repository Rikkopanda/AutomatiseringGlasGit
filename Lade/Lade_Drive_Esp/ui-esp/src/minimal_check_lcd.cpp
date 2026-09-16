#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);  // Address 0x27, 16x2 display

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n=== LCD TEST ON GPIO21/22 ===\n");
  
  Serial.println("[TEST] Initializing I2C on GPIO21/22 @ 100kHz...");
  Wire.begin(21, 22, 100000);  // SDA=21, SCL=22, 100kHz clock
  delay(100);
  
  Serial.println("[TEST] Scanning for devices...");
  uint8_t found_addr = 0;
  for (int addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[TEST] Found device at 0x%02X\n", addr);
      if (found_addr == 0) found_addr = addr;  // Remember first device
    }
  }
  
  if (found_addr == 0) {
    Serial.println("[ERROR] No I2C device found!");
    while(1) delay(1000);
  }
  
  Serial.printf("[TEST] Initializing LCD at 0x%02X...\n", found_addr);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Hello");
}

void loop() {
  delay(2000);
  
  // Flash the backlight to show it's alive
  // lcd.noBacklight();
  delay(200);
  // lcd.print("LCD Test OK!");

  // lcd.backlight();
  // lcd.print("GPIO21/22 works");

  // Serial.println("[LOOP] LCD backlight toggled");
}   