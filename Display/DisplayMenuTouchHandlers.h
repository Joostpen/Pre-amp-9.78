/*
 * DisplayMenuTouchHandlers.h
 *
 * Extracted menu/subscreen touch handlers from Display.cpp to keep the main
 * display translation unit more navigable while preserving the same static
 * scope and behavior. Included directly from Display.cpp.
 */

static inline void saveAndRedraw(void (*redrawFn)()) {
  saveSettings();
  redrawFn();
}

static void redrawVGToggleRowStrip() {
  display.startBuffering();
  display.fillRect(16, MENU_BTN_Y, SCREEN_W-32, MENU_BTN_H, C_BG);
  drawVGToggleRow(16, MENU_BTN_Y, SCREEN_W-32);
  display.endBuffering();
}

static void handleVolumeGainTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { vgSelection=VG_NONE; drawMainMenu(); return; }

  // Slider rijen 0–3: aantikken selecteert voor encoder
  static const int8_t rowId[3] = { VG_MAXVOL, VG_BALANCE, VG_LFGAIN };
  for (uint8_t r = 0; r < 3; r++) {
    int16_t ry = vgRowY(r);
    if (ty >= ry && ty < ry + SR_H) {
      int8_t hitId = rowId[r];
      int8_t prev  = vgSelection;
      vgSelection  = (vgSelection == hitId) ? VG_NONE : hitId;
      if (prev >= 0 && prev != vgSelection) vgRedrawRow(prev);
      if (vgSelection >= 0)                 vgRedrawRow(vgSelection);
      return;
    }
  }

  // Toggle-rij: 5 knoppen
  {
    const int16_t N  = 5;
    const int16_t bw = menuBtnW(N);

    if (ty >= MENU_BTN_Y && ty < MENU_BTN_Y + MENU_BTN_H) {
      for (int8_t i = 0; i < N; i++) {
        int16_t bx = menuBtnX(i, bw);
        if (tx >= bx && tx < bx + bw) {
          switch (i) {
            case 0: muteOnStartup = !muteOnStartup; saveAndRedraw(redrawVGToggleRowStrip); return;
            case 1: volUnitsMode = (uint8_t)((volUnitsMode + 1) % 3); saveAndRedraw(redrawVGToggleRowStrip); return;
            case 2: volumeCurve = (uint8_t)((volumeCurve + 1) % 3); saveAndRedraw(redrawVGToggleRowStrip); return;
            case 3: encSensitivity = (uint8_t)((encSensitivity + 1) % 3); saveAndRedraw(redrawVGToggleRowStrip); return;
            case 4: drawTransformerScreen(); return;
          }
        }
      }
    }
  }
}


