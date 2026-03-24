#pragma once

// Main-screen dirty scheduler for coalesced partial redraws.
enum MainDirtyFlag : uint8_t {
  DIRTY_MAIN_NAME    = 1 << 0,
  DIRTY_MAIN_STATUS  = 1 << 1,
  DIRTY_MAIN_HAIR    = 1 << 2,
  DIRTY_MAIN_PRIMARY = 1 << 3,
  DIRTY_MAIN_PANEL   = 1 << 4,
};

static uint8_t mainDirtyFlags = 0;

static void markMainDirty(uint8_t flags) {
  mainDirtyFlags |= flags;
}

static void flushMainDirty() {
  if (mainDirtyFlags == 0) return;
  if (currentScreen != SCR_MAIN || inStandby) {
    mainDirtyFlags = 0;
    return;
  }

  display.startBuffering();
  if (mainDirtyFlags & DIRTY_MAIN_NAME) drawInputName();
  if (mainDirtyFlags & DIRTY_MAIN_STATUS) drawMainStatusChips();
  if (mainDirtyFlags & DIRTY_MAIN_HAIR) drawMainHairline();
  if (mainDirtyFlags & DIRTY_MAIN_PRIMARY) {
    if (!redrawMainPrimaryValueIncremental()) {
      clearPrimaryValueTextArea();
      drawMainPrimaryValue();
    }
  }
  if ((mainDirtyFlags & DIRTY_MAIN_PANEL) && detailPanelActive()) {
    drawSimpleDetailPanel();
  }
  display.endBuffering();
  mainDirtyFlags = 0;
}

static void redrawVolumeZone() {
  if (inStandby) { drawMainScreen(); return; }
  if (volBlinkCount > 0 && !volBlinkOn) return;  // blink is in 'uit'-fase — niet overschrijven
  markMainDirty(DIRTY_MAIN_PRIMARY);
  flushMainDirty();
  if (detailPanelActive()) {
    display.startBuffering();
    redrawDetailAttenCard();
    display.endBuffering();
  }
}
