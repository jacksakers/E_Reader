#ifndef ARTFRAME_H
#define ARTFRAME_H

#include "EPD.h"
#include "SD.h"
#include <vector>

// Forward declarations for settings
extern int settingsGetArtCycleSeconds();
extern bool settingsGetArtAutoCycleEnabled();

// ==================== ART FRAME CONFIGURATION ====================
#define ARTFRAME_MAX_IMAGES 100        // Maximum number of art images to track
#define ARTFRAME_DISPLAY_WIDTH 792     // Actual display width
#define ARTFRAME_DISPLAY_HEIGHT 272    // Display height
#define ARTFRAME_UI_TIMEOUT 3000       // Hide UI after 3 seconds of inactivity

// Reference to global display buffer from OS_Main.ino
extern uint8_t ImageBW[27200];

// ==================== ART FRAME STATE ====================
namespace ArtFrameNS {
  // Art collection management
  static std::vector<String> artFiles;
  static int currentArtIndex = 0;
  static int totalArtCount = 0;
  
  // Timing
  static unsigned long lastCycleTime = 0;
  static bool autoCycleEnabled = true;
  
  // UI state
  static bool needsRefresh = true;
  static bool showUIOverlay = true;
  static unsigned long lastInteractionTime = 0;
  static String statusMessage = "";
  static String artPath = "/art";
}

// ==================== FILE MANAGEMENT ====================

// Scan the /art folder for image files
void artFrameScanFiles() {
  using namespace ArtFrameNS;
  
  Serial.println("[ARTFRAME] Scanning for art files...");
  artFiles.clear();
  
  // Check if art directory exists
  if (!SD.exists(artPath)) {
    Serial.println("[ARTFRAME] Creating /art directory...");
    SD.mkdir(artPath);
    statusMessage = "No art found. Upload via Web Portal!";
    totalArtCount = 0;
    return;
  }
  
  // Open art directory
  File artDir = SD.open(artPath);
  if (!artDir || !artDir.isDirectory()) {
    Serial.println("[ARTFRAME] Failed to open art directory");
    statusMessage = "Error: Cannot open /art folder";
    totalArtCount = 0;
    return;
  }
  
  // Scan for .art files (raw bitmap format)
  File entry;
  while ((entry = artDir.openNextFile()) && artFiles.size() < ARTFRAME_MAX_IMAGES) {
    String filename = entry.name();
    
    // Only accept .art files (raw bitmap format for e-ink)
    if (!entry.isDirectory() && filename.endsWith(".art")) {
      artFiles.push_back(filename);
      Serial.printf("[ARTFRAME] Found: %s\n", filename.c_str());
    }
    entry.close();
  }
  artDir.close();
  
  totalArtCount = artFiles.size();
  Serial.printf("[ARTFRAME] Found %d art file(s)\n", totalArtCount);
  
  if (totalArtCount == 0) {
    statusMessage = "No .art files found. Upload via Web Portal!";
  } else {
    statusMessage = String(totalArtCount) + " art files found";
  }
}

// ==================== IMAGE LOADING & DISPLAY ====================

// Load and display an art file
bool artFrameLoadAndDisplay(int index) {
  using namespace ArtFrameNS;
  
  if (index < 0 || index >= totalArtCount) {
    Serial.println("[ARTFRAME] Invalid index");
    return false;
  }
  
  String filepath = artPath + "/" + artFiles[index];
  Serial.printf("[ARTFRAME] Loading: %s\n", filepath.c_str());
  
  File artFile = SD.open(filepath, FILE_READ);
  if (!artFile) {
    Serial.println("[ARTFRAME] Failed to open file");
    statusMessage = "Error loading art";
    return false;
  }
  
  // Get file size
  size_t fileSize = artFile.size();
  Serial.printf("[ARTFRAME] File size: %d bytes\n", fileSize);
  
  // Check if file size matches expected buffer size
  // Expected: 27200 bytes for 800x272 display (or similar)
  if (fileSize < 20000 || fileSize > 30000) {
    Serial.printf("[ARTFRAME] Warning: Unexpected file size %d bytes\n", fileSize);
    // Continue anyway, but be careful
  }
  
  // Read directly into display buffer
  size_t bytesToRead = min(fileSize, (size_t)27200);
  size_t bytesRead = artFile.read(ImageBW, bytesToRead);
  artFile.close();
  
  if (bytesRead != bytesToRead) {
    Serial.printf("[ARTFRAME] Read error: expected %d, got %d bytes\n", bytesToRead, bytesRead);
    statusMessage = "Error reading art";
    return false;
  }
  
  Serial.printf("[ARTFRAME] Successfully loaded %d bytes\n", bytesRead);
  currentArtIndex = index;
  needsRefresh = true;
  return true;
}

