#include <Arduino.h>
#include "EPD.h"
#include "SD.h"
#include <WiFi.h>
#include <WebServer.h>

// ==================== PIN DEFINITIONS ====================
#define SCREEN_PWR 7         // Screen power pin
#define SD_PWR 42            // SD card power pin
#define HOME_KEY 2           // Home button
#define EXIT_KEY 1           // Exit/Back button
#define PRV_KEY 6            // Previous/Up button
#define NEXT_KEY 4           // Next/Down button
#define OK_KEY 5             // Confirm/Select button

// ==================== SD CARD PINS ====================
#define SD_MOSI 40
#define SD_MISO 13
#define SD_SCK 39
#define SD_CS 10
SPIClass SD_SPI = SPIClass(HSPI);

// ==================== DISPLAY PARAMETERS ====================
#define DISPLAY_WIDTH EPD_W   // 800 pixels (buffer width for dual drivers)
#define DISPLAY_HEIGHT EPD_H  // 272 pixels
#define FONT_HEIGHT 16

// ==================== DISPLAY BUFFER ====================
uint8_t ImageBW[27200];

// ==================== SYSTEM STATE ====================
enum SystemMode {
  MODE_HOME,
  MODE_EREADER,
  MODE_DASHBOARD,
  MODE_ARTFRAME,
  MODE_WEBPORTAL,
  MODE_SETTINGS
};

SystemMode currentMode = MODE_HOME;
int selectedMenuItem = 0;
bool needsRedraw = true;

// ==================== MODE MENU ====================
const int NUM_MODES = 6;
const char* modeNames[] = {
  "E-Reader",
  "Smart Dashboard",
  "Art Frame",
  "Web Portal",
  "Settings",
  "Sleep"
};

const char* modeDescriptions[] = {
  "Read books from SD card",
  "Weather, time, news",
  "Random art display",
  "File upload via WiFi",
  "System configuration",
  "Enter deep sleep"
};

// ==================== FUNCTION PROTOTYPES ====================
void initializeHardware();
void initializeSDCard();
void displayHomeScreen();
void handleHomeNavigation();
void launchMode(int modeIndex);
void returnToHome();

// E-Reader mode functions
void runEReaderMode();
void eReaderInit();
void eReaderLoop();
void eReaderExit();

// Other mode stubs
void runDashboardMode();
void runArtFrameMode();
void runWebPortalMode();
void runSettingsMode();
void enterDeepSleep();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=================================");
  Serial.println("E-Ink OS Booting...");
  Serial.println("=================================");
  
  initializeHardware();
  initializeSDCard();
  
  // Display boot logo
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, DISPLAY_WIDTH, DISPLAY_HEIGHT, Rotation, WHITE);
  Paint_Clear(WHITE);
  
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();
  
  Paint_Clear(WHITE);
  EPD_ShowString(300, 100, (char*)"E-INK OS", 16, BLACK);
  EPD_ShowString(280, 130, (char*)"Starting...", 16, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  delay(1000);
  
  currentMode = MODE_HOME;
  needsRedraw = true;
  
  Serial.println("Boot complete. Welcome to E-Ink OS!");
}

// ==================== MAIN LOOP ====================
void loop() {
  switch (currentMode) {
    case MODE_HOME:
      if (needsRedraw) {
        displayHomeScreen();
        needsRedraw = false;
      }
      handleHomeNavigation();
      break;
      
    case MODE_EREADER:
      runEReaderMode();
      break;
      
    case MODE_DASHBOARD:
      runDashboardMode();
      break;
      
    case MODE_ARTFRAME:
      runArtFrameMode();
      break;
      
    case MODE_WEBPORTAL:
      runWebPortalMode();
      break;
      
    case MODE_SETTINGS:
      runSettingsMode();
      break;
  }
  
  delay(50);  // Small delay to prevent busy loop
}