static void handleOffsetsTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { vgSelection=VG_NONE; drawMainMenu(); return; }

  const int16_t PAD     = VG_PAD;
  const int16_t TOTAL_W = SCREEN_W - PAD * 2;
  const int16_t DIV_X   = PAD + TOTAL_W / 2;
  const int16_t DIV_GAP = 8;
  const int16_t COL_W   = TOTAL_W / 2 - DIV_GAP;
  const int16_t COL_L_X = PAD;
  const int16_t COL_R_X = DIV_X + DIV_GAP;
  const int16_t GAP     = 10;
  const int16_t ROW_Y0  = CONT_Y + 42;

  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    int16_t ry = ROW_Y0 + i * (SR_H + GAP);
    if (ty < ry || ty >= ry + SR_H) continue;

    bool inLeft  = (tx >= COL_L_X && tx < DIV_X);
    bool inRight = (tx >= COL_R_X && tx < COL_R_X + COL_W);

    int8_t newSel = VG_NONE;
    if (inLeft)                              newSel = VG_IO_BASE + i;
    else if (inRight && i != surroundInput)  newSel = VG_SV_BASE + i;

    if (newSel == VG_NONE) return;

    int8_t prev = vgSelection;
    vgSelection = (vgSelection == newSel) ? VG_NONE : newSel;
    if (prev >= 0 && prev != vgSelection) vgRedrawRow(prev);
    if (vgSelection >= 0)                 vgRedrawRow(vgSelection);
    return;
  }
}
static void handleNamesTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) {
    namesSelection=-1; editLen=0; editBuffer[0]='\0'; kbShift=false;
    drawMainMenu(); return;
  }
  const int16_t LIST_Y=CONT_Y+4, ROW_SH=46;
  const int16_t NAME_X=130, NAME_W=490;
  const int16_t BP_X=632, BP_W=148;
  for (uint8_t i=0; i<INPUT_COUNT; i++) {
    int16_t ry=LIST_Y+i*ROW_SH;
    if (ty>=ry && ty<ry+ROW_SH) {
      // Dedicated bypass toggle column
      if (tx >= BP_X && tx <= BP_X + BP_W) {
        surroundInput = i;
        saveAndRedraw(drawNamesScreen);
        return;
      }

      // Name area: tap opent groot QWERTY keyboard scherm
      if (tx < NAME_X || tx > NAME_X + NAME_W) return;

      // Sla eventuele lopende bewerking op
      if (namesSelection>=0 && namesSelection!=(int8_t)i && editLen>0) {
        strncpy(inputNames[namesSelection], editBuffer, 15);
        inputNames[namesSelection][15]='\0';
      }
      namesSelection=i;
      strncpy(editBuffer, inputNames[i], 15); editBuffer[15]='\0';
      editLen=strlen(editBuffer); kbShift=false;
      drawKbEditScreen(); return;
    }
  }
  if (namesSelection<0) return;
  const char** rows = kbShift ? KB_UPPER : KB_LOWER;
  for (int row=0; row<KB_NROWS; row++) {
    int nk=strlen(rows[row]);
    for (int col=0; col<nk; col++) {
      int16_t kx=kbKeyX(row,col), ky=kbKeyY(row);
      if (tx>=kx && tx<=kx+KB_KEY_H && ty>=ky && ty<=ky+KB_KEY_H) {
        kbHandleKey(rows[row][col]); return;
      }
    }
  }
  if (ty>=KB_SP_Y && ty<=KB_SP_Y+KB_SP_H) {
    if (tx>=KB_SHIFT_X&&tx<=KB_SHIFT_X+KB_SHIFT_W) { kbShift=!kbShift; drawNamesScreen(); return; }
    if (tx>=KB_SPC_X  &&tx<=KB_SPC_X+KB_SPC_W)     { kbHandleKey(' '); return; }
    if (tx>=KB_DEL_X  &&tx<=KB_DEL_X+KB_DEL_W)     { kbHandleDel();    return; }
    if (tx>=KB_OK_X   &&tx<=KB_OK_X+KB_OK_W)        { kbHandleOK();     return; }
  }
}

static void handleDisplayTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { dsSelection = VG_NONE; drawMainMenu(); return; }

  const int16_t LX = 16, LW = SCREEN_W - 32;

  // Rijen 0–3: aantikken selecteert slider voor encoder
  static const int16_t rowY[4] = { DS_ROW0_Y, DS_ROW1_Y, DS_ROW2_Y, DS_ROW3_Y };
  for (int r = 0; r < 4; r++) {
    if (ty >= rowY[r] && ty < rowY[r] + SR_H) {
      int8_t prev = dsSelection;
      dsSelection = (dsSelection == r) ? VG_NONE : (int8_t)r;
      if (prev != dsSelection) {
        saveAndRedraw(drawDimSettingsScreen);
        if (r == 0 || prev == 0) setScreenBrightness(activeBrightness());
      }
      return;
    }
  }

  // Knoppen onderaan — shared helpers
  if (ty >= MENU_BTN_Y && ty < MENU_BTN_Y + MENU_BTN_H) {
    const int16_t DN  = 3;
    const int16_t dbw = menuBtnW(DN);
    for (int8_t i = 0; i < DN; i++) {
      if (tx >= menuBtnX(i,dbw) && tx < menuBtnX(i,dbw)+dbw) {
        switch(i) {
          case 0: detailMode = (detailMode==1)?0:1; detailPanelSuppressedByDim=false; saveAndRedraw(drawDimSettingsScreen); return;
          case 1: detailVisibleOnDim = !detailVisibleOnDim; saveAndRedraw(drawDimSettingsScreen); return;
          case 2: dsSelection = VG_NONE; drawThemeMotionScreen(); return;
        }
      }
    }
    return;
  }
}


