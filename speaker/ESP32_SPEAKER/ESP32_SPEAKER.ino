#include <Arduino.h>
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


/* ================= BLE UUID ================= */
#define BLE_DEVICE_NAME "ESP32_SPEAKER"

#define SERVICE_UUID     "12345678-1234-1234-1234-1234567890ab"
#define CMD_CHAR_UUID    "abcdefab-1234-5678-1234-abcdefabcdef"
#define STATE_CHAR_UUID  "fedcbafe-4321-8765-4321-fedcbafedcba"

/* ================= Audio ================= */
I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);

int currentVolume = 50;   // 0-100
bool wasConnected = false;
bool isPlaying = false;

/* ================= BLE ================= */
BLECharacteristic *cmdChar;
BLECharacteristic *stateChar = nullptr;


/* ================= Tone Generator ================= */
void playTone(float freq, int duration_ms) {
  const int sample_rate = 44100;
  const int num_samples = (sample_rate * duration_ms) / 1000;
  const float amplitude = 0.3;

  int16_t sample_buffer[2];

  for (int i = 0; i < num_samples; i++) {
    float t = (float)i / sample_rate;
    int16_t sample = (int16_t)(sin(2.0 * PI * freq * t) * amplitude * 32767);

    sample_buffer[0] = sample;
    sample_buffer[1] = sample;

    i2s.write((uint8_t*)sample_buffer, 4);
  }
}
void playStartupSound() {
  playTone(440, 100);
  delay(50);
  playTone(554, 100);
  delay(50);
  playTone(659, 150);
}
void playConnectSound() {
  playTone(523, 100);
  delay(50);
  playTone(659, 150);
}
void playDisconnectSound() {
  playTone(659, 100);
  delay(50);
  playTone(523, 150);
}


/* ================= BLE Callbacks ================= */
class SpeakerServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    Serial.println("✅ BLE Controller Connected");
  }

  void onDisconnect(BLEServer*) override {
    Serial.println("❌ BLE Controller Disconnected");
    delay(100);
    BLEDevice::startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String value = c->getValue();
    if (value.length() == 0) return;

    char cmd = value.charAt(0);
    Serial.printf("📩 Command: %c\n", cmd);

    switch (cmd) {
      case 'U':
        currentVolume = min(100, currentVolume + 10);
        a2dp_sink.set_volume(map(currentVolume, 0, 100, 0, 127));
        Serial.printf("🔊 Volume: %d\n", currentVolume);
        break;

      case 'D':
        currentVolume = max(0, currentVolume - 10);
        a2dp_sink.set_volume(map(currentVolume, 0, 100, 0, 127));
        Serial.printf("🔊 Volume: %d\n", currentVolume);
        break;

      case 'P':
        if (isPlaying) {
          a2dp_sink.pause();
          Serial.println("⏸ Paused");
        } else {
          a2dp_sink.play();
          Serial.println("▶ Playing");
        }
        isPlaying = !isPlaying;
        playTone(800, 100);
        // 🔔 اطلاع‌رسانی به کنترلر
        if (stateChar) {
            stateChar->setValue(isPlaying ? "PLAY" : "PAUSE");
            stateChar->notify();
        }
        break;
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("🔊 ESP32 BLE A2DP Speaker Starting...");

  /* ---------- I2S ---------- */
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.pin_bck  = 26;
  cfg.pin_ws   = 25;
  cfg.pin_data = 22;
  cfg.sample_rate = 44100;
  cfg.bits_per_sample = 16;
  cfg.channels = 2;
  i2s.begin(cfg);

  /* ---------- BLE ---------- */
  BLEDevice::init(BLE_DEVICE_NAME);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new SpeakerServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);

  BLECharacteristic* cmdChar = service->createCharacteristic(
    CMD_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  cmdChar->setCallbacks(new CommandCallbacks());
  
  BLECharacteristic* stateChar = service->createCharacteristic(
    STATE_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  stateChar->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("📡 BLE Advertising Started");

  /* ---------- A2DP ---------- */
  a2dp_sink.start(BLE_DEVICE_NAME);
  a2dp_sink.set_volume(map(currentVolume, 0, 100, 0, 127));

  playStartupSound();
  Serial.println("🎵 A2DP Ready");
}

void loop() {
  bool connected = a2dp_sink.is_connected();

  if (connected && !wasConnected) {
    Serial.println("📱 Bluetooth Audio Connected");
    playConnectSound();
    wasConnected = true;
    isPlaying = true;
    // 🔔 اتصال = شروع پخش
    if (stateChar) {
      stateChar->setValue("PLAY");
      stateChar->notify();
    }
  }
  else if (!connected && wasConnected) {
    Serial.println("📴 Bluetooth Audio Disconnected");
    playDisconnectSound();
    wasConnected = false;
    isPlaying = false;
    // 🔔 قطع اتصال = توقف
    if (stateChar) {
      stateChar->setValue("PAUSE");
      stateChar->notify();
    }
  }

  delay(300);
}
