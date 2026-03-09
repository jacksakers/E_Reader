#ifndef EREADER_H
#define EREADER_H

#include "EPD.h"
#include "SD.h"
#include <vector>

// ==================== E-READER CONFIGURATION ====================
// NOTE: Display is 792x272 pixels (landscape) but with Rotation 180:
//   - Paint.width = 272 (vertical axis in rotated coordinates)
//   - Paint.height = 800 (horizontal axis in rotated coordinates)
// Therefore: X coordinates must stay within 0-271, Y within 0-799

#define EREADER_CHARS_PER_LINE 32     // Max chars per line: 272px / 8px per char = 34, use 32 for safety
#define EREADER_LINES_PER_PAGE 42     // Lines per page: (800 - margins) / line_height
#define EREADER_LINE_HEIGHT 16        // Font height (16px for size 16 font)
#define EREADER_TOP_MARGIN 50         // Space for header
#define EREADER_BOTTOM_MARGIN 50      // Space for footer
#define EREADER_LEFT_MARGIN 10        // Left text margin
#define EREADER_MAX_Y 750             // Maximum Y coordinate for text (800 - BOTTOM_MARGIN)

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
    
    // Only add next line if we're not at the end and position has advanced
    if (pos < fileSize && pos > lineStarts.back()) {
      lineStarts.push_back(pos);
      linesProcessed++;
    }
  }
  
  if (linesProcessed >= MAX_LINES) {
    Serial.printf("[EREADER] WARNING: Hit maximum line limit (%d)\n", MAX_LINES);
  }
  
  totalLines = lineStarts.size();
  
  // Validate consistency
  if (totalLines != lineStarts.size()) {
    Serial.printf("[EREADER] ERROR: Line count mismatch! totalLines=%d, lineStarts.size()=%d\n", 
                  totalLines, lineStarts.size());
    totalLines = lineStarts.size();  // Force sync
  }
  
  Serial.printf("[EREADER] Built line index: %d lines\n", totalLines);
  Serial.printf("[EREADER] Free heap after index: %d bytes\n", ESP.getFreeHeap());
  Serial.flush();
}

// Get text for a specific line - writes directly to buffer
int eReaderGetLineToBuffer(int lineNumber, char* buffer, int maxLen) {
  using namespace EReaderNS;
  
  // Safety checks
  if (buffer == NULL || maxLen <= 0) {
    return 0;
  }
  
  buffer[0] = '\0';  // Always ensure null termination
  
  if (lineNumber < 0 || lineNumber >= totalLines || fileBuffer == NULL) {
    return 0;
  }
  
  if (lineNumber >= lineStarts.size()) {
    return 0;
  }
  
  size_t start = lineStarts[lineNumber];
  // CRITICAL FIX: Check against lineStarts.size(), not totalLines, to avoid vector out-of-bounds
  size_t end = ((size_t)(lineNumber + 1) < lineStarts.size()) ? lineStarts[lineNumber + 1] : fileSize;
  
  // Validate bounds
  if (start >= fileSize || end > fileSize || start > end) {
    return 0;
  }
  
  // Copy directly to buffer
  int copied = 0;
  for (size_t i = start; i < end && i < fileSize && copied < (maxLen - 1); i++) {
    char c = fileBuffer[i];
    if (c == '\n' || c == '\r') break;
    // Skip non-printable characters except space
    if ((c >= 32 && c <= 126) || c == '\t') {
      buffer[copied++] = c;
    }
  }
  buffer[copied] = '\0';
  
  return copied;
}

// ==================== UI RENDERING ====================

void eReaderDrawHeader(const char* title, int progress) {
  // With Rotation 180: X is 0-271, Y is 0-799
  // Header is at top: Y near 0
  
  // Top border line (horizontal)
  EPD_DrawLine(0, 35, 271, 35, BLACK);
  
  // Title - shortened to fit width
  if (title != NULL && strlen(title) > 0) {
    char titleBuf[20];  // 20 chars max at 8px/char = 160px
    strncpy(titleBuf, title, 19);
    titleBuf[19] = '\0';
    EPD_ShowString(EREADER_LEFT_MARGIN, 10, titleBuf, 16, BLACK);
  }
  
  // Progress percentage on same line as title
  char progressText[10];
  sprintf(progressText, "%d%%", progress);
  EPD_ShowString(200, 10, progressText, 16, BLACK);
}

