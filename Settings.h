#ifndef SETTINGS_H
#define SETTINGS_H

#include "EPD.h"
#include "SD.h"
#include "PersistentStorage.h"
#include <vector>

// ==================== SETTINGS CONFIGURATION ====================
#define SETTINGS_FILE "/system_settings.txt"
#define MAX_WIFI_SSID_LENGTH 32
#define MAX_WIFI_PASSWORD_LENGTH 64

// Reference to global display buffer from OS_Main.ino
extern uint8_t ImageBW[27200];

// ==================== SETTINGS DATA STRUCTURE ====================
namespace SettingsNS {
  // Configurable settings
  struct SystemSettings {
    // Power management
    int autoSleepMinutes;           // Minutes of inactivity before deep sleep (0 = disabled)
    
    // Art Frame settings
    int artCycleSeconds;            // Seconds between art changes
    bool artAutoCycleEnabled;       // Enable/disable auto-cycling
    
    // E-Reader settings
    int progressSaveFrequency;      // Save progress every N scrolls
    int fontSize;                   // Font size (16, 24, 32)
    
    // Display settings
    int rotation;                   // Screen rotation (0, 90, 180, 270)
    
    // WiFi settings (for AP mode)
    char wifiSSID[MAX_WIFI_SSID_LENGTH];
    char wifiPassword[MAX_WIFI_PASSWORD_LENGTH];
    bool wifiEnabled;
    
    // UI settings
    bool showBatteryPercent;        // Show battery % on home screen
    bool enableSounds;              // Enable button feedback sounds (if hardware supports)
  };
  
  // Default settings
  static SystemSettings currentSettings = {
    30,              // autoSleepMinutes
    30,              // artCycleSeconds
    true,            // artAutoCycleEnabled
    5,               // progressSaveFrequency
    16,              // fontSize
    0,               // rotation
    "E-Reader-Portal", // wifiSSID
    "ereader123",    // wifiPassword
    true,            // wifiEnabled
    true,            // showBatteryPercent
    false            // enableSounds
  };
  
  static bool settingsLoaded = false;
  
  // UI State
  enum SettingsMode {
    SETTINGS_MAIN_MENU,
    SETTINGS_MANAGE_BOOKS,
    SETTINGS_MANAGE_ART,
    SETTINGS_DISPLAY_OPTIONS,
    SETTINGS_POWER_OPTIONS,
    SETTINGS_WIFI_OPTIONS,
    SETTINGS_EREADER_OPTIONS,
    SETTINGS_ARTFRAME_OPTIONS
  };
  
  static SettingsMode currentMode = SETTINGS_MAIN_MENU;
  static int selectedItem = 0;
  static int scrollOffset = 0;
  static bool needsRefresh = true;
  
  // File management
  static std::vector<String> fileList;
  static int selectedFile = 0;
  static int fileScrollOffset = 0;
  static String currentPath = "";
  static bool confirmDelete = false;
  static String fileToDelete = "";
}

// ==================== SETTINGS PERSISTENCE ====================

// Parse a settings line
bool parseSettingsLine(const char* line) {
  using namespace SettingsNS;
  
  if (line == NULL || strlen(line) < 3) return false;
  
  char key[64];
  char value[128];
  
  // Simple key=value parser
  const char* equals = strchr(line, '=');
  if (equals == NULL) return false;
  
  int keyLen = equals - line;
  if (keyLen >= sizeof(key)) keyLen = sizeof(key) - 1;
  strncpy(key, line, keyLen);
  key[keyLen] = '\0';
  
  strncpy(value, equals + 1, sizeof(value) - 1);
  value[sizeof(value) - 1] = '\0';
  
  // Trim whitespace
  while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') value++;
  int vLen = strlen(value);
  while (vLen > 0 && (value[vLen-1] == ' ' || value[vLen-1] == '\t' || value[vLen-1] == '\r' || value[vLen-1] == '\n')) {
    value[--vLen] = '\0';
  }
  
  // Match settings
  if (strcmp(key, "autoSleepMinutes") == 0) currentSettings.autoSleepMinutes = atoi(value);
  else if (strcmp(key, "artCycleSeconds") == 0) currentSettings.artCycleSeconds = atoi(value);
  else if (strcmp(key, "artAutoCycleEnabled") == 0) currentSettings.artAutoCycleEnabled = (atoi(value) != 0);
  else if (strcmp(key, "progressSaveFrequency") == 0) currentSettings.progressSaveFrequency = atoi(value);
  else if (strcmp(key, "fontSize") == 0) currentSettings.fontSize = atoi(value);
  else if (strcmp(key, "rotation") == 0) currentSettings.rotation = atoi(value);
  else if (strcmp(key, "wifiSSID") == 0) strncpy(currentSettings.wifiSSID, value, MAX_WIFI_SSID_LENGTH - 1);
  else if (strcmp(key, "wifiPassword") == 0) strncpy(currentSettings.wifiPassword, value, MAX_WIFI_PASSWORD_LENGTH - 1);
  else if (strcmp(key, "wifiEnabled") == 0) currentSettings.wifiEnabled = (atoi(value) != 0);
  else if (strcmp(key, "showBatteryPercent") == 0) currentSettings.showBatteryPercent = (atoi(value) != 0);
  else if (strcmp(key, "enableSounds") == 0) currentSettings.enableSounds = (atoi(value) != 0);
  
  return true;
}

