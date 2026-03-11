#ifndef HTTPREADER_H
#define HTTPREADER_H

// ==================== HTTP READER ====================
// Fetches articles (text) or images from http_reader_server.py and renders
// them on the EPD.  The server exposes two endpoints:
//
//   GET /text?url=<URL>[&limit=<N>]
//     Returns: { "success": true, "text": "..." }
//     Text is word-wrapped and paginated like the E-Reader mode.
//
//   GET /image.bin?url=<IMG_URL>&w=792&h=272
//     Returns: raw 1-bit packed binary (27200 bytes) ready for ImageBW.
//
// Server address is read from /httpreader_config.txt on the SD card.
// If that file is absent the compile-time default below is used.
//
// Configuration file format (one key=value per line):
//   server=http://192.168.1.100:5000
//   text_limit=6000
// ===========================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "EPD.h"
#include "SD.h"
#include <vector>

// ==================== DEFAULT CONFIGURATION ====================
#ifndef HTTPREADER_DEFAULT_SERVER
#define HTTPREADER_DEFAULT_SERVER "http://192.168.1.58:5000"
#endif
#define HTTPREADER_CONFIG_FILE    "/httpreader_config.txt"

// Layout constants — mirror EReader.h dimensions so text looks identical
#define HTTPREADER_CHARS_PER_LINE  98
#define HTTPREADER_LINES_PER_PAGE  11
#define HTTPREADER_LINE_HEIGHT     18
#define HTTPREADER_TOP_MARGIN      45
#define HTTPREADER_BOTTOM_MARGIN   30
#define HTTPREADER_LEFT_MARGIN     10
#define HTTPREADER_MAX_Y           242   // 272 - BOTTOM_MARGIN

// Network limits
#define HTTPREADER_HTTP_TIMEOUT_MS  15000   // 15 s per request
#define HTTPREADER_DEFAULT_TEXT_LIMIT 6000  // chars requested from server
#define HTTPREADER_MAX_TEXT_BYTES  16384    // hard cap to protect heap
#define HTTPREADER_IMAGE_BUFFER_SIZE 27200  // must match ImageBW size

// Reference to global display buffer (defined in OS_Main.ino)
extern uint8_t ImageBW[27200];

// ==================== MODULE STATE ====================
namespace HttpReaderNS {

  enum State {
    HR_URL_SELECT,
    HR_FETCHING,
    HR_READING,
    HR_VIEWING_IMAGE,
    HR_ERROR
  };

  // ---- Configurable server address ----
  static char serverBase[128] = HTTPREADER_DEFAULT_SERVER;
  static int  textLimit       = HTTPREADER_DEFAULT_TEXT_LIMIT;

  // ---- URL Preset list ----
  struct Preset {
    char label[52];
    char path[164];   // path + query after serverBase, e.g. "/text?url=..."
    bool isImage;
  };

  static const int MAX_PRESETS = 8;
  static Preset    presets[MAX_PRESETS];
  static int       presetCount  = 0;
  static int       selectedPreset = 0;

  // ---- Text reader ----
  static char*              textBuffer = NULL;
  static size_t             textLength = 0;
  static std::vector<size_t> lineStarts;
  static int                totalLines  = 0;
  static int                currentLine = 0;

  // ---- UI ----
  static State  currentState = HR_URL_SELECT;
  static bool   needsRefresh = true;
  static String errorMessage;
  static String pageTitle;   // short title shown in header while reading

} // namespace HttpReaderNS

// ==================== CONFIG FILE ====================

// Load server address and text limit from /httpreader_config.txt
static void httpReaderLoadConfig() {
  using namespace HttpReaderNS;

  if (!SD.exists(HTTPREADER_CONFIG_FILE)) {
    Serial.println("[HTTPREADER] Config file not found, using defaults");
    return;
  }

  File f = SD.open(HTTPREADER_CONFIG_FILE, FILE_READ);
  if (!f) return;

  char line[200];
  int  i = 0;
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\n' || c == '\r') {
      if (i > 0) {
        line[i] = '\0';
        // Parse "key=value"
        char* eq = strchr(line, '=');
        if (eq) {
          *eq = '\0';
          const char* key = line;
          const char* val = eq + 1;
          if (strcmp(key, "server") == 0) {
            strncpy(serverBase, val, sizeof(serverBase) - 1);
            serverBase[sizeof(serverBase) - 1] = '\0';
          } else if (strcmp(key, "text_limit") == 0) {
            int lim = atoi(val);
            if (lim > 500 && lim <= HTTPREADER_MAX_TEXT_BYTES) {
              textLimit = lim;
            }
          }
        }
        i = 0;
      }
    } else if (i < (int)(sizeof(line) - 2)) {
      line[i++] = c;
    }
  }
  f.close();
  Serial.printf("[HTTPREADER] Config loaded: server=%s, limit=%d\n", serverBase, textLimit);
}

