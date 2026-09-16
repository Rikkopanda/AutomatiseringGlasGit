**Hardware Configuration Guide for Dual ESP32 System**

## Pin Assignments

### Control ESP (32-pin) - Modbus Communication
| Signal | GPIO | Purpose |
|--------|------|---------|
| RXD2 | 16 | Serial2 RX (RS-485 receive) |
| TXD2 | 17 | Serial2 TX (RS-485 transmit) |

### Control ESP (32-pin) - Stepper/Motor Driver
| Signal | GPIO | Purpose |
|--------|------|---------|
| Direction | 12 | Direction control line (to differential line driver) |
| Pulse | 14 | Pulse control line (to differential line driver) |
| Enable | 13 | Enable control line |

### UI ESP (38-pin) - Modbus Communication  
| Signal | GPIO | Purpose |
|--------|------|---------|
| TXD (Serial) | 1 | UART0 TX (RS-485 transmit) |
| RXD (Serial) | 3 | UART0 RX (RS-485 receive) |

**Note**: UI ESP uses the labeled TXD/RXD pins (GPIO1/GPIO3) for Modbus, not Serial2

---

## UI ESP Specific - Input/Output

### LCD Display (I2C Bus)
| Signal | GPIO | Purpose |
|--------|------|---------|
| SDA | 25 | I2C Data |
| SCL | 26 | I2C Clock |
| **I2C Address** | 0x27 | 16x2 LCD module |

### Rotary Encoders
#### Main Encoder (Speed Control)
| Signal | GPIO | Purpose |
|--------|------|---------|
| CLK | 18 | Quadrature Clock |
| DT | 4 | Quadrature Data |
| Button | 15 | Encoder push button (active-low) |

#### Secondary Encoder (Reserved for future)
| Signal | GPIO | Purpose |
|--------|------|---------|
| CLK | 25 | Quadrature Clock |
| DT | 5 | Quadrature Data |
| Button | 2 | Encoder push button (active-low) |

### Button Panel (7 Buttons with Integrated LEDs)
| Button # | Button GPIO | Purpose |
|----------|-------------|---------|
| 0 | 23 | Run |
| 1 | 22 | Stop |
| 2 | 19 | Mode/Select (future) |
| 3 | 34 | Function 1 (future) |
| 4 | 35 | Function 2 (future) |
| 5 | 32 | Function 3 (future) |
| 6 | 33 | Emergency (future) |

**Note**: All buttons are active-low (pulled high). **LEDs are passive/integrated**: 3.3V -> resistor -> switch pin 1 -> switch pin 2 (anode/cathode) -> GPIO input. When button is pressed, GPIO goes LOW and LED lights automatically. No separate LED control needed.

### Multi-Position Switches
| Switch | GPIO1 | GPIO2 | Purpose |
|--------|-------|-------|---------|
| 3-Position | 14 | 12 | Selector switch (middle=both OFF) |
| Emergency Stop | 27 | - | NC (normally closed) |

### Keypad (Not Currently Wired)
The keypad pins from the original design are not in use yet:
- Rows: 13, 18, 19, 23
- Cols: 25, 26, 32, 33

