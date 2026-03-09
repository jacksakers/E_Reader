#ifndef EREADER_H
#define EREADER_H

#include "EPD.h"
#include "SD.h"
#include <vector>

// ==================== E-READER CONFIGURATION ====================
#define EREADER_CHARS_PER_LINE 98     // Slightly less than 99 for margin
#define EREADER_LINES_PER_PAGE 13     // Leave room for header/footer
#define EREADER_LINE_HEIGHT 18        // Slightly more spacing for readability
#define EREADER_TOP_MARGIN 45         // Space for header
#define EREADER_BOTTOM_MARGIN 30      // Space for footer
#define EREADER_LEFT_MARGIN 10        // Left text margin

// Reference to global display buffer from OS_Main.ino
extern uint8_t ImageBW[27200];

// ==================== E-READER STATE ====================
namespace EReaderNS {
  // File management
  static char* fileBuffer = NULL;
  static size_t fileSize = 0;
  static String currentFilename;
  static String booksPath;
  
  // Reading position
  static int currentLine = 0;           // Current top line being displayed
  static int totalLines = 0;            // Total lines in the book
  static std::vector<size_t> lineStarts;  // Start position of each line in buffer
  
  // UI state
  static bool inBrowser = true;
  static bool needsRefresh = true;
  static std::vector<String> bookList;
  static int selectedBook = 0;
  static int browserScrollOffset = 0;   // For scrolling long book lists
  
  // Reading statistics
  static int readingProgress = 0;       // Percentage read (0-100)
}

// ==================== TEXT PROCESSING ====================

// Word-wrap text and build line index
void eReaderBuildLineIndex() {
  using namespace EReaderNS;
  
  Serial.println("[EREADER] Building line index...");
  Serial.flush();
  
  lineStarts.clear();
  if (fileBuffer == NULL || fileSize == 0) {
    Serial.println("[EREADER] ERROR: No file buffer or empty file");
    return;
  }
  
  try {
    lineStarts.reserve(fileSize / 50);  // Reserve space (estimate ~50 chars per line)
    Serial.printf("[EREADER] Reserved space for ~%d lines\n", fileSize / 50);
  } catch (...) {
    Serial.println("[EREADER] WARNING: Could not reserve vector space");
  }
  Serial.flush();
  
  lineStarts.push_back(0);  // First line starts at 0
  size_t pos = 0;
  int linesProcessed = 0;
  const int MAX_LINES = 10000;  // Safety limit
  
  while (pos < fileSize && linesProcessed < MAX_LINES) {
    // Progress update every 1000 lines
    if (linesProcessed > 0 && linesProcessed % 1000 == 0) {
      Serial.printf("[EREADER] Processed %d lines...\n", linesProcessed);
      Serial.flush();
      yield();  // Give other tasks a chance to run
    }
    
    size_t lineEnd = pos;
    size_t lastSpace = pos;
    int charCount = 0;
    bool foundNewline = false;
    
    // Find line break (word wrap or newline)
    while (charCount < EREADER_CHARS_PER_LINE && lineEnd < fileSize) {
      char c = fileBuffer[lineEnd];
      
      // Handle newline
      if (c == '\n') {
        lineEnd++;
        foundNewline = true;
        break;
      }
      
      // Skip carriage return
      if (c == '\r') {
        lineEnd++;
        continue;
      }
      
      // Track spaces for word wrapping
      if (c == ' ' || c == '\t') {
        lastSpace = lineEnd;
      }
      
      lineEnd++;
      charCount++;
    }
    
    // If we hit the character limit without a newline
    if (!foundNewline && lineEnd < fileSize && charCount >= EREADER_CHARS_PER_LINE) {
      // Try to break at last space for word wrap
      if (lastSpace > pos && (lineEnd - lastSpace) < 20) {
        lineEnd = lastSpace + 1;  // Break after the space
      }
    }
    
    pos = lineEnd;
    
    // Skip multiple consecutive newlines but preserve one
    while (pos < fileSize && (fileBuffer[pos] == '\r' || fileBuffer[pos] == '\n')) {
      if (fileBuffer[pos] == '\n') {
        lineStarts.push_back(pos + 1);  // Empty line
        linesProcessed++;
      }
      pos++;
      if (fileBuffer[pos - 1] == '\n' && pos < fileSize && fileBuffer[pos] != '\n') {
        break;  // Stop after first newline if next char isn't newline
      }
    }
    
    if (pos < fileSize && pos > lineStarts.back()) {
      lineStarts.push_back(pos);
      linesProcessed++;
    }
  }
  
  if (linesProcessed >= MAX_LINES) {
    Serial.printf("[EREADER] WARNING: Hit maximum line limit (%d)\n", MAX_LINES);
  }
  
  totalLines = lineStarts.size();
  Serial.printf("[EREADER] Built line index: %d lines\n", totalLines);
  Serial.printf("[EREADER] Free heap after index: %d bytes\n", ESP.getFreeHeap());
  Serial.flush();
}

