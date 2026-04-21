# ESP32-C3 E-Reader

A DIY e-book reader built with an ESP32-C3 Super Mini and a 2.13" e-paper display. Books are uploaded via WiFi and read on the device with full pagination, bookmarks, and sleep mode.

![Schematic](Schematic_kindle.png)

## Hardware

- **Board**: ESP32-C3 Super Mini
- **Display**: 2.13" DEPG0213BN e-paper (SSD1680, 122×250 pixels, B/W)
- **Storage**: LittleFS (internal flash)

### Pin Connections

| Component | GPIO | Notes |
|-----------|------|-------|
| Display CS | 5 | SPI |
| Display DC | 17 | |
| Display RST | 16 | |
| Display BUSY | 4 | |
| Display SCK | 18 | SPI |
| Display MOSI | 23 | SPI |
| Button UP | 12 | INPUT_PULLUP |
| Button DOWN | 13 | INPUT_PULLUP |
| Button LEFT | 14 | INPUT_PULLUP |
| Button RIGHT | 27 | INPUT_PULLUP |
| Wake Button | 25 | Deep sleep wake |
| Battery ADC | 32 | Via voltage divider |

### Battery (Optional)

Connect a voltage divider (two 100K resistors) between battery (+) and GND, with the tap point to GPIO 32:
- Battery (+) → 100K → GPIO 32 → 100K → GND

## Features

- **Book Management**: Upload books via WiFi web interface
- **Pagination**: Navigate pages with UP/DOWN buttons
- **Bookmarks**: Save bookmarks (long-press RIGHT), view on device or web
- **Reading Progress**: Auto-saves last page per book
- **Sleep Mode**: Auto-sleep after configurable timeout, manual sleep (long-press LEFT)
- **Wake on Any Button**: Resume from any button press
- **Battery Display**: Shows battery percentage in footer (if wired)
- **Dark Mode**: Toggle via web settings

## Web Interface

Access at `http://192.168.4.1` (default AP: **E-Reader**, password: **12345678**)

| Page | Description |
|------|-----------|
| `/` | Upload and manage books |
| `/library` | Browse and download books |
| `/edit` | Rename books |
| `/settings` | Dark mode, sleep timeout |
| `/bookmarks.html` | View all bookmarks |

## Controls

### Book List Screen
- **UP**: Show bookmarks
- **DOWN**: Select next book
- **RIGHT**: Open selected book
- **LEFT**: Open settings
- **Long-press LEFT**: Enter sleep

### Reading Screen
- **UP**: Next page
- **DOWN**: Previous page
- **RIGHT**: Next page
- **LEFT**: Return to book list
- **Long-press RIGHT**: Save bookmark

### Bookmarks Screen
- **UP/DOWN**: Select bookmark
- **RIGHT**: Open bookmarked page
- **LEFT**: Return to book list
- **Long-press DOWN**: Delete bookmark

### Settings Screen
- **UP/DOWN**: Select setting
- **RIGHT**: Change value
- **LEFT**: Return to book list

## Building

1. Install [ESP32 board support](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
2. Install libraries via Arduino IDE Library Manager:
   - GxEPD2
   - ESPAsyncWebServer
   - AsyncTCP
3. Upload sketch to ESP32
4. Upload `data/` folder to LittleFS:
   - Using Arduino IDE: Tools > ESP32 LittleFS Data Upload
   - Or via command line: `python mklittlefs.py -p 2264 0x1000 .data /tmp/littlefs.bin`
   - ESP32 uploads at: `http://192.168.4.1/littlefs`

```
ESP32-C3 → E-Paper Display
─────────────────────────────────────
GPIO 5   → CS
GPIO 17  → DC
GPIO 16  → RST
GPIO 4   → BUSY
GPIO 18  → SCK
GPIO 23  → MOSI
GND      → GND
3V3      → VCC

ESP32-C3 → Buttons (to GND)
─────────────────────────────────────
GPIO 12  → UP button
GPIO 13  → DOWN button
GPIO 14  → LEFT button
GPIO 27  → RIGHT button
GPIO 25  → WAKE button
```

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/list` | GET | List all books as JSON |
| `/download` | GET | Download book file |
| `/delete` | GET | Delete a book |
| `/upload` | POST | Upload a book |
| `/api/rename` | POST | Rename a book |
| `/api/progress` | GET | Get reading progress |
| `/api/bookmarks` | GET | List bookmarks |
| `/api/bookmarks` | DELETE | Delete bookmark |
| `/api/settings` | GET/POST | Get/update settings |

## Files

- `epaper-reader-esp32.ino` - Main sketch
- `data/` - Web UI files (HTML, CSS)
- `TASKS.md` - Feature tracking

## License

MIT