#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("  UI ESP32 (SLAVE)");
  Serial.println("  Version: 1.0");
  Serial.println("========================================");
  Serial.println();
  
  Serial.println("[TEST] Starting Wire.begin on GPIO21/222...");
  Wire.begin(21, 22);  // SDA, SCL
  Wire.setTimeOut(50);
  Serial.println("[TEST] Wire started");
  
  // Check I2C bus state before scanning
  Serial.println("[TEST] Reading GPIO21 (SDA) and GPIO222 (SCL) levels...");
  pinMode(21, INPUT);
  pinMode(22, INPUT);
  int sda_level = digitalRead(21);
  int scl_level = digitalRead(22);
  Serial.printf("[TEST] SDA (GPIO21) = %d, SCL (GPIO222) = %d\n", sda_level, scl_level);
  
  if (sda_level == 0 || scl_level == 0) {
    Serial.println("[TEST] WARNING: I2C bus lines are held LOW! Check pull-ups and level shifter.");
  }
  
  Serial.println("[TEST] Full I2C address scan (0x00-0x7F)...");
  uint8_t found_count = 0;
  for (uint8_t addr = 0; addr < 0x80; addr++) {
    unsigned long scan_start = millis();
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    unsigned long scan_time = millis() - scan_start;
    
    if (error == 0) {
      Serial.printf("[TEST] FOUND at 0x%02X (took %lu ms)\n", addr, scan_time);
      found_count++;
    } else if (scan_time > 100) {
      Serial.printf("[TEST] TIMEOUT at 0x%02X (error=%u, time=%lu ms) - BUS STUCK\n", addr, error, scan_time);
      break;  // Stop scanning if we hit a timeout; bus is stuck
    }
  }
  Serial.printf("[TEST] Total devices found: %u\n", found_count);
  
  if (found_count == 0) {
    Serial.println("[TEST] ERROR: No I2C devices found! Check wiring and power.");
    while (1) {
      delay(1000);
      Serial.println("[TEST] Waiting for manual recovery...");
    }
  }
  
  Serial.println("[TEST] Trying direct I2C transmission to 0x27...");
  Wire.beginTransmission(0x27);
  uint8_t error = Wire.endTransmission();
  Serial.printf("[TEST] I2C result code: %u (0=success, 1=data too long, 2=NACK addr, 3=NACK data, 4=other)\n", error);
  
  Serial.println("[TEST] Initializing LCD at 0x27...");
  delay(100);
  
  // Wrap lcd.init() with timeout protection
  unsigned long init_start = millis();
  lcd.init();
  unsigned long init_time = millis() - init_start;
  Serial.printf("[TEST] LCD init() took %lu ms\n", init_time);
  Serial.println("[TEST] LCD init() complete");
  
  Serial.println("[TEST] LCD backlight...");
  lcd.backlight();
  
  Serial.println("[TEST] LCD clear and write 'TEST'...");
  lcd.clear();
  delay(50);
  lcd.setCursor(0, 0);
  lcd.print("TEST");
  lcd.setCursor(0, 1);
  lcd.print("I2C OK");
  
  Serial.println("[TEST] Setup complete!");
}   

int i = 0;
char str[20];  // Allocate 20-byte buffer
void loop()
{
    Serial.println("ok");
    lcd.clear();
    snprintf(str, sizeof(str), "Hi %d", i);
    delay(500);
    lcd.print(str);
    i++;
}