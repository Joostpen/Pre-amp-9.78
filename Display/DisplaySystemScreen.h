#pragma once

static const uint16_t AUTO_STANDBY_OPTIONS_MIN[] = {0, 15, 30, 45, 60, 90, 120};

static const char* autoStandbyLabel(uint16_t minutes) {
  switch (minutes) {
    case 0:   return "Off";
    case 15:  return "15 min";
    case 30:  return "30 min";
    case 45:  return "45 min";
    case 60:  return "60 min";
    case 90:  return "90 min";
    case 120: return "120 min";
    default:  return "60 min";
  }
}

static void cycleAutoStandbySetting() {
  size_t currentIdx = 0;
  for (size_t i = 0; i < sizeof(AUTO_STANDBY_OPTIONS_MIN) / sizeof(AUTO_STANDBY_OPTIONS_MIN[0]); ++i) {
    if (AUTO_STANDBY_OPTIONS_MIN[i] == autoStandbyDelayMin) {
      currentIdx = i;
      break;
    }
  }
  currentIdx = (currentIdx + 1) % (sizeof(AUTO_STANDBY_OPTIONS_MIN) / sizeof(AUTO_STANDBY_OPTIONS_MIN[0]));
  autoStandbyDelayMin = AUTO_STANDBY_OPTIONS_MIN[currentIdx];
}

void drawSystemScreen() {
  currentScreen = SCR_SYS;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("SYSTEM");
  char buf[48];

  // Info rijen bovenaan
  const int16_t SYS_ROW1 = CONT_Y + 4;
  const int16_t SYS_ROW2 = SYS_ROW1 + SR_H + SYS_ROW_G;

  drawSysRow(SYS_ROW1, "Firmware build", FW_VERSION "  " __DATE__ " " __TIME__);
  if (diagBootDegraded) {
    sprintf(buf, "Degraded boot, %u recovery tries", (unsigned)diagRecoveryAttempts);
  } else {
    sprintf(buf, "Normal boot, %u recovery tries", (unsigned)diagRecoveryAttempts);
  }
  drawSysRow(SYS_ROW2, "Boot status", buf, true, !diagBootDegraded);

  // 2x rij knoppen — consistent met overige menu layouts
  const int16_t SYS_ROW1_N = 2;
  const int16_t SYS_ROW2_N = 3;
  const int16_t row1W = menuBtnW(SYS_ROW1_N);
  const int16_t row2W = menuBtnW(SYS_ROW2_N);
  const int16_t row2Y = MENU_BTN_Y;
  const int16_t row1Y = MENU_BTN_Y - MENU_BTN_H - MENU_BTN_GAP;

  drawMenuTogBtn(menuBtnX(0,row1W), row1Y, row1W, MENU_BTN_H,
                 "Auto stby", autoStandbyLabel(autoStandbyDelayMin),
                 autoStandbyDelayMin != SETTINGS_DEFAULT.autoStandbyDelayMin);
  drawMenuTogBtn(menuBtnX(1,row1W), row1Y, row1W, MENU_BTN_H,
                 "Warm stby", warmStandbyEnabled ? "Enabled" : "Disabled",
                 warmStandbyEnabled != SETTINGS_DEFAULT.warmStandbyEnabled);

  // Warm trigger: grayed als warm standby niet actief is
  if (warmStandbyEnabled) {
    drawMenuTogBtn(menuBtnX(0,row2W), row2Y, row2W, MENU_BTN_H,
                   "Warm trig", warmTrigRelayClosed ? "on" : "off",
                   warmTrigRelayClosed != SETTINGS_DEFAULT.warmTrigRelayClosed);
  } else {
    int16_t bx = menuBtnX(0,row2W), by = row2Y, bh = MENU_BTN_H;
    uint16_t bg  = dimC(C_CARD_BG);
    uint16_t bdr = dimC(scale565(VGC_BDR_NORM, 50));
    uint16_t lc  = dimC(scale565(VGC_LBL_NORM, 50));
    display.fillRoundRect(bx, by, row2W, bh, UI_CARD_R, bg);
    display.drawRoundRect(bx,   by,   row2W,   bh,   UI_CARD_R, bdr);
    display.drawRoundRect(bx+1, by+1, row2W-2, bh-2, UI_CARD_R, bdr);
    int16_t lblY = by + bh/3 + 2, valY = by + bh*2/3 + 8;
    AAFont_drawString(AA_XXS, "Warm trig", bx, lblY, lc, bg, AA_CENTER, row2W);
    AAFont_drawString(AA_XXS, warmTrigRelayClosed ? "on" : "off", bx, valY, lc, bg, AA_CENTER, row2W);
  }

  drawMenuTogBtn(menuBtnX(1,row2W), row2Y, row2W, MENU_BTN_H,
                 "Rem. trigger", remoteTriggerEnabled ? "Enabled" : "Disabled",
                 remoteTriggerEnabled != SETTINGS_DEFAULT.remoteTriggerEnabled);
  drawMenuNavBtn(menuBtnX(2,row2W), row2Y, row2W, MENU_BTN_H, "Diagnostics \xBB");
  display.endBuffering();
}
