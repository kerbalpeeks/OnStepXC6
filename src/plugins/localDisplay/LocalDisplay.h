// -----------------------------------------------------------------------------------
// Local display plugin — SSD1306 128×64 OLED + KY-040 rotary encoder.
//
// Provides a simple menu-driven UI for pointing the mount at solar system bodies.
// Configured via Config.h:
//   #define LOCAL_DISPLAY                  ON
//   #define LOCAL_DISPLAY_ENCODER_CLK_PIN  14
//   #define LOCAL_DISPLAY_ENCODER_DT_PIN    0
//   #define LOCAL_DISPLAY_ENCODER_BTN_PIN   1
//   #define LOCAL_DISPLAY_POLL_MS          100   // optional, default 100
//
// Required Arduino libraries: U8g2 (by olikraus, install via Library Manager).
//
// Recommended encoder pin assignment for ESP32-C6 Supermini:
//   #define LOCAL_DISPLAY_ENCODER_CLK_PIN  14   // right column — well clear of motor groups
//   #define LOCAL_DISPLAY_ENCODER_DT_PIN    9   // right column
//   #define LOCAL_DISPLAY_ENCODER_BTN_PIN  21   // bottom-right pin (GPIO8=RGB LED — avoid)
// GPIO0/GPIO1 — adjacent to Axis2 motor pins (GPIO2-5) — do NOT use for encoder.
// GPIO8  — onboard RGB LED peripheral; using as INPUT_PULLUP lights the LED — avoid.
// GPIO12/13 — USB D±; leave unconnected when USB-CDC serial is active — avoid.
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

// ---------------------------------------------------------------------------

class LocalDisplay {
  public:
    // Call once after telescope.init()
    void init();

    // Called by task scheduler at LOCAL_DISPLAY_POLL_MS — handles display + input
    void poll();

    // Called by fast encoder task every 10ms — samples CLK/DT/button
    void pollEncoder();

  private:
    // ---- Drawing helpers ----
    void drawHeader(const char *title);
    void drawMenu(const char * const *items, uint8_t count, uint8_t selected);
    void drawPointing();
    void drawStub(const char *label);

    // ---- Action ----
    void goToTarget(uint8_t idx);

    // ---- Display state ----
    LdScreen    _screen     = SCR_MAIN_MENU;
    uint8_t     _menuSel    = 0;
    uint8_t     _targetIdx  = 0;
    bool        _wasSlewing = false;
    const char *_stubTitle  = "";
    int8_t      _barPos     = 0;
    int8_t      _barDir     = 1;
    bool        _ready      = false;

    // ---- Encoder polling state (written by pollEncoder, read by poll) ----
    volatile int  _encDelta     = 0;
    uint8_t       _lastClkState = HIGH;  // previous CLK level for edge detection
    uint8_t       _clkConfirm   = 0;     // set to 1 on first LOW; step registered on second
    uint8_t       _clkDirSample = 0;     // DT level captured at the CLK falling edge

    // ---- Button state ----
    uint32_t _btnPressTime = 0;
    bool     _btnHeld      = false;
    volatile bool _btnShort = false;
    volatile bool _btnLong  = false;

    static constexpr uint16_t BTN_DEBOUNCE_MS   = 20;
    static constexpr uint16_t BTN_LONG_PRESS_MS = 600;
};

// Single global instance
extern LocalDisplay localDisplay;

// Task wrapper functions
void ldPollWrapper();
void ldEncoderWrapper();

#endif // LOCAL_DISPLAY_PRESENT
