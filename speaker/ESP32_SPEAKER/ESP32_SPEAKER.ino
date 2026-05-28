// ESP32_SPEAKER_02.ino
// ESP32 Speaker MCP5102
#include <Arduino.h>
#include <math.h>
#include <Preferences.h>
#include <vector>
std::vector<String> connectedDeviceNames;

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "esp_avrc_api.h"

/* ================= BLE UUID ================= */
#define BLE_DEVICE_NAME "ESP32_SPEAKER"

#define SERVICE_UUID     "12345678-1234-1234-1234-1234567890ab"
#define CMD_CHAR_UUID    "abcdefab-1234-5678-1234-abcdefabcdef"
#define STATE_CHAR_UUID  "fedcbafe-4321-8765-4321-fedcbafedcba"

/* ================= AVRCP Compatibility ================= */
#ifndef ESP_AVRC_PT_CMD_FAST_FORWARD
#define ESP_AVRC_PT_CMD_FAST_FORWARD 0x49
#endif

#ifndef ESP_AVRC_PT_CMD_REWIND
#define ESP_AVRC_PT_CMD_REWIND 0x48
#endif

/* ================= Audio ================= */
I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);

int currentVolume = 50;   // 0-100
bool wasConnected = false;
bool phoneConnected = false;
bool isPlaying = false;

/* ================= BLE ================= */
BLECharacteristic *cmdChar = nullptr;
BLECharacteristic *stateChar = nullptr;
bool deviceConnected = false;

/* ================= Metadata ================= */
String currentArtist, currentTitle;

// مدت کل آهنگ و موقعیت فعلی بر حسب ثانیه
uint32_t currentPosSec = 0;
uint32_t totalLenSec   = 0;

/* ================= AVRCP Seek ================= */
uint8_t avrcTl = 0;

bool allowNewPairing = false; // این فلگ تعیین می‌کنه اجازه داریم دستگاه جدید اضافه کنیم یا نه
unsigned long pairingWindowStart = 0;

String currentConnectedName;
Preferences prefs;

// تعداد مجاز دستگاه در وایت‌لیست
const int MAX_DEVICES = 5;

unsigned long lastDevNotify = 0;
const unsigned long DEV_NOTIFY_INTERVAL = 3000;
int devNotifyIndex = 0;

/* ================= Forward Declarations ================= */
void notifyState(const String &msg);
void sendPlaybackState();
void sendArtist();
void sendTitle();
void sendVolume();
void sendPosition();
void sendLength();
uint8_t nextAvrcTl();

String getFullWhitelistAsString() {
  String msg = "WL:";
  prefs.begin("whitelist", true);

  for (int i = 0; i < MAX_DEVICES; i++) {
    String key = String(i);
    String name = prefs.getString(key.c_str(), "");

    if (name.length() > 0) {
      bool isConnected = false;

      for (size_t j = 0; j < connectedDeviceNames.size(); j++) {
        if (connectedDeviceNames[j] == name) {
          isConnected = true;
          break;
        }
      }

      msg += name;
      msg += ",";
      msg += (isConnected ? "1" : "0");
      msg += "|";
    }
  }

  prefs.end();

  if (msg.endsWith("|")) {
    msg.remove(msg.length() - 1);
  }

  return msg;
}

String getWhitelistDeviceByIndexMsg(int index) {
  // پیام: DEV:<name>,<0/1>
  prefs.begin("whitelist", true);

  String name = prefs.getString(String(index).c_str(), "");
  prefs.end();

  if (name.length() == 0) {
    return ""; // این اسلات خالی است
  }

  bool isConnected = false;
  for (size_t j = 0; j < connectedDeviceNames.size(); j++) {
    if (connectedDeviceNames[j] == name) {
      isConnected = true;
      break;
    }
  }

  String msg = "DEV:";
  msg += name;
  msg += ",";
  msg += (isConnected ? "1" : "0");
  return msg;
}

void notifyNextWhitelistDevice() {
  if (!deviceConnected || !stateChar) return;

  // در بدترین حالت MAX_DEVICES بار می‌چرخیم تا یک اسلات پر پیدا کنیم
  for (int tries = 0; tries < MAX_DEVICES; tries++) {
    String msg = getWhitelistDeviceByIndexMsg(devNotifyIndex);

    devNotifyIndex++;
    if (devNotifyIndex >= MAX_DEVICES) devNotifyIndex = 0;

    if (msg.length() > 0) {
      notifyState(msg);
      return;
    }
  }

  // اگر همه اسلات‌ها خالی بودند
  notifyState("DEV:No Device,0");
}

