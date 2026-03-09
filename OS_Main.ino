#include <Arduino.h>
#include "EPD.h"
#include "SD.h"
#include <WiFi.h>
#include <WebServer.h>
#include "WebPortal.h"
#include "ButtonHandler.h"
#include "EReader.h"
#include "ArtFrame.h"
#include "Settings.h"

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

// ==================== BUTTON MANAGER ====================
ButtonManager* buttons = nullptr;

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
  
  // Initialize persistent storage and settings
  initializePersistentStorage();
  loadSettings();
  
  currentMode = MODE_HOME;
  needsRedraw = true;
  
  Serial.println("Boot complete. Welcome to E-Ink OS!");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  Serial.println("=========================================\n");
}

// ==================== MAIN LOOP ====================
void loop() {
  // Update button states every loop
  buttons->update();
  
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
  
  delay(10);  // Small delay for responsiveness
}

// ==================== HARDWARE INITIALIZATION ====================
void initializeHardware() {
  // Power pins
  pinMode(SCREEN_PWR, OUTPUT);
  digitalWrite(SCREEN_PWR, HIGH);
  
  pinMode(SD_PWR, OUTPUT);
  digitalWrite(SD_PWR, HIGH);
  
  delay(100);
  
  // Initialize button handler
  buttons = new ButtonManager(HOME_KEY, EXIT_KEY, PRV_KEY, NEXT_KEY, OK_KEY);
  buttons->begin();
  
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
  if (buttons->next()->wasPressed()) {
    selectedMenuItem++;
    if (selectedMenuItem >= NUM_MODES) {
      selectedMenuItem = 0;
    }
    needsRedraw = true;
    Serial.printf("[HOME] Selected: %s\n", modeNames[selectedMenuItem]);
  }
  
  // Navigate up
  else if (buttons->prv()->wasPressed()) {
    selectedMenuItem--;
    if (selectedMenuItem < 0) {
      selectedMenuItem = NUM_MODES - 1;
    }
    needsRedraw = true;
    Serial.printf("[HOME] Selected: %s\n", modeNames[selectedMenuItem]);
  }
  
  // Select / Launch mode
  else if (buttons->ok()->wasPressed()) {
    launchMode(selectedMenuItem);
  }
  
  // Refresh home screen
  else if (buttons->home()->wasPressed()) {
    needsRedraw = true;
  }
}

// ==================== MODE LAUNCHER ====================
void launchMode(int modeIndex) {
  Serial.printf("[OS] Launching mode: %s\n", modeNames[modeIndex]);  Serial.printf("[OS] Free heap before launch: %d bytes\\n", ESP.getFreeHeap());
  Serial.flush();  
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
      artFrameInit();
      break;
      
    case 3: // Web Portal
      currentMode = MODE_WEBPORTAL;
      break;
      
    case 4: // Settings
      currentMode = MODE_SETTINGS;
      settingsInit();
      break;
      
    case 5: // Sleep
      enterDeepSleep();
      break;
  }
}

// ==================== RETURN TO HOME ====================
void returnToHome() {
  Serial.println("[OS] ========================================");
  Serial.println("[OS] Returning to home screen...");
  Serial.printf("[OS] Free heap: %d bytes\\n", ESP.getFreeHeap());
  Serial.flush();
  delay(100);
  
  currentMode = MODE_HOME;
  needsRedraw = true;
  
  // Reinitialize display for home screen
  Serial.println("[OS] Reinitializing display...");
  Serial.flush();
  
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, DISPLAY_WIDTH, DISPLAY_HEIGHT, Rotation, WHITE);
  
  Serial.println("[OS] Display reinitialized");
  Serial.println("[OS] ========================================");
  Serial.flush();
}

