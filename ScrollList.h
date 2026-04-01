#ifndef SCROLLLIST_H
#define SCROLLLIST_H

#include "EPD.h"
#include <Arduino.h>

// ==================== SCROLL LIST ====================
// Reusable scrollable list component for PaperPal EPD display.
// Provides consistent navigation state, selection rendering, and scroll arrows
// that match the home-screen style: outline selection box + ">" indicator.
//
// Usage pattern:
//
//   static const ScrollListConfig CFG = { visibleCount, itemHeight, startY,
//                                         leftMargin, rightEdge };
//   static ScrollListState lst;
//   scrollListInit(lst);
//
//   // Navigation (in input handler, call on each button press):
//   if (downPressed) scrollListDown(lst, CFG, itemCount);
//   if (upPressed)   scrollListUp  (lst, CFG, itemCount);
//
//   // Rendering (in draw function — call once per refresh):
//   for (int i = 0; i < CFG.visibleCount; i++) {
//     int idx   = lst.scrollOffset + i;
//     if (idx >= itemCount) break;
//     int yRow  = scrollListRowY (CFG, i);
//     int yText = scrollListTextY(CFG, i);
//     if (idx == lst.selectedIdx) scrollListDrawSelection(CFG, yRow);
//     EPD_ShowString(CFG.leftMargin + 16, yText, (char*)items[idx], 16, BLACK);
//   }
//   scrollListDrawArrows(lst, CFG, itemCount);
// =====================================================

// ---- Configuration (typically a compile-time constant) ----
struct ScrollListConfig {
  int visibleCount;  // number of items visible at once
  int itemHeight;    // pixels per item row
  int startY;        // Y pixel of the top of the first visible row
  int leftMargin;    // X position of the ">" indicator
                     //   item text should start at leftMargin + 16
  int rightEdge;     // right edge of the selection box and scroll arrows
};

// ---- State (one instance per list, persisted between renders) ----
struct ScrollListState {
  int selectedIdx;   // currently highlighted item index (0-based, absolute)
  int scrollOffset;  // index of the first visible item
};

// Initialise (or reset) a scroll list state to the top.
inline void scrollListInit(ScrollListState& s) {
  s.selectedIdx  = 0;
  s.scrollOffset = 0;
}

// ---- Navigation ----

// Move selection one step down; wraps around from last item to first.
inline void scrollListDown(ScrollListState& s, const ScrollListConfig& cfg, int itemCount) {
  if (itemCount <= 0) return;
  s.selectedIdx++;
  if (s.selectedIdx >= itemCount) {
    // Wrap to top
    s.selectedIdx  = 0;
    s.scrollOffset = 0;
  } else if (s.selectedIdx >= s.scrollOffset + cfg.visibleCount) {
    // Scroll viewport down
    s.scrollOffset = s.selectedIdx - cfg.visibleCount + 1;
  }
}

// Move selection one step up; wraps around from first item to last.
inline void scrollListUp(ScrollListState& s, const ScrollListConfig& cfg, int itemCount) {
  if (itemCount <= 0) return;
  s.selectedIdx--;
  if (s.selectedIdx < 0) {
    // Wrap to bottom
    s.selectedIdx  = itemCount - 1;
    s.scrollOffset = (itemCount > cfg.visibleCount) ? itemCount - cfg.visibleCount : 0;
  } else if (s.selectedIdx < s.scrollOffset) {
    // Scroll viewport up
    s.scrollOffset = s.selectedIdx;
  }
}

// ---- Position helpers ----

// Y pixel of the top of visible row 'rowIndex' (0-based within viewport).
inline int scrollListRowY(const ScrollListConfig& cfg, int rowIndex) {
  return cfg.startY + rowIndex * cfg.itemHeight;
}

// Y pixel for 16-px text centred vertically inside a row.
inline int scrollListTextY(const ScrollListConfig& cfg, int rowIndex) {
  return scrollListRowY(cfg, rowIndex) + (cfg.itemHeight - 16) / 2;
}

// ---- Rendering ----

// Draw the selection outline box and ">" arrow for a highlighted row.
// Call this before drawing the item's text content.
inline void scrollListDrawSelection(const ScrollListConfig& cfg, int yRow) {
  EPD_DrawRectangle(5, yRow,
                    cfg.rightEdge, yRow + cfg.itemHeight - 1,
                    BLACK, 0);
  int yText = yRow + (cfg.itemHeight - 16) / 2;
  EPD_ShowString(cfg.leftMargin, yText, (char*)">", 16, BLACK);
}

// Draw ^ and v scroll arrows when the list extends beyond the visible viewport.
// Call this after the item loop.
inline void scrollListDrawArrows(const ScrollListState& s,
                                  const ScrollListConfig& cfg,
                                  int itemCount) {
  if (s.scrollOffset > 0) {
    EPD_ShowString(cfg.rightEdge - 14, cfg.startY, (char*)"^", 16, BLACK);
  }
  if (s.scrollOffset + cfg.visibleCount < itemCount) {
    int arrowY = cfg.startY + cfg.visibleCount * cfg.itemHeight - 20;
    EPD_ShowString(cfg.rightEdge - 14, arrowY, (char*)"v", 16, BLACK);
  }
}

#endif // SCROLLLIST_H
