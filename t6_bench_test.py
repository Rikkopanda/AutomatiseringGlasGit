#!/usr/bin/env python3
"""
T6 servo drive bench-test script — talks over RS-232 via CN4 pins 6/7/8,
using a plain USB-RS232 adapter. No RS-485 hardware needed for this.

Install first:
    pip install pymodbus pyserial

Wiring (crossed, as always):
    drive CN4 pin 6 (TXD) -> adapter RXD
    drive CN4 pin 7 (RXD) -> adapter TXD
    drive CN4 pin 8 (GND) -> adapter GND

Drive comm defaults (unchanged from factory): 9600 baud, 8 data bits,
no parity, 2 stop bits, slave ID 1 -- matches the settings below.

Find your adapter's device node first:
    ls /dev/ttyUSB*      (or /dev/ttyACM* depending on the chipset)
You may need to add your user to the 'dialout' group to access it without sudo:
    sudo usermod -aG dialout $USER      (then log out/in)
"""

import time
from pymodbus.client import ModbusSerialClient

PORT = "/dev/ttyUSB1"     # <-- change to match `ls /dev/ttyUSB*`
BAUD = 9600
SLAVE_ID = 1

# --- registers, same ones used in the ESP32 sketch ---
REG_ENABLE      = 0x2009
REG_PATH0_MODE  = 0x6200
REG_PATH0_SPEED = 0x6203
REG_PATH0_ACCEL = 0x6204
REG_PATH0_DECEL = 0x6205
REG_TRIGGER     = 0x6002
REG_INPUT_IO    = 0x602E
REG_OUTPUT_IO   = 0x602F

MODE_VELOCITY = 0x0002
TRIGGER_PATH0 = 0x0010
TRIGGER_ESTOP = 0x0040


def connect():
    client = ModbusSerialClient(
        port=PORT,
        baudrate=BAUD,
        bytesize=8,
        parity="N",
        stopbits=2,
        timeout=1,
    )
    if not client.connect():
        raise RuntimeError(f"Could not open {PORT} -- check the device node and permissions")
    return client


def write(client, addr, value, label=""):
    result = client.write_register(addr, value, device_id=SLAVE_ID)
    if result.isError():
        print(f"  ERROR writing 0x{addr:04X} = {value}  ({label})")
    else:
        print(f"  wrote 0x{addr:04X} = {value}  ({label})")


def read(client, addr, label=""):
    result = client.read_holding_registers(addr, count=1, device_id=SLAVE_ID)
    if result.isError():
        print(f"  ERROR reading 0x{addr:04X}  ({label})")
        return None
    val = result.registers[0]
    print(f"  read  0x{addr:04X} = {val}  ({label})")
    return val


def enable(client):
    write(client, REG_ENABLE, 1, "servo enable")


def disable(client):
    write(client, REG_ENABLE, 0, "servo disable")


def configure_velocity_mode(client, accel_ms=50, decel_ms=50):
    write(client, REG_PATH0_MODE, MODE_VELOCITY, "path0 mode = velocity")
    write(client, REG_PATH0_ACCEL, accel_ms, "path0 accel")
    write(client, REG_PATH0_DECEL, decel_ms, "path0 decel")


def run_at_speed(client, rpm):
    write(client, REG_PATH0_SPEED, rpm, f"path0 speed = {rpm} rpm")
    write(client, REG_TRIGGER, TRIGGER_PATH0, "trigger path0")


def estop(client):
    write(client, REG_TRIGGER, TRIGGER_ESTOP, "E-stop")


def status(client):
    read(client, REG_INPUT_IO, "input IO status")
    read(client, REG_OUTPUT_IO, "output IO status")


if __name__ == "__main__":
    client = connect()
    print("Connected. Reading status...")
    status(client)

    print("\nConfiguring path0 for velocity mode...")
    configure_velocity_mode(client)

    print("\nEnabling servo...")
    enable(client)
    time.sleep(0.2)

    print("\nCommanding 100 rpm for 3 seconds...")
    run_at_speed(client, 100)
    time.sleep(3)

    print("\nStopping and disabling...")
    estop(client)
    time.sleep(0.2)
    disable(client)

    print("\nFinal status:")
    status(client)

    client.close()
