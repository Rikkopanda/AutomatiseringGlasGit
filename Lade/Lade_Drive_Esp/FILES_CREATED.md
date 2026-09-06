# New Project Structure Summary

## Overview
Complete reorganization of the glass handling automation project into a modern dual-ESP32 Modbus system:

- **Control ESP (Master)**: Servo drive control and coordination
- **UI ESP (Slave)**: LCD, rotary encoders, buttons with LEDs, keypad
- **Shared Library**: Common Modbus configuration and constants

## Files Created

### Shared Library
```
shared/
└── modbus_common.h          # Modbus register map, timing constants, pin definitions
```

### Control ESP (Master) 
```
control-esp/
├── platformio.ini           # Build configuration for Control ESP
├── include/
│   ├── modbus_master.h      # Modbus master communication API
│   └── driver_control.h     # Servo drive control API
├── src/
│   ├── control_main.cpp     # Main application loop
│   ├── modbus_master.cpp    # Modbus master implementation
│   └── driver_control.cpp   # Driver control implementation
└── lib/
    └── README.md            # Library management guide
```

### UI ESP (Slave)
```
ui-esp/
├── platformio.ini           # Build configuration for UI ESP
├── include/
│   ├── modbus_slave.h       # Modbus slave communication API
│   ├── lcd_display.h        # LCD display management API
│   ├── rotary_encoder.h     # Rotary encoder input API
│   ├── button_handler.h     # Button and LED control API
│   └── keypad_handler.h     # Membrane keypad API
├── src/
│   ├── ui_main.cpp          # Main application loop
│   ├── modbus_slave.cpp     # Modbus slave implementation
│   ├── lcd_display.cpp      # LCD display implementation
│   ├── rotary_encoder.cpp   # Rotary encoder implementation
│   ├── button_handler.cpp   # Button and LED implementation
│   └── keypad_handler.cpp   # Keypad implementation
└── lib/
    └── README.md            # Library management guide
```

### Documentation
```
├── README_NEW_ARCHITECTURE.md   # Detailed architecture guide
├── HARDWARE_CONFIG.md           # Pin assignments and wiring guide
├── QUICKSTART.md                # Getting started guide
└── FILES_CREATED.md             # This file
```

## Old Files (Preserved)
```
src/                    # Original monolithic implementation
├── main.cpp
├── master.cpp
├── ui.cpp
├── modbus_network_test.cpp
└── find_i2c_address.cpp

platformio.ini         # Original config
CMakeLists.txt         # Original CMake config
```

## Key Design Principles

1. **Separation of Concerns**: Control logic on one ESP, UI on another
2. **Modular Architecture**: Each component has clear header/implementation
3. **Shared Constants**: All configuration in one place (`modbus_common.h`)
4. **Non-blocking Communication**: Modbus master polls, slave responds
5. **Clean Interfaces**: Well-documented public APIs in header files

## Build System

Each ESP has its own `platformio.ini`:
- **control-esp/platformio.ini**: Builds only Control ESP firmware
- **ui-esp/platformio.ini**: Builds only UI ESP firmware
- Both share the `shared/` library path

## Register Map

### UI ESP Holding Registers (Modbus ID = 2)
| Reg | Name | Direction | Purpose |
|-----|------|-----------|---------|
| 0 | SETPOINT | ← Master | Speed setpoint (r/min) |
| 1 | RUN | ← Master | Run command (0/1) |
| 2 | CONTROL_MODE | ← Master | Control flags (reserved) |
| 3 | DRIVE_STATUS | → Master | Drive status bits |
| 4 | ACTUAL_SPEED | → Master | Feedback speed |
| 5 | UI_STATUS | ← Master | UI status bits |

## Quick Links

- **Getting Started**: See `QUICKSTART.md`
- **Hardware Setup**: See `HARDWARE_CONFIG.md`  
- **Architecture Details**: See `README_NEW_ARCHITECTURE.md`
- **Modbus Details**: See `control-esp/src/modbus_master.cpp` header comments

## Testing Sequence

1. Build both firmwares
2. Upload to two ESP32 boards
3. Monitor both serial outputs
4. Verify Modbus communication (check master/slave messages)
5. Test individual I/O (buttons, encoders, LCD, keypad)
6. Connect servo drive
7. Test speed control end-to-end

## File Statistics

| Category | Count | Total Lines |
|----------|-------|-------------|
| Header Files | 8 | ~800 |
| Implementation Files | 10 | ~1,200 |
| Documentation | 4 | ~600 |
| Config Files | 2 | ~50 |
| **Total** | **24** | **~2,650** |

## Next Steps

1. ✅ **Code structure created** (this phase)
2. ⚙️ Adjust hardware pins if needed (see `HARDWARE_CONFIG.md`)
3. ⚙️ Compile and upload both firmwares
4. ⚙️ Test I/O and Modbus communication
5. ⚙️ Connect servo drive and test end-to-end
6. ⚙️ Add custom features (additional buttons, modes, etc.)

## Support for Future Development

The modular structure makes it easy to:
- Add new input devices (just create new handler in ui-esp/)
- Add new control features (extend driver_control.cpp)
- Add new Modbus registers (update modbus_common.h)
- Debug individual components (each has clear interfaces)
- Reuse code (shared library + headers with implementations)

---

**Version**: 1.0  
**Created**: 2026-09-05  
**Status**: Ready for compilation and testing
