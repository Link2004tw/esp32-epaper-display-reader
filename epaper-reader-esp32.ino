// ============================================================
//  ESP32-C3 Super Mini — DIY E-Reader
//  WiFi/LittleFS server  +  GxEPD2 e-paper book list
//
//  Display : 2.13" DEPG0213BN  (122 x 250, SSD1680, B/W)
//  Pins    : CS=5 DC=17 RST=16 BUSY=4  (SPI: SCK=18 MOSI=23)
//  Buttons : UP=0  DOWN=1  LEFT=9  RIGHT=20  (INPUT_PULLUP)
//
//  Buttons on book list screen:
//    UP    → scroll selection up
//    DOWN  → scroll selection down
//    RIGHT → open selected book
//    LEFT  → (while reading) return to book list
//
//  Web UI files live in LittleFS — upload via Arduino IDE
//  LittleFS Data Upload tool from the /data folder:
//    data/index.html   → http://192.168.4.1/
//    data/edit.html    → http://192.168.4.1/edit
// ============================================================

// ---------- GxEPD2 ----------
#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#define CS_PIN   5
#define DC_PIN   17
#define RES_PIN  16
#define BUSY_PIN 4

// Display layout constants (rotation=1 → landscape: 250 × 122 px)
#define VISIBLE_ROWS 5
#define ROW_H        17
#define LIST_Y_START 22
#define SCREEN_W     250
#define SCREEN_H     122

GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(
  GxEPD2_213_BN(CS_PIN, DC_PIN, RES_PIN, BUSY_PIN));

// ---------- WiFi / Server ----------
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/adc.h>

AsyncWebServer server(80);

const char* AP_SSID = "E-Reader";
const char* AP_PASS = "12345678";

// ---------- Buttons ----------
const uint8_t BTN_UP    = 12;
const uint8_t BTN_DOWN  = 13;
const uint8_t BTN_LEFT  = 14;
const uint8_t BTN_RIGHT = 27;

#define BATT_PIN 32

#define BTN_DEBOUNCE_MS 50

int lastBtnUpState    = HIGH, lastBtnDownState  = HIGH;
int lastBtnLeftState  = HIGH, lastBtnRightState = HIGH;
unsigned long lastBtnUpTime   = 0, lastBtnDownTime  = 0;
unsigned long lastBtnLeftTime = 0, lastBtnRightTime = 0;

// ---------- Book list state ----------
#define MAX_BOOKS 20
String bookNames[MAX_BOOKS];
int    bookCount     = 0;
int    selectedIndex = 0;

// ---------- Settings ----------
bool darkMode = false;
int sleepTimeoutMs = 300000;

// ---------- Battery ----------
int batteryPercent = 100;
unsigned long lastBatteryUpdate = 0;

// ---------- Reading state ----------
String currentBook = "";
int currentPage = 1;
int totalPages = 1;

// ---------- Bookmark state ----------
#define MAX_BOOKMARKS 20
String bookmarkFiles[MAX_BOOKMARKS];
int bookmarkPages[MAX_BOOKMARKS];
unsigned long bookmarkTimes[MAX_BOOKMARKS];
int bookmarkCount = 0;
int selectedBookmark = 0;
const int CHARS_PER_LINE = 26;
const int LINES_PER_PAGE = 5;
const int CHARS_PER_PAGE = CHARS_PER_LINE * LINES_PER_PAGE;
const int LINE_HEIGHT = 14;
const int CONTENT_Y_START = 32;
const int CONTENT_Y_END = SCREEN_H - 12;
const int CONTENT_MAX_Y = SCREEN_H - 12;

void loadSettings() {
  File f = LittleFS.open("/settings.txt", "r");
  if (f) {
    String line = f.readStringUntil('\n');
    line.trim();
    int sep = line.indexOf('|');
    if (sep > 0) {
      darkMode = (line.substring(0, sep) == "dark");
      sleepTimeoutMs = line.substring(sep + 1).toInt();
      if (sleepTimeoutMs == 0) sleepTimeoutMs = 300000;
    } else {
      darkMode = (line == "dark");
    }
    f.close();
  }
}

void saveSettings() {
  File f = LittleFS.open("/settings.txt", "w");
  if (f) {
    f.print(darkMode ? "dark" : "light");
    f.print("|");
    f.println(sleepTimeoutMs);
    f.close();
  }
}

// ---------- Battery ----------
void initBattery() {
  analogReadResolution(12);
}

int readBattery() {
  int raw = analogRead(BATT_PIN);
  float batteryV = raw * 6.6 / 4095.0;
  int percent = map((int)(batteryV * 100), 270, 420, 0, 100);
  return constrain(percent, 0, 100);
}

// ---------- Reading progress ----------
int loadProgress(const String& filename) {
  File f = LittleFS.open("/progress.txt", "r");
  if (!f) return 1;
  
  while (f.available()) {
    String line = f.readStringUntil('\n');
    int sep = line.indexOf('|');
    if (sep > 0) {
      String fname = line.substring(0, sep);
      if (fname == filename) {
        int page = line.substring(sep + 1).toInt();
        f.close();
        return page > 0 ? page : 1;
      }
    }
  }
  f.close();
  return 1;
}