// Load settings from SD card
bool loadSettings() {
  using namespace SettingsNS;
  
  Serial.println("[SETTINGS] Loading system settings...");
  
  if (!SD.exists(SETTINGS_FILE)) {
    Serial.println("[SETTINGS] No settings file found, using defaults");
    settingsLoaded = true;
    return saveSettings(); // Create with defaults
  }
  
  File file = SD.open(SETTINGS_FILE, FILE_READ);
  if (!file) {
    Serial.println("[SETTINGS] ERROR: Failed to open settings file");
    return false;
  }
  
  char line[256];
  while (file.available()) {
    int bytesRead = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[bytesRead] = '\0';
    
    if (bytesRead > 3) {
      parseSettingsLine(line);
    }
  }
  
  file.close();
  
  Serial.println("[SETTINGS] Settings loaded successfully");
  settingsLoaded = true;
  return true;
}

// Save settings to SD card
bool saveSettings() {
  using namespace SettingsNS;
  
  Serial.println("[SETTINGS] Saving system settings...");
  
  if (SD.exists(SETTINGS_FILE)) {
    SD.remove(SETTINGS_FILE);
  }
  
  File file = SD.open(SETTINGS_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("[SETTINGS] ERROR: Failed to open settings file for writing");
    return false;
  }
  
  // Write settings in key=value format
  file.printf("autoSleepMinutes=%d\n", currentSettings.autoSleepMinutes);
  file.printf("artCycleSeconds=%d\n", currentSettings.artCycleSeconds);
  file.printf("artAutoCycleEnabled=%d\n", currentSettings.artAutoCycleEnabled ? 1 : 0);
  file.printf("progressSaveFrequency=%d\n", currentSettings.progressSaveFrequency);
  file.printf("fontSize=%d\n", currentSettings.fontSize);
  file.printf("rotation=%d\n", currentSettings.rotation);
  file.printf("wifiSSID=%s\n", currentSettings.wifiSSID);
  file.printf("wifiPassword=%s\n", currentSettings.wifiPassword);
  file.printf("wifiEnabled=%d\n", currentSettings.wifiEnabled ? 1 : 0);
  file.printf("showBatteryPercent=%d\n", currentSettings.showBatteryPercent ? 1 : 0);
  file.printf("enableSounds=%d\n", currentSettings.enableSounds ? 1 : 0);
  
  file.close();
  
  Serial.printf("[SETTINGS] Settings saved (%d bytes)\n", file.size());
  return true;
}

// ==================== FILE MANAGEMENT ====================

// Scan directory for files
void scanDirectory(const String& path) {
  using namespace SettingsNS;
  
  Serial.printf("[SETTINGS] Scanning directory: %s\n", path.c_str());
  fileList.clear();
  selectedFile = 0;
  fileScrollOffset = 0;
  currentPath = path;
  
  if (!SD.exists(path)) {
    Serial.println("[SETTINGS] Directory does not exist");
    return;
  }
  
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    Serial.println("[SETTINGS] Failed to open directory");
    return;
  }
  
  File entry;
  while ((entry = dir.openNextFile())) {
    String filename = entry.name();
    if (!entry.isDirectory() && filename.length() > 0) {
      fileList.push_back(filename);
    }
    entry.close();
  }
  dir.close();
  
  Serial.printf("[SETTINGS] Found %d files\n", fileList.size());
}

// Delete a file
bool deleteFile(const String& path, const String& filename) {
  String fullPath = path + "/" + filename;
  Serial.printf("[SETTINGS] Deleting: %s\n", fullPath.c_str());
  
  if (!SD.exists(fullPath)) {
    Serial.println("[SETTINGS] File does not exist");
    return false;
  }
  
  bool result = SD.remove(fullPath);
  if (result) {
    Serial.println("[SETTINGS] File deleted successfully");
  } else {
    Serial.println("[SETTINGS] Failed to delete file");
  }
  
  return result;
}

