/*
 * Encoder.cpp - Rotary Encoder Handling
 * PHI Reference Pre-10
 *
 * Interrupt-driven op beide kanalen van elke encoder.
 * Rechter encoder (D24/D26) : volume
 * Linker encoder  (D28/D30) : gain (als gain scherm actief) of ingang
 *
 * Aanpak: Grey-code opzoektabel telt alleen geldige transities.
 * Geen tijdsdebounce in ISR — de opzoektabel filtert bounce al af
 * door alleen geldige Gray-code overgangen te tellen.
 * ENC_PPR=4: één mechanische klik = 4 flanken = 4 telpunten.
 */

#include "Encoder.h"
#include "Config.h"
#include "Controls.h"

// ENC_PPR_VOL / ENC_PPR_INP / ENC_PPR_GAIN — defined in Config.h

#include "VolumeControl.h"
#include "InputControl.h"
#include "Display.h"

extern bool isMuted;
extern uint8_t encSensitivity;  // 0=low 1=neutral 2=high

// Geeft de drempel in pulsen per stap op basis van gevoeligheidsinstelling
static inline int encPPR_Vol() {
  // low: 3 klikken per stap, neutral: standaard, high: elke klik
  return (encSensitivity == 0) ? 12 : (encSensitivity == 2) ? 2 : ENC_PPR_VOL;
}
static inline int encPPR_Inp() {
  // low: 6 klikken per ingang, neutral: standaard, high: 2 klikken per ingang
  return (encSensitivity == 0) ? 96 : (encSensitivity == 2) ? 12 : ENC_PPR_INP;
}
extern bool inStandby;
extern uint8_t screenBrightness;
extern bool mainCalmMode;
extern uint8_t uiBrightnessPct;
// activeBrightness() is static inline in Display.cpp — bereken hier direct
#define activeBrightness() ((uint8_t)((uint32_t)uiBrightnessPct * 255 / 100))
#define FULL_BRIGHTNESS 255

// Interrupt tellers — volatile want geschreven vanuit ISR
volatile int encRCount = 0;
volatile int encLCount = 0;

// Encoder toestand (2-bit Grey code)
static volatile uint8_t rState = 0;
static volatile uint8_t lState = 0;

// Accumulatie voor wake-detectie tijdens dim (voorkomt bounce-wake)
static int dimWakeAccR = 0;
static int dimWakeAccL = 0;

// Geldige Grey-code transitietabel
// Index = (prevState << 2) | newState
// +1 = rechtsom, -1 = linksom, 0 = ongeldig/bounce
static const int8_t ENC_TABLE[16] = {
   0, +1, -1,  0,
  -1,  0,  0, +1,
  +1,  0,  0, -1,
   0, -1, +1,  0
};

void isrEncoderR() {
  uint8_t newState = (digitalRead(ENC_R_A) << 1) | digitalRead(ENC_R_B);
  int8_t dir = ENC_TABLE[(rState << 2) | newState];
  if (dir != 0) encRCount += dir;
  rState = newState;
}

void isrEncoderL() {
  uint8_t newState = (digitalRead(ENC_L_A) << 1) | digitalRead(ENC_L_B);
  int8_t dir = ENC_TABLE[(lState << 2) | newState];
  if (dir != 0) encLCount += dir;
  lState = newState;
}

void initEncoders() {
  pinMode(ENC_R_A, INPUT_PULLUP);
  pinMode(ENC_R_B, INPUT_PULLUP);
  pinMode(ENC_L_A, INPUT_PULLUP);
  pinMode(ENC_L_B, INPUT_PULLUP);

  rState = (digitalRead(ENC_R_A) << 1) | digitalRead(ENC_R_B);
  lState = (digitalRead(ENC_L_A) << 1) | digitalRead(ENC_L_B);

  attachInterrupt(digitalPinToInterrupt(ENC_R_A), isrEncoderR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_R_B), isrEncoderR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_L_A), isrEncoderL, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_L_B), isrEncoderL, CHANGE);
}

void clearEncoderCounts() {
  noInterrupts();
  encRCount = 0;
  encLCount = 0;
  interrupts();
  dimWakeAccR = 0;
  dimWakeAccL = 0;
}

