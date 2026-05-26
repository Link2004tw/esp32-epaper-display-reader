// ============================================================
//  ESP32 — DIY E-Reader
//  WiFi/LittleFS server  +  GxEPD2 e-paper book reader
//
//  Display : 2.13" DEPG0213BN  (122 x 250, SSD1680, B/W)
//  Pins    : CS=5  DC=17  RST=16  BUSY=4  (SPI: SCK=18 MOSI=23)
//  Buttons : UP=12  DOWN=13  LEFT=14  RIGHT=27  (INPUT_PULLUP)
//  Battery : ADC pin 32
//
//  Button actions (book list screen):
//    UP    → scroll up; at top → bookmarks screen
//    DOWN  → scroll selection down
//    RIGHT → open selected book
//    LEFT  → settings screen
//
//  Button actions (reading screen):
//    UP    → previous page
//    DOWN  → next page
//    RIGHT → next page
//    LEFT  → return to book list
//
//  Web UI files live in LittleFS:
//    data/index.html   → http://192.168.4.1/
//    data/edit.html    → http://192.168.4.1/edit
// ============================================================

// ---------- Includes ----------
#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include "Arabi12pt7b.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#define CS_PIN   5
#define DC_PIN   17
#define RES_PIN  16
#define BUSY_PIN 4

// Display layout constants (rotation=1 → landscape: 250 × 122 px)
#define VISIBLE_ROWS  5
#define ROW_H         17
#define LIST_Y_START  22
#define SCREEN_W      250
#define SCREEN_H      122

GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(
  GxEPD2_213_BN(CS_PIN, DC_PIN, RES_PIN, BUSY_PIN));

AsyncWebServer server(80);

const char* AP_SSID = "E-Reader";
const char* AP_PASS = "12345678";

// ---------- Buttons ----------
const uint8_t BTN_UP    = 12;
const uint8_t BTN_DOWN  = 13;
const uint8_t BTN_LEFT  = 14;
const uint8_t BTN_RIGHT = 27;

#define BATT_PIN        32
#define BTN_DEBOUNCE_MS 50
#define LONG_PRESS_MS   800

int lastBtnUpState    = HIGH, lastBtnDownState  = HIGH;
int lastBtnLeftState  = HIGH, lastBtnRightState = HIGH;
unsigned long lastBtnUpTime    = 0, lastBtnDownTime   = 0;
unsigned long lastBtnLeftTime  = 0, lastBtnRightTime  = 0;
unsigned long pressStartUp     = 0, pressStartDown    = 0;
unsigned long pressStartLeft   = 0, pressStartRight   = 0;

// ---------- Book list state ----------
#define MAX_BOOKS 20
String bookNames[MAX_BOOKS];
int    bookCount     = 0;
int    selectedIndex = 0;

// ---------- Settings ----------
bool darkMode = false;
bool arabicMode = false;

// ---------- Battery ----------
int           batteryPercent    = 100;
unsigned long lastBatteryUpdate = 0;

// ---------- Reading state ----------
String currentBook = "";
int    currentPage = 1;
int    totalPages  = 1;

// ---------- Bookmark state ----------
#define MAX_BOOKMARKS 20
String        bookmarkFiles[MAX_BOOKMARKS];
int           bookmarkPages[MAX_BOOKMARKS];
unsigned long bookmarkTimes[MAX_BOOKMARKS];
int           bookmarkCount    = 0;
int           selectedBookmark = 0;

const int CHARS_PER_LINE = 26;
const int LINES_PER_PAGE = 5;
const int CHARS_PER_PAGE = CHARS_PER_LINE * LINES_PER_PAGE;
const int LINE_HEIGHT     = 14;
const int CONTENT_Y_START = 32;
const int CONTENT_MAX_Y   = SCREEN_H - 12;

// ============================================================
//  Forward declarations
// ============================================================
bool needsRedraw();
void updateDrawState();
void drawBookList();
void drawSettings();
void drawBookmarksScreen();
void drawReadingPage();
void loadBookList();
void loadBookmarks();
void saveBookmark(const String& filename, int page);
void getTheme(uint16_t& bg, uint16_t& fg);
void drawScreenHeader(const char* title, const String& rightText);
void drawScreenFooter(const char* hint);
void updateScrollOffset(int selected, int& scrollOffset);
bool pollButton(uint8_t pin, int& lastState, unsigned long& lastChangeTime,
                unsigned long& pressStartMs, unsigned long now,
                bool& shortPress, bool& longPress);

// ============================================================
//  Settings helpers
// ============================================================
void loadSettings() {
  File f = LittleFS.open("/settings.txt", "r");
  if (f) {
    String line = f.readStringUntil('\n');
    line.trim();
    int sep = line.indexOf('|');
    if (sep > 0) {
      darkMode = (line.substring(0, sep) == "dark");
      arabicMode = (line.substring(sep + 1).toInt() == 1);
    } else {
      darkMode = (line == "dark");
      arabicMode = false;
    }
    f.close();
  }
}

