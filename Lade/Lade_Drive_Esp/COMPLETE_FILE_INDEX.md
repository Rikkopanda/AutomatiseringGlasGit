# New Architecture - Complete File Index

## Shared Library (for both ESPs)

### `/shared/modbus_common.h`
**Purpose**: Central configuration and register map
- Modbus slave addresses (Control ESP=1, UI ESP=2, Drive=1)
- Serial2 pin definitions (RXD2=16, TXD2=17)
- Register addresses for UI ESP Modbus slave
- Register addresses for servo drive (reference only)
- Timing constants (debounce, polling intervals, update rates)
- Hardware limits (speed range, acceleration range)
- Component-specific constants (LCD address, encoder ranges, button timings)

**Lines**: ~130  
**Usage**: Included by all source files to maintain consistency

---

## Control ESP (Master) - Servo Drive Controller

### Header Files

#### `/control-esp/include/modbus_master.h`
**Purpose**: Modbus RTU master communication interface
**Key Functions**:
- `modbusInit()` - Initialize master on Serial2
- `modbusPollUI()` - Request setpoint/run from UI ESP
- `modbusPollDrive()` - Read speed/status from drive
- `modbusWriteStatusToUI()` - Echo drive status to UI
- `modbusWriteSpeedToDrive()` - Send speed command to drive
- `modbusSetServoEnable()` - Control servo ON/OFF
- `modbusTask()` - Background state machine (call in loop)
- Accessor functions for last-read values

**Lines**: ~95

#### `/control-esp/include/driver_control.h`
**Purpose**: Servo drive control state machine
**Key Functions**:
- `driverInit()` - Initialize drive and Modbus
- `driverTask()` - Main control loop (call in loop)
- `driverSetSpeed()` - Set target speed
- `driverEnable()` / `driverDisable()` - Servo control
- `driverEmergencyStop()` - Immediate shutdown
- Status accessor functions (speed, state, alarm, ready, running)

**Lines**: ~105

### Source Files

#### `/control-esp/src/control_main.cpp`
**Purpose**: Main application loop
**Responsibilities**:
1. Initialize driver and Modbus on startup
2. Poll for user inputs from UI ESP
3. Apply control logic (enable/disable, speed setpoint)
4. Poll drive for feedback
5. Echo status back to UI for display
6. Print diagnostic messages every 2 seconds

**Lines**: ~90
**Loop Rate**: ~100ms per cycle (with 10ms delay)

#### `/control-esp/src/modbus_master.cpp`
**Purpose**: Modbus master implementation
**Key State**:
- Connected/initialized flag
- Last-polled values (setpoint, run, status, actual speed)
- Polling timers and pending flags
- Register buffers for multi-register reads
- Callback functions for async operations

**Lines**: ~130
**Polling Schedule**:
- UI ESP every 100ms
- Drive every 200ms

#### `/control-esp/src/driver_control.cpp`
**Purpose**: Servo drive control implementation
**State Machine**:
1. IDLE - Waiting to start
2. INIT_SEQ - Running initialization sequence
3. READY - Normal operation
4. ERROR - Error condition
5. DISABLED - Servo disabled

**Lines**: ~160
**Features**:
- Soft initialization sequence
- Speed command ramping
- Servo enable/disable via Modbus
- Status bit monitoring (alarm, ready, running)
- Emergency stop capability

---

## UI ESP (Slave) - User Interface Controller

### Header Files

#### `/ui-esp/include/modbus_slave.h`
**Purpose**: Modbus RTU slave communication interface
**Key Functions**:
- `modbusSlaveInit()` - Initialize slave at address 2
- `modbusSlaveSetSetpoint()` - Expose user's speed input
- `modbusSlaveSetRun()` - Expose user's run command
- `modbusSlaveGetDriveStatus()` - Receive status from master
- `modbusSlaveSetUIStatus()` - Report UI state to master
- `modbusSlaveHasActiveMaster()` - Check master communication
- `modbusSlaveTask()` - Background state machine

**Lines**: ~85

#### `/ui-esp/include/lcd_display.h`
**Purpose**: 16x2 I2C LCD display management
**Key Functions**:
- `lcdInit()` - Initialize I2C and LCD
- `lcdUpdateSpeed()` - Update display with speed/status
- `lcdPrintLine0()` / `lcdPrintLine1()` - Direct line output
- `lcdClear()` - Clear display
- `lcdShowEncoderMode()` - Show current mode
- `lcdShowError()` - Display error message
- `lcdIsReady()` - Check if initialized

