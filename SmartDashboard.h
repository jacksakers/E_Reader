#ifndef SMARTDASHBOARD_H
#define SMARTDASHBOARD_H

// ==================== SMART DASHBOARD ====================
// Displays the current time (synced via NTP) and weather forecast
// fetched from Open-Meteo (free, no API key required).
//
// Time is kept by the ESP32's internal SNTP client after a successful
// NTP sync.  If the board reboots and no sync has been done the clock
// shows "--:--" everywhere rather than a wrong time.
//
// Weather is fetched at two configurable hours per day (default 06:00
// and 18:00) and only when WiFi STA mode is configured.  Data is kept
// in memory so it survives mode switches within a session.
//
// Open-Meteo endpoint used:
//   http://api.open-meteo.com/v1/forecast
//     ?latitude=<LAT>&longitude=<LON>
//     &current_weather=true
//     &daily=weathercode,temperature_2m_max,temperature_2m_min
//     &timezone=auto&forecast_days=7
// ==========================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "EPD.h"
#include "SD.h"

// Forward declarations – implemented in Settings.h
extern bool        settingsGetDashWeatherEnabled();
extern float       settingsGetDashLatitude();
extern float       settingsGetDashLongitude();
extern int         settingsGetDashTimezoneOffset();
extern int         settingsGetDashUpdateHour1();
extern int         settingsGetDashUpdateHour2();
extern bool        settingsGetDashUseFahrenheit();
extern bool        settingsGetDashDST();
extern bool        settingsGetWiFiEnabled();
extern int         settingsGetWiFiMode();
extern const char* settingsGetWiFiSTASSID();
extern const char* settingsGetWiFiSTAPassword();

// Reference to the global display buffer defined in OS_Main.ino
extern uint8_t ImageBW[27200];

// ==================== COMPILE-TIME CONFIGURATION ====================
#define DASH_NTP_SERVER1       "pool.ntp.org"
#define DASH_NTP_SERVER2       "time.nist.gov"
#define DASH_WIFI_TIMEOUT_MS   15000    // 15 s WiFi connect limit
#define DASH_HTTP_TIMEOUT_MS   20000    // 20 s HTTP request limit
#define DASH_MIN_VALID_YEAR    2024     // Below this → NTP data is garbage
#define DASH_MIN_REFETCH_MS    (4UL * 3600UL * 1000UL) // 4 h between auto-fetches

// Weather icon configuration – adjust W/H to match your .art files
#define DASH_WEATHER_ICON_W    48
#define DASH_WEATHER_ICON_H    48
#define DASH_WEATHER_ICON_PATH "/art/weather"
#define DASH_WEATHER_ICON_BYTES \
  ((DASH_WEATHER_ICON_W / 8 + ((DASH_WEATHER_ICON_W % 8) ? 1 : 0)) * DASH_WEATHER_ICON_H)

// Display layout constants
#define DASH_HDR_Y   7    // Header text Y
#define DASH_LINE1_Y 28   // Horizontal rule below header
#define DASH_LINE2_Y 76   // Horizontal rule below today section
#define DASH_FTR_LINE_Y 240
#define DASH_FTR_Y   246

// ==================== NAMESPACE ====================
namespace DashboardNS {

  // ---- Weather data structures ----
  struct DayForecast {
    char  date[12];       // "YYYY-MM-DD"
    char  dayName[4];     // "Mon"
    int   weatherCode;
    float tempMax;        // °C
    float tempMin;        // °C
  };

  struct WeatherData {
    float  currentTemp;   // °C from current_weather
    int    currentCode;   // WMO code from current_weather
    float  windSpeedKph;
    int    forecastCount;
    DayForecast forecast[7];
  };

  // ---- Dashboard states ----
  enum State {
    DASH_MAIN,
    DASH_FETCHING,
    DASH_ERROR
  };

  // ---- Time tracking ----
  static bool          timeIsSynced        = false;
  static int           lastDisplayMinute   = -1;
  static char          lastSyncTimeStr[8]  = "";    // "HH:MM" of last NTP sync

  // ---- Weather tracking ----
  static bool          weatherDataValid    = false;
  static WeatherData   weatherData;
  static unsigned long lastWeatherFetchMs  = 0;
  static bool          fetchAttempted      = false;
  static char          lastFetchTimeStr[8] = "";    // "HH:MM" of last weather fetch