// ==================== UI RENDERING ====================

void settingsDrawHeader(const char* title) {
  EPD_DrawLine(0, 0, 792, 0, BLACK);
  EPD_DrawLine(0, 30, 792, 30, BLACK);
  EPD_ShowString(10, 7, "SETTINGS", 16, BLACK);
  
  if (title != NULL) {
    EPD_ShowString(250, 7, (char*)title, 16, BLACK);
  }
}

void settingsDrawFooter(const char* controls) {
  int footerY = 272 - 25;
  EPD_DrawLine(0, footerY - 5, 792, footerY - 5, BLACK);
  
  if (controls != NULL) {
    EPD_ShowString(10, footerY, (char*)controls, 16, BLACK);
  }
}

// ==================== MAIN MENU ====================

void settingsDrawMainMenu() {
  using namespace SettingsNS;
  
  Paint_Clear(WHITE);
  settingsDrawHeader("Main Menu");
  
  const char* menuItems[] = {
    "Manage Books",
    "Manage Art",
    "Display Options",
    "Power Options",
    "WiFi Options",
    "E-Reader Options",
    "Art Frame Options",
    "Save & Exit"
  };
  const int numItems = 8;
  
  int startY = 45;
  int itemHeight = 25;
  
  for (int i = 0; i < numItems; i++) {
    int yPos = startY + (i * itemHeight);
    
    if (i == selectedItem) {
      // Highlight selected item
      EPD_DrawRectangle(5, yPos - 2, 787, yPos + itemHeight - 7, BLACK, 1);
      EPD_ShowString(15, yPos, (char*)menuItems[i], 16, WHITE);
    } else {
      EPD_ShowString(15, yPos, (char*)menuItems[i], 16, BLACK);
    }
  }
  
  settingsDrawFooter("UP/DOWN: Navigate  OK: Select  EXIT: Back");
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  needsRefresh = false;
}

// ==================== FILE MANAGEMENT SCREENS ====================

void settingsDrawFileList(const char* title) {
  using namespace SettingsNS;
  
  Paint_Clear(WHITE);
  settingsDrawHeader(title);
  
  if (fileList.size() == 0) {
    EPD_ShowString(250, 100, "No files found", 16, BLACK);
    settingsDrawFooter("EXIT: Back");
  } else {
    // Draw file list
    int startY = 45;
    int itemHeight = 22;
    int maxVisible = 8;
    
    // Adjust scroll offset
    if (selectedFile < fileScrollOffset) {
      fileScrollOffset = selectedFile;
    }
    if (selectedFile >= fileScrollOffset + maxVisible) {
      fileScrollOffset = selectedFile - maxVisible + 1;
    }
    
    for (int i = 0; i < maxVisible && (i + fileScrollOffset) < fileList.size(); i++) {
      int fileIndex = i + fileScrollOffset;
      int yPos = startY + (i * itemHeight);
      
      String displayName = fileList[fileIndex];
      if (displayName.length() > 60) {
        displayName = displayName.substring(0, 57) + "...";
      }
      
      if (fileIndex == selectedFile) {
        EPD_DrawRectangle(5, yPos - 2, 787, yPos + itemHeight - 6, BLACK, 1);
        EPD_ShowString(15, yPos, (char*)displayName.c_str(), 16, WHITE);
      } else {
        EPD_ShowString(15, yPos, (char*)displayName.c_str(), 16, BLACK);
      }
    }
    
    // Draw scroll indicator
    if (fileList.size() > maxVisible) {
      char scrollText[32];
      sprintf(scrollText, "%d/%d", selectedFile + 1, (int)fileList.size());
      EPD_ShowString(700, 10, scrollText, 16, BLACK);
    }
    
    settingsDrawFooter("UP/DOWN: Navigate  OK: Delete  EXIT: Back");
  }
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  needsRefresh = false;
}

void settingsDrawDeleteConfirm() {
  using namespace SettingsNS;
  
  Paint_Clear(WHITE);
  settingsDrawHeader("Confirm Delete");
  
  // Draw warning box
  EPD_DrawRectangle(150, 70, 642, 170, BLACK, 0);
  EPD_DrawRectangle(152, 72, 640, 168, BLACK, 0);
  
  EPD_ShowString(200, 85, "Delete this file?", 16, BLACK);
  
  // Show filename (truncated if needed)
  String displayName = fileToDelete;
  if (displayName.length() > 40) {
    displayName = displayName.substring(0, 37) + "...";
  }
  EPD_ShowString(200, 110, (char*)displayName.c_str(), 16, BLACK);
  
  EPD_ShowString(200, 135, "This cannot be undone!", 16, BLACK);
  
  settingsDrawFooter("OK: Confirm Delete  EXIT: Cancel");
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  needsRefresh = false;
}

