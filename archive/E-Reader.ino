#include <Arduino.h>
#include <SPI.h>      // must come BEFORE SD.h
#include <SD.h>       // ESP32’s SD library
#include "EPD.h"


// Pre-allocated black and white image array for the display
uint8_t ImageBW[27200];


// --- PIN DEFINITIONS ---
// Updated with Elecrow 5.79" exact GPIO pins
#define PIN_DIAL_UP    6  // PRV_KEY
#define PIN_DIAL_DOWN  4  // NEXT_KEY
#define PIN_DIAL_PUSH  5  // OK_KEY
#define PIN_BTN_MENU   2  // HOME_KEY
#define PIN_BTN_BACK   1  // EXIT_KEY
#define PIN_SCREEN_PWR 7  // Screen power control


// SD Card SPI Pins
#define SD_MOSI 40
#define SD_MISO 13
#define SD_SCK  39
#define SD_CS   10
#define PIN_SD_PWR 42
#define PIN_PWR_LED 41 // Power LED control


// SPI Instance for SD Card
SPIClass SD_SPI = SPIClass(HSPI);


// --- SYSTEM STATES (The "Apps") ---
enum AppState {
  STATE_HOME_MENU,
  STATE_E_READER,
  STATE_DASHBOARD,
  STATE_ART_MODE,
  STATE_WEB_PORTAL
};


// Global variables to track the system's current status
AppState currentState = STATE_HOME_MENU;
int menuSelection = 0;
const int NUM_APPS = 4;
String apps[NUM_APPS] = {"1. E-Reader", "2. Dashboard", "3. Art Mode", "4. Web Portal"};


// Global tracking for E-Ink ghosting management
int pagesSinceFullRefresh = 0;


// --- SETUP (Runs once on boot) ---
void setup() {
  Serial.begin(115200);
 
  // 0. Turn on screen power
  pinMode(PIN_SCREEN_PWR, OUTPUT);
  digitalWrite(PIN_SCREEN_PWR, HIGH);
 
  // Turn on SD Card power
  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, HIGH);
  delay(10); // Brief delay for power stability
 
  // Initialize Power LED
  pinMode(PIN_PWR_LED, OUTPUT);
  digitalWrite(PIN_PWR_LED, HIGH); // Turn ON during boot to show life
 
  // 1. Initialize Buttons (Elecrow uses standard INPUT based on their example)
  pinMode(PIN_DIAL_UP, INPUT);
  pinMode(PIN_DIAL_DOWN, INPUT);
  pinMode(PIN_DIAL_PUSH, INPUT);
  pinMode(PIN_BTN_MENU, INPUT);
  pinMode(PIN_BTN_BACK, INPUT);


  // 2. Initialize E-Ink Display
  Serial.println("Initializing E-Ink Display...");
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  Paint_Clear(WHITE);
 
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();
  EPD_Clear_R26A6H();
 
  // 3. Initialize SD Card
  SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI); // Initialize SPI with specific pins
 
  // Mount the SD card with the custom SPI instance and 80MHz clock speed
  if (!SD.begin(SD_CS, SD_SPI, 80000000)) {
    Serial.println("SD Card mount failed! Check connections.");
  } else {
    Serial.printf("SD Card mounted successfully. Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
  }


  // 4. Draw the initial Home Menu
  drawHomeMenu();
}


// --- DISPLAY ABSTRACTION ---
// Handles the hardware quirks of partial vs full screen refreshes
void performDisplayUpdate(bool fullRefresh) {
  digitalWrite(PIN_PWR_LED, HIGH); // Turn LED ON while processing/updating
 
  if (fullRefresh) {
    // FULL REFRESH: Flashes the screen black/white to clear all ghosting
    Serial.println("Performing Full Display Refresh...");
    EPD_GPIOInit();
    EPD_FastMode1Init();
    EPD_Display_Clear();
    EPD_Update();
   
    EPD_GPIOInit();
    EPD_FastMode1Init();
    EPD_Display(ImageBW);
    EPD_FastUpdate();
  } else {
    // PARTIAL REFRESH: Fast update, no flashing, but accumulates ghosting over time
    Serial.println("Performing Partial Display Refresh...");
    EPD_HW_RESET();
    EPD_Display(ImageBW);
    EPD_PartUpdate();
  }
 
  // Always go back to sleep to save battery!
  EPD_DeepSleep();
 
  digitalWrite(PIN_PWR_LED, LOW); // Turn LED OFF when idle to save battery
}