// ==================== HARDWARE INITIALIZATION ====================
void initializeHardware() {
  // Power pins
  pinMode(SCREEN_PWR, OUTPUT);
  digitalWrite(SCREEN_PWR, HIGH);
  
  pinMode(SD_PWR, OUTPUT);
  digitalWrite(SD_PWR, HIGH);
  
  delay(100);
  
  // Button pins
  pinMode(HOME_KEY, INPUT);
  pinMode(EXIT_KEY, INPUT);
  pinMode(PRV_KEY, INPUT);
  pinMode(NEXT_KEY, INPUT);
  pinMode(OK_KEY, INPUT);
  
  Serial.println("[HW] Hardware initialized");
}

// ==================== SD CARD INITIALIZATION ====================
void initializeSDCard() {
  Serial.println("[SD] Initializing SD card...");
  SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  
  if (!SD.begin(SD_CS, SD_SPI, 4000000)) {
    Serial.println("[SD] ERROR: Card mount failed!");
  } else {
    Serial.printf("[SD] Card mounted. Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
  }
}

// ==================== HOME SCREEN DISPLAY ====================
void displayHomeScreen() {
  Serial.println("[HOME] Rendering home screen...");
  
  Paint_Clear(WHITE);
  
  // Header
  EPD_DrawLine(0, 30, 792, 30, BLACK);
  EPD_ShowString(10, 5, (char*)"E-INK OS", 16, BLACK);
  
  // Battery indicator (placeholder)
  EPD_ShowString(700, 5, (char*)"[BAT]", 16, BLACK);
  
  // Menu items
  int startY = 50;
  int itemHeight = 35;
  
  for (int i = 0; i < NUM_MODES; i++) {
    int yPos = startY + (i * itemHeight);
    
    // Selection indicator
    if (i == selectedMenuItem) {
      EPD_DrawRectangle(5, yPos - 2, 787, yPos + 30, BLACK, 0);
      EPD_ShowString(15, yPos, (char*)"> ", 16, BLACK);
    } else {
      EPD_ShowString(15, yPos, (char*)"  ", 16, BLACK);
    }
    
    // Mode name
    EPD_ShowString(35, yPos, (char*)modeNames[i], 16, BLACK);
    
    // Mode description (smaller)
    EPD_ShowString(250, yPos + 2, (char*)modeDescriptions[i], 16, BLACK);
  }
  
  // Footer instructions
  EPD_DrawLine(0, 245, 792, 245, BLACK);
  EPD_ShowString(10, 250, (char*)"UP/DOWN: Navigate  OK: Select  HOME: Refresh", 16, BLACK);
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  Serial.println("[HOME] Home screen rendered");
}

// ==================== HOME NAVIGATION ====================
void handleHomeNavigation() {
  // Navigate down
  if (digitalRead(NEXT_KEY) == 0) {
    delay(100);
    if (digitalRead(NEXT_KEY) == 1) {
      selectedMenuItem++;
      if (selectedMenuItem >= NUM_MODES) {
        selectedMenuItem = 0;
      }
      needsRedraw = true;
      Serial.printf("[HOME] Selected: %s\n", modeNames[selectedMenuItem]);
    }
  }
  
  // Navigate up
  else if (digitalRead(PRV_KEY) == 0) {
    delay(100);
    if (digitalRead(PRV_KEY) == 1) {
      selectedMenuItem--;
      if (selectedMenuItem < 0) {
        selectedMenuItem = NUM_MODES - 1;
      }
      needsRedraw = true;
      Serial.printf("[HOME] Selected: %s\n", modeNames[selectedMenuItem]);
    }
  }
  
  // Select / Launch mode
  else if (digitalRead(OK_KEY) == 0) {
    delay(100);
    if (digitalRead(OK_KEY) == 1) {
      launchMode(selectedMenuItem);
    }
  }
  
  // Refresh home screen
  else if (digitalRead(HOME_KEY) == 0) {
    delay(100);
    if (digitalRead(HOME_KEY) == 1) {
      needsRedraw = true;
    }
  }
}

// ==================== MODE LAUNCHER ====================
void launchMode(int modeIndex) {
  Serial.printf("[OS] Launching mode: %s\n", modeNames[modeIndex]);
  
  switch (modeIndex) {
    case 0: // E-Reader
      currentMode = MODE_EREADER;
      eReaderInit();
      break;
      
    case 1: // Smart Dashboard
      currentMode = MODE_DASHBOARD;
      break;
      
    case 2: // Art Frame
      currentMode = MODE_ARTFRAME;
      break;
      
    case 3: // Web Portal
      currentMode = MODE_WEBPORTAL;
      break;
      
    case 4: // Settings
      currentMode = MODE_SETTINGS;
      break;
      
    case 5: // Sleep
      enterDeepSleep();
      break;
  }
}

// ==================== RETURN TO HOME ====================
void returnToHome() {
  Serial.println("[OS] Returning to home screen...");
  currentMode = MODE_HOME;
  needsRedraw = true;
  
  // Reinitialize display for home screen
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, DISPLAY_WIDTH, DISPLAY_HEIGHT, Rotation, WHITE);
}

