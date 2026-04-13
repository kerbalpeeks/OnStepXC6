// -----------------------------------------------------------------------------------
// Local display plugin implementation.

#include "LocalDisplay.h"

#ifdef LOCAL_DISPLAY_PRESENT

#include "Astronomy.h"
#include "../../lib/calendars/Calendars.h"

// ---------------------------------------------------------------------------
// U8g2 display object (128×64 SSD1306, hardware I2C, 2-page buffer)
static U8G2_SSD1306_128X64_NONAME_2_HW_I2C _u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

// ---------------------------------------------------------------------------
// Target descriptors

struct LdTarget {
  const char *name;
  const char *fact[3];
  void (*getPos)(double jd, double *ra, double *dec);
};

static const LdTarget _targets[] = {
  {
    "Moon",
    { "Dist:  ~384,400 km", "Diam:    3,475 km", "Gravity:    0.17 g" },
    Astronomy::moon
  },
  {
    "Jupiter",
    { "Dist:      ~5.2 AU", "Diam:  139,820 km", "Gravity:    2.53 g" },
    Astronomy::jupiter
  },
};
static constexpr uint8_t NUM_TARGETS = sizeof(_targets) / sizeof(_targets[0]);

// ---------------------------------------------------------------------------
// Menu item arrays

static const char * const _mainItems[] = {
  "Settings",
  "Move to...",
  "How to use",
  "Factory reset"
};
static constexpr uint8_t MAIN_COUNT = 4;

// ---------------------------------------------------------------------------
// Task wrappers

void ldPollWrapper()    { localDisplay.poll(); }
void ldEncoderWrapper() { localDisplay.pollEncoder(); }

// ---------------------------------------------------------------------------
// pollEncoder() — runs every 10ms via its own task
// Samples CLK/DT for encoder movement and SW for button state.
// Writes to _encDelta, _btnShort, _btnLong only.