// ==================== E-READER MODE ====================
void runEReaderMode() {
  // Update E-Reader display if needed
  eReaderUpdate();
  
  // Small delay to prevent overwhelming the system
  delay(20);
  
  // Handle input based on current state
  if (eReaderIsInBrowser()) {
    // Browser navigation
    bool prvPressed = buttons->prv()->wasPressed();
    bool nextPressed = buttons->next()->wasPressed();
    bool okPressed = buttons->ok()->wasPressed();
    bool exitPressed = buttons->exit()->wasPressed();
    
    if (prvPressed || nextPressed || okPressed || exitPressed) {
      Serial.printf("[OS] Browser input: PRV=%d, NEXT=%d, OK=%d, EXIT=%d\n", 
                    prvPressed, nextPressed, okPressed, exitPressed);
      Serial.flush();
    }
    
    eReaderHandleBrowserInput(prvPressed, nextPressed, okPressed, exitPressed);
    
    // Exit to home
    if (exitPressed) {
      Serial.println("[OS] Exiting E-Reader to home...");
      Serial.flush();
      eReaderCleanup();
      returnToHome();
    }
  } else {
    // Reading navigation
    bool prvPressed = buttons->prv()->wasPressed();
    bool nextPressed = buttons->next()->wasPressed();
    bool exitPressed = buttons->exit()->wasPressed();
    bool homePressed = buttons->home()->wasPressed();
    
    if (prvPressed || nextPressed || exitPressed || homePressed) {
      Serial.printf("[OS] Reader input: PRV=%d, NEXT=%d, EXIT=%d, HOME=%d\n", 
                    prvPressed, nextPressed, exitPressed, homePressed);
      Serial.flush();
    }
    
    eReaderHandleReaderInput(prvPressed, nextPressed, exitPressed, homePressed);
    
    // Exit to home
    if (homePressed) {
      Serial.println("[OS] Returning to home from reader...");
      Serial.flush();
      eReaderCleanup();
      returnToHome();
    }
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
    buttons->update();
    if (buttons->exit()->wasPressed() || buttons->home()->wasPressed()) {
      returnToHome();
      return;
    }
    delay(10);
  }
}

// ==================== ART FRAME MODE ====================
void runArtFrameMode() {
  // Update art frame display and cycle logic
  artFrameUpdate();
  
  // Small delay to prevent overwhelming the system
  delay(20);
  
  // Handle input
  bool prvPressed = buttons->prv()->wasPressed();
  bool nextPressed = buttons->next()->wasPressed();
  bool okPressed = buttons->ok()->wasPressed();
  bool exitPressed = buttons->exit()->wasPressed();
  bool homePressed = buttons->home()->wasPressed();
  
  if (prvPressed || nextPressed || okPressed || exitPressed || homePressed) {
    Serial.printf("[OS] ArtFrame input: PRV=%d, NEXT=%d, OK=%d, EXIT=%d, HOME=%d\n", 
                  prvPressed, nextPressed, okPressed, exitPressed, homePressed);
    Serial.flush();
  }
  
  artFrameHandleInput(prvPressed, nextPressed, okPressed, exitPressed);
  
  // Exit to home
  if (exitPressed || homePressed) {
    Serial.println("[OS] Exiting Art Frame to home...");
    Serial.flush();
    artFrameCleanup();
    returnToHome();
  }
}

// ==================== WEB PORTAL MODE ====================
void runWebPortalMode() {
  Serial.println("[PORTAL] Starting Web Portal Mode");
  
  // Initialize display
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, DISPLAY_WIDTH, DISPLAY_HEIGHT, Rotation, WHITE);
  
  // Initialize web portal
  if (!webPortalInit()) {
    Serial.println("[PORTAL] Failed to initialize");
    delay(3000);
    returnToHome();
    return;
  }
  
  Serial.println("[PORTAL] Web Portal running - waiting for uploads");
  
  // Main portal loop
  while (true) {
    // Update buttons
    buttons->update();
    
    // Handle web server requests
    webPortalUpdate();
    
    // Check for EXIT or HOME button
    if (buttons->exit()->wasPressed() || buttons->home()->wasPressed()) {
      Serial.println("[PORTAL] Exit button pressed, stopping portal");
      webPortalStop();
      returnToHome();
      return;
    }
    
    delay(10);  // Small delay for server handling
  }
}

// ==================== SETTINGS MODE ====================
void runSettingsMode() {
  // Update settings display if needed
  settingsUpdate();
  
  // Small delay to prevent overwhelming the system
  delay(20);
  
  // Handle input
  bool upPressed = buttons->prv()->wasPressed();
  bool downPressed = buttons->next()->wasPressed();
  bool okPressed = buttons->ok()->wasPressed();
  bool exitPressed = buttons->exit()->wasPressed();
  bool homePressed = buttons->home()->wasPressed();
  
  // Check if user wants to exit settings (from main menu only)
  if ((exitPressed || homePressed) && !upPressed && !downPressed && !okPressed) {
    Serial.println("[OS] Exiting Settings to home...");
    Serial.flush();
    settingsCleanup();
    returnToHome();
  } else {
    settingsHandleInput(upPressed, downPressed, okPressed, exitPressed);
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