// ==================== OPTIONS SCREENS ====================

void settingsDrawDisplayOptions() {
  using namespace SettingsNS;
  
  Paint_Clear(WHITE);
  settingsDrawHeader("Display Options");
  
  int startY = 45;
  int lineHeight = 25;
  int line = 0;
  
  // Rotation
  char rotationText[32];
  sprintf(rotationText, "Rotation: %d deg", currentSettings.rotation);
  EPD_ShowString(15, startY + (line++ * lineHeight), rotationText, 16, line - 1 == selectedItem ? WHITE : BLACK);
  if (selectedItem == 0) EPD_DrawRectangle(5, startY - 2, 787, startY + lineHeight - 7, BLACK, 1);
  
  // Battery indicator
  EPD_ShowString(15, startY + (line * lineHeight), 
                 currentSettings.showBatteryPercent ? "Battery: Show %" : "Battery: Icon only", 
                 16, line == selectedItem ? WHITE : BLACK);
  if (selectedItem == 1) EPD_DrawRectangle(5, startY + lineHeight - 2, 787, startY + (2 * lineHeight) - 7, BLACK, 1);
  line++;
  
  // Back option
  EPD_ShowString(15, startY + (line * lineHeight), "Back to Main Menu", 16, line == selectedItem ? WHITE : BLACK);
  if (selectedItem == 2) EPD_DrawRectangle(5, startY + (2 * lineHeight) - 2, 787, startY + (3 * lineHeight) - 7, BLACK, 1);
  
  settingsDrawFooter("UP/DOWN: Navigate  OK: Toggle  EXIT: Back");
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  needsRefresh = false;
}

void settingsDrawPowerOptions() {
  using namespace SettingsNS;
  
  Paint_Clear(WHITE);
  settingsDrawHeader("Power Options");
  
  int startY = 45;
  int lineHeight = 25;
  
  // Auto-sleep timeout
  char sleepText[64];
  if (currentSettings.autoSleepMinutes == 0) {
    sprintf(sleepText, "Auto-Sleep: Disabled");
  } else {
    sprintf(sleepText, "Auto-Sleep: %d min", currentSettings.autoSleepMinutes);
  }
  
  if (selectedItem == 0) EPD_DrawRectangle(5, startY - 2, 787, startY + lineHeight - 7, BLACK, 1);
  EPD_ShowString(15, startY, sleepText, 16, selectedItem == 0 ? WHITE : BLACK);
  
  // Back option
  if (selectedItem == 1) EPD_DrawRectangle(5, startY + lineHeight - 2, 787, startY + (2 * lineHeight) - 7, BLACK, 1);
  EPD_ShowString(15, startY + lineHeight, "Back to Main Menu", 16, selectedItem == 1 ? WHITE : BLACK);
  
  settingsDrawFooter("UP/DOWN: Navigate  OK: Adjust  EXIT: Back");
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  needsRefresh = false;
}

void settingsDrawEReaderOptions() {
  using namespace SettingsNS;
  
  Paint_Clear(WHITE);
  settingsDrawHeader("E-Reader Options");
  
  int startY = 45;
  int lineHeight = 25;
  int line = 0;
  
  // Font size
  char fontText[32];
  sprintf(fontText, "Font Size: %d", currentSettings.fontSize);
  if (selectedItem == line) EPD_DrawRectangle(5, startY + (line * lineHeight) - 2, 787, startY + ((line + 1) * lineHeight) - 7, BLACK, 1);
  EPD_ShowString(15, startY + (line * lineHeight), fontText, 16, line == selectedItem ? WHITE : BLACK);
  line++;
  
  // Progress save frequency
  char saveText[64];
  sprintf(saveText, "Save Progress: Every %d scrolls", currentSettings.progressSaveFrequency);
  if (selectedItem == line) EPD_DrawRectangle(5, startY + (line * lineHeight) - 2, 787, startY + ((line + 1) * lineHeight) - 7, BLACK, 1);
  EPD_ShowString(15, startY + (line * lineHeight), saveText, 16, line == selectedItem ? WHITE : BLACK);
  line++;
  
  // Back option
  if (selectedItem == line) EPD_DrawRectangle(5, startY + (line * lineHeight) - 2, 787, startY + ((line + 1) * lineHeight) - 7, BLACK, 1);
  EPD_ShowString(15, startY + (line * lineHeight), "Back to Main Menu", 16, line == selectedItem ? WHITE : BLACK);
  
  settingsDrawFooter("UP/DOWN: Navigate  OK: Adjust  EXIT: Back");
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  needsRefresh = false;
}