// ==================== PRESET BUILDER ====================

// Called after config is loaded so URLs use the correct serverBase.
static void httpReaderBuildPresets() {
  using namespace HttpReaderNS;

  presetCount = 0;

  // Helper: append a preset entry
  auto add = [](const char* label, const char* path, bool isImg) {
    if (HttpReaderNS::presetCount >= MAX_PRESETS) return;
    Preset& p = HttpReaderNS::presets[HttpReaderNS::presetCount++];
    strncpy(p.label, label, sizeof(p.label) - 1);
    p.label[sizeof(p.label) - 1] = '\0';
    strncpy(p.path, path, sizeof(p.path) - 1);
    p.path[sizeof(p.path) - 1] = '\0';
    p.isImage = isImg;
  };

  // Text presets — all go through the /text endpoint on the server
  add("Hacker News Front Page",
      "/text?url=https://news.ycombinator.com",
      false);
  add("Wikipedia: Random Article",
      "/text?url=https://en.m.wikipedia.org/wiki/Special:Random",
      false);
  add("Reuters: Top Stories",
      "/text?url=https://www.reuters.com",
      false);
  add("Project Gutenberg: Alice in Wonderland",
      "/text?url=https://www.gutenberg.org/files/11/11-h/11-h.htm",
      false);

  // Image presets — raw 1-bit binary sized for 792x272
  add("NASA Astronomy Pic of Day",
      "/image.bin?w=792&h=272&url=https://apod.nasa.gov/apod/astropix.html",
      true);

  Serial.printf("[HTTPREADER] %d preset(s) loaded\n", presetCount);
}

// Build the complete URL for a preset by concatenating serverBase + path
static void httpReaderBuildURL(int idx, char* outBuf, size_t outLen) {
  using namespace HttpReaderNS;
  snprintf(outBuf, outLen, "%s%s", serverBase, presets[idx].path);
}

// ==================== WORD-WRAP INDEXER ====================
// Mirrors eReaderBuildLineIndex() but operates on HttpReaderNS::textBuffer

static void httpReaderBuildLineIndex() {
  using namespace HttpReaderNS;

  lineStarts.clear();
  if (textBuffer == NULL || textLength == 0) return;

  lineStarts.reserve(textLength / 50);
  lineStarts.push_back(0);

  size_t pos           = 0;
  int    linesBuilt    = 0;
  const  int MAX_LINES = 5000;

  while (pos < textLength && linesBuilt < MAX_LINES) {
    size_t lineEnd     = pos;
    size_t lastSpace   = pos;
    int    charCount   = 0;
    bool   foundNL     = false;

    while (charCount < HTTPREADER_CHARS_PER_LINE && lineEnd < textLength) {
      char c = textBuffer[lineEnd];
      if (c == '\n') { lineEnd++; foundNL = true; break; }
      if (c == '\r') { lineEnd++; continue; }
      if (c == ' ' || c == '\t') lastSpace = lineEnd;
      lineEnd++;
      charCount++;
    }

    // Soft word-wrap: break at last space if possible
    if (!foundNL && lineEnd < textLength && charCount >= HTTPREADER_CHARS_PER_LINE) {
      if (lastSpace > pos && (lineEnd - lastSpace) < 20) {
        lineEnd = lastSpace + 1;
      }
    }

    pos = lineEnd;

    // Absorb blank lines (preserve as empty entries so paragraphs render)
    while (pos < textLength && (textBuffer[pos] == '\r' || textBuffer[pos] == '\n')) {
      if (textBuffer[pos] == '\n') {
        lineStarts.push_back(pos + 1);
        linesBuilt++;
      }
      pos++;
      if (pos < textLength && textBuffer[pos - 1] == '\n' && textBuffer[pos] != '\n') break;
    }

    if (pos < textLength && pos > lineStarts.back()) {
      lineStarts.push_back(pos);
      linesBuilt++;
    }
  }

  totalLines = lineStarts.size();
  Serial.printf("[HTTPREADER] Line index: %d lines from %d chars\n", totalLines, textLength);
}