void saveProgress(const String& filename, int page) {
  File f = LittleFS.open("/progress.txt", "r");
  String content = "";
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      int sep = line.indexOf('|');
      if (sep > 0 && line.substring(0, sep) != filename) {
        content += line + "\n";
      }
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

bool hasBookmark(const String& filename, int page) {
  File f = LittleFS.open("/bookmarks.txt", "r");
  if (!f) return false;
  String check = filename + "|" + String(page) + "|";
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.startsWith(check)) {
      f.close();
      return true;
    }
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
  
  if (selectedBookmark >= bookmarkCount) selectedBookmark = max(0, bookmarkCount - 1);
}

void deleteBookmark(int index) {
  if (index < 0 || index >= bookmarkCount) return;
  
  File f = LittleFS.open("/bookmarks.txt", "r");
  String content = "";
  if (f) {
    int current = 0;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      if (current != index) {
        content += line + "\n";
      }
      current++;
    }
    f.close();
  }
  
  f = LittleFS.open("/bookmarks.txt", "w");
  if (f) {
    f.print(content);
    f.close();
  }
  
  for (int i = index; i < bookmarkCount - 1; i++) {
    bookmarkFiles[i] = bookmarkFiles[i + 1];
    bookmarkPages[i] = bookmarkPages[i + 1];
    bookmarkTimes[i] = bookmarkTimes[i + 1];
  }
  bookmarkCount--;
  if (selectedBookmark >= bookmarkCount) selectedBookmark = max(0, bookmarkCount - 1);
}


// ============================================================
//  System files to hide from the book list and /list API
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
  File f = root.openNextFile();
  while (f && bookCount < MAX_BOOKS) {
    String name = String(f.name());
    if (!isSystemFile(name)) {
      bookNames[bookCount++] = name;
    }
    f = root.openNextFile();
  }
  if (selectedIndex >= bookCount) selectedIndex = max(0, bookCount - 1);
}

// ---------- Pagination helpers ----------
int getFileCharCount(const String& filename) {
  File f = LittleFS.open("/" + filename, "r");
  if (!f) return 0;
  int count = f.size();
  f.close();
  return count;
}

void calculatePages(const String& filename) {
  int charCount = getFileCharCount(filename);
  totalPages = max(1, (charCount / CHARS_PER_PAGE) + 1);
}

String getPageContent(File& f, int pageNum, int& charsRead) {
  int startChar = (pageNum - 1) * CHARS_PER_PAGE;
  int endChar = pageNum * CHARS_PER_PAGE;
  
  String content = "";
  charsRead = 0;
  int pos = 0;
  char c;
  
  while (f.available() && pos < endChar) {
    c = f.read();
    pos++;
    
    if (pos <= startChar) continue;
    
    if (c == '\r') continue;
    
    content += c;
    charsRead++;
    
    if (pos >= endChar) break;
  }
  
  return content;
}

