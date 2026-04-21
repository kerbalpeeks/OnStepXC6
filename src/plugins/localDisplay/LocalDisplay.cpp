// -----------------------------------------------------------------------------------
// Local display plugin implementation.

#include "LocalDisplay.h"

#ifdef LOCAL_DISPLAY_PRESENT

#include "Astronomy.h"
#include "../../lib/calendars/Calendars.h"
#include "../../lib/nv/Nv.h"

// SSD1306 default I2C address (0x3C, or 0x3D with SA0 tied HIGH)
#ifndef LOCAL_DISPLAY_I2C_ADDR
  #define LOCAL_DISPLAY_I2C_ADDR 0x3C
#endif

// Forward declarations and file-scope state for task callbacks defined later
#ifdef LOCAL_DISPLAY_STARTUP_ALT
  static void ldStartupSlewCb();
#endif
#if GOTO_LED_PIN != OFF
  static bool    _sLedState      = false;
  static uint8_t _sGotoLedHandle = 0;
  static void ldGotoLedBlink();
#endif

// ---------------------------------------------------------------------------
// U8g2 display object (128×64 SSD1306, hardware I2C, 2-page buffer)
static U8G2_SSD1306_128X64_NONAME_2_HW_I2C _u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

// ---------------------------------------------------------------------------
// Target descriptors

struct LdTarget {
  const char *name;
  const char *fact[3];
  LdShape     shape;
  void (*getPos)(double jd, double *ra, double *dec);
};

// \xc2\xb1 = UTF-8 for ± (U+00B1); rendered via drawUTF8 in drawPointing
static const LdTarget _targets[] = {
  {
    "Moon",
    { "Dist: \u00b1384,400km", "Diam:    3,475km", "Gravity:   0.17g" },
    SHAPE_MOON,
    Astronomy::moon
  },
  {
    "Mercury",
    { "Dist:  \u00b1 177M km", "Diam:    4,879km", "Gravity:   0.38g" },
    SHAPE_MERCURY,
    Astronomy::mercury
  },
  {
    "Venus",
    { "Dist: \u00b1 108M km", "Diam:   12,104km", "Gravity:   0.90g" },
    SHAPE_VENUS,
    Astronomy::venus
  },
  {
    "Mars",
    { "Dist: \u00b1 228M km", "Diam:    6,779km", "Gravity:   0.38g" },
    SHAPE_MARS,
    Astronomy::mars
  },
  {
    "Jupiter",
    { "Dist:  \u00b1 15.2 AU", "Diam:  139,820km", "Gravity:   2.53g" },
    SHAPE_JUPITER,
    Astronomy::jupiter
  },
  {
    "Saturn",
    { "Dist:  \u00b1 19.5 AU", "Diam:  116,460km", "Gravity:   1.07g" },
    SHAPE_SATURN,
    Astronomy::saturn
  },
  {
    "Uranus",
    { "Dist: \u00b1 119.2 AU", "Diam:   50,724km", "Gravity:   0.89g" },
    SHAPE_URANUS,
    Astronomy::uranus
  },
  {
    "Neptune",
    { "Dist: \u00b1 130.1 AU", "Diam:   49,244km", "Gravity:   1.14g" },
    SHAPE_NEPTUNE,
    Astronomy::neptune
  },
};
static constexpr uint8_t NUM_TARGETS = sizeof(_targets) / sizeof(_targets[0]);

// ---------------------------------------------------------------------------
// Main menu: "Move to..." first so a single click selects it immediately

static const char * const _mainItems[] = {
  "Move to...",
  "How to use",
  "Factory reset",
  "Settings"
};
static constexpr uint8_t MAIN_COUNT = 4;

// ---------------------------------------------------------------------------
// Task wrappers

void ldPollWrapper()    { localDisplay.poll(); }
void ldEncoderWrapper() { localDisplay.pollEncoder(); }

// ---------------------------------------------------------------------------
// pollEncoder() — runs every 2ms via its own task
// Uses falling-edge detection on CLK + micros() debounce to suppress bounce.
// Writes to _encDelta, _btnShort, _btnLong only.

void LocalDisplay::pollEncoder() {
  if (!_ready) return;

  // --- Rotary encoder: falling edge of CLK + micros debounce ---
  uint8_t clk = (uint8_t)digitalRead(LOCAL_DISPLAY_ENCODER_CLK_PIN);
  _encPollCount++;
  if (clk != _lastClkState) _encClkChangeCount++;
  if (clk == LOW && _lastClkState == HIGH) {
    _encFallCount++;
    uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - _encLastStepUs) >= ENC_DEBOUNCE_US) {
      uint8_t dt = (uint8_t)digitalRead(LOCAL_DISPLAY_ENCODER_DT_PIN);
      if (dt == HIGH) { _encDelta++; _encStepCwCount++; }
      else            { _encDelta--; _encStepCcwCount++; }
      _encLastStepUs = nowUs;
    } else {
      _encDebounceDrop++;
    }
  }
  _lastClkState = clk;

  // --- Button debounce ---
  bool pressed = (digitalRead(LOCAL_DISPLAY_ENCODER_BTN_PIN) == LOW);

  #if LOCAL_DISPLAY_DEBUG == ON
    if ((uint8_t)pressed != _dbgBtnRaw) {
      Serial.printf("[BTN] pin %d -> %d\n", LOCAL_DISPLAY_ENCODER_BTN_PIN, (int)pressed);
      _dbgBtnRaw = (uint8_t)pressed;
    }
  #endif

  if (pressed && !_btnHeld) {
    if (_btnPressTime == 0) {
      _btnPressTime = millis();
      #if LOCAL_DISPLAY_DEBUG == ON
        Serial.printf("[BTN] press start\n");
      #endif
    } else if (millis() - _btnPressTime > BTN_DEBOUNCE_MS) {
      _btnHeld = true;
      #if LOCAL_DISPLAY_DEBUG == ON
        Serial.printf("[BTN] debounce confirmed\n");
      #endif
    }
  }

  if (!pressed && _btnHeld) {
    uint32_t held = millis() - _btnPressTime;
    if (held >= BTN_LONG_PRESS_MS) {
      _btnLong = true;
      #if LOCAL_DISPLAY_DEBUG == ON
        Serial.printf("[BTN] LONG held=%lums\n", (unsigned long)held);
      #endif
    } else {
      _btnShort = true;
      #if LOCAL_DISPLAY_DEBUG == ON
        Serial.printf("[BTN] SHORT held=%lums\n", (unsigned long)held);
      #endif
    }
    _btnHeld      = false;
    _btnPressTime = 0;
  }

  if (!pressed && !_btnHeld) {
    if (_btnPressTime != 0) {
      #if LOCAL_DISPLAY_DEBUG == ON
        _dbgBounceCount++;
        Serial.printf("[BTN] bounce/abandon after %lums total=%u\n",
                      (unsigned long)(millis() - _btnPressTime), _dbgBounceCount);
      #endif
      _btnPressTime = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// Consume pending encoder and button events (called at start of poll)

static int _readDelta(volatile int &delta) {
  int d = delta;
  delta = 0;
  return d;
}

static bool _consumeShort(volatile bool &flag) { bool v = flag; flag = false; return v; }
static bool _consumeLong (volatile bool &flag) { bool v = flag; flag = false; return v; }

// ---------------------------------------------------------------------------
// logEncoderDiag — periodic encoder + button diagnostics (LOCAL_DISPLAY_DEBUG only)

void LocalDisplay::logEncoderDiag(int delta, bool shortPress, bool longPress) {
  #if LOCAL_DISPLAY_DEBUG == ON
    if (shortPress) _btnShortCount++;
    if (longPress)  _btnLongCount++;

    uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - _encDiagLastMs) < ENCODER_DIAG_MS) return;
    _encDiagLastMs = nowMs;

    VF("DBG: LocalDisplay Enc clk="); V(digitalRead(LOCAL_DISPLAY_ENCODER_CLK_PIN));
    VF(" dt="); V(digitalRead(LOCAL_DISPLAY_ENCODER_DT_PIN));
    VF(" btn="); V(digitalRead(LOCAL_DISPLAY_ENCODER_BTN_PIN));
    VF(" | polls="); V(_encPollCount);
    VF(" clkChg="); V(_encClkChangeCount);
    VF(" fall="); V(_encFallCount);
    VF(" drop="); V(_encDebounceDrop);
    VF(" +/-="); V(_encStepCwCount); VF("/"); V(_encStepCcwCount);
    VF(" delta="); V(delta);
    VF(" S/L="); V(_btnShortCount); VF("/"); VL(_btnLongCount);
  #else
    (void)delta; (void)shortPress; (void)longPress;
  #endif
}