void saveSettings() {
  File f = LittleFS.open("/settings.txt", "w");
  if (f) {
    f.print(darkMode ? "dark" : "light");
    f.print("|");
    f.println(arabicMode ? "1" : "0");
    f.close();
  }
}

// ============================================================
//  Arabic text detection
// ============================================================
// Detect Arabic (check for UTF-8 Arabic encoding bytes)
// ============================================================
bool detectArabicText(const String& text, int checkLen) {
  int arabicCount = 0;
  int check = min((int)text.length(), checkLen);
  for (int i = 0; i < check; i++) {
    unsigned char c = (unsigned char)text.charAt(i);
    // UTF-8 Arabic: D9 xx, DA xx, DB xx (first byte of 2-byte sequences)
    if (c == 0xD9 || c == 0xDA || c == 0xDB) {
      arabicCount++;
      if (i + 1 < check) i++; // Skip second byte if available
    }
  }
  // Lower threshold: 10% Arabic
  return (arabicCount * 3 >= check);
}

// Get first page content for Arabic detection
bool detectArabicBook(const String& filename) {
  File f = LittleFS.open("/" + filename, "r");
  if (!f) return false;
  String sample = "";
  for (int i = 0; i < 500 && f.available(); i++) {
    char c = f.read();
    if (c >= ' ') sample += c;  // Skip control chars
  }
  f.close();
  // Lower threshold: if just 10% Arabic bytes, enable RTL
  return detectArabicText(sample, 500);
}

// ============================================================
//  Battery helpers
// ============================================================
void initBattery() {
  analogReadResolution(12);
}

int readBattery() {
  int   raw      = analogRead(BATT_PIN);
  float batteryV = raw * 6.6f / 4095.0f;
  int   percent  = map((int)(batteryV * 100), 270, 420, 0, 100);
  return constrain(percent, 0, 100);
}

// ============================================================
//  Reading progress helpers
// ============================================================
int loadProgress(const String& filename) {
  File f = LittleFS.open("/progress.txt", "r");
  if (!f) return 1;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    int sep = line.indexOf('|');
    if (sep > 0 && line.substring(0, sep) == filename) {
      int page = line.substring(sep + 1).toInt();
      f.close();
      return page > 0 ? page : 1;
    }
  }
  f.close();
  return 1;
}

void saveProgress(const String& filename, int page) {
  File   f       = LittleFS.open("/progress.txt", "r");
  String content = "";
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      int sep = line.indexOf('|');
      if (sep > 0 && line.substring(0, sep) != filename)
        content += line + "\n";
    }
    f.close();
  }
  f = LittleFS.open("/progress.txt", "w");
  if (f) {
    f.print(content);
    f.print(filename);
    f.print('|');
    f.println(page);
    f.close();
  }
}

// ============================================================
//  Bookmark helpers
// ============================================================
bool hasBookmark(const String& filename, int page) {
  File f = LittleFS.open("/bookmarks.txt", "r");
  if (!f) return false;
  String check = filename + "|" + String(page) + "|";
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.startsWith(check)) { f.close(); return true; }
  }
  f.close();
  return false;
}

void loadBookmarks() {
  bookmarkCount = 0;
  File f = LittleFS.open("/bookmarks.txt", "r");
  if (!f) return;
  while (f.available() && bookmarkCount < MAX_BOOKMARKS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int sep1 = line.indexOf('|');
    int sep2 = line.indexOf('|', sep1 + 1);
    if (sep1 > 0 && sep2 > sep1) {
      bookmarkFiles[bookmarkCount] = line.substring(0, sep1);
      bookmarkPages[bookmarkCount] = line.substring(sep1 + 1, sep2).toInt();
      bookmarkTimes[bookmarkCount] = line.substring(sep2 + 1).toInt();
      bookmarkCount++;
    }
  }
  f.close();
  if (selectedBookmark >= bookmarkCount)
    selectedBookmark = max(0, bookmarkCount - 1);
}

void saveBookmark(const String& filename, int page) {
  if (hasBookmark(filename, page)) return;
  File f = LittleFS.open("/bookmarks.txt", "a");
  if (f) {
    f.print(filename);
    f.print('|');
    f.print(page);
    f.print('|');
    f.println(millis());
    f.close();
  }
  loadBookmarks();
}

void deleteBookmark(int index) {
  if (index < 0 || index >= bookmarkCount) return;
  File   f       = LittleFS.open("/bookmarks.txt", "r");
  String content = "";
  if (f) {
    int cur = 0;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      if (cur != index) content += line + "\n";
      cur++;
    }
    f.close();
  }
  f = LittleFS.open("/bookmarks.txt", "w");
  if (f) { f.print(content); f.close(); }

  for (int i = index; i < bookmarkCount - 1; i++) {
    bookmarkFiles[i] = bookmarkFiles[i + 1];
    bookmarkPages[i] = bookmarkPages[i + 1];
    bookmarkTimes[i] = bookmarkTimes[i + 1];
  }
  bookmarkCount--;
  if (selectedBookmark >= bookmarkCount)
    selectedBookmark = max(0, bookmarkCount - 1);
}