// ============================================================
//  E-paper: book list screen
// ============================================================
void drawBookList() {
  display.setRotation(1);
  display.setFullWindow();

  // Keep selected row visible
  static int scrollOffset = 0;
  if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
  if (selectedIndex >= scrollOffset + VISIBLE_ROWS)
    scrollOffset = selectedIndex - VISIBLE_ROWS + 1;

  if (!needsRedraw()) return;

  loadBookmarks();
  
  uint16_t bgColor = darkMode ? GxEPD_BLACK : GxEPD_WHITE;
  uint16_t fgColor = darkMode ? GxEPD_WHITE : GxEPD_BLACK;
  uint16_t accentColor = darkMode ? GxEPD_WHITE : GxEPD_BLACK;

  display.firstPage();
  do {
    display.fillScreen(bgColor);

    display.fillRect(0, 0, SCREEN_W, 18, fgColor);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(bgColor);
    display.setCursor(4, 13);
    display.print("E-READER");
    
    // Bookmark icon in header
    if (bookmarkCount > 0) {
      display.setFont(&FreeMono9pt7b);
      display.setCursor(95, 13);
      display.print("[");
      display.print(String(bookmarkCount));
      display.print("]");
    }
    
    display.setFont(&FreeMono9pt7b);
    String cnt = String(bookCount) + " book" + (bookCount != 1 ? "s" : "");
    int16_t tbx, tby; uint16_t tbw, tbh;
    display.getTextBounds(cnt, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(SCREEN_W - tbw - 4, 13);
    display.print(cnt);

    if (bookCount == 0) {
      display.setFont(&FreeMono9pt7b);
      display.setTextColor(fgColor);
      display.setCursor(6, LIST_Y_START + ROW_H);
      display.print("No books found.");
      display.setCursor(6, LIST_Y_START + ROW_H * 2);
      display.print("Upload via WiFi:");
      display.setCursor(6, LIST_Y_START + ROW_H * 3);
      display.print("192.168.4.1");
    } else {
      for (int i = 0; i < VISIBLE_ROWS; i++) {
        int bookIdx = scrollOffset + i;
        if (bookIdx >= bookCount) break;

        int rowY = LIST_Y_START + i * ROW_H;
        bool isSelected = (bookIdx == selectedIndex);

        if (isSelected) {
          display.fillRect(0, rowY, SCREEN_W, ROW_H - 1, fgColor);
          display.setTextColor(bgColor);
        } else {
          display.setTextColor(fgColor);
        }

        display.setFont(&FreeMono9pt7b);
        String label = bookNames[bookIdx];
        if (label.endsWith(".txt")) label = label.substring(0, label.length() - 4);
        if (label.length() > 30) label = label.substring(0, 28) + "..";

        display.setCursor(isSelected ? 6 : 4, rowY + ROW_H - 4);
        display.print((isSelected ? "> " : "  ") + label);
      }
    }

    display.drawLine(0, SCREEN_H - 10, SCREEN_W, SCREEN_H - 10, fgColor);
    display.setFont(0);
    display.setTextColor(fgColor);
    display.setCursor(2, SCREEN_H - 8);
    display.print("UP:bookmarks L:settings R:open");
  }
  while (display.nextPage());
  updateDrawState();
}

// ============================================================
//  E-paper: settings screen
// ============================================================
int settingsSelectedIndex = 0;

void drawSettings() {
  display.setRotation(1);
  display.setFullWindow();
  if (!needsRedraw()) return;
  
  uint16_t bgColor = darkMode ? GxEPD_BLACK : GxEPD_WHITE;
  uint16_t fgColor = darkMode ? GxEPD_WHITE : GxEPD_BLACK;
  display.firstPage();
  do {
    display.fillScreen(bgColor);

    display.fillRect(0, 0, SCREEN_W, 18, fgColor);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(bgColor);
    display.setCursor(4, 13);
    display.print("SETTINGS");

    display.setFont(&FreeMono9pt7b);
    display.setTextColor(fgColor);

    int y0 = 35;
    display.setCursor(4, y0);
    if (settingsSelectedIndex == 0) {
      display.fillRect(0, y0 - 12, SCREEN_W, ROW_H, fgColor);
      display.setTextColor(bgColor);
    }
    display.print("Dark:");
    display.setCursor(SCREEN_W - 30, y0);
    display.print(darkMode ? "ON " : "OFF");
    display.setTextColor(fgColor);

    int y1 = 35 + ROW_H;
    int timeoutMinutes = sleepTimeoutMs / 60000;
    display.setCursor(4, y1);
    if (settingsSelectedIndex == 1) {
      display.fillRect(0, y1 - 12, SCREEN_W, ROW_H, fgColor);
      display.setTextColor(bgColor);
    }
    display.print("Sleep:");
    display.setCursor(SCREEN_W - 30, y1);
    display.print(timeoutMinutes);
    display.print("m");
    display.setTextColor(fgColor);

    int y2 = 35 + ROW_H * 2;
    display.setCursor(4, y2);
    if (settingsSelectedIndex == 2) {
      display.fillRect(0, y2 - 12, SCREEN_W, ROW_H, fgColor);
      display.setTextColor(bgColor);
    }
    display.print("Apply");
    display.setTextColor(fgColor);

    display.drawLine(0, SCREEN_H - 10, SCREEN_W, SCREEN_H - 10, fgColor);
    display.setFont(0);
    display.setCursor(2, SCREEN_H - 8);
    display.print("UP/DN:select  RIGHT:change  LEFT:back");
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
  if (selectedBookmark < scrollOffset) scrollOffset = selectedBookmark;
  if (selectedBookmark >= scrollOffset + VISIBLE_ROWS)
    scrollOffset = selectedBookmark - VISIBLE_ROWS + 1;
  
  if (!needsRedraw()) return;
  
  loadBookmarks();
  
  uint16_t bgColor = darkMode ? GxEPD_BLACK : GxEPD_WHITE;
  uint16_t fgColor = darkMode ? GxEPD_WHITE : GxEPD_BLACK;
  
  display.firstPage();
  do {
    display.fillScreen(bgColor);
    
    display.fillRect(0, 0, SCREEN_W, 18, fgColor);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(bgColor);
    display.setCursor(4, 13);
    display.print("BOOKMARKS");
    display.setFont(&FreeMono9pt7b);
    String cnt = String(bookmarkCount) + " bookmark" + (bookmarkCount != 1 ? "s" : "");
    int16_t tbx, tby; uint16_t tbw, tbh;
    display.getTextBounds(cnt, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(SCREEN_W - tbw - 4, 13);
    display.print(cnt);
    
    if (bookmarkCount == 0) {
      display.setFont(&FreeMono9pt7b);
      display.setTextColor(fgColor);
      display.setCursor(6, LIST_Y_START + ROW_H);
      display.print("No bookmarks yet.");
      display.setCursor(6, LIST_Y_START + ROW_H * 2);
      display.print("Hold RIGHT in a book");
      display.setCursor(6, LIST_Y_START + ROW_H * 3);
      display.print("to save a bookmark.");
    } else {
      for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = scrollOffset + i;
        if (idx >= bookmarkCount) break;
        
        int rowY = LIST_Y_START + i * ROW_H;
        bool isSelected = (idx == selectedBookmark);
        
        if (isSelected) {
          display.fillRect(0, rowY, SCREEN_W, ROW_H - 1, fgColor);
          display.setTextColor(bgColor);
        } else {
          display.setTextColor(fgColor);
        }
        
        display.setFont(&FreeMono9pt7b);
        String label = bookmarkFiles[idx];
        if (label.endsWith(".txt")) label = label.substring(0, label.length() - 4);
        if (label.length() > 20) label = label.substring(0, 18) + "..";
        
        String line = String(isSelected ? "> " : "  ") + label + " p" + bookmarkPages[idx];
        display.setCursor(4, rowY + ROW_H - 4);
        display.print(line);
      }
    }
    
    display.drawLine(0, SCREEN_H - 10, SCREEN_W, SCREEN_H - 10, fgColor);
    display.setFont(0);
    display.setTextColor(fgColor);
    display.setCursor(2, SCREEN_H - 8);
    display.print("L:back  R:open  D:HOLD del");
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
  
  uint16_t bgColor = darkMode ? GxEPD_BLACK : GxEPD_WHITE;
  uint16_t fgColor = darkMode ? GxEPD_WHITE : GxEPD_BLACK;
  
  display.firstPage();
  do {
    display.fillScreen(bgColor);

    display.fillRect(0, 0, SCREEN_W, 18, fgColor);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(bgColor);
    display.setCursor(4, 13);
    String title = currentBook;
    if (title.endsWith(".txt")) title = title.substring(0, title.length() - 4);
    if (title.length() > 16) title = title.substring(0, 14) + "..";
    display.print(title);

    int16_t tbx, tby; uint16_t tbw, tbh;
    String pageStr = "(" + String(currentPage) + "/" + String(totalPages) + ")";
    display.getTextBounds(pageStr, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(SCREEN_W - tbw - 4, 13);
    display.print(pageStr);

    File f = LittleFS.open("/" + currentBook, "r");
    display.setFont(&FreeMono9pt7b);
    display.setTextColor(fgColor);

    if (!f) {
      display.setCursor(4, CONTENT_Y_START);
      display.print("Cannot open file.");
    } else {
      int charsToSkip = (currentPage - 1) * CHARS_PER_PAGE;
      int pos = 0;
      while (pos < charsToSkip && f.available()) {
        f.read();
        pos++;
      }

      int cursorX = 4, cursorY = CONTENT_Y_START;
      const int maxX = SCREEN_W - 4;
      String word = "";
      int charCount = 0;

      int16_t tbx, tby; uint16_t tbw, tbh;
      display.getTextBounds("A B", 0, 0, &tbx, &tby, &tbw, &tbh);
      uint16_t noSpacew, noSpaceh;
      display.getTextBounds("AB", 0, 0, &tbx, &tby, &noSpacew, &noSpaceh);
      int spaceWidth = tbw - noSpacew; // isolates exactly one space's advance width

      while (f.available() && cursorY <= CONTENT_MAX_Y && charCount < CHARS_PER_PAGE) {
        char c = f.read();
        charCount++;
        if (c == '\r') continue;

        if (c == ' ' || c == '\n') {
          if (word.length() > 0) {
            display.getTextBounds(word, 0, 0, &tbx, &tby, &tbw, &tbh);
            if (cursorX + (int)tbw + spaceWidth > maxX) {
              cursorX = 4; cursorY += LINE_HEIGHT;
              if (cursorY > CONTENT_MAX_Y) break;
            }
            display.setCursor(cursorX, cursorY);
            display.print(word);
            cursorX += tbw + spaceWidth;
            word = "";
          }
          if (c == '\n') {
            cursorX = 4; cursorY += LINE_HEIGHT;
            if (cursorY > CONTENT_MAX_Y) break;
          }
        } else {
          word += c;
          if (word.length() > CHARS_PER_LINE) {
            display.getTextBounds(word, 0, 0, &tbx, &tby, &tbw, &tbh);
            if (cursorX + (int)tbw > maxX) {
              cursorX = 4; cursorY += LINE_HEIGHT;
              if (cursorY > CONTENT_MAX_Y) break;
            }
            display.setCursor(cursorX, cursorY);
            display.print(word);
            cursorX += tbw;
            word = "";
          }
        }
      }
      if (word.length() > 0 && cursorY <= CONTENT_MAX_Y) {
        display.setCursor(cursorX, cursorY);
        display.print(word);
      }
      f.close();
    }

    display.drawLine(0, SCREEN_H - 10, SCREEN_W, SCREEN_H - 10, fgColor);
    display.setFont(&FreeMono9pt7b);
    display.setTextColor(fgColor);
    display.setCursor(2, SCREEN_H - 8);
    String footer = "L:back R:Pg Hold:Bk";
    if (hasBookmark(currentBook, currentPage)) footer = "* " + footer;
    display.print(footer);

    display.setCursor(SCREEN_W - 32, SCREEN_H - 8);
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
  if (currentPage < 1) currentPage = 1;
  drawReadingPage();
}

// ============================================================
//  Web server — HTML served from LittleFS
// ============================================================
void setupServer() {

  // Main upload page — served from LittleFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/index.html")) {
      req->send(LittleFS, "/index.html", "text/html");
    } else {
      req->send(200, "text/html",
        "<h2>E-Reader</h2><p>index.html missing from LittleFS.<br>"
        "Upload it via the Arduino LittleFS Data Upload tool.</p>");
    }
  });

  // Rename editor — served from LittleFS
  server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/edit.html")) {
      req->send(LittleFS, "/edit.html", "text/html");
    } else {
      req->send(200, "text/html",
        "<h2>E-Reader</h2><p>edit.html missing from LittleFS.</p>"
        "<a href='/'>Back</a>");
    }
  });

  // Library page — served from LittleFS
  server.on("/library", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/library.html")) {
      req->send(LittleFS, "/library.html", "text/html");
    } else {
      req->send(200, "text/html",
        "<h2>E-Reader</h2><p>library.html missing from LittleFS.</p>"
        "<a href='/'>Back</a>");
    }
  });

  // Bookmarks list page — served from LittleFS
  server.on("/bookmarks.html", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/bookmarks.html", "text/html");
  });

  // Bookmark detail page — served from LittleFS
  server.on("/bookmark.html", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/bookmark.html", "text/html");
  });

  // Settings page — served from LittleFS
  server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/settings.html", "text/html");
  });

  // Get current settings
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "{\"darkMode\":";
    json += darkMode ? "true" : "false";
    json += ",\"sleepTimeoutMs\":" + String(sleepTimeoutMs) + "}";
    req->send(200, "application/json", json);
  });

  // Update settings
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", "OK");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    String body = "";
    for (size_t i = 0; i < len; i++) {
      body += (char)data[i];
    }
    int darkSep = body.indexOf("\"darkMode\":");
    int sleepSep = body.indexOf("\"sleepTimeoutMs\":");
    if (darkSep >= 0) {
      int valStart = body.indexOf(":", darkSep) + 1;
      while (valStart < body.length() && (body.charAt(valStart) == ' ' || body.charAt(valStart) == ',')) valStart++;
      String val = "";
      for (size_t i = valStart; i < body.length() && (body.charAt(i) == 't' || body.charAt(i) == 'r' || body.charAt(i) == 'u' || body.charAt(i) == 'e' || body.charAt(i) == 'f' || body.charAt(i) == 'a' || body.charAt(i) == 'l' || body.charAt(i) == 's' || body.charAt(i) == ' ' || body.charAt(i) == ','); i++) {
        val += body.charAt(i);
      }
      val.trim();
      darkMode = (val == "true");
    }
    if (sleepSep >= 0) {
      int valStart = body.indexOf(":", sleepSep) + 1;
      while (valStart < body.length() && (body.charAt(valStart) == ' ' || body.charAt(valStart) == ',')) valStart++;
      String val = "";
      for (size_t i = valStart; i < body.length() && (body.charAt(i) >= '0' && body.charAt(i) <= '9'); i++) {
        val += body.charAt(i);
      }
      sleepTimeoutMs = val.toInt();
      if (sleepTimeoutMs < 60000) sleepTimeoutMs = 60000;
      if (sleepTimeoutMs > 600000) sleepTimeoutMs = 600000;
    }
    saveSettings();
  });

  // Download a book file
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("name")) {
      req->send(400, "text/plain", "Missing name param");
      return;
    }
    String name = req->getParam("name")->value();
    if (isSystemFile(name)) {
      req->send(403, "text/plain", "Cannot download system file");
      return;
    }
    if (!LittleFS.exists("/" + name)) {
      req->send(404, "text/plain", "File not found");
      return;
    }
    req->send(LittleFS, "/" + name, "text/plain");
  });

  // Get reading progress for all books
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
          String fname = line.substring(0, sep);
          int page = line.substring(sep + 1).toInt();
          json += "{\"name\":\"" + fname + "\",\"page\":" + page + "}";
          first = false;
        }
      }
      f.close();
    }
    json += "]";
    req->send(200, "application/json", json);
  });

  // List book files as JSON (hides system/UI files)
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

  // Rename API
  server.on("/api/rename", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      req->send(200, "application/json", "{\"ok\":true}");
    },
    NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      String body = "";
      for (size_t i = 0; i < len; i++) body += (char)data[i];

      int fromStart = body.indexOf("\"from\":\"") + 8;
      int fromEnd   = body.indexOf("\"", fromStart);
      int toStart   = body.indexOf("\"to\":\"") + 6;
      int toEnd     = body.indexOf("\"", toStart);

      String from = body.substring(fromStart, fromEnd);
      String to   = body.substring(toStart, toEnd);

      if (from.length() > 0 && to.length() > 0) {
        LittleFS.rename("/" + from, "/" + to);
        loadBookList();
        drawBookList();
      }
    }
  );

  // File upload
  server.on("/upload", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      req->send(200, "text/plain", "Upload complete!");
    },
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

        // Save a friendly book name (strip extension, underscores → spaces)
        String bookName = filename;
        int dot = filename.lastIndexOf('.');
        if (dot > 0) bookName = filename.substring(0, dot);
        bookName.replace("_", " ");
        File f = LittleFS.open("/bookname.txt", "w");
        if (f) { f.print(bookName); f.close(); }
        Serial.println("Book name: " + bookName);

        loadBookList();
        drawBookList();
      }
    }
  );

  // Delete a file (books only — guards against deleting UI files)
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("name")) {
      req->send(400, "text/plain", "Missing name param");
      return;
    }
    String name = req->getParam("name")->value();
    if (isSystemFile(name)) {
      req->send(403, "text/plain", "Cannot delete system file");
      return;
    }
    LittleFS.remove("/" + name);
    loadBookList();
    drawBookList();
    req->send(200, "text/plain", "Deleted");
  });

  // Button state (optional debug endpoint)
  server.on("/api/buttons", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "{\"up\":"    + String(lastBtnUpState    == LOW ? "true" : "false") +
                  ",\"down\":"  + String(lastBtnDownState  == LOW ? "true" : "false") +
                  ",\"left\":"  + String(lastBtnLeftState  == LOW ? "true" : "false") +
                  ",\"right\":" + String(lastBtnRightState == LOW ? "true" : "false") + "}";
    req->send(200, "application/json", json);
  });

  // List all bookmarks
  server.on("/api/bookmarks", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "{\"bookmarks\":[";
    File f = LittleFS.open("/bookmarks.txt", "r");
    int idx = 0;
    bool first = true;
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        
        int sep1 = line.indexOf('|');
        int sep2 = line.indexOf('|', sep1 + 1);
        if (sep1 > 0 && sep2 > sep1) {
          if (!first) json += ",";
          String fname = line.substring(0, sep1);
          int page = line.substring(sep1 + 1, sep2).toInt();
          unsigned long time = line.substring(sep2 + 1).toInt();
          json += "{\"index\":" + String(idx);
          json += ",\"file\":\"" + fname + "\"";
          json += ",\"page\":" + String(page);
          json += ",\"time\":" + String(time) + "}";
          first = false;
          idx++;
        }
      }
      f.close();
    }
    json += "]}";
    req->send(200, "application/json", json);
  });

  // Delete bookmark by index
  server.on("/api/bookmarks", HTTP_DELETE, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("index")) {
      req->send(400, "application/json", "{\"error\":\"missing index\"}");
      return;
    }
    int delIdx = req->getParam("index")->value().toInt();
    
    File f = LittleFS.open("/bookmarks.txt", "r");
    String content = "";
    int current = 0;
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (current != delIdx) {
          content += line + "\n";
        }
        current++;
      }
      f.close();
    }
    f = LittleFS.open("/bookmarks.txt", "w");
    if (f) {
      f.print(content);
      f.close();
    }
    
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // Get bookmark content (page text)
  server.on("/api/bookmark/content", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("file") || !req->hasParam("page")) {
      req->send(400, "application/json", "{\"error\":\"missing file or page\"}");
      return;
    }
    
    String filename = "/" + req->getParam("file")->value();
    int pageNum = req->getParam("page")->value().toInt();
    
    File bookFile = LittleFS.open(filename, "r");
    if (!bookFile) {
      req->send(404, "application/json", "{\"error\":\"book not found\"}");
      return;
    }
    
    const int CHARS_PER_LINE = 26;
    const int LINES_PER_PAGE = 5;
    const int CHARS_PER_PAGE = CHARS_PER_LINE * LINES_PER_PAGE;
    int startPos = (pageNum - 1) * CHARS_PER_PAGE;
    
    String pageContent = "";
    int pos = 0;
    while (bookFile.available() && pos < startPos + CHARS_PER_PAGE) {
      char c = bookFile.read();
      pos++;
      if (pos <= startPos) continue;
      if (c == '\n' || c == '\r') {
        if (pageContent.length() > 0 && pageContent.endsWith(" ")) {
          pageContent.trim();
        }
        pageContent += ' ';
      } else {
        pageContent += c;
      }
    }
    bookFile.close();
    
    pageContent.trim();
    req->send(200, "application/json", "{\"content\":\"" + pageContent + "\",\"page\":" + String(pageNum) + "}");
  });

  server.begin();
  Serial.println("Server started at http://192.168.4.1");
}