// ---------------------------------------------------------------------------
// drawHeader — title left, HH:MM:SS right, separator below

void LocalDisplay::drawHeader(const char *title) {
  char timeStr[9] = "--:--:--";
  if (site.isDateTimeReady()) {
    JulianDate now = site.getDateTime();
    double localHour = now.hour - site.location.timezone;
    while (localHour >= 24.0) localHour -= 24.0;
    while (localHour <   0.0) localHour += 24.0;
    int h = (int)localHour;
    int m = (int)((localHour - h) * 60.0);
    int s = (int)(((localHour - h) * 60.0 - m) * 60.0);
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", h, m, s);
  }
  _u8g2.setFont(u8g2_font_6x10_tf);
  _u8g2.drawStr(0, 9, title);
  _u8g2.drawStr(80, 9, timeStr);   // 8 chars × 6px = 48px → start at 128-48=80
  _u8g2.drawHLine(0, 11, 128);
}

// ---------------------------------------------------------------------------
// drawMenu — up to 4 items visible, cursor ">" on selected
// Items start at y=22, spaced 12px — clear of the header separator at y=11.

void LocalDisplay::drawMenu(const char * const *items, uint8_t count,
                             uint8_t selected) {
  _u8g2.setFont(u8g2_font_6x10_tf);
  uint8_t topItem = (selected >= 4) ? selected - 3 : 0;
  for (uint8_t i = 0; i < 4 && (topItem + i) < count; i++) {
    uint8_t idx = topItem + i;
    int y = 22 + (int)i * 12;
    _u8g2.drawStr(0, y, (idx == selected) ? ">" : " ");
    _u8g2.drawStr(8, y, items[idx]);
  }
}

// ---------------------------------------------------------------------------
// drawTargetIcon — simple icon per target type, centred at (20, 35)

	static const unsigned char moon_bits[] U8X8_PROGMEM = {
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x80, 0xFF, 0x01, 0x00, 0x00, 0xF0, 0xFF, 0x0F, 0x00,
	  0x00, 0xFC, 0xDF, 0x3F, 0x00, 0x00, 0x8F, 0xFF, 0xFF, 0x00,
	  0x80, 0xC3, 0xDF, 0xFF, 0x01, 0xC0, 0xC1, 0xFC, 0xDF, 0x03,
	  0xE0, 0xCC, 0xFC, 0x9F, 0x07, 0x70, 0xAC, 0xFF, 0xFC, 0x07,
	  0x30, 0x30, 0xFF, 0xFB, 0x0F, 0x38, 0x00, 0xFF, 0xBB, 0x0F,
	  0x18, 0x80, 0xEF, 0xFF, 0x1F, 0x1C, 0xD0, 0xFF, 0xFF, 0x3F,
	  0x8C, 0xB0, 0xFF, 0xFF, 0x3F, 0x0C, 0xF0, 0xF9, 0xFF, 0x3E,
	  0x06, 0x43, 0xF3, 0xFF, 0x7E, 0x06, 0x80, 0xF3, 0xFF, 0x77,
	  0x06, 0x80, 0xFB, 0x7F, 0x77, 0x06, 0x08, 0xF9, 0x7F, 0x77,
	  0x06, 0x08, 0xFE, 0x7F, 0x7C, 0x06, 0x40, 0xBE, 0xFE, 0x7F,
	  0x46, 0x58, 0xDA, 0xFD, 0x7F, 0x0E, 0x14, 0xDD, 0x6D, 0x7E,
	  0x0C, 0x22, 0xDC, 0xBD, 0x3D, 0x0C, 0x22, 0x3A, 0xBE, 0x35,
	  0x5C, 0x1C, 0xC6, 0xEF, 0x1F, 0x18, 0x00, 0x30, 0xB9, 0x1F,
	  0xB8, 0x80, 0x48, 0x9E, 0x0F, 0x30, 0x00, 0x80, 0xC9, 0x0E,
	  0x60, 0x00, 0x80, 0xC0, 0x06, 0xE0, 0x84, 0x20, 0x03, 0x03,
	  0xC0, 0x81, 0x00, 0xC7, 0x03, 0x80, 0x03, 0x80, 0xE5, 0x00,
	  0x00, 0x0F, 0x20, 0x78, 0x00, 0x00, 0x7C, 0x00, 0x3E, 0x00,
	  0x00, 0xF8, 0xFF, 0x0F, 0x00, 0x00, 0x80, 0xFF, 0x01, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};

	static const unsigned char jupiter_bits[] U8X8_PROGMEM = {
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0x01, 0x00,
	  0x00, 0xF0, 0xFF, 0x1F, 0x00, 0x00, 0x7C, 0x00, 0x3C, 0x00,
	  0x00, 0x0F, 0x9C, 0xF1, 0x00, 0x80, 0xA3, 0x7E, 0xD7, 0x01,
	  0xC0, 0xF9, 0x7E, 0x3E, 0x03, 0x60, 0x22, 0xFF, 0xFF, 0x07,
	  0x70, 0xF3, 0xFF, 0x7F, 0x0F, 0x38, 0x00, 0xC0, 0x30, 0x18,
	  0x18, 0xEE, 0xFF, 0xFF, 0x3F, 0x0C, 0xFF, 0xFF, 0xFF, 0x30,
	  0x0C, 0x00, 0x40, 0x10, 0x30, 0xC6, 0xFF, 0xFF, 0xFF, 0x7F,
	  0xE6, 0xFF, 0xFF, 0xFF, 0x7F, 0x86, 0xF3, 0xFF, 0x1B, 0xEB,
	  0x02, 0x00, 0x00, 0x18, 0xC0, 0x02, 0x00, 0x18, 0x00, 0xC0,
	  0x02, 0x00, 0x30, 0x00, 0xC0, 0xE2, 0xFF, 0xF7, 0xFF, 0xFF,
	  0xF2, 0xFF, 0xFF, 0xFF, 0xFF, 0xF2, 0xFF, 0xFF, 0xFF, 0xFF,
	  0xE6, 0xFF, 0xFF, 0xFF, 0xFF, 0xC6, 0xF0, 0xFF, 0x81, 0xFF,
	  0x06, 0x80, 0x30, 0x38, 0x62, 0x06, 0x00, 0x00, 0x7C, 0x68,
	  0x0C, 0x00, 0x00, 0xFE, 0x20, 0xCC, 0xFF, 0x13, 0xF3, 0x32,
	  0xD8, 0xFF, 0x7F, 0xD8, 0x33, 0x18, 0xFF, 0xFF, 0xC1, 0x1B,
	  0x30, 0xF6, 0xFF, 0xFF, 0x1F, 0x60, 0xF0, 0xF7, 0xFF, 0x0E,
	  0xE0, 0x80, 0xF7, 0x1E, 0x07, 0xC0, 0x21, 0xDC, 0xFE, 0x03,
	  0x80, 0x07, 0xBC, 0xE3, 0x01, 0x00, 0x1E, 0x00, 0x70, 0x00,
	  0x00, 0xF8, 0x00, 0x3F, 0x00, 0x00, 0xF0, 0xFF, 0x0F, 0x00,
	  0x00, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};

	static const unsigned char saturn_bits[] U8X8_PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x7F, 0x00, 0x00, 0xFC, 0xE1, 0x61,
  0x00, 0x80, 0x07, 0x3F, 0xDE, 0x00, 0xE0, 0x00, 0xDC, 0x5F,
  0x00, 0x70, 0xFE, 0x61, 0x4C, 0x00, 0x98, 0xFF, 0x07, 0x6C,
  0x00, 0xCC, 0xFF, 0x07, 0x36, 0x00, 0xC6, 0xFF, 0x17, 0x17,
  0x00, 0xE3, 0xFF, 0x9B, 0x1B, 0x00, 0xE1, 0xFF, 0xCB, 0x0D,
  0x80, 0xF1, 0xFF, 0xC7, 0x06, 0x80, 0xF9, 0xFF, 0x73, 0x02,
  0x80, 0x7C, 0xFF, 0x31, 0x02, 0xC0, 0x84, 0xFF, 0x1C, 0x02,
  0xC0, 0xE0, 0x7F, 0x0E, 0x02, 0xC0, 0xFC, 0x9F, 0x63, 0x02,
  0xC0, 0xFE, 0xCF, 0x31, 0x03, 0xC0, 0x80, 0xE7, 0x38, 0x03,
  0x60, 0xF0, 0x39, 0x16, 0x01, 0x30, 0x39, 0x1C, 0x9E, 0x01,
  0xD8, 0x00, 0x07, 0xCC, 0x01, 0x4C, 0xC0, 0xE1, 0xC1, 0x00,
  0x24, 0xF0, 0x10, 0x70, 0x00, 0x36, 0x3E, 0x40, 0x38, 0x00,
  0xFA, 0x07, 0x00, 0x0C, 0x00, 0xFB, 0x71, 0x80, 0x07, 0x00,
  0x03, 0xFC, 0xFF, 0x03, 0x00, 0xFE, 0x07, 0x7F, 0x00, 0x00,
  0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	
	static const unsigned char mars_bits[] U8X8_PROGMEM = {
  0x00, 0xF0, 0xFF, 0x01, 0x00, 0x00, 0xF8, 0xFE, 0x0F, 0x00,
  0x00, 0x1E, 0x7E, 0x3C, 0x00, 0x80, 0xC7, 0xFF, 0xF9, 0x00,
  0xC0, 0xF1, 0x7F, 0xEE, 0x01, 0xE0, 0xFC, 0xFF, 0x98, 0x03,
  0x70, 0xFC, 0xFF, 0x01, 0x07, 0x38, 0xF0, 0xFF, 0x00, 0x0E,
  0x18, 0x80, 0x3F, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x1D,
  0x0E, 0x00, 0x00, 0x00, 0x18, 0x06, 0x00, 0x80, 0x01, 0x36,
  0x03, 0x00, 0x00, 0x60, 0x76, 0x03, 0x00, 0x00, 0xCA, 0x7E,
  0x03, 0x00, 0x20, 0xFC, 0x7F, 0x83, 0x11, 0xC0, 0x39, 0xFF,
  0x91, 0x7D, 0xE0, 0xFF, 0xFF, 0x01, 0xDE, 0xFC, 0xFF, 0xFF,
  0x29, 0xDF, 0xFF, 0xFF, 0xF3, 0xA1, 0xFF, 0xFD, 0xFF, 0xF7,
  0x89, 0xFF, 0xFE, 0xFF, 0xF7, 0x81, 0xFD, 0xFF, 0xFF, 0xDF,
  0x81, 0xFF, 0x9F, 0xFF, 0xCF, 0x93, 0xFF, 0xCF, 0xFF, 0xEB,
  0x83, 0x7F, 0xCE, 0xFD, 0x6F, 0x03, 0xFF, 0xE7, 0xFC, 0x6E,
  0x03, 0xE6, 0xE0, 0xFC, 0x66, 0xC7, 0x79, 0xC0, 0xE7, 0x70,
  0x06, 0x24, 0x80, 0x03, 0x30, 0x0E, 0x00, 0xC0, 0x11, 0x3A,
  0x0C, 0x00, 0x80, 0x00, 0x18, 0x1C, 0x00, 0x00, 0x0C, 0x1C,
  0x38, 0x00, 0x00, 0x20, 0x0E, 0x70, 0x10, 0x00, 0x00, 0x07,
  0xE0, 0x00, 0x82, 0x81, 0x03, 0xC0, 0x01, 0x00, 0xE0, 0x01,
  0x80, 0x07, 0x00, 0xF0, 0x00, 0x00, 0x1E, 0x00, 0x3C, 0x00,
  0x00, 0xFC, 0x80, 0x1F, 0x00, 0x00, 0xF0, 0xFF, 0x07, 0x00,
};

