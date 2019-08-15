# ESPlay Retro Emulation  ![retro-emulation](https://raw.githubusercontent.com/pebri86/esplay-retro-emulation/master/docs/retro_emulation.png)

<a href="http://www.youtube.com/watch?feature=player_embedded&v=GPbRSVksWhE&t=4s" target="_blank"><img src="https://i.ytimg.com/vi/GPbRSVksWhE/hqdefault.jpg" alt="Youtube Video" width="240" height="180" border="10" /></a>

Retro Emulation Collection runs on [esplay-hardware](first prototype board) and [esplay-micro-hardware] (second prototype board - to be final prototype), an ESP32 Devices with WROVER Module (4MB PSRAM). All emulator have in-game menu pocketsprite style.
- Nofrendo NES Emulator for play Nintendo ROM
- GNUBoy for play GameBoy and GameBoy Color
- SMSPlusGX could play Sega Master System, Game Gear and Coleco Vision ROM
- Esplay launcher, new code base using UGUI library

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
for your reference, the code was tested against release/v3.3 branch of esp-idf.
edit mkrelease.sh with correct location for your mkfw.
Find mkfw tool in the base firmware in my repository.

edit your path (to match your location installation of esp-idf and clone of this repo). and simply execute mkrelease.sh
then you'll find esplay-retro-emu.fw in the root of repo.

run mkrelease.sh to compile and make an .fw file that could be flashed by base firmware bootloader. Place .fw file in esplay/firmware folder on sdcard, power on esplay device while press B button until bootloader screen appear. select .fw then A -> Start to flash firmware.

Base firmware is a modified bootloader fork from odroid-go.

ROM
---
Extract skeleton.zip from docs, and copy to the root of sdcard. Please provide ROM by yourself and place them in the appropriate folder on sdcard.

DONATION
--------
The software are free to use but we also accept small donation to make the project lives and continue development
<a href="https://www.paypal.me/pebri86/1.99USD" target="_blank"><img src="https://raw.githubusercontent.com/pebri86/esplay-retro-emulation/staging/docs/donate.jpeg" alt="Donate" /></a>

All Contributors
------------
Thanks to contribute your work for this project.
- [32teeth]


[32teeth]:https://github.com/32teeth