// ============================================================
//  App state machine
// ============================================================
enum AppState { STATE_LIST, STATE_READING, STATE_SETTINGS, STATE_BOOKMARKS };
AppState appState = STATE_LIST;

// ---------- Sleep mode ----------
#define WAKE_PIN 25
unsigned long lastActivityTime = 0;
RTC_DATA_ATTR AppState savedAppState = STATE_LIST;
RTC_DATA_ATTR char savedCurrentBook[64] = "";
RTC_DATA_ATTR int savedCurrentPage = 1;
volatile bool sleepRequested = false;

void IRAM_ATTR sleepButtonISR() {
  sleepRequested = true;
}

// ---------- Display state tracking ----------
AppState lastAppState = STATE_LIST;
int lastSelectedIndex = -1;
int lastSelectedBookmark = -1;
int lastSettingsSelected = -1;
int lastSleepTimeoutMs = 0;
String lastCurrentBook = "";
int lastCurrentPage = -1;
bool lastDarkMode = false;

bool needsRedraw() {
  if (lastAppState != appState) return true;
  if (lastDarkMode != darkMode) return true;
  if (appState == STATE_LIST && lastSelectedIndex != selectedIndex) return true;
  if (appState == STATE_BOOKMARKS && lastSelectedBookmark != selectedBookmark) return true;
  if (appState == STATE_SETTINGS) {
    if (lastSettingsSelected != settingsSelectedIndex) return true;
    if (lastSleepTimeoutMs != sleepTimeoutMs) return true;
  }
  if (appState == STATE_READING) {
    if (lastCurrentBook != currentBook) return true;
    if (lastCurrentPage != currentPage) return true;
  }
  return false;
}

