// ESP32 LCD BLE Controller + New UI (Portrait 240x320)
#include <SPI.h>
#include <Adafruit_GFX.h>//1.11.7
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <BLEDevice.h>
#include <cstring>

#include <FreeSans7pt7b.h>
#include <FreeSansBold7pt7b.h>
#include <FreeSans10pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include "splash.h"

// ---------- TFT & TOUCH ----------
#define TFT_CS    27
#define TFT_DC    17
#define TFT_RST   4
#define TOUCH_CS  26
#define TFT_BL    32

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);

// ---------- BLE UUIDs ----------
static BLEUUID SERVICE_UUID   ("12345678-1234-1234-1234-1234567890ab");
static BLEUUID CMD_CHAR_UUID  ("abcdefab-1234-5678-1234-abcdefabcdef");
static BLEUUID STATE_CHAR_UUID("fedcbafe-4321-8765-4321-fedcbafedcba");

// ---------- BLE ----------
BLEClient* bleClient = nullptr;
BLERemoteCharacteristic* cmdChar   = nullptr;
BLERemoteCharacteristic* stateChar = nullptr;

bool bleConnected = false;
bool phoneConnected = false;
bool speakerPlaying = false;

int phoneVolumePercent = -1;   // -1 یعنی هنوز ولوم دریافت نشده

// ---------- Metadata ----------
String currentArtist, currentTitle, currentDevice;

String StoreMobile = "0937 186 9494";
String StoreName = "Electro Payman";
String StoreAddress = "Gilan, Rasht, Saadi";

int currentPosSec = 0;
int totalLenSec   = 0;

// ---------- Device List Display ----------
bool currentDeviceConnected = false;

// ---------- SCREEN SLEEP ----------
const unsigned long SCREEN_TIMEOUT = 30000;   // 30s
const unsigned long DOUBLE_TAP_GAP = 500;     // ms

bool screenOn = true;
unsigned long lastTouchTime = 0;
unsigned long lastTapTime = 0;
uint8_t tapCount = 0;
bool touchWasDown = false;

// ---------- Timing ----------
unsigned long lastPing = 0;
unsigned long lastBleAttempt = 0;
const unsigned long BLE_RETRY_INTERVAL = 3000;

// ---------- Prev/Next Short Press & Long Press ----------
const unsigned long LONG_PRESS_TIME = 350;     // مدت لازم برای تشخیص نگه داشتن
const unsigned long SEEK_REPEAT_TIME = 350;    // فاصله تکرار B/F هنگام نگه داشتن

String pendingStateMessage = "";
volatile bool hasPendingStateMessage = false;

enum TouchTarget {
  TOUCH_NONE, TOUCH_PREV, TOUCH_NEXT, TOUCH_VOL_UP, TOUCH_VOL_DOWN, TOUCH_PLAY, TOUCH_OTHER
};

TouchTarget activeTouch = TOUCH_NONE;

bool longPressDone = false;
unsigned long touchStartTime = 0;
unsigned long lastSeekSendTime = 0;

unsigned long pairTimerStart = 0;

unsigned long pairOkStart = 0;

String lastDevName = "No Device";
int lastDevCount = 0;

// --------- PAIR timing constants ----------
const unsigned long PAIR_TIMER_DURATION   = 20000; // 30s تایمر pairing
const unsigned long PAIR_OK_DURATION      = 3000;  // 3s نمایش OK
const unsigned long PAIR_DOUBLE_TAP_GAP   = 400;   // فاصله دو تا tap
const unsigned long PAIR_HOLD_START_TIME  = 500;   // بعد از این، حالت HOLD (چشمک) شروع شود
const unsigned long PAIR_CLEAR_HOLD_TIME  = 10000; // 10s نگه داشتن برای ارسال Y
const unsigned long PAIR_DELETE_BLINK_MS  = 500;   // سرعت چشمک‌زدن
const unsigned long PAIR_KILL_SHOW_TIME   = 3000;  // نمایش KILL بعد از Y

// --------- PAIR timer state ----------
bool          pairTimerActive     = false;
unsigned long pairTimerEndMs      = 0;
int           lastPairSecondShown = -1;

// --------- OK state ----------
bool          pairOkActive   = false;
unsigned long pairOkUntilMs  = 0;