static const unsigned char mercury_bits[] U8X8_PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0xFF, 0x01, 0x00, 0x00, 0xF0, 0xFF, 0x0F, 0x00,
  0x00, 0x7C, 0xE0, 0x3F, 0x00, 0x00, 0x0F, 0xFC, 0x7F, 0x00,
  0x80, 0x03, 0xFE, 0xFF, 0x00, 0xC0, 0x01, 0xF1, 0xFF, 0x03,
  0xE0, 0x00, 0xC0, 0xFF, 0x07, 0x70, 0x00, 0x80, 0xFF, 0x07,
  0x30, 0x10, 0x00, 0xDF, 0x0F, 0x38, 0x00, 0x00, 0xDF, 0x1F,
  0x18, 0x00, 0x20, 0xE3, 0x1F, 0x0C, 0x00, 0xE0, 0x41, 0x1F,
  0x0C, 0x00, 0xC0, 0xC1, 0x3F, 0x0E, 0x08, 0xF1, 0x80, 0x3F,
  0x06, 0x80, 0xE3, 0x81, 0x73, 0x06, 0xE0, 0x17, 0x87, 0x63,
  0x06, 0xC0, 0xD7, 0x0E, 0x63, 0x06, 0xC0, 0xE3, 0x03, 0x66,
  0x16, 0x00, 0xF1, 0x07, 0x66, 0x06, 0x02, 0xEC, 0x07, 0x6C,
  0x06, 0x00, 0xDE, 0x47, 0x70, 0x06, 0x00, 0xFF, 0x7F, 0x70,
  0x0E, 0x02, 0xFC, 0xFF, 0x30, 0x1C, 0x03, 0xFC, 0xEF, 0x38,
  0x4C, 0x00, 0xF0, 0xFF, 0x38, 0x7C, 0x20, 0xFC, 0xFF, 0x1B,
  0x58, 0x20, 0xF8, 0x9F, 0x1F, 0xB8, 0x42, 0xFE, 0xDF, 0x0E,
  0xF0, 0xE3, 0xFF, 0xFF, 0x0F, 0xE0, 0xB7, 0xFF, 0xFF, 0x07,
  0xE0, 0xFF, 0xFF, 0xFF, 0x03, 0xC0, 0xF7, 0xFF, 0xFF, 0x01,
  0x00, 0xDF, 0xFF, 0xFF, 0x00, 0x00, 0xFE, 0xFF, 0x7F, 0x00,
  0x00, 0xFC, 0xFF, 0x1F, 0x00, 0x00, 0xF0, 0xFF, 0x0F, 0x00,
  0x00, 0x80, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char venus_bits[] U8X8_PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0x00, 0x00,
  0x00, 0xF0, 0xFF, 0x0F, 0x00, 0x00, 0x7C, 0xF8, 0x3F, 0x00,
  0x00, 0x0F, 0xFF, 0x7F, 0x00, 0x80, 0x83, 0xFF, 0xFF, 0x00,
  0xC0, 0xC1, 0xFF, 0xFF, 0x01, 0xE0, 0xE0, 0xFF, 0xFF, 0x03,
  0x70, 0xF0, 0xFF, 0xFF, 0x07, 0x30, 0xF8, 0xFF, 0xFF, 0x0F,
  0x18, 0xF8, 0xFF, 0xFF, 0x0F, 0x18, 0xFC, 0xFF, 0xFF, 0x1F,
  0x0C, 0xFC, 0xFF, 0xFF, 0x1F, 0x0E, 0xFE, 0xFF, 0xFF, 0x3F,
  0x06, 0xFE, 0xFF, 0xFF, 0x3F, 0x06, 0xFE, 0xFF, 0xFF, 0x3F,
  0x06, 0xFE, 0xFF, 0xFF, 0x7F, 0x06, 0xFF, 0xFF, 0xFF, 0x7F,
  0x06, 0xFE, 0xFF, 0xFF, 0x7F, 0x06, 0xFE, 0xFF, 0xFF, 0x7F,
  0x06, 0xFE, 0xFF, 0xFF, 0x7F, 0x06, 0xFE, 0xFF, 0xFF, 0x7F,
  0x06, 0xFC, 0xFF, 0xFF, 0x7F, 0x06, 0xFC, 0xFF, 0xFF, 0x3F,
  0x06, 0xF8, 0xFF, 0xFF, 0x3F, 0x0C, 0xF8, 0xFF, 0xFF, 0x3F,
  0x0C, 0xF8, 0xFF, 0xFF, 0x1F, 0x18, 0xF0, 0xFF, 0xFF, 0x1F,
  0x18, 0xE0, 0xFF, 0xFF, 0x0F, 0x30, 0xC0, 0xFF, 0xFF, 0x0F,
  0x70, 0x80, 0xFF, 0xFF, 0x07, 0xE0, 0x00, 0xFF, 0xFF, 0x07,
  0xC0, 0x01, 0xFE, 0xFF, 0x03, 0x80, 0x03, 0xFC, 0xFF, 0x01,
  0x00, 0x07, 0xE0, 0xFF, 0x00, 0x00, 0x1E, 0x00, 0x3C, 0x00,
  0x00, 0xF8, 0x80, 0x1F, 0x00, 0x00, 0xF0, 0xFF, 0x07, 0x00,
  0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
	
	static const unsigned char neptune_bits[] U8X8_PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0xFF, 0x03, 0x00, 0x00, 0xF0, 0xFF, 0x1F, 0x00,
  0x00, 0xFC, 0x00, 0x7E, 0x00, 0x00, 0x0F, 0xF0, 0xE0, 0x01,
  0x80, 0x07, 0xC0, 0xCF, 0x03, 0xC0, 0x01, 0x00, 0x10, 0x07,
  0xE0, 0x00, 0xF8, 0x0F, 0x0E, 0x70, 0x80, 0xFF, 0xFF, 0x1C,
  0x38, 0xF0, 0xFF, 0xFF, 0x19, 0x18, 0x1E, 0x00, 0xF8, 0x33,
  0x8C, 0x03, 0x00, 0xD8, 0x73, 0x4C, 0x80, 0xFF, 0xFF, 0x65,
  0x26, 0xF0, 0xFF, 0xFF, 0x67, 0x06, 0xFE, 0xFF, 0xFF, 0xEF,
  0x07, 0xFF, 0xFF, 0xFF, 0xCF, 0x03, 0xFF, 0xFF, 0xFF, 0xCF,
  0x03, 0xFF, 0xFF, 0xFF, 0xCF, 0x03, 0xFE, 0xFF, 0xFF, 0xCF,
  0x03, 0xF8, 0xFF, 0xFF, 0xC3, 0x03, 0x00, 0xFE, 0xFF, 0xC8,
  0x07, 0x00, 0xE0, 0x07, 0xCE, 0x06, 0x00, 0x00, 0xE0, 0xEF,
  0x06, 0x01, 0x00, 0xFE, 0x63, 0x0E, 0x00, 0xF0, 0xFF, 0x60,
  0x0C, 0xF0, 0xFF, 0x3F, 0x70, 0x1C, 0x00, 0xFE, 0x07, 0x31,
  0x38, 0x00, 0x00, 0x80, 0x18, 0x30, 0x00, 0x00, 0x40, 0x1C,
  0xF0, 0x00, 0x00, 0x0A, 0x0E, 0xE0, 0x01, 0x00, 0x00, 0x07,
  0xC0, 0x03, 0x00, 0x80, 0x03, 0x00, 0x07, 0x00, 0xC0, 0x01,
  0x00, 0x1E, 0x00, 0xF0, 0x00, 0x00, 0xF8, 0xFF, 0x3F, 0x00,
  0x00, 0xF0, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0xF9, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char uranus_bits[] U8X8_PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x7F, 0x00, 0x00, 0x78, 0xE0, 0xFF, 0x07, 0x00,
  0xFC, 0xF9, 0x80, 0x1F, 0x00, 0x86, 0x1F, 0x00, 0x3C, 0x00,
  0x33, 0x06, 0xFF, 0x70, 0x00, 0xF3, 0xC0, 0xFF, 0xC7, 0x01,
  0x93, 0xE0, 0xFF, 0x9F, 0x03, 0xD3, 0xF0, 0xFF, 0x3F, 0x03,
  0x36, 0xF0, 0xFF, 0x7F, 0x06, 0x36, 0xF8, 0xFF, 0xFF, 0x0C,
  0x64, 0xF8, 0xFF, 0xFF, 0x0C, 0x6C, 0xFC, 0xFF, 0xFF, 0x19,
  0xE6, 0xFC, 0xFF, 0xFF, 0x19, 0xC6, 0xFC, 0xFF, 0xFF, 0x39,
  0xC7, 0xF9, 0xFF, 0xFF, 0x33, 0xA3, 0xF3, 0xFF, 0xFF, 0x33,
  0x03, 0xE7, 0xFF, 0xFF, 0x32, 0x03, 0xE6, 0xFF, 0xFF, 0x33,
  0x03, 0xCE, 0xFF, 0xFF, 0x33, 0x83, 0x9C, 0xFF, 0xFF, 0x31,
  0x07, 0x38, 0xFF, 0xFF, 0x31, 0x26, 0x72, 0xFE, 0xFF, 0x31,
  0x26, 0xFE, 0xFC, 0xFF, 0x31, 0x0E, 0xE2, 0xF9, 0xFF, 0x30,
  0x0C, 0xD8, 0xF3, 0x3F, 0x30, 0x1C, 0xB8, 0xC7, 0x1F, 0x18,
  0x18, 0x00, 0x1E, 0x00, 0x30, 0x38, 0x80, 0x3C, 0x00, 0x30,
  0x70, 0x00, 0xF8, 0x00, 0x64, 0xE0, 0x80, 0xE0, 0x01, 0x64,
  0xC0, 0x01, 0xC0, 0x07, 0x65, 0x80, 0x07, 0x00, 0x1F, 0x64,
  0x00, 0x0F, 0x00, 0xFC, 0x67, 0x00, 0x7C, 0x80, 0x7F, 0x30,
  0x00, 0xF0, 0xFF, 0xE7, 0x3D, 0x00, 0x80, 0xFF, 0xC0, 0x1F,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
	
