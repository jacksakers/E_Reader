#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

// ==================== BATTERY MONITORING MODULE ====================
// This module provides battery voltage reading and percentage calculation
// for LiPo batteries connected to ESP32-S3 via a voltage divider.
//
// QUICK START:
// 1. Verify BATTERY_ADC_PIN matches your hardware (default: GPIO 3)
// 2. Measure your voltage divider ratio and update VOLTAGE_DIVIDER_RATIO
// 3. Call batteryInit() in setup()
// 4. Call batteryUpdate() in loop()
// 5. Check Serial Monitor for calibration info
//
// For detailed setup and calibration, see: design_docs/Battery_README.md
// ====================================================================

// ==================== BATTERY CONFIGURATION ====================
// GPIO pin for battery voltage reading (through voltage divider)
#define BATTERY_ADC_PIN 3

// Voltage divider ratio (typically 2:1 for LiPo batteries on ESP32)
// If your board has a different ratio, adjust this value
#define VOLTAGE_DIVIDER_RATIO 2.0

// ADC configuration for ESP32-S3
#define ADC_RESOLUTION 4096.0  // 12-bit ADC
#define ADC_REFERENCE_VOLTAGE 3.3  // ESP32-S3 ADC reference voltage

// LiPo battery voltage thresholds (in volts)
#define BATTERY_MAX_VOLTAGE 4.2   // Fully charged
#define BATTERY_MIN_VOLTAGE 3.0   // Empty (safety cutoff)
#define BATTERY_NOMINAL_VOLTAGE 3.7  // Nominal voltage

// Number of samples for averaging (reduces noise)
#define BATTERY_SAMPLE_COUNT 10

// Update interval (milliseconds) - don't read too frequently
#define BATTERY_UPDATE_INTERVAL 60000  // 1 minute

// ==================== BATTERY STATE ====================
namespace BatteryNS {
  // Cached battery data
  static float lastVoltage = 0.0;
  static int lastPercentage = 0;
  static unsigned long lastUpdateTime = 0;
  static bool isInitialized = false;
  
  // Charging detection (optional - requires hardware support)
  static bool isCharging = false;
}

// ==================== BATTERY FUNCTIONS ====================

// Initialize battery monitoring
void batteryInit() {
  using namespace BatteryNS;
  
  // Configure ADC pin
  pinMode(BATTERY_ADC_PIN, INPUT);
  
  // Set ADC attenuation for full range (0-3.3V)
  // For ESP32-S3, use ADC_11db for 0-3.3V range
  analogSetAttenuation(ADC_11db);
  
  // Initial reading
  isInitialized = true;
  lastUpdateTime = 0;  // Force immediate update
  
  Serial.println("[BATTERY] Battery monitoring initialized");
  Serial.printf("[BATTERY] ADC Pin: GPIO %d\n", BATTERY_ADC_PIN);
  Serial.printf("[BATTERY] Voltage divider ratio: %.1f:1\n", VOLTAGE_DIVIDER_RATIO);
}

// Read raw ADC value with averaging
int batteryReadRawADC() {
  long sum = 0;
  
  for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
    sum += analogRead(BATTERY_ADC_PIN);
    delayMicroseconds(100);  // Small delay between samples
  }
  
  return sum / BATTERY_SAMPLE_COUNT;
}

