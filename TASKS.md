# E-Reader ESP32 - Task List

## Task Display Function

When displaying tasks from this file, format as a markdown table:
- Sort by priority: High → Medium → Low
- Completed tasks ([x]) go to the bottom with ✅ prefix
- Columns: Order # | Task Description | Priority | Status

| Order | Task Description | Priority | Status |
|-------|------------------|----------|--------|
| 1 | Task name | High/Medium/Low | Pending |
| ... | ... | ... | ... |
| ✅ | Completed task | Medium | Done |

End with summary: "X tasks remaining (Y completed)"

## Status Legend
- [x] Completed
- [ ] Pending
- [ ] In Progress

---

## Bug Fixes

### [x] Fix: Space Rendering Bug in `drawReadingPage()`
**Description**: Spaces between words were not rendering correctly
**Status**: Fixed

**Root Cause**:
1. `getTextBounds(word + " ")` was measuring word+space width, then `cursorX += tbw` advanced by that combined width — but the space was already rendered, so the next word's position was calculated incorrectly
2. Long words (26+ chars) printed without wrapping check, causing text overflow

**Fix Applied**:
1. Measure space width separately using `getTextBounds(" ", ...)` once at start
2. Print word and space as separate `print()` calls
3. Advance cursor by `tbw + spaceWidth` (actual word + space widths)
4. Wrapping check for long words already present (the overflow issue was in cursor position calculation)

**Files Modified**:
- `epaper-reader-esp32.ino` (lines 391-437)

---

## Features

### [x] Feature 1: Reading with Pagination
**Description**: Full book reading with page navigation and page count display
**Status**: Completed

**Implemented**:
- Page navigation with RIGHT (next), DOWN (prev), UP (next)
- Page indicator in header: `(1/15)`
- Reading position persistence (resumes where you left off)
- `saveProgress()`/`loadProgress()` for last page tracking

**Files Modified**:
- `epaper-reader-esp32.ino`

---

### [x] Feature 2: Bookmark System
**Description**: Add bookmarks via device button (long-press)
**Status**: Completed

**Implemented**:
- Store bookmarks in `/bookmarks.txt`
- Format: `filename.txt|page|timestamp`
- Long-press RIGHT button to add bookmark
- `*` indicator on bookmarked pages
- "Bookmark saved" feedback

**Files Modified**:
- `epaper-reader-esp32.ino`

### [x] Feature 2b: Display Bookmarks on Device
**Description**: View and navigate to bookmarks via device screen
**Status**: Completed

**Implemented**:
- Bookmark count indicator in book list header `[n]`
- UP button from empty book list opens bookmarks screen
- UP button from book list shows bookmark count
- Bookmarks screen showing all bookmarks with book name and page
- RIGHT to open bookmarked book at that page
- DOWN long-press to delete selected bookmark
- LEFT to return to book list

**Files Modified**:
- `epaper-reader-esp32.ino`

---

### [x] Features 3 & 4: Bookmark Web UI
**Description**: View and manage bookmarks via web interface
**Status**: Completed

**Implemented**:
- `/bookmarks.html` page listing all bookmarks
- `GET /api/bookmarks` - list all bookmarks as JSON
- `DELETE /api/bookmarks?index=N` - delete bookmark by index
- `GET /api/bookmark/content?file=xxx&page=N` - get page text
- Bookmarks grouped by book name
- View button to see bookmarked page content
- Copy to clipboard with "- BookName" suffix
- Delete bookmarks from web UI

**Files Created/Modified**:
- `data/bookmarks.html` (new)
- `data/bookmark.html` (new)
- `epaper-reader-esp32.ino`
- `data/index.html` (added link)

---

### [x] Feature 5: Reading Progress Persistence
**Description**: Remember last page for each book
**Status**: Completed (part of Feature 1)

**Implementation**:
- File: `/progress.txt`
- Format: `filename.txt|page`
- Auto-saved when leaving book
- Auto-restored when opening book

---

### [ ] Feature 6: Arabic Book Support
**Description**: Display RTL Arabic text correctly
**Status**: Pending

**Requirements**:
- Add Arabic font (e.g., NotoNaskhArabic)
- Detect Arabic content (Unicode 0600-06FF)
- RTL text wrapping
- Auto-detect or manual Arabic mode toggle

**Files to Create/Modify**:
- `data/fonts/NotoNaskhArabic_Regular.h` (new)
- `epaper-reader-esp32.ino`
- `data/settings.html`

---

### [ ] Feature 6b: SD Card Storage
**Description**: Add SD card reader for storing books instead of internal flash
**Status**: Pending

**Requirements**:
- Connect SD card module (SPI: MISO, MOSI, SCK, CS)
- Use SD library or SDFat for file operations
- Move book storage from LittleFS to SD card
- Update `/list`, `/upload`, `/download` endpoints for SD
- Support larger library (GB+ storage)
- Handle SD card presence/insertion detection

**Files to Modify**:
- `epaper-reader-esp32.ino`
- Web API handlers for file operations

---

### [x] Feature 7: Battery Level Display
**Description**: Show battery percentage on e-paper display
**Status**: Completed

**Hardware**:
- Voltage divider (halved) connected to GPIO 32 (ADC1_CHANNEL_4)
- LiPo battery (4.2V = 100%, 3.0V = 0%)

**Requirements**:
- Define `#define BATT_PIN 32`
- Implement `readBattery()` function
- Draw battery icon + percentage in top-right corner
- Update every 60 seconds

**Files to Modify**:
- `epaper-reader-esp32.ino`