static const unsigned char earth_bits[] U8X8_PROGMEM = {
  0x00, 0xF8, 0xFF, 0x1F, 0x00, 0x00, 0x7C, 0x00, 0x3E, 0x00,
  0x00, 0x0F, 0x7F, 0xF0, 0x00, 0xC0, 0x83, 0xFD, 0xC0, 0x03,
  0xE0, 0x29, 0x78, 0x90, 0x07, 0x70, 0x28, 0x78, 0x00, 0x0E,
  0x38, 0xE0, 0x3C, 0x40, 0x1C, 0x18, 0x97, 0x9C, 0xE0, 0x1F,
  0xCC, 0x1F, 0x0C, 0xB0, 0x3F, 0xCC, 0x63, 0x00, 0x20, 0x7F,
  0xC6, 0xE3, 0x00, 0xE6, 0x7F, 0xE7, 0x77, 0x00, 0xFC, 0xFF,
  0xF3, 0x3F, 0x01, 0xF8, 0xF9, 0xFB, 0xFF, 0x01, 0xDC, 0xFB,
  0xF9, 0x7F, 0x00, 0x86, 0xFE, 0xFD, 0x1F, 0x00, 0x04, 0xF8,
  0xF9, 0x0F, 0x00, 0x3E, 0xFC, 0xF1, 0x0F, 0x00, 0xFF, 0xDF,
  0xF1, 0x06, 0x06, 0xFF, 0x9F, 0x61, 0x02, 0x80, 0xFF, 0xFF,
  0x61, 0x06, 0x80, 0xFF, 0x8F, 0xE1, 0x09, 0x80, 0xFF, 0xAF,
  0xC1, 0x03, 0x80, 0xFF, 0x9F, 0x0B, 0x3F, 0x00, 0xFF, 0x9F,
  0x13, 0xFC, 0x00, 0xE0, 0xCF, 0x03, 0xFC, 0x01, 0xE0, 0xC7,
  0x07, 0xFC, 0x07, 0xE0, 0xC7, 0x06, 0xFC, 0x1F, 0xC0, 0x63,
  0x06, 0xFC, 0x0F, 0xE0, 0x73, 0x0C, 0xF8, 0x0F, 0xE0, 0x35,
  0x1C, 0xF0, 0x0F, 0xE0, 0x39, 0x38, 0xE0, 0x07, 0xE0, 0x18,
  0x70, 0xE0, 0x03, 0x60, 0x0C, 0xE0, 0xE0, 0x00, 0x00, 0x07,
  0xC0, 0x61, 0x00, 0x80, 0x03, 0x80, 0x27, 0x00, 0xC0, 0x01,
  0x00, 0x3F, 0x00, 0xF0, 0x00, 0x00, 0xFC, 0x00, 0x3F, 0x00,
  0x00, 0xF0, 0xFF, 0x0F, 0x00, 0x00, 0x80, 0xFF, 0x01, 0x00,
};
	
