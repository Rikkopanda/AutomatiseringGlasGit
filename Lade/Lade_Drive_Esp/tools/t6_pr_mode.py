#!/usr/bin/env python3
"""Small, guarded Modbus-RTU tool for the T6 servo PR-mode interface.

The defaults match the successful mbpoll command used on this machine:
slave 17, /dev/ttyUSB0, 38400 8N2.

This is deliberately not a general-purpose motion controller.  It only offers
read-only inspection plus a bounded *relative* move.  A move is refused unless
the caller explicitly confirms that the mechanism has been made safe.
"""

import argparse
import struct
import sys
import time

import serial


# T6 manual, chapter 9 (PR mode).
REG_CONTROL_MODE = 0x0003       # Pr0.01: must be 6 (PR mode)
REG_COM_FORMAT = 0x053B         # Pr5.29
REG_COM_BAUD = 0x053D           # Pr5.30
REG_SLAVE_ID = 0x053F           # Pr5.31
REG_SERVO_ENABLE = 0x2009       # documented communication servo-enable
REG_PR_CONTROL = 0x6002         # Pr8.02
REG_PATH0 = 0x6200              # Pr9.00 ... Pr9.07
REG_CMD_POSITION = 0x602A       # Pr8.42/43, signed 32-bit
REG_MOTOR_POSITION = 0x602C     # Pr8.44/45, signed 32-bit

PULSES_PER_REV = 10_000         # T6 PR-mode unit, per manual


class ModbusError(RuntimeError):
    pass


def crc16(data: bytes) -> int:
    """Modbus RTU CRC-16; serialises low byte first."""
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


class T6:
    def __init__(self, args):
        self.slave = args.slave
        self.port = serial.Serial(
            args.port, args.baud, bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_TWO,
            timeout=args.timeout, write_timeout=args.timeout,
        )
        # Discard a partial frame left by another diagnostic program.
        self.port.reset_input_buffer()

    def close(self):
        self.port.close()

    def transaction(self, pdu: bytes) -> bytes:
        request = bytes([self.slave]) + pdu
        frame = request + struct.pack("<H", crc16(request))
        self.port.reset_input_buffer()
        self.port.write(frame)
        self.port.flush()

        head = self._read_exact(2)
        if head[0] != self.slave:
            raise ModbusError(f"response from unexpected slave {head[0]}")
        if head[1] == (pdu[0] | 0x80):
            code = self._read_exact(1)[0]
            self._check_crc(head + bytes([code]) + self._read_exact(2))
            raise ModbusError(f"drive rejected function 0x{pdu[0]:02X}: exception 0x{code:02X}")
        if head[1] != pdu[0]:
            raise ModbusError(f"unexpected function 0x{head[1]:02X}")

        if pdu[0] == 0x03:
            byte_count = self._read_exact(1)[0]
            rest = self._read_exact(byte_count + 2)
            response = head + bytes([byte_count]) + rest
            self._check_crc(response)
            return rest[:-2]
        if pdu[0] in (0x06, 0x10):
            rest = self._read_exact(6)
            response = head + rest
            self._check_crc(response)
            return response[2:-2]
        raise ModbusError("unsupported response")

    def _read_exact(self, count: int) -> bytes:
        data = self.port.read(count)
        if len(data) != count:
            raise ModbusError(f"timeout: expected {count} response bytes, received {len(data)}")
        return data

    @staticmethod
    def _check_crc(frame: bytes):
        received = struct.unpack("<H", frame[-2:])[0]
        calculated = crc16(frame[:-2])
        if received != calculated:
            raise ModbusError(f"bad CRC (got 0x{received:04X}, expected 0x{calculated:04X})")

    def read(self, address: int, count: int = 1) -> list[int]:
        payload = self.transaction(struct.pack(">BHH", 0x03, address, count))
        if len(payload) != count * 2:
            raise ModbusError("incorrect read response length")
        return list(struct.unpack(">" + "H" * count, payload))

    def write(self, address: int, value: int):
        reply = self.transaction(struct.pack(">BHH", 0x06, address, value & 0xFFFF))
        if reply != struct.pack(">HH", address, value & 0xFFFF):
            raise ModbusError("write-single reply did not echo request")

    def write_many(self, address: int, values: list[int]):
        body = struct.pack(">" + "H" * len(values), *(value & 0xFFFF for value in values))
        reply = self.transaction(struct.pack(">BHHB", 0x10, address, len(values), len(body)) + body)
        if reply != struct.pack(">HH", address, len(values)):
            raise ModbusError("write-multiple reply did not echo request")


def signed32(high: int, low: int) -> int:
    return struct.unpack(">i", struct.pack(">HH", high, low))[0]


def read_control_mode(drive: T6) -> int:
    """Read Pr0.01 using a four-register block.

    Some T6 firmware revisions return a stale zero for a one-register read at
    0x0003, but return the live value at offset 3 in a read beginning at 0.
    The latter is also the value shown by mbpoll's successful block diagnostic.
    """
    return drive.read(0x0000, 4)[3]


def show_status(drive: T6):
    mode = read_control_mode(drive)
    frame, baud, slave = drive.read(REG_COM_FORMAT, 1)[0], drive.read(REG_COM_BAUD, 1)[0], drive.read(REG_SLAVE_ID, 1)[0]
    pr_state = drive.read(REG_PR_CONTROL)[0]
    command_position = signed32(*drive.read(REG_CMD_POSITION, 2))
    motor_position = signed32(*drive.read(REG_MOTOR_POSITION, 2))
    print(f"slave={slave}  Pr0.01 control-mode={mode} {'(PR mode)' if mode == 6 else '(NOT PR mode)'}")
    print(f"RS485: format={frame} (5 means 8N2), baud-code={baud} (4 means 38400)")
    print(f"Pr8.02=0x{pr_state:04X}; command={command_position} pulses; motor={motor_position} pulses")
    print(f"motor position: {motor_position / PULSES_PER_REV:.4f} motor revolutions")


