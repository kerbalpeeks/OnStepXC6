// RC hobby servo motor driver (SG90-style 50 Hz PWM position servo, open-loop)
//
// Position is commanded by PWM pulse width on a single GPIO.
// The servo is a position device: sending 90° means the servo physically IS at 90°.
// No encoder, no PID, no homing required.
//
// Position model:
//   motorSteps = 0 → 90° (center, power-on position)
//   servoDeg = 90 + motorSteps / stepsPerDegree  (clamped 0..180)
//
// Speed is controlled via setFrequencySteps(): faster step rate = faster angular velocity.
#pragma once

#include "../Motor.h"

#ifdef RC_SERVO_MOTOR_PRESENT

#include <ESP32Servo.h>

class RcServoMotor : public Motor {
  public:
    // stepsPerDeg: pass AXIS*_STEPS_PER_DEGREE from Config.h
    RcServoMotor(uint8_t axisNumber, int8_t reverse, uint8_t pwmPin, float stepsPerDeg);

    bool  init();
    void  enable(bool value);

    DriverStatus getDriverStatus() { return ready ? noFault : Motor::getDriverStatus(); }

    float getFrequencySteps();
    void  setFrequencySteps(float frequency);
    void  setBacklashFrequencySteps(float frequency) { backlashFrequency = frequency; }

    int   getStepsPerStepSlewing() { return 1; }
    int   getSequencerSteps()      { return 1; }

    const char* name() { return "RC Servo (SG90)"; }

    void move();  // called by task scheduler at the current step rate

  private:
    void updateServoPosition();

    Servo   _servo;
    uint8_t _pwmPin;
    float   _stepsPerDeg;    // steps per degree (from AXIS*_STEPS_PER_DEGREE)
    static const DriverStatus noFault;  // pre-built ok status

    bool    ready   = false;
    bool    enabled = false;

    volatile int  step = 0;   // -1, 0, +1 (motorSteps/targetSteps inherited from Motor)

    float         currentFrequency  = 0.0F;
    float         backlashFrequency = 0.0F;
    unsigned long lastPeriod        = 0;
    unsigned long lastPeriodSet     = 0;

    // throttle servo writes to 50 Hz (SG90 update rate)
    uint32_t _lastServoUpdateMs = 0;
};

void moveRcServoAxis1();
void moveRcServoAxis2();

#endif // RC_SERVO_MOTOR_PRESENT
