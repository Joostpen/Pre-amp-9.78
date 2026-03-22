/*
 * Display.h - PHI Reference Pre-10
 * Arduino Giga R1 WiFi + Giga Display Shield
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// ── Scherm afmetingen ─────────────────────────────────────────────────────────
// Fallback als Config.h niet bereikbaar is vanuit library map
#ifndef SCREEN_W
#define SCREEN_W  800
#define SCREEN_H  480
#endif

// ── Timing constanten ─────────────────────────────────────────────────────────
#ifndef BOOT_DURATION_MS
#define BOOT_DURATION_MS  10000
#endif

// ── Display layout Y-posities ─────────────────────────────────────────────────
#ifndef VOL_TEXT_Y
#define VOL_TEXT_Y     (SCREEN_H/2 + 80)
#define STBY_TEXT_Y    (SCREEN_H/2 + 50)
#define MUTE_TEXT_Y    (SCREEN_H/2 + 70)
#define SWITCH_TEXT_Y  (SCREEN_H/2 + 40)
#endif


void initDisplay();
void startWarmup();           // Boot screen + 10s mute, dan hoofdscherm
void beginVolRamp(uint8_t target);  // Start volume ramp-up na standby-exit
void cancelVolRamp();               // Onderbreek ramp bij gebruikersinteractie
void updateDisplay();         // Elke loop() aanroepen
void updateVolumeDisplay();   // Volume of mute gewijzigd
void triggerVolBlink();       // Max volume bereikt of overschreden — 3× knipperen
void updateInputDisplay();
void updateAfterInputSwitch();
void showSwitchingScreen(const char* newName);    // Actieve ingang gewijzigd
void drawMainScreen();        // Volledig hoofdscherm hertekenen
void fadeTransition();         // Zachte overgang bij mute/standby
void setSwitchingInput(bool s); // Blokkeert timer-updates tijdens input-wissel
void showBalanceScreen();     // Balance remote knop ingedrukt
void showBalanceBar();        // Balance gewijzigd — toon overlay
bool isBalanceBarShowing();    // True als balance overlay momenteel zichtbaar is
void showMainMenu();          // Lange druk mute knop
void restoreMainDisplay();    // Terugkeren naar hoofdscherm
uint16_t dimC(uint16_t c);    // Helderheidsschaling
void notifyActivity();   // Reset dim-timer (encoder, volume, knoppen)
void notifyActivityNoWake(); // Reset dim-timer zonder backlight/full redraw
void notifyActivityKeepDimTimer(); // Calm wake: scherm aan, dim-timer ongewijzigd
void notifyTouch();      // Reset dim-timer + toon touchbar (alleen bij echte touch)
void drawDimSettingsScreen();
void drawSystemScreen();  // Scherm instellingen
void showIRLearnScreen();      // IR code leer-submenu
bool isIRLearnScreenActive();  // True als IR leer-scherm actief is
void onIRLearnSample(uint8_t protocol, uint16_t address, uint8_t command, uint32_t rawData, bool isRepeat);
void adjustGainSelection(int delta);  // Wrapper → adjustVGSelection (backward compat)
void adjustVGSelection(int delta);    // Encoder R → past geselecteerde parameter aan
void adjustDSSelection(int delta);    // Encoder R → past geselecteerde display slider aan
bool isGainScreenActive();            // True als Volume & Gain scherm open + item geselecteerd
bool isDimScreenActive();             // True als Display scherm open + slider geselecteerd
bool isMainScreen();                  // True als hoofdscherm actief
// drawFreqAnimScreen verwijderd — mute gebruikt hoofdscherm
void drawFaultScreen(const char* msg, uint8_t retryCountdown = 0);  // Hardware fault overlay

#endif
