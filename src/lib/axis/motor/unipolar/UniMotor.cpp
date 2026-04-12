// -----------------------------------------------------------------------------------
// Unipolar stepper motor driver (ULN2003 / 28BYJ-48 etc.)

#include "UniMotor.h"

#ifdef UNI_MOTOR_PRESENT

#include "../../../tasks/OnTask.h"

// ---------------------------------------------------------------------------
// Static member definitions

const DriverStatus UniMotor::noFault     = {false, {false, false}, {false, false}, false, false, false, false};
const DriverStatus UniMotor::errorStatus = {false, {false, false}, {false, false}, false, false, false, true};

// ---------------------------------------------------------------------------
// Coil-phase lookup tables
// Column order: IN1, IN2, IN3, IN4

// 2-phase full-step (4 states): energises two adjacent coils — better torque
const uint8_t UniMotor::fullStep[4][4] = {
  {1, 1, 0, 0},   // phase 0
  {0, 1, 1, 0},   // phase 1
  {0, 0, 1, 1},   // phase 2
  {1, 0, 0, 1},   // phase 3
};

// half-step (8 states): alternates single and double coil energisation
const uint8_t UniMotor::halfStep[8][4] = {
  {1, 0, 0, 0},   // phase 0
  {1, 1, 0, 0},   // phase 1
  {0, 1, 0, 0},   // phase 2
  {0, 1, 1, 0},   // phase 3
  {0, 0, 1, 0},   // phase 4
  {0, 0, 1, 1},   // phase 5
  {0, 0, 0, 1},   // phase 6
  {1, 0, 0, 1},   // phase 7
};

// ---------------------------------------------------------------------------
// Per-axis ISR free functions (same pattern as StepDirMotor)

static UniMotor *uniMotorInstance[9];

IRAM_ATTR void moveUniMotorAxis1() { uniMotorInstance[0]->move(); }
IRAM_ATTR void moveUniMotorAxis2() { uniMotorInstance[1]->move(); }
IRAM_ATTR void moveUniMotorAxis3() { uniMotorInstance[2]->move(); }
void moveUniMotorAxis4() { uniMotorInstance[3]->move(); }
void moveUniMotorAxis5() { uniMotorInstance[4]->move(); }
void moveUniMotorAxis6() { uniMotorInstance[5]->move(); }
void moveUniMotorAxis7() { uniMotorInstance[6]->move(); }
void moveUniMotorAxis8() { uniMotorInstance[7]->move(); }
void moveUniMotorAxis9() { uniMotorInstance[8]->move(); }

// ---------------------------------------------------------------------------

UniMotor::UniMotor(uint8_t axisNumber, int8_t reverse,
                   const UniPins *Pins, const UniSettings *Settings)
                   : Motor(axisNumber, reverse) {
  strcpy(axisPrefix, " Axis_UniMotor, ");
  axisPrefix[5] = '0' + axisNumber;

  driverType = STEP_DIR; // use step-grid park alignment (same coil-boundary logic)

  this->Pins     = Pins;
  this->Settings = Settings;

  // choose phase table depth from microstep setting
  phaseCount = (Settings->microsteps == 2) ? 8 : 4;

  uniMotorInstance[axisNumber - 1] = this;
  switch (axisNumber) {
    case 1: callback = moveUniMotorAxis1; break;
    case 2: callback = moveUniMotorAxis2; break;
    case 3: callback = moveUniMotorAxis3; break;
    case 4: callback = moveUniMotorAxis4; break;
    case 5: callback = moveUniMotorAxis5; break;
    case 6: callback = moveUniMotorAxis6; break;
    case 7: callback = moveUniMotorAxis7; break;
    case 8: callback = moveUniMotorAxis8; break;
    case 9: callback = moveUniMotorAxis9; break;
  }
}

bool UniMotor::init() {
  if (ready) return true;
  if (!Motor::init()) return false;

  VF("MSG:"); V(axisPrefix);
  VF("pins in1="); V(Pins->in1);
  VF(", in2="); V(Pins->in2);
  VF(", in3="); V(Pins->in3);
  VF(", in4="); V(Pins->in4);
  VF(", en="); if (Pins->enable == OFF) { VLF("OFF"); } else { VL(Pins->enable); }

  if (Pins->in1 == OFF || Pins->in2 == OFF || Pins->in3 == OFF || Pins->in4 == OFF) {
    DF("ERR:"); D(axisPrefix); DLF("one or more coil pins not assigned!");
    return false;
  }

  // configure coil pins as outputs, start de-energised
  pinModeEx(Pins->in1, OUTPUT); digitalWriteEx(Pins->in1, LOW);
  pinModeEx(Pins->in2, OUTPUT); digitalWriteEx(Pins->in2, LOW);
  pinModeEx(Pins->in3, OUTPUT); digitalWriteEx(Pins->in3, LOW);
  pinModeEx(Pins->in4, OUTPUT); digitalWriteEx(Pins->in4, LOW);

  // optional enable/power pin
  if (Pins->enable != OFF) {
    pinModeEx(Pins->enable, OUTPUT);
    digitalWriteEx(Pins->enable, !Pins->enabledState); // start disabled
  }

  VF("MSG:"); V(axisPrefix);
  VF("mode="); V(phaseCount == 8 ? "half" : "full"); VLF("-step");

  // register timer task
  VF("MSG:"); V(axisPrefix); VF("start task to move motor... ");
  char timerName[] = "Motor_";
  timerName[5] = '0' + axisNumber;
  taskHandle = tasks.add(0, 0, true, 0, callback, timerName);
  if (taskHandle) {
    V("success");
    // axes 1 and 2 can use hardware timers for precision tracking
    bool useHwTimer = (axisNumber <= 2);
    if (useHwTimer && !tasks.requestHardwareTimer(taskHandle, 0)) { VLF(" (no hardware timer!)"); } else { VLF(""); }
  } else {
    VLF("FAILED!");
    return false;
  }

  ready = true;
  return true;
}

