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
// Encoder state — modified in ISR, read in poll()

static volatile int  _encDelta = 0;      // accumulated ticks since last read
static volatile bool _encLastClk = true; // previous CLK level

// Button state — polled in poll()
static uint32_t _btnPressTime  = 0;
static bool     _btnHeld       = false;
static bool     _btnShort      = false;  // short press pending
static bool     _btnLong       = false;  // long press pending

static constexpr uint16_t BTN_DEBOUNCE_MS   = 20;
static constexpr uint16_t BTN_LONG_PRESS_MS = 600;

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

// Move-to items are derived from _targets at draw time.

// ---------------------------------------------------------------------------
// ISR: called on every change of CLK pin

void IRAM_ATTR ldEncoderClkISR() {
  bool clk = digitalRead(LOCAL_DISPLAY_ENCODER_CLK_PIN);
  bool dt  = digitalRead(LOCAL_DISPLAY_ENCODER_DT_PIN);
  if (!clk && _encLastClk) {        // falling edge of CLK
    _encDelta += (dt == HIGH) ? 1 : -1;
  }
  _encLastClk = clk;
}

// ---------------------------------------------------------------------------
// Task wrapper

void ldPollWrapper() { localDisplay.poll(); }

// ---------------------------------------------------------------------------
// Helper: read and reset encoder delta

static int _readDelta() {
  noInterrupts();
  int d = _encDelta;
  _encDelta = 0;
  interrupts();
  return d;
}

// ---------------------------------------------------------------------------
// Helper: poll button (call once per poll cycle)

static void _pollButton() {
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
    if (held >= BTN_LONG_PRESS_MS)  _btnLong  = true;
    else if (held >= BTN_DEBOUNCE_MS) _btnShort = true;
    _btnHeld      = false;
    _btnPressTime = 0;
  }

  if (!pressed && !_btnHeld) _btnPressTime = 0;
}

// ---------------------------------------------------------------------------
// Helper: consume pending button events

static bool _wasShortPress() { bool v = _btnShort; _btnShort = false; return v; }
static bool _wasLongPress()  { bool v = _btnLong;  _btnLong  = false; return v; }

// ---------------------------------------------------------------------------
// Helper: draw header bar (title left, HH:MM:SS right, separator line below)

void LocalDisplay::drawHeader(const char *title) {
  // Time string
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
  // Right-align the time (8 chars × 6px = 48px wide → x = 128-48 = 80)
  _u8g2.drawStr(80, 9, timeStr);
  _u8g2.drawHLine(0, 11, 128);
}

// ---------------------------------------------------------------------------
// Helper: draw a scrollable menu (up to 4 items visible, cursor = ">")

void LocalDisplay::drawMenu(const char * const *items, uint8_t count,
                             uint8_t selected) {
  _u8g2.setFont(u8g2_font_6x10_tf);
  // Scroll window: keep selected item visible
  uint8_t topItem = 0;
  if (selected >= 4) topItem = selected - 3;
  for (uint8_t i = 0; i < 4 && (topItem + i) < count; i++) {
    uint8_t idx = topItem + i;
    int y = 13 + (int)i * 13;
    _u8g2.drawStr(0,  y, (idx == selected) ? ">" : " ");
    _u8g2.drawStr(8,  y, items[idx]);
  }
}

// ---------------------------------------------------------------------------
// Helper: draw the pointing screen (image placeholder + facts + slew bar)

void LocalDisplay::drawPointing() {
  const LdTarget &tgt = _targets[_targetIdx];
  bool slewing = mount.isSlewing();

  // --- Object name in header (drawHeader writes HH:MM:SS on the right)
  drawHeader(tgt.name);

  // --- Placeholder image (40×40) at x=0, y=13..52
  // Centre of image: (20, 32)
  _u8g2.setDrawColor(1);
  if (_targetIdx == 0) {
    // Moon: solid disc
    _u8g2.drawDisc(20, 32, 15, U8G2_DRAW_ALL);
    // Dark crescent cutout (drawColor 0 = black)
    _u8g2.setDrawColor(0);
    _u8g2.drawDisc(27, 29, 12, U8G2_DRAW_ALL);
    _u8g2.setDrawColor(1);
  } else {
    // Jupiter: circle with band lines
    _u8g2.drawCircle(20, 32, 16, U8G2_DRAW_ALL);
    _u8g2.drawHLine(5, 27, 30);
    _u8g2.drawHLine(5, 30, 30);
    _u8g2.drawHLine(5, 34, 30);
    _u8g2.drawHLine(5, 37, 30);
  }

  // --- Facts (right panel: x=44..127)
  _u8g2.setFont(u8g2_font_5x7_tf);
  _u8g2.drawStr(44, 22, tgt.fact[0]);
  _u8g2.drawStr(44, 33, tgt.fact[1]);
  _u8g2.drawStr(44, 44, tgt.fact[2]);

  // --- Slew bar (bottom 7px: y=57..63), visible only while slewing
  if (slewing) {
    // Animate an indeterminate bouncing bar segment
    _barPos += _barDir * 3;
    if (_barPos + 30 >= 126 || _barPos <= 1) _barDir = -_barDir;
    _u8g2.drawFrame(0, 57, 128, 7);
    _u8g2.drawBox(_barPos, 58, 30, 5);
  }

  // Note: slew completion is silent — bar disappears, screen stays.
}