def require_motion_confirmation(args):
    if not args.confirm_motion:
        raise ModbusError("refusing motion: pass --confirm-motion after checking guards, limits, and clearance")
    if not args.assume_homed:
        raise ModbusError("refusing motion: pass --assume-homed only after establishing a safe machine zero")


def require_pr_mode(drive: T6):
    mode = read_control_mode(drive)
    if mode != 6:
        raise ModbusError(
            f"Pr0.01 is {mode}; the 0x2009/0x6002 PR-mode commands do not exist until "
            "Pr0.01 is set to 6 and the drive is power-cycled"
        )


def stop_and_disable(drive: T6):
    # The T6 manual defines 0x0040 at Pr8.02 as PR-mode E-stop.
    try:
        drive.write(REG_PR_CONTROL, 0x0040)
    finally:
        drive.write(REG_SERVO_ENABLE, 0)


def jog_relative(drive: T6, args):
    require_motion_confirmation(args)
    require_pr_mode(drive)
    if not 1 <= abs(args.pulses) <= args.max_pulses:
        raise ModbusError(f"pulses must be 1..{args.max_pulses} in either direction")
    if not 1 <= args.rpm <= args.max_rpm:
        raise ModbusError(f"rpm must be 1..{args.max_rpm}")

    position = struct.unpack(">HH", struct.pack(">i", args.pulses))
    # Mode 0x0041: position + relative-to-command.  Pr9.07=0x0010 triggers path 0 now.
    path0 = [0x0041, position[0], position[1], args.rpm, args.accel_ms, args.decel_ms, 0, 0x0010]
    print(f"Moving {args.pulses} pulses ({args.pulses / PULSES_PER_REV:.4f} rev) at <= {args.rpm} rpm.")
    if args.servo_already_enabled:
        print("Using the existing hardware/internal servo-enable; 0x2009 is not written.")
    else:
        drive.write(REG_SERVO_ENABLE, 1)
    completed = False
    try:
        drive.write_many(REG_PATH0, path0)
        deadline = time.monotonic() + args.move_timeout
        while time.monotonic() < deadline:
            state = drive.read(REG_PR_CONTROL)[0]
            print(f"  Pr8.02=0x{state:04X}")
            # Manual: 0x000P means finished; 0x10P/0x20P indicate activity.
            if (state & 0xF000) == 0:
                print("Move completed.")
                completed = True
                return
            time.sleep(0.1)
        raise ModbusError("move timeout")
    finally:
        if completed:
            if args.servo_already_enabled:
                print("Servo remains enabled; use the hardware servo-enable or safety circuit to disable it.")
            else:
                try:
                    drive.write(REG_SERVO_ENABLE, 0)
                    print("Servo disabled.")
                except ModbusError as exc:
                    print(f"WARNING: move completed, but communication disable was rejected: {exc}", file=sys.stderr)
        else:
            # Never conceal the original movement/timeout error with a cleanup error.
            try:
                drive.write(REG_PR_CONTROL, 0x0040)
                print("PR E-stop command accepted.", file=sys.stderr)
            except ModbusError as exc:
                print(f"WARNING: PR E-stop was rejected: {exc}", file=sys.stderr)
            if not args.servo_already_enabled:
                try:
                    drive.write(REG_SERVO_ENABLE, 0)
                except ModbusError as exc:
                    print(f"WARNING: communication disable was rejected: {exc}", file=sys.stderr)


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="/dev/ttyUSB0")
    p.add_argument("--slave", type=int, default=17)
    p.add_argument("--baud", type=int, default=38400)
    p.add_argument("--timeout", type=float, default=1.0)
    p.add_argument("--max-pulses", type=int, default=100, help="hard movement limit; default 0.01 motor rev")
    p.add_argument("--max-rpm", type=int, default=100, help="hard speed limit; default 100 rpm")
    sub = p.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="read configuration, PR state and position")
    sub.add_parser("disable", help="disable the servo through register 0x2009")
    sub.add_parser("estop", help="write PR E-stop then disable servo")
    jog = sub.add_parser("jog", help="one bounded relative PR-mode move, then disable")
    jog.add_argument("pulses", type=int, help="signed PR pulses; 10000 pulses = one motor revolution")
    jog.add_argument("--rpm", type=int, default=30)
    jog.add_argument("--accel-ms", type=int, default=500, help="ms / 1000 rpm")
    jog.add_argument("--decel-ms", type=int, default=500, help="ms / 1000 rpm")
    jog.add_argument("--move-timeout", type=float, default=10.0)
    jog.add_argument("--assume-homed", action="store_true")
    jog.add_argument("--confirm-motion", action="store_true")
    jog.add_argument("--servo-already-enabled", action="store_true",
                     help="skip unsupported 0x2009; only when Pr4.00/hardware already enables the servo")
    return p


def main():
    args = parser().parse_args()
    if not 1 <= args.slave <= 31:
        parser().error("--slave must be 1..31 for this T6 RS485 interface")
    drive = None
    try:
        drive = T6(args)
        if args.command == "status":
            show_status(drive)
        elif args.command == "disable":
            require_pr_mode(drive)
            drive.write(REG_SERVO_ENABLE, 0)
            print("Servo disable command accepted.")
        elif args.command == "estop":
            require_pr_mode(drive)
            stop_and_disable(drive)
            print("PR E-stop and servo-disable commands accepted.")
        else:
            jog_relative(drive, args)
    except (serial.SerialException, ModbusError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(2)
    finally:
        if drive:
            drive.close()


if __name__ == "__main__":
    main()