// --- HARDWARE UTILITIES ---


int getBatteryPercentage() {
  // TODO: Once you check the Elecrow wiki for the Battery ADC Pin,
  // we will do an analogRead() here and map the voltage (3.2V - 4.2V) to 0-100%.
  // For now, returning a dummy value so we can see it on the screen!
  return 90;
}


// --- UI COMPONENTS ---


void drawStatusBar() {
  // 1. Time & Date (Placeholder until we add Wi-Fi NTP sync in Dashboard mode)
  char timeStr[] = "10:25  08.23.2024";
 
  // 2. Battery Percentage
  int batPercent = getBatteryPercentage();
  char batStr[20];
  sprintf(batStr, "BAT: %d%%", batPercent);
 
  // 3. Draw them to the screen buffer
  // EPD_W is usually 792 for this screen. Center is ~396.
  EPD_ShowString(300, 5, timeStr, 16, BLACK); // Center-ish
  EPD_ShowString(680, 5, batStr, 16, BLACK);  // Right side
 
  // 4. Draw a separator line (using underscores to be safe across all library versions)
  EPD_ShowString(0, 20, (char*)"________________________________________________________________________________", 16, BLACK);
}


// --- MAIN LOOP ---
void loop() {
  // Helper lambda for Elecrow-style button debounce (waits for release)
  auto readButton = [](int pin) -> bool {
    if (digitalRead(pin) == 0) {
      delay(100); // Anti-shake
      if (digitalRead(pin) == 1) { // Released
        return true;
      }
    }
    return false;
  };


  // Read physical button states
  bool dialUp = readButton(PIN_DIAL_UP);
  bool dialDown = readButton(PIN_DIAL_DOWN);
  bool dialPush = readButton(PIN_DIAL_PUSH);
  bool btnMenu = readButton(PIN_BTN_MENU);
  bool btnBack = readButton(PIN_BTN_BACK);


  // GLOBAL OVERRIDE: Pressing the Menu button always forces you back to the Home Screen
  if (btnMenu) {
    currentState = STATE_HOME_MENU;
    drawHomeMenu();
    delay(400); // Simple debounce
    return;
  }


  // Route the logic based on which "App" is currently running
  switch (currentState) {
    case STATE_HOME_MENU:
      handleHomeMenu(dialUp, dialDown, dialPush);
      break;
    case STATE_E_READER:
      handleEReader(dialUp, dialDown, dialPush, btnBack);
      break;
    case STATE_DASHBOARD:
      // Dashboard logic will go here
      break;
    case STATE_ART_MODE:
      // Art mode logic will go here
      break;
    case STATE_WEB_PORTAL:
      // Web Server file upload logic will go here
      break;
  }
 
  delay(50); // Small delay for loop stability
}


// ==========================================
// APP: HOME MENU LAUNCHER
// ==========================================
void drawHomeMenu() {
  Serial.println("\n=== HOME MENU ===");
 
  // Clear E-Ink Image Buffer
  Paint_Clear(WHITE);
  EPD_FastMode1Init(); // Prepare for fast update


  // Draw the Global Status Bar
  drawStatusBar();


  // Draw Title (Shifted Y down to 40 to make room for status bar)
  EPD_ShowString(10, 40, (char*)"=== HOME MENU ===", 24, BLACK);


  for (int i = 0; i < NUM_APPS; i++) {
    char buffer[30];
    if (i == menuSelection) {
      Serial.print("-> "); // Highlights the current selection
      sprintf(buffer, "-> %s", apps[i].c_str());
    } else {
      Serial.print("   ");
      sprintf(buffer, "   %s", apps[i].c_str());
    }
    Serial.println(apps[i]);
   
    // Draw each menu item to E-Ink Buffer (Shifted Y down to 80)
    EPD_ShowString(10, 80 + (i * 40), buffer, 16, BLACK);
  }
 
  // Always use a Full Refresh for menus to ensure crisp text
  pagesSinceFullRefresh = 0;
  performDisplayUpdate(true);
}