static void handleThemeMotionTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { drawDimSettingsScreen(); return; }

  // Rij 0: Accent strength — tap selecteert voor encoder
  if (ty >= DS_ROW0_Y && ty < DS_ROW0_Y + SR_H) {
    int8_t prev = dsSelection;
    dsSelection = (dsSelection == 0) ? VG_NONE : 0;
    if (prev != dsSelection) { saveAndRedraw(drawThemeMotionScreen); }
    return;
  }

  // Rij 1: Color presets 0..3
  if (ty >= DS_ROW1_Y && ty < DS_ROW1_Y + SR_H) {
    const int16_t oW=88, oG=UI_CTRL_GAP;
    for (int i=0;i<4;i++) {
      int16_t ox=DS_CTRL_X+i*(oW+oG);
      if (tx>=ox&&tx<=ox+oW) { accentColor=ACCENT_PRESETS[i]; saveAndRedraw(drawThemeMotionScreen); break; }
    }
    return;
  }

  // Rij 2: Color presets 4..7
  if (ty >= DS_ROW2_Y && ty < DS_ROW2_Y + SR_H) {
    const int16_t oW=88, oG=UI_CTRL_GAP;
    for (int i=0;i<4;i++) {
      int16_t ox=DS_CTRL_X+i*(oW+oG);
      if (tx>=ox&&tx<=ox+oW) { accentColor=ACCENT_PRESETS[i+4]; saveAndRedraw(drawThemeMotionScreen); break; }
    }
    return;
  }

  // Rij 3: Hue tune balk
  if (ty >= DS_ROW3_Y && ty < DS_ROW3_Y + SR_H) {
    const int16_t btnW_t = 44, gap_t = 6;
    const int16_t mnX_t  = DS_CTRL_X;
    const int16_t barX   = mnX_t + btnW_t + gap_t;
    const int16_t barW   = (16 + SCREEN_W - 32) - barX - gap_t - btnW_t - 16;
    const int16_t plX_t  = barX + barW + gap_t;
    bool ch = false;
    uint8_t step = accentHueStepFromColor(accentColor);
    if (tx >= mnX_t && tx <= mnX_t + btnW_t) {
      step = (uint8_t)((step + 47) % 48); ch = true;
    } else if (tx >= plX_t && tx <= plX_t + btnW_t) {
      step = (uint8_t)((step + 1) % 48); ch = true;
    } else if (tx >= barX && tx <= barX + barW) {
      step = (uint8_t)constrain((int32_t)(tx - barX) * 48 / barW, 0, 47); ch = true;
    }
    if (ch) { accentColor = hueStepToColor565(step); saveAndRedraw(drawThemeMotionScreen); }
    return;
  }

  // Knoppen onderaan: Font | Detail color | Large on dim
  if (ty >= MENU_BTN_Y && ty < MENU_BTN_Y + MENU_BTN_H) {
    const int16_t TN  = 3;
    const int16_t tbw = menuBtnW(TN);
    if (tx >= menuBtnX(0,tbw) && tx < menuBtnX(0,tbw)+tbw) {
      mainFontMode = (uint8_t)((mainFontMode + 1) % 3); saveAndRedraw(drawThemeMotionScreen); return;
    }
    if (tx >= menuBtnX(1,tbw) && tx < menuBtnX(1,tbw)+tbw) {
      detailColorFollow = !detailColorFollow; saveAndRedraw(drawThemeMotionScreen); return;
    }
    if (tx >= menuBtnX(2,tbw) && tx < menuBtnX(2,tbw)+tbw) {
      largeFontOnDim = !largeFontOnDim; saveAndRedraw(drawThemeMotionScreen); return;
    }
  }
}

static void handleSystemTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { drawMainMenu(); return; }

  const int16_t row1Y = MENU_BTN_Y - MENU_BTN_H - MENU_BTN_GAP;
  const int16_t row2Y = MENU_BTN_Y;
  const int16_t row1W = menuBtnW(3);
  const int16_t row2W = menuBtnW(2);

  if (ty >= row1Y && ty < row1Y + MENU_BTN_H) {
    for (int8_t i = 0; i < 3; i++) {
      if (tx >= menuBtnX(i,row1W) && tx < menuBtnX(i,row1W)+row1W) {
        switch(i) {
          case 0:
            cycleAutoStandbySetting();
            saveAndRedraw(drawSystemScreen);
            return;
          case 1:
            warmStandbyEnabled = !warmStandbyEnabled;
            saveAndRedraw(drawSystemScreen);
            return;
          case 2:
            if (warmStandbyEnabled) {
              warmTrigRelayClosed = !warmTrigRelayClosed;
              saveAndRedraw(drawSystemScreen);
            }
            return;
        }
      }
    }
  }

  if (ty >= row2Y && ty < row2Y + MENU_BTN_H) {
    for (int8_t i = 0; i < 2; i++) {
      if (tx >= menuBtnX(i,row2W) && tx < menuBtnX(i,row2W)+row2W) {
        switch(i) {
          case 0:
            remoteTriggerEnabled = !remoteTriggerEnabled;
            if (!remoteTriggerEnabled) {
              digitalWrite(PIN_REMOTE_TRIG, LOW);
            } else {
              resetRemoteTrigger();  // Herstart delay-venster zodat trigger alsnog fired
            }
            saveAndRedraw(drawSystemScreen);
            return;
          case 1:
            diagLastRefreshMs = millis();
            drawDiagnosticsScreen();
            return;
        }
      }
    }
  }
}