void settingsDrawArtFrameOptions() {
  using namespace SettingsNS;
  
  Paint_Clear(WHITE);
  settingsDrawHeader("Art Frame Options");
  
  int startY = 45;
  int lineHeight = 25;
  int line = 0;
  
  // Auto-cycle
  EPD_ShowString(15, startY + (line * lineHeight), 
                 currentSettings.artAutoCycleEnabled ? "Auto-Cycle: ON" : "Auto-Cycle: OFF", 
                 16, line == selectedItem ? WHITE : BLACK);
  if (selectedItem == line) EPD_DrawRectangle(5, startY + (line * lineHeight) - 2, 787, startY + ((line + 1) * lineHeight) - 7, BLACK, 1);
  line++;
  
  // Cycle interval
  char cycleText[64];
  sprintf(cycleText, "Cycle Interval: %d sec", currentSettings.artCycleSeconds);
  EPD_ShowString(15, startY + (line * lineHeight), cycleText, 16, line == selectedItem ? WHITE : BLACK);
  if (selectedItem == line) EPD_DrawRectangle(5, startY + (line * lineHeight) - 2, 787, startY + ((line + 1) * lineHeight) - 7, BLACK, 1);
  line++;
  
  // Back option
  EPD_ShowString(15, startY + (line * lineHeight), "Back to Main Menu", 16, line == selectedItem ? WHITE : BLACK);
  if (selectedItem == line) EPD_DrawRectangle(5, startY + (line * lineHeight) - 2, 787, startY + ((line + 1) * lineHeight) - 7, BLACK, 1);
  
  settingsDrawFooter("UP/DOWN: Navigate  OK: Toggle/Adjust  EXIT: Back");
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  needsRefresh = false;
}

void settingsDrawWiFiOptions() {
  using namespace SettingsNS;
  
  Paint_Clear(WHITE);
  settingsDrawHeader("WiFi Options");
  
  int startY = 45;
  int lineHeight = 22;
  
  EPD_ShowString(15, startY, "WiFi AP Mode Settings", 16, BLACK);
  EPD_ShowString(15, startY + lineHeight, "(Restart required to apply)", 16, BLACK);
  
  char ssidText[64];
  sprintf(ssidText, "SSID: %s", currentSettings.wifiSSID);
  EPD_ShowString(15, startY + (3 * lineHeight), ssidText, 16, BLACK);
  
  char passText[64];
  // Mask password
  int passLen = strlen(currentSettings.wifiPassword);
  char maskedPass[32] = "";
  for (int i = 0; i < passLen && i < 20; i++) {
    maskedPass[i] = '*';
  }
  maskedPass[passLen < 20 ? passLen : 20] = '\0';
  sprintf(passText, "Password: %s", maskedPass);
  EPD_ShowString(15, startY + (4 * lineHeight), passText, 16, BLACK);
  
  EPD_ShowString(15, startY + (6 * lineHeight), 
                 currentSettings.wifiEnabled ? "WiFi: Enabled" : "WiFi: Disabled", 
                 16, selectedItem == 0 ? WHITE : BLACK);
  if (selectedItem == 0) EPD_DrawRectangle(5, startY + (6 * lineHeight) - 2, 787, startY + (7 * lineHeight) - 5, BLACK, 1);
  
  EPD_ShowString(15, startY + (7 * lineHeight), "Back to Main Menu", 16, selectedItem == 1 ? WHITE : BLACK);
  if (selectedItem == 1) EPD_DrawRectangle(5, startY + (7 * lineHeight) - 2, 787, startY + (8 * lineHeight) - 5, BLACK, 1);
  
  settingsDrawFooter("UP/DOWN: Navigate  OK: Toggle  EXIT: Back");
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  needsRefresh = false;
}

// ==================== INPUT HANDLING ====================

