// -------------------------------------------------------------------------------------------------
// Pin map for ESP32-C6 Supermini (OnStep, 2-axis, Arduino ESP32 core v3.x)
//
// Board: "ESP32C6 Dev Module" in Arduino IDE, or esp32:esp32:esp32c6 in PlatformIO.
//
// ESP32-C6 Supermini physical layout (USB-C connector at top):
//
//  Left column  (top→bottom): TX(GPIO16) · RX(GPIO17) · GPIO0 · GPIO1 · GPIO2 · GPIO3
//                              GPIO4 · GPIO5 · GPIO6 · GPIO7 · GPIO23 · GPIO22
//  Right column (top→bottom): 5V · GND · 3.3V · GPIO20 · GPIO19 · GPIO18 · GPIO15
//                              GPIO14 · GPIO9 · GPIO8 · GPIO12 · GPIO13 · GPIO21
//
//  NOT broken out as header pins: GPIO10, GPIO11 (internal chip only)
//
//  Onboard indicators:
//    GPIO15 — status LED (active LOW)
//    GPIO8  — RGB LED (WS2812 or similar)
//  Strapping / special pins:
//    GPIO9  — BOOT button (internal pull-up; LOW during boot); usable as output after boot
//    GPIO12 — USB D-  (do not drive while USB-CDC is active)
//    GPIO13 — USB D+  (do not drive while USB-CDC is active)
//
// Pin assignment summary (step/dir default configuration)
// ────────────────────────────────────────────────────────
//  GPIO  0  AUX7   — Limit SW / PPS input    (ADC-capable)
//  GPIO  1  PEC    — PEC index sense          (ADC-capable)
//  GPIO  2  AUX2   — SPI MISO / TMC UART RX  (shared input)
//  GPIO  3  AUX8   — 1-Wire / Buzzer
//  GPIO  4  Ax1    — AXIS1 STEP  (or ULN2003 IN1 Axis1)
//  GPIO  5  Ax1    — AXIS1 DIR   (or ULN2003 IN2 Axis1)
//  GPIO  6  I2C    — SDA  (AUX3 / Home SW Axis1)  [Wire default on ESP32-C6]
//  GPIO  7  I2C    — SCL  (AUX4 / Home SW Axis2)  [Wire default on ESP32-C6]
//  GPIO  8  —      — RGB LED (onboard); usable as GPIO but LED will respond
//  GPIO  9  —      — BOOT button (strapping); use with care
//  GPIO 10  —      — NOT BROKEN OUT on Supermini header
//  GPIO 11  —      — NOT BROKEN OUT on Supermini header
//  GPIO 12  USB D- — Leave unconnected when USB-CDC serial is in use
//  GPIO 13  USB D+ — Leave unconnected when USB-CDC serial is in use
//  GPIO 14  —      — General purpose (no default assignment)
//  GPIO 15  LED    — Onboard status LED (active LOW); also SPI SCK if SPI needed
//  GPIO 16  UART0  — TX (SERIAL_B / external UART adapter)
//  GPIO 17  UART0  — RX
//  GPIO 18  Ax2    — AXIS2 STEP  (or ULN2003 IN1 Axis2)
//  GPIO 19  Ax2    — AXIS2 DIR   (or ULN2003 IN2 Axis2)
//  GPIO 20  Ax3    — AXIS3/4/5 STEP  (or ULN2003 IN3 Axis2)
//  GPIO 21  Ax3    — AXIS3/4/5 DIR   (or ULN2003 IN4 Axis2 — bottom-right pin)
//  GPIO 22  Ax1    — AXIS1_M2 = SPI CS or TMC UART TX for Axis1 (or ULN2003 IN3 Axis1)
//  GPIO 23  Ax2    — AXIS2_M2 = SPI CS or TMC UART TX for Axis2 (or ULN2003 IN4 Axis1)
//
// Hardware timer limit: ESP32-C6 has 2 hardware timers.
// Set TASKS_HWTIMERS 2 (or lower) in Config.h.
// -------------------------------------------------------------------------------------------------
#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32C6)

// Serial ports ------------------------------------------------------------------------------------
// Primary: USB-CDC via built-in USB-JTAG peripheral (GPIO12/13).
// No external UART adapter needed; plug the Supermini directly into USB.
#if SERIAL_A_BAUD_DEFAULT != OFF
  #define SERIAL_A              Serial
#endif
// Secondary: UART0 on GPIO16(TX)/GPIO17(RX) — connect an external adapter here.
#if SERIAL_B_BAUD_DEFAULT != OFF
  #define SERIAL_B              Serial1
  #define SERIAL_B_RX           17
  #define SERIAL_B_TX           16
#endif

