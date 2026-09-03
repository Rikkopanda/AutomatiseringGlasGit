#include <Arduino.h>

/*
  UI ESP32 — Modbus RTU SLAVE, station address 2
  -------------------------------------------------
  Sits on the same RS-485 bus as the drive and the control ESP32, but never
  initiates anything — it just answers when the control ESP32 (the master)
  polls it. That's what avoids bus contention with a second master.

  Holding registers exposed:
    0  setpoint     r/min requested by the user (0..3000 here, adjust to taste)
    1  run          0 = stop, 1 = run
    2  status echo  control ESP32 writes the drive's Dn-18 status bits here
                     so this board can show them (bit=0 means that function
                     is ON: bit0 Alarm, bit1 Ready, bit7 Run, etc. — see the
                     table in control_esp32.ino)

  Library: "Modbus-ESP8266" by Alexander Emelianov (same one as the other sketch).
  Wiring: same as control_esp32.ino — MS2548 auto-direction, no direction pin needed.
          RE and SHDN on the module must be tied HIGH (usually already done on the breakout).
*/

#include <ModbusRTU.h>

#define RXD2 16
#define TXD2 17
// MS2548 handles TX/RX direction automatically -- no direction pin from the ESP32

#define POT_PIN 34         // potentiometer wiper, ADC-capable pin
#define RUN_BUTTON_PIN 27  // momentary button to GND, uses internal pull-up

ModbusRTU mb;
const uint8_t MY_SLAVE_ID = 2;

const uint16_t REG_SETPOINT = 0;
const uint16_t REG_RUN      = 1;
const uint16_t REG_STATUS   = 2;

void setup() {
  pinMode(RUN_BUTTON_PIN, INPUT_PULLUP);

  Serial2.begin(9600, SERIAL_8N2, RXD2, TXD2); // must match the drive's Pn066/Pn067
  mb.begin(&Serial2, -1); // -1 = no direction pin, MS2548 switches automatically
  mb.slave(MY_SLAVE_ID);

  mb.addHreg(REG_SETPOINT, 0);
  mb.addHreg(REG_RUN, 0);
  mb.addHreg(REG_STATUS, 0);
}

void loop() {
  mb.task();

  static unsigned long lastLocalUpdate = 0;
  if (millis() - lastLocalUpdate > 50) {
    lastLocalUpdate = millis();

    int raw = analogRead(POT_PIN);                  // 0..4095
    int16_t speed = map(raw, 0, 4095, 0, 3000);      // r/min — match to your motor/load
    mb.Hreg(REG_SETPOINT, (uint16_t)speed);

    bool run = (digitalRead(RUN_BUTTON_PIN) == LOW); // active-low button
    mb.Hreg(REG_RUN, run ? 1 : 0);
  }

  uint16_t status = mb.Hreg(REG_STATUS);
  // TODO: drive an OLED/LEDs from `status` bits here, e.g.:
  // bool ready = !(status & (1 << 1));
  // bool alarm = !(status & (1 << 0));
}
