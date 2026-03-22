/*
 * GainControl.cpp - LF/MF gain en surround ingang beheer
 * PHI Reference Pre-10
 */

#include "GainControl.h"
#include "Controls.h"
#include "MCP23017.h"
#include "Settings.h"
#include "VolumeControl.h"
#include "Display.h"
#include "InputControl.h"
#include <Arduino.h>

// ── Gain state ────────────────────────────────────────────────────────────────
int8_t  gainLF        =  0;  // 0 × 0.5 dB = 0 dB (default), bereik -7..+24 = -3.5..+12.0 dB
int8_t  gainLFOUT     = -12;  // -12 = -6dB (default)
int8_t  gainMFOUT     = -12;  // -12 = -6dB aan
uint8_t surroundInput =   4;  // RCA-2 standaard
uint8_t startupVolume[INPUT_COUNT] = {231, 231, 231, 231, 231};  // 0 dB per ingang bij eerste opstart

const int8_t GAIN_MIN[5] = {  -7, -12, -12,               0, 0 };  // LF gain min = -7 = -3.5dB (hwVal=0)
const int8_t GAIN_MAX[5] = {  24,   0,   0, INPUT_COUNT - 1, 0 };

// ── GPB register shadow (mute relay zit ook in GPB) ──────────────────────────
extern uint8_t controlPortB;  // Beheerd door Controls.cpp
extern uint8_t savedVolume[];  // Gedefinieerd in InputControl.cpp
extern uint8_t currentInput;   // Gedefinieerd in Pre-Amplifier.ino
extern bool    bypassLF;        // Gedefinieerd in Pre-Amplifier.ino
extern bool    bypassMF;        // Gedefinieerd in Pre-Amplifier.ino

static void writeGPB(uint8_t val) {
  controlPortB = val;
  writeRegister(I2C_ADDR_CONTROL, GPIOB, val);
}

// ── Hardware apply ────────────────────────────────────────────────────────────
void applyGainLF() {
  int8_t clamped = constrain(gainLF, GAIN_MIN[0], GAIN_MAX[0]);
  uint8_t hwVal  = (uint8_t)(clamped + 7);  // 0..31 in 5 bits
  uint8_t gpb    = (controlPortB & 0xE0) | (hwVal & 0x1F);
  writeGPB(gpb);
}

void applyGainLFOUT() {
  muteRelay(true);                          // relay dicht via state machine
  delay(10);                                // relay settle
  uint8_t gpb = controlPortB;              // lees shadow — niet muteren via |=
  // GPB7: relay actief (bit hoog) = 0dB, relay inactief (bit laag) = -6dB (default)
  if (gainLFOUT == 0) gpb |=  0x80;
  else                gpb &= ~0x80;
  writeGPB(gpb);                            // writeGPB updatet controlPortB
  delay(20);                                // relay settle
  if (!isMuted) muteRelay(false);           // herstel via state machine
}

void applyGainMFOUT() {
  muteRelay(true);
  delay(10);
  uint8_t gpb = controlPortB;
  // GPB6: relay actief (bit hoog) = 0dB, relay inactief (bit laag) = -6dB (default)
  if (gainMFOUT == 0) gpb |=  0x40;
  else                gpb &= ~0x40;
  writeGPB(gpb);
  delay(20);                                // relay settle
  if (!isMuted) muteRelay(false);           // herstel via state machine
}

// Roep applyGainAll() ALLEEN aan vanuit setup() — de functie bevat blocking delays
// (~60ms totaal) via applyGainLFOUT/MFOUT en mag de loop() niet blokkeren.
void applyGainAll() {
  applyGainLF();
  applyGainLFOUT();
  applyGainMFOUT();
}

// Zet alle audio-relais hardware uit bij standby — software waarden blijven bewaard.
// Volume naar 0x00, out-relais uit, bypass relais uit, ingang uit, LF gain uit.
// Na standby-uit worden de waarden hersteld via applyGainAll() + applyBypass() + setInput().
void standbyRelaysOff() {
  // Zet alle audio-relais hardware naar veilige toestand.
  // Volume chips: volledig stil
  setVolume(0x00, 0x00);

  // GPB: alle audio-bits uit. Mute relay (bit5) gesloten houden — dat is de veilige staat.
  // bits 0-4: LF gain relais uit
  // bit6: MF out relay uit
  // bit7: LF out relay uit
  // bit5 (MUTE_RELAY_BIT): blijft zoals gezet door applyRelayState(true) ervoor
  uint8_t gpb = controlPortB;
  gpb &= ~0x1F;   // LF gain bits 0-4 uit
  gpb &= ~0xC0;   // LF out (bit7) + MF out (bit6) uit
  writeGPB(gpb);

  // GPA: alle bits laag — geen ingang geselecteerd, geen bypass actief
  writeRegister(I2C_ADDR_CONTROL, GPIOA, 0x00);
}

