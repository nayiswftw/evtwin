// EV charging digital twin: ESP32-S3 control + status unit.
// Receives compact state from the laptop over WebSocket, draws it on the LCD,
// and sends four-button commands back.
#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"

#define NB 4  // number of charging bays (must match backend N_BAYS)

// ------------------------------------------------------------------ colours
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
const uint16_t C_BG    = rgb(7, 21, 28);
const uint16_t C_HEAD  = rgb(14, 40, 52);
const uint16_t C_CARD  = rgb(14, 34, 44);
const uint16_t C_DIM   = rgb(46, 76, 90);
const uint16_t C_TEXT  = rgb(228, 240, 241);
const uint16_t C_MUTED = rgb(134, 162, 172);
const uint16_t C_TEAL  = rgb(46, 230, 197);
const uint16_t C_AMBER = rgb(255, 179, 71);
const uint16_t C_RED   = rgb(255, 90, 106);
const uint16_t C_GREEN = rgb(155, 225, 93);
const uint16_t C_GREY  = rgb(127, 152, 166);

// -------------------------------------------------------------------- state
struct Bay { uint8_t code = 0, soc = 0, prio = 0; float kw = 0; };  // code: 0 free 1 arriving 2 charging 3 full 4 leaving
struct Twin {
  int cap = 0; float base = 0, ev = 0, price = 0; int n = 0; bool ovl = false, tight = false, autoMode = true;
  char clk[8] = "--:--"; Bay bays[NB]; bool valid = false;
};
Twin cur, shown;
uint32_t lastMsg = 0;
bool wsUp = false, wsStarted = false;

int selBay = 0;
bool gridMode = false;  // false: bays (select / priority), true: grid (capacity)

Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
WebSocketsClient ws;

// ---------------------------------------------------------------- indicators
// The active buzzer module makes its own tone when its signal pin is HIGH.
void buzzInit() { pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW); }
void buzzTone(int on) { digitalWrite(PIN_BUZZER, on ? HIGH : LOW); }
uint32_t beepUntil = 0;
void beep(uint16_t ms = 45) { beepUntil = millis() + ms; buzzTone(1); }

void rgbWrite(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, RGB_COMMON_CATHODE ? r : !r);
  digitalWrite(PIN_LED_G, RGB_COMMON_CATHODE ? g : !g);
  digitalWrite(PIN_LED_B, RGB_COMMON_CATHODE ? b : !b);
}

void updateRgb() {
  // Green = normal, blue = tight/mild load, red = overload. Dark = offline.
  if (WiFi.status() != WL_CONNECTED || !wsUp || !cur.valid || millis() - lastMsg > 3000) {
    rgbWrite(false, false, false); return;
  }
  if (cur.ovl) { rgbWrite(true, false, false); return; }
  if (cur.tight) { rgbWrite(false, false, true); return; }
  rgbWrite(false, true, false);
}

void handleBuzzer() {
  uint32_t now = millis();
  if (beepUntil) {                       // short UI click
    if (now >= beepUntil) { beepUntil = 0; buzzTone(0); }
    return;
  }
  if (cur.valid && cur.ovl) buzzTone((now / 350) % 2);  // overload alarm
  else buzzTone(0);
}

// ------------------------------------------------------------------ network
void sendCmd(const char* cmd, int bay = -1, const char* key = nullptr, int val = 0) {
  if (!wsUp) return;
  JsonDocument d;
  d["cmd"] = cmd;
  if (bay >= 0) d["bay"] = bay;
  if (key) d[key] = val;
  char buf[96];
  size_t n = serializeJson(d, buf, sizeof buf);
  ws.sendTXT((uint8_t*)buf, n);
}