void LocalDisplay::pollEncoder() {
  if (!_ready) return;

  // --- Rotary encoder: detect falling edge of CLK ---
  uint8_t clk = (uint8_t)digitalRead(LOCAL_DISPLAY_ENCODER_CLK_PIN);
  if (clk == LOW && _lastClkState == HIGH) {
    // CLK just fell — DT determines direction
    uint8_t dt = (uint8_t)digitalRead(LOCAL_DISPLAY_ENCODER_DT_PIN);
    if (dt == HIGH) _encDelta++; else _encDelta--;
  }
  _lastClkState = clk;

  // --- Button debounce ---
  bool pressed = (digitalRead(LOCAL_DISPLAY_ENCODER_BTN_PIN) == LOW);

  if (pressed && !_btnHeld) {
    if (_btnPressTime == 0) {
      _btnPressTime = millis();
    } else if (millis() - _btnPressTime > BTN_DEBOUNCE_MS) {
      _btnHeld = true;
    }
  }

  if (!pressed && _btnHeld) {
    uint32_t held = millis() - _btnPressTime;
    if (held >= BTN_LONG_PRESS_MS) _btnLong  = true;
    else                            _btnShort = true;
    _btnHeld      = false;
    _btnPressTime = 0;
  }

  if (!pressed && !_btnHeld) _btnPressTime = 0;
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
// drawHeader — title left, HH:MM:SS right, separator below

void LocalDisplay::drawHeader(const char *title) {
  char timeStr[9] = "--:--:--";
  if (site.isDateTimeReady()) {
    JulianDate now = site.getDateTime();
    int h = (int)now.hour;
    int m = (int)((now.hour - h) * 60.0);
    int s = (int)(((now.hour - h) * 60.0 - m) * 60.0);
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
// drawPointing — image placeholder + facts + optional slew bar

void LocalDisplay::drawPointing() {
  const LdTarget &tgt = _targets[_targetIdx];
  bool slewing = mount.isSlewing();

  drawHeader(tgt.name);

  // Placeholder image (40×40) centred at x=20, y=35
  _u8g2.setDrawColor(1);
  if (_targetIdx == 0) {
    // Moon: crescent shape
    _u8g2.drawDisc(20, 35, 15, U8G2_DRAW_ALL);
    _u8g2.setDrawColor(0);
    _u8g2.drawDisc(27, 32, 12, U8G2_DRAW_ALL);
    _u8g2.setDrawColor(1);
  } else {
    // Jupiter: circle with band lines
    _u8g2.drawCircle(20, 35, 16, U8G2_DRAW_ALL);
    _u8g2.drawHLine(5, 30, 30);
    _u8g2.drawHLine(5, 33, 30);
    _u8g2.drawHLine(5, 37, 30);
    _u8g2.drawHLine(5, 40, 30);
  }

  // Facts (right panel x=44)
  _u8g2.setFont(u8g2_font_5x7_tf);
  _u8g2.drawStr(44, 24, tgt.fact[0]);
  _u8g2.drawStr(44, 35, tgt.fact[1]);
  _u8g2.drawStr(44, 46, tgt.fact[2]);

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
// drawStub — shown for unimplemented menu options

void LocalDisplay::drawStub(const char *label) {
  drawHeader(label);
  _u8g2.setFont(u8g2_font_6x10_tf);
  _u8g2.drawStr(4, 36, "Coming soon...");
  _u8g2.drawStr(4, 52, "[press to go back]");
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
// init()

void LocalDisplay::init() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!_u8g2.begin()) {
    DLF("ERR: LocalDisplay, SSD1306 not found");
    return;
  }
  _u8g2.setContrast(200);

  // Configure encoder pins — INPUT_PULLUP so they read HIGH at rest
  pinMode(LOCAL_DISPLAY_ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(LOCAL_DISPLAY_ENCODER_DT_PIN,  INPUT_PULLUP);
  pinMode(LOCAL_DISPLAY_ENCODER_BTN_PIN, INPUT_PULLUP);
  _lastClkState = (uint8_t)digitalRead(LOCAL_DISPLAY_ENCODER_CLK_PIN);

  // Fast encoder poll task (10ms, priority 6)
  VF("MSG: LocalDisplay, start encoder task... ");
  if (tasks.add(10, 0, true, 6, ldEncoderWrapper, "LdEnc")) { VLF("ok"); }
  else { VLF("FAILED!"); return; }

  // Display poll task (LOCAL_DISPLAY_POLL_MS, priority 7)
  VF("MSG: LocalDisplay, start display task ("); V(LOCAL_DISPLAY_POLL_MS); VF("ms)... ");
  if (tasks.add(LOCAL_DISPLAY_POLL_MS, 0, true, 7, ldPollWrapper, "LdDisp")) { VLF("ok"); }
  else { VLF("FAILED!"); return; }

  _ready = true;
  VLF("MSG: LocalDisplay, ready");
}

// ---------------------------------------------------------------------------
// poll() — display update + input handling (runs at LOCAL_DISPLAY_POLL_MS)

void LocalDisplay::poll() {
  if (!_ready) return;

  // Consume accumulated encoder and button events
  int  delta = _readDelta(_encDelta);
  bool shortPress = _consumeShort(_btnShort);
  bool longPress  = _consumeLong (_btnLong);

  // ---- Input handling per screen ----
  switch (_screen) {

    case SCR_MAIN_MENU:
      if (delta != 0) {
        int next = (int)_menuSel + delta;
        _menuSel = (uint8_t)constrain(next, 0, (int)MAIN_COUNT - 1);
      }
      if (shortPress) {
        switch (_menuSel) {
          case 0: _stubTitle = _mainItems[0]; _screen = SCR_STUB;    break;
          case 1: _screen = SCR_MOVE_TO;                             break;
          case 2: _stubTitle = _mainItems[2]; _screen = SCR_STUB;    break;
          case 3: _stubTitle = _mainItems[3]; _screen = SCR_STUB;    break;
        }
        _menuSel = 0;
      }
      break;

    case SCR_MOVE_TO:
      if (delta != 0) {
        int next = (int)_menuSel + delta;
        _menuSel = (uint8_t)constrain(next, 0, (int)NUM_TARGETS - 1);
      }
      if (shortPress) {
        goToTarget(_menuSel);
        _menuSel = 0;
      }
      if (longPress) {
        _screen  = SCR_MAIN_MENU;
        _menuSel = 1;
      }
      break;

    case SCR_POINTING:
      if (shortPress || longPress) {
        _screen  = SCR_MAIN_MENU;
        _menuSel = 0;
      }
      break;

    case SCR_STUB:
      if (shortPress || longPress) {
        _screen  = SCR_MAIN_MENU;
        _menuSel = 0;
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
        break;

      case SCR_STUB:
        drawStub(_stubTitle);
        break;
    }
  } while (_u8g2.nextPage());
}

// ---------------------------------------------------------------------------
// Global instance

LocalDisplay localDisplay;

#endif // LOCAL_DISPLAY_PRESENT
