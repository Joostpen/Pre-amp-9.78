/*
 * VolumeControl.cpp - Volume and Balance Control
 */

#include "VolumeControl.h"
#include "MCP23017.h"
#include "Display.h"
#include "Settings.h"

// Extern globals (gedefinieerd in Pre-Amplifier.ino)
extern uint8_t  currentVolume;
extern uint8_t  currentInput;
extern bool     isMuted;
extern bool     inStandby;
uint8_t  volumeCurve = VOL_CURVE_ADAPTIVE;  // defined in Settings.h via Config.h
extern int8_t   balanceOffset;

// Extern gains (gedefinieerd in Display.cpp)
extern uint8_t  surroundInput;
extern uint8_t  maxVolume;  // Max volume beveiliging (Settings.cpp)

// inputOffset: volume-offset per ingang in 0.5 dB stappen (-12..+12)
// Gedefinieerd in Settings.cpp, extern gedeclareerd via Settings.h
extern int8_t inputOffset[INPUT_COUNT];

uint8_t effectiveMaxForInput(uint8_t input) {
  if (input >= INPUT_COUNT) return VOL_MIN;
  return (uint8_t)constrain((int)maxVolume - (int)inputOffset[input], VOL_MIN, VOL_MAX);
}

void setVolume(uint8_t left, uint8_t right) {
  // Hardware kanaaltoewijzing per schema (Excel pin layout):
  // GPB (OLATB, reg 0x15) = Links (L), GPA (OLATA, reg 0x14) = Rechts (R)
  // Schrijf naar OLAT registers (niet GPIO) — dit garandeert dat de output
  // latch direct gezet wordt zonder afhankelijkheid van de GPIO read-back staat.
  writeRegister(I2C_ADDR_VOLUME, OLATB, left);
  writeRegister(I2C_ADDR_VOLUME, OLATA, right);
}

void applyVolume() {
  // VOL_OFF = bewuste uitschakelstand — hardware volledig stil
  if (currentVolume == VOL_OFF) {
    setVolume(0x00, 0x00);
    return;
  }

  // Pas input-offset toe (offset in 0.5 dB stappen = 1 hardware-stap)
  int base = (int)currentVolume + (int)inputOffset[currentInput];
  base = constrain(base, VOL_MIN, (int)effectiveMaxForInput(currentInput));

  int leftVol  = base;
  int rightVol = base;

  // BALANCE_MAX+1 = mute-eindstop: één kanaal volledig op 0
  const int8_t BAL_MUTE = BALANCE_MAX + 1;
  if (balanceOffset <= -BAL_MUTE) {
    rightVol = VOL_MIN;                                              // rechts gedempt
  } else if (balanceOffset < 0) {
    rightVol = constrain(base + balanceOffset, VOL_MIN, VOL_MAX);
  } else if (balanceOffset >= BAL_MUTE) {
    leftVol  = VOL_MIN;                                              // links gedempt
  } else if (balanceOffset > 0) {
    leftVol  = constrain(base - balanceOffset, VOL_MIN, VOL_MAX);
  }

  setVolume(leftVol, rightVol);
}

static int adaptiveVolumeDelta(int delta) {
  int mag = abs(delta);
  if (mag == 0) return 0;

  int stepMul = 1;

  if (volumeCurve == VOL_CURVE_ADAPTIVE) {
    // Zone-gebaseerde stapgrootte op basis van volumepositie
    // Fijn onderin, grover bovenin
    if      (currentVolume >= 192) stepMul = 3;  // boven -20dB: 1.5 dB/klik
    else if (currentVolume >= 128) stepMul = 2;  // midden:      1.0 dB/klik
    // onder 128: 0.5 dB/klik

  } else if (volumeCurve == VOL_CURVE_VELOCITY) {
    // Velocity-based: stapgrootte op basis van draaisnelheid
    // Zones: >200ms=2, 100-200ms=2, 50-100ms=4, <50ms=8
    static uint32_t lastVelMs = 0;
    uint32_t now = millis();
    uint32_t dt  = (lastVelMs > 0) ? (now - lastVelMs) : 500;
    lastVelMs = now;

    if      (dt > 400) stepMul = 1;   // langzaam:    0.5 dB
    else if (dt > 200) stepMul = 2;   // rustig:      1.0 dB
    else if (dt > 100) stepMul = 3;   // snel:        1.5 dB
    else               stepMul = 6;   // heel snel:   3.0 dB
  }
  // VOL_CURVE_LINEAR: stepMul blijft 1

  int signedStep = mag * stepMul;
  return (delta < 0) ? -signedStep : signedStep;
}

