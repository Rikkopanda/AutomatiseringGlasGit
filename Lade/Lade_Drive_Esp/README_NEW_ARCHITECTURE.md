# Dual ESP32 Modbus Control System

A modern redesign of the glass handling automation system using two specialized ESP32 microcontrollers:
- **Control ESP** (Master): Drives the servo motor via Modbus RTU
- **UI ESP** (Slave): Handles all user interface elements

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         RS-485 Modbus Bus (9600, 8N2)           │
├──────────────┬──────────────────────────────┬───────────────────┤
│              │                              │                   │
│  Control ESP │          Servo Drive         │    UI ESP         │
│   (Master)   │         (Slave ID=1)         │   (Slave ID=2)    │
│              │                              │                   │
│ • Modbus RTU │ • Speed control              │ • LCD Display     │
│   Master     │ • Status feedback            │ • Rotary Encoders │
│ • Driver Ctrl│ • Motor enable/disable       │ • 7 Buttons + LEDs│
│ • Logic/Seq  │ • Speed setpoint             │ • 4x4 Keypad      │
│              │                              │ • Modbus RTU Slave│
└──────────────┴──────────────────────────────┴───────────────────┘
```

## Directory Structure

```
Lade_Drive_Esp/
├── shared/                    # Shared code between both ESPs
│   └── modbus_common.h       # Register map, constants, timing
│
├── control-esp/              # Control ESP32 (Master) firmware
│   ├── platformio.ini
│   ├── include/
│   │   ├── modbus_master.h   # Modbus master interface
│   │   └── driver_control.h  # Servo drive control logic
│   └── src/
│       ├── control_main.cpp  # Main application
│       ├── modbus_master.cpp # Modbus master implementation
│       └── driver_control.cpp# Driver control implementation
│
├── ui-esp/                   # UI ESP32 (Slave) firmware
│   ├── platformio.ini
│   ├── include/
│   │   ├── modbus_slave.h    # Modbus slave interface
│   │   ├── lcd_display.h     # LCD display management
│   │   ├── rotary_encoder.h  # Rotary encoder handling
│   │   ├── button_handler.h  # Button and LED control
│   │   └── keypad_handler.h  # Membrane keypad
│   └── src/
│       ├── ui_main.cpp       # Main application
│       ├── modbus_slave.cpp  # Modbus slave implementation
│       ├── lcd_display.cpp   # LCD display implementation
│       ├── rotary_encoder.cpp# Rotary encoder implementation
│       ├── button_handler.cpp# Button and LED implementation
│       └── keypad_handler.cpp# Keypad implementation
│
├── src/                      # OLD implementation (legacy)
│   ├── main.cpp             
│   ├── master.cpp
│   ├── ui.cpp
│   └── ...
│
└── [old config files and test files remain unchanged]
```

## Hardware Configuration

### Control ESP (Master)
- **Serial2**: RXD2=GPIO16, TXD2=GPIO17 (RS-485 bus)
- Modbus Master at 9600 baud, 8N2 format
- Communicates with:
  - Servo drive (Modbus Slave ID=1)
  - UI ESP (Modbus Slave ID=2)

### UI ESP (Slave)
- **Serial2**: RXD2=GPIO16, TXD2=GPIO17 (RS-485 bus)
- Modbus Slave ID=2 at 9600 baud, 8N2 format
- **I2C Display**: SDA=GPIO21, SCL=GPIO22
  - 16x2 LCD at I2C address 0x27
- **Rotary Encoders**:
  - Main encoder: pinA=19, pinB=18, button=5
  - Secondary: pinA=17, pinB=16, button=4
- **Buttons with LEDs** (7 pairs):
  - Button pins: 32, 33, 34, 35, 36, 37, 38
  - LED pins: 25, 26, 27, 12, 13, 14, 15
- **Keypad** (4x4 membrane):
  - Rows: 13, 18, 19, 23
  - Cols: 25, 26, 32, 33

## Modbus Communication

### Register Map (UI ESP Slave)

| Register | ID | Direction | Purpose |
|----------|----|-----------|-|
| 0 | SETPOINT | R (Master reads) | Speed setpoint from UI (r/min) |
| 1 | RUN | R (Master reads) | Run command (0=stop, 1=run) |
| 2 | CONTROL_MODE | R | Control mode flags (reserved) |
| 3 | DRIVE_STATUS | W (Master writes) | Drive status bits from control ESP |
| 4 | ACTUAL_SPEED | W (Master writes) | Actual speed feedback |
| 5 | UI_STATUS | R (Master reads) | UI status bits (LED states, etc.) |

### Register Map (Drive - Reference Only)

The drive's register map is documented in `control-esp/src/driver_control.cpp`. Key registers:
- **Pn169 (0x00A9)**: Speed setpoint (-5000..5000 r/min)
- **Dn-08 (0x0178)**: Actual motor speed (read-only)
- **Dn-18 (0x0182)**: Status bits (Alarm, Ready, Run, etc.)

## Building & Uploading

### Control ESP
```bash
cd control-esp
platformio run --environment env -t upload --upload-port /dev/ttyUSB0
```

### UI ESP
```bash
cd ui-esp
platformio run --environment env -t upload --upload-port /dev/ttyUSB1
```

### Monitor Serial Output
```bash
# Control ESP
platformio device monitor --port /dev/ttyUSB0 --baud 115200