void handleHomeMenu(bool up, bool down, bool push) {
  if (up) {
    menuSelection--;
    if (menuSelection < 0) menuSelection = NUM_APPS - 1; // Wrap around
    drawHomeMenu();
    delay(300); // Debounce
  }
 
  if (down) {
    menuSelection++;
    if (menuSelection >= NUM_APPS) menuSelection = 0; // Wrap around
    drawHomeMenu();
    delay(300); // Debounce
  }
 
  if (push) {
    Serial.print("Launching: ");
    Serial.println(apps[menuSelection]);
   
    // Switch the state machine to the selected app
    if (menuSelection == 0) {
      currentState = STATE_E_READER;
      openBook("/books/sample.txt");
    }
    else if (menuSelection == 1) currentState = STATE_DASHBOARD;
    else if (menuSelection == 2) currentState = STATE_ART_MODE;
    else if (menuSelection == 3) currentState = STATE_WEB_PORTAL;
   
    delay(400); // Debounce
  }
}


// ==========================================
// APP: E-READER
// ==========================================
int currentPage = 0;
String currentBookFile = "";


void openBook(String filename) {
  Serial.println("\nOpening book: " + filename);
  currentBookFile = filename;
  currentPage = 1;
  renderPage(currentPage);
}


void renderPage(int page) {
  Serial.print("Rendering Page ");
  Serial.println(page);
 
  // Clear buffer in memory
  Paint_Clear(WHITE);
 
  // Draw the Global Status Bar
  drawStatusBar();
 
  char titleBuffer[30];
  sprintf(titleBuffer, "E-Reader: Page %d", page);
  // Shifted Y down to 40 to make room for status bar
  EPD_ShowString(10, 40, titleBuffer, 24, BLACK);
 
  // Read a chunk of text from the SD card
  if (currentBookFile == "") {
    EPD_ShowString(10, 80, (char*)"No book selected.", 16, BLACK);
  } else {
    File file = SD.open(currentBookFile);
    if (!file) {
      EPD_ShowString(10, 80, (char*)"Error: Cannot open file!", 16, BLACK);
      EPD_ShowString(10, 100, (char*)currentBookFile.c_str(), 16, BLACK);
    } else {
      int linesPerPage = 10; // Adjust this later based on font size and screen length
      int currentLine = 0;
      int targetStartLine = (page - 1) * linesPerPage;
     
      // Fast-forward to the start of the correct page
      while (file.available() && currentLine < targetStartLine) {
        file.readStringUntil('\n');
        currentLine++;
      }


      // Read and draw the lines for the current page
      int yOffset = 80; // Shifted starting Y position down to 80
      int linesDrawn = 0;
     
      while (file.available() && linesDrawn < linesPerPage) {
        String line = file.readStringUntil('\n');
        line.trim(); // Remove trailing invisible characters like \r
       
        // Copy to character buffer (limit length so it doesn't crash drawing off-screen)
        char lineBuf[80];
        strncpy(lineBuf, line.c_str(), sizeof(lineBuf) - 1);
        lineBuf[sizeof(lineBuf) - 1] = '\0';
       
        // Draw line to the image buffer
        EPD_ShowString(10, yOffset, lineBuf, 16, BLACK);
        yOffset += 20; // Move cursor down: 16px font + 4px vertical spacing
        linesDrawn++;
      }
     
      if (linesDrawn == 0 && page > 1) {
         EPD_ShowString(10, yOffset, (char*)"--- End of Book ---", 16, BLACK);
      }
     
      file.close();
    }
  }
 
  // Determine if we need to flash the screen to clear ghosting
  bool doFullRefresh = (pagesSinceFullRefresh >= 5);
 
  if (doFullRefresh) {
    pagesSinceFullRefresh = 0; // Reset counter
  } else {
    pagesSinceFullRefresh++;
  }
 
  // Push to physical screen
  performDisplayUpdate(doFullRefresh);
}


void handleEReader(bool up, bool down, bool push, bool back) {
  if (down) { // Scroll dial down = next page
    currentPage++;
    renderPage(currentPage);
    delay(500); // E-ink takes time to refresh, longer debounce needed
  }
 
  if (up) { // Scroll dial up = previous page
    if (currentPage > 1) {
      currentPage--;
      renderPage(currentPage);
      delay(500);
    }
  }
 
  if (back) { // Exit book
    currentState = STATE_HOME_MENU;
    drawHomeMenu();
    delay(400);
  }
}

