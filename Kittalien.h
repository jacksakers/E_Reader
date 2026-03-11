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
#define KITTALIEN_HUNGER_DECAY_RATE 10     // Hunger decreases by 5 per hour
#define KITTALIEN_HAPPINESS_DECAY_RATE 5  // Happiness decreases by 3 per hour
#define KITTALIEN_ENERGY_DECAY_RATE 10     // Energy decreases by 2 per hour

// Action effects
#define KITTALIEN_FEED_AMOUNT 10
#define KITTALIEN_PLAY_HAPPINESS 15
#define KITTALIEN_PLAY_ENERGY_COST 20
#define KITTALIEN_PET_HAPPINESS 10

// Energy mechanics
#define KITTALIEN_CRAZY_THRESHOLD 85      // Energy above this = crazy (player must play)
#define KITTALIEN_SLEEP_THRESHOLD 30      // Energy below this = auto sleep
#define KITTALIEN_WAKEUP_THRESHOLD 60     // Energy above this = wake up
#define KITTALIEN_SLEEP_REGEN_RATE 30     // Energy regained per hour while sleeping

// UI Configuration
#define KITTALIEN_UI_TIMEOUT 5000         // Show action result for 5 seconds
#define KITTALIEN_ACTION_IMAGE_TIMEOUT 10000  // Show action image for 10 seconds
#define KITTALIEN_MAX_NAME_LENGTH 16           // Max length of pet name (e.g. "Ka-ray")

// Reference to global display buffer from OS_Main.ino
extern uint8_t ImageBW[27200];

// ==================== KITTALIEN STATE ====================
namespace KittalienNS {
  
  // Visual states based on stats
  enum PetState {
    STATE_SICK,      // Hunger < 20
    STATE_SAD,       // Happiness < 30
    STATE_SLEEPING,  // Auto-sleep when energy < KITTALIEN_SLEEP_THRESHOLD
    STATE_HAPPY,     // Happiness > 70 && Hunger > 50
    STATE_NEUTRAL,   // Default
    STATE_CRAZY      // Energy > KITTALIEN_CRAZY_THRESHOLD (must play)
  };
  
  // Available actions
  enum Action {
    ACTION_FEED,
    ACTION_PLAY,
    ACTION_PET,
    ACTION_COUNT
  };
  
  // Pet stats and state
  struct PetStats {
    int hunger;           // 0-100
    int happiness;        // 0-100
    int energy;           // 0-100
    unsigned long lastUpdateTime;  // Timestamp in seconds since epoch
    PetState currentState;
    char name[KITTALIEN_MAX_NAME_LENGTH];  // Randomized pet name
    bool isNewPet;        // First time initialization
    bool isSleeping;      // True when kittalien is auto-sleeping
  };
  
  // Game state
  static PetStats pet;
  static int selectedAction = 0;
  static bool needsRefresh = true;
  static String statusMessage = "";
  static unsigned long messageDisplayTime = 0;
  static bool showingMessage = false;

  // Action image state
  static String actionImage = "";
  static bool showingActionImage = false;
  static unsigned long actionImageStartTime = 0;
  
  // Art filenames for different states
  static const char* stateImages[] = {
    "sick_kittalien.art",      // STATE_SICK
    "sad_kittalien.art",       // STATE_SAD
    "sleeping_kittalien.art",  // STATE_SLEEPING
    "happy_kittalien.art",     // STATE_HAPPY
    "neutral_kittalien.art",   // STATE_NEUTRAL
    "crazy_kittalien.art"      // STATE_CRAZY
  };
  
  // Action names
  static const char* actionNames[] = {
    "Feed",
    "Play",
    "Pet"
  };
  
  // Action descriptions
  static const char* actionDescriptions[] = {
    "Give food (+Hunger)",
    "Play game (+Happy, -Energy)",
    "Pet gently (+Happy)"
  };
  
