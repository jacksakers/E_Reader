#ifndef PERSISTENTSTORAGE_H
#define PERSISTENTSTORAGE_H

#include "SD.h"
#include <Arduino.h>

// ==================== PERSISTENT STORAGE CONFIGURATION ====================
#define PROGRESS_FILE "/ereader_progress.txt"
#define MAX_PROGRESS_ENTRIES 100
#define MAX_FILENAME_LENGTH 64

// ==================== PROGRESS STORAGE ====================
namespace PersistentStorageNS {
  struct BookProgress {
    char filename[MAX_FILENAME_LENGTH];
    int currentLine;
    int totalLines;
    int percentage;
    unsigned long lastUpdated;  // Timestamp
  };
  
  // In-memory cache of progress data
  static BookProgress progressCache[MAX_PROGRESS_ENTRIES];
  static int progressCount = 0;
  static bool cacheLoaded = false;
}

// ==================== HELPER FUNCTIONS ====================

// Trim whitespace from a string
void trimString(char* str) {
  if (str == NULL) return;
  
  // Trim leading spaces
  int start = 0;
  while (str[start] == ' ' || str[start] == '\t' || str[start] == '\r' || str[start] == '\n') {
    start++;
  }
  
  // Trim trailing spaces
  int end = strlen(str) - 1;
  while (end >= 0 && (str[end] == ' ' || str[end] == '\t' || str[end] == '\r' || str[end] == '\n')) {
    str[end] = '\0';
    end--;
  }
  
  // Shift string if needed
  if (start > 0) {
    int i = 0;
    while (str[start] != '\0') {
      str[i++] = str[start++];
    }
    str[i] = '\0';
  }
}

// Simple JSON-like parser for our progress file
bool parseProgressLine(const char* line, PersistentStorageNS::BookProgress* progress) {
  if (line == NULL || progress == NULL || strlen(line) < 10) {
    return false;
  }
  
  // Expected format: {"filename":"book.txt","line":150,"total":500,"percent":30,"time":12345}
  // We'll do a simple manual parse for reliability
  
  char tempLine[256];
  strncpy(tempLine, line, sizeof(tempLine) - 1);
  tempLine[sizeof(tempLine) - 1] = '\0';
  
  // Find filename
  char* filenameStart = strstr(tempLine, "\"filename\":\"");
  if (filenameStart == NULL) return false;
  filenameStart += 12; // Skip past "filename":"
  
  char* filenameEnd = strchr(filenameStart, '\"');
  if (filenameEnd == NULL) return false;
  
  int filenameLen = filenameEnd - filenameStart;
  if (filenameLen >= MAX_FILENAME_LENGTH) filenameLen = MAX_FILENAME_LENGTH - 1;
  strncpy(progress->filename, filenameStart, filenameLen);
  progress->filename[filenameLen] = '\0';
  
  // Find line number
  char* lineStart = strstr(tempLine, "\"line\":");
  if (lineStart == NULL) return false;
  progress->currentLine = atoi(lineStart + 7);
  
  // Find total lines
  char* totalStart = strstr(tempLine, "\"total\":");
  if (totalStart == NULL) return false;
  progress->totalLines = atoi(totalStart + 8);
  
  // Find percentage
  char* percentStart = strstr(tempLine, "\"percent\":");
  if (percentStart == NULL) return false;
  progress->percentage = atoi(percentStart + 10);
  
  // Find timestamp (optional)
  char* timeStart = strstr(tempLine, "\"time\":");
  if (timeStart != NULL) {
    progress->lastUpdated = atol(timeStart + 7);
  } else {
    progress->lastUpdated = 0;
  }
  
  return true;
}

// ==================== LOAD PROGRESS DATA ====================

bool loadProgressData() {
  using namespace PersistentStorageNS;
  
  Serial.println("[STORAGE] Loading progress data from SD card...");
  
  // Reset cache
  progressCount = 0;
  memset(progressCache, 0, sizeof(progressCache));
  
  // Check if file exists
  if (!SD.exists(PROGRESS_FILE)) {
    Serial.println("[STORAGE] Progress file does not exist yet (will be created on first save)");
    cacheLoaded = true;
    return true;
  }
  
  // Open progress file
  File file = SD.open(PROGRESS_FILE, FILE_READ);
  if (!file) {
    Serial.println("[STORAGE] ERROR: Failed to open progress file for reading");
    return false;
  }
  
  Serial.printf("[STORAGE] Progress file size: %d bytes\n", file.size());
  
  // Read line by line
  char line[256];
  int lineNum = 0;
  
  while (file.available() && progressCount < MAX_PROGRESS_ENTRIES) {
    int bytesRead = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[bytesRead] = '\0';
    lineNum++;
    
    if (bytesRead > 10) {  // Skip empty lines
      trimString(line);
      
      if (strlen(line) > 0 && parseProgressLine(line, &progressCache[progressCount])) {
        Serial.printf("[STORAGE] Loaded: %s -> Line %d/%d (%d%%)\n", 
                      progressCache[progressCount].filename,
                      progressCache[progressCount].currentLine,
                      progressCache[progressCount].totalLines,
                      progressCache[progressCount].percentage);
        progressCount++;
      } else {
        Serial.printf("[STORAGE] Warning: Could not parse line %d: %s\n", lineNum, line);
      }
    }
  }
  
  file.close();
  
  Serial.printf("[STORAGE] Loaded %d progress entries\n", progressCount);
  cacheLoaded = true;
  return true;
}

