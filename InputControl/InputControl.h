/*
 * InputControl.h - Input Selection Control
 */

#ifndef INPUT_CONTROL_H
#define INPUT_CONTROL_H

#include <Arduino.h>

// Fallback als Config.h niet bereikbaar is vanuit library map
#ifndef INPUT_COUNT
#define INPUT_COUNT  5
#endif
#include "Config.h"

// State access (defined in main .ino)
extern uint8_t currentInput;

// Input names
extern char inputNames[INPUT_COUNT][16];

// Input functions
void setInput(uint8_t input);
void adjustInput(int delta);
void selectInput(uint8_t input);
void resetSessionInputVolumes();
void tickInputSwitch();      // Aanroepen vanuit hoofdloop — rondt wissel af na stilstand

// Input-switch status voor UI overlays
bool isInputSwitchPending();
uint8_t getInputSwitchProgressPct();  // 0..100, 100 = settle voltooid

// Geeft poortnaam terug voor ingang idx (bv "XLR-1", "RCA-2")
void getPortStr(uint8_t idx, char* buf);

#endif