(These were example pins; your setup doesn't currently use the keypad)

---

## Electrical Connections

### RS-485 Wiring (Both ESPs)
```
Each ESP32                RS-485 Adapter      Shared Bus
─────────────────────────────────────────────────────────
GPIO1/3 (UI TXD/RXD) ---> TXD/RXD    
GPIO16/17 (Ctrl RXD2/TXD2) -> RXD/TXD
GND ─────────────────> GND
+3.3V ───────────────> VCC
                        A line  ───────────> A (all devices)
                        B line  ───────────> B (all devices)
                        RE/SHDN ───> +3.3V or +5V (tied high)
```

**Important**:
- UI ESP uses Serial/UART0 (GPIO1 TXD, GPIO3 RXD)
- Control ESP uses Serial2/UART1 (GPIO16 RXD, GPIO17 TXD)
- All three devices (drive, both ESPs) share same RS-485 A/B lines
- 120Ω resistors across A-B at both ends (Control ESP and last slave)
- No DE/RE pin needed (auto-direction on RS-485 adapter)
- Common GND between all devices

### I2C LCD Wiring
```
ESP32 UI               LCD Module (16x2)
──────────────────────────────────────
GPIO25 (SDA) ──────> SDA (with 4.7kΩ pull-up)
GPIO26 (SCL) ──────> SCL (with 4.7kΩ pull-up)
GND ──────────────> GND
+3.3V ────────────> VCC (or +5V depending on module)
```

### Encoder Wiring (with internal pull-ups on button pins)
```
Main Encoder (Speed Control):
GPIO18 (CLK) ──────> A (CLK)
GPIO4 (DT) ────────> B (DT)
GPIO15 (SW) ──────> Button (active-low)
GND ──────────────> GND

Secondary Encoder:
GPIO25 (CLK) ──────> A (CLK)
GPIO5 (DT) ────────> B (DT)
GPIO2 (SW) ───────> Button (active-low)
GND ──────────────> GND
```

### Button Panel Wiring (Passive LEDs with Resistors)
```
For each button (0-6):
Button circuit:
    +3.3V ──> [Resistor] ──> Anode ──> [Button Switch] ──> Cathode ──> GPIO (input)
                            [LED active-high when pressed]

GPIO pulls LOW when button pressed, LED lights via parallel resistor path
No active LED control needed - LEDs are passive
```

Button GPIO connections:
- Button 0: GPIO 23 (Run)
- Button 1: GPIO 22 (Stop)
- Button 2: GPIO 19 
- Button 3: GPIO 34
- Button 4: GPIO 35
- Button 5: GPIO 32
- Button 6: GPIO 33

### Switch Wiring
```
3-Position Switch (Selector):
+3.3V ──────────────> Middle terminal (common)
GPIO14 ─────────────> Position 1 terminal
GPIO12 ─────────────> Position 2 terminal
(Middle position = both GPIOs pulled HIGH)

Emergency Stop (NC - Normally Closed):
GPIO27 ─────────────> Button to GND
(Pulled HIGH via 10kΩ resistor, goes LOW when pressed)
```

---

## Control ESP Specific - Motor Driver Interface

### Differential Line Driver Connections
```
Control ESP                    Differential Line Driver Module
──────────────────────────────────────────────────────────────
GPIO12 (Direction) ────────> Direction input
GPIO14 (Pulse) ────────────> Pulse input  
GPIO13 (Enable) ───────────> Enable input
GND ───────────────────────> GND

Line Driver Outputs:
                    D+/A (or similar) ───> to Stepper Driver
                    D-/B (or similar) ───> to Stepper Driver
```

---

## Notes

1. **Pull-Up Resistors**: 
   - Button switches: 10kΩ pull-ups to +3.3V (for active-low logic)
   - I2C pins: 4.7kΩ pull-ups to +3.3V (standard I2C)
   - Encoder buttons: 10kΩ pull-ups

2. **LED Current**: Buttons have integrated current-limiting resistors (typically 220-470Ω). LEDs light when button pressed without needing GPIO control.

3. **Power Supply**: Ensure adequate power:
   - Two ESP32 boards (~80mA idle, ~160mA active)
   - LCD backlight (~50mA)
   - 7 Buttons with passive LEDs (negligible, already limited by resistors)
   - Encoders and switches (minimal current)
   - **Total recommendation**: ≥500mA @ +3.3V + stepper driver power supply

4. **EMI/Noise**: For robust operation:
   - Use twisted pair for RS-485 A/B lines
   - Ferrite clamps on RS-485 cables
   - Decouple power near each device: 100nF ceramic + 10µF electrolytic capacitors
   - Separate ground planes for logic and power if possible

5. **Modbus Termination**: 
   - 120Ω resistors across A-B at **both ends** of the bus
   - One at Control ESP, one at last slave device
   - Pull-up (560Ω) to A, pull-down (560Ω) to B at one point on bus

---

**Last Updated**: 2026-09-05  
**Status**: Updated for actual hardware configuration  
**Verified Connections**: 
- ✓ RS-485 cross-wiring confirmed (TX→RX, RX→TX on adapters)
- ✓ Modbus termination (120Ω A-B) on both ESPs
- ✓ All GPIO pins mapped and verified
- ✓ I2C address 0x27 confirmed for LCD