/* ================= Notify Helpers ================= */
void notifyState(const String &msg) {
  if (deviceConnected && stateChar) {
    stateChar->setValue(msg.c_str());
    stateChar->notify();
  }
  Serial.print("🔔 Notify: ");
  Serial.println(msg);
}

void sendPlaybackState() {
  notifyState(isPlaying ? "PLAY" : "PAUSE");
}

void sendArtist() {
  notifyState("ART:" + currentArtist);
}

void sendTitle() {
  notifyState("TTL:" + currentTitle);
}

void sendVolume() {
  String msg = "VOL:" + String(currentVolume);
  Serial.print("Sending volume notify: ");
  Serial.println(msg);
  notifyState(msg);
}

void sendPosition() {
  notifyState("POS:" + String(currentPosSec));
}

void sendLength() {
  notifyState("LEN:" + String(totalLenSec));
}

void addDeviceToWhitelist(String deviceName) {
  if (deviceName == "" || deviceName == "Unknown") return; // اسم‌های نامعتبر رو اضافه نکن

  prefs.begin("whitelist", false);
  
  bool exists = false;
  for(int i = 0; i < MAX_DEVICES; i++) {
    if(prefs.getString(String(i).c_str(), "") == deviceName) {
      exists = true;
      break;
    }
  }

  if(!exists) {
    for(int i = 0; i < MAX_DEVICES; i++) {
      if(prefs.getString(String(i).c_str(), "") == "") {
        prefs.putString(String(i).c_str(), deviceName);
        Serial.println("✅ Added: " + deviceName + " to whitelist.");
        break;
      }
    }
  }
  prefs.end();
}


void printWhitelist() {
  prefs.begin("whitelist", true); // فقط خواندنی
  Serial.println("--- Current Whitelist ---");
  
  bool empty = true;
  for(int i = 0; i < MAX_DEVICES; i++) {
    String mac = prefs.getString(String(i).c_str(), "");
    if(mac != "") {
      Serial.println("Device " + String(i) + ": " + mac);
      empty = false;
    }
  }
  
  if(empty) Serial.println("Whitelist is empty!");
  Serial.println("-------------------------");
  prefs.end();
}

void clearWhitelist() {
  prefs.begin("whitelist", false);
  prefs.clear(); // پاک کردن همه داده‌های این namespace
  prefs.end();
  Serial.println("Whitelist cleared successfully.");
  notifyState("ART:" + String(""));
  notifyState("TTL:" + String(""));
  a2dp_sink.disconnect();
}

/* ================= AVRCP Helpers ================= */
uint8_t nextAvrcTl() {
  avrcTl++;

  if (avrcTl > 15) {
    avrcTl = 0;
  }

  return avrcTl;
}

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
    deviceConnected = true;
    Serial.println("✅ BLE Controller Connected");

    sendPlaybackState();
    sendArtist();
    sendTitle();
    sendVolume();
    sendPosition();
    sendLength();
    devNotifyIndex = 0;
    lastDevNotify = 0; // باعث میشه سریع در loop ارسال بشه
    // یا مستقیم:
    notifyNextWhitelistDevice();

    //notifyState(getFullWhitelistAsString());

  }
  void onDisconnect(BLEServer*) override {
    deviceConnected = false;
    Serial.println("❌ BLE Controller Disconnected");

    delay(100);
    BLEDevice::startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String value = c->getValue();
    if (value.length() == 0) {
      return;
    }

    char cmd = value.charAt(0);
    Serial.printf("📩 Command: %c\n", cmd);
    switch (cmd) {
      case 'U':
        currentVolume = min(100, currentVolume + 10);
        a2dp_sink.set_volume(map(currentVolume, 0, 100, 0, 127));
        Serial.printf("🔊 Volume: %d\n", currentVolume);
        sendVolume();
        break;
      case 'D':
        currentVolume = max(0, currentVolume - 10);
        a2dp_sink.set_volume(map(currentVolume, 0, 100, 0, 127));
        Serial.printf("🔊 Volume: %d\n", currentVolume);
        sendVolume();
        break;
      case 'P':
        if (isPlaying) {
          a2dp_sink.pause();
          Serial.println("⏸ Paused");
          isPlaying = false;
        } else {
          a2dp_sink.play();
          Serial.println("▶ Playing");
          isPlaying = true;
        }
        playTone(800, 100);
        sendPlaybackState();
        break;
      case 'B':
        Serial.println("⏮ Previous");
        a2dp_sink.previous();
        break;
      case 'F':
        Serial.println("⏭ Next");
        a2dp_sink.next();
        break;
      case 'R':
        Serial.println("⏪ Rewind Start");
        a2dp_sink.rewind();
        break;
      case 'S':
        Serial.println("⏩ Fast Forward Start");
        a2dp_sink.fast_forward();
        break;
      case 'T':
        Serial.println("⏹ Seek Stop");
        break;
      case 'X':
        allowNewPairing = true;
        pairingWindowStart = millis();
        printWhitelist();
        Serial.println("🔓 Pairing window opened for 30 seconds!");
        break;
      case 'Y':
        Serial.println("Received: Clear Whitelist");
        clearWhitelist();
        printWhitelist(); // بعد از پاک شدن نشون بده که خالیه
        break;

      default:
        Serial.print("⚠ Unknown command: ");
        Serial.println(cmd);
        break;
    }
  }
};


