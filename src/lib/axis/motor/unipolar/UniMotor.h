// -----------------------------------------------------------------------------------
// Unipolar stepper motor driver (ULN2003 / 28BYJ-48 etc.)
//
// Drives a 4-wire unipolar motor directly from four GPIO pins.
// Supports full-step (4 coil states, AXIS_DRIVER_MICROSTEPS 1) and
// half-step (8 coil states, AXIS_DRIVER_MICROSTEPS 2) operation.
// No external driver chip is needed — connect IN1..IN4 directly to the
// ULN2003 board inputs (3.3 V logic is sufficient).
//
// Configuration example (Config.h):
//   #define AXIS1_DRIVER_MODEL   ULN2003
//   #define AXIS1_DRIVER_MICROSTEPS 2       // 2 = half-step (default), 1 = full-step
//   #define AXIS1_IN1_PIN        4
//   #define AXIS1_IN2_PIN        5
//   #define AXIS1_IN3_PIN        22
//   #define AXIS1_IN4_PIN        19
//   #define AXIS1_STEPS_PER_DEGREE  <calculated for your gearing>
#pragma once

#include "../../../../Common.h"

#ifdef UNI_MOTOR_PRESENT

#include "../Motor.h"

// Pins describing a single unipolar motor channel
typedef struct UniPins {
  int16_t in1;          // coil output IN1
  int16_t in2;          // coil output IN2
  int16_t in3;          // coil output IN3
  int16_t in4;          // coil output IN4
  int16_t enable;       // optional: drives motor power rail on/off (OFF = always on)
  uint8_t enabledState; // logic level that asserts enable (usually HIGH)
} UniPins;

// Settings for a unipolar motor channel
typedef struct UniSettings {
  int16_t model;       // driver model constant (ULN2003)
  int8_t  status;      // fault pin config — always OFF for ULN2003
  int16_t microsteps;  // 1 = full-step, 2 = half-step
} UniSettings;

class UniMotor : public Motor {
  public:
    UniMotor(uint8_t axisNumber, int8_t reverse,
             const UniPins *Pins, const UniSettings *Settings);

    // configure GPIO pins and register ISR task
    bool init();

    // enable / disable motor coils (cuts power when not moving if wired)
    void enable(bool value);

    // always healthy — ULN2003 has no fault feedback
    DriverStatus getDriverStatus() { return ready ? noFault : Motor::getDriverStatus(); }

    // current step rate in steps/sec
    float getFrequencySteps();

    // set motion frequency (+/-) steps/sec; 0 stops, negative reverses
    void setFrequencySteps(float frequency);

    // set backlash frequency (same path as setFrequencySteps)
    void setBacklashFrequencySteps(float frequency) { backlashFrequency = frequency; }

    // no microstep mode switching
    int getStepsPerStepSlewing() { return 1; }

    // sequencer steps per motor step (4 full / 8 half)
    int getSequencerSteps() { return phaseCount; }

    // driver name
    const char* name() { return "Unipolar/ULN2003"; }

    // ISR callback — advance coil pattern one step toward target
    void move();

  private:
    // write the coil pattern for a given phase index to the four output pins
    void writeCoils(uint8_t p);

    // de-energise all coils (power saving when disabled)
    void coilsOff();

    const UniPins    *Pins;
    const UniSettings *Settings;

    uint8_t taskHandle   = 0;
    uint8_t phase        = 0;    // current coil phase index
    uint8_t phaseCount   = 8;    // 4 (full-step) or 8 (half-step)

    // motion state
    float   currentFrequency  = 0.0F;
    unsigned long lastPeriod    = 0;
    unsigned long lastPeriodSet = 0;
    volatile bool takeStep      = false; // square-wave toggle

    // direction tracking (mirroring StepDirMotor pattern)
    static constexpr uint8_t kDirFwd  = 1;
    static constexpr uint8_t kDirRev  = 2;
    static constexpr uint8_t kDirNone = 0;
    volatile uint8_t uniDirection = kDirNone;

    // pre-built status structs (defined in UniMotor.cpp)
    static const DriverStatus noFault;
    static const DriverStatus errorStatus;

    // 2-phase full-step sequence (more torque): 4 states × 4 pins
    static const uint8_t fullStep[4][4];

    // half-step sequence: 8 states × 4 pins
    static const uint8_t halfStep[8][4];

    void (*callback)() = NULL;
};

#endif // UNI_MOTOR_PRESENT