void updateDrawState() {
  lastAppState = appState;
  lastDarkMode = darkMode;
  lastSelectedIndex = selectedIndex;
  lastSelectedBookmark = selectedBookmark;
  lastSettingsSelected = settingsSelectedIndex;
  lastSleepTimeoutMs = sleepTimeoutMs;
  lastCurrentBook = currentBook;
  lastCurrentPage = currentPage;
}

void enterSleep() {
  savedAppState = appState;
  savedCurrentBook[0] = '\0';
  currentBook.toCharArray(savedCurrentBook, sizeof(savedCurrentBook));
  savedCurrentPage = currentPage;
  
  saveProgress(currentBook, currentPage);
  saveSettings();
  
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.display();
  delay(100);
  
  Serial.println("Configuring wake pins...");
  uint64_t wakeMask = (1ULL << WAKE_PIN) | (1ULL << BTN_UP) | (1ULL << BTN_DOWN) | 
                      (1ULL << BTN_LEFT) | (1ULL << BTN_RIGHT);
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ALL_LOW);
  Serial.println("Entering deep sleep... Wake on any button");
  esp_deep_sleep_start();
}

void setupWakePin() {
  uint64_t wakeMask = (1ULL << WAKE_PIN) | (1ULL << BTN_UP) | (1ULL << BTN_DOWN) | 
                      (1ULL << BTN_LEFT) | (1ULL << BTN_RIGHT);
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ALL_LOW);
}