// ---------------------------------------------------------------------------
// Helper: stub screen for unimplemented options

void LocalDisplay::drawStub(const char *label) {
  drawHeader(label);
  _u8g2.setFont(u8g2_font_6x10_tf);
  _u8g2.drawStr(4, 36, "Coming soon...");
  _u8g2.drawStr(4, 52, "[press to go back]");
}

// ---------------------------------------------------------------------------
// GoTo: compute current RA/Dec of target and request slew

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
  coord.r         = ra  * (M_PI / 12.0);   // hours → radians
  coord.d         = dec * (M_PI / 180.0);  // degrees → radians
  coord.pierSide  = PIER_SIDE_NONE;

  CommandError err = goTo.request(coord, PSS_BEST, true);
  if (err != CE_NONE) {
    VF("WRN: LocalDisplay, GoTo failed with error "); VL((int)err);
  }

  _targetIdx  = idx;
  _wasSlewing = false;
  _screen     = SCR_POINTING;
}

// ---------------------------------------------------------------------------
// init()

void LocalDisplay::init() {
  // Ensure I2C is started with the correct pins
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!_u8g2.begin()) {
    DLF("ERR: LocalDisplay, SSD1306 not found — display disabled");
    return;
  }
  _u8g2.setContrast(200);

  // Configure encoder pins
  pinMode(LOCAL_DISPLAY_ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(LOCAL_DISPLAY_ENCODER_DT_PIN,  INPUT_PULLUP);
  pinMode(LOCAL_DISPLAY_ENCODER_BTN_PIN, INPUT_PULLUP);
  _encLastClk = digitalRead(LOCAL_DISPLAY_ENCODER_CLK_PIN);
  attachInterrupt(digitalPinToInterrupt(LOCAL_DISPLAY_ENCODER_CLK_PIN),
                  ldEncoderClkISR, CHANGE);

  // Register poll task (priority 7 = lowest, non-critical UI)
  VF("MSG: LocalDisplay, start poll task (rate ");
  V(LOCAL_DISPLAY_POLL_MS); VF("ms)... ");
  if (tasks.add(LOCAL_DISPLAY_POLL_MS, 0, true, 7, ldPollWrapper, "LclDisp")) {
    VLF("success");
  } else {
    VLF("FAILED!");
    return;
  }

  _ready = true;
  VLF("MSG: LocalDisplay, ready");
}

// ---------------------------------------------------------------------------
// poll() — called every LOCAL_DISPLAY_POLL_MS ms by the task scheduler

void LocalDisplay::poll() {
  if (!_ready) return;

  _pollButton();
  int delta = _readDelta();

  // ---- Input handling per screen ----

  switch (_screen) {

    case SCR_MAIN_MENU:
      if (delta != 0) {
        int next = (int)_menuSel + delta;
        _menuSel = (uint8_t)constrain(next, 0, (int)MAIN_COUNT - 1);
      }
      if (_wasShortPress()) {
        switch (_menuSel) {
          case 0: _stubTitle = _mainItems[0]; _screen = SCR_STUB;    break;  // Settings
          case 1: _screen = SCR_MOVE_TO;                             break;  // Move to
          case 2: _stubTitle = _mainItems[2]; _screen = SCR_STUB;    break;  // How to use
          case 3: _stubTitle = _mainItems[3]; _screen = SCR_STUB;    break;  // Factory reset
        }
        _menuSel = 0;
      }
      break;

    case SCR_MOVE_TO:
      if (delta != 0) {
        int next = (int)_menuSel + delta;
        _menuSel = (uint8_t)constrain(next, 0, (int)NUM_TARGETS - 1);
      }
      if (_wasShortPress()) {
        goToTarget(_menuSel);     // changes _screen to SCR_POINTING
        _menuSel = 0;
      }
      if (_wasLongPress()) {
        _screen  = SCR_MAIN_MENU;
        _menuSel = 1; // return cursor to "Move to..."
      }
      break;

    case SCR_POINTING:
      if (_wasShortPress() || _wasLongPress()) {
        _screen  = SCR_MAIN_MENU;
        _menuSel = 0;
      }
      break;

    case SCR_STUB:
      if (_wasShortPress() || _wasLongPress()) {
        _screen  = SCR_MAIN_MENU;
        _menuSel = 0;
      }
      break;
  }

  // ---- Drawing ----

  _u8g2.firstPage();
  do {
    switch (_screen) {

      case SCR_MAIN_MENU:
        drawHeader("OnStep Pointer");
        drawMenu(_mainItems, MAIN_COUNT, _menuSel);
        break;

      case SCR_MOVE_TO: {
        // Build a const char* array pointing into _targets
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