---

### [x] Feature 8: Sleep Mode
**Description**: Deep sleep with dedicated wake button
**Status**: Completed

**Implemented**:
- Auto-sleep after 1 minute of inactivity (60s for testing)
- Manual sleep via long-press LEFT (>2s) on book list screen
- Wake button on GPIO 25 using `esp_sleep_enable_ext1_wakeup()`
- State saved before sleeping (progress, settings)
- State restored on wake (book, page, app state)
- Activity tracking on all button presses

**Files Modified**:
- `epaper-reader-esp32.ino`

### [x] Feature 8a: Configurable Sleep Timeout
**Description**: Make sleep timeout configurable via settings
**Status**: Completed

**Implemented**:
- Added `sleepTimeoutMs` variable (default: 5 minutes = 300000ms)
- Updated `loadSettings()`/`saveSettings()` with format: `dark|300000`
- Added `/api/settings` GET endpoint to return current settings
- Added `/api/settings` POST endpoint to update settings
- Web settings page has slider (1-10 minutes) with live value display
- Dark mode toggle now saves to device (not just localStorage)
- "Saved" toast notification on save
- Device settings screen now shows: Dark Mode, Sleep Timeout, Apply
- Device: UP/DOWN to navigate, RIGHT to change value
- Device: Sleep cycles 1-10 minutes

**Files Modified**:
- `epaper-reader-esp32.ino`
- `data/settings.html`

### [x] Feature 8b: Wake on Any Button
**Description**: Allow any button to wake from sleep
**Status**: Completed

**Implemented**:
- All 5 pins (WAKE, UP, DOWN, LEFT, RIGHT) configured as wake sources
- `esp_sleep_get_ext1_wakeup_status()` detects which button woke the device
- RIGHT button wake: Resume to reading if was reading
- Other buttons wake: Go to book list

**Files Modified**:
- `epaper-reader-esp32.ino`

### [x] Feature 8c: Light Sleep Optimization
**Description**: CPU sleeps between loop iterations for lower power consumption
**Status**: Completed

**Implemented**:
- ISRs for all buttons (UP, DOWN, LEFT, RIGHT, WAKE)
- `setupLightSleepWakeup()` configures GPIO wakeup for light sleep
- `esp_sleep_enable_timer_wakeup(100000)` for 100ms timer interval
- `esp_light_sleep_start()` at end of loop()
- Volatile flags cleared after wakeup

**Files Modified**:
- `epaper-reader-esp32.ino`

---

### [x] Feature 9: Web UI - Library Page
**Description**: View books and download them
**Status**: Completed

**Implemented**:
- `/library` page listing all books
- `/download?name=xxx` endpoint
- `/api/progress` endpoint
- Book count badge
- Reading progress with progress bar
- Download button for each book

**Files Created/Modified**:
- `data/library.html` (new)
- `epaper-reader-esp32.ino`
- `data/index.html` (added link)

---

### [x] Feature 10: Display Optimization
**Description**: Reduce screen flashing
**Status**: Completed

**Implemented**:
- State tracking variables to detect content changes
- `needsRedraw()` helper returns false for redundant redraws
- `updateDrawState()` updates tracking after successful draws
- Partial refresh for bookmark saved notification (20px strip instead of full screen)
- Skip button press redraws when selection hasn't changed

**Files Modified**:
- `epaper-reader-esp32.ino`

---

## Project Overview

**Hardware**:
- ESP32-C3 Super Mini
- 2.13" DEPG0213BN e-paper (SSD1680, 122x250)
- SPI: CS=5, DC=17, RST=16, BUSY=4, SCK=18, MOSI=23
- Buttons: UP=12, DOWN=13, LEFT=14, RIGHT=27
- Battery: LiPo via voltage divider to GPIO 32
- Wake button: GPIO 25

**Storage**: LittleFS (internal flash)

**Web UI**: Served from LittleFS at http://192.168.4.1

**Web Pages**:
- `/` - Upload/manage books
- `/library` - View and download books
- `/edit` - Rename books
- `/settings` - Settings (dark mode)
- `/bookmarks.html` - Bookmark list (grouped by book)
- `/bookmark.html` - View bookmark content

**API Endpoints**:
- `GET /list` - List books as JSON
- `GET /download?name=xxx` - Download book file
- `GET /api/progress` - Get reading progress for all books
- `POST /api/rename` - Rename a book
- `GET /delete?name=xxx` - Delete a book
- `POST /upload` - Upload a book
- `GET /api/bookmarks` - List all bookmarks as JSON
- `DELETE /api/bookmarks?index=N` - Delete bookmark by index
- `GET /api/bookmark/content?file=xxx&page=N` - Get page text content

---

## Implementation Order (Recommended)

1. Feature 5 (progress) - Foundation - **DONE**
2. Feature 1 (pagination) - Core reading - **DONE**
3. Feature 2 (bookmarks) - Bookmark storage - **DONE**
4. Feature 2b (bookmarks screen) - Display bookmarks on device - **DONE**
5. Features 3&4 (bookmark web UI) - Bookmark management - **DONE**
6. Feature 10 (display optimization) - UX improvement - **DONE**
7. Feature 8 (sleep mode) - Power optimization - **DONE**
8. Feature 8a (configurable sleep) - Settings improvement - **DONE**
  9. Feature 7 (battery) - Hardware display
 10. Feature 8b (wake on any button) - UX improvement - **DONE**
 11. Feature 8c (light sleep) - Power optimization - **DONE**
12. Feature 6 (Arabic) - Localization