// ── Transformer bypass (GPA0 = LF, GPA1 = MF) ────────────────────────────────
// GPA van control chip bevat ook de ingangsselectie (GPA3..GPA7).
// applyBypass() leest de huidige ingang en combineert die met bypass bits.
static void applyBypass() {
  extern uint8_t currentInput;
  static const uint8_t inputBitsLocal[] = {0x80, 0x40, 0x20, 0x10, 0x08};
  uint8_t gpa = inputBitsLocal[currentInput];
  if (bypassLF) gpa |= CTRL_GPA_BYPASS_LF;  // bit0
  if (bypassMF) gpa |= CTRL_GPA_BYPASS_MF;  // bit1
  writeRegister(I2C_ADDR_CONTROL, GPIOA, gpa);
}

void applyBypassLF() {
  if (bypassLF) {
    // Bypass actief: LF out relay altijd hardware-uit, ongeacht gainLFOUT
    // Software waarde blijft bewaard voor herstel als bypass uitgaat
    uint8_t gpb = controlPortB;
    gpb &= ~0x80;   // bit7 laag = relay inactief = -6dB circuit
    writeGPB(gpb);
  } else {
    // Bypass inactief: herstel software waarde naar hardware
    applyGainLFOUT();
  }
  applyBypass();
}

void applyBypassMF() {
  if (bypassMF) {
    // Bypass actief: MF out relay altijd hardware-uit, ongeacht gainMFOUT
    uint8_t gpb = controlPortB;
    gpb &= ~0x40;   // bit6 laag = relay inactief = -6dB circuit
    writeGPB(gpb);
  } else {
    // Bypass inactief: herstel software waarde naar hardware
    applyGainMFOUT();
  }
  applyBypass();
}

// ── Waarde formatteren voor weergave (legacy — niet meer actief aangeroepen) ──
void formatGainVal(int i, char* buf) {
  if (i == 4) {
    float db = (startupVolume[0] * 0.5f) - 115.5f;
    if (db >= 0.0f) sprintf(buf, "+%.1f dB", db);
    else            sprintf(buf, "%.1f dB",  db);
  } else if (i == 3) {
    char p[8]; getPortStr(surroundInput, p); strcpy(buf, p);
  } else if (i == 1 || i == 2) {
    int8_t* v = (i == 1) ? &gainLFOUT : &gainMFOUT;
    strcpy(buf, (*v == 0) ? "0 dB" : "-6 dB");
  } else {
    float db = gainLF * 0.5f;
    if (db == (int16_t)db) sprintf(buf, "%d dB", (int16_t)db);
    else if (db >= 0)      sprintf(buf, "+%.1f dB", db);
    else                   sprintf(buf, "%.1f dB",  db);
  }
}

// ── Encoder aanpassing (legacy — niet meer actief aangeroepen) ────────────────
void adjustGain(int gainIndex, int delta) {
  if (gainIndex == 4) {
    int newVol = constrain((int)startupVolume[0] + delta, 0, 255);
    startupVolume[0] = (uint8_t)newVol;
    for (uint8_t i = 0; i < INPUT_COUNT; i++) {
      if (i != surroundInput) savedVolume[i] = startupVolume[0];
    }
    // saved on standby entry
  } else if (gainIndex == 3) {
    surroundInput = (uint8_t)constrain((int)surroundInput + delta, 0, INPUT_COUNT-1);
    // saved on standby entry
  } else if (gainIndex == 1 || gainIndex == 2) {
    int8_t* v = (gainIndex == 1) ? &gainLFOUT : &gainMFOUT;
    *v = (*v == 0) ? -12 : 0;
    if (gainIndex == 1) applyGainLFOUT();
    else                applyGainMFOUT();
    // saved on standby entry
  } else {
    gainLF = (int8_t)constrain((int)gainLF + delta, GAIN_MIN[0], GAIN_MAX[0]);
    applyGainLF();
    // saved on standby entry
  }
}