  // ---- UI state ----
  static State   currentState  = DASH_MAIN;
  static bool    needsRefresh  = true;
  static String  errorMessage  = "";

  // Track whether WIFi was already up before we touched it
  static bool    wifiWasConnected = false;

  // Buffer for loading weather icon bitmaps from SD card
  static uint8_t weatherIconBuf[DASH_WEATHER_ICON_BYTES];

} // namespace DashboardNS


// ==================== HELPER: WEATHER CODE DESCRIPTIONS ====================

static const char* dashWeatherDesc(int code) {
  if (code == 0)                      return "Clear Sky";
  if (code == 1)                      return "Mostly Clear";
  if (code == 2)                      return "Partly Cloudy";
  if (code == 3)                      return "Overcast";
  if (code == 45 || code == 48)       return "Foggy";
  if (code >= 51 && code <= 55)       return "Drizzle";
  if (code >= 61 && code <= 65)       return "Rain";
  if (code >= 71 && code <= 77)       return "Snow";
  if (code == 80 || code == 81 || code == 82) return "Showers";
  if (code == 85 || code == 86)       return "Snow Showers";
  if (code == 95)                     return "Thunderstorm";
  if (code == 96 || code == 99)       return "Thunder+Hail";
  return "Unknown";
}

// Short 5-char ASCII "icon" for weather code, shown in the forecast strip
static const char* dashWeatherIcon(int code) {
  if (code == 0)                      return "[SUN]";
  if (code <= 2)                      return "[SNC]";  // sun + cloud
  if (code == 3)                      return "[CLD]";
  if (code == 45 || code == 48)       return "[FOG]";
  if (code >= 51 && code <= 55)       return "[DRZ]";
  if (code >= 61 && code <= 65)       return "[RAN]";
  if (code >= 71 && code <= 77)       return "[SNW]";
  if (code >= 80 && code <= 82)       return "[SHR]";
  if (code >= 85 && code <= 86)       return "[SNS]";
  if (code >= 95)                     return "[THR]";
  return "[???]";
}

// Derive 3-char day name from "YYYY-MM-DD" date string
static void dashDayFromDate(const char* dateStr, char* outBuf, size_t bufLen) {
  int yr = 2024, mo = 1, dy = 1;
  sscanf(dateStr, "%d-%d-%d", &yr, &mo, &dy);
  struct tm t = {};
  t.tm_year = yr - 1900;
  t.tm_mon  = mo - 1;
  t.tm_mday = dy;
  mktime(&t);
  const char* names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  strncpy(outBuf, names[t.tm_wday], bufLen - 1);
  outBuf[bufLen - 1] = '\0';
}

static void dashFormatTemp(float tempC, char* buf, size_t len) {
  if (settingsGetDashUseFahrenheit()) {
    snprintf(buf, len, "%.0fF", tempC * 9.0f / 5.0f + 32.0f);
  } else {
    snprintf(buf, len, "%.0fC", tempC);
  }
}


// ==================== WEATHER ICON HELPERS ====================

// Maps a WMO weather code to a filename inside DASH_WEATHER_ICON_PATH.
// Returns nullptr when no icon is defined for the code (e.g. snow).
static const char* dashWeatherIconFile(int code) {
  if (code == 0)                      return "sun.art";
  if (code <= 2)                      return "sun-and-cloud.art";
  if (code == 3)                      return "cloud.art";
  if (code == 45 || code == 48)       return "fog.art";
  if (code >= 51 && code <= 55)       return "drizzle.art";
  if (code >= 61 && code <= 65)       return "rain.art";
  if (code >= 80 && code <= 82)       return "showers.art";
  if (code >= 95)                     return "t-storm.art";
  return nullptr;  // snow / other – no icon, caller uses text fallback
}