// Get text for a specific line
String eReaderGetLine(int lineNumber) {
  using namespace EReaderNS;
  
  if (lineNumber < 0 || lineNumber >= totalLines || fileBuffer == NULL) {
    return "";
  }
  
  size_t start = lineStarts[lineNumber];
  size_t end = (lineNumber + 1 < totalLines) ? lineStarts[lineNumber + 1] : fileSize;
  
  // Build string from buffer
  String line = "";
  for (size_t i = start; i < end && i < fileSize; i++) {
    char c = fileBuffer[i];
    if (c == '\n' || c == '\r') break;
    line += c;
  }
  
  return line;
}

// ==================== UI RENDERING ====================

void eReaderDrawHeader(const char* title, int progress) {
  // Top border
  EPD_DrawLine(0, 0, 792, 0, BLACK);
  EPD_DrawLine(0, 35, 792, 35, BLACK);
  
  // Title - make a safe copy first
  if (title != NULL && strlen(title) > 0) {
    char titleBuf[60];
    strncpy(titleBuf, title, 59);
    titleBuf[59] = '\0';
    EPD_ShowString(EREADER_LEFT_MARGIN, 8, titleBuf, 16, BLACK);
  }
  
  // Progress bar
  int barWidth = 200;
  int barX = 550;
  int barY = 12;
  int barHeight = 12;
  
  // Progress bar background
  EPD_DrawRectangle(barX, barY, barX + barWidth, barY + barHeight, BLACK, 0);
  
  // Progress bar fill
  int fillWidth = (barWidth - 4) * progress / 100;
  if (fillWidth > 0) {
    for (int y = barY + 2; y < barY + barHeight - 2; y++) {
      EPD_DrawLine(barX + 2, y, barX + 2 + fillWidth, y, BLACK);
    }
  }
  
  // Progress text
  char progressText[10];
  sprintf(progressText, "%d%%", progress);
  EPD_ShowString(barX + barWidth + 10, barY, progressText, 16, BLACK);
}

void eReaderDrawFooter(int currentPage, int totalPages, const char* controls) {
  int footerY = 272 - EREADER_BOTTOM_MARGIN + 5;
  
  // Bottom border
  EPD_DrawLine(0, 272 - EREADER_BOTTOM_MARGIN, 792, 272 - EREADER_BOTTOM_MARGIN, BLACK);
  
  // Page numbers
  char pageText[50];
  sprintf(pageText, "Page %d / %d", currentPage, totalPages);
  EPD_ShowString(EREADER_LEFT_MARGIN, footerY, pageText, 16, BLACK);
  
  // Controls hint - make safe copy
  if (controls != NULL) {
    char controlsBuf[80];
    strncpy(controlsBuf, controls, 79);
    controlsBuf[79] = '\0';
    EPD_ShowString(300, footerY, controlsBuf, 16, BLACK);
  }
}

// ==================== READER DISPLAY ====================

