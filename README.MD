# ESPlay Retro Emulation  ![retro-emulation](https://raw.githubusercontent.com/pebri86/esplay-retro-emulation/master/docs/retro_emulation.png)

<a href="http://www.youtube.com/watch?feature=player_embedded&v=GPbRSVksWhE&t=4s" target="_blank"><img src="http://img.youtube.com/vi/GPbRSVksWhE&t=4s/0.jpg" alt="Youtube Video" width="240" height="180" border="10" /></a>

Retro Emulation Collection runs on [esplay-hardware](first prototype board) and [esplay-micro-hardware] (second prototype board - to be final prototype), an ESP32 Devices with WROVER Module (4MB PSRAM). 
- Nofrendo NES Emulator for play Nintendo ROM
- GNUBoy for play GameBoy and GameBoy Color
- SMSPlusGX could play Sega Master System, Game Gear and Coleco Vision ROM
- Esplay launcher an application to change rom and emulator selection, based on go-go launcher for odroid-go.

Launcher Option
---------------

Now you have options to choose launcher, Retro-ESP32 launcher by [32teeth] or ESPlay launcher by me.
ESPlay Launcher has Wifi AP options so you can upload rom by using web interface via phone or your computer. 

Here spoiler Retro-ESP32 launcher running on [esplay-micro-hardware]
![esplay-micro-hardware](https://raw.githubusercontent.com/pebri86/esplay-retro-emulation/master/docs/esplay-retro-esp32.png)

[esplay-hardware]: https://github.com/pebri86/esplay-hardware
[esplay-micro-hardware]: https://github.com/pebri86/esplay_micro_hardware

Compiling
---------

This code is an esp-idf project. You will need esp-idf to compile it. Newer versions of esp-idf may introduce incompatibilities with this code;
for your reference, the code was tested against release/v3.2 branch of esp-idf.
edit mkrelease.sh with correct location for your mktool.
Find mktool in the base firmware in my repository.

run mkrelease.sh to compile and make an .fw file that could be flashed by base firmware bootloader. Place .fw file in esplay/firmware folder on sdcard, power on esplay device while press B button until bootloader screen appear. select .fw then A -> Start to flash firmware.

Base firmware is a modified bootloader fork from odroid-go. 

ROM
--- 
Extract skeleton.zip from docs, and copy to the root of sdcard. Please provide ROM by yourself and place them in the appropriate folder on sdcard.

All Contributors
------------
Thanks to contribute your work for this project. 
- [32teeth]


[32teeth]:https://github.com/32teeth

