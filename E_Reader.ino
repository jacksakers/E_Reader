#include <Arduino.h>         // Core Arduino library
#include "EPD.h"              // EPD library for e-paper display
#include "SD.h"               // SD card library
#include <vector>             // For dynamic text storage

// ==================== PIN DEFINITIONS ====================
#define SCREEN_PWR 7         // Screen power pin
#define HOME_KEY 2           // Home button
#define EXIT_KEY 1           // Exit/Back button
#define PRV_KEY 6            // Previous page button
#define NEXT_KEY 4           // Next page button
#define OK_KEY 5             // Confirm/OK button

// ==================== SD CARD PINS ====================
#define SD_MOSI 40
#define SD_MISO 13
#define SD_SCK 39
#define SD_CS 10
SPIClass SD_SPI = SPIClass(HSPI);

// ==================== DISPLAY PARAMETERS ====================
#define DISPLAY_WIDTH EPD_W  // 792 pixels
#define DISPLAY_HEIGHT EPD_H // 272 pixels
#define FONT_WIDTH 8         // Character width for 16pt font
#define FONT_HEIGHT 16       // Character height for 16pt font
#define CHARS_PER_LINE (DISPLAY_WIDTH / FONT_WIDTH) // ~99 chars
#define LINES_PER_PAGE (DISPLAY_HEIGHT / FONT_HEIGHT) // ~17 lines

// ==================== BUFFER & STATE ====================
uint8_t ImageBW[27200];      // Display buffer
char* fileBuffer = NULL;      // Dynamically allocated file content
size_t fileSize = 0;
size_t currentPos = 0;        // Current position in file
int currentPage = 0;          // Current page number

// ==================== FILE BROWSER STATE ====================
String currentPath = "/books/";
std::vector<String> bookList;
int selectedBook = 0;
bool inFileBrowser = true;
bool inReader = false;

// ==================== FUNCTION PROTOTYPES ====================
void initializeSDCard();
void displayBootScreen();
void fileBrowser();
void loadBook(String filename);
void displayPage();
void handleNavigation();
void pageUp();
void pageDown();
void scrollUp();
void scrollDown();
void displayMenu();
void calculatePages();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  
  // Initialize power and display
  pinMode(SCREEN_PWR, OUTPUT);
  digitalWrite(SCREEN_PWR, HIGH);  // Turn on screen power
  
  // Initialize SD card power
  pinMode(42, OUTPUT);
  digitalWrite(42, HIGH);  // Turn on SD card power (if needed)
  
  delay(100);  // Wait for power to stabilize
  
  // Initialize button pins
  pinMode(HOME_KEY, INPUT);
  pinMode(EXIT_KEY, INPUT);
  pinMode(PRV_KEY, INPUT);
  pinMode(NEXT_KEY, INPUT);
  pinMode(OK_KEY, INPUT);
  
  // Initialize EPD GPIO
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, DISPLAY_WIDTH, DISPLAY_HEIGHT, Rotation, WHITE);
  Paint_Clear(WHITE);
  
  // Initialize display
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();
  
  // Initialize SD card
  initializeSDCard();
  
  Serial.println("E-Reader initialization complete");
}

// ==================== MAIN LOOP ====================
void loop() {
  if (inFileBrowser) {
    fileBrowser();
  } else if (inReader) {
    displayPage();
    handleNavigation();
  }
}