// --------- Delete/HOLD state ----------
bool          pairDeleteHoldActive  = false; // الان در حالت نگه‌داشتن (blinking) است؟
bool          pairDeleteDoneActive  = false; // Y ارسال شده و داریم KILL نشان می‌دهیم؟
unsigned long pairDeleteHoldStartMs = 0;
unsigned long pairDeleteLastBlinkMs = 0;
bool          pairDeleteBlinkState  = false; // false = بنفش, true = قرمز
unsigned long pairKillUntilMs       = 0;

// --------- Touch / input state ----------
bool          pairBtnDown      = false; // الان انگشت روی دکمه است؟
unsigned long pairBtnDownTime  = 0;
bool          pairLongDone     = false; // long-press شروع شده؟
unsigned long pairLastTapTime  = 0;
int           pairTapCount     = 0;


// --------- Splash Screen ----------
enum AppState {
  STATE_SPLASH, STATE_MAIN_UI
};

AppState appState = STATE_SPLASH;

unsigned long splashStartMillis = 0;
const unsigned long splashDuration = 2000;

bool splashDrawn = false;
bool mainUiDrawn = false;


// ---------- UI Colors ----------
uint16_t COLOR_BG   = 0x0000;
uint16_t COLOR_GRAY = 0x0020;

// ---------- Button Struct ----------
struct RectButton {
  int x, y, w, h;
  const char* label;
  char command;
  uint16_t color;
  uint16_t pressedColor;
};

struct CircleButton {
  int cx, cy, r;
  const char* label;
  char command;
  uint16_t color;
  uint16_t pressedColor;
};

// ---------- UI Buttons ----------
RectButton btnPrev = {10, 174, 55, 65, "PREV", 'B', ILI9341_DARKCYAN, ILI9341_CYAN};
RectButton btnNext = {175, 174, 55, 65, "NEXT", 'F', ILI9341_DARKCYAN, ILI9341_CYAN};
RectButton btnVolUp = {10, 260, 100, 50, "Vol +", 'U', ILI9341_DARKGREEN, ILI9341_GREEN};
RectButton btnVolDown = {130, 260, 100, 50, "Vol -", 'D', ILI9341_MAROON, ILI9341_RED};
CircleButton btnPlay = {120, 205, 46, "PLAY", 'P', ILI9341_NAVY, ILI9341_BLUE};

bool pointInPairCircle(int x, int y) {
  int dx = x - 120;
  int dy = y - 20;
  return (dx * dx + dy * dy) <= (30 * 30);
}

// ---------- Helpers ----------
uint16_t fixColor(uint16_t c) {
  uint8_t r = (c >> 11) & 0x1F;
  uint8_t g = (c >> 5) & 0x3F;
  uint8_t b = c & 0x1F;
  return (b << 11) | (g << 5) | r;
}


void showSplashScreen() {
  tft.fillScreen(ILI9341_BLACK);

  // اگر اندازه تصویرت دقیقاً 320x240 باشد:
  tft.drawRGBBitmap(0, 0, splash_img, SPLASH_WIDTH, SPLASH_HEIGHT);
}

String formatTime(int sec) {
  if (sec < 0) sec = 0;

  int minutes = sec / 60;
  int seconds = sec % 60;

  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
  return String(buf);
}

String shortenText(String text, int maxLen) {
  if (text.length() <= maxLen) return text;
  return text.substring(0, maxLen - 3) + "...";
}


const char* getPairButtonText() {
  static char buf[8];

  if (pairDeleteDoneActive) {
    return "KILL";
  }
  if (pairOkActive) {
    return "ADD";
  }
  if (pairTimerActive) {
    unsigned long now = millis();
    if (now >= pairTimerEndMs) {
      return "PAIR";
    }
    int remaining = (pairTimerEndMs - now + 999) / 1000;
    snprintf(buf, sizeof(buf), "%d", remaining);
    return buf;
  }
  return "PAIR";
}


void fillRoundRectHelper(int x, int y, int w, int h, int r, uint16_t color) {
  tft.fillRect(x + r, y, w - 2 * r, h, color);
  tft.fillRect(x, y + r, r, h - 2 * r, color);
  tft.fillRect(x + w - r, y + r, r, h - 2 * r, color);

  tft.fillCircle(x + r, y + r, r, color);
  tft.fillCircle(x + w - r - 1, y + r, r, color);
  tft.fillCircle(x + r, y + h - r - 1, r, color);
  tft.fillCircle(x + w - r - 1, y + h - r - 1, r, color);
}


// ---------- Icons ----------
void drawPlayIcon(int x, int y) {
  tft.fillTriangle(
    x - 12, y - 20,
    x - 12, y + 20,
    x + 18, y,
    fixColor(ILI9341_WHITE)
  );
}