// ==================== SAVE PROGRESS DATA ====================

bool saveProgressData() {
  using namespace PersistentStorageNS;
  
  Serial.println("[STORAGE] Saving progress data to SD card...");
  
  // Delete old file
  if (SD.exists(PROGRESS_FILE)) {
    SD.remove(PROGRESS_FILE);
  }
  
  // Open file for writing
  File file = SD.open(PROGRESS_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("[STORAGE] ERROR: Failed to open progress file for writing");
    return false;
  }
  
  // Write each progress entry as a JSON line
  for (int i = 0; i < progressCount; i++) {
    char line[256];
    snprintf(line, sizeof(line), 
             "{\"filename\":\"%s\",\"line\":%d,\"total\":%d,\"percent\":%d,\"time\":%lu}\n",
             progressCache[i].filename,
             progressCache[i].currentLine,
             progressCache[i].totalLines,
             progressCache[i].percentage,
             progressCache[i].lastUpdated);
    
    file.print(line);
  }
  
  file.close();
  
  Serial.printf("[STORAGE] Saved %d progress entries (%d bytes)\n", progressCount, file.size());
  return true;
}

// ==================== GET BOOK PROGRESS ====================

bool getBookProgress(const String& filename, int* outLine, int* outTotal, int* outPercent) {
  using namespace PersistentStorageNS;
  
  // Load cache if not loaded
  if (!cacheLoaded) {
    loadProgressData();
  }
  
  // Search for matching filename
  for (int i = 0; i < progressCount; i++) {
    if (strcmp(progressCache[i].filename, filename.c_str()) == 0) {
      if (outLine != NULL) *outLine = progressCache[i].currentLine;
      if (outTotal != NULL) *outTotal = progressCache[i].totalLines;
      if (outPercent != NULL) *outPercent = progressCache[i].percentage;
      
      Serial.printf("[STORAGE] Found progress for '%s': Line %d/%d (%d%%)\n", 
                    filename.c_str(), progressCache[i].currentLine, 
                    progressCache[i].totalLines, progressCache[i].percentage);
      return true;
    }
  }
  
  Serial.printf("[STORAGE] No saved progress found for '%s'\n", filename.c_str());
  return false;
}

// ==================== SET BOOK PROGRESS ====================

bool setBookProgress(const String& filename, int currentLine, int totalLines, bool saveImmediately = false) {
  using namespace PersistentStorageNS;
  
  // Load cache if not loaded
  if (!cacheLoaded) {
    loadProgressData();
  }
  
  // Calculate percentage
  int percentage = (totalLines > 0) ? (currentLine * 100 / totalLines) : 0;
  
  // Search for existing entry
  int existingIndex = -1;
  for (int i = 0; i < progressCount; i++) {
    if (strcmp(progressCache[i].filename, filename.c_str()) == 0) {
      existingIndex = i;
      break;
    }
  }
  
  // Update or create entry
  PersistentStorageNS::BookProgress* entry = NULL;
  
  if (existingIndex >= 0) {
    // Update existing
    entry = &progressCache[existingIndex];
  } else if (progressCount < MAX_PROGRESS_ENTRIES) {
    // Create new
    entry = &progressCache[progressCount];
    progressCount++;
  } else {
    Serial.println("[STORAGE] ERROR: Progress cache is full!");
    return false;
  }
  
  // Set values
  strncpy(entry->filename, filename.c_str(), MAX_FILENAME_LENGTH - 1);
  entry->filename[MAX_FILENAME_LENGTH - 1] = '\0';
  entry->currentLine = currentLine;
  entry->totalLines = totalLines;
  entry->percentage = percentage;
  entry->lastUpdated = millis();
  
  Serial.printf("[STORAGE] Updated progress: %s -> Line %d/%d (%d%%)\n", 
                filename.c_str(), currentLine, totalLines, percentage);
  
  // Optionally save immediately
  if (saveImmediately) {
    return saveProgressData();
  }
  
  return true;
}

// ==================== CLEAR BOOK PROGRESS ====================

bool clearBookProgress(const String& filename) {
  using namespace PersistentStorageNS;
  
  // Load cache if not loaded
  if (!cacheLoaded) {
    loadProgressData();
  }
  
  // Find and remove entry
  for (int i = 0; i < progressCount; i++) {
    if (strcmp(progressCache[i].filename, filename.c_str()) == 0) {
      // Shift all entries after this one
      for (int j = i; j < progressCount - 1; j++) {
        progressCache[j] = progressCache[j + 1];
      }
      progressCount--;
      
      Serial.printf("[STORAGE] Cleared progress for '%s'\n", filename.c_str());
      return saveProgressData();
    }
  }
  
  Serial.printf("[STORAGE] No progress found to clear for '%s'\n", filename.c_str());
  return false;
}

// ==================== INITIALIZE STORAGE ====================

void initializePersistentStorage() {
  using namespace PersistentStorageNS;
  
  Serial.println("[STORAGE] Initializing persistent storage...");
  
  // Load existing progress data
  if (!loadProgressData()) {
    Serial.println("[STORAGE] Warning: Failed to load progress data");
  }
  
  Serial.println("[STORAGE] Persistent storage ready");
}

#endif // PERSISTENTSTORAGE_H
