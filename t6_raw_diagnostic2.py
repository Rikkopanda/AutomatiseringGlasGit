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

# all 6 frame formats the drive's Pr5.29 supports
FRAME_FORMATS = [
    (serial.EIGHTBITS, serial.PARITY_EVEN, serial.STOPBITS_TWO, "8E2"),
    (serial.EIGHTBITS, serial.PARITY_ODD,  serial.STOPBITS_TWO, "8O2"),
    (serial.EIGHTBITS, serial.PARITY_EVEN, serial.STOPBITS_ONE, "8E1"),
    (serial.EIGHTBITS, serial.PARITY_ODD,  serial.STOPBITS_ONE, "8O1"),
    (serial.EIGHTBITS, serial.PARITY_NONE, serial.STOPBITS_ONE, "8N1"),
    (serial.EIGHTBITS, serial.PARITY_NONE, serial.STOPBITS_TWO, "8N2"),
]

# manual's own worked example: servo enable, slave ID 1
FRAME = bytes.fromhex("01 06 20 09 00 01 93 C8".replace(" ", ""))

found = False
for baud in BAUD_CANDIDATES:
    for bytesize, parity, stopbits, label in FRAME_FORMATS:
        print(f"--- {baud} baud, {label} ---", end=" ")
        try:
            ser = serial.Serial(
                port=PORT,
                baudrate=baud,
                bytesize=bytesize,
                parity=parity,
                stopbits=stopbits,
                timeout=0.3,
            )
        except serial.SerialException as e:
            print(f"could not open port: {e}")
            raise SystemExit(1)

        ser.reset_input_buffer()
        ser.write(FRAME)
        ser.flush()
        time.sleep(0.08)
        response = ser.read(64)
        ser.close()

        if response:
            match = " <-- MATCH, this is it" if response == FRAME else " (response, but no match)"
            print(f"got {response.hex(' ')}{match}")
            if response == FRAME:
                found = True
        else:
            print("nothing")

if not found:
    print("\nNo combination responded at all. At this point I'd want to see the")
    print("raw bytes Motion Studio itself sends on Windows (e.g. via a serial")
    print("sniffer, or just noting the baud/parity it reports in its connection")
    print("dialog) rather than keep guessing combinations blind.")