// Draw the art with UI overlay
void artFrameDrawScreen() {
  using namespace ArtFrameNS;
  
  if (!needsRefresh) return;
  
  // Clear the display buffer first
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  
  if (totalArtCount == 0) {
    // No art available - show message
    Paint_Clear(WHITE);
    
    // Title
    EPD_DrawRectangle(0, 0, 791, 50, BLACK, 1);
    EPD_ShowString(250, 15, "ART FRAME MODE", 24, WHITE);
    
    // Instructions
    EPD_ShowString(150, 100, "No artwork found!", 24, BLACK);
    EPD_ShowString(100, 140, "1. Enter Web Portal mode", 16, BLACK);
    EPD_ShowString(100, 160, "2. Upload .art files to /art folder", 16, BLACK);
    EPD_ShowString(100, 180, "3. Return here to view", 16, BLACK);
    
    // Footer
    EPD_ShowString(250, 240, "Press EXIT to return", 16, BLACK);
    
  } else {
    // Art is already loaded in ImageBW buffer from artFrameLoadAndDisplay
    // Just add UI overlay with minimal text
    
    if (showUIOverlay) {
      // Top bar with art info (semi-transparent effect by using rectangles)
      EPD_DrawRectangle(0, 0, 791, 30, BLACK, 0);
      EPD_DrawRectangle(0, 1, 791, 29, BLACK, 0);
      
      // Art counter
      char infoText[50];
      snprintf(infoText, sizeof(infoText), "%d / %d", currentArtIndex + 1, totalArtCount);
      EPD_ShowString(10, 8, infoText, 16, BLACK);
      
      // Art filename (truncated)
      String shortName = artFiles[currentArtIndex];
      if (shortName.length() > 50) {
        shortName = shortName.substring(0, 47) + "...";
      }
      EPD_ShowString(150, 8, shortName.c_str(), 16, BLACK);
      
      // Bottom bar with controls
      EPD_DrawRectangle(0, 242, 791, 271, BLACK, 0);
      EPD_DrawRectangle(0, 243, 791, 270, BLACK, 0);
      
      EPD_ShowString(10, 248, "UP/DN: Navigate", 16, BLACK);
      
      if (autoCycleEnabled) {
        EPD_ShowString(250, 248, "Auto-cycle: ON", 16, BLACK);
      } else {
        EPD_ShowString(250, 248, "Auto-cycle: OFF", 16, BLACK);
      }
      
      EPD_ShowString(550, 248, "EXIT: Return", 16, BLACK);
    }
  }
  
  // Display on e-ink
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  
  needsRefresh = false;
}

// ==================== NAVIGATION ====================

void artFrameNextArt() {
  using namespace ArtFrameNS;
  
  if (totalArtCount == 0) return;
  
  currentArtIndex = (currentArtIndex + 1) % totalArtCount;
  artFrameLoadAndDisplay(currentArtIndex);
  lastCycleTime = millis(); // Reset timer
}

void artFramePreviousArt() {
  using namespace ArtFrameNS;
  
  if (totalArtCount == 0) return;
  
  currentArtIndex--;
  if (currentArtIndex < 0) {
    currentArtIndex = totalArtCount - 1;
  }
  artFrameLoadAndDisplay(currentArtIndex);
  lastCycleTime = millis(); // Reset timer
}

void artFrameToggleAutoCycle() {
  using namespace ArtFrameNS;
  autoCycleEnabled = !autoCycleEnabled;
  needsRefresh = true;
  Serial.printf("[ARTFRAME] Auto-cycle: %s\n", autoCycleEnabled ? "ON" : "OFF");
}