// TMC UART (single hardware UART, up to 4 drivers via address select) ----------------------------
#if defined(STEP_DIR_TMC_UART_PRESENT) || defined(SERVO_TMC2209_PRESENT)
  #if defined(SERIAL_TMC_HARDWARE_UART)
    #define SERIAL_TMC          Serial1
    #define SERIAL_TMC_BAUD     460800
    #define SERIAL_TMC_RX       2        // AUX2 — shared RX
    #define SERIAL_TMC_TX       22       // AXIS1_M2 — TX (Axis1 address 0)
    #define SERIAL_TMC_ADDRESS_MAP(x) (x)
  #endif
#endif

// I2C (Wire) --------------------------------------------------------------------------------------
// Explicitly set SDA/SCL so WIRE_INIT() calls Wire.begin(SDA,SCL) before setClock().
// These are also AUX3/AUX4 and home-switch pins — choose one role per project.
// NOTE: when ULN2003 motors use GPIO6/7 for coil outputs, I2C must be reassigned
// or disabled (set I2C_SDA_PIN/I2C_SCL_PIN to other free pins in Config.h).
#ifndef I2C_SDA_PIN
  #define I2C_SDA_PIN             6
#endif
#ifndef I2C_SCL_PIN
  #define I2C_SCL_PIN             7
#endif

// Multi-purpose auxiliary pins --------------------------------------------------------------------
#define AUX2_PIN                2    // SPI MISO / TMC UART RX (shared input)
#define AUX3_PIN                6    // Home SW Axis1  — or I2C SDA
#define AUX4_PIN                7    // Home SW Axis2  — or I2C SCL
#define AUX7_PIN                0    // Limit SW, PPS, etc.
#define AUX8_PIN                3    // 1-Wire, Buzzer (GPIO3 — NOT the status LED)

// 1-Wire bus
#ifndef ONE_WIRE_PIN
  #define ONE_WIRE_PIN          AUX8_PIN
#endif

// PEC index
#ifndef PEC_SENSE_PIN
  #define PEC_SENSE_PIN         1    // ADC-capable GPIO1
#endif

// Status / mount LED — onboard LED is GPIO15 (active LOW)
#ifndef STATUS_LED_PIN
  #define STATUS_LED_PIN        15
#endif
#define MOUNT_LED_PIN           STATUS_LED_PIN
#ifndef RETICLE_LED_PIN
  #define RETICLE_LED_PIN       STATUS_LED_PIN
#endif

// Piezo buzzer
#ifndef STATUS_BUZZER_PIN
  #define STATUS_BUZZER_PIN     AUX8_PIN
#endif

// PPS time source
#ifndef PPS_SENSE_PIN
  #define PPS_SENSE_PIN         AUX7_PIN
#endif

// Limit switch (shared with PPS by default; wire to one or the other)
#ifndef LIMIT_SENSE_PIN
  #define LIMIT_SENSE_PIN       AUX7_PIN
#endif

// Shared enable -----------------------------------------------------------------------------------
// GPIO10 is NOT broken out on the Supermini header — SHARED_ENABLE is set to OFF.
// For step/dir drivers that need a hardware enable line, define AXIS1_ENABLE_PIN
// and AXIS2_ENABLE_PIN to a free GPIO in your Config.h.
#define SHARED_DIRECTION_PINS
#define SHARED_ENABLE_PIN       OFF  // GPIO10 not accessible on this board

// Axis1 — RA / Azimuth step-dir driver ------------------------------------------------------------
#define AXIS1_ENABLE_PIN        OFF  // set to a free GPIO in Config.h if needed
#define AXIS1_M0_PIN            OFF  // GPIO11 not broken out; SPI MOSI must be reassigned
#define AXIS1_M1_PIN            15   // GPIO15 (SPI SCK) — also the status LED
#define AXIS1_M2_PIN            22   // SPI CS Axis1  —or—  TMC UART TX Axis1
#define AXIS1_M3_PIN            AUX2_PIN  // SPI MISO  —or—  TMC UART RX (shared)
#define AXIS1_STEP_PIN          4
#define AXIS1_DIR_PIN           5
#ifndef AXIS1_SENSE_HOME_PIN
  #define AXIS1_SENSE_HOME_PIN  AUX3_PIN
#endif

// Axis2 — Declination / Altitude step-dir driver -------------------------------------------------
#define AXIS2_ENABLE_PIN        OFF  // set to a free GPIO in Config.h if needed
#define AXIS2_M0_PIN            OFF  // GPIO11 not broken out
#define AXIS2_M1_PIN            15   // GPIO15 (SPI SCK) — also the status LED
#define AXIS2_M2_PIN            23   // SPI CS Axis2  —or—  TMC UART TX Axis2
#define AXIS2_M3_PIN            AUX2_PIN  // SPI MISO  —or—  TMC UART RX (shared)
#define AXIS2_STEP_PIN          18
#define AXIS2_DIR_PIN           19
#ifndef AXIS2_SENSE_HOME_PIN
  #define AXIS2_SENSE_HOME_PIN  AUX4_PIN