// ==================== SD CARD INITIALIZATION ====================
void initializeSDCard() {
  Serial.println("Initializing SD SPI...");
  SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  Serial.println("SD SPI initialized");
  
  Serial.printf("Attempting SD.begin with CS=%d, Clock=4MHz\n", SD_CS);
  if (!SD.begin(SD_CS, SD_SPI, 4000000)) {
    Serial.println("ERROR: File system mount failed!");
    displayBootScreen();
    EPD_ShowString(0, 20, (char*)"SD Card Failed", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    EPD_DeepSleep();
    return;
  }
  
  Serial.printf("SD Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
  displayBootScreen();
}

// ==================== BOOT SCREEN ====================
void displayBootScreen() {
  Paint_Clear(WHITE);
  EPD_ShowString(0, 0, (char*)"E-READER", 16, BLACK);
  EPD_ShowString(0, 20, (char*)"Initializing...", 16, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// ==================== FILE BROWSER ====================
void fileBrowser() {
  Paint_Clear(WHITE);
  EPD_ShowString(0, 0, (char*)"SELECT BOOK", 16, BLACK);
  EPD_ShowString(0, 20, (char*)"---", 16, BLACK);
  
  File dir = SD.open(currentPath);
  if (!dir) {
    EPD_ShowString(0, 40, (char*)"No books folder", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    EPD_DeepSleep();
    return;
  }
  
  bookList.clear();
  int lineY = 40;
  int displayCount = 0;
  
  File file = dir.openNextFile();
  while (file && displayCount < LINES_PER_PAGE - 2) {
    if (!file.isDirectory()) {
      String filename = file.name();
      bookList.push_back(filename);
      
      // Highlight selected book
      if (displayCount == selectedBook) {
        EPD_ShowString(0, lineY, (char*)"> ", 16, BLACK);
        EPD_ShowString(16, lineY, (char*)filename.c_str(), 16, BLACK);
      } else {
        EPD_ShowString(0, lineY, (char*)filename.c_str(), 16, BLACK);
      }
      
      lineY += FONT_HEIGHT;
      displayCount++;
    }
    file.close();
    file = dir.openNextFile();
  }
  dir.close();
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  // Handle navigation in file browser
  if (digitalRead(NEXT_KEY) == 0) {
    delay(100);
    if (digitalRead(NEXT_KEY) == 1) {
      if (selectedBook < (int)bookList.size() - 1) {
        selectedBook++;
      }
      Serial.println("Next book");
    }
  } else if (digitalRead(PRV_KEY) == 0) {
    delay(100);
    if (digitalRead(PRV_KEY) == 1) {
      if (selectedBook > 0) {
        selectedBook--;
      }
      Serial.println("Previous book");
    }
  } else if (digitalRead(OK_KEY) == 0) {
    delay(100);
    if (digitalRead(OK_KEY) == 1) {
      Serial.printf("OK pressed! selectedBook=%d, bookListSize=%d\n", selectedBook, (int)bookList.size());
      if (selectedBook < (int)bookList.size()) {
        Serial.printf("Loading: %s\n", bookList[selectedBook].c_str());
        loadBook(bookList[selectedBook]);
        inFileBrowser = false;
        inReader = true;
        currentPage = 0;
        currentPos = 0;
      } else {
        Serial.println("ERROR: selectedBook >= bookList.size()");
      }
    }
  }
  
  delay(200);
}

// ==================== LOAD BOOK ====================
void loadBook(String filename) {
  Serial.printf("Loading book: %s\n", filename.c_str());
  
  // Free previous buffer
  if (fileBuffer != NULL) {
    free(fileBuffer);
  }
  
  String filepath = currentPath + filename;
  File file = SD.open(filepath);
  
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  
  fileSize = file.size();
  fileBuffer = (char*)malloc(fileSize + 1);
  
  if (fileBuffer == NULL) {
    Serial.println("Not enough memory for file");
    file.close();
    return;
  }
  
  file.read((uint8_t*)fileBuffer, fileSize);
  fileBuffer[fileSize] = '\0';
  file.close();
  
  Serial.printf("Book loaded: %d bytes\n", fileSize);
  calculatePages();
}

// ==================== CALCULATE PAGES ====================
void calculatePages() {
  // Simple calculation: count newlines and wrap text
  // For a full implementation, would need proper word wrapping
  Serial.printf("File size: %d bytes\n", fileSize);
  currentPos = 0;
}

// ==================== DISPLAY PAGE ====================
void displayPage() {
  if (fileBuffer == NULL || fileSize == 0) {
    Paint_Clear(WHITE);
    EPD_ShowString(0, 0, (char*)"No file loaded", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    return;
  }
  
  Paint_Clear(WHITE);
  
  // Display title
  EPD_ShowString(0, 0, (char*)"PAGE", 16, BLACK);
  
  // Extract and display text from current position
  int lineY = FONT_HEIGHT + 5;
  int lineCount = 0;
  size_t pos = currentPos;
  
  while (lineCount < LINES_PER_PAGE - 2 && pos < fileSize) {
    // Extract one line of text
    char lineBuffer[CHARS_PER_LINE + 1];
    int charCount = 0;
    
    // Handle word wrapping and line breaks
    while (charCount < CHARS_PER_LINE && pos < fileSize && fileBuffer[pos] != '\n') {
      lineBuffer[charCount] = fileBuffer[pos];
      charCount++;
      pos++;
    }
    
    // Skip newline character
    if (pos < fileSize && fileBuffer[pos] == '\n') {
      pos++;
    }
    
    lineBuffer[charCount] = '\0';
    
    if (charCount > 0) {
      EPD_ShowString(0, lineY, lineBuffer, 16, BLACK);
      lineY += FONT_HEIGHT;
      lineCount++;
    } else {
      // Empty line
      lineY += FONT_HEIGHT;
      lineCount++;
      if (pos < fileSize && fileBuffer[pos] == '\r') {
        pos++;
      }
    }
  }
  
  // Display page info
  char pageInfo[50];
  sprintf(pageInfo, "Page %d - Pos: %d/%d", currentPage, (int)currentPos, (int)fileSize);
  EPD_ShowString(0, DISPLAY_HEIGHT - FONT_HEIGHT - 5, pageInfo, 16, BLACK);
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// ==================== HANDLE NAVIGATION ====================
void handleNavigation() {
  // Next page - scroll down
  if (digitalRead(NEXT_KEY) == 0) {
    delay(100);
    if (digitalRead(NEXT_KEY) == 1) {
      pageDown();
      Serial.println("Next page");
    }
  }
  // Previous page - scroll up
  else if (digitalRead(PRV_KEY) == 0) {
    delay(100);
    if (digitalRead(PRV_KEY) == 1) {
      pageUp();
      Serial.println("Previous page");
    }
  }
  // Exit to file browser
  else if (digitalRead(EXIT_KEY) == 0) {
    delay(100);
    if (digitalRead(EXIT_KEY) == 1) {
      if (fileBuffer != NULL) {
        free(fileBuffer);
        fileBuffer = NULL;
      }
      inReader = false;
      inFileBrowser = true;
      currentPage = 0;
      currentPos = 0;
      Serial.println("Exit to file browser");
    }
  }
  // Home - go to beginning
  else if (digitalRead(HOME_KEY) == 0) {
    delay(100);
    if (digitalRead(HOME_KEY) == 1) {
      currentPos = 0;
      currentPage = 0;
      Serial.println("Home - beginning of file");
    }
  }
  
  delay(200);
}

// ==================== PAGE DOWN ====================
void pageDown() {
  if (currentPos >= fileSize) return;
  
  // Jump by approximate page size
  size_t pageJump = LINES_PER_PAGE * CHARS_PER_LINE;
  currentPos += pageJump;
  
  if (currentPos > fileSize) {
    currentPos = fileSize;
  }
  
  // Find next line boundary
  while (currentPos < fileSize && fileBuffer[currentPos] != '\n') {
    currentPos++;
  }
  if (currentPos < fileSize) currentPos++; // Skip the newline
  
  currentPage++;
}

// ==================== PAGE UP ====================
void pageUp() {
  if (currentPos == 0) return;
  
  size_t pageJump = LINES_PER_PAGE * CHARS_PER_LINE;
  
  if (currentPos > pageJump) {
    currentPos -= pageJump;
  } else {
    currentPos = 0;
  }
  
  // Find previous line boundary
  while (currentPos > 0 && fileBuffer[currentPos] != '\n') {
    currentPos--;
  }
  if (currentPos > 0) currentPos++; // Skip the newline
  
  if (currentPage > 0) currentPage--;
}

// ==================== SCROLL UP ====================
void scrollUp() {
  if (currentPos > CHARS_PER_LINE) {
    currentPos -= CHARS_PER_LINE;
  } else {
    currentPos = 0;
  }
}

// ==================== SCROLL DOWN ====================
void scrollDown() {
  size_t lineJump = CHARS_PER_LINE;
  if (currentPos + lineJump < fileSize) {
    currentPos += lineJump;
  }
}