// ============================================================
//  System-file filter
// ============================================================
bool isSystemFile(const String& name) {
  return name.startsWith(".")
      || name == "bookname.txt"
      || name == "settings.txt"
      || name == "progress.txt"
      || name == "bookmarks.txt"
      || name == "index.html"
      || name == "edit.html"
      || name == "settings.html"
      || name == "bookmarks.html"
      || name == "bookmark.html"
      || name == "library.html";
}

// ============================================================
//  LittleFS helpers
// ============================================================
void loadBookList() {
  bookCount = 0;
  File root = LittleFS.open("/");
  File f    = root.openNextFile();
  while (f && bookCount < MAX_BOOKS) {
    String name = String(f.name());
    if (!isSystemFile(name)) bookNames[bookCount++] = name;
    f = root.openNextFile();
  }
  if (selectedIndex >= bookCount) selectedIndex = max(0, bookCount - 1);
  loadBookmarks();
}

// ---------- Pagination ----------
int getFileCharCount(const String& filename) {
  File f = LittleFS.open("/" + filename, "r");
  if (!f) return 0;
  int sz = f.size();
  f.close();
  return sz;
}

void calculatePages(const String& filename) {
  totalPages = max(1, (getFileCharCount(filename) / CHARS_PER_PAGE) + 1);
}

// ============================================================
//  App state machine
// ============================================================
enum AppState { STATE_LIST, STATE_READING, STATE_SETTINGS, STATE_BOOKMARKS };
AppState appState = STATE_LIST;

// ---------- Settings screen selection ----------
int settingsSelectedIndex = 0;

// ---------- Display dirty-tracking ----------
AppState lastAppState        = STATE_LIST;
int      lastSelectedIndex   = -1;
int      lastSelectedBkmk    = -1;
int      lastSettingsSel     = -1;
String   lastCurrentBook     = "";
int      lastCurrentPage     = -1;
bool     lastDarkMode        = false;
bool     lastArabicMode     = false;

bool needsRedraw() {
  if (lastAppState != appState)       return true;
  if (lastDarkMode != darkMode)       return true;
  if (lastArabicMode != arabicMode)   return true;
  if (appState == STATE_LIST     && lastSelectedIndex  != selectedIndex)      return true;
  if (appState == STATE_BOOKMARKS && lastSelectedBkmk  != selectedBookmark)   return true;
  if (appState == STATE_SETTINGS) {
    if (lastSettingsSel    != settingsSelectedIndex) return true;
  }
  if (appState == STATE_READING) {
    if (lastCurrentBook != currentBook) return true;
    if (lastCurrentPage != currentPage) return true;
  }
  return false;
}

void updateDrawState() {
  lastAppState       = appState;
  lastDarkMode       = darkMode;
  lastArabicMode   = arabicMode;
  lastSelectedIndex  = selectedIndex;
  lastSelectedBkmk   = selectedBookmark;
  lastSettingsSel    = settingsSelectedIndex;
  lastCurrentBook    = currentBook;
  lastCurrentPage    = currentPage;
}

// ============================================================
//  Display helpers
// ============================================================
void getTheme(uint16_t& bg, uint16_t& fg) {
  bg = darkMode ? GxEPD_BLACK : GxEPD_WHITE;
  fg = darkMode ? GxEPD_WHITE : GxEPD_BLACK;
}

