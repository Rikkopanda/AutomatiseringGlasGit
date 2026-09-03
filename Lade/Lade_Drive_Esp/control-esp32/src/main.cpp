#include <Arduino.h>

/*
  CONTROL ESP32 — Modbus RTU MASTER
  ----------------------------------
  One RS-485 bus, three nodes:
    - this board (master, no address of its own on Modbus)
    - servo drive     (slave, station = drive's Pn065, default 1)
    - UI ESP32        (slave, station = 2, see ui_esp32.ino)

  Library: "Modbus-ESP8266" by Alexander Emelianov (emelianov/modbus-esp8266)
           install via Library Manager, search "Modbus ESP8266" — works on ESP32 too.

  Wiring: MS2548 auto-direction transceiver, no DE/RE pin needed from the ESP32.
          R (RXD) -> RXD2, D (TXD) -> TXD2, A/B -> bus, GND -> common ground with the other two nodes.
          On the module, RE and SHDN must be tied HIGH (usually hardwired on the breakout already).
          Bus needs 560R pull-up on A->VCC and 560R pull-down on B->GND at ONE point on the bus,
          plus 120R termination at each of the two physical ends of the bus.

  ---------------------------------------------------------------------------
  Register map on the servo drive (from the manual, chapter 4 & 7):

    Pn002  0x0002  control mode          -> 1 = speed mode
                    *marked (triangle) in the manual = needs a POWER CYCLE after
                     changing, so set this once via the keypad before you rely on
                     comms, don't expect the write below to take effect live.
    Pn064  0x0040  comm interface        -> 2 = RS-485   (set via keypad first)
    Pn065  0x0041  station address       -> drive's slave id (default 1)
    Pn066  0x0042  baud rate             -> 0=4800 1=9600 2=19200 3=38400 4=57600 5=115200
    Pn067  0x0043  frame format          -> 6 = 8,N,2,RTU   (also needs power cycle)
    Pn068  0x0044  comm-control enable   -> bit0=1: SON (servo enable) is now driven by comms
                                             instead of the CN2 input pin
    Pn070  0x0046  comm-control state    -> bit0 of this register IS the SON command.
                                             Logic is inverted: 0 = ON, 1 = OFF.
                                             Default value is 32691 (0x7FB3) with bit0=1 (OFF).
    Pn168  0x00A8  speed command source  -> 1 = use internal speed presets 1-8
    Pn169  0x00A9  internal speed 1      -> signed r/min, -5000..5000.
                                             We reuse this register as our live speed
                                             setpoint (see note below).
    Dn-01  0x0171  speed command (read-only monitor)
    Dn-08  0x0178  actual motor speed (read-only monitor)
    Dn-18  0x0182  output status bits (read-only). Bit = 0 means that function is ON:
                    bit0 Alarm, bit1 Ready, bit2 Emg, bit3 Preach, bit4 Sreach,
                    bit5 Treach, bit6 ZeroSpeed, bit7 Run

  IMPORTANT: with Pn168=1, the drive normally picks which of the 8 internal speed
  presets is active using its physical SP1/SP2/SP3 inputs (SigIn7/8/9 on CN2).
  Leave those inputs unwired/open on the drive — with no signal they default to
  "000" = internal speed 1, which is the Pn169 register we're rewriting live.
  If your CN2 has those pins wired to something else, this trick won't work and
  you'd need to also comm-control bits 8-10 of Pn068/Pn070 (Sp1/Sp2/Sp3).

  BEFORE ANY OF THIS WORKS: on the drive's own keypad, set Pn064=2 (RS-485),
  Pn065=1 (or whatever station id you want), Pn066 and Pn067 to match the baud/
  frame settings below, then power-cycle the drive once for Pn064/Pn067 to load.
  ---------------------------------------------------------------------------
*/

#include <ModbusRTU.h>

#define RXD2 16
#define TXD2 17
// MS2548 handles TX/RX direction automatically -- no direction pin from the ESP32

ModbusRTU mb;

