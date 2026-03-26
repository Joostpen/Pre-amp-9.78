/*
 * Settings.cpp - PHI Reference Pre-10
 *
 * Persistentie via mbed KVStore.
 * saveSettings() markeert dirty — tickSettings() schrijft pas na 2s inactiviteit.
 */

#include "Settings.h"
#include "GainControl.h"
#include "mbed.h"
#include "KVStore.h"
#include "kvstore_global_api.h"

// Externe globals
extern uint8_t  currentVolume;
extern uint8_t  currentInput;
extern int8_t   balanceOffset;
extern char     inputNames[INPUT_COUNT][16];
extern uint8_t  irProtocolMap[IR_ACTION_COUNT];
extern uint16_t irAddressMap[IR_ACTION_COUNT];
extern uint8_t  irCommandMap[IR_ACTION_COUNT];

// ── inputOffset: volume-offset per ingang ────────────────────────────────────
// Gedefinieerd hier; extern gedeclareerd in Settings.h
int8_t inputOffset[INPUT_COUNT] = { 0, 0, 0, 0, 0 };
uint8_t maxVolume = SETTINGS_DEFAULT.maxVolume;
bool remoteTriggerEnabled = SETTINGS_DEFAULT.remoteTriggerEnabled;
bool largeFontOnDim       = SETTINGS_DEFAULT.largeFontOnDim;
uint8_t encSensitivity    = SETTINGS_DEFAULT.encSensitivity;
uint16_t autoStandbyDelayMin = SETTINGS_DEFAULT.autoStandbyDelayMin;


// ── Debounce state ────────────────────────────────────────────────────────────
#define SETTINGS_DEBOUNCE_MS  2000

static bool     settingsDirty   = false;
static uint32_t settingsDirtyMs = 0;
struct StoredResumeState {
  uint8_t input;
  uint8_t volume;
  uint8_t savedVolume[INPUT_COUNT];
};

static StoredResumeState storedResumeState = {
  SETTINGS_DEFAULT.currentInput,
  SETTINGS_DEFAULT.currentVolume,
  {
    SETTINGS_DEFAULT.savedVolume[0], SETTINGS_DEFAULT.savedVolume[1], SETTINGS_DEFAULT.savedVolume[2],
    SETTINGS_DEFAULT.savedVolume[3], SETTINGS_DEFAULT.savedVolume[4]
  }
};

// ── CRC16-CCITT ───────────────────────────────────────────────────────────────
static uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
  }
  return crc;
}

static uint16_t calcCRC(const Settings& s) {
  return crc16((const uint8_t*)&s, sizeof(Settings) - sizeof(uint16_t));
}

static void copyStoredResumeStateToSettings(Settings& s) {
  s.currentInput  = storedResumeState.input;
  s.currentVolume = storedResumeState.volume;
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    s.savedVolume[i] = storedResumeState.savedVolume[i];
  }
}

static void loadStoredResumeStateFromSettings(const Settings& s) {
  storedResumeState.input  = constrain(s.currentInput, 0, INPUT_COUNT - 1);
  storedResumeState.volume = constrain((int)s.currentVolume, 0, (int)s.maxVolume);
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    storedResumeState.savedVolume[i] = constrain((int)s.savedVolume[i], 0, (int)s.maxVolume);
  }
}

