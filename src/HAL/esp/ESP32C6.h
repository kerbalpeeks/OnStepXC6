// Platform setup for ESP32-C6 (RISC-V) with Arduino ESP32 core v3.x
// -------------------------------------------------------------------------------------------------
#pragma once

// Fast processor with hardware FP
#define HAL_FAST_PROCESSOR

// Base rate for critical task timing (0.0095s = 0.14", 0.2 sec/day)
#define HAL_FRACTIONAL_SEC 105.2631579F

// Hardware timer frequency override for ESP32-C6.
//
// The ESP32-C6 on this board has a 4 MHz XTAL (confirmed via board-info serial dump).
// timerBegin(16000000) iterates clock sources {PLL_F80M, RC_FAST, XTAL} looking for an
// integer divider in [2, 65536].  For a 4 MHz XTAL:
//   - XTAL  4 MHz ÷ 16 MHz = 0.25 → integer 0 → rejected
//   - RC_FAST ~17.5 MHz ÷ 16 MHz = 1 → rejected (below minimum of 2)
//   - If PLL_F80M is unavailable or returns 0 → all sources fail → NULL returned
// 2 MHz is the highest frequency achievable in all configurations:
//   - 4 MHz XTAL ÷ 2 = 2 MHz  (exact, divider = 2 = minimum valid)
//   - PLL_F80M 80 MHz ÷ 40 = 2 MHz  (exact, when PLL is active)
//   - RC_FAST ~17.5 MHz ÷ 8 ≈ 2.2 MHz  (8% off, last-resort fallback)
// Timer precision 0.5 µs is more than adequate for telescope mount motor control.
// HAL_FRACTIONAL_SEC and the sub-micros accounting are unchanged; only the hardware
// frequency and the tick-conversion ratio (TIMER_RATE_16MHZ_TICKS) change.
#define TIMER_FREQ_HZ          2000000UL  // 2 MHz: achievable from 4 MHz XTAL (÷2) or PLL (÷40)
#define TIMER_RATE_MHZ         2L         // TIMER_FREQ_HZ in MHz
#define TIMER_RATE_16MHZ_TICKS 8L         // 16L / TIMER_RATE_MHZ: converts sub-micros → timer ticks

// Analog read and write
#ifndef HAL_VCC
  #define HAL_VCC 3.3F
#endif
#ifndef ANALOG_READ_RANGE
  #define ANALOG_READ_RANGE 1023
#endif
#ifndef ANALOG_WRITE_RANGE
  #define ANALOG_WRITE_RANGE 255
#else
  #error "Configuration (Config.h): ANALOG_WRITE_RANGE can't be changed on this platform"
#endif

// Analog read/write capabilities.
// Arduino ESP32 core v3.x removed the global analogWriteResolution(bits) and
// analogWriteFrequency(freq) overloads; both are now per-pin:
//   analogWriteResolution(pin, bits)
//   analogWriteFrequency(pin, freq)
// Set PER_PIN flags so Analog.cpp takes the correct code path.
#define HAL_HAS_PER_PIN_PWM_RESOLUTION 1
#define HAL_HAS_PER_PIN_PWM_FREQUENCY 1
#define HAL_HAS_GLOBAL_PWM_RESOLUTION 0
#define HAL_HAS_GLOBAL_PWM_FREQUENCY 0
#define HAL_PWM_HZ_MAX 200000U

#define HAL_HAS_GLOBAL_ADC_RESOLUTION 1

#define HAL_PWM_BITS_MAX 16
#define HAL_ADC_BITS_MAX 12

// Step rate limits.
// ESP32-C6 is RISC-V at up to 160 MHz (vs Xtensa at 240 MHz).
// HAL_FAST_TICKS uses micros() on RISC-V, so timing resolution is 1 µs.
// 60 µs lower limit is conservative; tune down after bench measurement.
#if CAN_PLUS != OFF
  #define HAL_MAXRATE_LOWER_LIMIT 80
#else
  #define HAL_MAXRATE_LOWER_LIMIT 60
#endif
// Step pulse width measured on RISC-V core at 160 MHz
#define HAL_PULSE_WIDTH 300 // in ns

// I2C ---------------------------------------------------------------------------------------------
#include <Wire.h>
#ifndef HAL_WIRE
  #define HAL_WIRE Wire
#endif
#ifndef HAL_WIRE_CLOCK
  #define HAL_WIRE_CLOCK 100000
#endif

// Non-volatile storage via Preferences (NVS) ------------------------------------------------------
#if NV_DRIVER == NV_DEFAULT
  #undef NV_DRIVER
  #define NV_DRIVER NV_ESP
#endif

// Internal MCU temperature ------------------------------------------------------------------------
// ESP32-C6 has an internal temperature sensor; temperatureRead() is available in Arduino ESP32 v3.x.
// The C6 sensor has a different calibration than Xtensa variants; apply a correction if needed.
#ifndef INTERNAL_TEMP_CORRECTION
  #define INTERNAL_TEMP_CORRECTION 0
#endif
#define HAL_TEMP() ( temperatureRead() + INTERNAL_TEMP_CORRECTION )

// Bluetooth ---------------------------------------------------------------------------------------
// ESP32-C6 has BLE only — Classic Bluetooth (BluetoothSerial) is NOT available.
// SERIAL_BT_MODE must be OFF for this platform.
#ifndef SERIAL_BT_MODE
  #define SERIAL_BT_MODE OFF
#endif
#if SERIAL_BT_MODE != OFF
  #error "SERIAL_BT_MODE is not supported on ESP32-C6 (no Classic Bluetooth). Set SERIAL_BT_MODE OFF."
#endif
#define SERIAL_BT_BEGIN()

// Hardware timer note -----------------------------------------------------------------------------
// The ESP32-C6 has 2 general-purpose hardware timers (vs 4 on original ESP32).
// Do NOT set TASKS_HWTIMERS > 2 in Config.h for this platform.

// HAL initialisation ------------------------------------------------------------------------------
#define HAL_INIT() { \
  HAL_FAST_TICKS_INIT(); \
}

// MCU reset
#define HAL_RESET() ESP.restart()
