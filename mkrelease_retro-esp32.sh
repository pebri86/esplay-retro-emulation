#!/bin/sh

#edit your ESP-IDF path here, tested with release/v3.3 branch 
export IDF_PATH=~/esp/esp-idf

cd retro-esp32
make -j4
cd ../esplay-gnuboy
make -j4
cd ../esplay-nofrendo
make -j4
cd ../esplay-smsplusgx
make -j4
cd ..
#ffmpeg -i Tile.png -f rawvideo -pix_fmt rgb565 tile.raw
/home/Peruri/esp/esplay-base-firmware/tools/mkfw/mkfw.exe Retro-ESP32 assets/retro-esp32.raw 0 16 1179648 retro-esp32 retro-esp32/build/retro-esp32.bin 0 17 655360 esplay-nofrendo esplay-nofrendo/build/esplay-nofrendo.bin 0 18 655360 esplay-gnuboy esplay-gnuboy/build/esplay-gnuboy.bin 0 19 1310720 esplay-smsplusgx esplay-smsplusgx/build/esplay-smsplusgx.bin
rm retro-esp32.fw
mv firmware.fw retro-esp32.fw