void handleEncoders() {

  // ── Scherm gedimed: encoder-draai wekt scherm en reset dim-timer ──
  if (screenBrightness < activeBrightness()) {
    int rVal, lVal;
    noInterrupts();
    rVal = encRCount; lVal = encLCount;
    encRCount = 0;    encLCount = 0;
    interrupts();
    dimWakeAccR += rVal;
    dimWakeAccL += lVal;
    if (abs(dimWakeAccR) >= encPPR_Vol() || abs(dimWakeAccL) >= encPPR_Inp()) {
      // Zet accumulatie terug als volwaardige tellers zodat de draaibeweging
      // die de wake triggerde direct verwerkt wordt als volume/input stap.
      noInterrupts();
      encRCount = dimWakeAccR;
      encLCount = dimWakeAccL;
      interrupts();
      dimWakeAccR = 0;
      dimWakeAccL = 0;
      notifyActivity();  // reset lastActivityMs + wake scherm
      // Niet returnen — val door naar normale verwerking hieronder
    } else {
      return;
    }
  }

  // ── Calm: encoder-draai wekt scherm maar dim-timer loopt door ──
  if (mainCalmMode) {
    int rVal, lVal;
    noInterrupts();
    rVal = encRCount; lVal = encLCount;
    encRCount = 0;    encLCount = 0;
    interrupts();
    dimWakeAccR += rVal;
    dimWakeAccL += lVal;
    if (abs(dimWakeAccR) >= encPPR_Vol() || abs(dimWakeAccL) >= encPPR_Inp()) {
      noInterrupts();
      encRCount = dimWakeAccR;
      encLCount = dimWakeAccL;
      interrupts();
      dimWakeAccR = 0;
      dimWakeAccL = 0;
      notifyActivityKeepDimTimer();  // wake scherm, lastActivityMs NIET resetten
      // Niet returnen — val door naar normale verwerking hieronder
    } else {
      return;
    }
  }

  // Niet gedimd: eventuele wake-accumulatie wissen
  dimWakeAccR = 0;
  dimWakeAccL = 0;


  // ── Rechter encoder: uitsluitend volume op het hoofdscherm ───────────────
  if (!inStandby) {
    noInterrupts();
    int rVal = encRCount;
    encRCount = 0;
    interrupts();

    if (isMainScreen()) {
      if (abs(rVal) >= encPPR_Vol()) {
        int steps  = rVal / encPPR_Vol();
        int rest   = rVal % encPPR_Vol();
        int mag    = abs(steps);
        int scaled = (mag <= 2) ? 1 : (mag <= 4) ? 2 : (mag <= 5) ? 3 : mag - 2;
        // Gevoeligheid: high = dubbele stapgrootte, low = halve (min 1)
        if (encSensitivity == 2) scaled = scaled * 2;
        else if (encSensitivity == 0) scaled = max(1, scaled / 2);
        int delta  = ((steps > 0) ? scaled : -scaled) * ENC_R_DIR;
        if (isMuted) {
          // Draaien tijdens mute: unmute en pas direct volume aan
          toggleMute();
        }
        adjustVolume(delta);
        // Restant bewaren — maar niet als scherm gedimmd is (voorkomt late bounce-wake)
        if (rest != 0 && screenBrightness >= activeBrightness()) {
          noInterrupts(); encRCount += rest; interrupts();
        }
      } else if (rVal != 0 && screenBrightness >= activeBrightness()) {
        // Sub-drempel restant alleen bewaren als scherm actief is
        noInterrupts(); encRCount += rVal; interrupts();
      }
    }
    // Op andere schermen: teller geleegd, niets doen
  } else {
    noInterrupts(); encRCount = 0; interrupts();
  }

  // ── Linker encoder: ingang (hoofdscherm) of slider (menu-schermen) ───────
  if (!inStandby) {
    noInterrupts();
    int lVal = encLCount;
    encLCount = 0;
    interrupts();

    if (isGainScreenActive()) {
      // VG-scherm: één stap per klik, rest bewaren
      if (abs(lVal) >= encPPR_Vol()) {
        int steps = lVal / encPPR_Vol();
        int rest  = lVal % encPPR_Vol();
        adjustVGSelection(((steps > 0) ? 1 : -1) * ENC_L_DIR);
        if (rest != 0) { noInterrupts(); encLCount += rest; interrupts(); }
      } else if (lVal != 0) {
        noInterrupts(); encLCount += lVal; interrupts();
      }
    } else if (isDimScreenActive()) {
      // Display scherm: één stap per klik
      if (abs(lVal) >= encPPR_Vol()) {
        int steps = lVal / encPPR_Vol();
        int rest  = lVal % encPPR_Vol();
        adjustDSSelection(((steps > 0) ? 1 : -1) * ENC_L_DIR);
        if (rest != 0) { noInterrupts(); encLCount += rest; interrupts(); }
      } else if (lVal != 0) {
        noInterrupts(); encLCount += lVal; interrupts();
      }
    } else if (isMainScreen()) {
      // Hoofdscherm: ingang-selectie als draaischakelaar
      // Restant alleen bewaren als scherm actief — voorkomt late bounce-wake
      if (abs(lVal) >= encPPR_Inp()) {
        adjustInput((lVal > 0 ? 1 : -1) * ENC_L_DIR);
        // Rest weggooien — vaste stapgrootte draaischakelaar
      } else if (lVal != 0 && screenBrightness >= activeBrightness()) {
        noInterrupts(); encLCount += lVal; interrupts();
      }
    }
    // Andere schermen (dim, system, IR, ...): teller geleegd, niets doen
  } else {
    noInterrupts(); encLCount = 0; interrupts();
  }
}
