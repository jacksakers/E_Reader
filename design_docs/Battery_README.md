# Battery Indicator Implementation Guide

## Overview

The E-Ink OS now includes a battery monitoring system that displays the current battery level on the home screen and provides detailed battery information in the Settings menu.

## Features

- **Real-time Battery Monitoring**: Reads battery voltage via ADC and displays percentage
- **Configurable Display**: Toggle between percentage display and icon-only display
- **Smart Updating**: Updates battery reading every 60 seconds to minimize power consumption
- **Battery Health Check**: Detects if battery monitoring is working correctly
- **Low Battery Warnings**: Identifies low (< 20%) and critical (< 10%) battery levels

## Hardware Configuration

### Default Settings

- **ADC Pin**: GPIO 3 (configurable in Battery.h)
- **Voltage Divider Ratio**: 2.0:1 (typical for ESP32-S3 boards)
- **Battery Type**: 3.7V LiPo (1500mAh)
- **Voltage Range**: 3.0V (empty) to 4.2V (full)

### Pin Configuration

If your board uses a different GPIO pin for battery monitoring, modify `BATTERY_ADC_PIN` in [Battery.h](c:\Users\PQX863\Documents\side\E_Reader\Battery.h):

```cpp
#define BATTERY_ADC_PIN 3  // Change to your board's battery ADC pin
```

## Calibration

### Step 1: Verify Voltage Divider Ratio

Your board likely has a voltage divider circuit that reduces the battery voltage to a safe range for the ESP32's ADC (0-3.3V). 

**To find your voltage divider ratio:**

1. Measure the actual battery voltage with a multimeter (e.g., 4.2V when fully charged)
2. Check the voltage at the ADC pin (should be around 2.1V for a 2:1 divider)
3. Calculate ratio: `VOLTAGE_DIVIDER_RATIO = Battery Voltage / ADC Voltage`

Update in Battery.h:

```cpp
#define VOLTAGE_DIVIDER_RATIO 2.0  // Adjust based on your measurement
```

### Step 2: Verify Battery Voltage Thresholds

The default thresholds assume a standard LiPo battery:

```cpp
#define BATTERY_MAX_VOLTAGE 4.2   // Fully charged
#define BATTERY_MIN_VOLTAGE 3.0   // Empty (safety cutoff)
```

These values should work for most LiPo batteries, but you can adjust them if needed.

### Step 3: Test and Adjust

1. **Upload the code** with battery connected
2. **Check Serial Monitor** for debug output:
   ```
   [BATTERY] Battery monitoring initialized
   [BATTERY] ADC Pin: GPIO 3
   [BATTERY] Voltage divider ratio: 2.0:1
   [BATTERY] ADC: 2456, Voltage: 4.03V, Percentage: 85%
   ```
3. **Compare displayed voltage** with multimeter reading
4. **Adjust voltage divider ratio** if readings don't match

### Common ADC Pins for ESP32-S3

If GPIO 3 doesn't work, try these common battery monitoring pins:

- GPIO 4 (ADC1_CH3)
- GPIO 5 (ADC1_CH4)
- GPIO 6 (ADC1_CH5)
- GPIO 9 (ADC1_CH8)
- GPIO 10 (ADC1_CH9)

**Note**: Different boards use different pins. Check your board's schematic or documentation.

## Display Modes

### Home Screen Display

The battery indicator appears in the top-right corner of the home screen:

- **Icon Mode**: `[===]`, `[== ]`, `[=  ]`, `[!  ]`, `[!! ]`
  - Full (90-100%)
  - Good (60-89%)
  - Medium (30-59%)
  - Low (10-29%)
  - Critical (0-9%)

- **Percentage Mode**: `85%`, `42%`, etc.

### Settings Menu Display

Navigate to **Settings → Power Options** to view detailed battery information:

```
Battery: 4.03V (85%)
Auto-Sleep: 30 min
Back to Main Menu
```

This shows both voltage and percentage for diagnostics.

## Configuration Options

### Toggle Display Mode

1. Go to **Home Screen → Settings → Display Options**
2. Select **"Battery: Show %"** or **"Battery: Icon only"**
3. Press OK to toggle

This setting is saved to SD card and persists across reboots.

### Update Frequency

