// EV charging digital twin: ESP32-S3 control + status unit.
// Receives compact state from the laptop over WebSocket, draws it on the LCD,
// and sends joystick / button commands back.
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
  int cap = 0; float base = 0, ev = 0, price = 0; int n = 0; bool ovl = false, tight = false;
  char clk[8] = "--:--"; Bay bays[NB]; bool valid = false;
};
Twin cur, shown;
uint32_t lastMsg = 0;
bool wsUp = false, wsStarted = false;

int  selBay = 0;
bool gridMode = false;  // false: bays (select / priority), true: grid (capacity)

Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
WebSocketsClient ws;

// ------------------------------------------------------------------- buzzer
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  void buzzInit() { ledcAttach(PIN_BUZZER, 2000, 8); }
  void buzzTone(int f) { ledcWriteTone(PIN_BUZZER, f); }
#else
  void buzzInit() { ledcSetup(0, 2000, 8); ledcAttachPin(PIN_BUZZER, 0); }
  void buzzTone(int f) { ledcWriteTone(0, f); }
#endif
uint32_t beepUntil = 0;
void beep(uint16_t ms = 40) { beepUntil = millis() + ms; buzzTone(2400); }

void handleBuzzer() {
  uint32_t now = millis();
  if (beepUntil) {                       // short UI click
    if (now >= beepUntil) { beepUntil = 0; buzzTone(0); }
    return;
  }
  if (cur.valid && cur.ovl) buzzTone((now / 250) % 2 ? 1500 : 0);  // overload alarm
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
  const char* t = gridMode ? "GRID  L/R budget +/-10 kW   BACK: bays       "
                           : "BAYS  L/R select  U/D priority  OK plug/out ";
  tft.fillRect(0, 224, 320, 16, C_HEAD);
  drawText(6, 228, 1, gridMode ? C_AMBER : C_MUTED, C_HEAD, t);
}

bool sameBay(const Bay& a, const Bay& b) { return a.code == b.code && a.soc == b.soc && a.prio == b.prio && a.kw == b.kw; }

bool uiInit = false;
int shownSel = -1;
bool shownMode = false;
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
  if (!shown.valid || shownMode != gridMode) drawFooter();
  shown = cur;
  shownSel = selBay;
  shownMode = gridMode;
}

// -------------------------------------------------------------------- input
int joyCx = 2048, joyCy = 2048;
void calibrateJoy() {
  long sx = 0, sy = 0;
  for (int i = 0; i < 32; i++) { sx += analogRead(PIN_JOY_X); sy += analogRead(PIN_JOY_Y); delay(5); }
  joyCx = sx / 32; joyCy = sy / 32;
}

struct Btn { uint8_t pin; bool down; uint32_t since; bool fired; };
Btn bJoy{PIN_JOY_SW, false, 0, false}, bSpike{PIN_BTN_SPIKE, false, 0, false},
    bReset{PIN_BTN_RESET, false, 0, false}, bBack{PIN_BTN_BACK, false, 0, false};

// returns true once per press (after 30 ms debounce); holdMs > 0 -> fires after being held that long
bool pressed(Btn& b, uint32_t holdMs = 0) {
  bool d = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (d && !b.down) { b.down = true; b.since = now; b.fired = false; return false; }
  if (!d) { bool fire = b.down && !b.fired && holdMs == 0 && now - b.since > 30; b.down = false; return fire; }
  if (holdMs > 0 && !b.fired && now - b.since >= holdMs) { b.fired = true; return true; }
  return false;
}

void handleInputs() {
  uint32_t now = millis();
  static uint32_t nextX = 0, nextY = 0;
  int dx = (analogRead(PIN_JOY_X) - joyCx) * JOY_X_SIGN;  // >0 right
  int dy = (analogRead(PIN_JOY_Y) - joyCy) * JOY_Y_SIGN;  // >0 up

  if (abs(dx) > JOY_DEAD) {
    if (now >= nextX) {
      int dir = dx > 0 ? 1 : -1;
      if (gridMode) {
        nextX = now + constrain(map(abs(dx), JOY_DEAD, 2000, 300, 50), 50, 300);  // hold = faster
        sendCmd("adjust_capacity", -1, "delta", dir * 10);
      } else {
        nextX = now + 250;
        selBay = (selBay + dir + NB) % NB;
        beep();
      }
    }
  } else nextX = 0;

  if (abs(dy) > JOY_DEAD && !gridMode) {
    if (now >= nextY) {
      nextY = now + 350;
      sendCmd("adjust_priority", selBay, "delta", dy > 0 ? 1 : -1);
      beep();
    }
  } else nextY = 0;

  if (pressed(bJoy))   { sendCmd("toggle_ev", selBay); beep(80); }
  if (pressed(bSpike)) { sendCmd("spike"); beep(80); }
  if (pressed(bReset, 1000)) { sendCmd("reset"); beep(200); }
  if (pressed(bBack))  { gridMode = !gridMode; beep(); }
}

// -------------------------------------------------------------------- setup
void setup() {
  Serial.begin(115200);
  pinMode(PIN_JOY_SW, INPUT_PULLUP);
  pinMode(PIN_BTN_SPIKE, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  pinMode(PIN_BTN_BACK, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  buzzInit();
  buzzTone(0);
  if (PIN_TFT_BL >= 0) { pinMode(PIN_TFT_BL, OUTPUT); digitalWrite(PIN_TFT_BL, HIGH); }

  SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(240, 320);
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(TFT_INVERT);
  tft.fillScreen(C_BG);
  calibrateJoy();

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

  static uint32_t lastUi = 0;
  if (millis() - lastUi > 100) { lastUi = millis(); refreshUi(); }
}
