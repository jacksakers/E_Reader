#ifndef KITTALIEN_H
#define KITTALIEN_H

#include "EPD.h"
#include "SD.h"
#include <Arduino.h>

// ==================== KITTALIEN CONFIGURATION ====================
#define KITTALIEN_SAVE_FILE "/kittalien_save.txt"
#define KITTALIEN_MAX_STAT 100
#define KITTALIEN_MIN_STAT 0

// Decay rates (per hour of real time)
#define KITTALIEN_HUNGER_DECAY_RATE 5     // Hunger decreases by 5 per hour
#define KITTALIEN_HAPPINESS_DECAY_RATE 3  // Happiness decreases by 3 per hour
#define KITTALIEN_ENERGY_DECAY_RATE 2     // Energy decreases by 2 per hour

// Action effects
#define KITTALIEN_FEED_AMOUNT 25
#define KITTALIEN_PLAY_HAPPINESS 20
#define KITTALIEN_PLAY_ENERGY_COST 15
#define KITTALIEN_PET_HAPPINESS 10
#define KITTALIEN_SLEEP_ENERGY 40

// UI Configuration
#define KITTALIEN_UI_TIMEOUT 5000         // Show action result for 5 seconds

// Reference to global display buffer from OS_Main.ino
extern uint8_t ImageBW[27200];

// ==================== KITTALIEN STATE ====================
namespace KittalienNS {
  
  // Visual states based on stats
  enum PetState {
    STATE_SICK,      // Hunger < 20
    STATE_SAD,       // Happiness < 30
    STATE_SLEEPING,  // After Sleep action
    STATE_HAPPY,     // Happiness > 70 && Hunger > 50
    STATE_NEUTRAL    // Default
  };
  
  // Available actions
  enum Action {
    ACTION_FEED,
    ACTION_PLAY,
    ACTION_PET,
    ACTION_SLEEP,
    ACTION_COUNT
  };
  
  // Pet stats and state
  struct PetStats {
    int hunger;           // 0-100
    int happiness;        // 0-100
    int energy;           // 0-100
    unsigned long lastUpdateTime;  // Timestamp in seconds since epoch
    PetState currentState;
    bool isNewPet;        // First time initialization
  };
  
  // Game state
  static PetStats pet;
  static int selectedAction = 0;
  static bool needsRefresh = true;
  static String statusMessage = "";
  static unsigned long messageDisplayTime = 0;
  static bool showingMessage = false;
  
  // Art filenames for different states
  static const char* stateImages[] = {
    "sick_kittalien.art",      // STATE_SICK
    "sad_kittalien.art",       // STATE_SAD
    "sleeping_kittalien.art",  // STATE_SLEEPING
    "happy_kittalien.art",     // STATE_HAPPY
    "neutral_kittalien.art"    // STATE_NEUTRAL
  };
  
  // Action names
  static const char* actionNames[] = {
    "Feed",
    "Play",
    "Pet",
    "Sleep"
  };
  
  // Action descriptions
  static const char* actionDescriptions[] = {
    "Give food (+Hunger)",
    "Play game (+Happy, -Energy)",
    "Pet gently (+Happy)",
    "Take a nap (+Energy)"
  };
}

// ==================== HELPER FUNCTIONS ====================