void drawScreenHeader(const char* title, const String& rightText) {
  uint16_t bg, fg;
  getTheme(bg, fg);
  display.fillRect(0, 0, SCREEN_W, 18, fg);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(bg);
  display.setCursor(4, 13);
  display.print(title);
  if (rightText.length() > 0) {
    display.setFont(&FreeMono9pt7b);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(rightText, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(SCREEN_W - tbw - 4, 13);
    display.print(rightText);
  }
}

void drawScreenFooter(const char* hint) {
  uint16_t bg, fg;
  getTheme(bg, fg);
  display.drawLine(0, SCREEN_H - 10, SCREEN_W, SCREEN_H - 10, fg);
  display.setFont(0);
  display.setTextColor(fg);
  display.setCursor(2, SCREEN_H - 8);
  display.print(hint);
}

void updateScrollOffset(int selected, int& scrollOffset) {
  if (selected < scrollOffset) scrollOffset = selected;
  if (selected >= scrollOffset + VISIBLE_ROWS)
    scrollOffset = selected - VISIBLE_ROWS + 1;
}

bool pollButton(uint8_t pin, int& lastState, unsigned long& lastChangeTime,
                unsigned long& pressStartMs, unsigned long now,
                bool& shortPress, bool& longPress) {
  shortPress = false;
  longPress  = false;
  int raw = digitalRead(pin);
  if (raw == lastState) return false;
  if (now - lastChangeTime <= (unsigned long)BTN_DEBOUNCE_MS) return false;

  lastChangeTime = now;
  lastState = raw;
  if (raw == LOW) {
    pressStartMs = now;
    return true;
  }
  unsigned long held = now - pressStartMs;
  if (held >= (unsigned long)LONG_PRESS_MS) longPress = true;
  else shortPress = true;
  return true;
}

// ============================================================
//  E-paper: book list screen
// ============================================================
void drawBookList() {
  display.setRotation(1);
  display.setFullWindow();

  static int scrollOffset = 0;
  updateScrollOffset(selectedIndex, scrollOffset);

  if (!needsRedraw()) return;

  uint16_t bg, fg;
  getTheme(bg, fg);

  String headerRight = String(bookCount) + " book" + (bookCount != 1 ? "s" : "");
  if (bookmarkCount > 0)
    headerRight = "[" + String(bookmarkCount) + "] " + headerRight;

  display.firstPage();
  do {
    display.fillScreen(bg);
    drawScreenHeader("E-READER", headerRight);

    if (bookCount == 0) {
      display.setTextColor(fg);
      display.setCursor(6, LIST_Y_START + ROW_H);
      display.print("No books found.");
      display.setCursor(6, LIST_Y_START + ROW_H * 2);
      display.print("Upload via WiFi:");
      display.setCursor(6, LIST_Y_START + ROW_H * 3);
      display.print("192.168.4.1");
    } else {
      for (int i = 0; i < VISIBLE_ROWS; i++) {
        int  bookIdx    = scrollOffset + i;
        if (bookIdx >= bookCount) break;
        int  rowY       = LIST_Y_START + i * ROW_H;
        bool isSelected = (bookIdx == selectedIndex);

        if (isSelected) {
          display.fillRect(0, rowY, SCREEN_W, ROW_H - 1, fg);
          display.setTextColor(bg);
        } else {
          display.setTextColor(fg);
        }

        String label = bookNames[bookIdx];
        if (label.endsWith(".txt"))   label = label.substring(0, label.length() - 4);
        if (label.length() > 30)      label = label.substring(0, 28) + "..";

        display.setFont(&FreeMono9pt7b);
        display.setCursor(isSelected ? 6 : 4, rowY + ROW_H - 4);
        display.print((isSelected ? "> " : "  ") + label);
      }
    }

    drawScreenFooter("UP/DN:sel TOP:bkmk L:set R:open");
  }
  while (display.nextPage());
  updateDrawState();
}

// ============================================================
//  E-paper: settings screen
// ============================================================
void drawSettings() {
  display.setRotation(1);
  display.setFullWindow();
  if (!needsRedraw()) return;

  uint16_t bg, fg;
  getTheme(bg, fg);

  display.firstPage();
  do {
    display.fillScreen(bg);
    drawScreenHeader("SETTINGS", "");
    display.setFont(&FreeMono9pt7b);
    display.setTextColor(fg);

    // Row 0: Dark mode
    int y0 = 35;
    if (settingsSelectedIndex == 0) {
      display.fillRect(0, y0 - 12, SCREEN_W, ROW_H, fg);
      display.setTextColor(bg);
    }
    display.setCursor(4, y0);
    display.print("Dark:");
    display.setCursor(SCREEN_W - 30, y0);
    display.print(darkMode ? "ON " : "OFF");
    display.setTextColor(fg);

    // Row 1: Arabic mode
    int y1 = 35 + ROW_H;
    if (settingsSelectedIndex == 1) {
      display.fillRect(0, y1 - 12, SCREEN_W, ROW_H, fg);
      display.setTextColor(bg);
    }
    display.setCursor(4, y1);
    display.print("Arabic:");
    display.setCursor(SCREEN_W - 30, y1);
    display.print(arabicMode ? "ON " : "OFF");
    display.setTextColor(fg);

    // Row 2: Apply / back
    int y2 = 35 + ROW_H * 2;
    if (settingsSelectedIndex == 2) {
      display.fillRect(0, y2 - 12, SCREEN_W, ROW_H, fg);
      display.setTextColor(bg);
    }
    display.setCursor(4, y2);
    display.print("Apply");
    display.setTextColor(fg);

    drawScreenFooter("UP/DN:select  RIGHT:change  LEFT:back");
  }
  while (display.nextPage());
  updateDrawState();
}

// ============================================================
//  E-paper: bookmarks screen
// ============================================================
void drawBookmarksScreen() {
  display.setRotation(1);
  display.setFullWindow();

  static int scrollOffset = 0;
  updateScrollOffset(selectedBookmark, scrollOffset);

  if (!needsRedraw()) return;

  uint16_t bg, fg;
  getTheme(bg, fg);

  String cnt = String(bookmarkCount) + " bookmark" + (bookmarkCount != 1 ? "s" : "");

  display.firstPage();
  do {
    display.fillScreen(bg);
    drawScreenHeader("BOOKMARKS", cnt);

    if (bookmarkCount == 0) {
      display.setTextColor(fg);
      display.setCursor(6, LIST_Y_START + ROW_H);
      display.print("No bookmarks yet.");
      display.setCursor(6, LIST_Y_START + ROW_H * 2);
      display.print("Hold RIGHT in a book");
      display.setCursor(6, LIST_Y_START + ROW_H * 3);
      display.print("to save a bookmark.");
    } else {
      for (int i = 0; i < VISIBLE_ROWS; i++) {
        int  idx        = scrollOffset + i;
        if (idx >= bookmarkCount) break;
        int  rowY       = LIST_Y_START + i * ROW_H;
        bool isSelected = (idx == selectedBookmark);

        if (isSelected) {
          display.fillRect(0, rowY, SCREEN_W, ROW_H - 1, fg);
          display.setTextColor(bg);
        } else {
          display.setTextColor(fg);
        }

        String label = bookmarkFiles[idx];
        if (label.endsWith(".txt")) label = label.substring(0, label.length() - 4);
        if (label.length() > 20)   label = label.substring(0, 18) + "..";

        display.setFont(&FreeMono9pt7b);
        display.setCursor(4, rowY + ROW_H - 4);
        display.print(String(isSelected ? "> " : "  ") + label + " p" + bookmarkPages[idx]);
      }
    }

    drawScreenFooter("L:back  R:open  D:HOLD del");
  }
  while (display.nextPage());
  updateDrawState();
}

// ============================================================
//  E-paper: reading screen
// ============================================================
void drawReadingPage() {
  display.setRotation(1);
  display.setFullWindow();
  if (!needsRedraw()) return;

  uint16_t bg, fg;
  getTheme(bg, fg);

  String title = currentBook;
  if (title.endsWith(".txt")) title = title.substring(0, title.length() - 4);
  if (title.length() > 16) title = title.substring(0, 14) + "..";
  String pageStr = "(" + String(currentPage) + "/" + String(totalPages) + ")";
  if (arabicMode) pageStr = "(RTL) " + pageStr;

  display.firstPage();
  do {
    display.fillScreen(bg);
    drawScreenHeader(title.c_str(), pageStr);

    if (arabicMode) display.setFont(&Arabi12pt7b);
    else display.setFont(&FreeMono9pt7b);
    display.setTextColor(fg);

    int16_t tbx, tby;
    uint16_t tbw, tbh;

    File f = LittleFS.open("/" + currentBook, "r");
    if (!f) {
      display.setCursor(4, CONTENT_Y_START);
      display.print("Cannot open file.");
    } else {
      // Skip to the right page
      int charsToSkip = (currentPage - 1) * CHARS_PER_PAGE;
      for (int i = 0; i < charsToSkip && f.available(); i++) f.read();

      // Measure space width once
      int16_t sx, sy; uint16_t sw, sh, nsw, nsh;
      display.getTextBounds("A B", 0, 0, &sx, &sy, &sw, &sh);
      display.getTextBounds("AB",  0, 0, &sx, &sy, &nsw, &nsh);
      int spaceWidth = sw - nsw;

      // RTL or LTR starting X position
      int cursorX  = arabicMode ? (SCREEN_W - 4) : 4;
      const int maxX  = SCREEN_W - 4;
      const int minX  = 4;
      int cursorY = CONTENT_Y_START;
      String word     = "";
      int charCount = 0;

      while (f.available() && cursorY <= CONTENT_MAX_Y && charCount < CHARS_PER_PAGE) {
        char c = f.read();
        charCount++;
        if (c == '\r') continue;

        if (c == ' ' || c == '\n') {
          if (word.length() > 0) {
            display.getTextBounds(word, 0, 0, &tbx, &tby, &tbw, &tbh);
            int wordWidth = tbw + spaceWidth;
            if (arabicMode) {
              if (cursorX - wordWidth < minX) {
                cursorX = SCREEN_W - 4; cursorY += LINE_HEIGHT;
                if (cursorY > CONTENT_MAX_Y) break;
              }
            } else {
              if (cursorX + wordWidth > maxX) {
                cursorX = 4; cursorY += LINE_HEIGHT;
                if (cursorY > CONTENT_MAX_Y) break;
              }
            }
            display.setCursor(cursorX, cursorY);
            display.print(word);
            if (arabicMode) cursorX -= wordWidth;
            else cursorX += wordWidth;
            word = "";
          }
          if (c == '\n') {
            cursorX = arabicMode ? (SCREEN_W - 4) : 4;
            cursorY += LINE_HEIGHT;
            if (cursorY > CONTENT_MAX_Y) break;
          }
        } else {
          word += c;
          // Force-flush very long words
          if ((int)word.length() > CHARS_PER_LINE) {
            display.getTextBounds(word, 0, 0, &tbx, &tby, &tbw, &tbh);
            if (arabicMode) {
              if (cursorX - (int)tbw < minX) {
                cursorX = SCREEN_W - 4; cursorY += LINE_HEIGHT;
                if (cursorY > CONTENT_MAX_Y) break;
              }
            } else {
              if (cursorX + (int)tbw > maxX) {
                cursorX = 4; cursorY += LINE_HEIGHT;
                if (cursorY > CONTENT_MAX_Y) break;
              }
            }
            display.setCursor(cursorX, cursorY);
            display.print(word);
            if (arabicMode) cursorX -= tbw;
            else cursorX += tbw;
            word = "";
          }
        }
      }
      // Flush remaining word
      if (word.length() > 0 && cursorY <= CONTENT_MAX_Y) {
        display.setCursor(cursorX, cursorY);
        display.print(word);
      }
      f.close();
    }

    String footer = "L:back R:Pg Hold:Bk";
    if (hasBookmark(currentBook, currentPage)) footer = "* " + footer;
    drawScreenFooter(footer.c_str());
    display.setFont(&FreeMono9pt7b);
    display.setTextColor(fg);
    display.setCursor(SCREEN_W - 40, SCREEN_H - 8);
    display.print(String(batteryPercent) + "%");
  }
  while (display.nextPage());
  updateDrawState();
}

void openBook(const String& filename) {
  currentBook = filename;
  calculatePages(filename);
  currentPage = loadProgress(filename);
  if (currentPage > totalPages) currentPage = totalPages;
  if (currentPage < 1)          currentPage = 1;
  arabicMode = detectArabicBook(filename);
  drawReadingPage();
}

// ============================================================
//  Web server
// ============================================================
void setupServer() {

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/index.html"))
      req->send(LittleFS, "/index.html", "text/html");
    else
      req->send(200, "text/html",
        "<h2>E-Reader</h2><p>index.html missing from LittleFS.</p>");
  });

  server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/edit.html"))
      req->send(LittleFS, "/edit.html", "text/html");
    else
      req->send(200, "text/html",
        "<h2>E-Reader</h2><p>edit.html missing.</p><a href='/'>Back</a>");
  });

  server.on("/library", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/library.html"))
      req->send(LittleFS, "/library.html", "text/html");
    else
      req->send(200, "text/html",
        "<h2>E-Reader</h2><p>library.html missing.</p><a href='/'>Back</a>");
  });

  server.on("/bookmarks.html", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/bookmarks.html", "text/html");
  });

  server.on("/bookmark.html", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/bookmark.html", "text/html");
  });

  server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/settings.html", "text/html");
  });

  // GET settings JSON
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "{\"darkMode\":";
    json += darkMode ? "true" : "false";
    json += "}";
    req->send(200, "application/json", json);
  });

  // POST settings JSON
  server.on("/api/settings", HTTP_POST,
    [](AsyncWebServerRequest *req) { req->send(200, "text/plain", "OK"); },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String body = "";
      for (size_t i = 0; i < len; i++) body += (char)data[i];

      int darkSep = body.indexOf("\"darkMode\":");

      if (darkSep >= 0) {
        int vs = body.indexOf(":", darkSep) + 1;
        while (vs < (int)body.length() && (body[vs] == ' ' || body[vs] == ',')) vs++;
        String val = "";
        for (int i = vs; i < (int)body.length(); i++) {
          char c = body[i];
          if (c == 't'||c=='r'||c=='u'||c=='e'||c=='f'||c=='a'||c=='l'||c=='s') val += c;
          else break;
        }
        val.trim();
        darkMode = (val == "true");
      }
      saveSettings();
    }
  );

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("name")) { req->send(400, "text/plain", "Missing name"); return; }
    String name = req->getParam("name")->value();
    if (isSystemFile(name))         { req->send(403, "text/plain", "Forbidden");    return; }
    if (!LittleFS.exists("/" + name)) { req->send(404, "text/plain", "Not found");  return; }
    req->send(LittleFS, "/" + name, "text/plain");
  });

  server.on("/api/progress", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "[";
    File f = LittleFS.open("/progress.txt", "r");
    bool first = true;
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int sep = line.indexOf('|');
        if (sep > 0) {
          if (!first) json += ",";
          json += "{\"name\":\"" + line.substring(0, sep) +
                  "\",\"page\":" + line.substring(sep + 1) + "}";
          first = false;
        }
      }
      f.close();
    }
    json += "]";
    req->send(200, "application/json", json);
  });

  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "[";
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    bool first = true;
    while (file) {
      String name = String(file.name());
      if (!isSystemFile(name)) {
        if (!first) json += ",";
        json += "{\"name\":\"" + name + "\",\"size\":" + file.size() + "}";
        first = false;
      }
      file = root.openNextFile();
    }
    json += "]";
    req->send(200, "application/json", json);
  });

  server.on("/api/rename", HTTP_POST,
    [](AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":true}"); },
    NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      String body = "";
      for (size_t i = 0; i < len; i++) body += (char)data[i];
      int fs = body.indexOf("\"from\":\"") + 8, fe = body.indexOf("\"", fs);
      int ts = body.indexOf("\"to\":\"")   + 6, te = body.indexOf("\"", ts);
      String from = body.substring(fs, fe), to = body.substring(ts, te);
      if (from.length() > 0 && to.length() > 0) {
        LittleFS.rename("/" + from, "/" + to);
        loadBookList();
        drawBookList();
      }
    }
  );

  server.on("/upload", HTTP_POST,
    [](AsyncWebServerRequest *req) { req->send(200, "text/plain", "Upload complete!"); },
    [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      static File uploadFile;
      if (!index) {
        Serial.println("Uploading: " + filename);
        uploadFile = LittleFS.open("/" + filename, "w");
      }
      if (uploadFile) uploadFile.write(data, len);
      if (final) {
        uploadFile.close();
        Serial.println("Upload done: " + String(index + len) + " bytes");
        String bookName = filename;
        int dot = filename.lastIndexOf('.');
        if (dot > 0) bookName = filename.substring(0, dot);
        bookName.replace("_", " ");
        File f = LittleFS.open("/bookname.txt", "w");
        if (f) { f.print(bookName); f.close(); }
        loadBookList();
        drawBookList();
      }
    }
  );

  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("name")) { req->send(400, "text/plain", "Missing name"); return; }
    String name = req->getParam("name")->value();
    if (isSystemFile(name)) { req->send(403, "text/plain", "Forbidden"); return; }
    LittleFS.remove("/" + name);
    loadBookList();
    drawBookList();
    req->send(200, "text/plain", "Deleted");
  });

  server.on("/api/buttons", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "{\"up\":"    + String(lastBtnUpState    == LOW ? "true" : "false") +
                  ",\"down\":"  + String(lastBtnDownState  == LOW ? "true" : "false") +
                  ",\"left\":"  + String(lastBtnLeftState  == LOW ? "true" : "false") +
                  ",\"right\":" + String(lastBtnRightState == LOW ? "true" : "false") + "}";
    req->send(200, "application/json", json);
  });

  server.on("/api/bookmarks", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "{\"bookmarks\":[";
    File f = LittleFS.open("/bookmarks.txt", "r");
    int idx = 0; bool first = true;
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int s1 = line.indexOf('|'), s2 = line.indexOf('|', s1 + 1);
        if (s1 > 0 && s2 > s1) {
          if (!first) json += ",";
          json += "{\"index\":"  + String(idx) +
                  ",\"file\":\"" + line.substring(0, s1) + "\"" +
                  ",\"page\":"   + line.substring(s1 + 1, s2) +
                  ",\"time\":"   + line.substring(s2 + 1) + "}";
          first = false; idx++;
        }
      }
      f.close();
    }
    json += "]}";
    req->send(200, "application/json", json);
  });

  server.on("/api/bookmarks", HTTP_DELETE, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("index")) {
      req->send(400, "application/json", "{\"error\":\"missing index\"}");
      return;
    }
    int delIdx = req->getParam("index")->value().toInt();
    File f = LittleFS.open("/bookmarks.txt", "r");
    String content = "";
    int cur = 0;
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (cur != delIdx) content += line + "\n";
        cur++;
      }
      f.close();
    }
    f = LittleFS.open("/bookmarks.txt", "w");
    if (f) { f.print(content); f.close(); }
    loadBookmarks();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/bookmark/content", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("file") || !req->hasParam("page")) {
      req->send(400, "application/json", "{\"error\":\"missing file or page\"}");
      return;
    }
    String filename = "/" + req->getParam("file")->value();
    int    pageNum  = req->getParam("page")->value().toInt();
    File   bookFile = LittleFS.open(filename, "r");
    if (!bookFile) {
      req->send(404, "application/json", "{\"error\":\"book not found\"}");
      return;
    }
    int    startPos    = (pageNum - 1) * CHARS_PER_PAGE;
    String pageContent = "";
    int    pos         = 0;
    while (bookFile.available() && pos < startPos + CHARS_PER_PAGE) {
      char c = bookFile.read(); pos++;
      if (pos <= startPos) continue;
      if (c == '\n' || c == '\r') pageContent += ' ';
      else                        pageContent += c;
    }
    bookFile.close();
    pageContent.trim();
    req->send(200, "application/json",
      "{\"content\":\"" + pageContent + "\",\"page\":" + String(pageNum) + "}");
  });

  server.begin();
  Serial.println("Server started at http://192.168.4.1");
}