void setupLightSleepWakeup() {
  gpio_wakeup_enable((gpio_num_t)BTN_UP,    GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_DOWN,  GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_LEFT,  GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_RIGHT, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)WAKE_PIN,  GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
}

// ============================================================
//  setup()
// ============================================================
void setup() {
  Serial.begin(115200);

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("Woke from deep sleep!");
    
    uint64_t wakeGpio = esp_sleep_get_ext1_wakeup_status();
    int wakeButton = -1;
    if (wakeGpio & (1ULL << WAKE_PIN)) wakeButton = WAKE_PIN;
    else if (wakeGpio & (1ULL << BTN_UP)) wakeButton = BTN_UP;
    else if (wakeGpio & (1ULL << BTN_DOWN)) wakeButton = BTN_DOWN;
    else if (wakeGpio & (1ULL << BTN_LEFT)) wakeButton = BTN_LEFT;
    else if (wakeGpio & (1ULL << BTN_RIGHT)) wakeButton = BTN_RIGHT;
    Serial.print("Wake button: GPIO"); Serial.println(wakeButton);
    
    lastActivityTime = millis();
    pinMode(BTN_UP,    INPUT_PULLUP);
    pinMode(BTN_DOWN,  INPUT_PULLUP);
    pinMode(BTN_LEFT,  INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(WAKE_PIN, INPUT_PULLUP);
    attachInterrupt(WAKE_PIN, sleepButtonISR, FALLING);
    LittleFS.begin(true);
    display.init(115200, true, 50, false);
    loadSettings();
    loadBookList();
    
    if (wakeButton == BTN_RIGHT && savedAppState == STATE_READING && savedCurrentBook[0] != '\0') {
      appState = STATE_READING;
      currentBook = String(savedCurrentBook);
      currentPage = savedCurrentPage;
      calculatePages(currentBook);
      if (currentPage > totalPages) currentPage = totalPages;
      if (currentPage < 1) currentPage = 1;
      Serial.println("Resuming to reading page (RIGHT wake)");
      drawReadingPage();
    } else {
      appState = STATE_LIST;
      Serial.println("Going to book list");
      drawBookList();
    }
    return;
  }

  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  pinMode(WAKE_PIN, INPUT_PULLUP);
  attachInterrupt(WAKE_PIN, sleepButtonISR, FALLING);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  setupServer();

  display.init(115200, true, 50, false);

  initBattery();
  batteryPercent = readBattery();
  lastBatteryUpdate = millis();

  setupWakePin();
  setupLightSleepWakeup();
  lastActivityTime = millis();

  loadSettings();
  loadBookList();
  drawBookList();
}