// ==================== NETWORK HELPERS ====================

// Show a simple "loading" screen while the request is in flight
static void httpReaderShowLoading(const char* url) {
  Paint_Clear(WHITE);
  EPD_DrawLine(0, 0, 792, 0, BLACK);
  EPD_DrawLine(0, 35, 792, 35, BLACK);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 8,
                 (char*)"HTTP READER - FETCHING...", 16, BLACK);

  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 65,
                 (char*)"Connecting to server, please wait...", 16, BLACK);

  // Show a truncated URL so the user knows what's being fetched
  char display[96];
  strncpy(display, url, 95);
  display[95] = '\0';
  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 95, display, 16, BLACK);

  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// Fetch text from /text endpoint, populate textBuffer and build line index
static bool httpReaderFetchText(const char* url) {
  using namespace HttpReaderNS;

  httpReaderShowLoading(url);

  HTTPClient http;
  http.setTimeout(HTTPREADER_HTTP_TIMEOUT_MS);
  http.begin(url);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    errorMessage = String("Server returned HTTP ") + String(code);
    currentState = HR_ERROR;
    needsRefresh  = true;
    return false;
  }

  // The payload should be a small JSON envelope; getString() is fine here
  // because the server already caps the inner text at textLimit chars.
  String payload = http.getString();
  http.end();

  if (payload.isEmpty()) {
    errorMessage = "Empty response from server";
    currentState = HR_ERROR;
    needsRefresh  = true;
    return false;
  }

  // Parse JSON: { "success": bool, "text": "...", "error": "..." }
  // Allocate document with a reasonable size cap
  const size_t JSON_CAPACITY = min((int)payload.length() + 512,
                                   HTTPREADER_MAX_TEXT_BYTES + 512);
  DynamicJsonDocument doc(JSON_CAPACITY);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    errorMessage = String("JSON parse error: ") + err.c_str();
    currentState = HR_ERROR;
    needsRefresh  = true;
    return false;
  }

  if (!doc["success"].as<bool>()) {
    errorMessage = String("Server error: ") + doc["error"].as<String>();
    currentState = HR_ERROR;
    needsRefresh  = true;
    return false;
  }

  const char* text = doc["text"];
  if (text == NULL || strlen(text) == 0) {
    errorMessage = "No text content in response";
    currentState = HR_ERROR;
    needsRefresh  = true;
    return false;
  }

  // Copy text into a persistent heap buffer (free old one first)
  if (textBuffer != NULL) { free(textBuffer); textBuffer = NULL; }

  textLength = strnlen(text, HTTPREADER_MAX_TEXT_BYTES);
  textBuffer = (char*)malloc(textLength + 1);
  if (textBuffer == NULL) {
    errorMessage = "Out of memory allocating text buffer";
    currentState = HR_ERROR;
    needsRefresh  = true;
    return false;
  }
  memcpy(textBuffer, text, textLength);
  textBuffer[textLength] = '\0';

  httpReaderBuildLineIndex();

  currentLine  = 0;
  currentState = HR_READING;
  needsRefresh  = true;

  Serial.printf("[HTTPREADER] Text loaded: %d chars, %d lines\n", textLength, totalLines);
  return true;
}

// Fetch image from /image.bin endpoint, stream directly into ImageBW
static bool httpReaderFetchImage(const char* url) {
  using namespace HttpReaderNS;

  httpReaderShowLoading(url);

  HTTPClient http;
  http.setTimeout(HTTPREADER_HTTP_TIMEOUT_MS);
  http.begin(url);
  http.addHeader("Accept", "application/octet-stream");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    errorMessage = String("Server returned HTTP ") + String(code);
    currentState = HR_ERROR;
    needsRefresh  = true;
    return false;
  }

  // Stream binary payload in chunks directly into the display buffer
  WiFiClient* stream   = http.getStreamPtr();
  size_t      received = 0;
  uint8_t     chunk[256];
  unsigned long deadline = millis() + HTTPREADER_HTTP_TIMEOUT_MS;

  while ((http.connected() || stream->available()) &&
         received < HTTPREADER_IMAGE_BUFFER_SIZE) {
    int avail = stream->available();
    if (avail > 0) {
      int toRead = min(avail, (int)sizeof(chunk));
      toRead     = min(toRead, (int)(HTTPREADER_IMAGE_BUFFER_SIZE - (int)received));
      int got    = stream->readBytes(chunk, toRead);
      if (got > 0) {
        memcpy(ImageBW + received, chunk, got);
        received += (size_t)got;
      }
    } else if (millis() > deadline) {
      break;
    }
    yield(); // keep the watchdog happy during long transfers
  }
  http.end();

  if (received < 1000) {
    errorMessage = String("Incomplete image data: ") + String(received) + " bytes";
    currentState = HR_ERROR;
    needsRefresh  = true;
    return false;
  }

  // Zero-fill any remaining bytes so stale pixels don't show
  if (received < HTTPREADER_IMAGE_BUFFER_SIZE) {
    memset(ImageBW + received, 0xFF, HTTPREADER_IMAGE_BUFFER_SIZE - received);
  }

  Serial.printf("[HTTPREADER] Image received: %d bytes\n", received);
  currentState = HR_VIEWING_IMAGE;
  needsRefresh  = true;
  return true;
}

