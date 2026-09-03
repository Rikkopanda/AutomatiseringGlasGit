# T6 bench tool

`t6_pr_mode.py` talks directly to the T6 drive using the Modbus RTU settings
confirmed by the successful `mbpoll` query: slave 17, 38400 baud, 8N2, and
`/dev/ttyUSB0`. It uses only `pyserial`, which is already installed on this
computer.

## Result for the currently connected drive

The connected slave 17 can *read* the PR registers (`0x6000` and `0x6200`),
but rejects both documented write methods to `0x6200` with Modbus exception
`0x02` (illegal data address): single write `0x06` and multiple write `0x10`.
It also rejects the documented communication servo-enable address `0x2009`.
Therefore this particular drive/firmware cannot perform PR-mode motion over
Modbus RTU as described by this manual. Do not use the `jog`, `disable`, or
`estop` commands below against it; they are retained as a diagnostic reference
for a T6 firmware that implements the documented write map. `status` is safe
and read-only.

Read-only check:

```bash
python3 tools/t6_pr_mode.py status
```

Before any motion, configure the drive from its panel/software, then power
cycle it as required by the manual:

1. `Pr0.01 = 6` (PR mode).
2. `Pr4.00 = 0x0083` (shown as `0083` on a hexadecimal panel) for the
   driver's internally enabled / normally-closed servo-enable input, or provide
   a correctly wired external servo-enable input (`0x0003`).
3. Configure and prove the hardware emergency stop and both travel limits.
4. Establish a safe home position. The tool will not perform homing for you.

`Pr0.01` is power-cycle dependent. After changing it to `0006`, turn the
drive's control power fully off and back on, then verify it before proceeding:

```bash
python3 tools/t6_pr_mode.py status
```

It must report `Pr0.01 control-mode=6 (PR mode)`. While it reports `0`, the
T6 correctly rejects the PR servo-enable, disable, e-stop, and motion registers
with Modbus exception `0x02`; no command was carried out. To disable a drive
that is not yet in PR mode, use its panel or the correctly wired hardware
servo-enable/safety circuit.

Some T6 firmware responds incorrectly to a one-register read at `0x0003`.
The tool reads registers `0x0000` through `0x0003` together and uses the last
value, as verified with `mbpoll`; this is the live value of `Pr0.01`.

For a first bench move, make sure the shaft/load is clear, use a very small
number of pulses, and keep the physical e-stop accessible. `10000` PR pulses
are one motor revolution, so `10` is 0.001 motor revolution:

```bash
python3 tools/t6_pr_mode.py --max-pulses 20 --max-rpm 30 jog 10 \
  --rpm 10 --assume-homed --confirm-motion
```

This model rejects the manual's `0x2009` communication servo-enable register,
but it is already servo-enabled by `Pr4.00=0x0083`. Use this variant only when
that hardware/internal enable is intentionally active and the physical safety
circuit can disable it:

```bash
python3 tools/t6_pr_mode.py --max-pulses 20 --max-rpm 30 jog 10 \
  --rpm 10 --assume-homed --confirm-motion --servo-already-enabled
```

The motion command sends a relative PR path and waits up to ten seconds. On a
failure or timeout it requests T6 PR-mode e-stop. With
`--servo-already-enabled`, it leaves the servo enabled after a normal move; use
the hardware servo-enable or safety circuit to disable it. It will not alter
configuration parameters or save anything to EEPROM.

Emergency commands (PR mode only):

```bash
python3 tools/t6_pr_mode.py estop
python3 tools/t6_pr_mode.py disable
```

Do not run `mbpoll`, this tool, or an ESP32 Modbus master simultaneously on the
same RS-485 bus: Modbus RTU permits one master.
