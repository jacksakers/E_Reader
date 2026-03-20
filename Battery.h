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
// GPIO pin connected to the VBUS voltage divider (R10 1K + R11 10K on the CrowPanel).
// This pin reads ~0.45V when USB is plugged in and ~0V when running on battery,
// so it is used for USB/charging detection only — not true battery voltage.
// See design_docs/Battery_README.md for wiring an external divider for real battery monitoring.
#define BATTERY_ADC_PIN 3

// ADC threshold (raw 0-4095) above which USB power is considered present.
// At 5V USB: rawADC ≈ 785. At 0V (battery only): rawADC ≈ 0. 300 is a safe midpoint.
#define USB_DETECT_THRESHOLD 300

// --- Below settings are for future true battery monitoring (see README) ---
#define VOLTAGE_DIVIDER_RATIO 2.0  // Update when external divider is wired
#define ADC_RESOLUTION 4096.0
#define ADC_REFERENCE_VOLTAGE 2.5  // ESP32-S3 ADC_11db effective range is ~2.5V

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
  
  // USB/charging detection via VBUS divider on GPIO 3
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
  
  // Detect USB/charging from VBUS divider on GPIO 3
  isCharging = (rawADC > USB_DETECT_THRESHOLD);
  
  // Voltage/percentage are not meaningful without an external battery divider
  lastVoltage = 0.0;
  lastPercentage = 0;
  lastUpdateTime = currentTime;
  
  // Debug output (reduce frequency to avoid spam)
  static unsigned long lastDebugPrint = 0;
  if (currentTime - lastDebugPrint > 300000) {  // Print every 5 minutes
    Serial.printf("[BATTERY] ADC: %d, USB/Charging: %s\n",
                  rawADC, isCharging ? "YES" : "NO");
    lastDebugPrint = currentTime;
  }
}

// Force an immediate battery reading (bypasses update interval)
void batteryForceUpdate() {
  using namespace BatteryNS;
  lastUpdateTime = 0;  // Reset timer to force update
  
  int rawADC = batteryReadRawADC();
  isCharging = (rawADC > USB_DETECT_THRESHOLD);
  lastVoltage = 0.0;
  lastPercentage = 0;
  lastUpdateTime = millis();
  
  Serial.printf("[BATTERY] FORCED: rawADC=%d, USB/Charging: %s\n",
                rawADC, isCharging ? "YES" : "NO");
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
  
  if (isCharging) {
    snprintf(buffer, bufferSize, "Charging");
  } else {
    snprintf(buffer, bufferSize, "On Battery");
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

// Returns true when USB power is detected via the VBUS divider on GPIO 3
bool batteryIsCharging() {
  using namespace BatteryNS;
  return isCharging;
}

#endif // BATTERY_H
