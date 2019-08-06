#!/bin/sh

#edit your ESP-IDF path here, tested with release/v3.3 branch
export IDF_PATH=~/esp/esp-idf

#tune this to match yours
export ESPLAY_SDK=~/esp/esplay-retro-emulation/esplay-sdk

cd esplay-launcher
make -j4
cd ../esplay-gnuboy
make -j4
cd ../esplay-nofrendo
make -j4
cd ../esplay-smsplusgx
make -j4
cd ..
#ffmpeg -i Tile.png -f rawvideo -pix_fmt rgb565 tile.raw
/home/Peruri/esp/esplay-base-firmware/tools/mkfw/mkfw.exe Retro-Emulation assets/tile.raw 0 16 1048576 launcher esplay-launcher/build/RetroLauncher.bin 0 17 655360 esplay-nofrendo esplay-nofrendo/build/esplay-nofrendo.bin 0 18 655360 esplay-gnuboy esplay-gnuboy/build/esplay-gnuboy.bin 0 19 1376256 esplay-smsplusgx esplay-smsplusgx/build/esplay-smsplusgx.bin
rm esplay-retro-emu.fw
mv firmware.fw esplay-retro-emu.fw