// ==================== E-READER MODE ====================
// E-Reader state variables
char* eReaderFileBuffer = NULL;
size_t eReaderFileSize = 0;
size_t eReaderCurrentPos = 0;
int eReaderCurrentPage = 0;
String eReaderCurrentPath = "/books/";
std::vector<String> eReaderBookList;
int eReaderSelectedBook = 0;
bool eReaderInBrowser = true;
bool eReaderBrowserNeedsRefresh = true;

#define CHARS_PER_LINE 99
#define LINES_PER_PAGE (DISPLAY_HEIGHT / FONT_HEIGHT)

void eReaderInit() {
  Serial.println("[E-READER] Initializing E-Reader mode...");
  
  eReaderInBrowser = true;
  eReaderSelectedBook = 0;
  eReaderBrowserNeedsRefresh = true;
  eReaderCurrentPos = 0;
  eReaderCurrentPage = 0;
  
  if (eReaderFileBuffer != NULL) {
    free(eReaderFileBuffer);
    eReaderFileBuffer = NULL;
  }
  
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, DISPLAY_WIDTH, DISPLAY_HEIGHT, Rotation, WHITE);
}

void runEReaderMode() {
  if (eReaderInBrowser) {
    eReaderFileBrowser();
  } else {
    eReaderDisplayPage();
    eReaderHandleNavigation();
  }
}