void settingsHandleMainMenuInput(bool upPressed, bool downPressed, bool okPressed) {
  using namespace SettingsNS;
  
  if (upPressed) {
    selectedItem--;
    if (selectedItem < 0) selectedItem = 7;
    needsRefresh = true;
  }
  
  if (downPressed) {
    selectedItem++;
    if (selectedItem > 7) selectedItem = 0;
    needsRefresh = true;
  }
  
  if (okPressed) {
    switch (selectedItem) {
      case 0: // Manage Books
        currentMode = SETTINGS_MANAGE_BOOKS;
        scanDirectory("/books");
        needsRefresh = true;
        break;
        
      case 1: // Manage Art
        currentMode = SETTINGS_MANAGE_ART;
        scanDirectory("/art");
        needsRefresh = true;
        break;
        
      case 2: // Display Options
        currentMode = SETTINGS_DISPLAY_OPTIONS;
        selectedItem = 0;
        needsRefresh = true;
        break;
        
      case 3: // Power Options
        currentMode = SETTINGS_POWER_OPTIONS;
        selectedItem = 0;
        needsRefresh = true;
        break;
        
      case 4: // WiFi Options
        currentMode = SETTINGS_WIFI_OPTIONS;
        selectedItem = 0;
        needsRefresh = true;
        break;
        
      case 5: // E-Reader Options
        currentMode = SETTINGS_EREADER_OPTIONS;
        selectedItem = 0;
        needsRefresh = true;
        break;
        
      case 6: // Art Frame Options
        currentMode = SETTINGS_ARTFRAME_OPTIONS;
        selectedItem = 0;
        needsRefresh = true;
        break;
        
      case 7: // Save & Exit
        saveSettings();
        // Return handled by main loop
        break;
    }
  }
}

void settingsHandleFileListInput(bool upPressed, bool downPressed, bool okPressed) {
  using namespace SettingsNS;
  
  if (fileList.size() == 0) return;
  
  if (upPressed) {
    selectedFile--;
    if (selectedFile < 0) selectedFile = fileList.size() - 1;
    needsRefresh = true;
  }
  
  if (downPressed) {
    selectedFile++;
    if (selectedFile >= fileList.size()) selectedFile = 0;
    needsRefresh = true;
  }
  
  if (okPressed && selectedFile >= 0 && selectedFile < fileList.size()) {
    // Show delete confirmation
    fileToDelete = fileList[selectedFile];
    confirmDelete = true;
    needsRefresh = true;
  }
}

void settingsHandleDeleteConfirmInput(bool okPressed, bool exitPressed) {
  using namespace SettingsNS;
  
  if (okPressed) {
    // Delete the file
    if (deleteFile(currentPath, fileToDelete)) {
      // Rescan directory
      scanDirectory(currentPath);
    }
    confirmDelete = false;
    needsRefresh = true;
  }
  
  if (exitPressed) {
    // Cancel deletion
    confirmDelete = false;
    needsRefresh = true;
  }
}

void settingsHandleDisplayOptionsInput(bool upPressed, bool downPressed, bool okPressed) {
  using namespace SettingsNS;
  
  if (upPressed) {
    selectedItem--;
    if (selectedItem < 0) selectedItem = 2;
    needsRefresh = true;
  }
  
  if (downPressed) {
    selectedItem++;
    if (selectedItem > 2) selectedItem = 0;
    needsRefresh = true;
  }
  
  if (okPressed) {
    switch (selectedItem) {
      case 0: // Rotation
        currentSettings.rotation = (currentSettings.rotation + 90) % 360;
        needsRefresh = true;
        break;
        
      case 1: // Battery display
        currentSettings.showBatteryPercent = !currentSettings.showBatteryPercent;
        needsRefresh = true;
        break;
        
      case 2: // Back
        currentMode = SETTINGS_MAIN_MENU;
        selectedItem = 0;
        needsRefresh = true;
        break;
    }
  }
}

void settingsHandlePowerOptionsInput(bool upPressed, bool downPressed, bool okPressed) {
  using namespace SettingsNS;
  
  if (upPressed) {
    selectedItem--;
    if (selectedItem < 0) selectedItem = 1;
    needsRefresh = true;
  }
  
  if (downPressed) {
    selectedItem++;
    if (selectedItem > 1) selectedItem = 0;
    needsRefresh = true;
  }
  
  if (okPressed) {
    switch (selectedItem) {
      case 0: // Auto-sleep
        // Cycle through: 0, 5, 10, 15, 30, 60 minutes
        if (currentSettings.autoSleepMinutes == 0) currentSettings.autoSleepMinutes = 5;
        else if (currentSettings.autoSleepMinutes == 5) currentSettings.autoSleepMinutes = 10;
        else if (currentSettings.autoSleepMinutes == 10) currentSettings.autoSleepMinutes = 15;
        else if (currentSettings.autoSleepMinutes == 15) currentSettings.autoSleepMinutes = 30;
        else if (currentSettings.autoSleepMinutes == 30) currentSettings.autoSleepMinutes = 60;
        else currentSettings.autoSleepMinutes = 0;
        needsRefresh = true;
        break;
        
      case 1: // Back
        currentMode = SETTINGS_MAIN_MENU;
        selectedItem = 0;
        needsRefresh = true;
        break;
    }
  }
}