String limitText(String text, int maxLen) {
  text.trim();
  if (text.length() > maxLen) {
    text = text.substring(0, maxLen);
  }
  return text;
}

/* ================= AVRCP Metadata Callback ================= */
void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
  if (text == nullptr) return;

  String value = String((const char *)text);
  value.trim();

  if (value.length() == 0) return;

  if (id == ESP_AVRC_MD_ATTR_TITLE) {
    String newTitle = limitText(value, 20);

    if (newTitle != currentTitle) {
      currentTitle = newTitle;
      sendTitle();
    }
  }
  else if (id == ESP_AVRC_MD_ATTR_ARTIST) {
    String newArtist = limitText(value, 20);

    if (newArtist != currentArtist) {
      currentArtist = newArtist;
      sendArtist();
    }
  }
  else if (id == ESP_AVRC_MD_ATTR_PLAYING_TIME) {
    // معمولاً میلی‌ثانیه به صورت رشته می‌آید
    uint32_t totalMs = value.toInt();

    if (totalMs > 0) {
      totalLenSec = totalMs / 1000;
      Serial.printf("⏱ Total Length: %lu sec\n", (unsigned long)totalLenSec);
      sendLength();
    }
  }
}

/* ================= AVRCP Playback Status Callback ================= */
void playstatus_changed(esp_avrc_playback_stat_t playback) {
  Serial.printf("▶ AVRCP Play Status: %d\n", playback);

  switch (playback) {
    case ESP_AVRC_PLAYBACK_STOPPED:
    case ESP_AVRC_PLAYBACK_PAUSED:
      isPlaying = false;
      break;

    case ESP_AVRC_PLAYBACK_PLAYING:
      isPlaying = true;
      break;

    default:
      break;
  }

  sendPlaybackState();
}

/* ================= AVRCP Play Position Callback ================= */
void play_position_changed(uint32_t play_pos_ms) {
  // بعضی گوشی‌ها ممکن است 0xFFFFFFFF برگردانند یعنی نامعتبر
  if (play_pos_ms == 0xFFFFFFFF) {
    Serial.println("⏱ Play position invalid");
    return;
  }

  currentPosSec = play_pos_ms / 1000;

  Serial.printf("⏱ Current Position: %lu sec\n", (unsigned long)currentPosSec);

  sendPosition();
}

/* ================= Optional A2DP State Callbacks ================= */
void audio_state_changed(esp_a2d_audio_state_t state, void *ptr) {
  Serial.printf("🎧 Audio state changed: %d\n", state);

  if (state == ESP_A2D_AUDIO_STATE_STARTED) {
    isPlaying = true;
    sendPlaybackState();
  }
  else if (state == ESP_A2D_AUDIO_STATE_STOPPED ||
           state == ESP_A2D_AUDIO_STATE_SUSPEND) {
    isPlaying = false;
    sendPlaybackState();
  }
}

void peer_name_callback(char *peer_name) {
  if (peer_name == nullptr) return;
  String name = String(peer_name);
  currentConnectedName = name;
  Serial.print("📱 Peer phone name: ");
  Serial.println(name);

  // اضافه کردن به لیست (اگر قبلاً نبود)
  bool found = false;
  for(auto &d : connectedDeviceNames) if(d == name) found = true;
  if(!found) connectedDeviceNames.push_back(name);

  if (allowNewPairing) {
    addDeviceToWhitelist(name);
    allowNewPairing = false;
    Serial.println("✅ Device added to whitelist.");
  }

  if (!isDeviceAllowed(name)) {
    Serial.println("⛔ Unauthorized device: " + name + ". Disconnecting...");
    a2dp_sink.disconnect(); 
  } else {
    Serial.println("✅ Device authorized.");
  }
  notifyNextWhitelistDevice();
}