// ==================== DISPLAY RENDERING ====================

static void httpReaderDrawHeader(const char* title, int progressPct) {
  EPD_DrawLine(0, 0, 792, 0, BLACK);
  EPD_DrawLine(0, 35, 792, 35, BLACK);

  if (title && strlen(title) > 0) {
    char buf[60];
    strncpy(buf, title, 59);
    buf[59] = '\0';
    EPD_ShowString(HTTPREADER_LEFT_MARGIN, 8, buf, 16, BLACK);
  }

  // Progress bar (same layout as EReader)
  const int barX = 550, barY = 12, barW = 200, barH = 12;
  EPD_DrawRectangle(barX, barY, barX + barW, barY + barH, BLACK, 0);
  int fill = (barW - 4) * progressPct / 100;
  if (fill > 0) {
    for (int y = barY + 2; y < barY + barH - 2; y++) {
      EPD_DrawLine(barX + 2, y, barX + 2 + fill, y, BLACK);
    }
  }
  char pct[10];
  sprintf(pct, "%d%%", progressPct);
  EPD_ShowString(barX + barW + 10, barY, pct, 16, BLACK);
}

static void httpReaderDrawFooter(int page, int totalPages) {
  int y = 272 - HTTPREADER_BOTTOM_MARGIN + 5;
  EPD_DrawLine(0, 272 - HTTPREADER_BOTTOM_MARGIN, 792,
               272 - HTTPREADER_BOTTOM_MARGIN, BLACK);
  char buf[50];
  sprintf(buf, "Page %d / %d", page, totalPages);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN, y, buf, 16, BLACK);
  EPD_ShowString(310, y, (char*)"DN/UP: Scroll  EXIT: Menu", 16, BLACK);
}