void eReaderDrawFooter(int currentPage, int totalPages, const char* controls) {
  // Footer is at bottom: Y near 800
  int footerY = 800 - EREADER_BOTTOM_MARGIN + 10;
  
  // Bottom border line
  EPD_DrawLine(0, footerY - 5, 271, footerY - 5, BLACK);
  
  // Page numbers
  char pageText[20];
  sprintf(pageText, "Pg %d/%d", currentPage, totalPages);
  EPD_ShowString(EREADER_LEFT_MARGIN, footerY, pageText, 16, BLACK);
  
  // Simplified controls hint 
  if (controls != NULL) {
    char controlsBuf[15];
    strncpy(controlsBuf, "EXIT=Menu", 14);
    controlsBuf[14] = '\0';
    EPD_ShowString(150, footerY, controlsBuf, 16, BLACK);
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
  Serial.printf("[EREADER] totalLines=%d, currentLine=%d, LINES_PER_PAGE=%d\n", 
                totalLines, currentLine, EREADER_LINES_PER_PAGE);
  Serial.printf("[EREADER] Y range: %d to %d (max safe: %d) | X range: %d to %d\n", 
                EREADER_TOP_MARGIN, EREADER_MAX_Y, EREADER_MAX_Y, EREADER_LEFT_MARGIN, 271);
  Serial.flush();
  
  // Validate state before drawing
  if (lineStarts.size() != totalLines) {
    Serial.printf("[EREADER] ERROR: lineStarts size mismatch! size=%d totalLines=%d\n", 
                  lineStarts.size(), totalLines);
    // Force sync to prevent crash
    totalLines = lineStarts.size();
  }
  
  if (lineStarts.empty() || totalLines == 0) {
    Serial.println("[EREADER] ERROR: No lines in index");
    Paint_Clear(WHITE);
    EPD_ShowString(EREADER_LEFT_MARGIN, EREADER_TOP_MARGIN + 30, (char*)"ERROR: Empty line index", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    return;
  }
  
  int yPos = EREADER_TOP_MARGIN;
  int linesDisplayed = 0;
  
  // Pre-allocate line buffer outside loop and zero it completely
  char lineBuf[EREADER_CHARS_PER_LINE + 1];
  
  Serial.println("[EREADER] Drawing lines...");
  Serial.flush();
  
  for (int i = 0; i < EREADER_LINES_PER_PAGE && (currentLine + i) < totalLines; i++) {
    int targetLine = currentLine + i;
    
    // Additional safety check to prevent vector out-of-bounds
    if (targetLine >= lineStarts.size()) {
      Serial.printf("[EREADER] WARNING: Line %d exceeds lineStarts size %d, stopping\n", 
                    targetLine, lineStarts.size());
      break;
    }
    
    // Check if yPos is within safe bounds BEFORE attempting to draw
    // Ensure there's enough room for a 16-pixel high font
    if (yPos < EREADER_TOP_MARGIN || yPos > (EREADER_MAX_Y - 16)) {
      Serial.printf("[EREADER] WARNING: Line %d at y=%d out of safe range [%d-%d], stopping\n", 
                    targetLine, yPos, EREADER_TOP_MARGIN, EREADER_MAX_Y - 16);
      break;
    }
    
    // Clear buffer for this line
    memset(lineBuf, 0, sizeof(lineBuf));
    
    // Get line directly into buffer (no String operations)
    int lineLen = eReaderGetLineToBuffer(targetLine, lineBuf, EREADER_CHARS_PER_LINE + 1);
    
    if (lineLen > 0) {
      // Extra safety: validate x position is also within bounds
      if (EREADER_LEFT_MARGIN >= 0 && EREADER_LEFT_MARGIN < 780) {
        EPD_ShowString(EREADER_LEFT_MARGIN, yPos, lineBuf, 16, BLACK);
      }
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
  
  // Header (with correct rotated dimensions: X=0-271, Y=0-799)
  EPD_DrawLine(0, 35, 271, 35, BLACK);
  char* headerText = (char*)"E-READER";
  EPD_ShowString(EREADER_LEFT_MARGIN, 10, headerText, 16, BLACK);
  
  // Check for books folder
  File dir = SD.open(booksPath);
  if (!dir) {
    EPD_ShowString(EREADER_LEFT_MARGIN, 100, (char*)"ERROR: No /books/", 16, BLACK);
    EPD_ShowString(EREADER_LEFT_MARGIN, 130, (char*)"folder found", 16, BLACK);
    EPD_ShowString(EREADER_LEFT_MARGIN, 700, (char*)"EXIT: Home", 16, BLACK);
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
    EPD_ShowString(EREADER_LEFT_MARGIN, 100, (char*)"No books found", 16, BLACK);
    EPD_ShowString(EREADER_LEFT_MARGIN, 130, (char*)"Upload .txt", 16, BLACK);
    EPD_ShowString(EREADER_LEFT_MARGIN, 700, (char*)"EXIT: Home", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    return;
  }
  
  // Display books (scrolling support)
  int maxVisible = 35;  // Books visible at once (more with taller display)
  int startIdx = browserScrollOffset;
  int yPos = 60;
  
  for (int i = 0; i < maxVisible && (startIdx + i) < bookList.size(); i++) {
    int bookIdx = startIdx + i;
    
    // Check if we're running out of Y space
    if (yPos + 18 > 700) break;
    
    // Highlight selected book
    if (bookIdx == selectedBook) {
      // Draw selection indicator
      EPD_ShowString(EREADER_LEFT_MARGIN, yPos, (char*)">", 16, BLACK);
    }
    
    // Display filename (truncate if too long - max 30 chars at 8px/char)
    String displayName = bookList[bookIdx];
    if (displayName.length() > 28) {
      displayName = displayName.substring(0, 25) + "...";
    }
    
    EPD_ShowString(EREADER_LEFT_MARGIN + 16, yPos, (char*)displayName.c_str(), 16, BLACK);
    yPos += 18;
  }
  
  // Footer
  EPD_DrawLine(0, 755, 271, 755, BLACK);
  char footer[30];
  sprintf(footer, "%d books", bookList.size());
  EPD_ShowString(EREADER_LEFT_MARGIN, 765, footer, 16, BLACK);
  EPD_ShowString(150, 765, (char*)"OK=Open", 16, BLACK);
  
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
  
  Serial.printf("[EREADER] Buffer allocated at %p\n", fileBuffer);
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