// ============================================================
//  Common init shared by both setup paths
// ============================================================
static void commonInit() {
  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  LittleFS.begin(true);
  display.init(115200, true, 50, false);
  initBattery();
  batteryPercent    = readBattery();
  lastBatteryUpdate = millis();

  loadSettings();
  loadBookList();
}

// ============================================================
//  setup()
// ============================================================
void setup() {
  Serial.begin(115200);

  // Start WiFi first (before slow display init)
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  setupServer();

  commonInit();

  drawBookList();
}

// ============================================================
//  loop()
// ============================================================
void loop() {
  unsigned long now = millis();

  // Battery update every 60 s
  if (now - lastBatteryUpdate > 60000) {
    batteryPercent    = readBattery();
    lastBatteryUpdate = now;
  }

  bool shortUp = false, shortDown = false, shortLeft = false, shortRight = false;
  bool longUp = false, longDown = false, longLeft = false, longRight = false;

  pollButton(BTN_UP,    lastBtnUpState,    lastBtnUpTime,    pressStartUp,    now, shortUp,    longUp);
  pollButton(BTN_DOWN,  lastBtnDownState,  lastBtnDownTime,  pressStartDown,  now, shortDown,  longDown);
  pollButton(BTN_LEFT,  lastBtnLeftState,  lastBtnLeftTime,  pressStartLeft,  now, shortLeft,  longLeft);
  pollButton(BTN_RIGHT, lastBtnRightState, lastBtnRightTime, pressStartRight, now, shortRight, longRight);

  if (shortUp)    Serial.println("BTN_UP");
  if (shortDown)  Serial.println("BTN_DOWN");
  if (shortLeft)  Serial.println("BTN_LEFT");
  if (shortRight) Serial.println("BTN_RIGHT");

  // ---- State machine ----
  if (appState == STATE_LIST) {
    if (shortUp) {
      if (selectedIndex == 0) {
        appState = STATE_BOOKMARKS;
        loadBookmarks();
        drawBookmarksScreen();
      } else if (bookCount > 0) {
        selectedIndex--;
        drawBookList();
      }
    }
    if (shortDown && bookCount > 0) {
      selectedIndex = (selectedIndex + 1) % bookCount;
      drawBookList();
    }
    if (shortRight && bookCount > 0) {
      appState = STATE_READING;
      openBook(bookNames[selectedIndex]);
    }
    if (shortLeft) {
      appState = STATE_SETTINGS;
      settingsSelectedIndex = 0;
      drawSettings();
    }

  } else if (appState == STATE_READING) {
    if (longRight) {
      saveBookmark(currentBook, currentPage);
      drawReadingPage();
    }
    if (shortLeft) {
      saveProgress(currentBook, currentPage);
      appState = STATE_LIST;
      loadBookmarks();
      drawBookList();
    }
    if (shortUp && currentPage > 1) {
      currentPage--;
      drawReadingPage();
    }
    if ((shortDown || shortRight) && currentPage < totalPages) {
      currentPage++;
      drawReadingPage();
    }

  } else if (appState == STATE_SETTINGS) {
    if (shortUp) {
      settingsSelectedIndex = (settingsSelectedIndex - 1 + 3) % 3;
      drawSettings();
    }
    if (shortDown) {
      settingsSelectedIndex = (settingsSelectedIndex + 1) % 3;
      drawSettings();
    }
    if (shortRight) {
      if (settingsSelectedIndex == 0) {
        darkMode = !darkMode;
        saveSettings();
        drawSettings();
      } else if (settingsSelectedIndex == 1) {
        arabicMode = !arabicMode;
        saveSettings();
        drawSettings();
      } else {
        appState = STATE_LIST;
        drawBookList();
      }
    }
    if (shortLeft) {
      appState = STATE_LIST;
      drawBookList();
    }

  } else if (appState == STATE_BOOKMARKS) {
    if (longDown && bookmarkCount > 0) {
      deleteBookmark(selectedBookmark);
      drawBookmarksScreen();
    }
    if (shortUp && bookmarkCount > 0) {
      selectedBookmark = (selectedBookmark - 1 + bookmarkCount) % bookmarkCount;
      drawBookmarksScreen();
    }
    if (shortDown && bookmarkCount > 0) {
      selectedBookmark = (selectedBookmark + 1) % bookmarkCount;
      drawBookmarksScreen();
    }
    if (shortRight && bookmarkCount > 0) {
      currentBook = bookmarkFiles[selectedBookmark];
      calculatePages(currentBook);
      currentPage = constrain(bookmarkPages[selectedBookmark], 1, totalPages);
      appState = STATE_READING;
      drawReadingPage();
    }
    if (shortLeft) {
      appState = STATE_LIST;
      drawBookList();
    }
  }
}