// ── Globals ↔ struct ──────────────────────────────────────────────────────────
static void globalsToStruct(Settings& s) {
  s.structVersion = SETTINGS_DEFAULT.structVersion;
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    strncpy(s.inputNames[i], inputNames[i], 15);
    s.inputNames[i][15] = '\0';
  }
  s.gainLF             = gainLF;
  s.gainLFOUT          = gainLFOUT;
  s.gainMFOUT          = gainMFOUT;
  s.surroundInput      = surroundInput;
  copyStoredResumeStateToSettings(s);
  for (uint8_t i = 0; i < INPUT_COUNT; i++) s.startupVolume[i] = startupVolume[i];
  s.maxVolume          = maxVolume;
  s.balanceOffset      = balanceOffset;
  s.dimDelaySec        = dimDelaySec;
  s.dimPercent         = dimPercent;
  s.deepDimDelayMin    = deepDimDelayMin;
  s.autoStandbyDelayMin = autoStandbyDelayMin;
  s.uiBrightnessPct    = uiBrightnessPct;
  s.volUnitsMode       = volUnitsMode;
  s.volumeCurve = volumeCurve;
  s.detailMode         = detailMode;
  s.detailVisibleOnDim = detailVisibleOnDim;
  s.detailColorFollow  = detailColorFollow;
  s.mainFontMode  = mainFontMode;
  s.muteOnStartup      = muteOnStartup;
  s.accentColor        = accentColor;
  s.mainAccentPct      = mainAccentPct;
  s.warmStandbyEnabled  = warmStandbyEnabled;
  s.warmTrigRelayClosed = warmTrigRelayClosed;
  s.bypassLF            = bypassLF;
  s.bypassMF            = bypassMF;
  s.remoteTriggerEnabled = remoteTriggerEnabled;
  s.largeFontOnDim       = largeFontOnDim;
  s.encSensitivity       = encSensitivity;
  for (uint8_t i = 0; i < INPUT_COUNT; i++) s.inputOffset[i] = inputOffset[i];
  for (uint8_t i = 0; i < IR_ACTION_COUNT; i++) {
    s.irProtocolMap[i] = irProtocolMap[i];
    s.irAddressMap[i]  = irAddressMap[i];
    s.irCommandMap[i]  = irCommandMap[i];
  }
  s.crc = calcCRC(s);
}

static void structToGlobals(const Settings& s) {
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    strncpy(inputNames[i], s.inputNames[i], 15);
    inputNames[i][15] = '\0';
  }
  gainLF             = (int8_t)constrain((int)s.gainLF,     -7,  24);
  gainLFOUT          = (int8_t)constrain((int)s.gainLFOUT, -12,   0);
  gainMFOUT          = (int8_t)constrain((int)s.gainMFOUT, -12,   0);
  surroundInput      = constrain(s.surroundInput, 0, INPUT_COUNT - 1);
  for (uint8_t i = 0; i < INPUT_COUNT; i++)
    startupVolume[i] = constrain((int)s.startupVolume[i], 0, (int)s.maxVolume);
  maxVolume          = constrain(s.maxVolume, 0, 255);
  balanceOffset      = constrain((int)s.balanceOffset, -(BALANCE_MAX + 1), BALANCE_MAX + 1);
  loadStoredResumeStateFromSettings(s);
  currentInput       = storedResumeState.input;
  currentVolume      = storedResumeState.volume;
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    savedVolume[i] = storedResumeState.savedVolume[i];
  }
  dimDelaySec        = constrain(s.dimDelaySec,      10,  600);
  dimPercent         = constrain(s.dimPercent,         5,   95);
  deepDimDelayMin    = constrain(s.deepDimDelayMin,    1,   61);
  switch (s.autoStandbyDelayMin) {
    case 0:
    case 15:
    case 30:
    case 45:
    case 60:
    case 90:
    case 120:
      autoStandbyDelayMin = s.autoStandbyDelayMin;
      break;
    default:
      autoStandbyDelayMin = SETTINGS_DEFAULT.autoStandbyDelayMin;
      break;
  }
  uiBrightnessPct    = constrain(s.uiBrightnessPct,   20,  100);
  volUnitsMode       = constrain((int)s.volUnitsMode, 0, 2);
  volumeCurve        = constrain(s.volumeCurve, 0, 2);
  detailMode         = constrain(s.detailMode, 0, 1);
  detailVisibleOnDim = s.detailVisibleOnDim;
  detailColorFollow  = s.detailColorFollow;
  mainFontMode       = constrain(s.mainFontMode, 0, 2);  // 0=standard 1=matrix 2=orbitron
  muteOnStartup      = s.muteOnStartup;
  accentColor        = s.accentColor;
  mainAccentPct      = constrain(s.mainAccentPct, 70, 100);
  warmStandbyEnabled  = s.warmStandbyEnabled;
  warmTrigRelayClosed = s.warmTrigRelayClosed;
  bypassLF            = s.bypassLF;
  bypassMF            = s.bypassMF;
  remoteTriggerEnabled = s.remoteTriggerEnabled;
  largeFontOnDim       = s.largeFontOnDim;
  encSensitivity       = constrain(s.encSensitivity, 0, 2);
  for (uint8_t i = 0; i < INPUT_COUNT; i++)
    inputOffset[i] = constrain((int)s.inputOffset[i], -12, 12);
  for (uint8_t i = 0; i < IR_ACTION_COUNT; i++) {
    irProtocolMap[i] = s.irProtocolMap[i];
    irAddressMap[i]  = s.irAddressMap[i];
    irCommandMap[i]  = s.irCommandMap[i];
  }

  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    uint8_t effectiveMax = (uint8_t)constrain((int)maxVolume - (int)inputOffset[i], VOL_MIN, VOL_MAX);
    startupVolume[i] = constrain((int)startupVolume[i], 0, (int)effectiveMax);
    if (i != surroundInput) {
      savedVolume[i] = constrain((int)savedVolume[i], 0, (int)effectiveMax);
    }
    storedResumeState.savedVolume[i] = savedVolume[i];
  }

  if (currentInput != surroundInput) {
    uint8_t currentEffectiveMax = (uint8_t)constrain((int)maxVolume - (int)inputOffset[currentInput], VOL_MIN, VOL_MAX);
    currentVolume = constrain((int)currentVolume, 0, (int)currentEffectiveMax);
  }
  storedResumeState.input  = currentInput;
  storedResumeState.volume = currentVolume;
}

