/**
 * @file modbus_common.h
 * @brief Shared Modbus configuration and register map for dual-ESP32 system
 * 
 * This file defines common constants, register addresses, and communication
 * parameters shared between the Control ESP (Master) and UI ESP (Slave).
 */

#ifndef MODBUS_COMMON_H
#define MODBUS_COMMON_H

#include <stdint.h>

// ============================================================================
// MODBUS RS-485 COMMUNICATION PINS & SETTINGS
// ============================================================================
// Control ESP: Uses UART1 (GPIO 16/17)
#define CONTROL_RXD  16  // Serial2
#define CONTROL_TXD  17  // Serial2

// UI ESP: Uses Serial2 (UART2) on GPIO 17 (RX) and GPIO 16 (TX)
#define UI_SERIAL_RXD  16   // GPIO 17 (RX from module TX)
#define UI_SERIAL_TXD  17   // GPIO 16 (TX to module RX)

#define MODBUS_BAUDRATE   9600
#define MODBUS_SERIAL_CFG SERIAL_8N2  // 8 bits, no parity, 2 stop bits

// ============================================================================
// MODBUS SLAVE ADDRESSES
// ============================================================================
#define CONTROL_ESP_SLAVE_ID  1  // Control ESP (master role, but also slave for future)
#define UI_ESP_SLAVE_ID       2  // UI ESP (slave)
#define DRIVE_SLAVE_ID        1  // Servo drive (on same Modbus bus)

// ============================================================================
// UI ESP HOLDING REGISTERS (0 = Control ESP master writes, UI ESP responds)
// ============================================================================
// Commands FROM control ESP TO UI ESP
#define UI_REG_SETPOINT       0  // Speed setpoint in r/min (0..3000 or custom range)
#define UI_REG_RUN            1  // Run command: 0 = stop, 1 = run
#define UI_REG_ACCEL          3  // Acceleration setting (steps/s^2 or RPM/s)
#define UI_REG_CONTROL_MODE   2  // Control mode flags (reserved for future)

// Status FROM control ESP TO UI ESP (echoed back from drive)
#define UI_REG_DRIVE_STATUS   3  // Drive's Dn-18 status bits (bit=0 means ON)
                                  // bit0: Alarm, bit1: Ready, bit2: Emg, bit3: Preach,
                                  // bit4: Sreach, bit5: Treach, bit6: ZeroSpeed, bit7: Run

// Status FROM UI ESP TO control ESP (UI state feedback)
#define UI_REG_ACTUAL_SPEED   4  // Actual speed displayed on UI (for diagnostics)
#define UI_REG_UI_STATUS      5  // UI status bits (LED states, encoder feedback, etc.)

#define UI_REGISTERS_COUNT    6  // Total number of registers in UI ESP

// ============================================================================
// DRIVE HOLDING REGISTERS (Servo drive Modbus map - for reference)
// ============================================================================
#define REG_PN068_COMM_EN     0x0044  // Comm-control enable (bit0: SON control via comms)
#define REG_PN070_COMM_STATE  0x0046  // Comm-control state (bit0 inverted SON command)
#define REG_PN168_SPD_SRC     0x00A8  // Speed command source (1 = internal presets)
#define REG_PN169_SPEED_SET   0x00A9  // Internal speed preset 1 / live setpoint
#define REG_DN08_ACTUAL_SPEED 0x0178  // Actual motor speed (read-only)
#define REG_DN18_STATUS       0x0182  // Output status bits (read-only)

#define PN070_DEFAULT         32691   // 0x7FB3 = servo OFF at startup

// ============================================================================
// UI ESP COMPONENT PINS
// ============================================================================
// LCD Display (I2C)
#define LCD_I2C_ADDR    0x27
#define LCD_SDA_PIN     21
#define LCD_SCL_PIN     22
#define LCD_COLS        16
#define LCD_ROWS        2
#define LCD_UPDATE_MS   300

// Rotary Encoders (reassigned away from GPIO 16/17 which are dedicated to Modbus Serial2)
#define ENCODER1_CLK    18   // Quadrature Clock (moved from 17)
#define ENCODER1_DT     4    // Quadrature Data
#define ENCODER1_SW     15   // Push button (active-low)

#define ENCODER2_CLK    25   // Quadrature Clock (moved from 16)
#define ENCODER2_DT     5    // Quadrature Data (moved from 18)
#define ENCODER2_SW     2    // Push button (active-low) (moved from 5)

// Button Inputs (7 buttons, LEDs are passive/integrated)
#define BUTTON_GPIO_0   23
#define BUTTON_GPIO_1   26
#define BUTTON_GPIO_2   19
#define BUTTON_GPIO_3   34
#define BUTTON_GPIO_4   35
#define BUTTON_GPIO_5   32
#define BUTTON_GPIO_6   33

// Switches
#define SWITCH_3POS_PIN1   14   // 3-position switch (middle=off)
#define SWITCH_3POS_PIN2   12
#define EMERGENCY_STOP_PIN 27   // NC (normally closed)

// Rotary encoder ranges
#define ENCODER_MIN_SPEED     0
#define ENCODER_MAX_SPEED     3000   // Match to your motor/drive
#define ENCODER_STEP          10     // RPM increment per encoder pulse

#define UI_REG_SETPOINT       0  // Speed setpoint in r/min (0..3000 or custom range)
#define UI_REG_RUN            1  // Run command: 0 = stop, 1 = run

// ============================================================================
// CONTROL ESP COMPONENT PINS
// ============================================================================
// Stepper/Motor Driver pins
#define CONTROL_DIRECTION_PIN  12  // Direction line
#define CONTROL_PULSE_PIN      14  // Pulse line
#define CONTROL_ENABLE_PIN     13  // Enable line

// ============================================================================
// TIMING CONSTANTS
// ============================================================================
#define MODBUS_POLL_INTERVAL_MS      100   // Master polls UI ESP every 100ms
#define MODBUS_DRIVE_POLL_INTERVAL_MS 200  // Master polls drive every 200ms
#define BUTTON_DEBOUNCE_MS           50
#define ENCODER_DEBOUNCE_MS          20
#define SWITCH_DEBOUNCE_MS           100

#endif // MODBUS_COMMON_H