// ==================== AUTO-CYCLING ====================

void artFrameUpdateCycle() {
  using namespace ArtFrameNS;
  
  if (!autoCycleEnabled || totalArtCount <= 1) return;
  
  unsigned long currentTime = millis();
  
  // Check for timer overflow (happens every ~49 days)
  if (currentTime < lastCycleTime) {
    lastCycleTime = currentTime;
    return;
  }
  
  // Get cycle interval from settings (convert seconds to milliseconds)
  unsigned long cycleInterval = settingsGetArtCycleSeconds() * 1000UL;
  
  // Check if it's time to cycle
  if (currentTime - lastCycleTime >= cycleInterval) {
    Serial.println("[ARTFRAME] Auto-cycling to next art...");
    artFrameNextArt();
    lastCycleTime = currentTime;
  }
}

// ==================== PUBLIC INTERFACE ====================

void artFrameInit() {
  using namespace ArtFrameNS;
  
  Serial.println("[ARTFRAME] Initializing Art Frame mode...");
  
  // Initialize display
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  
  // Scan for art files
  artFrameScanFiles();
  
  // Load first art if available
  if (totalArtCount > 0) {
    artFrameLoadAndDisplay(0);
  }
  
  // Initialize timing
  lastCycleTime = millis();
  lastInteractionTime = millis();
  autoCycleEnabled = settingsGetArtAutoCycleEnabled();
  showUIOverlay = true;
  needsRefresh = true;
  
  // Show initial display
  artFrameDrawScreen();
  
  Serial.println("[ARTFRAME] Initialization complete");
}

void artFrameUpdate() {
  using namespace ArtFrameNS;
  
  // Handle auto-cycling
  artFrameUpdateCycle();
  
  // Check if UI should be hidden after timeout
  unsigned long currentTime = millis();
  if (showUIOverlay && totalArtCount > 0) {
    // Handle timer overflow
    if (currentTime < lastInteractionTime) {
      lastInteractionTime = currentTime;
    }
    
    // Hide UI after timeout
    if (currentTime - lastInteractionTime >= ARTFRAME_UI_TIMEOUT) {
      showUIOverlay = false;
      needsRefresh = true;
      Serial.println("[ARTFRAME] Hiding UI overlay");
    }
  }
  
  // Redraw if needed
  if (needsRefresh) {
    artFrameDrawScreen();
  }
}

void artFrameHandleInput(bool upPressed, bool downPressed, bool okPressed, bool exitPressed) {
  using namespace ArtFrameNS;
  
  // Show UI on any button press
  if (upPressed || downPressed || okPressed || exitPressed) {
    if (!showUIOverlay) {
      showUIOverlay = true;
      needsRefresh = true;
      Serial.println("[ARTFRAME] Showing UI overlay");
    }
    lastInteractionTime = millis();
  }
  
  if (upPressed) {
    Serial.println("[ARTFRAME] Previous art");
    artFramePreviousArt();
  }
  
  if (downPressed) {
    Serial.println("[ARTFRAME] Next art");
    artFrameNextArt();
  }
  
  if (okPressed) {
    // Toggle auto-cycle
    artFrameToggleAutoCycle();
  }
  
  // exitPressed is handled by the main loop to return to home
}

void artFrameCleanup() {
  using namespace ArtFrameNS;
  
  Serial.println("[ARTFRAME] Cleaning up...");
  artFiles.clear();
  currentArtIndex = 0;
  totalArtCount = 0;
}

// ==================== HELPER FUNCTIONS ====================

// Check if art folder has files (for web portal integration)
int artFrameGetFileCount() {
  return ArtFrameNS::totalArtCount;
}

// Refresh file list (call after uploading new art via web portal)
void artFrameRefresh() {
  using namespace ArtFrameNS;
  Serial.println("[ARTFRAME] Refreshing art list...");
  artFrameScanFiles();
  
  if (totalArtCount > 0 && currentArtIndex >= totalArtCount) {
    currentArtIndex = 0;
  }
  
  if (totalArtCount > 0) {
    artFrameLoadAndDisplay(currentArtIndex);
  } else {
    needsRefresh = true;
  }
}

#endif // ARTFRAME_H