void eReaderFileBrowser() {
  if (eReaderBrowserNeedsRefresh) {
    Paint_Clear(WHITE);
    EPD_ShowString(0, 0, (char*)"E-READER - SELECT BOOK", 16, BLACK);
    EPD_ShowString(0, 20, (char*)"---", 16, BLACK);
    
    File dir = SD.open(eReaderCurrentPath);
    if (!dir) {
      EPD_ShowString(0, 40, (char*)"No /books/ folder", 16, BLACK);
      EPD_ShowString(0, 60, (char*)"Press EXIT to return", 16, BLACK);
      EPD_Display(ImageBW);
      EPD_PartUpdate();
      eReaderBrowserNeedsRefresh = false;
    } else {
      eReaderBookList.clear();
      int lineY = 40;
      int displayCount = 0;
      
      File file = dir.openNextFile();
      while (file && displayCount < LINES_PER_PAGE - 3) {
        if (!file.isDirectory()) {
          String filename = file.name();
          eReaderBookList.push_back(filename);
          
          if (displayCount == eReaderSelectedBook) {
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
      
      EPD_ShowString(0, DISPLAY_HEIGHT - 16, (char*)"EXIT: Back to Home", 16, BLACK);
      EPD_Display(ImageBW);
      EPD_PartUpdate();
      eReaderBrowserNeedsRefresh = false;
    }
  }
  
  // Browser navigation
  if (digitalRead(NEXT_KEY) == 0) {
    delay(100);
    if (digitalRead(NEXT_KEY) == 1) {
      if (eReaderSelectedBook < (int)eReaderBookList.size() - 1) {
        eReaderSelectedBook++;
        eReaderBrowserNeedsRefresh = true;
      }
    }
  } else if (digitalRead(PRV_KEY) == 0) {
    delay(100);
    if (digitalRead(PRV_KEY) == 1) {
      if (eReaderSelectedBook > 0) {
        eReaderSelectedBook--;
        eReaderBrowserNeedsRefresh = true;
      }
    }
  } else if (digitalRead(OK_KEY) == 0) {
    delay(100);
    if (digitalRead(OK_KEY) == 1) {
      if (eReaderSelectedBook < (int)eReaderBookList.size()) {
        eReaderLoadBook(eReaderBookList[eReaderSelectedBook]);
        eReaderInBrowser = false;
      }
    }
  } else if (digitalRead(EXIT_KEY) == 0) {
    delay(100);
    if (digitalRead(EXIT_KEY) == 1) {
      eReaderExit();
      returnToHome();
    }
  }
}

void eReaderLoadBook(String filename) {
  Serial.printf("[E-READER] Loading: %s\n", filename.c_str());
  
  if (eReaderFileBuffer != NULL) {
    free(eReaderFileBuffer);
    eReaderFileBuffer = NULL;
  }
  
  String filepath = eReaderCurrentPath + filename;
  File file = SD.open(filepath);
  
  if (!file) {
    Serial.println("[E-READER] Failed to open file");
    return;
  }
  
  eReaderFileSize = file.size();
  eReaderFileBuffer = (char*)malloc(eReaderFileSize + 1);
  
  if (eReaderFileBuffer == NULL) {
    Serial.println("[E-READER] Not enough memory");
    file.close();
    return;
  }
  
  file.read((uint8_t*)eReaderFileBuffer, eReaderFileSize);
  eReaderFileBuffer[eReaderFileSize] = '\0';
  file.close();
  
  eReaderCurrentPos = 0;
  eReaderCurrentPage = 0;
  
  Serial.printf("[E-READER] Book loaded: %d bytes\n", eReaderFileSize);
}

void eReaderDisplayPage() {
  Paint_Clear(WHITE);
  
  if (eReaderFileBuffer == NULL || eReaderFileSize == 0) {
    EPD_ShowString(0, 0, (char*)"No file loaded", 16, BLACK);
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    return;
  }
  
  // Display header
  EPD_ShowString(0, 0, (char*)"E-READER", 16, BLACK);
  EPD_DrawLine(0, 18, 792, 18, BLACK);
  
  // Display text
  int lineY = 25;
  int lineCount = 0;
  size_t pos = eReaderCurrentPos;
  
  while (lineCount < LINES_PER_PAGE - 3 && pos < eReaderFileSize) {
    char lineBuffer[100];
    int charCount = 0;
    
    while (charCount < CHARS_PER_LINE && pos < eReaderFileSize && eReaderFileBuffer[pos] != '\n') {
      lineBuffer[charCount] = eReaderFileBuffer[pos];
      charCount++;
      pos++;
    }
    
    if (pos < eReaderFileSize && eReaderFileBuffer[pos] == '\n') {
      pos++;
    }
    
    lineBuffer[charCount] = '\0';
    
    if (charCount > 0) {
      EPD_ShowString(0, lineY, lineBuffer, 16, BLACK);
    }
    
    lineY += FONT_HEIGHT;
    lineCount++;
  }
  
  // Footer
  char footer[100];
  sprintf(footer, "Page %d | Pos: %d/%d | EXIT: Browser", 
          eReaderCurrentPage, (int)eReaderCurrentPos, (int)eReaderFileSize);
  EPD_ShowString(0, DISPLAY_HEIGHT - 16, footer, 16, BLACK);
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

void eReaderHandleNavigation() {
  if (digitalRead(NEXT_KEY) == 0) {
    delay(100);
    if (digitalRead(NEXT_KEY) == 1) {
      // Page down
      size_t pageJump = (LINES_PER_PAGE - 3) * CHARS_PER_LINE;
      eReaderCurrentPos += pageJump;
      if (eReaderCurrentPos > eReaderFileSize) {
        eReaderCurrentPos = eReaderFileSize;
      }
      eReaderCurrentPage++;
    }
  } else if (digitalRead(PRV_KEY) == 0) {
    delay(100);
    if (digitalRead(PRV_KEY) == 1) {
      // Page up
      size_t pageJump = (LINES_PER_PAGE - 3) * CHARS_PER_LINE;
      if (eReaderCurrentPos > pageJump) {
        eReaderCurrentPos -= pageJump;
      } else {
        eReaderCurrentPos = 0;
      }
      if (eReaderCurrentPage > 0) eReaderCurrentPage--;
    }
  } else if (digitalRead(EXIT_KEY) == 0) {
    delay(100);
    if (digitalRead(EXIT_KEY) == 1) {
      eReaderInBrowser = true;
      eReaderBrowserNeedsRefresh = true;
    }
  } else if (digitalRead(HOME_KEY) == 0) {
    delay(100);
    if (digitalRead(HOME_KEY) == 1) {
      // Return to home OS
      eReaderExit();
      returnToHome();
    }
  }
}

void eReaderExit() {
  Serial.println("[E-READER] Exiting E-Reader mode");
  if (eReaderFileBuffer != NULL) {
    free(eReaderFileBuffer);
    eReaderFileBuffer = NULL;
  }
}

// ==================== DASHBOARD MODE ====================
void runDashboardMode() {
  Paint_Clear(WHITE);
  EPD_ShowString(100, 100, (char*)"SMART DASHBOARD", 16, BLACK);
  EPD_ShowString(100, 130, (char*)"Coming Soon!", 16, BLACK);
  EPD_ShowString(100, 160, (char*)"Press EXIT to return", 16, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  while (true) {
    if (digitalRead(EXIT_KEY) == 0) {
      delay(100);
      if (digitalRead(EXIT_KEY) == 1) {
        returnToHome();
        return;
      }
    }
    delay(50);
  }
}

// ==================== ART FRAME MODE ====================
void runArtFrameMode() {
  Paint_Clear(WHITE);
  EPD_ShowString(100, 100, (char*)"ART FRAME", 16, BLACK);
  EPD_ShowString(100, 130, (char*)"Coming Soon!", 16, BLACK);
  EPD_ShowString(100, 160, (char*)"Press EXIT to return", 16, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  while (true) {
    if (digitalRead(EXIT_KEY) == 0) {
      delay(100);
      if (digitalRead(EXIT_KEY) == 1) {
        returnToHome();
        return;
      }
    }
    delay(50);
  }
}

// ==================== WEB PORTAL MODE ====================
void runWebPortalMode() {
  Paint_Clear(WHITE);
  EPD_ShowString(100, 100, (char*)"WEB PORTAL", 16, BLACK);
  EPD_ShowString(100, 130, (char*)"Coming Soon!", 16, BLACK);
  EPD_ShowString(100, 160, (char*)"Press EXIT to return", 16, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  while (true) {
    if (digitalRead(EXIT_KEY) == 0) {
      delay(100);
      if (digitalRead(EXIT_KEY) == 1) {
        returnToHome();
        return;
      }
    }
    delay(50);
  }
}

// ==================== SETTINGS MODE ====================
void runSettingsMode() {
  Paint_Clear(WHITE);
  EPD_ShowString(100, 100, (char*)"SETTINGS", 16, BLACK);
  EPD_ShowString(100, 130, (char*)"Coming Soon!", 16, BLACK);
  EPD_ShowString(100, 160, (char*)"Press EXIT to return", 16, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  while (true) {
    if (digitalRead(EXIT_KEY) == 0) {
      delay(100);
      if (digitalRead(EXIT_KEY) == 1) {
        returnToHome();
        return;
      }
    }
    delay(50);
  }
}

// ==================== DEEP SLEEP ====================
void enterDeepSleep() {
  Paint_Clear(WHITE);
  EPD_ShowString(250, 120, (char*)"Entering Sleep Mode...", 16, BLACK);
  EPD_ShowString(200, 140, (char*)"Press any button to wake", 16, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  EPD_DeepSleep();
  
  delay(2000);
  
  // Configure wake-up sources
  esp_sleep_enable_ext0_wakeup((gpio_num_t)HOME_KEY, 0);  // Wake on HOME button press
  esp_sleep_enable_ext1_wakeup(
    (1ULL << HOME_KEY) | (1ULL << EXIT_KEY) | (1ULL << OK_KEY),
    ESP_EXT1_WAKEUP_ANY_HIGH
  );
  
  Serial.println("[OS] Entering deep sleep...");
  esp_deep_sleep_start();
}
