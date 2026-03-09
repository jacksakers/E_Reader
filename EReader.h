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

// ==================== E-READER STATE ====================
namespace EReaderNS {
  // External display buffer
  extern uint8_t ImageBW[27200];
  
  // File management
  char* fileBuffer = NULL;
  size_t fileSize = 0;
  String currentFilename = "";
  String booksPath = "/books/";
  
  // Reading position
  int currentLine = 0;           // Current top line being displayed
  int totalLines = 0;            // Total lines in the book
  std::vector<size_t> lineStarts;  // Start position of each line in buffer
  
  // UI state
  bool inBrowser = true;
  bool needsRefresh = true;
  std::vector<String> bookList;
  int selectedBook = 0;
  int browserScrollOffset = 0;   // For scrolling long book lists
  
  // Reading statistics
  int readingProgress = 0;       // Percentage read (0-100)
}

// ==================== TEXT PROCESSING ====================

// Word-wrap text and build line index
void eReaderBuildLineIndex() {
  using namespace EReaderNS;
  
  lineStarts.clear();
  if (fileBuffer == NULL || fileSize == 0) return;
  
  lineStarts.push_back(0);  // First line starts at 0
  size_t pos = 0;
  
  while (pos < fileSize) {
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
      }
      pos++;
      if (fileBuffer[pos - 1] == '\n' && pos < fileSize && fileBuffer[pos] != '\n') {
        break;  // Stop after first newline if next char isn't newline
      }
    }
    
    if (pos < fileSize && pos > lineStarts.back()) {
      lineStarts.push_back(pos);
    }
  }
  
  totalLines = lineStarts.size();
  Serial.printf("[EREADER] Built line index: %d lines\n", totalLines);
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
  
  // Title
  EPD_ShowString(EREADER_LEFT_MARGIN, 8, (char*)title, 16, BLACK);
  
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
  
  // Controls hint
  EPD_ShowString(300, footerY, (char*)controls, 16, BLACK);
}

// ==================== READER DISPLAY ====================

void eReaderDisplayPage() {
  using namespace EReaderNS;
  
  Paint_Clear(WHITE);
  
  if (fileBuffer == NULL || fileSize == 0) {
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
  
  // Draw header
  eReaderDrawHeader(currentFilename.c_str(), readingProgress);
  
  // Draw text content
  int yPos = EREADER_TOP_MARGIN;
  int linesDisplayed = 0;
  
  for (int i = 0; i < EREADER_LINES_PER_PAGE && (currentLine + i) < totalLines; i++) {
    String line = eReaderGetLine(currentLine + i);
    
    if (line.length() > 0) {
      EPD_ShowString(EREADER_LEFT_MARGIN, yPos, (char*)line.c_str(), 16, BLACK);
    }
    
    yPos += EREADER_LINE_HEIGHT;
    linesDisplayed++;
  }
  
  // Draw footer
  eReaderDrawFooter(currentPage, totalPages, "EXIT: Menu | HOME: Quit");
  
  // Update display
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  Serial.printf("[EREADER] Page %d/%d, Line %d/%d, Progress %d%%\n", 
                currentPage, totalPages, currentLine, totalLines, readingProgress);
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
  
  Serial.printf("[EREADER] Loading book: %s\n", filename.c_str());
  
  // Free previous buffer
  if (fileBuffer != NULL) {
    free(fileBuffer);
    fileBuffer = NULL;
  }
  
  // Reset state
  lineStarts.clear();
  currentLine = 0;
  totalLines = 0;
  
  // Open file
  String filepath = booksPath + filename;
  File file = SD.open(filepath);
  
  if (!file) {
    Serial.println("[EREADER] Failed to open file");
    return false;
  }
  
  fileSize = file.size();
  Serial.printf("[EREADER] File size: %d bytes\n", fileSize);
  
  // Check if file fits in memory
  if (fileSize > 1000000) {  // 1MB limit
    Serial.println("[EREADER] File too large");
    file.close();
    return false;
  }
  
  // Allocate memory
  fileBuffer = (char*)malloc(fileSize + 1);
  if (fileBuffer == NULL) {
    Serial.println("[EREADER] Not enough memory");
    file.close();
    return false;
  }
  
  // Read file
  size_t bytesRead = file.read((uint8_t*)fileBuffer, fileSize);
  fileBuffer[fileSize] = '\0';
  file.close();
  
  if (bytesRead != fileSize) {
    Serial.printf("[EREADER] Read error: %d / %d bytes\n", bytesRead, fileSize);
    free(fileBuffer);
    fileBuffer = NULL;
    return false;
  }
  
  // Store filename
  currentFilename = filename;
  if (currentFilename.length() > 50) {
    currentFilename = currentFilename.substring(0, 47) + "...";
  }
  
  // Build line index with word wrapping
  eReaderBuildLineIndex();
  
  Serial.printf("[EREADER] Book loaded successfully: %d lines\n", totalLines);
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
  
  Serial.println("[EREADER] Initializing E-Reader...");
  
  inBrowser = true;
  needsRefresh = true;
  selectedBook = 0;
  browserScrollOffset = 0;
  currentLine = 0;
  
  if (fileBuffer != NULL) {
    free(fileBuffer);
    fileBuffer = NULL;
  }
  
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, 800, 272, Rotation, WHITE);
}

void eReaderUpdate() {
  using namespace EReaderNS;
  
  if (needsRefresh) {
    if (inBrowser) {
      eReaderDisplayBrowser();
    } else {
      eReaderDisplayPage();
    }
    needsRefresh = false;
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
    if (eReaderLoadBook(bookList[selectedBook])) {
      inBrowser = false;
      needsRefresh = true;
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
