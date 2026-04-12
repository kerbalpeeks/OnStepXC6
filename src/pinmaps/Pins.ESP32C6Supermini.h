// -------------------------------------------------------------------------------------------------
// Pin map for ESP32-C6 Supermini (OnStep, 2-axis, Arduino ESP32 core v3.x)
//
// Board: "ESP32C6 Dev Module" in Arduino IDE, or esp32:esp32:esp32c6 in PlatformIO.
//
// ESP32-C6 Supermini physical layout (USB-C connector at top):
//
//  Left column  (top→bottom): 5V · GND · 3V3 · GPIO2 · GPIO3 · GPIO8¹ · GPIO9¹
//                              GPIO10 · GPIO11 · GPIO12² · GPIO13² · GPIO4
//  Right column (top→bottom): GND · EN · GPIO6 · GPIO5 · GPIO0 · GPIO1
//                              GPIO7 · GPIO22 · GPIO23 · GPIO19 · GPIO20 · GPIO21
//
//  ¹ Strapping pins — safe as inputs; avoid driving them LOW at power-on.
//  ² GPIO12/13 are USB D-/D+.  Leave unconnected when USB-CDC serial is in use.
//
// Pin assignment summary
// ──────────────────────
//  GPIO  0  AUX7   — Limit SW / PPS input  (ADC-capable)
//  GPIO  1  PEC    — PEC index sense        (ADC-capable)
//  GPIO  2  AUX2   — SPI MISO / TMC UART RX (shared)
//  GPIO  3  AUX8   — Status LED / 1-Wire / Buzzer
//  GPIO  4  Ax1    — AXIS1 STEP
//  GPIO  5  Ax1    — AXIS1 DIR
//  GPIO  6  I2C    — SDA  (AUX3 / Home SW Axis1)  [Wire default on ESP32-C6]
//  GPIO  7  I2C    — SCL  (AUX4 / Home SW Axis2)  [Wire default on ESP32-C6]
//  GPIO  8  —      — BOOT button (strapping); leave for user input if needed
//  GPIO  9  —      — Strapping; avoid as output
//  GPIO 10  Ax1+2  — SHARED ENABLE (active LOW)
//  GPIO 11  SPI    — MOSI (AXIS1_M0 / AXIS2_M0, shared)
//  GPIO 12  USB D- — Do not use while USB-CDC is active
//  GPIO 13  USB D+ — Do not use while USB-CDC is active
//  GPIO 15  SPI    — SCK  (AXIS1_M1 / AXIS2_M1, shared)
//  GPIO 16  UART0  — TX   (SERIAL_B if not using USB-CDC)
//  GPIO 17  UART0  — RX
//  GPIO 18  Ax2    — AXIS2 STEP
//  GPIO 19  Ax2    — AXIS2 DIR
//  GPIO 20  Ax3    — AXIS3/4/5 STEP  (rotator / focuser — shared)
//  GPIO 21  Ax3    — AXIS3/4/5 DIR   (rotator / focuser — shared)
//  GPIO 22  Ax1    — AXIS1_M2 = SPI CS¹ or TMC UART TX for Axis1
//  GPIO 23  Ax2    — AXIS2_M2 = SPI CS² or TMC UART TX for Axis2
//
// ST4 guide port: no dedicated pins remain in the default layout.
// To add ST4, reassign unused AUX pins in your Config.h, e.g.:
//   #define ST4_RA_W_PIN  20   (only if Axis3 not used)
//   #define ST4_DEC_S_PIN 21
//   #define ST4_DEC_N_PIN 22   (only if SPI/UART TMC not used)
//   #define ST4_RA_E_PIN  23
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
// ESP32-C6 Arduino core defaults: SDA=GPIO6, SCL=GPIO7.
// These are also AUX3/AUX4 and home-switch pins — choose one role per project.
// Override by defining I2C_SDA_PIN / I2C_SCL_PIN in Config.h before the pinmap is loaded.

// Multi-purpose auxiliary pins --------------------------------------------------------------------
#define AUX2_PIN                2    // SPI MISO / TMC UART RX (shared input)
#define AUX3_PIN                6    // Home SW Axis1  — or I2C SDA
#define AUX4_PIN                7    // Home SW Axis2  — or I2C SCL
#define AUX7_PIN                0    // Limit SW, PPS, etc.
#define AUX8_PIN                3    // 1-Wire, Status LED, Reticle LED, Tone

// 1-Wire bus
#ifndef ONE_WIRE_PIN
  #define ONE_WIRE_PIN          AUX8_PIN
#endif

// PEC index
#ifndef PEC_SENSE_PIN
  #define PEC_SENSE_PIN         1    // ADC-capable GPIO1
#endif

// Status / mount LED (active LOW unless STATUS_LED_ON_STATE overridden)
#ifndef STATUS_LED_PIN
  #define STATUS_LED_PIN        AUX8_PIN
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

// Shared enable / direction hints -----------------------------------------------------------------
#define SHARED_DIRECTION_PINS
#define SHARED_ENABLE_PIN       10   // Active LOW; drives all axis enable lines

// Axis1 — RA / Azimuth step-dir driver ------------------------------------------------------------
#define AXIS1_ENABLE_PIN        SHARED
#define AXIS1_M0_PIN            11   // SPI MOSI  (shared with Axis2)
#define AXIS1_M1_PIN            15   // SPI SCK   (shared with Axis2)
#define AXIS1_M2_PIN            22   // SPI CS Axis1  —or—  TMC UART TX Axis1
#define AXIS1_M3_PIN            AUX2_PIN  // SPI MISO  —or—  TMC UART RX (shared)
#define AXIS1_STEP_PIN          4
#define AXIS1_DIR_PIN           5
#ifndef AXIS1_SENSE_HOME_PIN
  #define AXIS1_SENSE_HOME_PIN  AUX3_PIN
#endif

// Axis2 — Declination / Altitude step-dir driver -------------------------------------------------
#define AXIS2_ENABLE_PIN        SHARED
#define AXIS2_M0_PIN            11   // SPI MOSI  (shared)
#define AXIS2_M1_PIN            15   // SPI SCK   (shared)
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

#else
  #error "Wrong processor for this configuration!"
#endif