// Loads and draws a weather icon for the given WMO code at (x, y).
// Returns true when the icon was rendered; false = caller should show text.
static bool dashShowWeatherIcon(int code, uint16_t x, uint16_t y) {
  using namespace DashboardNS;

  const char* iconFile = dashWeatherIconFile(code);
  if (!iconFile) return false;

  char path[64];
  snprintf(path, sizeof(path), "%s/%s", DASH_WEATHER_ICON_PATH, iconFile);

  if (!SD.exists(path)) return false;

  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  size_t bytesRead = f.read(weatherIconBuf, sizeof(weatherIconBuf));
  f.close();

  if (bytesRead < sizeof(weatherIconBuf)) {
    Serial.printf("[DASHBOARD] Icon too small: %s (%d bytes, need %d)\n",
                  path, (int)bytesRead, (int)sizeof(weatherIconBuf));
    return false;
  }

  EPD_ShowPicture(x, y, DASH_WEATHER_ICON_W, DASH_WEATHER_ICON_H, weatherIconBuf, WHITE);
  return true;
}


// ==================== PUBLIC: TIME ACCESS ====================

// Returns "HH:MM" when synced, "--:--" when not synced.
// Safe to call from any module (e.g. home screen header).
void dashGetShortTimeString(char* buf, size_t len) {
  using namespace DashboardNS;
  if (!timeIsSynced) {
    strncpy(buf, "--:--", len - 1);
    buf[len - 1] = '\0';
    return;
  }
  struct tm ti = {};
  if (!getLocalTime(&ti, 100)) {
    strncpy(buf, "--:--", len - 1);
    buf[len - 1] = '\0';
    return;
  }
  snprintf(buf, len, "%02d:%02d", ti.tm_hour, ti.tm_min);
}

// Returns true if the displayed minute has changed since last call.
// Used by OS_Main to decide when to redraw the home screen clock.
bool dashCheckMinuteChanged() {
  using namespace DashboardNS;
  if (!timeIsSynced) return false;
  struct tm ti = {};
  if (!getLocalTime(&ti, 0)) return false;
  if (ti.tm_min != lastDisplayMinute) {
    lastDisplayMinute = ti.tm_min;
    return true;
  }
  return false;
}

bool dashIsTimeSynced() {
  return DashboardNS::timeIsSynced;
}


// ==================== WIFI HELPERS ====================

static bool dashConnectWiFi() {
  using namespace DashboardNS;

  if (WiFi.status() == WL_CONNECTED) {
    wifiWasConnected = true;
    return true;
  }
  wifiWasConnected = false;

  if (settingsGetWiFiMode() != 2) {
    Serial.println("[DASHBOARD] WiFi must be in STA mode to fetch data");
    return false;
  }
  if (!settingsGetWiFiEnabled()) {
    Serial.println("[DASHBOARD] WiFi disabled in settings");
    return false;
  }

  const char* ssid = settingsGetWiFiSTASSID();
  const char* pass = settingsGetWiFiSTAPassword();

  Serial.printf("[DASHBOARD] Connecting to WiFi: %s\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > DASH_WIFI_TIMEOUT_MS) {
      Serial.println("[DASHBOARD] WiFi timed out");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return false;
    }
    delay(200);
  }

  Serial.printf("[DASHBOARD] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

static void dashDisconnectWiFi() {
  using namespace DashboardNS;
  if (!wifiWasConnected) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[DASHBOARD] WiFi disconnected");
  }
}


// ==================== NTP SYNC ====================

static bool dashSyncNTP() {
  using namespace DashboardNS;

  int tzOffsetHours = settingsGetDashTimezoneOffset();
  if (settingsGetDashDST()) tzOffsetHours += 1;
  Serial.printf("[DASHBOARD] Syncing NTP (UTC%+d, DST=%s)...\n",
                tzOffsetHours, settingsGetDashDST() ? "on" : "off");
  configTime((long)tzOffsetHours * 3600L, 0, DASH_NTP_SERVER1, DASH_NTP_SERVER2);

  struct tm ti = {};
  unsigned long t0 = millis();
  while (!getLocalTime(&ti, 250)) {
    if (millis() - t0 > 10000) {
      Serial.println("[DASHBOARD] NTP sync timed out");
      return false;
    }
  }

  if (ti.tm_year + 1900 < DASH_MIN_VALID_YEAR) {
    Serial.printf("[DASHBOARD] NTP returned bad year: %d\n", ti.tm_year + 1900);
    return false;
  }

  timeIsSynced = true;
  snprintf(lastSyncTimeStr, sizeof(lastSyncTimeStr), "%02d:%02d", ti.tm_hour, ti.tm_min);
  Serial.printf("[DASHBOARD] NTP OK – %02d:%02d:%02d\n", ti.tm_hour, ti.tm_min, ti.tm_sec);
  return true;
}