  // Name generation syllables
  static const char* nameFirstParts[] = {
    "Qu", "Ka", "No", "Zu", "Mi", "Bo", "Re", "Ty", "Xa", "Pi",
    "Lu", "Fo", "Di", "Ve", "Co", "Wa", "Na", "Ge", "Ru", "Se"
  };
  static const char* nameSecondParts[] = {
    "ade", "ray", "jo", "kin", "lix", "mur", "pro", "zel", "via", "dex",
    "sol", "fin", "mox", "ren", "tis", "vor", "bix", "dun", "pan", "sky"
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

// ==================== NAME GENERATION ====================

void generateKittalienName(char* outName, size_t maxLen) {
  using namespace KittalienNS;
  int firstIdx = random(0, 20);
  int secondIdx = random(0, 20);
  snprintf(outName, maxLen, "%s-%s", nameFirstParts[firstIdx], nameSecondParts[secondIdx]);
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
  file.printf("isSleeping=%d\n", pet.isSleeping ? 1 : 0);
  file.printf("name=%s\n", pet.name);
  
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
    pet.isSleeping = false;
    generateKittalienName(pet.name, KITTALIEN_MAX_NAME_LENGTH);
    
    return savePetState();
  }
  
  // Open save file
  File file = SD.open(KITTALIEN_SAVE_FILE, FILE_READ);
  if (!file) {
    Serial.println("[KITTALIEN] ERROR: Failed to open save file");
    return false;
  }
  
  // Initialize name in case it is absent from older save files
  pet.name[0] = '\0';

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
      } else if (strcmp(key, "isSleeping") == 0) {
        pet.isSleeping = (atoi(value) == 1);
      } else if (strcmp(key, "name") == 0) {
        strncpy(pet.name, value, KITTALIEN_MAX_NAME_LENGTH - 1);
        pet.name[KITTALIEN_MAX_NAME_LENGTH - 1] = '\0';
      }
    }
  }
  
  file.close();

  // If name was absent from an older save file, generate one now
  if (pet.name[0] == '\0') {
    generateKittalienName(pet.name, KITTALIEN_MAX_NAME_LENGTH);
    Serial.printf("[KITTALIEN] Generated name for existing pet: %s\n", pet.name);
  }
  
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

    // simulate some decay assuming at least 4 hours passed
    pet.hunger -= KITTALIEN_HUNGER_DECAY_RATE * 4;
    pet.happiness -= KITTALIEN_HAPPINESS_DECAY_RATE * 4; 

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

  pet.hunger -= hungerDecay;
  pet.happiness -= happinessDecay;

  // Energy regenerates while sleeping, decays while awake
  if (pet.isSleeping) {
    int energyRegen = (int)(hoursPassed * KITTALIEN_SLEEP_REGEN_RATE);
    pet.energy += energyRegen;
  } else {
    int energyDecay = (int)(hoursPassed * KITTALIEN_ENERGY_DECAY_RATE);
    pet.energy -= energyDecay;
  }

  // Clamp stats
  pet.hunger = clamp(pet.hunger, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
  pet.happiness = clamp(pet.happiness, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
  pet.energy = clamp(pet.energy, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);

  // Update timestamp
  pet.lastUpdateTime = currentTime;

  Serial.printf("[KITTALIEN] After decay: H=%d, Ha=%d, E=%d (sleeping=%d)\n",
                pet.hunger, pet.happiness, pet.energy, pet.isSleeping ? 1 : 0);
  
  // Save immediately after decay
  savePetState();
}

// ==================== STATE DETERMINATION ====================

void updatePetState() {
  using namespace KittalienNS;
  
  PetState oldState = pet.currentState;
  bool wasSleeping = pet.isSleeping;

  // Auto-sleep when energy too low
  if (!pet.isSleeping && pet.energy < KITTALIEN_SLEEP_THRESHOLD) {
    pet.isSleeping = true;
    Serial.println("[KITTALIEN] Fell asleep (low energy)");
  }

  // Wake up when energy is restored
  if (pet.isSleeping && pet.energy >= KITTALIEN_WAKEUP_THRESHOLD) {
    pet.isSleeping = false;
    Serial.println("[KITTALIEN] Woke up (energy restored)");
  }

  // Determine state based on stats (priority order)
  if (pet.isSleeping) {
    pet.currentState = STATE_SLEEPING;
  } else if (pet.energy > KITTALIEN_CRAZY_THRESHOLD) {
    pet.currentState = STATE_CRAZY;
  } else if (pet.hunger < 20) {
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

  // Save if sleeping state changed
  if (pet.isSleeping != wasSleeping) {
    savePetState();
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
      actionImage = "eating_kittalien.art";
      showingActionImage = true;
      actionImageStartTime = millis();
      break;

    case ACTION_PLAY:
      if (pet.isSleeping) {
        statusMessage = "Kittalien is asleep!";
      } else if (pet.energy < KITTALIEN_PLAY_ENERGY_COST) {
        statusMessage = "Too tired to play!";
      } else {
        pet.happiness += KITTALIEN_PLAY_HAPPINESS;
        pet.energy -= KITTALIEN_PLAY_ENERGY_COST;
        pet.happiness = clamp(pet.happiness, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
        pet.energy = clamp(pet.energy, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
        statusMessage = "Played fun game! (+Happy, -Energy)";
        actionImage = "playing_kittalien.art";
        showingActionImage = true;
        actionImageStartTime = millis();
      }
      break;

    case ACTION_PET:
      pet.happiness += KITTALIEN_PET_HAPPINESS;
      pet.happiness = clamp(pet.happiness, KITTALIEN_MIN_STAT, KITTALIEN_MAX_STAT);
      statusMessage = "Petted softly! (+Happy)";
      actionImage = "pet_kittalien.art";
      showingActionImage = true;
      actionImageStartTime = millis();
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

bool loadArtFile(const char* filename) {
  String filepath = "/art/game_art/" + String(filename);
  Serial.printf("[KITTALIEN] Loading image: %s\n", filepath.c_str());

  if (!SD.exists(filepath)) {
    Serial.printf("[KITTALIEN] Image not found: %s\n", filepath.c_str());
    return false;
  }

  File artFile = SD.open(filepath, FILE_READ);
  if (!artFile) {
    Serial.println("[KITTALIEN] Failed to open image file");
    return false;
  }

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

bool loadPetImage(KittalienNS::PetState state) {
  using namespace KittalienNS;

  if (!loadArtFile(stateImages[(int)state])) {
    if (state != STATE_NEUTRAL) {
      Serial.println("[KITTALIEN] Falling back to neutral state");
      return loadArtFile(stateImages[(int)STATE_NEUTRAL]);
    }
    return false;
  }
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
  bool imageLoaded = false;
  if (showingActionImage && actionImage.length() > 0) {
    imageLoaded = loadArtFile(actionImage.c_str());
    if (!imageLoaded) {
      Serial.printf("[KITTALIEN] Action image not found: %s\n", actionImage.c_str());
    }
  }
  if (!imageLoaded) {
    imageLoaded = loadPetImage(pet.currentState);
  }
  if (!imageLoaded) {
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
  EPD_ShowString(320, 5, (char*)pet.name, 24, BLACK);
  
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

  unsigned long currentTime = millis();

  // Hide message after timeout
  if (showingMessage) {
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

  // Hide action image after timeout
  if (showingActionImage) {
    // Handle millis() overflow
    if (currentTime < actionImageStartTime) {
      actionImageStartTime = currentTime;
    }
    if (currentTime - actionImageStartTime >= KITTALIEN_ACTION_IMAGE_TIMEOUT) {
      showingActionImage = false;
      needsRefresh = true;
      Serial.println("[KITTALIEN] Hiding action image");
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
  showingActionImage = false;
  
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
  generateKittalienName(pet.name, KITTALIEN_MAX_NAME_LENGTH);
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
  Serial.printf("Sleeping:  %s\n", pet.isSleeping ? "yes" : "no");
  Serial.printf("Last Time: %lu seconds\n", pet.lastUpdateTime);
  Serial.println("====================================");
}

const char* kittalienGetName() {
  using namespace KittalienNS;
  if (pet.name[0] == '\0') {
    loadPetState();
  }
  return pet.name;
}

void kittalienRandomizeName() {
  using namespace KittalienNS;
  generateKittalienName(pet.name, KITTALIEN_MAX_NAME_LENGTH);
  savePetState();
  needsRefresh = true;
  Serial.printf("[KITTALIEN] New name: %s\n", pet.name);
}

#endif // KITTALIEN_H