void adjustVolume(int delta) {
  if (currentInput == surroundInput) return;  // Surround: volume geblokkeerd

  static uint32_t lastVolStepMs = 0;
  uint32_t now = millis();
  if (lastVolStepMs > 0 && (now - lastVolStepMs) < 25) return;

  int effDelta = adaptiveVolumeDelta(delta);

  // VOL_OFF (0x00) is één stap voorbij VOL_MIN — bewuste uitschakelstand
  // Naar beneden: VOL_MIN → VOL_OFF met één stap (niet meeschalen met effDelta)
  // Omhoog vanuit VOL_OFF: altijd één stap naar VOL_MIN
  // Effectieve bovengrens voor currentVolume, gecorrigeerd voor input-offset.
  // inputOffset stappen zijn gelijk aan volume-stappen (beide 0.5 dB).
  // Een +4 offset betekent dat currentVolume maximaal (maxVolume - 4) mag zijn
  // zodat de hardware nooit boven maxVolume uitkomt.
  int effectiveMax = (int)effectiveMaxForInput(currentInput);

  int newVol;
  if (currentVolume == VOL_OFF) {
    if (delta > 0) newVol = VOL_MIN;
    else           return;
  } else if (currentVolume == VOL_MIN && delta < 0) {
    newVol = VOL_OFF;
  } else {
    newVol = constrain((int)currentVolume + effDelta, VOL_MIN, effectiveMax);
  }

  if (newVol != currentVolume) {
    cancelVolRamp();
    currentVolume = (uint8_t)newVol;
    applyVolume();
    notifyActivity();
    updateVolumeDisplay();
    lastVolStepMs = now;
    if (delta > 0 && (int)currentVolume >= effectiveMax) {
      triggerVolBlink();
    }
  } else if (delta > 0 && (int)currentVolume >= effectiveMax) {
    triggerVolBlink();
  }
}

// Center detent: na een richtingswisseling naar 0 wordt een tijdvenster
// ingesteld waarbinnen geen nieuwe stap geaccepteerd wordt — ook geen
// niet-repeat frames. Dit vangt de naijlende frames op die sommige remotes
// sturen na een druk (repeat frame gevolgd door nog een niet-repeat frame).
#define BAL_DETENT_LOCKOUT_MS  350

bool     balCenterDetentActive = false;
uint32_t balDetentLockoutMs    = 0;   // millis() waarop detent geactiveerd werd

void adjustBalanceLeft(bool isRepeat) {
  uint32_t now = millis();

  // Blokkeer naijlers binnen lockout-venster na center-snap
  if (balCenterDetentActive && (now - balDetentLockoutMs) < BAL_DETENT_LOCKOUT_MS) return;
  balCenterDetentActive = false;

  // Eerste druk toont alleen de bar; vanaf tweede druk pas waarde aanpassen.
  if (!isRepeat && !isBalanceBarShowing()) {
    showBalanceBar();
    notifyActivity();

    return;
  }

  if (balanceOffset <= -(BALANCE_MAX + 1)) return;  // al op mute-eindstop

  if (balanceOffset == 1) {
    // Eén stap van center komend van rechts: snap naar center
    balanceOffset = 0;
    balCenterDetentActive = true;
    balDetentLockoutMs    = now;
  } else if (balanceOffset > -BALANCE_MAX) {
    balanceOffset--;
  } else {
    // Op -BALANCE_MAX: extra stap zet rechts kanaal op mute
    balanceOffset = -(BALANCE_MAX + 1);
  }
  applyVolume();
  notifyActivity();

  showBalanceBar();  // tekent balk en detail panel
}

void adjustBalanceRight(bool isRepeat) {
  uint32_t now = millis();

  if (balCenterDetentActive && (now - balDetentLockoutMs) < BAL_DETENT_LOCKOUT_MS) return;
  balCenterDetentActive = false;

  // Eerste druk toont alleen de bar; vanaf tweede druk pas waarde aanpassen.
  if (!isRepeat && !isBalanceBarShowing()) {
    showBalanceBar();
    notifyActivity();

    return;
  }

  if (balanceOffset >= (BALANCE_MAX + 1)) return;  // al op mute-eindstop

  if (balanceOffset == -1) {
    // Eén stap van center komend van links: snap naar center
    balanceOffset = 0;
    balCenterDetentActive = true;
    balDetentLockoutMs    = now;
  } else if (balanceOffset < BALANCE_MAX) {
    balanceOffset++;
  } else {
    // Op +BALANCE_MAX: extra stap zet links kanaal op mute
    balanceOffset = BALANCE_MAX + 1;
  }
  applyVolume();
  notifyActivity();

  showBalanceBar();  // tekent balk en detail panel
}