// تابعی برای چک کردن اینکه آیا گوشی در لیست سفید هست یا نه
bool isDeviceAllowed(String deviceName) {
  prefs.begin("whitelist", true); // فقط خواندنی
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (prefs.getString(String(i).c_str(), "") == deviceName) {
      prefs.end();
      return true; // پیدا شد!
    }
  }
  prefs.end();
  return false; // نبود
}

void connection_state_changed(esp_a2d_connection_state_t state, void *ptr) {
  Serial.printf("📶 A2DP connection state: %d\n", state);

  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    phoneConnected = true;
    notifyState("PHONE:1");
  } 
  else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    phoneConnected = false;
    // پاک کردن از لیست
    for (auto it = connectedDeviceNames.begin(); it != connectedDeviceNames.end(); ++it) {
      if (*it == currentConnectedName) {
        connectedDeviceNames.erase(it);
        break;
      }
    }
    notifyNextWhitelistDevice();
    notifyState("PHONE:0");
    notifyState("TTL:" + String(""));
    notifyState("ART:" + String(""));
  }
}


void volume_changed(int volume) {
  // volume معمولاً بین 0 تا 127 است
  currentVolume = map(volume, 0, 127, 0, 100);

  if (currentVolume < 0) currentVolume = 0;
  if (currentVolume > 100) currentVolume = 100;

  Serial.printf("📱 Phone Volume Changed: raw=%d percent=%d\n", volume, currentVolume);

  sendVolume();
}

/* ================= Setup ================= */
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

  cmdChar = service->createCharacteristic(
    CMD_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_WRITE_NR
  );

  stateChar = service->createCharacteristic(
    STATE_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_READ
  );

  cmdChar->setCallbacks(new CommandCallbacks());

  stateChar->addDescriptor(new BLE2902());
  stateChar->setValue("PAUSE");

  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println("📡 BLE Advertising Started");

  /* ---------- A2DP ---------- */
  a2dp_sink.set_peer_name_callback(peer_name_callback);
  a2dp_sink.set_on_audio_state_changed(audio_state_changed);
  a2dp_sink.set_on_connection_state_changed(connection_state_changed);

  // مهم: playing time را هم درخواست کن
  a2dp_sink.set_avrc_metadata_attribute_mask(
    ESP_AVRC_MD_ATTR_TITLE |
    ESP_AVRC_MD_ATTR_ARTIST |
    ESP_AVRC_MD_ATTR_PLAYING_TIME
  );

  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  //a2dp_sink.set_on_connection_state_changed(connection_state_changed);

  // وضعیت پخش
  a2dp_sink.set_avrc_rn_playstatus_callback(playstatus_changed);

  // موقعیت فعلی پخش - هر 1 ثانیه
  a2dp_sink.set_avrc_rn_play_pos_callback(play_position_changed, 1);

  // Volume from phone / AVRCP
  a2dp_sink.set_avrc_rn_volumechange(volume_changed);

  a2dp_sink.start(BLE_DEVICE_NAME);
  a2dp_sink.set_volume(map(currentVolume, 0, 100, 0, 127));

  playStartupSound();

  Serial.println("🎵 A2DP Ready");
}

/* ================= Loop ================= */
void loop() {
  bool connected = a2dp_sink.is_connected();
  if (connected && !wasConnected) {
    Serial.println("📱 Bluetooth Audio Connected");
    playConnectSound();
    wasConnected = true;
    isPlaying = true;
    sendPlaybackState();
    sendArtist();
    sendTitle();
    sendVolume();
    sendPosition();
    sendLength();
  }
  else if (!connected && wasConnected) {
    Serial.println("📴 Bluetooth Audio Disconnected");
    playDisconnectSound();
    wasConnected = false;
    isPlaying = false;
    currentPosSec = 0;
    totalLenSec = 0;
    sendPlaybackState();
    sendPosition();
    sendLength();
  }
  if (allowNewPairing && (millis() - pairingWindowStart > 30000)) {
    allowNewPairing = false;
    Serial.println("🔒 Pairing window closed.");
    printWhitelist();
  }
  if (deviceConnected && (millis() - lastDevNotify >= DEV_NOTIFY_INTERVAL)) {
    lastDevNotify = millis();
    notifyNextWhitelistDevice();
  }
  delay(300);
}