// Convert ADC reading to actual battery voltage
float batteryCalculateVoltage(int rawADC) {
  // Convert ADC reading to voltage at the ADC pin
  float adcVoltage = (rawADC / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;
  
  // Account for voltage divider to get actual battery voltage
  float batteryVoltage = adcVoltage * VOLTAGE_DIVIDER_RATIO;
  
  return batteryVoltage;
}

// Convert battery voltage to percentage
int batteryVoltageToPercentage(float voltage) {
  // Clamp voltage to valid range
  if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
  if (voltage <= BATTERY_MIN_VOLTAGE) return 0;
  
  // Linear interpolation between min and max voltage
  float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / 
                      (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
  
  // For more accurate LiPo discharge curve, use a lookup table or polynomial
  // This is a simplified linear approximation
  
  return (int)percentage;
}

// Update battery readings (call periodically)
void batteryUpdate() {
  using namespace BatteryNS;
  
  // Check if enough time has passed since last update
  unsigned long currentTime = millis();
  if (isInitialized && (currentTime - lastUpdateTime) < BATTERY_UPDATE_INTERVAL) {
    return;  // Skip update, not enough time passed
  }
  
  // Read ADC value
  int rawADC = batteryReadRawADC();
  
  // Calculate voltage
  float voltage = batteryCalculateVoltage(rawADC);
  
  // Calculate percentage
  int percentage = batteryVoltageToPercentage(voltage);
  
  // Update cached values
  lastVoltage = voltage;
  lastPercentage = percentage;
  lastUpdateTime = currentTime;
  
  // Debug output (reduce frequency to avoid spam)
  static unsigned long lastDebugPrint = 0;
  if (currentTime - lastDebugPrint > 300000) {  // Print every 5 minutes
    Serial.printf("[BATTERY] ADC: %d, Voltage: %.2fV, Percentage: %d%%\n", 
                  rawADC, voltage, percentage);
    lastDebugPrint = currentTime;
  }
}

// Force an immediate battery reading (bypasses update interval)
void batteryForceUpdate() {
  using namespace BatteryNS;
  lastUpdateTime = 0;  // Reset timer to force update
  batteryUpdate();
}

// Get current battery voltage
float batteryGetVoltage() {
  using namespace BatteryNS;
  return lastVoltage;
}

// Get current battery percentage
int batteryGetPercentage() {
  using namespace BatteryNS;
  return lastPercentage;
}

// Check if battery is low (below 20%)
bool batteryIsLow() {
  using namespace BatteryNS;
  return lastPercentage < 20;
}

// Check if battery is critical (below 10%)
bool batteryIsCritical() {
  using namespace BatteryNS;
  return lastPercentage < 10;
}

// Get battery status as a formatted string
void batteryGetStatusString(char* buffer, size_t bufferSize, bool showPercentage) {
  using namespace BatteryNS;
  
  if (showPercentage) {
    snprintf(buffer, bufferSize, "%d%%", lastPercentage);
  } else {
    // Icon representation based on percentage
    if (lastPercentage >= 90) {
      snprintf(buffer, bufferSize, "[===]");  // Full
    } else if (lastPercentage >= 60) {
      snprintf(buffer, bufferSize, "[== ]");  // Good
    } else if (lastPercentage >= 30) {
      snprintf(buffer, bufferSize, "[=  ]");  // Medium
    } else if (lastPercentage >= 10) {
      snprintf(buffer, bufferSize, "[!  ]");  // Low
    } else {
      snprintf(buffer, bufferSize, "[!! ]");  // Critical
    }
  }
}

// Get battery level icon for display (returns icon index)
int batteryGetIconIndex() {
  using namespace BatteryNS;
  
  if (lastPercentage >= 90) return 5;      // 90-100%
  else if (lastPercentage >= 70) return 4;  // 70-89%
  else if (lastPercentage >= 50) return 3;  // 50-69%
  else if (lastPercentage >= 30) return 2;  // 30-49%
  else if (lastPercentage >= 10) return 1;  // 10-29%
  else return 0;                            // 0-9%
}

// Check if battery monitoring is working
bool batteryIsHealthy() {
  using namespace BatteryNS;
  
  // If voltage is unreasonably low or high, ADC might not be working
  if (lastVoltage < 2.5 || lastVoltage > 5.0) {
    return false;
  }
  
  return true;
}

// Optional: Detect if battery is charging (requires charging detection circuit)
// On some boards, this might be connected to a different GPIO
bool batteryIsCharging() {
  using namespace BatteryNS;
  // TODO: Implement if your hardware has charging detection
  // For example: return digitalRead(CHARGE_STATUS_PIN) == LOW;
  return false;  // Placeholder
}

#endif // BATTERY_H
