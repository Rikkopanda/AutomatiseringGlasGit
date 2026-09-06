#include <Wire.h>
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n=== I2C BUS TEST (NO DEVICE) ===\n");
  
  Serial.println("[TEST] Initializing I2C on GPIO25/26...");
  Wire.begin(25, 26);
  Wire.setTimeOut(100);  // 100ms timeout per operation
  Serial.println("[TEST] Wire started");
  
  // Test 1: Check line levels
  Serial.println("\n[TEST] Checking I2C line levels (should be 1=HIGH):");
  pinMode(25, INPUT);
  pinMode(26, INPUT);
  int sda = digitalRead(25);
  int scl = digitalRead(26);
  Serial.printf("  SDA (GPIO25) = %d\n", sda);
  Serial.printf("  SCL (GPIO26) = %d\n", scl);
  
  if (sda == 0 || scl == 0) {
    Serial.println("[ERROR] I2C bus lines held LOW! Level shifter or pull-ups are broken.");
    Serial.println("[FIX] Try:");
    Serial.println("  1. Remove LCD backpack (disconnect entirely)");
    Serial.println("  2. Check level shifter has 3.3V power AND GND");
    Serial.println("  3. Verify pull-up resistors on backpack (should be desoldered or disconnected)");
    while(1) delay(1000);
  }
  
  // Test 2: Try a simple transmission to non-existent address
  Serial.println("\n[TEST] Attempting transmission to 0x20 (no device expected):");
  unsigned long start = micros();
  Wire.beginTransmission(0x20);
  uint8_t error = Wire.endTransmission();
  unsigned long elapsed_us = micros() - start;
  
  Serial.printf("  Result: error=%u, time=%lu µs\n", error, elapsed_us);
  Serial.println("  (error=2=NACK is GOOD; error=4/5=timeout is BAD)");
  
  if (elapsed_us > 10000) {
    Serial.println("\n[ERROR] I2C operation is FROZEN (>10ms)!");
    Serial.println("\n[DIAGNOSIS] Attempting I2C bus recovery by bit-banging SCL...");
    
    // Try to recover stuck bus by clocking SCL
    pinMode(26, OUTPUT);  // SCL as output
    pinMode(25, INPUT);   // SDA as input to monitor
    digitalWrite(26, HIGH);
    
    Serial.println("[RECOVERY] Sending 9 clock pulses on SCL to unstick any slave...");
    for (int i = 0; i < 9; i++) {
      digitalWrite(26, LOW);
      delayMicroseconds(10);
      digitalWrite(26, HIGH);
      delayMicroseconds(10);
      int sda_state = digitalRead(25);
      Serial.printf("  Pulse %d: SDA=%d\n", i+1, sda_state);
    }
    
    // Send STOP condition
    digitalWrite(26, LOW);
    delayMicroseconds(10);
    digitalWrite(26, HIGH);
    delayMicroseconds(10);
    
    Serial.println("\n[RECOVERY] Bus recovery attempted.");
    Serial.println("[NEXT] Disconnect LCD backpack completely and try again.");
    Serial.println("[OR]   Check level shifter is properly powered.");
    
    while(1) delay(1000);
  }
  
  // Test 3: Multiple rapid transmissions
  Serial.println("\n[TEST] Rapid I2C tests (5x transmissions to 0x50):");
  for (int i = 0; i < 5; i++) {
    start = micros();
    Wire.beginTransmission(0x50);
    error = Wire.endTransmission();
    elapsed_us = micros() - start;
    Serial.printf("  Attempt %d: error=%u, time=%lu µs\n", i+1, error, elapsed_us);
    delay(50);
  }
  
  Serial.println("\n[TEST] If all times < 5ms: I2C bus is healthy!");
  Serial.println("[TEST] If times > 10ms or error=4/5: Bus is stuck (LCD backpack issue)\n");
}

void loop() {
  delay(5000);
  Serial.println("[LOOP] I2C test complete, waiting...");
}