void LocalDisplay::drawTargetIcon(LdShape shape) {
  _u8g2.setDrawColor(1);
  switch (shape) {

    case SHAPE_MOON:
	  _u8g2.drawXBM(1, 15, 40, 40, moon_bits);
      break;

    case SHAPE_DISC:
      _u8g2.drawDisc(20, 35, 13, U8G2_DRAW_ALL);
      break;

    case SHAPE_BANDS:
      _u8g2.drawCircle(20, 35, 16, U8G2_DRAW_ALL);
      _u8g2.drawHLine(5, 30, 30);
      _u8g2.drawHLine(5, 33, 30);
      _u8g2.drawHLine(5, 37, 30);
      _u8g2.drawHLine(5, 40, 30);
      break;
	  
    case SHAPE_JUPITER:
	  _u8g2.drawXBM(1, 15, 40, 40, jupiter_bits);
      break;
	  
	case SHAPE_MARS:
	  _u8g2.drawXBM(1, 15, 40, 40, mars_bits);
      break;
	  
	case SHAPE_URANUS:
	  _u8g2.drawXBM(1, 15, 40, 40, uranus_bits);
      break;
	  
	case SHAPE_MERCURY:
	  _u8g2.drawXBM(1, 15, 40, 40, mercury_bits);
      break;
	
	case SHAPE_NEPTUNE:
	  _u8g2.drawXBM(1, 15, 40, 40, neptune_bits);
      break;
	
	case SHAPE_EARTH:
	  _u8g2.drawXBM(1, 15, 40, 40, earth_bits);
      break;

    case SHAPE_SATURN:
	  _u8g2.drawXBM(1, 15, 40, 40, saturn_bits);
      break;
	  
	case SHAPE_VENUS:
	  _u8g2.drawXBM(1, 15, 40, 40, venus_bits);
      break;
  }
}

// ---------------------------------------------------------------------------
// drawPointing — icon left, facts right, optional slew bar at bottom

void LocalDisplay::drawPointing() {
  const LdTarget &tgt = _targets[_targetIdx];
  bool slewing = mount.isSlewing();

  drawHeader(tgt.name);
  drawTargetIcon(tgt.shape);

  // Facts (right panel x=44) — drawUTF8 to render ± correctly
  _u8g2.setFont(u8g2_font_5x7_tf);
  _u8g2.drawUTF8(44, 24, tgt.fact[0]);
  _u8g2.drawUTF8(44, 35, tgt.fact[1]);
  _u8g2.drawUTF8(44, 46, tgt.fact[2]);

  // Slew bar — indeterminate bouncing segment
  if (slewing) {
    _barPos += _barDir * 3;
    if (_barPos + 30 >= 126 || _barPos <= 1) _barDir = -_barDir;
    _u8g2.setDrawColor(1);
    _u8g2.drawFrame(0, 57, 128, 7);
    _u8g2.drawBox(_barPos, 58, 30, 5);
  }
}

// ---------------------------------------------------------------------------
// drawModal — "Move to X?" overlay on top of the pointing screen

void LocalDisplay::drawModal() {
  // Clear a box and draw a framed dialog
  _u8g2.setDrawColor(0);
  _u8g2.drawBox(4, 16, 120, 44);
  _u8g2.setDrawColor(1);
  _u8g2.drawFrame(4, 16, 120, 44);
  _u8g2.setFont(u8g2_font_6x10_tf);
  _u8g2.drawStr(10, 28, "Move to:");
  _u8g2.drawStr(10, 42, _targets[_modalSel].name);
  _u8g2.setFont(u8g2_font_5x7_tf);
  _u8g2.drawStr(10, 56, "OK=press  cancel=hold");
}

// ---------------------------------------------------------------------------
// drawStub — shown for unimplemented menu options

void LocalDisplay::drawStub(const char *label) {
  drawHeader(label);
  _u8g2.setFont(u8g2_font_6x10_tf);
  _u8g2.drawStr(4, 36, "Coming soon...");
  _u8g2.drawStr(4, 52, "[press to go back]");
}

// ---------------------------------------------------------------------------
// Settings sub-menu items

static const char * const _settingsItems[] = { "Locations", "Date & Time", "Back" };
static constexpr uint8_t SETTINGS_COUNT = 3;

// ---------------------------------------------------------------------------
// drawField — render a value string with highlight if active (5×7 font assumed)

void LocalDisplay::drawField(const char *str, int16_t x, int16_t y, bool active) {
  int16_t w = (int16_t)_u8g2.getStrWidth(str) + 2;
  if (active) {
    _u8g2.setDrawColor(1);
    _u8g2.drawBox(x - 1, y - 7, w, 9);
    _u8g2.setDrawColor(0);
    _u8g2.drawStr(x, y, str);
    _u8g2.setDrawColor(1);
  } else {
    _u8g2.drawStr(x, y, str);
  }
}

// ---------------------------------------------------------------------------
// enterLocEdit — load site siteIdx into _locFields (DMS) for editing

void LocalDisplay::enterLocEdit(uint8_t siteIdx) {
  _editSiteIdx = siteIdx;
  _editField   = 0;

  Location loc = {};
  if (siteIdx == _activeSiteIdx) {
    loc = site.location;
  } else {
    char key[24];
    snprintf(key, sizeof(key), "LOCATION%u_SETTINGS", siteIdx);
    nv().kv().getOrInit(key, loc);
  }

  auto toFields = [](double radians, int16_t *flds, int maxDeg) {
    double deg = fabs(radians * 180.0 / M_PI);
    int d = (int)deg;
    double rm = (deg - d) * 60.0;
    int m = (int)rm;
    int s = (int)round((rm - m) * 60.0);
    if (s >= 60) { s = 0; m++; }
    if (m >= 60) { m = 0; d++; }
    if (d > maxDeg) d = maxDeg;
    flds[0] = d; flds[1] = m; flds[2] = s;
  };

  toFields(loc.latitude,  _locFields,     90);
  _locFields[3] = (loc.latitude  < 0) ? 1 : 0;  // 0=N, 1=S
  toFields(loc.longitude, _locFields + 4, 180);
  _locFields[7] = (loc.longitude < 0) ? 1 : 0;  // 0=E, 1=W
  _locFields[8] = (int16_t)round(loc.timezone);
}

// ---------------------------------------------------------------------------
// saveLocEdit — write _locFields back to NV and apply if active site

void LocalDisplay::saveLocEdit() {
  Location loc = {};
  if (_editSiteIdx == _activeSiteIdx) {
    loc = site.location;
  } else {
    char key[24];
    snprintf(key, sizeof(key), "LOCATION%u_SETTINGS", _editSiteIdx);
    nv().kv().getOrInit(key, loc);
  }

  double latDeg = _locFields[0] + _locFields[1] / 60.0 + _locFields[2] / 3600.0;
  if (_locFields[3] == 1) latDeg = -latDeg;
  double lonDeg = _locFields[4] + _locFields[5] / 60.0 + _locFields[6] / 3600.0;
  if (_locFields[7] == 1) lonDeg = -lonDeg;

  loc.latitude  = latDeg * M_PI / 180.0;
  loc.longitude = lonDeg * M_PI / 180.0;
  loc.timezone  = (float)_locFields[8];

  char key[24];
  snprintf(key, sizeof(key), "LOCATION%u_SETTINGS", _editSiteIdx);
  nv().kv().put(key, loc);
  VF("MSG: LocalDisplay, saved location "); VL(_editSiteIdx);

  if (_editSiteIdx == _activeSiteIdx) {
    site.location = loc;
    site.updateLocation();
  }
}