// ==================== WEATHER FETCH ====================

static bool dashFetchWeather() {
  using namespace DashboardNS;

  if (!settingsGetDashWeatherEnabled()) return false;

  float lat = settingsGetDashLatitude();
  float lon = settingsGetDashLongitude();

  char url[300];
  snprintf(url, sizeof(url),
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=%.2f&longitude=%.2f"
    "&current_weather=true"
    "&daily=weathercode,temperature_2m_max,temperature_2m_min"
    "&timezone=auto&forecast_days=7",
    lat, lon);

  Serial.printf("[DASHBOARD] Fetching weather: lat=%.2f, lon=%.2f\n", lat, lon);

  HTTPClient http;
  http.setTimeout(DASH_HTTP_TIMEOUT_MS);
  http.begin(url);

  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("[DASHBOARD] HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();
  Serial.printf("[DASHBOARD] Weather response: %d bytes\n", payload.length());

  // Parse JSON – 6 kB doc is plenty for this response
  DynamicJsonDocument doc(6144);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[DASHBOARD] JSON error: %s\n", err.c_str());
    return false;
  }

  // Current conditions
  JsonObject cw = doc["current_weather"];
  weatherData.currentTemp    = cw["temperature"].as<float>();
  weatherData.currentCode    = cw["weathercode"].as<int>();
  weatherData.windSpeedKph   = cw["windspeed"].as<float>();

  // 7-day daily forecast
  JsonObject daily    = doc["daily"];
  JsonArray  dates    = daily["time"];
  JsonArray  codes    = daily["weathercode"];
  JsonArray  maxTemps = daily["temperature_2m_max"];
  JsonArray  minTemps = daily["temperature_2m_min"];

  weatherData.forecastCount = min((int)dates.size(), 7);
  for (int i = 0; i < weatherData.forecastCount; i++) {
    const char* ds = dates[i];
    strncpy(weatherData.forecast[i].date, ds ? ds : "", 11);
    weatherData.forecast[i].date[11] = '\0';
    weatherData.forecast[i].weatherCode = codes[i].as<int>();
    weatherData.forecast[i].tempMax     = maxTemps[i].as<float>();
    weatherData.forecast[i].tempMin     = minTemps[i].as<float>();
    dashDayFromDate(weatherData.forecast[i].date,
                    weatherData.forecast[i].dayName, 4);
  }

  weatherDataValid    = true;
  fetchAttempted      = true;
  lastWeatherFetchMs  = millis();

  // Record fetch time for display
  struct tm ti = {};
  if (getLocalTime(&ti, 100)) {
    snprintf(lastFetchTimeStr, sizeof(lastFetchTimeStr), "%02d:%02d", ti.tm_hour, ti.tm_min);
  } else {
    strncpy(lastFetchTimeStr, "--:--", sizeof(lastFetchTimeStr));
  }

  Serial.printf("[DASHBOARD] Weather OK – %.1fC, code=%d, %d days forecast\n",
                weatherData.currentTemp, weatherData.currentCode,
                weatherData.forecastCount);
  return true;
}


// ==================== DISPLAY HELPERS ====================

static void dashDrawHeader() {
  using namespace DashboardNS;

  char timeStr[8];
  dashGetShortTimeString(timeStr, sizeof(timeStr));
  EPD_ShowString(10, DASH_HDR_Y, timeStr, 16, BLACK);

  if (!timeIsSynced) {
    EPD_ShowString(70, DASH_HDR_Y, "[unsynced]", 16, BLACK);
  }

  // Date centered
  char dateStr[40] = "";
  struct tm ti = {};
  if (timeIsSynced && getLocalTime(&ti, 100)) {
    const char* days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                             "Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(dateStr, sizeof(dateStr), "%s, %s %02d %04d",
             days[ti.tm_wday], months[ti.tm_mon],
             ti.tm_mday, ti.tm_year + 1900);
  } else {
    strncpy(dateStr, "Date unknown", sizeof(dateStr));
  }
  EPD_ShowString(250, DASH_HDR_Y, dateStr, 16, BLACK);

  EPD_DrawLine(0, DASH_LINE1_Y, 792, DASH_LINE1_Y, BLACK);
}