// Render the current text page
static void httpReaderDisplayPage() {
  using namespace HttpReaderNS;

  if (textBuffer == NULL || totalLines == 0) {
    Paint_Clear(WHITE);
    EPD_ShowString(200, 110, (char*)"No content loaded", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    return;
  }

  int totalPages  = (totalLines + HTTPREADER_LINES_PER_PAGE - 1) / HTTPREADER_LINES_PER_PAGE;
  int currentPage = (currentLine / HTTPREADER_LINES_PER_PAGE) + 1;
  int progress    = totalLines > 0 ? (currentLine * 100 / totalLines) : 0;

  Paint_Clear(WHITE);
  httpReaderDrawHeader(pageTitle.c_str(), progress);

  int  yPos = HTTPREADER_TOP_MARGIN;
  char lineBuf[HTTPREADER_CHARS_PER_LINE + 1];

  for (int i = 0; i < HTTPREADER_LINES_PER_PAGE; i++) {
    int lineIdx = currentLine + i;
    if (lineIdx >= totalLines) break;
    if (yPos + 16 > HTTPREADER_MAX_Y) break;

    size_t start = lineStarts[lineIdx];
    size_t end   = (lineIdx + 1 < totalLines) ? lineStarts[lineIdx + 1] : textLength;

    int copied = 0;
    for (size_t j = start; j < end && j < textLength &&
                            copied < HTTPREADER_CHARS_PER_LINE; j++) {
      char c = textBuffer[j];
      if (c == '\n' || c == '\r') break;
      if (c == '\t') c = ' ';
      if (c >= 32 && c <= 126) lineBuf[copied++] = c;
    }
    lineBuf[copied] = '\0';

    if (copied > 0) {
      EPD_ShowString(HTTPREADER_LEFT_MARGIN, yPos, lineBuf, 16, BLACK);
    }
    yPos += HTTPREADER_LINE_HEIGHT;
  }

  httpReaderDrawFooter(currentPage, totalPages);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// Render the fetched image (buffer already loaded by httpReaderFetchImage)
static void httpReaderDisplayImage() {
  // Display the raw bitmap that was streamed into ImageBW
  EPD_Display(ImageBW);
  EPD_FastUpdate();

  // Brief footer so the user knows which button to press
  Serial.println("[HTTPREADER] Image displayed — press EXIT or OK to return");
}

// Render the URL selection menu
static void httpReaderDisplayURLSelect() {
  using namespace HttpReaderNS;

  Paint_Clear(WHITE);
  EPD_DrawLine(0, 0, 792, 0, BLACK);
  EPD_DrawLine(0, 35, 792, 35, BLACK);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 8,
                 (char*)"HTTP READER - SELECT SOURCE", 16, BLACK);

  // Show configured server so the user can see it
  char hint[90];
  snprintf(hint, sizeof(hint), "Server: %s", serverBase);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 42, hint, 16, BLACK);

  int yPos = 68;
  for (int i = 0; i < presetCount; i++) {
    if (yPos + 16 > HTTPREADER_MAX_Y) break;

    bool sel = (i == selectedPreset);
    if (sel) {
      EPD_DrawRectangle(HTTPREADER_LEFT_MARGIN - 2, yPos - 2,
                        788, yPos + 18, BLACK, 0);
      EPD_ShowString(HTTPREADER_LEFT_MARGIN + 4, yPos,
                     (char*)"> ", 16, BLACK);
    }

    // Build label: "[TXT] Hacker News..." or "[IMG] NASA..."
    char row[72];
    snprintf(row, sizeof(row), "%s %s",
             presets[i].isImage ? "[IMG]" : "[TXT]",
             presets[i].label);
    EPD_ShowString(HTTPREADER_LEFT_MARGIN + 22, yPos, row, 16, BLACK);

    yPos += 22;
  }

  EPD_DrawLine(0, 242, 792, 242, BLACK);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 250,
                 (char*)"UP/DN: Select  OK: Fetch  EXIT: Back to Home", 16, BLACK);

  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// Render the error screen
static void httpReaderDisplayError() {
  using namespace HttpReaderNS;

  Paint_Clear(WHITE);
  EPD_DrawLine(0, 0, 792, 0, BLACK);
  EPD_DrawLine(0, 35, 792, 35, BLACK);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 8,
                 (char*)"HTTP READER - ERROR", 16, BLACK);

  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 58,
                 (char*)"Could not fetch content:", 16, BLACK);

  char errBuf[100];
  strncpy(errBuf, errorMessage.c_str(), 99);
  errBuf[99] = '\0';
  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 82, errBuf, 16, BLACK);

  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 120,
                 (char*)"Please check:", 16, BLACK);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN + 12, 140,
                 (char*)"1. Device is connected to WiFi (STA mode)", 16, BLACK);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN + 12, 160,
                 (char*)"2. http_reader_server.py is running on your PC", 16, BLACK);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN + 12, 180,
                 (char*)"3. Server IP matches /httpreader_config.txt", 16, BLACK);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN + 12, 200,
                 (char*)"   (create file: server=http://IP:5000)", 16, BLACK);

  EPD_DrawLine(0, 242, 792, 242, BLACK);
  EPD_ShowString(HTTPREADER_LEFT_MARGIN, 250,
                 (char*)"OK: Try Again  EXIT: Back to Home", 16, BLACK);

  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// ==================== PUBLIC INTERFACE ====================

// Override the server base URL at runtime (e.g. called from Settings)
void httpReaderSetServer(const char* baseUrl) {
  strncpy(HttpReaderNS::serverBase, baseUrl, sizeof(HttpReaderNS::serverBase) - 1);
  HttpReaderNS::serverBase[sizeof(HttpReaderNS::serverBase) - 1] = '\0';
  httpReaderBuildPresets();
  Serial.printf("[HTTPREADER] Server set to: %s\n", HttpReaderNS::serverBase);
}

// Called when entering HTTP Reader mode from the home menu
void httpReaderInit() {
  using namespace HttpReaderNS;

  Serial.println("[HTTPREADER] Initializing...");

  // Load server address / limit from SD card config file
  httpReaderLoadConfig();
  httpReaderBuildPresets();

  selectedPreset = 0;
  currentLine    = 0;
  needsRefresh   = true;

  if (WiFi.status() != WL_CONNECTED) {
    errorMessage = "WiFi not connected — enable STA mode in Settings";
    currentState = HR_ERROR;
  } else {
    currentState = HR_URL_SELECT;
    Serial.printf("[HTTPREADER] WiFi OK: %s\n", WiFi.localIP().toString().c_str());
  }
}

