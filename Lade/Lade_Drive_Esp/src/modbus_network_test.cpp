#include <Arduino.h>
#include <ModbusRTU.h>

// Bench test master: talks only to the UI slave. It never addresses the drive.
constexpr uint8_t UI_ID = 2;
constexpr int RXD2 = 16;
constexpr int TXD2 = 17;

ModbusRTU mb;
uint16_t uiRegisters[3] = {};
bool requestPending = false;
uint32_t lastRequestMs = 0;

bool onUiRead(Modbus::ResultCode result, uint16_t transactionId, void*) {
  requestPending = false;

  if (result == Modbus::EX_SUCCESS) {
    Serial.printf("PASS: UI id=%u transaction=%u setpoint=%u run=%u status=0x%04X\n",
                  UI_ID, transactionId, uiRegisters[0], uiRegisters[1], uiRegisters[2]);
  } else {
    Serial.printf("FAIL: UI id=%u transaction=%u Modbus error=0x%02X\n",
                  UI_ID, transactionId, result);
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N2, RXD2, TXD2);
  mb.begin(&Serial2, -1); // RS-485 adapter with automatic direction control
  mb.master();
  Serial.println("Modbus RTU UI test starting: slave=2, 9600 8N2, registers 0..2");
}

void loop() {
  mb.task();

  if (!requestPending && millis() - lastRequestMs >= 1000) {
    lastRequestMs = millis();
    requestPending = mb.readHreg(UI_ID, 0, uiRegisters, 3, onUiRead) != 0;
    if (!requestPending) {
      Serial.println("FAIL: unable to queue Modbus request");
    }
  }
}