// ============================================================
//  loop()
// ============================================================
void loop() {
  if (sleepRequested) {
    sleepRequested = false;
    enterSleep();
  }

  unsigned long now = millis();

  if (now - lastActivityTime > sleepTimeoutMs) {
    enterSleep();
  }

  if (now - lastBatteryUpdate > 60000) {
    batteryPercent = readBattery();
    lastBatteryUpdate = now;
  }

  int upRaw    = digitalRead(BTN_UP);
  int downRaw  = digitalRead(BTN_DOWN);
  int leftRaw  = digitalRead(BTN_LEFT);
  int rightRaw = digitalRead(BTN_RIGHT);

  bool pressedUp    = false, pressedDown  = false;
  bool pressedLeft  = false, pressedRight = false;

  if (upRaw != lastBtnUpState && now - lastBtnUpTime > BTN_DEBOUNCE_MS) {
    lastBtnUpState = upRaw; lastBtnUpTime = now;
    if (upRaw == LOW) { pressedUp = true; lastActivityTime = now; Serial.println("BTN_UP"); }
  }
  if (downRaw != lastBtnDownState && now - lastBtnDownTime > BTN_DEBOUNCE_MS) {
    lastBtnDownState = downRaw; lastBtnDownTime = now;
    if (downRaw == LOW) { pressedDown = true; lastActivityTime = now; Serial.println("BTN_DOWN"); }
  }
  if (leftRaw != lastBtnLeftState && now - lastBtnLeftTime > BTN_DEBOUNCE_MS) {
    lastBtnLeftState = leftRaw; lastBtnLeftTime = now;
    if (leftRaw == LOW) { pressedLeft = true; lastActivityTime = now; Serial.println("BTN_LEFT"); }
  }
  if (rightRaw != lastBtnRightState && now - lastBtnRightTime > BTN_DEBOUNCE_MS) {
    lastBtnRightState = rightRaw; lastBtnRightTime = now;
    if (rightRaw == LOW) { pressedRight = true; lastActivityTime = now; Serial.println("BTN_RIGHT"); }
  }

  if (appState == STATE_LIST) {
    bool changed = false;
    if (pressedUp) {
      appState = STATE_BOOKMARKS;
      drawBookmarksScreen();
    }
    if (pressedDown && bookCount > 0) {
      selectedIndex = (selectedIndex + 1) % bookCount;
      changed = true;
    }
    if (pressedRight && bookCount > 0) {
      appState = STATE_READING;
      openBook(bookNames[selectedIndex]);
    }
    if (pressedLeft) {
      appState = STATE_SETTINGS;
      settingsSelectedIndex = 0;
      drawSettings();
    }

    if (changed) drawBookList();

  } else if (appState == STATE_READING) {
    if (pressedLeft) {
      saveProgress(currentBook, currentPage);
      appState = STATE_LIST;
      drawBookList();
    }
    if (pressedUp && currentPage > 1) {
      currentPage--;
      drawReadingPage();
    }
    if (pressedDown && currentPage < totalPages) {
      currentPage++;
      drawReadingPage();
    }
    if (pressedRight && currentPage < totalPages) {
      currentPage++;
      drawReadingPage();
    }
  } else if (appState == STATE_SETTINGS) {
    if (pressedUp) {
      settingsSelectedIndex = (settingsSelectedIndex - 1 + 3) % 3;
      drawSettings();
    }
    if (pressedDown) {
      settingsSelectedIndex = (settingsSelectedIndex + 1) % 3;
      drawSettings();
    }
    if (pressedRight) {
      if (settingsSelectedIndex == 0) {
        darkMode = !darkMode;
        saveSettings();
        drawSettings();
      } else if (settingsSelectedIndex == 1) {
        int minutes = sleepTimeoutMs / 60000;
        minutes = (minutes >= 10) ? 1 : minutes + 1;
        sleepTimeoutMs = minutes * 60000;
        saveSettings();
        drawSettings();
      } else if (settingsSelectedIndex == 2) {
        appState = STATE_LIST;
        drawBookList();
      }
    }
    if (pressedLeft) {
      appState = STATE_LIST;
      drawBookList();
    }
  } else if (appState == STATE_BOOKMARKS) {
    if (pressedUp && bookmarkCount > 0) {
      selectedBookmark = (selectedBookmark - 1 + bookmarkCount) % bookmarkCount;
      drawBookmarksScreen();
    }
    if (pressedDown && bookmarkCount > 0) {
      selectedBookmark = (selectedBookmark + 1) % bookmarkCount;
      drawBookmarksScreen();
    }
    if (pressedRight && bookmarkCount > 0) {
      currentBook = bookmarkFiles[selectedBookmark];
      calculatePages(currentBook);
      currentPage = bookmarkPages[selectedBookmark];
      if (currentPage > totalPages) currentPage = totalPages;
      if (currentPage < 1) currentPage = 1;
      appState = STATE_READING;
      drawReadingPage();
    }
    if (pressedLeft) {
      appState = STATE_LIST;
      drawBookList();
    }
  }

  esp_sleep_enable_timer_wakeup(100000);  // 100ms timer wakeup
  esp_light_sleep_start();
}