void settingsHandleEReaderOptionsInput(bool upPressed, bool downPressed, bool okPressed) {
  using namespace SettingsNS;
  
  if (upPressed) {
    selectedItem--;
    if (selectedItem < 0) selectedItem = 2;
    needsRefresh = true;
  }
  
  if (downPressed) {
    selectedItem++;
    if (selectedItem > 2) selectedItem = 0;
    needsRefresh = true;
  }
  
  if (okPressed) {
    switch (selectedItem) {
      case 0: // Font size
        if (currentSettings.fontSize == 16) currentSettings.fontSize = 24;
        else if (currentSettings.fontSize == 24) currentSettings.fontSize = 32;
        else currentSettings.fontSize = 16;
        needsRefresh = true;
        break;
        
      case 1: // Save frequency
        // Cycle: 3, 5, 10, 20
        if (currentSettings.progressSaveFrequency == 3) currentSettings.progressSaveFrequency = 5;
        else if (currentSettings.progressSaveFrequency == 5) currentSettings.progressSaveFrequency = 10;
        else if (currentSettings.progressSaveFrequency == 10) currentSettings.progressSaveFrequency = 20;
        else currentSettings.progressSaveFrequency = 3;
        needsRefresh = true;
        break;
        
      case 2: // Back
        currentMode = SETTINGS_MAIN_MENU;
        selectedItem = 0;
        needsRefresh = true;
        break;
    }
  }
}

void settingsHandleArtFrameOptionsInput(bool upPressed, bool downPressed, bool okPressed) {
  using namespace SettingsNS;
  
  if (upPressed) {
    selectedItem--;
    if (selectedItem < 0) selectedItem = 2;
    needsRefresh = true;
  }
  
  if (downPressed) {
    selectedItem++;
    if (selectedItem > 2) selectedItem = 0;
    needsRefresh = true;
  }
  
  if (okPressed) {
    switch (selectedItem) {
      case 0: // Auto-cycle toggle
        currentSettings.artAutoCycleEnabled = !currentSettings.artAutoCycleEnabled;
        needsRefresh = true;
        break;
        
      case 1: // Cycle interval
        // Cycle: 10, 30, 60, 120, 300 seconds
        if (currentSettings.artCycleSeconds == 10) currentSettings.artCycleSeconds = 30;
        else if (currentSettings.artCycleSeconds == 30) currentSettings.artCycleSeconds = 60;
        else if (currentSettings.artCycleSeconds == 60) currentSettings.artCycleSeconds = 120;
        else if (currentSettings.artCycleSeconds == 120) currentSettings.artCycleSeconds = 300;
        else currentSettings.artCycleSeconds = 10;
        needsRefresh = true;
        break;
        
      case 2: // Back
        currentMode = SETTINGS_MAIN_MENU;
        selectedItem = 0;
        needsRefresh = true;
        break;
    }
  }
}

void settingsHandleWiFiOptionsInput(bool upPressed, bool downPressed, bool okPressed) {
  using namespace SettingsNS;
  
  if (upPressed) {
    selectedItem--;
    if (selectedItem < 0) selectedItem = 1;
    needsRefresh = true;
  }
  
  if (downPressed) {
    selectedItem++;
    if (selectedItem > 1) selectedItem = 0;
    needsRefresh = true;
  }
  
  if (okPressed) {
    switch (selectedItem) {
      case 0: // WiFi enable/disable
        currentSettings.wifiEnabled = !currentSettings.wifiEnabled;
        needsRefresh = true;
        break;
        
      case 1: // Back
        currentMode = SETTINGS_MAIN_MENU;
        selectedItem = 0;
        needsRefresh = true;
        break;
    }
  }
}

// ==================== PUBLIC INTERFACE ====================

void settingsInit() {
  using namespace SettingsNS;
  
  Serial.println("[SETTINGS] Initializing settings mode...");
  
  // Load settings
  if (!settingsLoaded) {
    loadSettings();
  }
  
  // Initialize display
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  
  // Reset UI state
  currentMode = SETTINGS_MAIN_MENU;
  selectedItem = 0;
  scrollOffset = 0;
  needsRefresh = true;
  confirmDelete = false;
  
  Serial.println("[SETTINGS] Initialization complete");
}

