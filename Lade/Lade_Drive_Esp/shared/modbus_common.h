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
#define DRIVE_SLAVE_ID        3  // Servo drive (on same Modbus bus)

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
#define UI_REG_RESET_ALARM    6  // UI status bits (LED states, encoder feedback, etc.)

#define UI_STATUS_ESTOP       0x0002  // bit1: 1 = button 7 is 0, motion must stop

#define UI_REGISTERS_COUNT    7  // Total number of registers in UI ESP

// ============================================================================
// DRIVE HOLDING REGISTERS (Servo drive Modbus map - for reference)
// ============================================================================
#define REG_PN068_COMM_EN     0x0044  // Comm-control enable (bit0: SON control via comms)
#define REG_PN070_COMM_STATE  0x0046  // Comm-control state (bit0 inverted SON command)
#define REG_PN168_SPD_SRC     0x00A8  // Speed command source (1 = internal presets)
#define REG_PN169_SPEED_SET   0x00A9  // Internal speed preset 1 / live setpoint
#define REG_DN08_ACTUAL_SPEED 0x0178  // Actual motor speed (read-only)
#define REG_DN18_STATUS       0x0182  // Output status bits (read-only)

// T6 (Stepperonline) uses a different map. Pr5.31 is the RS485 station id.
// This drive is set to 3 (Pr5.31). 0x0178 is not a T6 address.
#define REG_T6_PR529_FRAME    0x053B  // 5 = 8 data, no parity, 2 stop
#define REG_T6_PR530_BAUD     0x053D  // 2 = 9600
#define REG_T6_PR531_SLAVE_ID 0x053F

// PR position mode (not the pulse-input direction in Pr0.06).
#define REG_T6_PR001_MODE     0x0003  // 6 = PR mode. Needs a drive power cycle.
#define REG_T6_PR006_PULSEDIR 0x000D  // pulse-input direction only. Needs a power cycle.
#define REG_T6_PR025_AUX      0x0033  // write 0x2211 to save parameters
#define REG_T6_PR400_SI1      0x0401  // SI1 function. 0x83 = internal servo-on (no wire). Needs power cycle.
#define REG_T6_PR401_SI2      0x0403  // SI2 function (NL = negative limit, POT/NOT logic)
#define REG_T6_PR402_SI3      0x0405  // SI3 function (PL = positive limit)
#define REG_T6_PR403_SI4      0x0407  // SI4 function (E-STOP / STP)
#define REG_T6_PR802_TRIGGER  0x6002  // write 0x0011 to run path 1, 0x0040 to e-stop, 0x0020 to home
#define REG_T6_PATH1_MODE     0x6208
#define REG_T6_PATH1_POS_H    0x6209
#define REG_T6_PATH1_POS_L    0x620A
#define REG_T6_PATH1_SPEED    0x620B  // r/min
#define REG_T6_PATH1_ACCEL    0x620C  // ms per 1000 r/min
#define REG_T6_PATH1_DECEL    0x620D
#define REG_T6_RESET_ALARM    0x0033  // write 0x1111 to reset current alarm
#define VALUE_T6_RESET_ALARM  0x1111

// T6 Motion status (read from REG_T6_PR802_TRIGGER)
// 0x0000 = move/homing done (ready)
// 0x01xx = motor moving (path xx)
// 0x02xx = fine-positioning
// 0x0300 = homing in progress
#define T6_STATUS_DONE        0x0000

// T6 DI function codes for Pr4.00–Pr4.03
// Format: 0xTTFF where TT=type (00=NO, 80=NC), FF=function
#define T6_DI_SRV_ON_NC       0x0083  // SI1: internal servo enable (drive manual: Pr4.00=83)
#define T6_DI_SRV_ON_NO       0x0003  // SI1: external servo enable input (drive manual: Pr4.00=03)
#define T6_DI_NL_NC           0x00A6  // Negative limit (NOT), NC — stops neg motion when open
#define T6_DI_NL_NO           0x0026  // Negative limit (NOT), NO
#define T6_DI_PL_NC           0x00A5  // Positive limit (POT), NC — stops pos motion when open
#define T6_DI_PL_NO           0x0025  // Positive limit (POT), NO
#define T6_DI_ESTOP_NC        0x00A2  // E-STOP (STP), NC — stops drive when open (fail-safe)
#define T6_DI_ESTOP_NO        0x0022  // E-STOP (STP), NO
#define T6_DI_HOME_SENSOR_NO  0x000F  // Home sensor (ORG), NO
#define T6_DI_NONE            0x0000  // Not assigned

// T6 soft-reset: write to Pr0.25 to restart drive logic without power cycle
#define REG_T6_SOFT_RESET     0x0033  // NOTE: same address as PR025_AUX – value distinguishes action
#define VALUE_T6_SOFT_RESET   0x6666  // Triggers internal firmware restart

#define T6_MODE_PR            6
#define T6_SI1_SERVO_ON       T6_DI_SRV_ON_NO
#define T6_AUX_SAVE           0x2211
#define T6_PATH_ABSOLUTE      0x0011  // position type, interrupt the move already running
#define T6_PATH_RELATIVE      0x0041  // position type, relative to current command
#define T6_TRIGGER_PATH1      0x0011
#define T6_TRIGGER_ESTOP      0x0040
#define T6_TRIGGER_HOME       0x0020  // Start homing routine
#define T6_MOVE_SPEED_RPM     60
#define T6_MOVE_MAX_SPEED_RPM 3000
#define T6_MOVE_ACCEL_MS      500
#define T6_MOVE_MAX_PULSES    50000

// T6 Monitor Registers (PR Mode / Internal)
#define REG_T6_CMD_POS_H      0x602A  // Command position high (signed 32-bit)
#define REG_T6_CMD_POS_L      0x602B  // Command position low
#define REG_T6_MOTOR_POS_H    0x602C  // Motor actual position high (signed 32-bit, 10000 pulses = 1 rev)
#define REG_T6_MOTOR_POS_L    0x602D  // Motor actual position low
#define REG_T6_ACTUAL_SPEED   0x0B00  // Actual motor speed in RPM
#define REG_T6_ALARM_CODE     0x0B03  // PrB.03 active alarm/error code (0 = no alarm)
#define REG_T6_DI_STATUS      0x0B11  // PrB.17 physical DI status bits: bit0=SI1..bit3=SI4
#define REG_T6_PR_STATUS      0x602E  // Pr8.46 PR function status (homing, limits, etc.)

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
#define USE_ENABLE_PIN 1    // 1 = physical enable wire used, 0 = no enable pin on drive
#define ENABLE_ACTIVE_LOW 1   // 1: ENABLE low = ON (common), 0: ENABLE high = ON

// ============================================================================
// TIMING CONSTANTS
// ============================================================================
#define MODBUS_POLL_INTERVAL_MS      100   // Master polls UI ESP every 100ms
#define MODBUS_DRIVE_POLL_INTERVAL_MS 200  // Master polls drive every 200ms
#define BUTTON_DEBOUNCE_MS           50
#define ENCODER_DEBOUNCE_MS          20
#define SWITCH_DEBOUNCE_MS           100

#endif // MODBUS_COMMON_H