By default, the battery is read once per minute to conserve power. To change this:

```cpp
#define BATTERY_UPDATE_INTERVAL 60000  // milliseconds (60000 = 1 minute)
```

**Note**: More frequent updates consume more power but provide fresher data.

## API Reference

### Initialization

```cpp
void batteryInit();              // Initialize battery monitoring
void batteryForceUpdate();       // Force immediate reading
void batteryUpdate();            // Update if interval elapsed
```

### Reading Values

```cpp
float batteryGetVoltage();       // Get voltage (e.g., 3.87)
int batteryGetPercentage();      // Get percentage (0-100)
bool batteryIsLow();            // Check if < 20%
bool batteryIsCritical();       // Check if < 10%
bool batteryIsHealthy();        // Verify ADC is working
```

### Display Functions

```cpp
void batteryGetStatusString(char* buffer, size_t size, bool showPercent);
int batteryGetIconIndex();      // Returns 0-5 for icon selection
```

## Troubleshooting

### Issue: Battery shows 0% or 100% always

**Cause**: Wrong ADC pin or voltage divider ratio

**Solution**:
1. Check your board's schematic for the correct battery ADC pin
2. Measure actual voltages and recalculate the divider ratio
3. Update `BATTERY_ADC_PIN` and `VOLTAGE_DIVIDER_RATIO` in Battery.h

### Issue: Battery percentage fluctuates wildly

**Cause**: Noisy ADC readings

**Solution**:
- Increase `BATTERY_SAMPLE_COUNT` for more averaging (default: 10)
- Add a small capacitor (0.1µF) to the ADC input if hardware allows

### Issue: Battery readings are inverted

**Cause**: Voltage divider or ADC misconfiguration

**Solution**:
- Verify your voltage divider calculation
- Check if your board has an inverted battery sense circuit

### Issue: No battery reading (shows 0.00V)

**Cause**: ADC not reading or wrong pin

**Solution**:
1. Check if battery is actually connected
2. Verify `BATTERY_ADC_PIN` matches your hardware
3. Try different ADC pins (GPIO 4, 5, 6, 9, 10)
4. Check Serial Monitor for initialization messages

## Power Consumption

The battery monitoring system is designed for minimal power impact:

- **Update Frequency**: Once per minute (configurable)
- **ADC Averaging**: 10 samples per reading
- **Deep Sleep Compatible**: Can be disabled during deep sleep
- **Estimated Impact**: < 0.1mA average current draw

## Hardware Support

### Tested Boards

- ✅ ELECROW CrowPanel ESP32-S3 5.79" E-paper HMI Display

### Untested But Should Work

- ESP32-S3 DevKit with LiPo battery connector
- ESP32-WROOM with battery management
- Other ESP32 boards with voltage divider circuits

### Not Supported

- Boards without battery voltage sensing
- USB-only powered devices

## Future Enhancements

Planned features for future updates:

- [ ] Charging detection (requires hardware support)
- [ ] Battery history graphing
- [ ] Low battery auto-sleep
- [ ] More accurate LiPo discharge curve modeling
- [ ] Battery health tracking over time

## Technical Notes

### LiPo Discharge Curve

The current implementation uses linear interpolation between min (3.0V) and max (4.2V) voltage. This is a simplification. A more accurate model would use the actual LiPo discharge curve:

- 4.20V = 100%
- 4.15V = 95%
- 4.11V = 90%
- 4.08V = 85%
- 3.95V = 75%
- 3.84V = 60%
- 3.80V = 50%
- 3.70V = 30%
- 3.60V = 15%
- 3.50V = 5%
- 3.00V = 0%

You can implement this as a lookup table in `batteryVoltageToPercentage()` for more accurate readings.

### ADC Calibration

ESP32-S3 ADCs can benefit from calibration using the built-in eFuse values. For production devices, consider using:

```cpp
#include "esp_adc_cal.h"
```

And implementing ADC calibration for more accurate voltage readings.

## Support

For issues or questions about battery monitoring:

1. Check the troubleshooting section above
2. Review Serial Monitor output for debug information
3. Verify your hardware configuration matches the code settings
4. Test with a known-good battery and multimeter

## License

This battery monitoring implementation is part of the E-Ink OS project and follows the same license terms.