**Lines**: ~90

#### `/ui-esp/include/rotary_encoder.h`
**Purpose**: Rotary encoder input (2 encoders with buttons)
**Key Functions**:
- `encoderInit()` - Initialize GPIO pins
- `encoderGetDelta()` - Get rotation since last check (+/-1 per pulse)
- `encoderGetPosition()` - Get absolute position
- `encoderReset()` - Reset position counter
- `encoderButtonWasPressed()` - Edge-triggered button detection
- `encoderButtonIsPressed()` - Level-triggered button state
- `encoderGetButtonPressCount()` - Multi-click detection
- `encoderTask()` - Debounce and decode quadrature

**Lines**: ~105

#### `/ui-esp/include/button_handler.h`
**Purpose**: 7 push buttons with integrated LEDs
**Key Functions**:
- `buttonInit()` - Initialize pins
- `buttonWasPressed()` - Edge-triggered detection
- `buttonIsPressed()` - Level-triggered state
- `buttonGetPressCount()` - Multi-click counting
- LED control: `ledSet()`, `ledToggle()`, `ledBlink()`, `ledPulse()`
- `ledStopPulse()` - Stop fade effect
- `buttonTask()` - Debounce and LED effects

**Lines**: ~115

#### `/ui-esp/include/keypad_handler.h`
**Purpose**: 4x4 membrane keypad
**Key Functions**:
- `keypadInit()` - Initialize row/column GPIO
- `keypadGetKeyPress()` - Get pressed key (edge-triggered)
- `keypadGetCurrentKey()` - Get current key (level-triggered)
- `keypadIsKeyPressed()` - Check if any key pressed
- `keypadPrintMap()` - Debug: print key layout
- `keypadTask()` - Scan matrix and debounce

**Lines**: ~90

### Source Files

#### `/ui-esp/src/ui_main.cpp`
**Purpose**: Main application loop and UI logic
**Responsibilities**:
1. Initialize all input devices (LCD, encoders, buttons, keypad)
2. Initialize Modbus slave
3. Main loop:
   - Scan all inputs (encoders, buttons, keypad)
   - Update Modbus registers with user input
   - Receive and display drive status on LCD
   - Handle LED feedback
   - Report diagnostics every 5 seconds

**Lines**: ~175
**Input Handlers**:
- `handleEncoder()` - Adjust speed setpoint, toggle run/stop
- `handleButtons()` - Button-specific actions (run, stop, etc.)
- `handleKeypad()` - Reserved for future numeric/mode input

#### `/ui-esp/src/modbus_slave.cpp`
**Purpose**: Modbus slave implementation
**State**:
- Holding registers (setpoint, run, control mode, drive status, UI status)
- Master contact timeout (5 seconds)
- Register update tracking

**Lines**: ~95
**Features**:
- Responds to master register reads/writes
- Tracks master communication status
- Non-blocking operation

#### `/ui-esp/src/lcd_display.cpp`
**Purpose**: LCD display implementation
**Features**:
- Wire I2C initialization (GPIO 21/22)
- Display updates throttled (300ms minimum interval)
- Line formatting with padding
- Status interpretation (alarm, ready, running)
- Current implementation: 2-line display

**Lines**: ~140

#### `/ui-esp/src/rotary_encoder.cpp`
**Purpose**: Rotary encoder implementation
**Implementation**:
- Gray code quadrature decoding
- Debouncing for encoder button (20ms)
- Position tracking
- Delta accumulation
- Edge and level-triggered button events
- Press counting for multi-click detection

**Lines**: ~180
**Pin Assignments**:
- Main: A=19, B=18, Button=5
- Secondary: A=17, B=16, Button=4

#### `/ui-esp/src/button_handler.cpp`
**Purpose**: Button and LED implementation
**Features**:
- Debouncing (50ms configurable)
- Edge-triggered press detection
- Press counting
- LED control with effects:
  - On/Off
  - Pulse (fade in/out)
  - Blink (N times with on/off timing)
- 7 independent button/LED pairs

**Lines**: ~210
**Pin Assignments**: See button_handler.cpp