// ---------------------------------------------------------------------------
// enterDateTimeEdit — populate _dtFields from current site time (local)

void LocalDisplay::enterDateTimeEdit() {
  _editField = 0;
  if (site.isDateTimeReady()) {
    JulianDate jd       = site.getDateTime();
    GregorianDate greg  = calendars.julianDayToGregorian(jd);
    double localHour    = greg.hour - site.location.timezone;
    while (localHour >= 24.0) localHour -= 24.0;
    while (localHour <   0.0) localHour += 24.0;
    _dtFields[0] = (int16_t)localHour;
    _dtFields[1] = (int16_t)((localHour - _dtFields[0]) * 60.0);
    _dtFields[2] = greg.day;
    _dtFields[3] = greg.month;
    _dtFields[4] = greg.year;
  } else {
    _dtFields[0] = 12; _dtFields[1] = 0;
    _dtFields[2] = 1;  _dtFields[3] = 1; _dtFields[4] = 2024;
  }
}

// ---------------------------------------------------------------------------
// saveDateTimeEdit — build JulianDate from _dtFields and apply

void LocalDisplay::saveDateTimeEdit() {
  GregorianDate greg;
  greg.year  = _dtFields[4];
  greg.month = (uint8_t)_dtFields[3];
  greg.day   = (uint8_t)_dtFields[2];
  greg.hour  = _dtFields[0] + _dtFields[1] / 60.0 + site.location.timezone;
  while (greg.hour >= 24.0) greg.hour -= 24.0;
  while (greg.hour <   0.0) greg.hour += 24.0;
  JulianDate jd = calendars.gregorianToJulianDay(greg);
  site.setDateTime(jd);
}

// ---------------------------------------------------------------------------
// drawSettings — top-level settings sub-menu

void LocalDisplay::drawSettings() {
  drawHeader("Settings");
  drawMenu(_settingsItems, SETTINGS_COUNT, _menuSel);
}

// ---------------------------------------------------------------------------
// drawLocList — list of 3 favourite sites + Back

void LocalDisplay::drawLocList() {
  drawHeader("Locations");
  _u8g2.setFont(u8g2_font_6x10_tf);
  for (uint8_t i = 0; i < 3; i++) {
    int y = 22 + (int)i * 12;
    _u8g2.drawStr(0, y, (i == _activeSiteIdx) ? "*" : " ");
    _u8g2.drawStr(8, y, (i == _menuSel) ? ">" : " ");
    _u8g2.drawStr(16, y, _siteNames[i][0] ? _siteNames[i] : "---");
  }
  int y = 22 + 3 * 12;
  _u8g2.drawStr(8, y, (_menuSel == 3) ? ">" : " ");
  _u8g2.drawStr(16, y, "Back");
  _u8g2.setFont(u8g2_font_5x7_tf);
  _u8g2.drawStr(0, 62, "hold=edit loc");
}

// ---------------------------------------------------------------------------
// drawLocEdit — field-by-field lat/lon/tz editor
// Fields: 0=lat-d 1=lat-m 2=lat-s 3=lat-NS 4=lon-d 5=lon-m 6=lon-s 7=lon-EW 8=tz 9=Save

void LocalDisplay::drawLocEdit() {
  char siteName[16] = "Edit";
  if (_editSiteIdx == _activeSiteIdx && site.location.name[0])
    strncpy(siteName, site.location.name, 15);
  else
    snprintf(siteName, sizeof(siteName), "Fav %d", _editSiteIdx + 1);
  drawHeader(siteName);

  _u8g2.setFont(u8g2_font_5x7_tf);
  char buf[8];

  // LAT row (y=22)
  _u8g2.drawStr(0, 22, "LAT");
  snprintf(buf, sizeof(buf), "%2d", _locFields[0]);
  drawField(buf, 20, 22, _editField == 0);
  _u8g2.drawStr(32, 22, "\xb0");
  snprintf(buf, sizeof(buf), "%02d", _locFields[1]);
  drawField(buf, 38, 22, _editField == 1);
  _u8g2.drawStr(49, 22, "'");
  snprintf(buf, sizeof(buf), "%02d", _locFields[2]);
  drawField(buf, 55, 22, _editField == 2);
  _u8g2.drawStr(66, 22, "\"");
  buf[0] = (_locFields[3] == 0) ? 'N' : 'S'; buf[1] = '\0';
  drawField(buf, 72, 22, _editField == 3);

  // LON row (y=33)
  _u8g2.drawStr(0, 33, "LON");
  snprintf(buf, sizeof(buf), "%3d", _locFields[4]);
  drawField(buf, 20, 33, _editField == 4);
  _u8g2.drawStr(35, 33, "\xb0");
  snprintf(buf, sizeof(buf), "%02d", _locFields[5]);
  drawField(buf, 41, 33, _editField == 5);
  _u8g2.drawStr(52, 33, "'");
  snprintf(buf, sizeof(buf), "%02d", _locFields[6]);
  drawField(buf, 58, 33, _editField == 6);
  _u8g2.drawStr(69, 33, "\"");
  buf[0] = (_locFields[7] == 0) ? 'E' : 'W'; buf[1] = '\0';
  drawField(buf, 75, 33, _editField == 7);

  // TZ row (y=44)
  _u8g2.drawStr(0, 44, "TZ");
  snprintf(buf, sizeof(buf), "%+3d", _locFields[8]);
  drawField(buf, 20, 44, _editField == 8);

  // Bottom row (y=57)
  if (_editField == 9) {
    drawField("[Save]", 0, 57, true);
    _u8g2.drawStr(42, 57, "hold=cancel");
  } else {
    _u8g2.drawStr(0, 57, "next>  hold=cancel");
  }
}

// ---------------------------------------------------------------------------
// drawDateTimeEdit — field-by-field time/date editor
// Fields: 0=hour 1=min 2=day 3=month 4=year 5=Save

void LocalDisplay::drawDateTimeEdit() {
  drawHeader("Date & Time");
  _u8g2.setFont(u8g2_font_5x7_tf);
  char buf[8];

  // Time row (y=24)
  _u8g2.drawStr(0, 24, "Time");
  snprintf(buf, sizeof(buf), "%02d", _dtFields[0]);
  drawField(buf, 30, 24, _editField == 0);
  _u8g2.drawStr(41, 24, ":");
  snprintf(buf, sizeof(buf), "%02d", _dtFields[1]);
  drawField(buf, 47, 24, _editField == 1);

  // Date row (y=36)
  _u8g2.drawStr(0, 36, "Date");
  snprintf(buf, sizeof(buf), "%02d", _dtFields[2]);
  drawField(buf, 30, 36, _editField == 2);
  _u8g2.drawStr(41, 36, "/");
  snprintf(buf, sizeof(buf), "%02d", _dtFields[3]);
  drawField(buf, 47, 36, _editField == 3);
  _u8g2.drawStr(58, 36, "/");
  snprintf(buf, sizeof(buf), "%4d", _dtFields[4]);
  drawField(buf, 64, 36, _editField == 4);

  // Bottom row (y=57)
  if (_editField == 5) {
    drawField("[Save]", 0, 57, true);
    _u8g2.drawStr(42, 57, "hold=cancel");
  } else {
    _u8g2.drawStr(0, 57, "next>  hold=cancel");
  }
}

// ---------------------------------------------------------------------------
// goToTarget — compute RA/Dec and request slew

void LocalDisplay::goToTarget(uint8_t idx) {
  if (idx >= NUM_TARGETS) return;

  if (!site.isDateTimeReady()) {
    VLF("WRN: LocalDisplay, time not set — skipping GoTo");
    return;
  }

  JulianDate now = site.getDateTime();
  double jd = now.day + now.hour / 24.0;

  double ra, dec;
  _targets[idx].getPos(jd, &ra, &dec);

  Coordinate coord;
  memset(&coord, 0, sizeof(coord));
  coord.r        = ra  * (M_PI / 12.0);
  coord.d        = dec * (M_PI / 180.0);
  coord.pierSide = PIER_SIDE_NONE;

  CommandError err = goTo.request(coord, PSS_BEST, true);
  if (err != CE_NONE) { VF("WRN: LocalDisplay, GoTo error "); VL((int)err); }

  _targetIdx  = idx;
  _wasSlewing = false;
  _screen     = SCR_POINTING;
}