void onState(uint8_t* p, size_t len) {
  JsonDocument d;
  if (deserializeJson(d, p, len)) return;
  const char* t = d["t"] | "";
  if (strcmp(t, "s") != 0) return;
  cur.cap = d["cap"] | 0;
  cur.base = d["base"] | 0.0f;
  cur.ev = d["ev"] | 0.0f;
  cur.price = d["price"] | 0.0f;
  cur.n = d["n"] | 0;
  cur.ovl = (d["ovl"] | 0) != 0;
  cur.tight = (d["tight"] | 0) != 0;
  cur.autoMode = (d["auto"] | 1) != 0;
  strlcpy(cur.clk, d["clk"] | "--:--", sizeof cur.clk);
  JsonArray a = d["bays"];
  for (int i = 0; i < NB && i < (int)a.size(); i++) {
    cur.bays[i].code = a[i][0] | 0;
    cur.bays[i].soc  = a[i][1] | 0;
    cur.bays[i].kw   = a[i][2] | 0.0f;
    cur.bays[i].prio = a[i][3] | 0;
  }
  cur.valid = true;
  lastMsg = millis();
}

void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:    wsUp = true;  Serial.println("WS connected"); break;
    case WStype_DISCONNECTED: wsUp = false; Serial.println("WS disconnected"); break;
    case WStype_TEXT:         onState(payload, length); break;
    default: break;
  }
}

// ------------------------------------------------------------------ drawing
const char* linkText(uint16_t& col) {
  if (WiFi.status() != WL_CONNECTED) { col = C_AMBER; return "WIFI..."; }
  if (!wsUp)                         { col = C_AMBER; return "SERVER..."; }
  if (millis() - lastMsg > 3000)     { col = C_AMBER; return "NO DATA"; }
  col = C_TEAL; return "LIVE";
}

void drawText(int x, int y, uint8_t size, uint16_t fg, uint16_t bg, const char* s) {
  tft.setTextSize(size);
  tft.setTextColor(fg, bg);
  tft.setCursor(x, y);
  tft.print(s);
}

char shownLinkTxt[12] = "";
char shownClk[8] = "";
void drawHeader() {
  uint16_t col;
  const char* lt = linkText(col);
  if (strcmp(lt, shownLinkTxt) == 0 && strcmp(cur.clk, shownClk) == 0) return;
  strlcpy(shownLinkTxt, lt, sizeof shownLinkTxt);
  strlcpy(shownClk, cur.clk, sizeof shownClk);
  char buf[16];
  tft.fillRect(0, 0, 320, 24, C_HEAD);
  drawText(8, 4, 2, C_TEXT, C_HEAD, "EV TWIN");
  snprintf(buf, sizeof buf, "%-9s", lt);
  drawText(130, 8, 1, col, C_HEAD, buf);
  drawText(320 - 8 - 60, 4, 2, C_TEXT, C_HEAD, cur.clk);
}

void drawGrid() {
  char buf[40];
  uint16_t loadCol = cur.ovl ? C_RED : (cur.tight ? C_AMBER : C_TEXT);
  snprintf(buf, sizeof buf, "%3d / %-3d kW", (int)(cur.base + cur.ev + 0.5f), cur.cap);
  drawText(8, 32, 2, loadCol, C_BG, buf);
  snprintf(buf, sizeof buf, "Rs%5.1f", cur.price);
  drawText(200, 32, 2, cur.tight ? C_AMBER : C_TEAL, C_BG, buf);

  const int bx = 8, by = 56, bw = 304, bh = 14;
  float cap = cur.cap > 0 ? cur.cap : 1;
  int baseW = constrain((int)(cur.base / cap * bw), 0, bw);
  int evW = constrain((int)(cur.ev / cap * bw), 0, bw - baseW);
  tft.fillRect(bx, by, bw, bh, C_DIM);
  tft.fillRect(bx, by, baseW, bh, cur.ovl ? C_RED : C_GREY);
  tft.fillRect(bx + baseW, by, evW, bh, cur.tight ? C_AMBER : C_TEAL);

  const char* msg = cur.ovl ? "OVERLOAD: demand above capacity   "
                  : cur.tight ? "Tight grid: charging at needed rate"
                              : "Grid OK: fast charging, cheap price";
  drawText(8, 76, 1, cur.ovl ? C_RED : (cur.tight ? C_AMBER : C_MUTED), C_BG, msg);
}

