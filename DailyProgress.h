#ifndef DAILYPROGRESS_H
#define DAILYPROGRESS_H

// ==================== DAILY PROGRESS MODE ====================
// Displays four progress bars showing temporal progress:
//   1. Year  – how far through the current calendar year
//   2. Month – how far through the current month (day-segment bar)
//   3. Week  – how far through Sun–Sat (7 labelled day blocks)
//   4. Workday (9–17, Mon–Fri)  OR  Weekend (Sat–Sun)
//
// Each bar uses a distinct visual style to make the screen
// easy to read at a glance.
//
// Time is sourced from the ESP32 SNTP client as configured by
// SmartDashboard / Settings.  When not synced the screen shows a
// "Time not synced" message instead of wrong values.
// =============================================================

#include "EPD.h"
#include <time.h>

// Forward declarations
extern uint8_t ImageBW[27200];
extern bool dashIsTimeSynced();

namespace DailyProgressNS {
  static bool needsRefresh = true;
}

// ==================== HELPER UTILITIES ====================

static int dpDaysInMonth(int year, int month) {
  if (month == 2) {
    bool leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
    return leap ? 29 : 28;
  }
  if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
  return 31;
}

static float dpClamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Seconds elapsed in the current day (floating-point)
static float dpSecsToday(const struct tm& t) {
  return t.tm_hour * 3600.0f + t.tm_min * 60.0f + t.tm_sec;
}

// ==================== PROGRESS CALCULATIONS ====================

static float dpYearPercent(const struct tm& t) {
  bool leap = (t.tm_year % 4 == 0) && (t.tm_year % 100 != 0 || t.tm_year % 400 == 0);
  float total = leap ? 366.0f : 365.0f;
  float elapsed = t.tm_yday + dpSecsToday(t) / 86400.0f;
  return dpClamp(elapsed / total * 100.0f, 0.0f, 100.0f);
}

static float dpMonthPercent(const struct tm& t) {
  int days = dpDaysInMonth(t.tm_year + 1900, t.tm_mon + 1);
  float elapsed = (t.tm_mday - 1) + dpSecsToday(t) / 86400.0f;
  return dpClamp(elapsed / days * 100.0f, 0.0f, 100.0f);
}

static float dpWeekPercent(const struct tm& t) {
  // Week runs Sun (0) … Sat (6)
  float elapsed = t.tm_wday + dpSecsToday(t) / 86400.0f;
  return dpClamp(elapsed / 7.0f * 100.0f, 0.0f, 100.0f);
}

// Returns 0-100 on a Mon-Fri workday during 09:00-17:00, else -1
static float dpWorkdayPercent(const struct tm& t) {
  if (t.tm_wday == 0 || t.tm_wday == 6) return -1.0f;  // weekend
  float secs = dpSecsToday(t);
  if (secs <= 9.0f  * 3600.0f) return   0.0f;
  if (secs >= 17.0f * 3600.0f) return 100.0f;
  return (secs - 9.0f * 3600.0f) / (8.0f * 3600.0f) * 100.0f;
}

// Returns 0-100 on Sat/Sun (Sat 00:00 → Sun 23:59 = 48 h), else -1
static float dpWeekendPercent(const struct tm& t) {
  if (t.tm_wday != 0 && t.tm_wday != 6) return -1.0f;  // weekday
  float secs = (t.tm_wday == 6 ? 0.0f : 86400.0f) + dpSecsToday(t);
  return dpClamp(secs / (48.0f * 3600.0f) * 100.0f, 0.0f, 100.0f);
}

// ==================== BAR DRAWING STYLES ====================

// Style 1 — Classic solid fill (used for Year)
static void dpBarSolid(int x, int y, int w, int h, float pct) {
  EPD_DrawRectangle(x, y, x + w, y + h, BLACK, 0);
  int fill = (int)((w - 4) * pct / 100.0f);
  if (fill > 0) {
    EPD_DrawRectangle(x + 2, y + 2, x + 2 + fill, y + h - 2, BLACK, 1);
  }
}

