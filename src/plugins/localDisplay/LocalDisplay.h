// -----------------------------------------------------------------------------------
// Local display plugin — SSD1306 128×64 OLED + KY-040 rotary encoder.
//
// Provides a simple menu-driven UI for pointing the mount at solar system bodies.
// Configured via Config.h:
//   #define LOCAL_DISPLAY                  ON
//   #define LOCAL_DISPLAY_ENCODER_CLK_PIN  14
//   #define LOCAL_DISPLAY_ENCODER_DT_PIN   21
//   #define LOCAL_DISPLAY_ENCODER_BTN_PIN  22
//   #define LOCAL_DISPLAY_POLL_MS          100   // optional, default 100
//
// Required Arduino libraries: U8g2 (by olikraus, install via Library Manager).
//
// Recommended encoder pin assignment for ESP32-C6 Supermini:
//   #define LOCAL_DISPLAY_ENCODER_CLK_PIN  14   // right column, free GPIO
//   #define LOCAL_DISPLAY_ENCODER_DT_PIN    0   // left column, AUX7 (unused here)
//   #define LOCAL_DISPLAY_ENCODER_BTN_PIN   1   // left column, PEC (unused here)
// GPIO8 (RGB LED) and GPIO9 (BOOT strapping) are intentionally avoided.
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

    // Called by the task scheduler at LOCAL_DISPLAY_POLL_MS intervals
    void poll();

  private:
    // ---- Drawing helpers ----
    void drawHeader(const char *title);
    void drawMenu(const char * const *items, uint8_t count, uint8_t selected);
    void drawPointing();
    void drawStub(const char *label);

    // ---- Action ----
    // Compute coordinates for target[idx] and issue GoTo
    void goToTarget(uint8_t idx);

    // ---- State ----
    LdScreen _screen     = SCR_MAIN_MENU;
    uint8_t  _menuSel    = 0;       // cursor in current menu
    uint8_t  _targetIdx  = 0;       // which object we're pointing at
    bool     _wasSlewing = false;   // rising-edge detection for slew completion
    const char *_stubTitle = "";    // title to show on stub screen

    // Indeterminate slew-bar animation
    int8_t _barPos = 0;
    int8_t _barDir = 1;

    bool _ready = false;

    // ---- Encoder state (managed by ISR + poll) ----
    // Exposed as volatile so ISR can write
};

// Single global instance
extern LocalDisplay localDisplay;

// Free functions (file-scope, called by attachInterrupt and tasks.add)
void IRAM_ATTR ldEncoderClkISR();
void ldPollWrapper();

#endif // LOCAL_DISPLAY_PRESENT
