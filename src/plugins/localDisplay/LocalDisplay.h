// -----------------------------------------------------------------------------------
// Local display plugin — SSD1306 128×64 OLED + KY-040 rotary encoder.
//
// Provides a simple menu-driven UI for pointing the mount at solar system bodies.
// Configured via Config.h:
//   #define LOCAL_DISPLAY                  ON
//   #define LOCAL_DISPLAY_ENCODER_CLK_PIN  14
//   #define LOCAL_DISPLAY_ENCODER_DT_PIN    9
//   #define LOCAL_DISPLAY_ENCODER_BTN_PIN   1   // GPIO1: free when PEC_SENSE OFF
//   #define LOCAL_DISPLAY_POLL_MS          100   // optional, default 100
//
// Button pin notes for ESP32-C6 Supermini:
//   GPIO8  — onboard RGB LED; INPUT_PULLUP holds pin at ~mid-rail → AVOID for BTN
//   GPIO1  — PEC_SENSE pin (free when PEC_SENSE OFF); recommended for BTN
//   GPIO12/13 — USB D±; avoid while USB-CDC is active
//   GPIO21/22/23 — not on the accessible breadboard header row — avoid
//
// I2C note: Wire.begin() must already have been called before LocalDisplay::init()
// runs (the DS3231 driver does this). LocalDisplay does NOT call Wire.begin() to
// avoid double-initialising the ESP-IDF I2C driver (causes ESP_ERR_INVALID_STATE).
//
// Button debug: add  #define LOCAL_DISPLAY_DEBUG  in Config.h to enable
// Serial.printf logging of raw pin transitions and press/release events.
//
// Required Arduino library: U8g2 (by olikraus, install via Library Manager).
#pragma once

#include "../../Common.h"

#ifdef LOCAL_DISPLAY_PRESENT

#include <Wire.h>
#include <U8g2lib.h>

#include "../../telescope/mount/goto/Goto.h"
#include "../../telescope/mount/Mount.h"
#include "../../telescope/mount/site/Site.h"
#include "../../lib/tasks/OnTask.h"

// ---------------------------------------------------------------------------
// Screen identifiers
enum LdScreen : uint8_t {
  SCR_MAIN_MENU = 0,
  SCR_MOVE_TO,
  SCR_POINTING,
  SCR_STUB        // placeholder for Settings / How to use / Factory reset
};

// Icon shape for each target's pointing screen
enum LdShape : uint8_t {
  SHAPE_MOON,    // crescent
  SHAPE_DISC,    // plain filled disc (Venus, Mars)
  SHAPE_BANDS,   // disc with horizontal bands (Jupiter)
  SHAPE_SATURN,   // disc + ellipse ring
  SHAPE_JUPITER,
  SHAPE_MERCURY,
  SHAPE_VENUS,
  SHAPE_PLUTO,
  SHAPE_MARS,
  SHAPE_URANUS,
  SHAPE_NEPTUNE,
  SHAPE_EARTH
};

// ---------------------------------------------------------------------------

class LocalDisplay {
  public:
    // Call right after WIRE_INIT (before telescope.init) to show "Starting..." splash
    void earlyInit();

    // Call once after telescope.init()
    void init();

    // Called by task scheduler at LOCAL_DISPLAY_POLL_MS — handles display + input
    void poll();

    // Called by fast encoder task every 2ms — samples CLK/DT/button
    void pollEncoder();

  private:
    // ---- Drawing helpers ----
    void drawHeader(const char *title);
    void drawMenu(const char * const *items, uint8_t count, uint8_t selected);
    void drawPointing();
    void drawModal();
    void drawStub(const char *label);
    void drawTargetIcon(LdShape shape);
    void logEncoderDiag(int delta, bool shortPress, bool longPress);

    // ---- Action ----
    void goToTarget(uint8_t idx);

    // ---- Display state ----
    LdScreen    _screen        = SCR_MAIN_MENU;
    uint8_t     _menuSel       = 0;
    uint8_t     _targetIdx     = 0;
    bool        _wasSlewing    = false;
    const char *_stubTitle     = "";
    int8_t      _barPos        = 0;
    int8_t      _barDir        = 1;
    bool        _ready         = false;
    bool        _earlyInitDone = false;

    // ---- Modal dialog state (pointing screen body-switch) ----
    bool    _modalActive = false;
    uint8_t _modalSel    = 0;

    // ---- Encoder polling state (written by pollEncoder, read by poll) ----
    volatile int  _encDelta      = 0;
    uint8_t       _lastClkState  = HIGH;
    uint32_t      _encLastStepUs = 0;    // micros-based debounce for encoder steps

    // ---- Button state ----
    uint32_t _btnPressTime = 0;
    bool     _btnHeld      = false;
    volatile bool _btnShort = false;
    volatile bool _btnLong  = false;

    static constexpr uint16_t BTN_DEBOUNCE_MS   = 20;
    static constexpr uint16_t BTN_LONG_PRESS_MS = 600;
    static constexpr uint32_t ENC_DEBOUNCE_US   = 1500;
    static constexpr uint16_t ENCODER_DIAG_MS   = 2000;

    // ---- Encoder diagnostics (always-on via V macros in VERBOSE mode) ----
    uint32_t _encDiagLastMs     = 0;
    uint32_t _encPollCount      = 0;
    uint32_t _encClkChangeCount = 0;
    uint32_t _encFallCount      = 0;
    uint32_t _encDebounceDrop   = 0;
    uint32_t _encStepCwCount    = 0;
    uint32_t _encStepCcwCount   = 0;
    uint32_t _btnShortCount     = 0;
    uint32_t _btnLongCount      = 0;

    // ---- Button debug (active when LOCAL_DISPLAY_DEBUG is defined) ----
    #ifdef LOCAL_DISPLAY_DEBUG
      uint8_t  _dbgBtnRaw      = HIGH;
      uint16_t _dbgBounceCount = 0;
    #endif
};

// Single global instance
extern LocalDisplay localDisplay;

// Task wrapper functions
void ldPollWrapper();
void ldEncoderWrapper();

#endif // LOCAL_DISPLAY_PRESENT