// Style 2 — Day-segment bar, one block per day (used for Month)
static void dpBarDaySegments(int x, int y, int w, int h, float pct, int numSegs) {
  if (numSegs < 1) numSegs = 1;
  float segW  = (float)(w - 2) / numSegs;
  float filled = pct * numSegs / 100.0f;
  int   fullSegs = (int)filled;
  float partial  = filled - fullSegs;

  EPD_DrawRectangle(x, y, x + w, y + h, BLACK, 0);

  for (int i = 0; i < numSegs; i++) {
    int sx = x + 1 + (int)(i * segW);
    int ex = x + 1 + (int)((i + 1) * segW) - 1;
    if (ex <= sx) continue;

    if (i < fullSegs) {
      EPD_DrawRectangle(sx, y + 1, ex, y + h - 1, BLACK, 1);
    } else if (i == fullSegs && partial > 0.02f) {
      int partX = sx + (int)((ex - sx) * partial);
      if (partX > sx) {
        EPD_DrawRectangle(sx, y + 1, partX, y + h - 1, BLACK, 1);
      }
    }
  }
}

// Style 3 — 7 labelled day columns  Su Mo Tu We Th Fr Sa  (used for Week)
// totalH is the total height allocated including the label row above the blocks
static void dpBarWeekColumns(int x, int y, int w, int totalH, float pct) {
  const char* dayLabels[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
  const int   LBL_H = 18;   // row reserved for day-name text
  int         blockH = totalH - LBL_H;
  if (blockH < 4) blockH = 4;

  float blockW  = (float)w / 7.0f;
  float filled   = pct * 7.0f / 100.0f;
  int   fullDays = (int)filled;
  float partial  = filled - fullDays;

  for (int i = 0; i < 7; i++) {
    int sx = x + (int)(i * blockW);
    int ex = x + (int)((i + 1) * blockW) - 1;
    if (ex <= sx) continue;
    int colW = ex - sx;

    // Centred day label (2 chars ≈ 16 px wide at scale-8 font)
    int labelX = sx + (colW - 16) / 2;
    EPD_ShowString(labelX, y, (char*)dayLabels[i], 16, BLACK);

    // Block
    int bx = sx + 1;
    int by = y + LBL_H;
    int ex2 = ex - 1;
    int ey2 = by + blockH;
    EPD_DrawRectangle(bx, by, ex2, ey2, BLACK, 0);

    if (i < fullDays) {
      EPD_DrawRectangle(bx + 1, by + 1, ex2 - 1, ey2 - 1, BLACK, 1);
    } else if (i == fullDays && partial > 0.02f) {
      int partX = bx + 1 + (int)((ex2 - bx - 2) * partial);
      if (partX > bx + 1) {
        EPD_DrawRectangle(bx + 1, by + 1, partX, ey2 - 1, BLACK, 1);
      }
    }
  }
}

// Style 4 — Double-border chunky bar (used for Workday / Weekend)
static void dpBarChunky(int x, int y, int w, int h, float pct) {
  EPD_DrawRectangle(x,     y,     x + w,     y + h,     BLACK, 0);
  EPD_DrawRectangle(x + 2, y + 2, x + w - 2, y + h - 2, BLACK, 0);
  int fill = (int)((w - 8) * pct / 100.0f);
  if (fill > 0) {
    EPD_DrawRectangle(x + 4, y + 4, x + 4 + fill, y + h - 4, BLACK, 1);
  }
}

// ==================== MAIN DRAW ====================

static void dpDraw() {
  struct tm ti = {};
  bool timeOk = dashIsTimeSynced() && getLocalTime(&ti, 100);

  Paint_Clear(WHITE);

  // ---- Header ----
  EPD_DrawLine(0, 0, 792, 0, BLACK);
  EPD_DrawLine(0, 32, 792, 32, BLACK);
  EPD_ShowString(10, 8, (char*)"DAILY PROGRESS", 16, BLACK);

  if (timeOk) {
    const char* dayNames[]   = { "Sunday","Monday","Tuesday","Wednesday",
                                  "Thursday","Friday","Saturday" };
    const char* monthNames[] = { "Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec" };
    char hdrBuf[52];
    snprintf(hdrBuf, sizeof(hdrBuf), "%s  %s %d %d   %02d:%02d",
             dayNames[ti.tm_wday], monthNames[ti.tm_mon],
             ti.tm_mday, ti.tm_year + 1900,
             ti.tm_hour, ti.tm_min);
    EPD_ShowString(220, 8, hdrBuf, 16, BLACK);
  } else {
    EPD_ShowString(220, 8, (char*)"Time not synced - configure WiFi in Settings", 16, BLACK);
  }

  // ---- Footer ----
  EPD_DrawLine(0, 245, 792, 245, BLACK);
  EPD_ShowString(10, 251, (char*)"EXIT/HOME: Return   OK: Refresh", 16, BLACK);

  if (!timeOk) {
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    return;
  }

  // ==================== SECTION LAYOUT ====================
  // Available area: y=33 to y=244 = 211 px, split into 4 rows of 52 px each.
  // Each row: 18 px label text + 18 px bar + 16 px padding (= 52 px).
  // The Week row uses a taller combined label+block element in the same 52 px.
  const int BAR_X  = 10;
  const int BAR_W  = 772;
  const int ROW_H  = 52;
  const int LBL_H  = 18;   // height reserved for label text
  const int BAR_H  = 18;   // standard bar height

  int rowY[4];
  for (int r = 0; r < 4; r++) rowY[r] = 35 + r * ROW_H;

  // ---- Row 1: Year (solid fill) ----
  {
    float pct = dpYearPercent(ti);
    char lbl[52];
    snprintf(lbl, sizeof(lbl), "Year Progress - %d   %.1f%%",
             ti.tm_year + 1900, pct);
    EPD_ShowString(BAR_X, rowY[0], lbl, 16, BLACK);
    dpBarSolid(BAR_X, rowY[0] + LBL_H, BAR_W, BAR_H, pct);
  }

  // ---- Row 2: Month (day-segment bar, one block per day) ----
  {
    float pct  = dpMonthPercent(ti);
    int   days = dpDaysInMonth(ti.tm_year + 1900, ti.tm_mon + 1);
    const char* monthNames[] = { "January","February","March","April","May","June",
                                  "July","August","September","October","November","December" };
    char lbl[64];
    snprintf(lbl, sizeof(lbl), "Month Progress - %s (%d days)   %.1f%%",
             monthNames[ti.tm_mon], days, pct);
    EPD_ShowString(BAR_X, rowY[1], lbl, 16, BLACK);
    dpBarDaySegments(BAR_X, rowY[1] + LBL_H, BAR_W, BAR_H, pct, days);
  }

  // ---- Row 3: Week (7 labelled day columns) ----
  {
    float pct = dpWeekPercent(ti);
    char lbl[52];
    snprintf(lbl, sizeof(lbl), "Week Progress (Sun to Sat)   %.1f%%", pct);
    EPD_ShowString(BAR_X, rowY[2], lbl, 16, BLACK);
    // dpBarWeekColumns takes the space below the main label line.
    // It draws its own day-name labels then the blocks beneath them.
    // Total height available = ROW_H - LBL_H - 2 = 32 px.
    dpBarWeekColumns(BAR_X, rowY[2] + LBL_H + 2, BAR_W, ROW_H - LBL_H - 2, pct);
  }

  // ---- Row 4: Workday (9:00-17:00) or Weekend (Sat-Sun), double-border bar ----
  {
    float workPct = dpWorkdayPercent(ti);
    float wkndPct = dpWeekendPercent(ti);
    char lbl[64];
    float pct;

    if (workPct >= 0.0f) {
      // Currently a weekday, 9-5 window applies
      snprintf(lbl, sizeof(lbl), "Workday Progress (9:00 - 17:00)   %.1f%%", workPct);
      pct = workPct;
    } else if (wkndPct >= 0.0f) {
      // Weekend
      snprintf(lbl, sizeof(lbl), "Weekend Progress (Sat to Sun)   %.1f%%", wkndPct);
      pct = wkndPct;
    } else {
      // Weekday but outside work hours
      float secs = dpSecsToday(ti);
      const char* status = (secs < 9.0f * 3600.0f)
                           ? "Before workday (starts 9:00 AM)"
                           : "After workday  (ended  5:00 PM)";
      snprintf(lbl, sizeof(lbl), "Workday (9:00 - 17:00)   %s", status);
      pct = (secs >= 17.0f * 3600.0f) ? 100.0f : 0.0f;
    }

    EPD_ShowString(BAR_X, rowY[3], lbl, 16, BLACK);
    dpBarChunky(BAR_X, rowY[3] + LBL_H, BAR_W, BAR_H + 4, pct);
  }

  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// ==================== PUBLIC INTERFACE ====================

void progressInit() {
  using namespace DailyProgressNS;
  Serial.println("[PROGRESS] Initializing Daily Progress mode");
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, 800, 272, Rotation, WHITE);
  needsRefresh = true;
}

void progressUpdate() {
  using namespace DailyProgressNS;
  if (needsRefresh) {
    dpDraw();
    needsRefresh = false;
  }
}

bool progressHandleInput(bool upPressed, bool downPressed, bool okPressed, bool exitPressed) {
  using namespace DailyProgressNS;
  (void)upPressed;
  (void)downPressed;
  if (okPressed) {
    needsRefresh = true;   // manual refresh
    return true;
  }
  return !exitPressed;     // false → caller returns to home
}

#endif // DAILYPROGRESS_H