void UniMotor::enable(bool state) {
  if (!ready || state == enabled) return;

  if (!state) {
    // disable first: stop timer, de-energise coils
    if (taskHandle) tasks.setPeriodSubMicros(taskHandle, 0);
    lastPeriod    = 0;
    lastPeriodSet = 0;
    currentFrequency = 0.0F;
    noInterrupts();
    step     = 0;
    takeStep = false;
    interrupts();
    coilsOff();
  }

  if (Pins->enable != OFF) {
    digitalWriteEx(Pins->enable, state ? Pins->enabledState : !Pins->enabledState);
  }

  if (state) {
    // re-energise at current phase
    writeCoils(phase);
  }

  enabled = state;
  VF("MSG:"); V(axisPrefix); VF("driver powered "); VLF(state ? "up" : "down");
}

float UniMotor::getFrequencySteps() {
  if (!ready || lastPeriod == 0) return 0.0F;
  #if STEP_WAVE_FORM == SQUARE
    return 8000000.0F / lastPeriod;
  #else
    return 16000000.0F / lastPeriod;
  #endif
}

void UniMotor::setFrequencySteps(float frequency) {
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

    #if STEP_WAVE_FORM == SQUARE
      float period = 500000.0F / frequency;
    #else
      float period = 1000000.0F / frequency;
    #endif

    if (!isnan(period) && period <= 130000000.0F) {
      period *= 16.0F;
      lastPeriod = (unsigned long)lroundf(period);
    } else {
      lastPeriod = 0;
      frequency  = 0.0F;
      dir        = 0;
    }

    if (step != dir) step = 0;
    if (lastPeriodSet != lastPeriod) {
      tasks.setPeriodSubMicros(taskHandle, lastPeriod);
      lastPeriodSet = lastPeriod;
    }
    step = dir;
  } else {
    noInterrupts();
    step = dir;
    interrupts();
  }
}

// ---------------------------------------------------------------------------
// ISR: called at each timer tick; advances coil sequence toward target

IRAM_ATTR void UniMotor::move() {
  #if STEP_WAVE_FORM == SQUARE
    if (takeStep) {
  #endif

  long lastTargetSteps = targetSteps;
  if (sync && !inBacklash) targetSteps += step;

  if (motorSteps > targetSteps || (inBacklash && uniDirection == kDirRev)) {
    // need to move in reverse
    if (uniDirection != kDirRev) {
      targetSteps  = lastTargetSteps;
      uniDirection = kDirRev;
      return; // wait one tick for direction to settle
    }

    if (backlashSteps > 0) {
      backlashSteps--;
      inBacklash = backlashSteps > 0;
    } else {
      motorSteps--;
      inBacklash = false;
    }

    // retreat one phase
    if (phase == 0) phase = phaseCount - 1; else phase--;
    writeCoils(phase);

  } else if (motorSteps < targetSteps || (inBacklash && uniDirection == kDirFwd)) {
    // need to move forward
    if (uniDirection != kDirFwd) {
      targetSteps  = lastTargetSteps;
      uniDirection = kDirFwd;
      return; // wait one tick
    }

    if (backlashSteps < backlashAmountSteps) {
      backlashSteps++;
      inBacklash = backlashSteps < backlashAmountSteps;
    } else {
      motorSteps++;
      inBacklash = false;
    }

    // advance one phase
    phase = (phase + 1) % phaseCount;
    writeCoils(phase);

  } else if (!inBacklash) {
    uniDirection = kDirNone;
  }

  #if STEP_WAVE_FORM == SQUARE
    }
    takeStep = !takeStep;
  #endif
}

// ---------------------------------------------------------------------------

IRAM_ATTR void UniMotor::writeCoils(uint8_t p) {
  const uint8_t (*tbl)[4] = (phaseCount == 8) ? halfStep : fullStep;
  digitalWriteF(Pins->in1, tbl[p][0]);
  digitalWriteF(Pins->in2, tbl[p][1]);
  digitalWriteF(Pins->in3, tbl[p][2]);
  digitalWriteF(Pins->in4, tbl[p][3]);
}

void UniMotor::coilsOff() {
  digitalWriteEx(Pins->in1, LOW);
  digitalWriteEx(Pins->in2, LOW);
  digitalWriteEx(Pins->in3, LOW);
  digitalWriteEx(Pins->in4, LOW);
}

#endif // UNI_MOTOR_PRESENT
