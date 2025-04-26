#! /bin/bash
# Requires the installation of
# - jq
# - esptool
# - platformio
#
VDPTAG=`curl  "https://api.github.com/repos/AgonPlatform/agon-vdp/tags" | jq -r '.[0].name'`
MOSTAG=`curl  "https://api.github.com/repos/AgonPlatform/agon-mos/tags" | jq -r '.[0].name'`
RELEASE=MOS_$MOSTAG-VDP_$VDPTAG.bin
rm ./data/MOS.bin
wget https://github.com/AgonPlatform/agon-mos/releases/latest/download/MOS.bin -P ./data
rm ./firmware/input/firmware.bin
wget https://github.com/AgonPlatform/agon-vdp/releases/latest/download/firmware.bin -P ./firmware/input
pio run --target buildfs
pio run
esptool.py --chip esp32 merge_bin -o ./firmware/merged.bin --flash_mode dio --flash_freq 40m --flash_size 4MB 0x1000 "./.pio/build/esp32dev/bootloader.bin" 0x8000 "./.pio/build/esp32dev/partitions.bin" 0x10000 "./.pio/build/esp32dev/firmware.bin" 0x150000 "./firmware/input/firmware.bin" 0x290000 "./.pio/build/esp32dev/spiffs.bin"

