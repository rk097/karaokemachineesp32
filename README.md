# 🎤 ESP32 Karaoke Machine

An embedded karaoke box using an ESP32 with:
- Real-time microphone input
- Bluetooth A2DP music streaming
- Vocal effects (echo/delay)
- Scrolling synced lyrics on OLED
- Adjustable mic and speaker volume
- One onboard test track

## 🔧 Features
- 🎙️ Mic input with digital processing
- 🎶 Bluetooth streaming from phone
- 🔊 Speaker and mic volume control
- ⏱️ Lyric scroll synced to song
- 🎛️ Echo effect toggle
- 📁 WAV file test track (on flash or SD)

## 🛒 Hardware
- ESP32-WROOM-32
- MAX9814 (mic) or INMP441
- MAX98357A I²S DAC/amp
- 0.96" I²C OLED (SSD1306)
- SD card reader (optional)
- Rotary encoder + pushbuttons
- Powered speaker

## 🗺️ Roadmap
- [ ] Mic passthrough
- [ ] I²S speaker output
- [ ] Echo effect
- [ ] A2DP sink streaming
- [ ] Lyric scroller synced to test track
- [ ] Volume control logic
- [ ] Integrated UX/UI

## 📦 License
MIT