void drawCard(int i, bool full) {
  const int w = 74, h = 128;
  int x = 2 + i * 80, y = 92;
  const Bay& b = cur.bays[i];
  bool sel = (i == selBay);
  char buf[16];
  static const char* names[] = {"FREE", "ARRIVE", "CHARGE", "FULL", "LEAVE"};
  static const char* prios[] = {"-", "LOW", "NORM", "HIGH"};
  uint8_t code = b.code > 4 ? 0 : b.code;

  if (full) {
    tft.fillRoundRect(x, y, w, h, 6, C_CARD);
    tft.drawRoundRect(x, y, w, h, 6, sel ? C_TEAL : C_DIM);
    if (sel) tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5, C_TEAL);
    snprintf(buf, sizeof buf, "BAY %d", i + 1);
    drawText(x + 8, y + 6, 1, sel ? C_TEAL : C_MUTED, C_CARD, buf);
  }
  uint16_t sc = code == 2 ? C_TEAL : code == 3 ? C_GREEN : code == 0 ? C_MUTED : C_AMBER;
  snprintf(buf, sizeof buf, "%-6s", names[code]);
  drawText(x + 8, y + 20, 1, sc, C_CARD, buf);

  // vertical charge bar
  const int bx = x + 8, by = y + 34, bw = 14, bh = 80;
  tft.fillRect(bx, by, bw, bh, C_DIM);
  if (code) {
    int fh = bh * constrain((int)b.soc, 0, 100) / 100;
    tft.fillRect(bx, by + bh - fh, bw, fh, code == 3 ? C_GREEN : C_TEAL);
  }
  if (code == 0) {
    drawText(x + 28, y + 36, 1, C_MUTED, C_CARD, "    ");
    drawText(x + 28, y + 52, 1, C_MUTED, C_CARD, "    ");
    drawText(x + 28, y + 62, 1, C_MUTED, C_CARD, "  ");
    drawText(x + 28, y + 84, 1, C_MUTED, C_CARD, "    ");
    drawText(x + 28, y + 100, 1, C_MUTED, C_CARD, "OK");
    drawText(x + 28, y + 109, 1, C_MUTED, C_CARD, "adds");
  } else {
    snprintf(buf, sizeof buf, "%3d%%", b.soc);
    drawText(x + 28, y + 36, 1, C_TEXT, C_CARD, buf);
    snprintf(buf, sizeof buf, "%4.1f", b.kw);
    drawText(x + 28, y + 52, 1, C_TEXT, C_CARD, buf);
    drawText(x + 28, y + 62, 1, C_MUTED, C_CARD, "kW");
    snprintf(buf, sizeof buf, "%-4s", prios[b.prio > 3 ? 0 : b.prio]);
    drawText(x + 28, y + 84, 1, C_MUTED, C_CARD, buf);
    drawText(x + 28, y + 100, 1, C_MUTED, C_CARD, "    ");
    drawText(x + 28, y + 109, 1, C_MUTED, C_CARD, "    ");
  }
}

void drawFooter() {
  tft.fillRect(0, 220, 320, 20, C_HEAD);
  if (!cur.autoMode) {
    drawText(6, 222, 1, C_MUTED, C_HEAD, "MANUAL: touch 1-4 toggles the matching bay");
    drawText(6, 231, 1, C_MUTED, C_HEAD, "Grid, spike, priority and reset stay on the frontend");
  } else {
    drawText(6, 222, 1, C_AMBER, C_HEAD, "AUTO KEYPAD: 12 spike  13/24 priority  14/23 bay");
    drawText(6, 231, 1, C_MUTED, C_HEAD, "Hold 3+4 to reset; single-key controls still work");
  }
}

bool sameBay(const Bay& a, const Bay& b) { return a.code == b.code && a.soc == b.soc && a.prio == b.prio && a.kw == b.kw; }

