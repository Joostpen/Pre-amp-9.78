/*
 * InputControl.cpp - Input Selection Control
 * PHI Reference Pre-10
 *
 * Input-wissel logica:
 *  - adjustInput()     : meteen mute + scherm tonen, timer starten
 *  - tickInputSwitch() : elke loop-iteratie — wacht op stilstand, dan unmute
 *
 * Zo worden tussenliggende ingangen nooit hoorbaar geactiveerd bij snel draaien.
 */

#include "InputControl.h"
#include "Controls.h"
#include "MCP23017.h"
#include "Display.h"
#include "Settings.h"
#include "VolumeControl.h"

// Extern globals
extern uint8_t  currentInput;
extern uint8_t  currentVolume;
extern uint8_t  surroundInput;
extern uint8_t  startupVolume[INPUT_COUNT];
extern bool     inStandby;
extern bool     isMuted;

// Input bit patterns op MCP23017 GPA (bits 3..7)
static const uint8_t inputBits[] = {0x80, 0x40, 0x20, 0x10, 0x08};

// Extern bypass state (gedefinieerd in Pre-Amplifier.ino)
extern bool bypassLF;
extern bool bypassMF;

// Volume per ingang
uint8_t savedVolume[INPUT_COUNT] = {
  SETTINGS_DEFAULT.savedVolume[0], SETTINGS_DEFAULT.savedVolume[1], SETTINGS_DEFAULT.savedVolume[2],
  SETTINGS_DEFAULT.savedVolume[3], SETTINGS_DEFAULT.savedVolume[4]
};

// Poortnamen
static const char* INPUT_TYPES[INPUT_COUNT] = { "XLR","XLR","XLR","RCA","RCA" };

void getPortStr(uint8_t idx, char* buf) {
  uint8_t n = 1;
  for (uint8_t i = 0; i < idx; i++)
    if (strcmp(INPUT_TYPES[i], INPUT_TYPES[idx]) == 0) n++;
  sprintf(buf, "%s-%d", INPUT_TYPES[idx], n);
}

void setInput(uint8_t input) {
  uint8_t gpa = inputBits[input];
  if (bypassLF) gpa |= CTRL_GPA_BYPASS_LF;
  if (bypassMF) gpa |= CTRL_GPA_BYPASS_MF;
  writeRegister(I2C_ADDR_CONTROL, GPIOA, gpa);
}

void resetSessionInputVolumes() {
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    savedVolume[i] = (i == surroundInput)
                   ? 231
                   : (uint8_t)min((int)startupVolume[i], (int)effectiveMaxForInput(i));
  }
  currentVolume = (currentInput == surroundInput)
                ? 231
                : (uint8_t)min((int)savedVolume[currentInput], (int)effectiveMaxForInput(currentInput));
}

// ── Debounce state ─────────────────────────────────────────────────────────
#define INPUT_SETTLE_MS   400    // Wachttijd na laatste draai

static bool     switchPending = false;
static uint8_t  pendingInput  = 0;
static uint32_t lastAdjustMs  = 0;

static void queueInputSwitch(uint8_t targetInput) {
  uint8_t base = switchPending ? pendingInput : currentInput;
  if (targetInput >= INPUT_COUNT || targetInput == base) return;

  pendingInput = targetInput;
  lastAdjustMs = millis();

  if (!switchPending) {
    // Eerste wijziging: mute aan, scherm in wissel-modus
    switchPending = true;
    setSwitchingInput(true);
    savedVolume[currentInput] = currentVolume;
    muteRelay(true);
  }

  // Activiteit verlengen zonder onnodige full-wake/redraw.
  notifyActivityNoWake();

  // Toon target-invoer, maar schakel hardware nog NIET.
  // Pas schakelen na encoder-stilstand voorkomt onnodige tussenstappen.
  showSwitchingScreen(pendingInput);
}

bool isInputSwitchPending() {
  return switchPending;
}

uint8_t getInputSwitchProgressPct() {
  if (!switchPending) return 100;
  uint32_t dt = millis() - lastAdjustMs;
  if (dt >= INPUT_SETTLE_MS) return 100;
  return (uint8_t)((dt * 100UL) / INPUT_SETTLE_MS);
}

// ── adjustInput ─────────────────────────────────────────────────────────────
void adjustInput(int delta) {
  if (inStandby) return;

  // Optelbaar doordraaien: gebruik pendingInput als basis
  uint8_t base = switchPending ? pendingInput : currentInput;
  int newInput = constrain((int)base + delta, 0, INPUT_COUNT - 1);
  queueInputSwitch((uint8_t)newInput);
}

void selectInput(uint8_t input) {
  if (inStandby) return;
  queueInputSwitch(input);
}

void cancelInputSwitch() {
  if (!switchPending) return;

  switchPending = false;
  pendingInput  = currentInput;
  setSwitchingInput(false);
}

// ── tickInputSwitch ─────────────────────────────────────────────────────────
// Aanroepen vanuit hoofdloop — rondt wissel af na stilstand encoder
void tickInputSwitch() {
  if (!switchPending) return;
  if ((millis() - lastAdjustMs) < INPUT_SETTLE_MS) return;

  // Schakel nu pas de hardware-ingang (relay staat al dicht door mute)
  currentInput = pendingInput;
  setInput(currentInput);
  delay(8);    // ingang relais settle

  // Volume van nieuwe ingang laden terwijl nog gemute is
  if (currentInput == surroundInput) {
    currentVolume = 231;
  } else {
    currentVolume = (uint8_t)min((int)savedVolume[currentInput],
                                 (int)effectiveMaxForInput(currentInput));
  }
  applyVolume();

  delay(20);   // mute-relay settle voor unmute

  if (!isMuted) muteRelay(false);

  notifyActivityNoWake();
  updateAfterInputSwitch();
  switchPending = false;

}