void settingsUpdate() {
  using namespace SettingsNS;
  
  if (!needsRefresh) return;
  
  switch (currentMode) {
    case SETTINGS_MAIN_MENU:
      settingsDrawMainMenu();
      break;
      
    case SETTINGS_MANAGE_BOOKS:
    case SETTINGS_MANAGE_ART:
      if (confirmDelete) {
        settingsDrawDeleteConfirm();
      } else {
        settingsDrawFileList(currentMode == SETTINGS_MANAGE_BOOKS ? "Manage Books" : "Manage Art");
      }
      break;
      
    case SETTINGS_DISPLAY_OPTIONS:
      settingsDrawDisplayOptions();
      break;
      
    case SETTINGS_POWER_OPTIONS:
      settingsDrawPowerOptions();
      break;
      
    case SETTINGS_WIFI_OPTIONS:
      settingsDrawWiFiOptions();
      break;
      
    case SETTINGS_EREADER_OPTIONS:
      settingsDrawEReaderOptions();
      break;
      
    case SETTINGS_ARTFRAME_OPTIONS:
      settingsDrawArtFrameOptions();
      break;
  }
}

void settingsHandleInput(bool upPressed, bool downPressed, bool okPressed, bool exitPressed) {
  using namespace SettingsNS;
  
  // Handle delete confirmation separately
  if (confirmDelete) {
    settingsHandleDeleteConfirmInput(okPressed, exitPressed);
    return;
  }
  
  // Exit pressed - go back
  if (exitPressed) {
    if (currentMode != SETTINGS_MAIN_MENU) {
      currentMode = SETTINGS_MAIN_MENU;
      selectedItem = 0;
      needsRefresh = true;
    }
    // else: exit handled by main loop
    return;
  }
  
  // Route input based on current mode
  switch (currentMode) {
    case SETTINGS_MAIN_MENU:
      settingsHandleMainMenuInput(upPressed, downPressed, okPressed);
      break;
      
    case SETTINGS_MANAGE_BOOKS:
    case SETTINGS_MANAGE_ART:
      settingsHandleFileListInput(upPressed, downPressed, okPressed);
      break;
      
    case SETTINGS_DISPLAY_OPTIONS:
      settingsHandleDisplayOptionsInput(upPressed, downPressed, okPressed);
      break;
      
    case SETTINGS_POWER_OPTIONS:
      settingsHandlePowerOptionsInput(upPressed, downPressed, okPressed);
      break;
      
    case SETTINGS_WIFI_OPTIONS:
      settingsHandleWiFiOptionsInput(upPressed, downPressed, okPressed);
      break;
      
    case SETTINGS_EREADER_OPTIONS:
      settingsHandleEReaderOptionsInput(upPressed, downPressed, okPressed);
      break;
      
    case SETTINGS_ARTFRAME_OPTIONS:
      settingsHandleArtFrameOptionsInput(upPressed, downPressed, okPressed);
      break;
  }
}

void settingsCleanup() {
  using namespace SettingsNS;
  
  Serial.println("[SETTINGS] Cleaning up...");
  
  // Save settings before exit
  saveSettings();
  
  fileList.clear();
  selectedFile = 0;
  fileScrollOffset = 0;
  currentPath = "";
}

// ==================== GETTERS FOR OTHER MODULES ====================

int settingsGetAutoSleepMinutes() {
  return SettingsNS::currentSettings.autoSleepMinutes;
}

int settingsGetArtCycleSeconds() {
  return SettingsNS::currentSettings.artCycleSeconds;
}

bool settingsGetArtAutoCycleEnabled() {
  return SettingsNS::currentSettings.artAutoCycleEnabled;
}

int settingsGetProgressSaveFrequency() {
  return SettingsNS::currentSettings.progressSaveFrequency;
}

int settingsGetFontSize() {
  return SettingsNS::currentSettings.fontSize;
}

int settingsGetRotation() {
  return SettingsNS::currentSettings.rotation;
}

bool settingsGetWiFiEnabled() {
  return SettingsNS::currentSettings.wifiEnabled;
}

const char* settingsGetWiFiSSID() {
  return SettingsNS::currentSettings.wifiSSID;
}

const char* settingsGetWiFiPassword() {
  return SettingsNS::currentSettings.wifiPassword;
}

bool settingsGetShowBatteryPercent() {
  return SettingsNS::currentSettings.showBatteryPercent;
}

bool settingsGetEnableSounds() {
  return SettingsNS::currentSettings.enableSounds;
}

#endif // SETTINGS_H
