
// ===============================
// ESP32 LCD BLE Controller (FINAL)
// ===============================

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <BLEDevice.h>

// ---------- TFT & TOUCH ----------
#define TFT_CS   27
#define TFT_DC   17
#define TFT_RST  4
#define TOUCH_CS 26

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
bool speakerPlaying = false;

// ---------- UI ----------
#define PLAY_BTN_INDEX 2

struct Button {
  int x, y, w, h;
  const char* label;
  char command;
};

Button buttons[] = {
  {20,  40, 90, 55, "Vol +", 'U'},
  {120, 40, 90, 55, "Vol -", 'D'},
  {75, 105, 90, 55, "Play", 'P'},
  {20,  170, 90, 55, "Back", 'B'},
  {120, 170, 90, 55, "Forward", 'F'}
};

// ---------- Timing ----------
unsigned long lastPing = 0;
unsigned long lastBleAttempt = 0;
const unsigned long BLE_RETRY_INTERVAL = 3000;

// ---------- Helpers ----------
uint16_t fixColor(uint16_t c) {
  uint8_t r = (c >> 11) & 0x1F;
  uint8_t g = (c >> 5) & 0x3F;
  uint8_t b = c & 0x1F;
  return (b << 11) | (g << 5) | r;
}

void drawCenteredText(Button& b, uint16_t color) {
  tft.setTextColor(color);
  tft.setTextSize(2);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(b.label, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2);
  tft.print(b.label);
}

void drawButton(Button& b, bool highlight) {
  uint16_t bg = highlight ? fixColor(ILI9341_YELLOW) : fixColor(ILI9341_BLUE);
  uint16_t fg = highlight ? fixColor(ILI9341_BLACK)  : fixColor(ILI9341_WHITE);
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, bg);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, fixColor(ILI9341_WHITE));
  drawCenteredText(b, fg);
}

void drawStatusBar() {
  tft.fillRect(0, 0, 40, 30,
    fixColor(bleConnected ? ILI9341_GREEN : ILI9341_RED));
  tft.setTextColor(fixColor(ILI9341_WHITE));
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print(bleConnected ? "CC" : "CD");
}

void updatePlayButtonUI() {
  buttons[PLAY_BTN_INDEX].label = speakerPlaying ? "PAUSE" : "PLAY";
  drawButton(buttons[PLAY_BTN_INDEX], false);
}

// ---------- BLE CONNECT ----------
bool connectToSpeaker(BLEAdvertisedDevice device) {
  bleClient = BLEDevice::createClient();

  if (!bleClient->connect(&device)) {
    Serial.println("❌ BLE connect failed");
    return false;
  }

  BLERemoteService* service = bleClient->getService(SERVICE_UUID);
  if (!service) {
    Serial.println("❌ Service not found");
    return false;
  }

  cmdChar = service->getCharacteristic(CMD_CHAR_UUID);
  stateChar = service->getCharacteristic(STATE_CHAR_UUID);

  if (!cmdChar || !stateChar) {
    Serial.println("❌ Characteristic missing");
    return false;
  }

  // ✅ REGISTER NOTIFY (LAMBDA)
  if (stateChar->canNotify()) {
    stateChar->registerForNotify(
      [](BLERemoteCharacteristic*,
         uint8_t* data,
         size_t length,
         bool) {

        String value = "";
        for (size_t i = 0; i < length; i++)
          value += (char)data[i];

        Serial.print("🔔 State: ");
        Serial.println(value);

        if (value == "PLAY")  speakerPlaying = true;
        if (value == "PAUSE") speakerPlaying = false;

        updatePlayButtonUI();
      }
    );
  }

  bleConnected = true;
  drawStatusBar();
  Serial.println("✅ BLE CONNECTED");
  return true;
}

// ---------- BLE SCAN ----------
void tryBLEConnect() {
  if (millis() - lastBleAttempt < BLE_RETRY_INTERVAL) return;
  lastBleAttempt = millis();

  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);

  BLEScanResults* results = scan->start(4, false);

  for (int i = 0; i < results->getCount(); i++) {
    BLEAdvertisedDevice device = results->getDevice(i);

    if (device.haveServiceUUID() &&
        device.isAdvertisingService(SERVICE_UUID)) {

      Serial.println("✅ Speaker found");
      scan->clearResults();
      connectToSpeaker(device);
      return;
    }
  }
  scan->clearResults();
}

// ---------- TOUCH ----------
void handleTouch() {
  if (!ts.touched()) return;

  TS_Point p = ts.getPoint();
  int x = map(p.x, 300, 3800, 0, 320);
  int y = map(p.y, 300, 3800, 0, 240);

  for (int i = 0; i < 5; i++) {
    Button& b = buttons[i];
    if (x >= b.x && x <= b.x + b.w &&
        y >= b.y && y <= b.y + b.h) {

      drawButton(b, true);

      if (bleConnected && cmdChar) {
        cmdChar->writeValue((uint8_t*)&b.command, 1);
      }

      delay(180);
      drawButton(b, false);
      break;
    }
  }
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);

  BLEDevice::init("ESP32_Controller");

  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(fixColor(ILI9341_BLACK));

  ts.begin();
  ts.setRotation(3);

  drawStatusBar();
  for (int i = 0; i < 5; i++)
    drawButton(buttons[i], false);
}

// ---------- LOOP ----------
void loop() {
  if (!bleConnected)
    tryBLEConnect();

  if (bleConnected && millis() - lastPing > 2000) {
    lastPing = millis();
    if (!bleClient->isConnected()) {
      bleConnected = false;
      drawStatusBar();
      Serial.println("❌ BLE Lost");
    }
  }

  handleTouch();
}
