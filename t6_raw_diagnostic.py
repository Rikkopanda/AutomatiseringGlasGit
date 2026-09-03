#!/usr/bin/env python3
"""
Raw serial diagnostic -- bypasses pymodbus entirely and sends the EXACT byte
sequence the T6 manual documents for "servo enable" (Appendix A / 9.5.2.1
example 3): 01 06 20 09 00 01 93 C8

On success, Modbus write-single-register echoes the identical frame back.
This isolates whether the problem is wiring/settings (nothing comes back at
all) vs. something specific to how pymodbus builds its requests (something
comes back, just not what pymodbus expected).

Install first:  pip install pyserial
"""

import serial
import time

PORT = "/dev/ttyUSB0"   # change to match `ls /dev/ttyUSB*`

BAUD_CANDIDATES = [9600, 19200, 38400, 4800, 2400, 57600, 115200]

# manual's own worked example: servo enable, slave ID 1
FRAME = bytes.fromhex("01 06 20 09 00 01 93 C8".replace(" ", ""))

for baud in BAUD_CANDIDATES:
    print(f"\n--- trying {baud} baud, 8N2 ---")
    try:
        ser = serial.Serial(
            port=PORT,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_TWO,
            timeout=0.5,
        )
    except serial.SerialException as e:
        print(f"  could not open port: {e}")
        break  # port problem, not a baud problem -- no point trying more

    ser.reset_input_buffer()
    ser.write(FRAME)
    ser.flush()
    time.sleep(0.1)
    response = ser.read(64)
    ser.close()

    if response:
        print(f"  sent:     {FRAME.hex(' ')}")
        print(f"  received: {response.hex(' ')}")
        if response == FRAME:
            print(f"  MATCH -- drive is responding correctly at {baud} baud, 8N2")
        else:
            print("  got a response but it doesn't match -- check parity/stopbits next")
    else:
        print("  nothing received")

print("\nIf every baud rate above returned nothing, double-check the drive's")
print("current Pr5.29 (frame format) and Pr5.30 (baud) on its own keypad --")
print("something may have set them away from the 5/2 (8N2/9600) factory default.")