# UI ESP
platformio device monitor --port /dev/ttyUSB1 --baud 115200
```

## Firmware Flow

### Control ESP (Main Loop)
1. Poll UI ESP for user setpoint and run command
2. Poll servo drive for actual speed and status
3. Apply control logic (enable/disable, set speed)
4. Echo drive status back to UI ESP for display

### UI ESP (Main Loop)
1. Scan rotary encoders for speed adjustments
2. Scan button panel for run/stop commands
3. Scan keypad for additional inputs
4. Expose inputs via Modbus for master to read
5. Receive and display drive status on LCD

## Key Features

✓ **Dual-controller design**: Separation of concerns (control vs. UI)
✓ **Modbus RTU communication**: Proven, noise-robust protocol
✓ **Multi-input support**: Rotary encoders, buttons, keypad
✓ **LED feedback**: Button LEDs with pulse/blink effects
✓ **LCD status display**: Real-time motor status and feedback
✓ **Graceful degradation**: UI continues to operate even if control ESP is temporarily unavailable
✓ **Modular code**: Well-organized headers and implementations for easy maintenance

## Testing & Troubleshooting

### Modbus Bus Diagnostics
- Check RS-485 adapter wiring: A, B, GND, VCC
- Verify termination: 120Ω at each end of the bus
- Pull-up/pull-down resistors on A (+) and B (−)
- Serial monitor shows "Modbus error" messages on communication failures

### LED Test
- Use button handler `ledBlink()` and `ledPulse()` functions in ui_main.cpp
- Example: Call `ledBlink(BUTTON_0, 3, 200, 200)` to blink 3 times on startup

### LCD Test
- lcdUpdateSpeed() is called periodically to refresh display
- Ensure I2C address 0x27 is correct for your module

### Encoder Test
- Monitor `encoderGetDelta()` return values in ui_main.cpp
- Print to serial for verification

### Keypad Test
- `keypadPrintMap()` shows the button layout
- Monitor `keypadGetKeyPress()` output

## Notes for Future Development

1. **Button Functions**: Currently buttons 0 (Run) and 1 (Stop) are implemented. Buttons 2-6 are reserved.
2. **Keypad Modes**: Consider implementing numeric entry mode or preset speed selection via keypad
3. **LED Effects**: Extend button_handler with more effects (fade, chase, etc.)
4. **Error Handling**: Add watchdog timers and heartbeat monitoring
5. **Configuration**: Consider storing speed profiles and settings in EEPROM/NVS

## Legacy Files

Old implementation files remain in `src/` for reference:
- `main.cpp` - Original monolithic implementation
- `master.cpp` - Original Modbus master code
- `ui.cpp` - Original UI slave code
- `modbus_network_test.cpp` - Test/diagnostic code

These will not be compiled unless explicitly selected via platformio.ini build filters.

---

**Last Updated**: 2026-09-05
**Status**: New architecture implemented, ready for testing