// Clamp a value between min and max
int clamp(int value, int min, int max) {
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

// Get current time in seconds (using millis as a fallback)
// Note: For accurate time tracking, you should integrate an RTC module
unsigned long getCurrentTimeSeconds() {
  // Using millis() / 1000 as a simple timestamp
  // WARNING: This resets when the device reboots!
  // For production, integrate DS3231 RTC or NTP time sync
  return millis() / 1000UL;
}

// ==================== SAVE/LOAD FUNCTIONS ====================

bool savePetState() {
  using namespace KittalienNS;
  
  Serial.println("[KITTALIEN] Saving pet state...");
  
  // Delete old save file
  if (SD.exists(KITTALIEN_SAVE_FILE)) {
    SD.remove(KITTALIEN_SAVE_FILE);
  }
  
  // Open file for writing
  File file = SD.open(KITTALIEN_SAVE_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("[KITTALIEN] ERROR: Failed to open save file");
    return false;
  }
  
  // Write stats in simple key=value format
  file.printf("hunger=%d\n", pet.hunger);
  file.printf("happiness=%d\n", pet.happiness);
  file.printf("energy=%d\n", pet.energy);
  file.printf("lastUpdateTime=%lu\n", pet.lastUpdateTime);
  file.printf("currentState=%d\n", (int)pet.currentState);
  file.printf("isNewPet=%d\n", pet.isNewPet ? 0 : 1);
  
  file.close();
  
  Serial.printf("[KITTALIEN] Saved: H=%d, Ha=%d, E=%d, Time=%lu\n", 
                pet.hunger, pet.happiness, pet.energy, pet.lastUpdateTime);
  return true;
}

bool loadPetState() {
  using namespace KittalienNS;
  
  Serial.println("[KITTALIEN] Loading pet state...");
  
  // Check if save file exists
  if (!SD.exists(KITTALIEN_SAVE_FILE)) {
    Serial.println("[KITTALIEN] No save file found - creating new pet");
    
    // Initialize new pet with good stats
    pet.hunger = 80;
    pet.happiness = 80;
    pet.energy = 80;
    pet.lastUpdateTime = getCurrentTimeSeconds();
    pet.currentState = STATE_NEUTRAL;
    pet.isNewPet = true;
    
    return savePetState();
  }
  
  // Open save file
  File file = SD.open(KITTALIEN_SAVE_FILE, FILE_READ);
  if (!file) {
    Serial.println("[KITTALIEN] ERROR: Failed to open save file");
    return false;
  }
  
  // Parse save data
  char line[128];
  while (file.available()) {
    int bytesRead = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[bytesRead] = '\0';
    
    // Parse key=value pairs
    char* equals = strchr(line, '=');
    if (equals != NULL) {
      *equals = '\0';
      char* key = line;
      char* value = equals + 1;
      
      if (strcmp(key, "hunger") == 0) {
        pet.hunger = atoi(value);
      } else if (strcmp(key, "happiness") == 0) {
        pet.happiness = atoi(value);
      } else if (strcmp(key, "energy") == 0) {
        pet.energy = atoi(value);
      } else if (strcmp(key, "lastUpdateTime") == 0) {
        pet.lastUpdateTime = atol(value);
      } else if (strcmp(key, "currentState") == 0) {
        pet.currentState = (PetState)atoi(value);
      } else if (strcmp(key, "isNewPet") == 0) {
        pet.isNewPet = (atoi(value) == 0);
      }
    }
  }
  
  file.close();
  
  Serial.printf("[KITTALIEN] Loaded: H=%d, Ha=%d, E=%d, Time=%lu\n", 
                pet.hunger, pet.happiness, pet.energy, pet.lastUpdateTime);
  return true;
}

// ==================== TIME DECAY SYSTEM ====================

void applyTimeDecay() {
  using namespace KittalienNS;
  
  unsigned long currentTime = getCurrentTimeSeconds();
  unsigned long timeDiff = currentTime - pet.lastUpdateTime;
  
  // If time went backwards (device reboot), just use current time
  if (currentTime < pet.lastUpdateTime) {
    Serial.println("[KITTALIEN] Time anomaly detected - resetting timer");
    pet.lastUpdateTime = currentTime;
    savePetState();
    return;
  }
  
  // Calculate hours passed (with fractional precision)
  float hoursPassed = timeDiff / 3600.0f;
  
  Serial.printf("[KITTALIEN] Time passed: %.2f hours (%lu seconds)\n", 
                hoursPassed, timeDiff);
  
  // Only apply decay if significant time has passed (>1 minute)
  if (timeDiff < 60) {
    return;
  }
  
  // Apply decay
  int hungerDecay = (int)(hoursPassed * KITTALIEN_HUNGER_DECAY_RATE);
  int happinessDecay = (int)(hoursPassed * KITTALIEN_HAPPINESS_DECAY_RATE);
  int energyDecay = (int)(hoursPassed * KITTALIEN_ENERGY_DECAY_RATE);
  
  pet.hunger -= hungerDecay;
  pet.happiness -= happinessDecay;
  pet.energy -= energyDecay;
  
  // Clamp stats
  pet.hunger = clamp(pet.hunger, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
  pet.happiness = clamp(pet.happiness, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
  pet.energy = clamp(pet.energy, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
  
  // Update timestamp
  pet.lastUpdateTime = currentTime;
  
  Serial.printf("[KITTALIEN] After decay: H=%d (-%d), Ha=%d (-%d), E=%d (-%d)\n",
                pet.hunger, hungerDecay, pet.happiness, happinessDecay, 
                pet.energy, energyDecay);
  
  // Save immediately after decay
  savePetState();
}

// ==================== STATE DETERMINATION ====================

void updatePetState() {
  using namespace KittalienNS;
  
  PetState oldState = pet.currentState;
  
  // Determine state based on stats (priority order)
  if (pet.hunger < 20) {
    pet.currentState = STATE_SICK;
  } else if (pet.happiness < 30) {
    pet.currentState = STATE_SAD;
  } else if (pet.happiness > 70 && pet.hunger > 50) {
    pet.currentState = STATE_HAPPY;
  } else {
    pet.currentState = STATE_NEUTRAL;
  }
  
  // If state changed, trigger refresh
  if (oldState != pet.currentState) {
    Serial.printf("[KITTALIEN] State changed: %d -> %d\n", oldState, pet.currentState);
    needsRefresh = true;
  }
}

// ==================== ACTION EXECUTION ====================

void executeAction(KittalienNS::Action action) {
  using namespace KittalienNS;
  
  Serial.printf("[KITTALIEN] Executing action: %d (%s)\n", action, actionNames[action]);
  
  switch (action) {
    case ACTION_FEED:
      pet.hunger += KITTALIEN_FEED_AMOUNT;
      pet.hunger = clamp(pet.hunger, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
      statusMessage = "Fed Kittalien! (+Hunger)";
      break;
      
    case ACTION_PLAY:
      if (pet.energy < KITTALIEN_PLAY_ENERGY_COST) {
        statusMessage = "Too tired to play!";
      } else {
        pet.happiness += KITTALIEN_PLAY_HAPPINESS;
        pet.energy -= KITTALIEN_PLAY_ENERGY_COST;
        pet.happiness = clamp(pet.happiness, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
        pet.energy = clamp(pet.energy, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
        statusMessage = "Played fun game! (+Happy, -Energy)";
      }
      break;
      
    case ACTION_PET:
      pet.happiness += KITTALIEN_PET_HAPPINESS;
      pet.happiness = clamp(pet.happiness, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
      statusMessage = "Petted softly! (+Happy)";
      break;
      
    case ACTION_SLEEP:
      pet.energy += KITTALIEN_SLEEP_ENERGY;
      pet.energy = clamp(pet.energy, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
      pet.currentState = STATE_SLEEPING;
      statusMessage = "Taking a nap... (+Energy)";
      needsRefresh = true; // Force immediate state update
      break;
  }
  
  // Update pet state after action
  updatePetState();
  
  // Save state
  savePetState();
  
  // Show message
  showingMessage = true;
  messageDisplayTime = millis();
  needsRefresh = true;
  
  Serial.printf("[KITTALIEN] After action: H=%d, Ha=%d, E=%d\n", 
                pet.hunger, pet.happiness, pet.energy);
}

// ==================== IMAGE LOADING ====================

bool loadPetImage(KittalienNS::PetState state) {
  using namespace KittalienNS;
  
  // Get filename for current state
  String filepath = "/art/" + String(stateImages[(int)state]);
  Serial.printf("[KITTALIEN] Loading image: %s\n", filepath.c_str());
  
  // Check if file exists
  if (!SD.exists(filepath)) {
    Serial.printf("[KITTALIEN] Image not found: %s\n", filepath.c_str());
    // Try neutral as fallback
    if (state != STATE_NEUTRAL) {
      Serial.println("[KITTALIEN] Falling back to neutral state");
      return loadPetImage(STATE_NEUTRAL);
    }
    return false;
  }
  
  // Open file
  File artFile = SD.open(filepath, FILE_READ);
  if (!artFile) {
    Serial.println("[KITTALIEN] Failed to open image file");
    return false;
  }
  
  // Read image data into buffer
  size_t fileSize = artFile.size();
  size_t bytesToRead = min(fileSize, (size_t)27200);
  size_t bytesRead = artFile.read(ImageBW, bytesToRead);
  artFile.close();
  
  if (bytesRead != bytesToRead) {
    Serial.printf("[KITTALIEN] Read error: expected %d, got %d bytes\n", 
                  bytesToRead, bytesRead);
    return false;
  }
  
  Serial.printf("[KITTALIEN] Successfully loaded %d bytes\n", bytesRead);
  return true;
}

// ==================== UI RENDERING ====================

void drawStatBar(int x, int y, const char* label, int value, int maxValue) {
  // Draw label
  EPD_ShowString(x, y, (char*)label, 16, BLACK);
  
  // Draw bar background
  int barWidth = 150;
  int barHeight = 12;
  int labelWidth = strlen(label) * 8 + 10;
  
  EPD_DrawRectangle(x + labelWidth, y + 2, x + labelWidth + barWidth, y + 2 + barHeight, BLACK, 0);
  
  // Draw filled portion
  int fillWidth = (barWidth * value) / maxValue;
  if (fillWidth > 0) {
    for (int i = 0; i < barHeight; i++) {
      EPD_DrawLine(x + labelWidth + 1, y + 2 + i, 
                   x + labelWidth + fillWidth - 1, y + 2 + i, BLACK);
    }
  }
  
  // Draw percentage
  char percentText[16];
  snprintf(percentText, sizeof(percentText), "%d%%", value);
  EPD_ShowString(x + labelWidth + barWidth + 10, y, percentText, 16, BLACK);
}

void kittalienDrawScreen() {
  using namespace KittalienNS;
  
  if (!needsRefresh) return;
  
  Serial.println("[KITTALIEN] Drawing screen...");
  
  // Load and display the pet image (full screen background)
  if (!loadPetImage(pet.currentState)) {
    // If image loading fails, just clear the screen
    Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
    Paint_Clear(WHITE);
    EPD_ShowString(200, 120, "Image not found!", 24, BLACK);
  }
  
  // Initialize paint system (this sets up proper drawing on top of the loaded image)
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  
  // Draw semi-transparent overlay boxes for UI elements
  // Top bar for stats
  EPD_DrawRectangle(0, 0, 791, 80, BLACK, 0);
  for (int i = 0; i < 3; i++) {
    EPD_DrawLine(0, i, 791, i, BLACK);
  }
  
  // Title
  EPD_ShowString(320, 5, "KITTALIEN", 24, BLACK);
  
  // Draw stat bars
  drawStatBar(20, 35, "Hunger:  ", pet.hunger, KITTALIEN_MAX_STAT);
  drawStatBar(20, 55, "Happy:   ", pet.happiness, KITTALIEN_MAX_STAT);
  drawStatBar(420, 45, "Energy:  ", pet.energy, KITTALIEN_MAX_STAT);
  
  // Draw action menu on the left side
  int menuX = 10;
  int menuY = 100;
  int menuSpacing = 35;
  
  EPD_DrawRectangle(0, 90, 180, 271, BLACK, 0);
  for (int i = 0; i < 2; i++) {
    EPD_DrawLine(i, 90, i, 271, BLACK);
  }
  
  EPD_ShowString(menuX + 10, menuY - 15, "ACTIONS:", 16, BLACK);
  
  for (int i = 0; i < ACTION_COUNT; i++) {
    int yPos = menuY + (i * menuSpacing);
    
    // Selection indicator
    if (i == selectedAction) {
      EPD_ShowString(menuX, yPos, ">", 16, BLACK);
      EPD_DrawRectangle(menuX + 15, yPos - 2, menuX + 165, yPos + 18, BLACK, 0);
    }
    
    // Action name
    EPD_ShowString(menuX + 20, yPos, (char*)actionNames[i], 16, BLACK);
  }
  
  // Draw action description box on the right side
  EPD_DrawRectangle(612, 90, 791, 180, BLACK, 0);
  for (int i = 0; i < 2; i++) {
    EPD_DrawLine(790 - i, 90, 790 - i, 180, BLACK);
  }
  
  EPD_ShowString(620, 95, "INFO:", 16, BLACK);
  
  // Word-wrap the description
  String desc = String(actionDescriptions[selectedAction]);
  int lineY = 115;
  int maxCharsPerLine = 20;
  int startIdx = 0;
  
  while (startIdx < desc.length()) {
    int endIdx = startIdx + maxCharsPerLine;
    if (endIdx > desc.length()) endIdx = desc.length();
    
    String line = desc.substring(startIdx, endIdx);
    EPD_ShowString(620, lineY, (char*)line.c_str(), 16, BLACK);
    
    lineY += 20;
    startIdx = endIdx;
  }
  
  // Status message box (if showing)
  if (showingMessage) {
    EPD_DrawRectangle(200, 200, 590, 250, WHITE, 1);
    EPD_DrawRectangle(200, 200, 590, 250, BLACK, 0);
    EPD_ShowString(220, 220, (char*)statusMessage.c_str(), 16, BLACK);
  }
  
  // Controls footer
  EPD_DrawRectangle(0, 245, 791, 271, BLACK, 0);
  for (int i = 0; i < 2; i++) {
    EPD_DrawLine(0, 270 - i, 791, 270 - i, BLACK);
  }
  EPD_ShowString(10, 250, "UP/DN: Select Action", 16, BLACK);
  EPD_ShowString(280, 250, "OK: Execute", 16, BLACK);
  EPD_ShowString(550, 250, "EXIT: Return", 16, BLACK);
  
  // Display on screen
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  needsRefresh = false;
  Serial.println("[KITTALIEN] Screen updated");
}

// ==================== INPUT HANDLING ====================

void kittalienHandleInput(bool upPressed, bool downPressed, bool okPressed, bool exitPressed) {
  using namespace KittalienNS;
  
  // Navigate up in menu
  if (upPressed) {
    selectedAction--;
    if (selectedAction < 0) {
      selectedAction = ACTION_COUNT - 1;
    }
    needsRefresh = true;
    Serial.printf("[KITTALIEN] Selected action: %s\n", actionNames[selectedAction]);
  }
  
  // Navigate down in menu
  if (downPressed) {
    selectedAction++;
    if (selectedAction >= ACTION_COUNT) {
      selectedAction = 0;
    }
    needsRefresh = true;
    Serial.printf("[KITTALIEN] Selected action: %s\n", actionNames[selectedAction]);
  }
  
  // Execute action
  if (okPressed) {
    executeAction((KittalienNS::Action)selectedAction);
  }
  
  // Exit is handled by main loop
}

// ==================== UPDATE LOOP ====================

void kittalienUpdate() {
  using namespace KittalienNS;
  
  // Hide message after timeout
  if (showingMessage) {
    unsigned long currentTime = millis();
    
    // Handle millis() overflow
    if (currentTime < messageDisplayTime) {
      messageDisplayTime = currentTime;
    }
    
    if (currentTime - messageDisplayTime >= KITTALIEN_UI_TIMEOUT) {
      showingMessage = false;
      needsRefresh = true;
      Serial.println("[KITTALIEN] Hiding status message");
    }
  }
  
  // Redraw if needed
  if (needsRefresh) {
    kittalienDrawScreen();
  }
}

// ==================== PUBLIC INTERFACE ====================

void kittalienInit() {
  using namespace KittalienNS;
  
  Serial.println("[KITTALIEN] Initializing Kittalien mode...");
  
  // Initialize display
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  
  // Show loading screen
  EPD_ShowString(280, 120, "Loading Kittalien...", 24, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  // Load or create pet state
  loadPetState();
  
  // Apply time decay
  applyTimeDecay();
  
  // Update pet state based on stats
  updatePetState();
  
  // Reset UI state
  selectedAction = 0;
  needsRefresh = true;
  showingMessage = false;
  
  // Show welcome message for new pets
  if (pet.isNewPet) {
    statusMessage = "Welcome to Kittalien!";
    showingMessage = true;
    messageDisplayTime = millis();
    pet.isNewPet = false;
    savePetState();
  }
  
  // Draw initial screen
  kittalienDrawScreen();
  
  Serial.println("[KITTALIEN] Initialization complete");
  Serial.printf("[KITTALIEN] Stats - H:%d, Ha:%d, E:%d\n", 
                pet.hunger, pet.happiness, pet.energy);
}

void kittalienCleanup() {
  using namespace KittalienNS;
  
  Serial.println("[KITTALIEN] Cleaning up...");
  
  // Final save before exit
  savePetState();
}

// ==================== DEBUG/CHEAT FUNCTIONS ====================

// For testing: reset pet to default state
void kittalienResetPet() {
  using namespace KittalienNS;
  
  Serial.println("[KITTALIEN] RESET: Creating new pet");
  pet.hunger = 80;
  pet.happiness = 80;
  pet.energy = 80;
  pet.lastUpdateTime = getCurrentTimeSeconds();
  pet.isNewPet = true;
  savePetState();
  needsRefresh = true;
}

// For testing: get current stats
void kittalienPrintStats() {
  using namespace KittalienNS;
  
  Serial.println("========== KITTALIEN STATS ==========");
  Serial.printf("Hunger:    %d / %d\n", pet.hunger, KITTALIEN_MAX_STAT);
  Serial.printf("Happiness: %d / %d\n", pet.happiness, KITTALIEN_MAX_STAT);
  Serial.printf("Energy:    %d / %d\n", pet.energy, KITTALIEN_MAX_STAT);
  Serial.printf("State:     %d\n", (int)pet.currentState);
  Serial.printf("Last Time: %lu seconds\n", pet.lastUpdateTime);
  Serial.println("====================================");
}

#endif // KITTALIEN_H
