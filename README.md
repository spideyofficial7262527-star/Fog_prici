# Mochi WiFi GIF Player — ESP32 DevKit V1 + ILI9341

This project creates a Wi-Fi access point on the ESP32. From a phone, connect to the AP, open `http://192.168.4.1`, upload an animated `.gif`, and the ESP32 stores it in LittleFS flash and plays it on the TFT. It decodes GIF frames at runtime; it does not generate a huge C++ header or require PSRAM.

## Target hardware
- ESP32 DevKit V1 / ESP32-WROOM-32
- 2.8-inch ILI9341 TFT, native 240x320, landscape 320x240
- Current pin setup from the user's prior wiring:
  - TFT CS -> GPIO 5
  - TFT DC -> GPIO 2
  - TFT RST -> GPIO 4
  - TFT SCK -> GPIO 18
  - TFT MOSI/SDI -> GPIO 23
  - TFT MISO/SDO -> GPIO 19
  - TFT VCC -> 3.3V (follow the module's own specifications)
  - TFT GND -> GND

## Build
PlatformIO or GitHub Actions can build it. `platformio.ini` targets `board = esp32dev`, not ESP32-S3.

## Flashing
This is a PlatformIO project. The easiest method is to flash the complete `firmware.bin` using a compatible ESP web flasher that supports an ESP32 build's merged image, OR flash the separate files using their generated offsets. Do not guess offsets if the flashing tool asks for separate files; inspect the build's `flash_args` / `flash_args` output for the exact layout. Standard ESP32 Arduino layouts commonly use bootloader at `0x1000`, partitions at `0x8000`, and app at `0x10000`.

## Use from Android
1. Flash firmware once to the ESP32.
2. Power-cycle it.
3. On Android Wi-Fi settings connect to `Mochi-GIF`, password `mochigif123`.
4. Keep the phone connected even if Android says the Wi-Fi has no internet.
5. Open `http://192.168.4.1` in the browser.
6. Upload a small animated GIF. Start with 160x120 or 240x180, a modest frame count, and ideally under 1 MB.
7. It should start looping automatically.

## Notes
- Standard ESP32-WROOM boards generally have no PSRAM. This project uses AnimatedGIF's file callbacks and LittleFS so the full GIF is not loaded into RAM.
- The default 4 MB partition layout has limited filesystem space. A GIF must fit in the LittleFS partition; upload errors can mean it is too large.
- This project was generated for the user's reported pinout but has not been compiled or tested on their physical hardware. If the build fails, share the first compiler error. If it builds but the TFT stays blank, verify the ILI9341 driver and pin wiring.