const uint8_t DRIVE_ID = 1;   // must match drive's Pn065
const uint8_t UI_ID    = 2;   // must match MY_SLAVE_ID in ui_esp32.ino

// --- drive registers ---
const uint16_t REG_PN068_COMM_EN     = 0x0044;
const uint16_t REG_PN070_COMM_STATE  = 0x0046;
const uint16_t REG_PN168_SPD_SRC     = 0x00A8;
const uint16_t REG_PN169_SPEED_SET   = 0x00A9;
const uint16_t REG_DN08_ACTUAL_SPEED = 0x0178;
const uint16_t REG_DN18_STATUS       = 0x0182;

const uint16_t PN070_DEFAULT = 32691; // 0x7FB3, all bits at their factory "inactive" state

// --- UI ESP32 registers (defined in ui_esp32.ino) ---
const uint16_t UI_REG_SETPOINT = 0; // r/min the user asked for
const uint16_t UI_REG_RUN      = 1; // 0 = stop, 1 = run
const uint16_t UI_REG_STATUS   = 2; // we write drive status here for the UI to show

// live state
int16_t  actualSpeed = 0;
uint16_t driveStatus = 0;
uint16_t uiSetpoint  = 0;
uint16_t uiRun       = 0;

uint8_t setupStep = 0;
unsigned long lastSetupTx = 0;
unsigned long lastDrivePoll = 0;
unsigned long lastUiPoll = 0;

bool cbDefault(Modbus::ResultCode event, uint16_t, void*) {
  if (event != Modbus::EX_SUCCESS) {
    Serial.printf("Modbus error 0x%02X\n", event);
  }
  return true;
}

void setDriveEnable(bool en) {
  uint16_t val = en ? (PN070_DEFAULT & ~0x0001) : PN070_DEFAULT;
  mb.writeHreg(DRIVE_ID, REG_PN070_COMM_STATE, val, cbDefault);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N2, RXD2, TXD2); // must match drive's Pn066/Pn067
  mb.begin(&Serial2, -1); // -1 = no direction pin, MS2548 switches automatically
  mb.master();
}

void loop() {
  mb.task();

  // one-time drive setup on boot, one write at a time so we don't flood the bus
  if (setupStep < 3 && millis() - lastSetupTx > 200) {
    lastSetupTx = millis();
    switch (setupStep) {
      case 0: mb.writeHreg(DRIVE_ID, REG_PN068_COMM_EN, 0x0001, cbDefault); break; // SON via comms
      case 1: mb.writeHreg(DRIVE_ID, REG_PN168_SPD_SRC, 1, cbDefault); break;      // internal speed mode
      case 2: setDriveEnable(false); break;                                       // start disabled — safe default
    }
    setupStep++;
  }

  if (setupStep < 3) return; // wait for setup writes to be sent before polling

  // poll the drive every 50 ms — this is the control-loop-relevant read
  if (millis() - lastDrivePoll > 50) {
    lastDrivePoll = millis();
    mb.readHreg(DRIVE_ID, REG_DN08_ACTUAL_SPEED, (uint16_t*)&actualSpeed, 1, cbDefault);
    mb.readHreg(DRIVE_ID, REG_DN18_STATUS, &driveStatus, 1, cbDefault);
  }

  // poll the UI ESP32 every 150 ms — not time-critical
  if (millis() - lastUiPoll > 150) {
    lastUiPoll = millis();
    mb.readHreg(UI_ID, UI_REG_SETPOINT, &uiSetpoint, 1, cbDefault);
    mb.readHreg(UI_ID, UI_REG_RUN, &uiRun, 1, cbDefault);

    // act on what the UI asked for
    setDriveEnable(uiRun != 0);
    mb.writeHreg(DRIVE_ID, REG_PN169_SPEED_SET, uiSetpoint, cbDefault);

    // echo drive status back to the UI so it can display it
    mb.writeHreg(UI_ID, UI_REG_STATUS, driveStatus, cbDefault);
  }

  // TODO: replace the direct "uiSetpoint -> drive" pass-through above with
  // your actual control loop (ramping, limits, closed-loop correction, etc.)
}
