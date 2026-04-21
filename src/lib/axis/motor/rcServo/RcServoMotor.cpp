// RC hobby servo motor driver — see RcServoMotor.h for full description.
#include "RcServoMotor.h"

#ifdef RC_SERVO_MOTOR_PRESENT

#include "../../../tasks/OnTask.h"

const DriverStatus RcServoMotor::noFault = {false, {false, false}, {false, false}, false, false, false, false};

static RcServoMotor *rcServoInstance[2] = { nullptr, nullptr };
void moveRcServoAxis1() { if (rcServoInstance[0]) rcServoInstance[0]->move(); }
void moveRcServoAxis2() { if (rcServoInstance[1]) rcServoInstance[1]->move(); }

RcServoMotor::RcServoMotor(uint8_t axisNumber, int8_t reverse, uint8_t pwmPin, float stepsPerDeg)
  : Motor(axisNumber, reverse), _pwmPin(pwmPin), _stepsPerDeg(stepsPerDeg) {
  strcpy(axisPrefix, " Axis_RcServo, ");
  axisPrefix[5] = '0' + axisNumber;
  if (axisNumber >= 1 && axisNumber <= 2) {
    rcServoInstance[axisNumber - 1] = this;
    callback = (axisNumber == 1) ? moveRcServoAxis1 : moveRcServoAxis2;
  }
}

bool RcServoMotor::init() {
  if (!Motor::init()) return false;

  _servo.setPeriodHertz(50);
  _servo.attach(_pwmPin, 500, 2500);  // SG90: 500–2500 µs covers full 0–180°
  _servo.write(90);                   // center — servo physically moves here on boot
  // SG90 worst-case travel (180°) = 400ms. Wait here so this servo's inrush current
  // dissipates before the next axis initialises, preventing I2C bus voltage sag.
  delay(400);

  VF("MSG:"); V(axisPrefix); VF("RC servo on GPIO"); V(_pwmPin); VLF(", homed to 90° (center)");

  char taskName[8] = "RcSrv_";
  taskName[6] = '0' + axisNumber;
  taskName[7] = '\0';
  taskHandle = tasks.add(0, 0, true, 0, callback, taskName);
  if (!taskHandle) { DLF("ERR: RcServoMotor, task creation failed"); return false; }
  tasks.setPeriodSubMicros(taskHandle, 0);

  ready = true;
  return true;
}

void RcServoMotor::enable(bool value) {
  enabled = value;
  if (!enabled) {
    _servo.write(90);
    if (taskHandle) { tasks.setPeriodSubMicros(taskHandle, 0); lastPeriod = 0; lastPeriodSet = 0; }
    noInterrupts(); _rcStep = 0; interrupts();
  }
}

float RcServoMotor::getFrequencySteps() {
  return currentFrequency * _rcStep;
}

void RcServoMotor::setFrequencySteps(float frequency) {
  if (!ready) return;
  if (!enabled) {
    if (taskHandle) tasks.setPeriodSubMicros(taskHandle, 0);
    return;
  }

  int dir = 0;
  if      (frequency > 0.0F) { dir =  1; }
  else if (frequency < 0.0F) { dir = -1; frequency = -frequency; }

  if (inBacklash) frequency = backlashFrequency;

  if (frequency != currentFrequency) {
    currentFrequency = frequency;

    float period = 1000000.0F / frequency;
    if (!isnan(period) && period <= 130000000.0F) {
      period *= 16.0F;
      lastPeriod = (unsigned long)lroundf(period);
    } else {
      lastPeriod = 0;
      frequency  = 0.0F;
      dir        = 0;
    }

    if (_rcStep != dir) _rcStep = 0;
    if (lastPeriodSet != lastPeriod) {
      tasks.setPeriodSubMicros(taskHandle, lastPeriod);
      lastPeriodSet = lastPeriod;
    }
    _rcStep = dir;
  } else {
    noInterrupts();
    _rcStep = dir;
    interrupts();
  }
}

IRAM_ATTR void RcServoMotor::move() {
  if (sync && !inBacklash) targetSteps += _rcStep;

  if (motorSteps > targetSteps) {
    motorSteps--;
  } else if (motorSteps < targetSteps) {
    motorSteps++;
  } else {
    return;
  }

  updateServoPosition();
}

void RcServoMotor::updateServoPosition() {
  uint32_t now = millis();
  if (now - _lastServoUpdateMs < 20) return;  // 50 Hz throttle
  _lastServoUpdateMs = now;

  long steps = normalizedReverse ? -motorSteps : motorSteps;
  float deg = 90.0F + (float)steps / _stepsPerDeg;
  deg = constrain(deg, 0.0F, 180.0F);
  _servo.write((int)roundf(deg));
}

#endif // RC_SERVO_MOTOR_PRESENT
