#include <Arduino.h>
#include <ModbusRTU.h>

#define RXD2 16
#define TXD2 17

ModbusRTU mb;

uint16_t value = 0;

bool cb(Modbus::ResultCode event, uint16_t, void*) {
  if (event != Modbus::EX_SUCCESS) {
    Serial.printf("Modbus error: 0x%02X\n", event);
  } else {
    Serial.printf("Received: %u\n", value);
  }
  return true;
}

void setup() {
  Serial.begin(115200);

  Serial2.begin(9600, SERIAL_8N2, RXD2, TXD2);
  mb.begin(&Serial2, -1);
  mb.master();

  Serial.println("MASTER READY");
}

void loop() {
  mb.task();

  static uint32_t last = 0;

  if (millis() - last > 1000) {
    last = millis();

    Serial.println("Requesting slave...");

    mb.readHreg(
      2,        // slave ID
      0,        // register 0
      &value,
      1,
      cb
    );
  }
}