#### `/ui-esp/src/keypad_handler.cpp`
**Purpose**: 4x4 membrane keypad scanner
**Implementation**:
- Row-by-row scanning
- Column reading with pull-ups
- Debouncing (20ms configurable)
- Character mapping (0-9, A-D, *, #)
- Edge and level-triggered key detection

**Lines**: ~180
**Pin Assignments**:
- Rows: 13, 18, 19, 23
- Columns: 25, 26, 32, 33

---

## Configuration Files

### `/control-esp/platformio.ini`
**Purpose**: Build configuration for Control ESP
- Platform: espressif32, board: esp32doit-devkit-v1
- Dependencies: modbus-esp8266
- Build filter: Only compiles `control_main.cpp`, `modbus_master.cpp`, `driver_control.cpp`
- Include paths: `-I../../../shared`

### `/ui-esp/platformio.ini`
**Purpose**: Build configuration for UI ESP
- Platform: espressif32, board: esp32doit-devkit-v1
- Dependencies: modbus-esp8266, LiquidCrystal_I2C
- Build filter: Only compiles `ui_main.cpp` and all handler implementations
- Include paths: `-I../../include -I../../../shared`

---

## Documentation Files

### `/README_NEW_ARCHITECTURE.md`
**Content**:
- Architecture overview with diagram
- Directory structure
- Hardware configuration summary
- Modbus register map
- Building and uploading instructions
- Main loop flow descriptions
- Key features and future development notes

### `/HARDWARE_CONFIG.md`
**Content**:
- Detailed pin assignments for both ESPs
- Electrical connections (RS-485, I2C, GPIO)
- Wiring diagrams (textual)
- Pull-up/pull-down requirements
- Power supply recommendations
- EMI/noise mitigation tips

### `/QUICKSTART.md`
**Content**:
- Prerequisites
- Building steps
- Programming instructions (port detection)
- Serial monitoring setup
- Initial testing checklist
- Troubleshooting guide
- Configuration adjustment examples
- Development tips

### `/FILES_CREATED.md` (this file)
**Content**:
- Project summary
- File listing by directory
- Key design principles
- Build system overview
- Register map reference
- File statistics
- Next steps and support notes

---

## Code Organization Summary

```
Total Files Created: 24
├── Headers: 8 (~800 lines)
├── Implementations: 10 (~1,200 lines)
├── Config: 2 (~50 lines)
├── Documentation: 4 (~600 lines)
└── Total Code: ~2,650 lines

By Category:
├── Modbus Communication: 4 files
├── Driver Control: 2 files
├── LCD Display: 2 files
├── Input Handling: 6 files (encoders, buttons, keypad)
├── Configuration: 3 files (shared + 2 platformio.ini)
└── Documentation: 4 files
```

---

## File Sizes

| File | Lines | Purpose |
|------|-------|---------|
| modbus_common.h | 130 | Shared config |
| modbus_master.h | 95 | Control ESP API |
| modbus_master.cpp | 130 | Implementation |
| driver_control.h | 105 | Drive control API |
| driver_control.cpp | 160 | Implementation |
| control_main.cpp | 90 | Main loop |
| modbus_slave.h | 85 | UI ESP Modbus API |
| modbus_slave.cpp | 95 | Implementation |
| lcd_display.h | 90 | LCD API |
| lcd_display.cpp | 140 | Implementation |
| rotary_encoder.h | 105 | Encoder API |
| rotary_encoder.cpp | 180 | Implementation |
| button_handler.h | 115 | Button API |
| button_handler.cpp | 210 | Implementation |
| keypad_handler.h | 90 | Keypad API |
| keypad_handler.cpp | 180 | Implementation |
| ui_main.cpp | 175 | Main loop |
| platformio (×2) | 50 | Build config |
| Docs (×4) | 600 | Documentation |
| **Total** | **~2,650** | |

---

## Usage Patterns

### Control ESP Loop
```cpp
modbusTask();           // Handle Modbus communication
driverTask();           // Update driver state
driverSetSpeed(value);  // Command speed
modbusWriteStatusToUI();// Echo status
```

### UI ESP Loop
```cpp
modbusSlaveTask();      // Handle Modbus communication
encoderTask();          // Scan encoders
buttonTask();           // Scan buttons  
keypadTask();           // Scan keypad
lcdTask();              // Update display
```

---

**Last Updated**: 2026-09-05  
**Status**: Ready for compilation and testing  
**Next Phase**: Hardware verification and debugging
