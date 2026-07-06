# Flashing chronolink_v4_os

Files:
- build\bootloader\bootloader.bin
- build\partition_table\partition-table.bin
- build\chronolink_v4_os.bin

Flash command (ESP32):
python -m esptool --chip esp32 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 2MB --flash-freq 40m 0x1000 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\chronolink_v4_os.bin

Or use idf.py (recommended when ESP-IDF environment is active):
idf.py -p COMx flash

After flashing, open monitor:
idf.py -p COMx monitor
