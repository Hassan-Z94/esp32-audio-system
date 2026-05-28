🎧 ESP32 A2DP + BLE Speaker with LCD Controller
A complete wireless speaker system built using ESP32, featuring:

🔊 Bluetooth A2DP Audio Streaming
📱 BLE Remote Control
🖥️ Touch LCD Interface
🎚️ Volume & Play/Pause Control
🔔 System Sounds (Startup / Connect / Disconnect)
📦 Project Structure
esp32-audio-system/
├─ README.md
├─ controller/
│  └─ ESP32_CONTROLLER/
│     └─ ESP32_CONTROLLER.ino
│     └─ FreeSans7pt7b.h
│     └─ FreeSans10pt7b.h
│     └─ FreeSansBold7pt7b.h
│     └─ partitions.csv
│     └─ splash.h
├─ speaker/
│  └─ ESP32_SPEAKER/
│     └─ ESP32_SPEAKER.ino


🔊 Speaker (ESP32 + PCM5102)
✅ Features
Bluetooth A2DP Sink
BLE Server
Volume Control
Play / Pause
Startup / Connect / Disconnect sounds
Event-driven architecture (callbacks)
📡 BLE Configuration
Item	Value
Service UUID	12345678-1234-1234-1234-1234567890ab
Command Characteristic	abcdefab-1234-5678-1234-abcdefabcdef
Mode	WRITE (Controller → Speaker)
🎵 Supported Commands
P → Toggle Play/Pause

→ Volume Up
→ Volume Down
📱 Controller (ESP32 + TFT + Touch)
✅ Features
TFT Display UI
Touchscreen Control
BLE Client
Auto-connect to speaker
Sends Play/Pause & Volume commands
📡 BLE Client Behavior
Connects to speaker using Service UUID
Finds Command Characteristic
Sends control commands
🛠 Hardware Requirements
Speaker Side
ESP32
PCM5102 I2S DAC
Amplifier
Speaker
Power Supply
Controller Side
ESP32
TFT Display (ILI9341 or similar)
XPT2046 Touch Controller
🔌 Wiring (Speaker - I2S Example)
ESP32 Pin	PCM5102
26	BCK
25	LCK
22	DIN
GND	GND
3.3V	VCC
⚠️ Verify pins according to your board configuration.

📚 Required Libraries
Install from Arduino Library Manager:

✅ ESP32 Board Package
✅ BluetoothA2DPSink
✅ ESP32 BLE Arduino
✅ TFT_eSPI (for controller)
✅ XPT2046_Touchscreen
⚙️ How It Works
Speaker boots
Plays startup sound
Starts A2DP service
Starts BLE server
Controller connects via BLE
User touches UI buttons
Commands sent via BLE
Speaker executes command
All audio streaming is handled by A2DP,

Control channel is handled by BLE.

🧠 Architecture Overview
Phone → A2DP → ESP32 Speaker → PCM5102 → AMP → Speaker

↑

│ BLE

↓

ESP32 LCD Controller

🚀 Future Improvements (Planned)
✅ Two-way State Sync (PLAYING / PAUSED / VOL)
🔁 Auto-Reconnect BLE
🎵 AVRCP Track Info
🔋 Battery Monitoring
🌙 Sleep Mode
📡 OTA Updates
🎛 Equalizer
🐞 Known Limitations
Current BLE communication is one-way (no state feedback)
Controller uses fixed MAC address (can be improved)
No AVRCP metadata yet
👨‍💻 Author
ESP32 A2DP + BLE Speaker Project

Built with ❤️ using ESP32