void drawPauseIcon(int x, int y) {
  tft.fillRect(x - 14, y - 18, 10, 36, fixColor(ILI9341_WHITE));
  tft.fillRect(x + 4,  y - 18, 10, 36, fixColor(ILI9341_WHITE));
}

void drawPrevIcon(int x, int y) {
  tft.fillTriangle(x + 4, y, x + 14, y - 12, x + 14, y + 12, fixColor(ILI9341_WHITE));
  tft.fillTriangle(x - 6, y, x + 4, y - 12, x + 4, y + 12, fixColor(ILI9341_WHITE));
  tft.fillRect(x - 12, y - 12, 3, 24, fixColor(ILI9341_WHITE));
}

void drawNextIcon(int x, int y) {
  tft.fillTriangle(x - 4, y, x - 14, y - 12, x - 14, y + 12, fixColor(ILI9341_WHITE));
  tft.fillTriangle(x + 6, y, x - 4, y - 12, x - 4, y + 12, fixColor(ILI9341_WHITE));
  tft.fillRect(x + 9, y - 12, 3, 24, fixColor(ILI9341_WHITE));
}

// ---------- UI Draw ----------
void drawStatusBar() {
  tft.fillRect(0, 0, 240, 32, fixColor(COLOR_GRAY));
  
  tft.fillCircle(48, 20, 26, fixColor(COLOR_BG));
  tft.fillCircle(48, 20, 20, fixColor(bleConnected ? ILI9341_GREEN : ILI9341_RED));
  tft.setFont(&FreeSansBold7pt7b);
  tft.setTextColor(fixColor(ILI9341_WHITE));
  tft.setCursor(30, 24);
  tft.print("CTRL");
  
  tft.fillCircle(120, 20, 26, fixColor(COLOR_BG));
  drawPairButton(false);
  
  tft.fillCircle(192, 20, 26, fixColor(COLOR_BG));
  tft.fillCircle(192, 20, 20, fixColor(phoneConnected ? ILI9341_GREEN : ILI9341_RED));
  tft.setFont(&FreeSansBold7pt7b);
  tft.setTextColor(fixColor(ILI9341_WHITE));
  tft.setCursor(178, 24);
  tft.print("DEV");
}

void drawPairButton(bool pressed) {
  // رنگ پایه: بنفش (بسته به کتابخانه‌ات، رنگ را تنظیم کن)
  uint16_t fillColor = ILI9341_PURPLE;

  // اگر در حالت HOLD (برای حذف) هستیم → رنگ چشمک‌زن بین بنفش و قرمز
  if (pairDeleteHoldActive) {
    fillColor = pairDeleteBlinkState ? ILI9341_RED : ILI9341_PURPLE;
  }

  // اگر KILL فعال است، می‌توانی یک رنگ خاص مثلاً قرمز ثابت بگذاری
  if (pairDeleteDoneActive) {
    fillColor = ILI9341_RED;
  }

  const char* label = getPairButtonText();

  tft.fillCircle(120, 20, 20, fixColor(fillColor));
  tft.setTextColor(ILI9341_WHITE, fillColor);
  tft.setFont(&FreeSansBold7pt7b);

  int16_t x;
  int len = strlen(label);
  if (len == 4) {
    x = 105;
  } else if (len == 3) {
    x = 106;
  } else if (len == 2) {
    x = 112;
  } else {
    x = 117;
  }
  tft.setCursor(x, 24);
  tft.print(label);
}

void startPairDeleteHold() {
  // هر چه مربوط به pairing/OK است قطع کن
  pairTimerActive     = false;
  pairOkActive        = false;
  lastPairSecondShown = -1;

  pairDeleteHoldActive  = true;
  pairDeleteDoneActive  = false;
  pairDeleteHoldStartMs = millis();
  pairDeleteLastBlinkMs = millis();
  pairDeleteBlinkState  = false;

  Serial.println("PAIR delete HOLD started");
  drawPairButton(false);
}

void cancelPairDeleteHold() {
  pairDeleteHoldActive = false;
  pairDeleteBlinkState = false;

  Serial.println("PAIR delete HOLD cancelled");
  drawPairButton(false);
}

void completePairDeleteHold() {
  pairDeleteHoldActive = false;
  pairDeleteDoneActive = true;
  pairDeleteBlinkState = false;

  // اینجا دستور پاک کردن whitelist را می‌فرستیم
  Serial.println("PAIR delete HOLD completed -> sending Y");
  sendCommand('Y');   // تابع خودت برای ارسال فرمان به اسپیکر

  // حالا KILL را به مدت PAIR_KILL_SHOW_TIME نشان بده
  pairKillUntilMs = millis() + PAIR_KILL_SHOW_TIME;

  drawPairButton(false);
}