bool uiInit = false;
int shownSel = -1;
bool shownMode = false, shownAutoMode = true;
void refreshUi() {
  if (!uiInit) {
    tft.fillScreen(C_BG);
    shown = Twin();
    shownSel = -1;
    shownLinkTxt[0] = 0;
    uiInit = true;
  }
  drawHeader();
  if (!cur.valid) {
    drawText(8, 100, 2, C_MUTED, C_BG, "Waiting for the");
    drawText(8, 122, 2, C_MUTED, C_BG, "simulation...");
    return;
  }
  if (!shown.valid) tft.fillRect(0, 26, 320, 198, C_BG);  // clear the waiting text
  bool gridChanged = !shown.valid || cur.cap != shown.cap || cur.base != shown.base || cur.ev != shown.ev ||
                     cur.price != shown.price || cur.ovl != shown.ovl || cur.tight != shown.tight;
  if (gridChanged) drawGrid();
  for (int i = 0; i < NB; i++) {
    bool structural = !shown.valid || shownSel != selBay || cur.bays[i].code != shown.bays[i].code || cur.bays[i].prio != shown.bays[i].prio;
    if (structural || !sameBay(cur.bays[i], shown.bays[i])) drawCard(i, structural);
  }
  if (!shown.valid || shownMode != gridMode || shownAutoMode != cur.autoMode) drawFooter();
  shown = cur;
  shownSel = selBay;
  shownMode = gridMode;
  shownAutoMode = cur.autoMode;
}

// -------------------------------------------------------------------- input
// In manual mode each key toggles its matching bay. In automatic mode, keys
// retain their individual controls and two-key chords provide quick actions.
// Channel order calibrated from the connected module: key 1 is expected on
// GPIO6, key 2 on GPIO13, key 3 on GPIO4, and key 4 on GPIO7.
const uint8_t buttonPins[4] = {PIN_BTN_OK, PIN_BTN_RIGHT, PIN_BTN_LEFT, PIN_BTN_BACK};
struct TouchButton {
  bool raw = false, stable = false;
  uint32_t changedAt = 0, pressedAt = 0, nextRepeat = 0;
};
TouchButton buttons[4];
uint8_t gestureMask = 0;
uint32_t gestureStarted = 0;
bool gestureActive = false, gestureChordEligible = true, gestureActionFired = false;

void shortAction(int i) {
  if (i == 0 || i == 1) {
    const int dir = i == 1 ? 1 : -1;
    if (gridMode) sendCmd("adjust_capacity", -1, "delta", dir * 10);
    else { selBay = (selBay + dir + NB) % NB; beep(); }
  } else if (i == 2) {
    sendCmd("toggle_ev", selBay);
    beep(80);
  } else {
    gridMode = !gridMode;
    beep();
  }
}

void longAction(int i) {
  if (i == 0 || i == 1) {
    if (gridMode) {
      sendCmd("adjust_capacity", -1, "delta", i == 1 ? 10 : -10);
      buttons[i].nextRepeat = millis() + 180;
    } else {
      sendCmd("adjust_priority", selBay, "delta", i == 1 ? 1 : -1);
      beep();
    }
  } else if (i == 2) {
    sendCmd("spike");
    beep(100);
  } else {
    sendCmd("reset");
    beep(200);
  }
}

void chordAction(uint8_t mask, uint32_t heldMs) {
  if (mask == 0x03) {                         // 1 + 2: demand spike
    sendCmd("spike"); beep(100);
  } else if (mask == 0x05) {                  // 1 + 3: lower selected bay priority
    sendCmd("adjust_priority", selBay, "delta", -1); beep();
  } else if (mask == 0x09) {                  // 1 + 4: previous bay
    selBay = (selBay + NB - 1) % NB; beep();
  } else if (mask == 0x06) {                  // 2 + 3: next bay
    selBay = (selBay + 1) % NB; beep();
  } else if (mask == 0x0A) {                  // 2 + 4: raise selected bay priority
    sendCmd("adjust_priority", selBay, "delta", 1); beep();
  } else if (mask == 0x0C && heldMs >= 900) { // hold 3 + 4: reset
    sendCmd("reset"); beep(200);
  } else {
    return;
  }
}