// ── loadSettings ──────────────────────────────────────────────────────────────
void loadSettings() {
  Settings s;
  size_t actual = 0;
  int res = kv_get(SETTINGS_KEY, &s, sizeof(Settings), &actual);

  bool ok_res     = (res == 0);
  bool ok_size    = (actual == sizeof(Settings));
  bool ok_crc     = ok_size && (calcCRC(s) == s.crc);
  bool ok_version = ok_size && (s.structVersion == SETTINGS_DEFAULT.structVersion);
  bool valid = ok_res && ok_size && ok_crc && ok_version;

  if (ok_size) {
  }

  if (valid) {
    structToGlobals(s);
  } else {
    structToGlobals(SETTINGS_DEFAULT);
    // Direct flushen zodat volgende boot wel kan laden
    Settings def;
    globalsToStruct(def);
    kv_set(SETTINGS_KEY, &def, sizeof(Settings), 0);
  }
}

// ── flushSettings: schrijft echt naar flash ───────────────────────────────────
void flushSettings() {
  Settings s;
  globalsToStruct(s);
  int res = kv_set(SETTINGS_KEY, &s, sizeof(Settings), 0);
  if (res == 0) {
    settingsDirty = false;
  } else {
  }
}

void persistCurrentInputForStandby() {
  bool inputChanged = (storedResumeState.input != currentInput);
  storedResumeState.input = currentInput;
  if (inputChanged || settingsDirty) {
    flushSettings();
  }
}

// ── saveSettings: markeert dirty, schrijft pas na debounce ───────────────────
void saveSettings() {
  settingsDirty   = true;
  settingsDirtyMs = millis();
}

// ── tickSettings: aanroepen vanuit loop() ────────────────────────────────────
void tickSettings() {
  if (settingsDirty && (millis() - settingsDirtyMs) >= SETTINGS_DEBOUNCE_MS) {
    flushSettings();
  }
}
