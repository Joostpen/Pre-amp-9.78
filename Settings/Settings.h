/*
 * Settings.h - PHI Reference Pre-10
 *
 * Persistente opslag van alle gebruikersinstellingen via mbed KVStore.
 * KVStore is ingebouwd in het Arduino Giga board package — geen extra
 * library nodig.
 *
 * Bewaart:
 *   - Ingangsnamen (5 x max 15 tekens)
 *   - Gain instellingen (LF gain, LF OUT, MF OUT, surround ingang)
 *   - Volume en balans
 *   - Actieve ingang
 *
 * Gebruik:
 *   loadSettings()  — aanroepen in setup(), voor initDisplay()
 *   saveSettings()  — aanroepen na menu-instellingen; runtime/session state blijft RAM-only
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include "Config.h"
#include "IR_Remote.h"

// SETTINGS_KEY defined in Config.h — bump version there on every struct change
#ifndef INPUT_COUNT
#define INPUT_COUNT  5
#endif

// ── Instellingen struct ───────────────────────────────────────────────────────
// Alles in een struct. CRC16 aan het einde detecteert ontbrekende of corrupte data.
struct Settings {
  uint8_t  structVersion;               // Struct versie — verhoog bij elke layout-wijziging
  char     inputNames[INPUT_COUNT][16];  // ingangsnamen, max 15 tekens + null
  int8_t   gainLF;                       // LF gain in 0.5 dB stappen (-7..+24 = -3.5..+12.0 dB)
  int8_t   gainLFOUT;                    // LF OUT gain
  int8_t   gainMFOUT;                    // MF OUT gain
  uint8_t  surroundInput;                // Surround pass ingang (0..INPUT_COUNT-1)
  uint8_t  currentVolume;                // Volume (0x00..0xFF)
  uint8_t  startupVolume[INPUT_COUNT];   // Startup volume per ingang (0x00..0xFF)
  uint8_t  maxVolume;                    // Max volume beveiliging (0x00..0xFF, default 0.0 dB = 231)
  int8_t   balanceOffset;                // Balans (-(BALANCE_MAX+1)..+(BALANCE_MAX+1), +1 stap = kanaal-mute)
  uint8_t  currentInput;                 // Actieve ingang (0..INPUT_COUNT-1)
  uint8_t  savedVolume[INPUT_COUNT];      // Volume per ingang
  uint16_t dimDelaySec;                  // Auto-dim vertraging in seconden
  uint8_t  dimPercent;                   // Dim percentage (0-100)
  uint8_t  deepDimDelayMin;             // Deep-dim vertraging in minuten na dim (1-60)
  uint8_t  uiBrightnessPct;              // Algemene display helderheid (20-100)
  uint8_t  volUnitsMode;                 // 0=steps, 1=dB, 2=0-100
  uint8_t  volumeCurve;                  // 0=linear, 1=adaptive (zone), 2=velocity
  uint8_t  detailMode;                   // Detail panel modus: 0=Uit 1=Aan
  bool     detailVisibleOnDim;           // Detail panel zichtbaar houden bij auto-dim
  bool     detailColorFollow;            // Detail panel volgt accentkleur
  uint8_t  mainFontMode;                  // 0=standard 1=matrix 2=orbitron
  bool     muteOnStartup;                // Mute actief bij opstart en uit standby komen
  uint16_t accentColor;                  // Accent kleur voor input/volume
  uint8_t  mainAccentPct;                // Accent governance sterkte op hoofdscherm (70-100)
  bool     warmStandbyEnabled;           // true = warm standby (power relay blijft aan)
  bool     warmTrigRelayClosed;          // true = remote trigger blijft actief in warm standby
  bool     bypassLF;                     // true = transformer bypass LF actief (GPA0)
  bool     bypassMF;                     // true = transformer bypass MF actief (GPA1)
  bool     remoteTriggerEnabled;         // true = remote trigger actief na warmup delay
  bool     largeFontOnDim;               // true = groot font als detailpanel verborgen is
  uint8_t  encSensitivity;               // Encoder gevoeligheid: 0=low 1=neutral 2=high
  uint8_t  reservedFlags2;               // Toekomstige booleans (bit 0..7)
  int8_t   inputOffset[INPUT_COUNT];     // Volume-offset per ingang in 0.5 dB stappen (-12..+12 = ±6 dB)
  uint8_t  irProtocolMap[IR_ACTION_COUNT];
  uint16_t irAddressMap[IR_ACTION_COUNT];
  uint8_t  irCommandMap[IR_ACTION_COUNT];
  uint16_t crc;                          // CRC16 verificatie (altijd laatste veld)
};

// ── Standaard waarden ─────────────────────────────────────────────────────────
static const Settings SETTINGS_DEFAULT = {
  29,     // structVersion — startupVolume uitgebreid naar array per ingang
  { "DAC", "Phono", "Tape", "Tuner", "Surround" },
    0,    // gainLF     =   0.0 dB
  -12,    // gainLFOUT  =  -6.0 dB
  -12,    // gainMFOUT  =  -6.0 dB
    4,    // surroundInput = RCA-2
  128,    // currentVolume = -51.5 dB
  {128, 128, 128, 128, 128},  // startupVolume per ingang = -51.5 dB
  231,    // maxVolume     = 0.0 dB
    0,    // balanceOffset = gecentreerd
    0,    // currentInput  = ingang 1 (DAC)
  {128, 128, 128, 128, 128},  // savedVolume per ingang
   30,    // dimDelaySec = 30 seconden
   50,    // dimPercent  = 50%
   10,    // deepDimDelayMin = 10 minuten
  100,    // uiBrightnessPct = 100%
    1,    // volUnitsMode    = dB weergave
    1,    // volumeCurve = 1 (adaptive)
    1,    // detailMode     = 1 (Aan)
 true,    // detailVisibleOnDim = panel zichtbaar bij dim
 false,   // detailColorFollow = standaard neutrale detailkleur
    0,    // mainFontMode = Standard
 true,    // muteOnStartup  = mute bij opstart en standby-resume
 0xF759u,  // accentColor = ivory (default)
  92,      // mainAccentPct = gebalanceerde accent governance
 false,    // warmStandbyEnabled = warm standby uit (default)
 false,    // warmTrigRelayClosed = trigger uit in warm standby
 false,    // bypassLF = transformer bypass LF uit (default)
 false,    // bypassMF = transformer bypass MF uit (default)
  true,    // remoteTriggerEnabled = trigger actief (default)
  true,    // largeFontOnDim = groot font zonder detailpanel (default)
  1,       // encSensitivity = neutral (default)
  0x00,    // reservedFlags2
  {0, 0, 0, 0, 0},  // inputOffset per ingang = 0 dB
  {IR_PROTOCOL_ANY, IR_PROTOCOL_ANY, IR_PROTOCOL_ANY, IR_PROTOCOL_ANY,
   IR_PROTOCOL_ANY, IR_PROTOCOL_ANY, IR_PROTOCOL_ANY, IR_PROTOCOL_ANY,
   IR_PROTOCOL_UNKNOWN, IR_PROTOCOL_UNKNOWN, IR_PROTOCOL_UNKNOWN,
   IR_PROTOCOL_UNKNOWN, IR_PROTOCOL_UNKNOWN},
  {IR_ADDRESS, IR_ADDRESS, IR_ADDRESS, IR_ADDRESS,
   IR_ADDRESS, IR_ADDRESS, IR_ADDRESS, IR_ADDRESS,
   0, 0, 0, 0, 0},
  {IR_CMD_VOL_UP, IR_CMD_VOL_DOWN, IR_CMD_INPUT_UP, IR_CMD_INPUT_DOWN,
   IR_CMD_BAL_LEFT, IR_CMD_BAL_RIGHT, IR_CMD_MUTE, IR_CMD_STANDBY,
   0, 0, 0, 0, 0},
    0     // crc wordt berekend bij opslaan
};

// ── Runtime variabelen (elders gedefinieerd) ────────────────────────────────
// Gain: zie GainControl.h
// Volume/input: zie VolumeControl.h / InputControl.h
extern uint8_t  savedVolume[INPUT_COUNT];
extern int8_t   inputOffset[INPUT_COUNT]; // Volume-offset per ingang (0.5 dB/stap, -12..+12)
extern uint16_t dimDelaySec;
extern uint8_t  dimPercent;
extern uint8_t  deepDimDelayMin;
extern uint8_t  uiBrightnessPct;
extern uint8_t  volUnitsMode;
#define VOL_CURVE_LINEAR    0
#define VOL_CURVE_ADAPTIVE   1
#define VOL_CURVE_VELOCITY   2
extern uint8_t  volumeCurve;
extern uint8_t  detailMode;
extern bool     detailVisibleOnDim;
extern bool     detailColorFollow;
extern uint8_t  mainFontMode;
extern bool     muteOnStartup;
extern uint16_t accentColor;
extern uint8_t  mainAccentPct;
extern bool     warmStandbyEnabled;
extern bool     warmTrigRelayClosed;
extern bool     remoteTriggerEnabled;
extern bool     largeFontOnDim;
extern uint8_t  encSensitivity;
extern uint8_t  maxVolume;

// ── Publieke functies ─────────────────────────────────────────────────────────
void loadSettings();
void saveSettings();
void flushSettings();   // Direct naar flash schrijven — gebruik voor expliciete/confirmed save-acties
void persistCurrentInputForStandby(); // Sla alleen de actieve ingang op bij standby-ingang
void tickSettings();   // Aanroepen vanuit loop()

#endif