void eReaderDisplayPage() {
  using namespace EReaderNS;
  
  Serial.println("[EREADER] Displaying page...");
  Serial.flush();
  
  // Safety check BEFORE doing anything
  if (fileBuffer == NULL || fileSize == 0 || totalLines == 0) {
    Serial.println("[EREADER] ERROR: No file loaded or empty");
    Paint_Clear(WHITE);
    EPD_ShowString(200, 100, (char*)"No book loaded", 16, BLACK);
    EPD_ShowString(200, 130, (char*)"Press EXIT to return to browser", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    return;
  }
  
  // Calculate pages
  int totalPages = (totalLines + EREADER_LINES_PER_PAGE - 1) / EREADER_LINES_PER_PAGE;
  int currentPage = (currentLine / EREADER_LINES_PER_PAGE) + 1;
  
  // Calculate reading progress
  readingProgress = totalLines > 0 ? (currentLine * 100 / totalLines) : 0;
  
  Serial.printf("[EREADER] Drawing page %d/%d (lines %d-%d of %d)\n", 
                currentPage, totalPages, currentLine, 
                min(currentLine + EREADER_LINES_PER_PAGE, totalLines), totalLines);
  Serial.flush();
  
  // Clear screen before drawing
  Serial.println("[EREADER] Clearing screen...");
  Serial.flush();
  Paint_Clear(WHITE);
  Serial.println("[EREADER] Screen cleared");
  Serial.flush();
  
  // Draw header - use safe string
  Serial.println("[EREADER] Drawing header...");
  Serial.flush();
  eReaderDrawHeader(currentFilename.c_str(), readingProgress);
  Serial.println("[EREADER] Header drawn");
  Serial.flush();
  
  // Draw text content
  Serial.println("[EREADER] Drawing text content...");
  Serial.flush();
  
  int yPos = EREADER_TOP_MARGIN;
  int linesDisplayed = 0;
  
  for (int i = 0; i < EREADER_LINES_PER_PAGE && (currentLine + i) < totalLines; i++) {
    String line = eReaderGetLine(currentLine + i);
    
    if (line.length() > 0) {
      // Make a safe copy in a char buffer
      char lineBuf[EREADER_CHARS_PER_LINE + 1];
      int copyLen = min((int)line.length(), EREADER_CHARS_PER_LINE);
      strncpy(lineBuf, line.c_str(), copyLen);
      lineBuf[copyLen] = '\0';
      
      EPD_ShowString(EREADER_LEFT_MARGIN, yPos, lineBuf, 16, BLACK);
    }
    
    yPos += EREADER_LINE_HEIGHT;
    linesDisplayed++;
  }
  
  Serial.printf("[EREADER] Drew %d lines\n", linesDisplayed);
  Serial.flush();
  
  // Draw footer
  Serial.println("[EREADER] Drawing footer...");
  Serial.flush();
  eReaderDrawFooter(currentPage, totalPages, "EXIT: Menu | HOME: Quit");
  Serial.println("[EREADER] Footer drawn");
  Serial.flush();
  
  // Update display
  Serial.println("[EREADER] Updating EPD display...");
  Serial.flush();
  EPD_Display(ImageBW);
  Serial.println("[EREADER] Calling EPD_PartUpdate...");
  Serial.flush();
  EPD_PartUpdate();
  Serial.println("[EREADER] EPD update complete");
  Serial.flush();
  
  Serial.printf("[EREADER] Page %d/%d displayed successfully\n", currentPage, totalPages);
  Serial.flush();
}

// ==================== FILE BROWSER ====================

void eReaderDisplayBrowser() {
  using namespace EReaderNS;
  
  Paint_Clear(WHITE);
  
  // Header
  EPD_DrawLine(0, 0, 792, 0, BLACK);
  EPD_DrawLine(0, 35, 792, 35, BLACK);
  EPD_ShowString(EREADER_LEFT_MARGIN, 8, (char*)"E-READER - SELECT BOOK", 16, BLACK);
  
  // Check for books folder
  File dir = SD.open(booksPath);
  if (!dir) {
    EPD_ShowString(EREADER_LEFT_MARGIN, 60, (char*)"ERROR: /books/ folder not found", 16, BLACK);
    EPD_ShowString(EREADER_LEFT_MARGIN, 90, (char*)"Please create /books/ on SD card", 16, BLACK);
    EPD_ShowString(EREADER_LEFT_MARGIN, 120, (char*)"Use Web Portal to upload books", 16, BLACK);
    EPD_DrawLine(0, 240, 792, 240, BLACK);
    EPD_ShowString(EREADER_LEFT_MARGIN, 248, (char*)"EXIT: Return to Home", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    return;
  }
  
  // Load book list
  bookList.clear();
  File file = dir.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String filename = String(file.name());
      // Only show .txt files
      if (filename.endsWith(".txt") || filename.endsWith(".TXT")) {
        bookList.push_back(filename);
      }
    }
    file.close();
    file = dir.openNextFile();
  }
  dir.close();
  
  // Sort book list alphabetically
  std::sort(bookList.begin(), bookList.end());
  
  if (bookList.size() == 0) {
    EPD_ShowString(EREADER_LEFT_MARGIN, 60, (char*)"No books found in /books/", 16, BLACK);
    EPD_ShowString(EREADER_LEFT_MARGIN, 90, (char*)"Upload .txt files via Web Portal", 16, BLACK);
    EPD_DrawLine(0, 240, 792, 240, BLACK);
    EPD_ShowString(EREADER_LEFT_MARGIN, 248, (char*)"EXIT: Return to Home", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    return;
  }
  
  // Display books (with scrolling support)
  int maxVisible = 11;  // Books visible at once
  int startIdx = browserScrollOffset;
  int yPos = 50;
  
  for (int i = 0; i < maxVisible && (startIdx + i) < bookList.size(); i++) {
    int bookIdx = startIdx + i;
    
    // Highlight selected book
    if (bookIdx == selectedBook) {
      // Draw selection box
      EPD_DrawRectangle(5, yPos - 2, 787, yPos + 16, BLACK, 0);
      EPD_ShowString(EREADER_LEFT_MARGIN + 5, yPos, (char*)"> ", 16, BLACK);
    }
    
    // Display filename (truncate if too long)
    String displayName = bookList[bookIdx];
    if (displayName.length() > 90) {
      displayName = displayName.substring(0, 87) + "...";
    }
    
    EPD_ShowString(EREADER_LEFT_MARGIN + 25, yPos, (char*)displayName.c_str(), 16, BLACK);
    yPos += 18;
  }
  
  // Footer with instructions
  EPD_DrawLine(0, 240, 792, 240, BLACK);
  char footer[100];
  sprintf(footer, "Books: %d | UP/DOWN: Select | OK: Open | EXIT: Home", bookList.size());
  EPD_ShowString(EREADER_LEFT_MARGIN, 248, footer, 16, BLACK);
  
  // Scroll indicators
  if (browserScrollOffset > 0) {
    EPD_ShowString(750, 50, (char*)"^", 16, BLACK);
  }
  if (browserScrollOffset + maxVisible < bookList.size()) {
    EPD_ShowString(750, 220, (char*)"v", 16, BLACK);
  }
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  Serial.printf("[EREADER] Browser showing books %d-%d of %d\n", 
                startIdx + 1, min(startIdx + maxVisible, (int)bookList.size()), bookList.size());
}

// ==================== FILE LOADING ====================

bool eReaderLoadBook(const String& filename) {
  using namespace EReaderNS;
  
  Serial.println("[EREADER] ========================================");
  Serial.printf("[EREADER] Loading book: %s\n", filename.c_str());
  Serial.printf("[EREADER] Free heap before load: %d bytes\n", ESP.getFreeHeap());
  Serial.flush();
  
  // Free previous buffer
  if (fileBuffer != NULL) {
    Serial.println("[EREADER] Freeing previous buffer...");
    free(fileBuffer);
    fileBuffer = NULL;
  }
  
  // Reset state
  Serial.println("[EREADER] Resetting state...");
  lineStarts.clear();
  currentLine = 0;
  totalLines = 0;
  Serial.flush();
  
  // Open file
  String filepath = booksPath + filename;
  Serial.printf("[EREADER] Opening file: %s\n", filepath.c_str());
  Serial.flush();
  
  File file = SD.open(filepath);
  
  if (!file) {
    Serial.println("[EREADER] ERROR: Failed to open file");
    Serial.flush();
    return false;
  }
  
  fileSize = file.size();
  Serial.printf("[EREADER] File opened. Size: %d bytes\n", fileSize);
  Serial.flush();
  
  // Check if file fits in memory
  size_t availableHeap = ESP.getFreeHeap();
  Serial.printf("[EREADER] Available heap: %d bytes\n", availableHeap);
  
  if (fileSize > 500000) {  // 500KB limit for safety
    Serial.printf("[EREADER] ERROR: File too large (%d bytes, limit 500KB)\n", fileSize);
    file.close();
    return false;
  }
  
  if (fileSize + 50000 > availableHeap) {  // Need at least 50KB buffer
    Serial.printf("[EREADER] ERROR: Not enough heap (need %d, have %d)\n", fileSize + 50000, availableHeap);
    file.close();
    return false;
  }
  
  // Allocate memory
  Serial.printf("[EREADER] Allocating %d bytes...\n", fileSize + 1);
  Serial.flush();
  
  fileBuffer = (char*)malloc(fileSize + 1);
  if (fileBuffer == NULL) {
    Serial.println("[EREADER] ERROR: malloc() failed");
    Serial.flush();
    file.close();
    return false;
  }
  
  Serial.printf("[EREADER] Buffer allocated at 0x%p\n", fileBuffer);
  Serial.printf("[EREADER] Free heap after malloc: %d bytes\n", ESP.getFreeHeap());
  Serial.flush();
  
  // Read file
  Serial.println("[EREADER] Reading file...");
  Serial.flush();
  
  size_t bytesRead = file.read((uint8_t*)fileBuffer, fileSize);
  fileBuffer[fileSize] = '\0';
  file.close();
  
  Serial.printf("[EREADER] Read %d bytes\n", bytesRead);
  Serial.flush();
  
  if (bytesRead != fileSize) {
    Serial.printf("[EREADER] ERROR: Read mismatch: %d / %d bytes\n", bytesRead, fileSize);
    free(fileBuffer);
    fileBuffer = NULL;
    return false;
  }
  
  // Store filename
  Serial.println("[EREADER] Storing filename...");
  currentFilename = filename;
  if (currentFilename.length() > 50) {
    currentFilename = currentFilename.substring(0, 47) + "...";
  }
  Serial.printf("[EREADER] Filename: %s\n", currentFilename.c_str());
  Serial.flush();
  
  // Build line index with word wrapping
  Serial.println("[EREADER] Building line index...");
  Serial.flush();
  delay(100);
  
  eReaderBuildLineIndex();
  
  Serial.printf("[EREADER] Book loaded successfully: %d lines\n", totalLines);
  Serial.printf("[EREADER] Final free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("[EREADER] ========================================");
  Serial.flush();
  delay(100);
  
  return true;
}

// ==================== NAVIGATION ====================

void eReaderScrollDown(int lines = 1) {
  using namespace EReaderNS;
  
  currentLine += lines;
  if (currentLine >= totalLines - EREADER_LINES_PER_PAGE) {
    currentLine = max(0, totalLines - EREADER_LINES_PER_PAGE);
  }
  needsRefresh = true;
}

void eReaderScrollUp(int lines = 1) {
  using namespace EReaderNS;
  
  currentLine -= lines;
  if (currentLine < 0) {
    currentLine = 0;
  }
  needsRefresh = true;
}

void eReaderPageDown() {
  eReaderScrollDown(EREADER_LINES_PER_PAGE);
}

void eReaderPageUp() {
  eReaderScrollUp(EREADER_LINES_PER_PAGE);
}

void eReaderGoToStart() {
  using namespace EReaderNS;
  currentLine = 0;
  needsRefresh = true;
}

void eReaderGoToEnd() {
  using namespace EReaderNS;
  currentLine = max(0, totalLines - EREADER_LINES_PER_PAGE);
  needsRefresh = true;
}

// ==================== PUBLIC INTERFACE ====================

void eReaderInit() {
  using namespace EReaderNS;
  
  Serial.println("[EREADER] ======== INITIALIZING E-READER ========");
  Serial.printf("[EREADER] Free heap at init: %d bytes\n", ESP.getFreeHeap());
  Serial.flush();
  
  // Initialize String variables
  currentFilename = "";
  booksPath = "/books/";
  bookList.clear();
  
  inBrowser = true;
  needsRefresh = true;
  selectedBook = 0;
  browserScrollOffset = 0;
  currentLine = 0;
  totalLines = 0;
  lineStarts.clear();
  
  if (fileBuffer != NULL) {
    free(fileBuffer);
    fileBuffer = NULL;
  }
  fileSize = 0;
  
  Serial.println("[EREADER] Initializing display...");
  Serial.flush();
  
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, 800, 272, Rotation, WHITE);
  
  Serial.println("[EREADER] E-Reader initialized");
  Serial.printf("[EREADER] Free heap after init: %d bytes\n", ESP.getFreeHeap());
  Serial.println("[EREADER] =====================================");
  Serial.flush();
}

void eReaderUpdate() {
  using namespace EReaderNS;
  
  if (needsRefresh) {
    Serial.printf("[EREADER] Update requested (inBrowser=%d)\n", inBrowser);
    Serial.flush();
    
    if (inBrowser) {
      eReaderDisplayBrowser();
    } else {
      eReaderDisplayPage();
    }
    needsRefresh = false;
    
    Serial.println("[EREADER] Update complete");
    Serial.flush();
  }
}

void eReaderHandleBrowserInput(bool upPressed, bool downPressed, bool okPressed, bool exitPressed) {
  using namespace EReaderNS;
  
  if (upPressed) {
    if (selectedBook > 0) {
      selectedBook--;
      
      // Adjust scroll offset if needed
      int maxVisible = 11;
      if (selectedBook < browserScrollOffset) {
        browserScrollOffset = selectedBook;
      }
      
      needsRefresh = true;
    }
  }
  
  if (downPressed) {
    if (selectedBook < bookList.size() - 1) {
      selectedBook++;
      
      // Adjust scroll offset if needed
      int maxVisible = 11;
      if (selectedBook >= browserScrollOffset + maxVisible) {
        browserScrollOffset = selectedBook - maxVisible + 1;
      }
      
      needsRefresh = true;
    }
  }
  
  if (okPressed && bookList.size() > 0) {
    Serial.println("[EREADER] OK pressed - loading book...");
    Serial.flush();
    delay(100);
    
    if (eReaderLoadBook(bookList[selectedBook])) {
      Serial.println("[EREADER] Book loaded, switching to reader view...");
      Serial.flush();
      delay(100);
      
      inBrowser = false;
      needsRefresh = true;
      
      Serial.println("[EREADER] Switched to reader mode");
      Serial.flush();
    } else {
      Serial.println("[EREADER] ERROR: Failed to load book");
      Serial.flush();
    }
  }
  
  if (exitPressed) {
    // Signal to exit E-Reader mode
  }
}

void eReaderHandleReaderInput(bool upPressed, bool downPressed, bool exitPressed, bool homePressed) {
  using namespace EReaderNS;
  
  if (upPressed) {
    eReaderScrollUp(3);  // Scroll 3 lines at a time for smooth navigation
  }
  
  if (downPressed) {
    eReaderScrollDown(3);
  }
  
  if (exitPressed) {
    inBrowser = true;
    needsRefresh = true;
  }
  
  if (homePressed) {
    // Signal to return to OS home
  }
}

bool eReaderIsInBrowser() {
  return EReaderNS::inBrowser;
}

void eReaderCleanup() {
  using namespace EReaderNS;
  
  Serial.println("[EREADER] Cleaning up...");
  
  if (fileBuffer != NULL) {
    free(fileBuffer);
    fileBuffer = NULL;
  }
  
  lineStarts.clear();
  bookList.clear();
}

#endif // EREADER_H