// Called every loop() iteration while in HTTP Reader mode
void httpReaderUpdate() {
  using namespace HttpReaderNS;

  if (!needsRefresh) return;
  needsRefresh = false;

  switch (currentState) {
    case HR_URL_SELECT:    httpReaderDisplayURLSelect(); break;
    case HR_READING:       httpReaderDisplayPage();      break;
    case HR_VIEWING_IMAGE: httpReaderDisplayImage();     break;
    case HR_ERROR:         httpReaderDisplayError();     break;
    default: break;
  }
}

// Handle button input.  Parameters match the pattern used by other modules.
// Returns true if the module consumed the input, false if EXIT was pressed
// from the top-level menu (meaning the caller should return to home).
bool httpReaderHandleInput(bool upPressed, bool downPressed,
                           bool okPressed, bool exitPressed) {
  using namespace HttpReaderNS;

  switch (currentState) {

    // ---- URL selection menu ----
    case HR_URL_SELECT:
      if (upPressed && selectedPreset > 0) {
        selectedPreset--;
        needsRefresh = true;
        return true;
      }
      if (downPressed && selectedPreset < presetCount - 1) {
        selectedPreset++;
        needsRefresh = true;
        return true;
      }
      if (okPressed && presetCount > 0) {
        char url[256];
        httpReaderBuildURL(selectedPreset, url, sizeof(url));
        // Derive a short page title from the preset label
        pageTitle = String(presets[selectedPreset].label);
        if (pageTitle.length() > 40) {
          pageTitle = pageTitle.substring(0, 37) + "...";
        }
        if (presets[selectedPreset].isImage) {
          httpReaderFetchImage(url);
        } else {
          // Append the text limit so the server truncates safely
          char urlWithLimit[300];
          // Check if path already has a query string
          bool hasQuery = strchr(url, '?') != NULL;
          snprintf(urlWithLimit, sizeof(urlWithLimit), "%s%climit=%d",
                   url, hasQuery ? '&' : '?', textLimit);
          httpReaderFetchText(urlWithLimit);
        }
        return true;
      }
      if (exitPressed) {
        return false; // Signal caller to return to home
      }
      break;

    // ---- Text pager ----
    case HR_READING:
      if (downPressed) {
        if (currentLine + HTTPREADER_LINES_PER_PAGE < totalLines) {
          currentLine += HTTPREADER_LINES_PER_PAGE;
          needsRefresh = true;
        }
        return true;
      }
      if (upPressed) {
        currentLine -= HTTPREADER_LINES_PER_PAGE;
        if (currentLine < 0) currentLine = 0;
        needsRefresh = true;
        return true;
      }
      if (exitPressed) {
        currentState = HR_URL_SELECT;
        needsRefresh  = true;
        return true;
      }
      break;

    // ---- Image viewer ----
    case HR_VIEWING_IMAGE:
      if (exitPressed || okPressed) {
        currentState = HR_URL_SELECT;
        needsRefresh  = true;
        return true;
      }
      break;

    // ---- Error screen ----
    case HR_ERROR:
      if (okPressed) {
        // Retry: go back to URL select so the user can pick again
        currentState = HR_URL_SELECT;
        needsRefresh  = true;
        return true;
      }
      if (exitPressed) {
        return false; // Signal caller to return to home
      }
      break;

    default: break;
  }

  return true;
}

// Returns true when the top-level URL menu is shown (used by OS_Main to
// decide whether EXIT should exit to home vs navigate within the module)
bool httpReaderIsInURLSelect() {
  return HttpReaderNS::currentState == HttpReaderNS::HR_URL_SELECT;
}

// Free heap memory and reset state.  Call when leaving HTTP Reader mode.
void httpReaderCleanup() {
  using namespace HttpReaderNS;

  if (textBuffer != NULL) {
    free(textBuffer);
    textBuffer = NULL;
  }
  textLength   = 0;
  totalLines   = 0;
  currentLine  = 0;
  lineStarts.clear();
  currentState = HR_URL_SELECT;
  needsRefresh = true;

  Serial.println("[HTTPREADER] Cleaned up");
}

#endif // HTTPREADER_H