static void handleDiagnosticsTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { drawSystemScreen(); return; }

  // Now-knop: directe refresh met visuele feedback
  // drawDispActionRow tekent de knop rechts in SYS_Y7 rij
  const int16_t X = 16, W = SCREEN_W - 32;
  const int16_t oW = 130, btnH = SR_H - 20, btnY = SYS_Y7 + 10;
  const int16_t oX = X + W - UI_PAD_R - oW;
  if (ty >= SYS_Y7 && ty <= SYS_Y7 + SR_H && tx >= oX && tx <= oX + oW) {
    // Visuele feedback: knop flitst
    display.startBuffering();
    display.fillRoundRect(oX, btnY, oW, btnH, UI_BTN_R, dimC(VGC_BDR_SEL));
    AAFont_drawString(AA_XXS, "Refreshing...", oX, SYS_Y7 + SR_H/2 + 9,
                      dimC(VGC_BG_NORM), dimC(VGC_BDR_SEL), AA_CENTER, oW);
    display.endBuffering();
    drawDiagnosticsScreen();
    return;
  }
}



static void updateIRLearnRows() {
  // Herteken alleen de variabele IR-gegevens + actieknoppen.
  drawIRDataRow();

  uint8_t target = irLearnTarget;
  bool hasLearnedForTarget = irProtocolMap[target] != IR_PROTOCOL_UNKNOWN;
  drawIRActionButtons(hasLearnedForTarget && !irLearnArmed);
  drawIRResetButton();
}


static void performIRResetAllCodes() {
  resetIRMappingsToDefaults();
  irLearnArmed = false;
  flushSettings();
  drawIRLearnScreen();
}

static void handleIRLearnTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { irLearnArmed = false; drawMainMenu(); return; }

  // Function rij — volgende slot
  if (ty >= SYS_Y1 && ty <= SYS_Y1 + SR_H) {
    irLearnTarget = (uint8_t)((irLearnTarget + 1) % IR_ACTION_COUNT);
    irLearnArmed = false;
    drawIRLearnScreen();
    return;
  }

  // Learn + Save op dezelfde rij
  if (ty >= IR_ACT_Y && ty <= IR_ACT_Y + IR_BTN_H) {
    uint8_t i = irLearnTarget;
    bool hasCode = irProtocolMap[i] != IR_PROTOCOL_UNKNOWN;

    if (tx >= IR_LEARN_X && tx <= IR_LEARN_X + IR_ACT_W) {
      irLearnArmed = !irLearnArmed;
      updateIRLearnRows();
      return;
    }

    if (tx >= IR_SAVE_X && tx <= IR_SAVE_X + IR_ACT_W) {
      if (hasCode && !irLearnArmed) {
        flushSettings();
        uint16_t bg = dimC(C_STATUS_OK_FILL);
        display.fillRoundRect(IR_SAVE_X, IR_ACT_Y, IR_ACT_W, IR_BTN_H, UI_BTN_R + 1, bg);
        display.drawRoundRect(IR_SAVE_X, IR_ACT_Y, IR_ACT_W, IR_BTN_H, UI_BTN_R + 1, dimC(C_STATUS_OK));
        AAFont_drawString(AA_XXS, "Saved!", IR_SAVE_X, IR_ACT_Y + IR_BTN_H/2 + 9,
                          dimC(C_WHITE), bg, AA_CENTER, IR_ACT_W);
        irSavedFeedbackMs = millis();  // updateDisplay wist na IR_SAVED_FEEDBACK_MS
      }
      return;
    }
  }

}
