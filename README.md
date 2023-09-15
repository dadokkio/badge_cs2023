# badge_cs2023

- [Original badge firmware](https://github.com/sqfmi/badgy) to see how to connect your board and flash

- To flash badge.ino.bin image use esptool:
  
```esptool.py –chip esp8266 –port /dev/ttyUSB0 -b 115200 write_flash –flash_size=detect -fm dio 0 badge.ino.bin```
