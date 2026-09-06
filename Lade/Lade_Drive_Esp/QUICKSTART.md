**QUICKSTART: Building and Deploying the Dual ESP32 System**

## Prerequisites

- PlatformIO CLI installed
- Two USB cables for programming/monitoring
- Two ESP32 boards (DOIT DevKit v1 or compatible)
- RS-485 adapter with auto-direction control (e.g., MS2548)
- Hardware connections complete (see `HARDWARE_CONFIG.md`)

## Building Both Firmware Images

### Option 1: Build and Upload Both Together

From project root:
```bash
# Build Control ESP
cd control-esp
platformio run

# Build UI ESP  
cd ../ui-esp
platformio run
```

### Option 2: Using PlatformIO GUI
1. Open project in VS Code with PlatformIO extension
2. Left sidebar: Select "control-esp/platformio.ini" → Build
3. Left sidebar: Select "ui-esp/platformio.ini" → Build

## Programming the Boards

### Find USB Port Names
```bash
# Linux/Mac
ls -la /dev/ttyUSB* /dev/ttyACM*

# Windows (COM port number)
# Usually COM3, COM4, etc.
```

### Upload Control ESP
```bash
cd control-esp

# Automatically detect port and upload
platformio run -t upload

# OR specify port explicitly
platformio run -t upload --upload-port /dev/ttyUSB0
```

### Upload UI ESP
```bash
cd ../ui-esp

# Upload to second port
platformio run -t upload --upload-port /dev/ttyUSB1
```

## Monitoring Serial Output

### Control ESP Serial Monitor
```bash
cd control-esp
platformio device monitor --port /dev/ttyUSB0 --baud 115200
```

### UI ESP Serial Monitor
```bash
cd ui-esp
platformio device monitor --port /dev/ttyUSB1 --baud 115200
```

Expected startup output:

**Control ESP:**
```
========================================
  CONTROL ESP32 (MASTER)
  Version: 1.0
========================================

[ModbusMaster] Initialized on Serial2
[DriverControl] Initialized
Setup complete. Waiting for drive...
```

**UI ESP:**
```
========================================
  UI ESP32 (SLAVE)
  Version: 1.0
========================================

Initializing devices...
[LCD] Initialized at 0x27
[Encoder] Initialized
[Button] Initialized 7 buttons with LEDs
[Keypad] Initialized 4x4 keypad
[ModbusSlave] Initialized as slave ID 2
Setup complete!
```

## Initial Testing Checklist

- [ ] Both ESPs boot without errors
- [ ] LCD displays on UI ESP
- [ ] Rotary encoder responds (Serial output shows encoder values)
- [ ] Buttons and LEDs respond (press button 0 to toggle LED)
- [ ] Keypad responds (press keys, check serial output)
- [ ] Modbus communication established (Control ESP polls UI ESP)
- [ ] Drive is detected on Modbus bus

## Troubleshooting

### No Serial Output
- Check USB cable connection
- Verify correct COM/TTY port
- Try different baud rates (9600, 115200)

### Modbus Communication Errors
- Verify RS-485 wiring (A, B, GND)
- Check termination resistors (120Ω at bus ends)
- Ensure all three devices (drive, control ESP, UI ESP) share the same ground

### LCD Not Showing
- Verify I2C address with: `platformio run -t upload --upload-port /dev/ttyUSB1 --build-flag -DLCD_DEBUG`
- Check SDA/SCL connections (GPIO 21/22)
- Contrast potentiometer on LCD module may need adjustment

### Encoder Not Responding
- Verify GPIO pin assignments in `rotary_encoder.cpp`
- Test with simple GPIO read/write to confirm pin connectivity
- Check for interference on encoder signal lines

### Buttons/LEDs Not Working
- Verify button and LED GPIO pins in `button_handler.cpp`
- Test LED by forcing: `digitalWrite(LED_PIN, HIGH)` in code
- Check pull-up resistors on button inputs

## Configuration Adjustments

### Change Speed Range
Edit `shared/modbus_common.h`:
```c
#define ENCODER_MIN_SPEED  0
#define ENCODER_MAX_SPEED  5000   // Adjust to your motor
#define ENCODER_STEP       10     // RPM per encoder pulse
```

### Change LCD Update Frequency
Edit `shared/modbus_common.h`:
```c
#define LCD_UPDATE_MS  500   // Update every 500ms instead of 300ms
```

### Change Modbus Polling Interval
Edit `shared/modbus_common.h`:
```c
#define MODBUS_POLL_INTERVAL_MS  150   // Poll every 150ms
```

### Change Button Debounce Time
Edit `shared/modbus_common.h`:
```c
#define BUTTON_DEBOUNCE_MS  100   // Longer debounce for noisy environment
```

## Next Steps

1. **Verify all I/O works** using the troubleshooting checklist
2. **Test Modbus communication** by monitoring Control ESP serial output
3. **Connect the servo drive** once Modbus communication is stable
4. **Calibrate encoders and buttons** by adjusting GPIO pins if needed
5. **Customize UI logic** in `ui_main.cpp` and `control_main.cpp`
6. **Add new features** using the modular structure (e.g., additional buttons, encoders)

## Development Tips

### Adding a New Button Function
1. Define handler in `ui_main.cpp` handleButtons()
2. Set button LED using `ledSet()`, `ledBlink()`, or `ledPulse()`
3. Set speed or run command using `modbusSlaveSetSetpoint()`, `modbusSlaveSetRun()`

### Adding Custom Speed Logic
1. Modify speed calculation in `ui_main.cpp` handleEncoder()
2. Or implement via keypad numeric entry in handleKeypad()

### Debugging Modbus Issues
- Enable verbose logging in `modbus_master.cpp` and `modbus_slave.cpp`
- Add Serial.printf() statements to track register values
- Use oscilloscope on RS-485 A/B lines to verify signal integrity

---

**Version**: 1.0  
**Last Updated**: 2026-09-05  
**Questions or Issues?** Check `README_NEW_ARCHITECTURE.md` for detailed documentation
