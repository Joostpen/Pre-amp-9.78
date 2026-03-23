/*
 * GainControl.h - LF/MF gain en surround ingang beheer
 * PHI Reference Pre-10
 *
 * Bevat alle gain-gerelateerde state en hardware aanpassing.
 * Display.cpp roept alleen de publieke functies aan voor tekenen en interactie.
 */

#ifndef GAIN_CONTROL_H
#define GAIN_CONTROL_H

#include <Arduino.h>
#include "Config.h"

// Fallback als Config.h niet bereikbaar is vanuit library map
#ifndef INPUT_COUNT
#define INPUT_COUNT  5
#endif

// ── Gain state (extern beschikbaar voor Settings en Display) ──────────────────
extern int8_t  gainLF;        // LF gain in 0.5 dB stappen (-29..+24)
extern int8_t  gainLFOUT;     // LF OUT schakelaar: 0 = 0dB, -12 = -6dB
extern int8_t  gainMFOUT;     // MF OUT schakelaar: 0 = 0dB, -12 = -6dB
extern uint8_t surroundInput; // Surround pass ingang (0..INPUT_COUNT-1)
extern uint8_t startupVolume[INPUT_COUNT]; // Startup volume per ingang (0..255)
extern bool    bypassLF;      // Transformer bypass LF (GPA0)
extern bool    bypassMF;      // Transformer bypass MF (GPA1)

extern const int8_t GAIN_MIN[5];
extern const int8_t GAIN_MAX[5];

// ── Publieke functies ─────────────────────────────────────────────────────────
void applyGainLF();           // LF gain naar hardware
void applyGainLFOUT();        // LF OUT -6dB schakelaar naar hardware
void applyGainMFOUT();        // MF OUT -6dB schakelaar naar hardware
void applyGainAll();          // Alle drie tegelijk (bij init)
void beginGainRestore();      // Non-blocking herstel van LF/LF OUT/MF OUT in gefaseerde stappen
void tickGainRestore();       // Aanroepen vanuit loop() voor non-blocking herstel
bool isGainRestoreActive();   // True zolang non-blocking gain-herstel bezig is
void applyBypassLF();         // Transformer bypass LF naar hardware (GPA0)
void applyBypassMF();         // Transformer bypass MF naar hardware (GPA1)
void standbyRelaysOff();      // Alle audio-relais hardware uit bij standby

// Encoder aanpassing vanuit gain scherm
void adjustGain(int gainIndex, int delta);

// Hulpfunctie: formatteer waarde van gain-rij i als string
void formatGainVal(int i, char* buf);

#endif
