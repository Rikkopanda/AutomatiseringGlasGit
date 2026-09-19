#include <Arduino.h>
#include <ModbusRTU.h>

#define RXD2 16
#define TXD2 17

ModbusRTU mb;

void setup() {
  Serial.begin(115200);

  Serial2.begin(9600, SERIAL_8N2, RXD2, TXD2);
  mb.begin(&Serial2, -1);
  mb.slave(2);

  mb.addHreg(0, 1234);

  Serial.println("SLAVE READY");
}

void loop() {
  mb.task();

  static uint32_t last = 0;

  if (millis() - last > 1000) {
    last = millis();
    Serial.println("SLAVE ALIVE");
  }
}