void drawArtistTitle() {
  fillRoundRectHelper(10, 40, 220, 72, 15, fixColor(COLOR_GRAY));

  tft.setFont(&FreeSans7pt7b);
  if (currentDeviceConnected) {
    tft.setTextColor(fixColor(ILI9341_GREEN));
  } else {
    tft.setTextColor(fixColor(ILI9341_WHITE));
  }
  tft.setCursor(20, 60);
  tft.print(shortenText(currentDevice, 30));

  tft.setFont(&FreeSans7pt7b);
  tft.setTextColor(fixColor(ILI9341_CYAN));
  tft.setCursor(20, 80);
  tft.print(currentArtist); 
  
  tft.setFont(&FreeSans10pt7b);
  tft.setTextColor(fixColor(ILI9341_WHITE));
  tft.setCursor(20, 105);
  tft.print(currentTitle);
}

void drawRectButton(RectButton &btn, bool pressed = false) {
  uint16_t color = pressed ? btn.pressedColor : btn.color;
  fillRoundRectHelper(btn.x, btn.y, btn.w, btn.h, 15, fixColor(color));

  if (btn.command == 'B') {
    drawPrevIcon(btn.x + btn.w / 2, btn.y + btn.h / 2);
  } else if (btn.command == 'F') {
    drawNextIcon(btn.x + btn.w / 2, btn.y + btn.h / 2);
  } else {
    tft.setFont(&FreeSans12pt7b);
    tft.setTextColor(fixColor(ILI9341_WHITE));
    int16_t x1, y1;
    uint16_t tw, th;
    tft.getTextBounds(btn.label, 0, 0, &x1, &y1, &tw, &th);
    tft.setCursor(btn.x + (btn.w - tw) / 2, (btn.y + 15) + (btn.h - th) / 2);
    tft.print(btn.label);
  }
}

void drawPlayButton(bool pressed = false) {
  uint16_t color = pressed ? btnPlay.pressedColor : btnPlay.color;
  tft.fillCircle(btnPlay.cx, btnPlay.cy, btnPlay.r, fixColor(color));

  if (speakerPlaying)
    drawPauseIcon(btnPlay.cx, btnPlay.cy);
  else
    drawPlayIcon(btnPlay.cx, btnPlay.cy);
}