void handleInputs() {
  const uint32_t now = millis();
  uint8_t downMask = 0;
  for (int i = 0; i < 4; ++i) {
    const bool raw = digitalRead(buttonPins[i]) == (TOUCH_ACTIVE_HIGH ? HIGH : LOW);
    if (raw != buttons[i].raw) {
      buttons[i].raw = raw;
      buttons[i].changedAt = now;
    }
    if (raw != buttons[i].stable && now - buttons[i].changedAt >= 45) {
      buttons[i].stable = raw;
      if (raw) {
        buttons[i].pressedAt = now;
        if (!gestureActive) {
          gestureActive = true;
          gestureMask = 0;
          gestureStarted = now;
          gestureChordEligible = true;
          gestureActionFired = false;
        } else if (now - gestureStarted > 250) {
          gestureChordEligible = false;
        }
        gestureMask |= (1u << i);
        // In manual mode, act on the debounced press edge. Some touch-key
        // modules hold their output high until the next touch, so waiting for
        // a release edge leaves the gesture open forever and drops the car.
        if (!cur.autoMode) {
          sendCmd("toggle_ev", i);
          beep(65);
          gestureActionFired = true;
        }
      }
    }
    if (buttons[i].stable) downMask |= (1u << i);
  }

  if (gestureActive && cur.autoMode && gestureChordEligible && gestureMask == 0x0C &&
      downMask == 0x0C && now - gestureStarted >= 900 && !gestureActionFired) {
    sendCmd("reset"); beep(200); gestureActionFired = true;
  }

  // Individual long-press actions remain available alongside the chords.
  if (gestureActive && cur.autoMode && gestureChordEligible && (gestureMask == 0x01 || gestureMask == 0x02) &&
      now - gestureStarted >= 650 && !gestureActionFired) {
    longAction(gestureMask == 0x01 ? 0 : 1);
    gestureActionFired = true;
  }
  if (gestureActive && cur.autoMode && gestureChordEligible && (gestureMask == 0x01 || gestureMask == 0x02) &&
      gestureActionFired && gridMode && downMask == gestureMask) {
    const int i = gestureMask == 0x01 ? 0 : 1;
    if (now >= buttons[i].nextRepeat) {
      sendCmd("adjust_capacity", -1, "delta", i == 1 ? 10 : -10);
      buttons[i].nextRepeat = now + 120;
    }
  }

  if (gestureActive && downMask == 0) {
    const uint32_t heldMs = now - gestureStarted;
    if (!gestureActionFired) {
      if (cur.autoMode && gestureChordEligible && gestureMask && (gestureMask & (gestureMask - 1)) && gestureMask != 0x0C) {
        chordAction(gestureMask, heldMs);
      } else if (cur.autoMode && gestureMask && !(gestureMask & (gestureMask - 1))) {
        int i = 0;
        while (!(gestureMask & (1u << i))) ++i;
        if (heldMs < 650) shortAction(i);
        else longAction(i);
      } else if (cur.autoMode && gestureChordEligible && gestureMask == 0x0C && heldMs >= 900) {
        chordAction(gestureMask, heldMs);
      }
    }
    gestureActive = false;
  }
}

// -------------------------------------------------------------------- setup
void setup() {
  Serial.begin(115200);
  pinMode(PIN_CAM_PWDN, OUTPUT);
  digitalWrite(PIN_CAM_PWDN, HIGH);  // OV5640 PWDN is active-high; release shared camera-bus GPIOs.
  for (int i = 0; i < 4; ++i) pinMode(buttonPins[i], INPUT);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  rgbWrite(false, false, false);
  buzzInit();
  buzzTone(0);
  if (PIN_TFT_BL >= 0) { pinMode(PIN_TFT_BL, OUTPUT); digitalWrite(PIN_TFT_BL, HIGH); }

  SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(240, 320);
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(TFT_INVERT);
  tft.fillScreen(C_BG);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  ws.onEvent(wsEvent);
  ws.setReconnectInterval(2000);
  ws.enableHeartbeat(15000, 3000, 2);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !wsStarted) {
    Serial.printf("WiFi OK, IP %s -> ws://%s:%d\n", WiFi.localIP().toString().c_str(), SERVER_HOST, SERVER_PORT);
    ws.begin(SERVER_HOST, SERVER_PORT, "/ws?role=device");
    wsStarted = true;
  }
  if (wsStarted) ws.loop();
  handleInputs();
  handleBuzzer();
  updateRgb();

  static uint32_t lastUi = 0;
  if (millis() - lastUi > 100) { lastUi = millis(); refreshUi(); }
}
