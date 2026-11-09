# Image assets

Images were converted to the LVGL binary format by LCD Wizard - there is
no separate conversion step. Everything in this folder goes onto the board.

## Files for the SPIFFS filesystem

| File | Source | Size | Format | Referenced as |
|---|---|---|---|---|
| `dovora_simplistic_day_partial_cloud_96.bin` | dovora_simplistic_day_partial_cloud_96.png | 96x96 | RGB565A8 | `S:/dovora_simplistic_day_partial_cloud_96.bin` |
| `overcast.bin` | overcast.png | 40x40 | RGB565A8 | `S:/overcast.bin` |
| `overcast_2.bin` | overcast.png | 48x48 | RGB565A8 | `S:/overcast_2.bin` |
| `day_clear.bin` | day_clear.png | 48x48 | RGB565A8 | `S:/day_clear.bin` |
| `day_partial_cloud.bin` | day_partial_cloud.png | 48x48 | RGB565A8 | `S:/day_partial_cloud.bin` |
| `rain.bin` | rain.png | 48x48 | RGB565A8 | `S:/rain.bin` |
| `cloudy.bin` | cloudy.png | 48x48 | RGB565A8 | `S:/cloudy.bin` |
| `snow.bin` | snow.png | 48x48 | RGB565A8 | `S:/snow.bin` |
| `rain_2.bin` | rain.png | 42x42 | RGB565A8 | `S:/rain_2.bin` |
| `partly_cloudy.bin` | partly_cloudy.png | 42x42 | RGB565A8 | `S:/partly_cloudy.bin` |
| `clear.bin` | clear.png | 42x42 | RGB565A8 | `S:/clear.bin` |
| `snow_2.bin` | snow.png | 42x42 | RGB565A8 | `S:/snow_2.bin` |
| `fog.bin` | fog.png | 42x42 | RGB565A8 | `S:/fog.bin` |
| `partly_cloudy_main_96x96.bin` | partly_cloudy_main_96x96.png | 96x96 | RGB565 | `S:/partly_cloudy_main_96x96.bin` |
| `partly_cloudy_96x96_tr.bin` | partly_cloudy_96x96_tr.png | 96x96 | RGB565A8 | `S:/partly_cloudy_96x96_tr.bin` |

Total: **143.1 KB** of roughly 896.0 KB available.

## Uploading

The files must sit in a folder named `data/` next to the `.ino`, then be
flashed as a filesystem image - they are **not** part of the sketch upload.

### PlatformIO

```bash
pio run -t uploadfs
```

### Arduino IDE

Arduino IDE 2 uploads a `data/` folder through a plugin:

1. Download the `.vsix` from [arduino-spiffs-upload](https://github.com/espx-cz/arduino-spiffs-upload/releases) into
   `C:\Users\<you>\.arduinoIDE\plugins\` on Windows, or `~/.arduinoIDE/plugins/` on macOS and
   Linux (make the `plugins` folder if it is not there), and restart the IDE.
2. Set **Tools > Partition Scheme** to **Huge APP (3MB No OTA/1MB SPIFFS)**. This uploader takes the
   filesystem's position from that menu and nowhere else - it does not read `partitions.csv` -
   and with *Custom* it stops with "Partitions not defined for this ESP32 board". That scheme
   is the same table as this project's `partitions.csv`, row for row.
3. Close the Serial Monitor, then press **Ctrl+Shift+P** and run **Upload SPIFFS to Pico/ESP8266/ESP32**.

It builds a 896 KB SPIFFS image from `data/` and writes it at `0x310000`.

The filesystem partition is set by `partitions.csv` in the project folder -
choose a layout with more room for files on the Hardware tab if these do not fit.

## How it is wired up

`ui_fs.cpp` registers SPIFFS with LVGL as drive `S:`, so a widget
refers to an image by path:

```c
lv_image_set_src(ui_Image1, "S:/dovora_simplistic_day_partial_cloud_96.bin");
```

`ui_fs_init()` runs from `setup()` before the UI is built, and prints a warning
for any file it cannot find so a missing image is obvious on the serial monitor.

## Changing an image

Re-import it in LCD Wizard and export again. Editing these files by hand means
the header and the pixel data can disagree, which shows up as a garbled image.