void drawVolumeLabel() {
  int boxX = 10;
  int boxY = 120;
  int boxW = 220;
  int boxH = 32;

  fillRoundRectHelper(boxX, boxY, boxW, boxH, 15, fixColor(COLOR_GRAY));

  String leftText  = formatTime(currentPosSec);
  String rightText = formatTime(totalLenSec);
  String midText   = (phoneVolumePercent < 0) ? "VOL --%" : "VOL " + String(phoneVolumePercent) + "%";

  int16_t x1, y1;
  uint16_t tw, th;

  tft.setFont(&FreeSans9pt7b);

  // Left
  tft.setTextColor(fixColor(ILI9341_WHITE));
  tft.getTextBounds(leftText, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(boxX + 10, boxY + 22);
  tft.print(leftText);

  // Center
  tft.setTextColor(fixColor(ILI9341_ORANGE));
  tft.getTextBounds(midText, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(boxX + (boxW - tw) / 2, boxY + 22);
  tft.print(midText);

  // Right
  tft.setTextColor(fixColor(ILI9341_WHITE));
  tft.getTextBounds(rightText, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(boxX + boxW - tw - 10, boxY + 22);
  tft.print(rightText);

  // برگشت به فونت پیش‌فرض
  tft.setFont();
}

void drawMainUI() {
  tft.fillScreen(fixColor(COLOR_BG));
  drawArtistTitle();
  drawStatusBar();
  drawVolumeLabel();
  drawRectButton(btnPrev, false);
  drawRectButton(btnNext, false);
  drawRectButton(btnVolUp, false);
  drawRectButton(btnVolDown, false);
  drawPlayButton(false);
}

void updatePlayButtonUI() {
  drawPlayButton(false);
}

// ---------- Screen Power ----------
void turnScreenOff() {
  if (!screenOn) return;

  screenOn = false;
  tapCount = 0;
  touchWasDown = false;
  activeTouch = TOUCH_NONE;
  longPressDone = false;

  tft.fillScreen(fixColor(ILI9341_BLACK));
  digitalWrite(TFT_BL, LOW);

  Serial.println("Screen sleep");
}

void turnScreenOn() {
  if (screenOn) return;

  digitalWrite(TFT_BL, HIGH);
  delay(50);

  screenOn = true;
  tapCount = 0;
  touchWasDown = false;
  activeTouch = TOUCH_NONE;
  longPressDone = false;
  lastTapTime = 0;
  lastTouchTime = millis();

  drawMainUI();

  Serial.println("Screen wake");
}

void handleScreenSleep() {
  if (screenOn && (millis() - lastTouchTime > SCREEN_TIMEOUT)) {
    turnScreenOff();
  }
}

void parseWhitelistList(String payload) {
  payload.trim();
  String deviceListText = "";
  bool anyConnected = false;
  bool first = true;
  int start = 0;
  while (start < payload.length()) {
    int sep = payload.indexOf('|', start);
    String item;
    if (sep == -1) {
      item = payload.substring(start);
      start = payload.length();
    } else {
      item = payload.substring(start, sep);
      start = sep + 1;
    }

    item.trim();
    if (item.length() == 0) {
      continue;
    }

    int comma = item.lastIndexOf(',');
    String devName;
    String connectedFlag = "0";
    if (comma != -1) {
      devName = item.substring(0, comma);
      connectedFlag = item.substring(comma + 1);
    } else {
      devName = item;
    }

    devName.trim();
    connectedFlag.trim();
    if (devName.length() == 0) {
      continue;
    }

    if (!first) {
      deviceListText += ", ";
    }
    deviceListText += devName;
    first = false;
    if (connectedFlag == "1") {
      anyConnected = true;
    }
  }

  if (deviceListText.length() == 0) {
    currentDevice = "No Device";
    currentDeviceConnected = false;
  } else {
    currentDevice = deviceListText;
    currentDeviceConnected = anyConnected;

  }
}

void handleSingleDevice(String payload) {
  payload.trim();
  int comma = payload.lastIndexOf(',');
  String devName;
  String connectedFlag = "0";
  if (comma != -1) {
    devName = payload.substring(0, comma);
    connectedFlag = payload.substring(comma + 1);
  } else {
    devName = payload;
  }
  
  devName.trim();
  connectedFlag.trim();
  if (devName.length() == 0) {
    devName = "No Device";
    connectedFlag = "0";
  }
  currentDevice = devName;
  currentDeviceConnected = (connectedFlag == "1");

  Serial.print("Current DEV: ");
  Serial.println(currentDevice);
}

// ---------- BLE Notify Handler ----------
void handleStateMessage(String value) {
  Serial.print("State: ");
  Serial.println(value);
  if (value == "PHONE:1") {
    phoneConnected = true;
    if (screenOn) {
      drawStatusBar();
    }
  }
  else if (value == "PHONE:0") {
    phoneConnected = false;
    currentDeviceConnected = false;
    Serial.println("PHONE disconnected.");
    if (currentDevice.length() == 0) {
      currentDevice = "No Device";
    }
    if (screenOn) {
      drawArtistTitle();
      drawStatusBar();
    }
  }
  else if (value.startsWith("WL:")) {
    String payload = value.substring(3);
    parseWhitelistList(payload);
    if (screenOn) {
      drawArtistTitle();
      drawStatusBar();
      drawVolumeLabel();
    }
  }
  else if (value.startsWith("DEV:")) {
    String payload = value.substring(4);
    String devName = payload;
    int devCount = 0;

    int commaIndex = payload.lastIndexOf(',');
    if (commaIndex >= 0) {
      devName = payload.substring(0, commaIndex);
      devCount = payload.substring(commaIndex + 1).toInt();
    }

    bool newDeviceAddedDuringPair =
      pairTimerActive &&
      devCount > 0 &&
      devName != "No Device" &&
      lastDevCount == 0;

    handleSingleDevice(payload);

    if (newDeviceAddedDuringPair) {
      pairTimerActive = false;
      lastPairSecondShown = -1;

      pairOkActive = true;
      pairOkUntilMs = millis() + PAIR_OK_DURATION;

      if (screenOn) {
        drawPairButton(false);
      }
    }

    lastDevName = devName;
    lastDevCount = devCount;

    if (screenOn) {
      drawArtistTitle();
      drawStatusBar();
    }
  }
  else if (value == "PLAY") {
    speakerPlaying = true;
    if (screenOn) {
      updatePlayButtonUI();
    }
  }
  else if (value == "PAUSE") {
    speakerPlaying = false;
    if (screenOn) {
      updatePlayButtonUI();
    }
  }
  else if (value.startsWith("ART:")) {
    currentArtist = value.substring(4);
    if(currentArtist.length() == 0) currentArtist =  StoreName;
    if (screenOn) {
      drawArtistTitle();
      drawStatusBar();
      drawVolumeLabel();
    }
  }
  else if (value.startsWith("TTL:")) {
    currentTitle = value.substring(4);
    if(currentTitle.length() == 0) currentTitle =  StoreAddress;
    if (screenOn) {
      drawArtistTitle();
      drawStatusBar();
      drawVolumeLabel();
    }
  }
  else if (value.startsWith("VOL:")) {
    phoneVolumePercent = value.substring(4).toInt();
    if (phoneVolumePercent < 0) {
      phoneVolumePercent = 0;
    }
    if (phoneVolumePercent > 100) {
      phoneVolumePercent = 100;
    }
    Serial.print("Volume Percent: ");
    Serial.println(phoneVolumePercent);
    if (screenOn) {
      drawVolumeLabel();
    }
  }
  else if (value.startsWith("POS:")) {
    currentPosSec = value.substring(4).toInt();
    if (currentPosSec < 0) {
      currentPosSec = 0;
    }
    Serial.print("Current Position Sec: ");
    Serial.println(currentPosSec);
    if (screenOn) {
      drawVolumeLabel();
    }
  }
  else if (value.startsWith("LEN:")) {
    totalLenSec = value.substring(4).toInt();
    if (totalLenSec < 0) {
      totalLenSec = 0;
    }
    Serial.print("Total Length Sec: ");
    Serial.println(totalLenSec);
    if (screenOn) {
      drawVolumeLabel();
    }
  }
}

// ---------- BLE CONNECT ----------
bool connectToSpeaker(BLEAdvertisedDevice device) {
  bleClient = BLEDevice::createClient();

  if (!bleClient->connect(&device)) {
    Serial.println("BLE connect failed");
    return false;
  }

  BLERemoteService* service = bleClient->getService(SERVICE_UUID);
  if (!service) {
    Serial.println("Service not found");
    bleClient->disconnect();
    return false;
  }

  cmdChar = service->getCharacteristic(CMD_CHAR_UUID);
  stateChar = service->getCharacteristic(STATE_CHAR_UUID);

  if (!cmdChar || !stateChar) {
    Serial.println("Characteristic missing");
    bleClient->disconnect();
    return false;
  }

  if (stateChar->canRead()) {
    String initialValue = stateChar->readValue().c_str();
    if (initialValue.length()) {
      handleStateMessage(initialValue);
    }
  }

  if (stateChar->canNotify()) {
    stateChar->registerForNotify(
      [](BLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
        String value = "";
        for (size_t i = 0; i < length; i++) value += (char)data[i];

        pendingStateMessage = value;
        hasPendingStateMessage = true;
      }
    );
  }


  bleConnected = true;
  if (screenOn) {
    drawArtistTitle();
    drawStatusBar();
    drawVolumeLabel();
    updatePlayButtonUI();
  }


  Serial.println("BLE CONNECTED");
  return true;
}

// ---------- BLE SCAN ----------
void tryBLEConnect() {
  if (millis() - lastBleAttempt < BLE_RETRY_INTERVAL) return;
  lastBleAttempt = millis();

  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);

  BLEScanResults* results = scan->start(2, false);

  for (int i = 0; i < results->getCount(); i++) {
    BLEAdvertisedDevice device = results->getDevice(i);

    if (device.haveServiceUUID() && device.isAdvertisingService(SERVICE_UUID)) {
      Serial.println("Speaker found");
      scan->clearResults();
      connectToSpeaker(device);
      return;
    }
  }

  scan->clearResults();
}

// ---------- Touch Helpers ----------
bool pointInRect(int x, int y, RectButton &btn) {
  return (x >= btn.x && x <= btn.x + btn.w &&
          y >= btn.y && y <= btn.y + btn.h);
}

bool pointInCircle(int x, int y, CircleButton &btn) {
  int dx = x - btn.cx;
  int dy = y - btn.cy;
  return (dx * dx + dy * dy) <= (btn.r * btn.r);
}

void sendCommand(char cmd) {
  Serial.print("Send command: ");
  Serial.println(cmd);

  if (bleConnected && cmdChar) {
    cmdChar->writeValue((uint8_t*)&cmd, 1);
  }
}

void animateRectButton(RectButton &btn) {
  drawRectButton(btn, true);
  delay(120);
  drawRectButton(btn, false);
}

void animatePlayButton() {
  drawPlayButton(true);
  delay(120);
  drawPlayButton(false);
}

// ---------- TOUCH ----------
void handleTouch() {
  bool touched = ts.touched();
  unsigned long now = millis();

  // ================= SCREEN OFF =================
  if (!screenOn) {
    static bool lastTouched = false;

    if (touched && !lastTouched) {
      unsigned long dt = now - lastTapTime;

      if (dt <= DOUBLE_TAP_GAP)
        tapCount++;
      else
        tapCount = 1;

      lastTapTime = now;

      Serial.print("tapCount=");
      Serial.println(tapCount);

      if (tapCount >= 2) {
        tapCount = 0;
        turnScreenOn();
      }
    }

    lastTouched = touched;
    return;
  }

  // ================= TOUCH RELEASE =================
  if (!touched) {
    if (touchWasDown) {
      if (activeTouch == TOUCH_PREV) {
        drawRectButton(btnPrev, false);
        if (!longPressDone) {
          sendCommand('B');   // Previous Track
        }
      }
      else if (activeTouch == TOUCH_NEXT) {
        drawRectButton(btnNext, false);
        if (!longPressDone) {
          sendCommand('F');   // Next Track
        }
      }
      else if (activeTouch == TOUCH_VOL_UP) {
        drawRectButton(btnVolUp, false);
      }
      else if (activeTouch == TOUCH_VOL_DOWN) {
        drawRectButton(btnVolDown, false);
      }
      else if (activeTouch == TOUCH_PLAY) {
        drawPlayButton(false);
      }
    }

    if (pairBtnDown) {
      if (pairDeleteHoldActive) {
        cancelPairDeleteHold();
      } else {
        drawPairButton(false);  // PURPLE
        if (!pairLongDone) { // اگر long press نشده بود، چک کن tap کن
          if (now - pairLastTapTime <= PAIR_DOUBLE_TAP_GAP) {
            pairTapCount++;
          } else {
            pairTapCount = 1;
          }
          pairLastTapTime = now;

          if (pairTapCount >= 2) {
            pairTapCount = 0;
            Serial.println("PAIR double tap: 30s");
            sendCommand('X');

            pairTimerActive = true;
            //pairTimerStart = millis();
            pairTimerEndMs = millis() + PAIR_TIMER_DURATION;
            lastPairSecondShown = -1;

            pairOkActive = false;

            drawPairButton(false);
          }
        }
      }
      pairBtnDown = false;
    }

    touchWasDown = false;
    activeTouch = TOUCH_NONE;
    longPressDone = false;
    return;
  }

  // ================= TOUCH ACTIVE =================
  TS_Point p = ts.getPoint();

  int x = map(p.x, 300, 3800, 0, 240);
  int y = map(p.y, 300, 3800, 0, 320);

  // ================= SCREEN ON =================
  lastTouchTime = now;
  tapCount = 0;

  // ================= TOUCH START =================
  if (!touchWasDown) {
    touchWasDown = true;
    touchStartTime = now;
    lastSeekSendTime = now;
    longPressDone = false;

    if (pointInRect(x, y, btnPrev)) {
      activeTouch = TOUCH_PREV;
      drawRectButton(btnPrev, true);
    }
    else if (pointInRect(x, y, btnNext)) {
      activeTouch = TOUCH_NEXT;
      drawRectButton(btnNext, true);
    }
    else if (pointInRect(x, y, btnVolUp)) {
      activeTouch = TOUCH_VOL_UP;
      drawRectButton(btnVolUp, true);
      sendCommand('U');
    }
    else if (pointInRect(x, y, btnVolDown)) {
      activeTouch = TOUCH_VOL_DOWN;
      drawRectButton(btnVolDown, true);
      sendCommand('D');
    }
    else if (pointInCircle(x, y, btnPlay)) {
      activeTouch = TOUCH_PLAY;
      drawPlayButton(true);
      sendCommand('P');
    }
    else if (pointInPairCircle(x, y)) {
      activeTouch = TOUCH_OTHER; // یعنی هیچ‌کدوم از اون‌های دیگه نباشه
      pairBtnDown = true;
      pairBtnDownTime = now;
      pairLongDone = false;
      drawPairButton(true);   // MAGENTA
    }
    else {
      activeTouch = TOUCH_OTHER;
    }

    return;
  }

  // ================= HOLDING TOUCH =================
  if (activeTouch == TOUCH_PREV) {
    if (!longPressDone && now - touchStartTime >= LONG_PRESS_TIME) {
      sendCommand('R');   // Rewind
      longPressDone = true;
      lastSeekSendTime = now;
    }

    if (longPressDone && now - lastSeekSendTime >= SEEK_REPEAT_TIME) {
      sendCommand('R');
      lastSeekSendTime = now;
    }
  }
  else if (activeTouch == TOUCH_NEXT) {
    if (!longPressDone && now - touchStartTime >= LONG_PRESS_TIME) {
      sendCommand('S');   // Fast Forward
      longPressDone = true;
      lastSeekSendTime = now;
    }

    if (longPressDone && now - lastSeekSendTime >= SEEK_REPEAT_TIME) {
      sendCommand('S');
      lastSeekSendTime = now;
    }
  }

  if (pairBtnDown && !pairLongDone && (now - pairBtnDownTime >= PAIR_HOLD_START_TIME)) {
    pairLongDone = true;
    pairTapCount = 0;
    Serial.println("PAIR HOLD started");
    startPairDeleteHold();
  }

}

void updatePairButtonState() {
  unsigned long now = millis();

  // --- OK timeout ---
  if (pairOkActive && now >= pairOkUntilMs) {
    pairOkActive = false;
    if (screenOn) {
      drawPairButton(false);
    }
  }

  // --- PAIR countdown ---
  if (pairTimerActive) {
    if (now >= pairTimerEndMs) {
      pairTimerActive = false;
      lastPairSecondShown = -1;
      if (screenOn) {
        drawPairButton(false);
      }
    } else {
      int sec = (pairTimerEndMs - now + 999) / 1000;
      if (sec != lastPairSecondShown) {
        lastPairSecondShown = sec;
        if (screenOn) {
          drawPairButton(false);
        }
      }
    }
  }

  // --- HOLD blink ---
  if (pairDeleteHoldActive) {
    if (now - pairDeleteLastBlinkMs >= PAIR_DELETE_BLINK_MS) {
      pairDeleteLastBlinkMs = now;
      pairDeleteBlinkState = !pairDeleteBlinkState;
      if (screenOn) {
        drawPairButton(false);
      }
    }

    // چک کن آیا 10 ثانیه کامل شده؟
    if (now - pairDeleteHoldStartMs >= PAIR_CLEAR_HOLD_TIME) {
      completePairDeleteHold();
    }
  }

  // --- KILL نمایش موقت ---
  if (pairDeleteDoneActive && now >= pairKillUntilMs) {
    pairDeleteDoneActive = false;
    if (screenOn) {
      drawPairButton(false);
    }
  }
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  BLEDevice::init("ESP32_Controller");

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(fixColor(ILI9341_BLACK));

  ts.begin();
  ts.setRotation(0);

  currentDevice = "No Device";
  currentDeviceConnected = false;
  phoneConnected = false;

  currentArtist = StoreName;
  currentTitle = StoreAddress;

  showSplashScreen();
  splashDrawn = true;
  splashStartMillis = millis();
  appState = STATE_SPLASH;

  lastTouchTime = millis();
}


// ---------- LOOP ----------
void loop() {
  unsigned long currentMillis = millis();
  if (appState == STATE_SPLASH) {
    if (currentMillis - splashStartMillis >= splashDuration) {
      if (!mainUiDrawn) {
        drawMainUI();
        mainUiDrawn = true;
      }
      appState = STATE_MAIN_UI;
    }
  }

  handleTouch();
  handleScreenSleep();
  updatePairButtonState();

  if (!bleConnected)
    tryBLEConnect();

  if (hasPendingStateMessage) {
    String msg = pendingStateMessage;
    hasPendingStateMessage = false;
    handleStateMessage(msg);
  }

  if (bleConnected && millis() - lastPing > 2000) {
    lastPing = millis();

    if (!bleClient || !bleClient->isConnected()) {
      bleConnected = false;
      cmdChar = nullptr;
      stateChar = nullptr;
      speakerPlaying = false;
      phoneConnected = false;
      currentDeviceConnected = false;
      currentDevice = "No Device";

      currentPosSec = 0;
      totalLenSec = 0;
      currentArtist = "";
      currentTitle = "";

      phoneVolumePercent = -1;

      if (screenOn && appState == STATE_MAIN_UI) drawMainUI();

      Serial.println("BLE Lost");
    }
  }
}
