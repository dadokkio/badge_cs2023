# badge_cs2023

- [Original badge firmware](https://github.com/sqfmi/badgy) to see how to connect your board and flash

### Required libraries:
- https://github.com/ZinggJM/GxEPD2_4G (4-color support for display)
- Adafruit GFX Library 1.11.7
- Arduinojson 6.21.3
- StreamUtils 1.7.3
- WiFiManager 12.0.16-rc.2

### How to flash:
  
```esptool.py –chip esp8266 –port /dev/ttyUSB0 -b 115200 write_flash –flash_size=detect -fm dio 0 badge.ino.bin```