#endif

// Axis3 — Rotator (optional) ----------------------------------------------------------------------
// GPIO20/21 are shared among Axis3, Axis4, and Axis5 (rotator / dual focuser).
// Only one of these axes can be physically wired at a time.
#define AXIS3_ENABLE_PIN        OFF
#define AXIS3_M0_PIN            OFF
#define AXIS3_M1_PIN            OFF
#define AXIS3_M2_PIN            OFF
#define AXIS3_M3_PIN            OFF
#define AXIS3_STEP_PIN          20
#define AXIS3_DIR_PIN           21

// Axis4 — Focuser 1 (optional, shares GPIO20/21 with Axis3) --------------------------------------
#define AXIS4_ENABLE_PIN        OFF
#define AXIS4_M0_PIN            OFF
#define AXIS4_M1_PIN            OFF
#define AXIS4_M2_PIN            OFF
#define AXIS4_M3_PIN            OFF
#define AXIS4_STEP_PIN          20
#define AXIS4_DIR_PIN           21

// Axis5 — Focuser 2 (optional, shares GPIO20/21 with Axis3) --------------------------------------
#define AXIS5_ENABLE_PIN        OFF
#define AXIS5_M0_PIN            OFF
#define AXIS5_M1_PIN            OFF
#define AXIS5_M2_PIN            OFF
#define AXIS5_M3_PIN            OFF
#define AXIS5_STEP_PIN          20
#define AXIS5_DIR_PIN           21

// ST4 guide port ----------------------------------------------------------------------------------
// No dedicated pins in the default layout.  To enable ST4, add to Config.h:
//   #define ST4_RA_W_PIN  20  (if Axis3/4/5 unused)
//   #define ST4_DEC_S_PIN 21
//   #define ST4_DEC_N_PIN 22  (if SPI/UART TMC unused on Axis1)
//   #define ST4_RA_E_PIN  23  (if SPI/UART TMC unused on Axis2)

// ULN2003 unipolar motor driver (28BYJ-48) --------------------------------------------------------
// Physical pin groups — 4 consecutive header pins per motor for easy wiring:
//
//  Motor 1 — RIGHT column pins (GPIO20→19→18→15, top-to-bottom):
//    #define AXIS1_IN1_PIN  20   // GPIO20 — right column, 1st free pin
//    #define AXIS1_IN2_PIN  19   // GPIO19
//    #define AXIS1_IN3_PIN  18   // GPIO18
//    #define AXIS1_IN4_PIN  15   // GPIO15 — onboard LED will pulse with steps (visual indicator)
//
//  Motor 2 — LEFT column pins (GPIO4→5→6→7, top-to-bottom):
//    #define AXIS2_IN1_PIN   4   // GPIO4  — left column, 4 consecutive pins
//    #define AXIS2_IN2_PIN   5   // GPIO5
//    #define AXIS2_IN3_PIN   6   // GPIO6  (normally I2C SDA — free when no I2C devices)
//    #define AXIS2_IN4_PIN   7   // GPIO7  (normally I2C SCL — free when no I2C devices)
//
//  Power: connect ULN2003 board 5V to the 5V header pin; IN1..IN4 accept 3.3V logic directly.
//  AXIS1_ENABLE_PIN / AXIS2_ENABLE_PIN: leave as OFF (coils always energised when motor is enabled
//  by OnStep) or wire an external transistor to a free GPIO for full power-cut.
//
//  If I2C sensors are needed alongside Motor 2, reassign I2C in Config.h, for example:
//    #define I2C_SDA_PIN  14   // GPIO14 is free and has no other default use
//    #define I2C_SCL_PIN  21   // GPIO21 (bottom-right pin)

// SG90 RC servo (SERVO_RC driver) ----------------------------------------------------------------
// When switching from ULN2003 to SG90 servos, only 1 GPIO per axis is needed (PWM signal).
// Reuse the IN1 pin of each ULN2003 group — it is physically accessible and proven:
//
//  In Config.h:
//    #define AXIS1_DRIVER_MODEL    SERVO_RC
//    #define AXIS1_SERVO_PIN       20   // GPIO20 — was AXIS1_IN1, right column
//    #define AXIS1_STEPS_PER_DEGREE 10.0
//
//    #define AXIS2_DRIVER_MODEL    SERVO_RC
//    #define AXIS2_SERVO_PIN        2   // GPIO2 — was AXIS2_IN1, left column
//    #define AXIS2_STEPS_PER_DEGREE 10.0
//
//  SG90 wiring: Red → 5V header pin, Brown → GND header pin, Yellow → AXIS*_SERVO_PIN
//  Position model: 90° = center (power-on). servoDeg = 90 + steps/stepsPerDeg, clamped 0–180°.
//  Physical alignment: orient the device so 90° points in your desired home direction.

#else
  #error "Wrong processor for this configuration!"
#endif