// Screen shown while connecting / fetching
static void dashDrawFetchingScreen() {
  Paint_Clear(WHITE);
  EPD_ShowString(10, DASH_HDR_Y, "SMART DASHBOARD", 16, BLACK);
  EPD_DrawLine(0, DASH_LINE1_Y, 792, DASH_LINE1_Y, BLACK);
  EPD_ShowString(10, 55, "Connecting to WiFi and syncing...", 16, BLACK);
  EPD_ShowString(10, 80, "Please wait.", 16, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// Screen shown when there is no weather data yet
static void dashDrawNoDataScreen() {
  using namespace DashboardNS;

  Paint_Clear(WHITE);
  dashDrawHeader();

  int y = 40;
  EPD_ShowString(10, y, "SMART DASHBOARD", 16, BLACK);
  EPD_DrawLine(0, y + 20, 792, y + 20, BLACK);
  y += 30;

  if (!timeIsSynced) {
    EPD_ShowString(10, y, "Clock not synced - press OK to connect and sync now.", 16, BLACK);
    y += 24;
  }

  if (!weatherDataValid) {
    EPD_ShowString(10, y, "No weather data yet - press OK to fetch.", 16, BLACK);
    y += 24;
  }

  EPD_ShowString(10, y, "Note: WiFi must be set to STA mode in Settings.", 16, BLACK);

  EPD_DrawLine(0, DASH_FTR_LINE_Y, 792, DASH_FTR_LINE_Y, BLACK);
  EPD_ShowString(10, DASH_FTR_Y, "OK: Sync & Fetch Now   EXIT: Back to Home", 16, BLACK);

  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// Main dashboard screen: header + today + 7-day forecast
static void dashDrawMainScreen() {
  using namespace DashboardNS;

  Paint_Clear(WHITE);
  dashDrawHeader();

  // ---- TODAY band (y = DASH_LINE1_Y+4 to DASH_LINE2_Y) ----
  int y = DASH_LINE1_Y + 4;

  char curTempStr[12];
  dashFormatTemp(weatherData.currentTemp, curTempStr, sizeof(curTempStr));

  char hiStr[10], loStr[10];
  if (weatherData.forecastCount > 0) {
    dashFormatTemp(weatherData.forecast[0].tempMax, hiStr, sizeof(hiStr));
    dashFormatTemp(weatherData.forecast[0].tempMin, loStr, sizeof(loStr));
  } else {
    strncpy(hiStr, "?", sizeof(hiStr));
    strncpy(loStr, "?", sizeof(loStr));
  }

  char windStr[24];
  snprintf(windStr, sizeof(windStr), "Wind %.0f km/h", weatherData.windSpeedKph);

  char hiloStr[24];
  snprintf(hiloStr, sizeof(hiloStr), "H:%s  L:%s", hiStr, loStr);

  // Current conditions: try SD icon first, fall back to ASCII text
  bool mainIconShown = dashShowWeatherIcon(weatherData.currentCode, 10, y);
  if (mainIconShown) {
    int tx = 10 + DASH_WEATHER_ICON_W + 8;
    EPD_ShowString(tx,       y,      curTempStr,                                       16, BLACK);
    EPD_ShowString(tx + 64,  y,      (char*)dashWeatherDesc(weatherData.currentCode),  16, BLACK);
    EPD_ShowString(tx,       y + 20, windStr,                                           16, BLACK);
    EPD_ShowString(tx,       y + 40, hiloStr,                                           16, BLACK);
    y += DASH_WEATHER_ICON_H + 4;
  } else {
    EPD_ShowString(10,  y, (char*)dashWeatherIcon(weatherData.currentCode), 16, BLACK);
    EPD_ShowString(65,  y, curTempStr,                                       16, BLACK);
    EPD_ShowString(120, y, (char*)dashWeatherDesc(weatherData.currentCode),  16, BLACK);
    EPD_ShowString(400, y, windStr,                                           16, BLACK);
    EPD_ShowString(590, y, hiloStr,                                           16, BLACK);
    y += 22;
  }
  EPD_DrawLine(0, y, 792, y, BLACK);
  y += 4;

  // ---- 7-Day forecast strip ----
  // Guard against zero to avoid division by zero
  int count = weatherData.forecastCount > 0 ? weatherData.forecastCount : 1;
  int colW  = 792 / count;

  for (int i = 0; i < weatherData.forecastCount; i++) {
    int x = i * colW + 6;
    DashboardNS::DayForecast& fc = weatherData.forecast[i];

    // Day label
    const char* dayLabel = (i == 0) ? "TODAY" : fc.dayName;
    EPD_ShowString(x, y, (char*)dayLabel, 16, BLACK);

    // Icon or text fallback for each forecast column
    bool fcIconShown = dashShowWeatherIcon(fc.weatherCode, x, y + 18);
    if (!fcIconShown) {
      EPD_ShowString(x, y + 20, (char*)dashWeatherIcon(fc.weatherCode), 16, BLACK);
    }

    // Condition and temps – placed below icon or text row
    int condY = fcIconShown ? (y + 18 + DASH_WEATHER_ICON_H + 2) : (y + 40);
    const char* cond = dashWeatherDesc(fc.weatherCode);
    char condShort[14];
    strncpy(condShort, cond, 13);
    condShort[13] = '\0';
    EPD_ShowString(x, condY, condShort, 16, BLACK);

    char fcHi[10], fcLo[10];
    dashFormatTemp(fc.tempMax, fcHi, sizeof(fcHi));
    dashFormatTemp(fc.tempMin, fcLo, sizeof(fcLo));
    EPD_ShowString(x, condY + 20, fcHi, 16, BLACK);
    EPD_ShowString(x, condY + 40, fcLo, 16, BLACK);

    // Column divider – extend full height of the forecast band
    if (i < weatherData.forecastCount - 1) {
      EPD_DrawLine(x + colW - 4, y - 2, x + colW - 4, DASH_FTR_LINE_Y - 2, BLACK);
    }
  }

  // ---- Footer ----
  EPD_DrawLine(0, DASH_FTR_LINE_Y, 792, DASH_FTR_LINE_Y, BLACK);

  char footerBuf[96];
  const char* syncTag = timeIsSynced ? "Synced" : "NOT SYNCED";
  if (strlen(lastFetchTimeStr) > 0) {
    snprintf(footerBuf, sizeof(footerBuf),
             "OK: Force Resync  EXIT: Back  [%s]  Weather fetched: %s",
             syncTag, lastFetchTimeStr);
  } else {
    snprintf(footerBuf, sizeof(footerBuf),
             "OK: Force Resync  EXIT: Back  [%s]", syncTag);
  }
  EPD_ShowString(10, DASH_FTR_Y, footerBuf, 16, BLACK);

  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

static void dashDrawErrorScreen() {
  using namespace DashboardNS;

  Paint_Clear(WHITE);
  EPD_ShowString(10, DASH_HDR_Y, "SMART DASHBOARD - SYNC ERROR", 16, BLACK);
  EPD_DrawLine(0, DASH_LINE1_Y, 792, DASH_LINE1_Y, BLACK);

  EPD_ShowString(10, 45, "Sync failed:", 16, BLACK);
  EPD_ShowString(10, 65, (char*)errorMessage.c_str(), 16, BLACK);
  EPD_ShowString(10, 100,
    "Ensure WiFi is set to STA mode with correct credentials.", 16, BLACK);
  EPD_ShowString(10, 120,
    "Go to Settings > WiFi Options to configure.", 16, BLACK);

  EPD_DrawLine(0, DASH_FTR_LINE_Y, 792, DASH_FTR_LINE_Y, BLACK);
  EPD_ShowString(10, DASH_FTR_Y, "OK: Try Again   EXIT: Back to Home", 16, BLACK);

  EPD_Display(ImageBW);
  EPD_PartUpdate();
}


// ==================== SYNC OPERATION ====================

// Performs a full: WiFi connect → NTP sync → weather fetch → WiFi disconnect.
// Updates currentState and sets needsRefresh when done.
static void dashDoSync() {
  using namespace DashboardNS;

  currentState = DASH_FETCHING;
  dashDrawFetchingScreen();

  bool connected = dashConnectWiFi();
  if (!connected) {
    errorMessage = "Could not connect to WiFi. Check STA SSID/password.";
    currentState  = DASH_ERROR;
    needsRefresh  = true;
    return;
  }

  bool ntpOk     = dashSyncNTP();
  bool weatherOk = dashFetchWeather();
  dashDisconnectWiFi();

  if (!ntpOk && !weatherOk) {
    errorMessage = "NTP and weather both failed. Check internet access.";
    currentState = DASH_ERROR;
  } else {
    if (!ntpOk) {
      Serial.println("[DASHBOARD] NTP failed but weather OK – proceeding");
    }
    currentState = DASH_MAIN;
  }
  needsRefresh = true;
}

// Returns true if it is a scheduled fetch hour and enough time has passed.
static bool dashShouldAutoUpdate() {
  using namespace DashboardNS;

  if (!timeIsSynced) return false;
  if (!settingsGetDashWeatherEnabled()) return false;

  // Don't re-fetch if it's been less than 4 hours
  if (fetchAttempted) {
    if (millis() - lastWeatherFetchMs < DASH_MIN_REFETCH_MS) return false;
  }

  struct tm ti = {};
  if (!getLocalTime(&ti, 0)) return false;

  int h1 = settingsGetDashUpdateHour1();
  int h2 = settingsGetDashUpdateHour2();
  return (ti.tm_hour == h1 || (h2 >= 0 && ti.tm_hour == h2));
}


// ==================== PUBLIC INTERFACE ====================

// Attempt a background NTP sync.  Intended to be called once at startup
// (before the home screen) when WiFi STA is configured, so the clock on
// the home screen shows the correct time immediately.
void dashBackgroundTimeSync() {
  using namespace DashboardNS;

  if (settingsGetWiFiMode() != 2 || !settingsGetWiFiEnabled()) return;

  Serial.println("[DASHBOARD] Background time sync...");

  if (!dashConnectWiFi()) {
    Serial.println("[DASHBOARD] Background sync: WiFi failed, skipping");
    return;
  }
  dashSyncNTP();      // best-effort, errors are non-fatal
  dashDisconnectWiFi();
}

// Called when entering Smart Dashboard mode from the home menu
void dashboardInit() {
  using namespace DashboardNS;

  Serial.println("[DASHBOARD] Initializing Smart Dashboard mode...");

  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);

  currentState = DASH_MAIN;
  needsRefresh  = true;

  // If we have no data at all, trigger a sync immediately
  if (!weatherDataValid && !timeIsSynced) {
    dashDoSync();
  } else if (!weatherDataValid) {
    // Time is synced but no weather – show no-data screen
    needsRefresh = true;
  }

  Serial.println("[DASHBOARD] Initialization complete");
}

// Called every loop() iteration while in Smart Dashboard mode
void dashboardUpdate() {
  using namespace DashboardNS;

  // Scheduled auto-update
  if (dashShouldAutoUpdate()) {
    dashDoSync();
    return;
  }

  // Trigger redraw when the displayed minute changes
  struct tm ti = {};
  if (timeIsSynced && getLocalTime(&ti, 0) && ti.tm_min != lastDisplayMinute) {
    lastDisplayMinute = ti.tm_min;
    needsRefresh = true;
  }

  if (!needsRefresh) return;
  needsRefresh = false;

  switch (currentState) {
    case DASH_MAIN:
      if (weatherDataValid) {
        dashDrawMainScreen();
      } else {
        dashDrawNoDataScreen();
      }
      break;
    case DASH_FETCHING:
      dashDrawFetchingScreen();
      break;
    case DASH_ERROR:
      dashDrawErrorScreen();
      break;
  }
}

// Handle button input while in Smart Dashboard mode.
// Returns true to stay in the mode, false to exit to home.
bool dashboardHandleInput(bool upPressed, bool downPressed,
                          bool okPressed, bool exitPressed) {
  using namespace DashboardNS;
  (void)upPressed; (void)downPressed;   // not used currently

  if (exitPressed) return false;        // caller will call dashboardCleanup()

  if (okPressed) {
    dashDoSync();                       // force re-sync
  }

  return true;
}

// Called when leaving Smart Dashboard mode.
// Weather data is intentionally kept in memory so returning to the
// dashboard in the same session does not require a re-fetch.
void dashboardCleanup() {
  Serial.println("[DASHBOARD] Cleanup (weather cache retained)");
}

#endif // SMARTDASHBOARD_H