// ---------------------------------------------------------------------------
// earlyInit() — called right after WIRE_INIT in setup(), before telescope.init().
// Shows a "Starting..." screen immediately so the user gets visual feedback.

void LocalDisplay::earlyInit() {
  Wire.beginTransmission(LOCAL_DISPLAY_I2C_ADDR);
  if (Wire.endTransmission() != 0) return;  // no display, stay silent

  if (!_u8g2.begin()) return;
  _u8g2.setContrast(200);
  _earlyInitDone = true;

  _u8g2.firstPage();
  do {
    _u8g2.setFont(u8g2_font_6x10_tf);
    _u8g2.drawStr(0, 9, "OnStep Pointer");
    _u8g2.drawHLine(0, 11, 128);
    _u8g2.drawXBM(44, 14, 40, 40, earth_bits);
    _u8g2.drawStr(4, 60, "Starting...");
  } while (_u8g2.nextPage());
}

// ---------------------------------------------------------------------------
// init()

void LocalDisplay::init() {
  if (!_earlyInitDone) {
    // earlyInit was not called (or display absent) — probe and init now.
    // Wire was already initialised by DS3231 during telescope.init().
    // A second Wire.begin() on ESP32 v3.x tears down the ESP-IDF I2C driver.
    Wire.beginTransmission(LOCAL_DISPLAY_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
      DLF("WRN: LocalDisplay, SSD1306 not found on I2C - display disabled");
      return;
    }
    if (!_u8g2.begin()) {
      DLF("ERR: LocalDisplay, SSD1306 init failed");
      return;
    }
    _u8g2.setContrast(200);
  }

  // Restore last-used favourite site (before splash so lat/lon is correct)
  {
    uint8_t lastSite = 0;
    nv().kv().getOrInit("LD_ACTV_SITE", lastSite);
    if (lastSite > 2) lastSite = 0;
    _activeSiteIdx = lastSite;
    if (lastSite != 0) {
      site.readLocation(lastSite);
      site.updateLocation();
      VF("MSG: LocalDisplay, restored site "); VL(lastSite);
    }
  }

  // Splash screen: show site lat/lon in DMS for 2 seconds
  // \xc2\xb0 = UTF-8 for ° (U+00B0)
  {
    auto toDMS = [](double deg, char posHemi, char negHemi, char *buf, size_t sz) {
      char hemi = (deg >= 0.0) ? posHemi : negHemi;
      double a  = fabs(deg);
      int    d  = (int)a;
      double rm = (a - d) * 60.0;
      int    m  = (int)rm;
      int    s  = (int)round((rm - m) * 60.0);
      if (s >= 60) { s = 0; m++; }
      if (m >= 60) { m = 0; d++; }
      snprintf(buf, sz, "%d\xc2\xb0%d'%d\"%c", d, m, s, hemi);
    };
    char latBuf[20], lonBuf[20];
    toDMS(site.location.latitude  * 180.0 / M_PI, 'N', 'S', latBuf, sizeof(latBuf));
    toDMS(site.location.longitude * 180.0 / M_PI, 'E', 'W', lonBuf, sizeof(lonBuf));
    _u8g2.firstPage();
    do {
      _u8g2.setFont(u8g2_font_6x10_tf);
      _u8g2.drawStr(0, 12, "OnStep Pointer");
      _u8g2.drawHLine(0, 14, 128);
      _u8g2.drawUTF8(0, 32, latBuf);
      _u8g2.drawUTF8(0, 48, lonBuf);
    } while (_u8g2.nextPage());
    delay(2000);
  }

  // Configure encoder pins — INPUT_PULLUP so they read HIGH at rest
  pinMode(LOCAL_DISPLAY_ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(LOCAL_DISPLAY_ENCODER_DT_PIN,  INPUT_PULLUP);
  pinMode(LOCAL_DISPLAY_ENCODER_BTN_PIN, INPUT_PULLUP);
  _lastClkState = (uint8_t)digitalRead(LOCAL_DISPLAY_ENCODER_CLK_PIN);

  #if LOCAL_DISPLAY_DEBUG == ON
    Serial.printf("[LocalDisplay] BTN=%d CLK=%d DT=%d\n",
                  LOCAL_DISPLAY_ENCODER_BTN_PIN,
                  LOCAL_DISPLAY_ENCODER_CLK_PIN,
                  LOCAL_DISPLAY_ENCODER_DT_PIN);
  #endif

  // Fast encoder poll task (2ms, priority 6)
  VF("MSG: LocalDisplay, start encoder task (2ms)... ");
  if (tasks.add(2, 0, true, 6, ldEncoderWrapper, "LdEnc")) { VLF("ok"); }
  else { VLF("FAILED!"); return; }

  // Display poll task (LOCAL_DISPLAY_POLL_MS, priority 7)
  VF("MSG: LocalDisplay, start display task ("); V(LOCAL_DISPLAY_POLL_MS); VF("ms)... ");
  if (tasks.add(LOCAL_DISPLAY_POLL_MS, 0, true, 7, ldPollWrapper, "LdDisp")) { VLF("ok"); }
  else { VLF("FAILED!"); return; }

  _ready = true;
  VLF("MSG: LocalDisplay, ready");

  // Goto-complete LED — configure GPIO now so it's immediately usable
  #if GOTO_LED_PIN != OFF
    pinMode(GOTO_LED_PIN, OUTPUT);
    digitalWrite(GOTO_LED_PIN, LOW);
  #endif

  // Startup slew — schedule 3 s after init so DS3231 and axes are fully settled
  #ifdef LOCAL_DISPLAY_STARTUP_ALT
    _startupSlewHandle = tasks.add(3000, 0, false, 7, ldStartupSlewCb, "LdStrt");
    if (!_startupSlewHandle) { VLF("WRN: LocalDisplay, startup slew task failed"); }
  #endif
}

// ---------------------------------------------------------------------------
// poll() — display update + input handling (runs at LOCAL_DISPLAY_POLL_MS)

void LocalDisplay::poll() {
  if (!_ready) return;

  // Goto-complete LED: detect GS_GOTO → GS_NONE transition
  #if GOTO_LED_PIN != OFF
  {
    bool gotoNowActive = (goTo.state == GS_GOTO);
    if (_wasGotoActive && !gotoNowActive) {
      // cancel any in-progress blink and restart the 10-second burst
      if (_sGotoLedHandle) { tasks.setDurationComplete(_sGotoLedHandle); _sGotoLedHandle = 0; }
      _sLedState = false;
      digitalWrite(GOTO_LED_PIN, LOW);
      _sGotoLedHandle = tasks.add(100, 10000, true, 6, ldGotoLedBlink, "GtoLED");
    }
    _wasGotoActive = gotoNowActive;
  }
  #endif

  // Consume accumulated encoder and button events
  int  delta = _readDelta(_encDelta);
  bool shortPress = _consumeShort(_btnShort);
  bool longPress  = _consumeLong (_btnLong);
  logEncoderDiag(delta, shortPress, longPress);

  // ---- Input handling per screen ----
  // Wrap-around helper: (sel+d+N*bigMultiple) % N is always positive
  #define WRAP(sel, d, N) ((uint8_t)(((int)(sel) + (d) % (int)(N) + (int)(N) * 16) % (int)(N)))

  switch (_screen) {

    case SCR_MAIN_MENU:
      if (delta != 0) _menuSel = WRAP(_menuSel, delta, MAIN_COUNT);
      if (shortPress) {
        switch (_menuSel) {
          case 0: _screen = SCR_MOVE_TO;                             break;
          case 1: _stubTitle = _mainItems[1]; _screen = SCR_STUB;    break;
          case 2: _stubTitle = _mainItems[2]; _screen = SCR_STUB;     break;
          case 3: _screen = SCR_SETTINGS;                            break;
        }
        _menuSel = 0;
      }
      break;

    case SCR_MOVE_TO:
      if (delta != 0) _menuSel = WRAP(_menuSel, delta, NUM_TARGETS);
      if (shortPress) {
        goToTarget(_menuSel);
        _menuSel = 0;
      }
      if (longPress) {
        _screen  = SCR_MAIN_MENU;
        _menuSel = 0;
      }
      break;

    case SCR_POINTING:
      if (_modalActive) {
        if (delta != 0) _modalSel = WRAP(_modalSel, delta, NUM_TARGETS);
        if (shortPress) {
          goToTarget(_modalSel);
          _modalActive = false;
        }
        if (longPress) _modalActive = false;
      } else {
        if (delta != 0) {
          _modalActive = true;
          _modalSel    = WRAP(_targetIdx, delta, NUM_TARGETS);
        }
        if (shortPress || longPress) {
          _screen  = SCR_MAIN_MENU;
          _menuSel = 0;
        }
      }
      break;

    case SCR_STUB:
      if (shortPress || longPress) {
        _screen  = SCR_MAIN_MENU;
        _menuSel = 0;
      }
      break;

    case SCR_SETTINGS:
      if (delta != 0) _menuSel = WRAP(_menuSel, delta, SETTINGS_COUNT);
      if (shortPress) {
        if (_menuSel == 0) {
          // Load site names from NV for list display
          for (uint8_t i = 0; i < 3; i++) {
            Location tempLoc = {};
            char key[24];
            snprintf(key, sizeof(key), "LOCATION%u_SETTINGS", i);
            nv().kv().getOrInit(key, tempLoc);
            strncpy(_siteNames[i], tempLoc.name, 15);
            _siteNames[i][15] = '\0';
          }
          _screen = SCR_LOC_LIST;
          _menuSel = _activeSiteIdx;
        } else if (_menuSel == 1) {
          enterDateTimeEdit();
          _screen = SCR_DATETIME_EDIT;
        } else {
          _screen = SCR_MAIN_MENU;
          _menuSel = 3;
        }
      }
      if (longPress) { _screen = SCR_MAIN_MENU; _menuSel = 3; }
      break;

    case SCR_LOC_LIST:
      if (delta != 0) _menuSel = WRAP(_menuSel, delta, 4);
      if (shortPress) {
        if (_menuSel < 3) {
          _activeSiteIdx = _menuSel;
          uint8_t n = _activeSiteIdx;
          nv().kv().put("LD_ACTV_SITE", n);
          site.readLocation(n);
          site.updateLocation();
          VF("MSG: LocalDisplay, activated site "); VL(n);
        } else {
          _screen = SCR_SETTINGS;
          _menuSel = 0;
        }
      }
      if (longPress) {
        if (_menuSel < 3) {
          enterLocEdit(_menuSel);
          _screen = SCR_LOC_EDIT;
        } else {
          _screen = SCR_SETTINGS;
          _menuSel = 0;
        }
      }
      break;

    case SCR_LOC_EDIT:
      if (delta != 0) {
        switch (_editField) {
          case 0: _locFields[0] = (int16_t)constrain((int)_locFields[0] + delta, 0,   90); break;
          case 1: _locFields[1] = (int16_t)WRAP(_locFields[1], delta, 60);                  break;
          case 2: _locFields[2] = (int16_t)WRAP(_locFields[2], delta, 60);                  break;
          case 3: _locFields[3] = (int16_t)WRAP(_locFields[3], delta, 2);                   break;
          case 4: _locFields[4] = (int16_t)constrain((int)_locFields[4] + delta, 0,  180); break;
          case 5: _locFields[5] = (int16_t)WRAP(_locFields[5], delta, 60);                  break;
          case 6: _locFields[6] = (int16_t)WRAP(_locFields[6], delta, 60);                  break;
          case 7: _locFields[7] = (int16_t)WRAP(_locFields[7], delta, 2);                   break;
          case 8: _locFields[8] = (int16_t)constrain((int)_locFields[8] + delta, -12, 12); break;
        }
      }
      if (shortPress) {
        if (_editField == 9) {
          saveLocEdit();
          // Refresh site names cache
          strncpy(_siteNames[_editSiteIdx],
                  site.location.name[0] ? site.location.name : "", 15);
          _screen = SCR_LOC_LIST;
          _menuSel = _editSiteIdx;
        } else {
          _editField++;
        }
      }
      if (longPress) {
        _screen = SCR_LOC_LIST;
        _menuSel = _editSiteIdx;
      }
      break;

    case SCR_DATETIME_EDIT:
      if (delta != 0) {
        switch (_editField) {
          case 0: _dtFields[0] = (int16_t)WRAP(_dtFields[0], delta, 24);                     break;
          case 1: _dtFields[1] = (int16_t)WRAP(_dtFields[1], delta, 60);                     break;
          case 2: _dtFields[2] = (int16_t)constrain((int)_dtFields[2] + delta, 1,   31);     break;
          case 3: _dtFields[3] = (int16_t)constrain((int)_dtFields[3] + delta, 1,   12);     break;
          case 4: _dtFields[4] = (int16_t)constrain((int)_dtFields[4] + delta, 2020, 2099);  break;
        }
      }
      if (shortPress) {
        if (_editField == 5) {
          saveDateTimeEdit();
          _screen = SCR_SETTINGS;
          _menuSel = 1;
        } else {
          _editField++;
        }
      }
      if (longPress) {
        _screen = SCR_SETTINGS;
        _menuSel = 1;
      }
      break;
  }

  // ---- Draw ----
  _u8g2.firstPage();
  do {
    switch (_screen) {

      case SCR_MAIN_MENU:
        drawHeader("OnStep Pointer");
        drawMenu(_mainItems, MAIN_COUNT, _menuSel);
        break;

      case SCR_MOVE_TO: {
        const char *moveToItems[NUM_TARGETS];
        for (uint8_t i = 0; i < NUM_TARGETS; i++) moveToItems[i] = _targets[i].name;
        drawHeader("Move to...");
        drawMenu(moveToItems, NUM_TARGETS, _menuSel);
        break;
      }

      case SCR_POINTING:
        drawPointing();
        if (_modalActive) drawModal();
        break;

      case SCR_STUB:
        drawStub(_stubTitle);
        break;

      case SCR_SETTINGS:
        drawSettings();
        break;

      case SCR_LOC_LIST:
        drawLocList();
        break;

      case SCR_LOC_EDIT:
        drawLocEdit();
        break;

      case SCR_DATETIME_EDIT:
        drawDateTimeEdit();
        break;
    }
  } while (_u8g2.nextPage());
}

// ---------------------------------------------------------------------------
// Startup slew: auto-goto after boot if LOCAL_DISPLAY_STARTUP_ALT/AZ are defined

#ifdef LOCAL_DISPLAY_STARTUP_ALT
static void ldStartupSlewCb() {
  if (!site.isDateTimeReady()) return;

  Coordinate coord;
  memset(&coord, 0, sizeof(coord));
  coord.z        = degToRad((double)LOCAL_DISPLAY_STARTUP_AZ);
  coord.a        = degToRad((double)LOCAL_DISPLAY_STARTUP_ALT);
  coord.pierSide = PIER_SIDE_NONE;
  transform.horToEqu(&coord);
  double st = site.getSiderealTime() * (M_PI / 12.0);
  coord.r = st - coord.h;
  while (coord.r < 0)          coord.r += 2.0 * M_PI;
  while (coord.r > 2.0 * M_PI) coord.r -= 2.0 * M_PI;

  CommandError err = goTo.request(coord, PSS_BEST, false);
  if (err != CE_NONE) { VF("WRN: LocalDisplay, startup slew error "); VL((int)err); }
}
#endif

// ---------------------------------------------------------------------------
// Goto-complete LED blink callback

#if GOTO_LED_PIN != OFF
static void ldGotoLedBlink() {
  _sLedState = !_sLedState;
  digitalWrite(GOTO_LED_PIN, _sLedState ? HIGH : LOW);
}
#endif

// ---------------------------------------------------------------------------
// Global instance

LocalDisplay localDisplay;

#endif // LOCAL_DISPLAY_PRESENT
