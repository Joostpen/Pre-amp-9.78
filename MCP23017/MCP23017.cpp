/*
 * MCP23017.cpp - I2C I/O Expander met foutafhandeling
 * PHI Reference Pre-10
 */
#include "MCP23017.h"
#include <Wire.h>

static bool controlChipAvailable = true;
static bool volumeChipAvailable  = true;
static uint16_t controlErrCount  = 0;
static uint16_t volumeErrCount   = 0;
static uint32_t controlLastErrMs = 0;
static uint32_t volumeLastErrMs  = 0;

#define I2C_EVT_CAP 16
static I2CRecentEvent i2cEvents[I2C_EVT_CAP];
static uint8_t i2cEvtHead = 0;
static uint8_t i2cEvtCount = 0;

static void pushI2CEvent(uint8_t addr, uint8_t reg, uint8_t err) {
  i2cEvents[i2cEvtHead].addr = addr;
  i2cEvents[i2cEvtHead].reg  = reg;
  i2cEvents[i2cEvtHead].err  = err;
  i2cEvents[i2cEvtHead].ms   = millis();
  i2cEvtHead = (uint8_t)((i2cEvtHead + 1) % I2C_EVT_CAP);
  if (i2cEvtCount < I2C_EVT_CAP) i2cEvtCount++;
}

static void markI2CError(uint8_t addr, uint8_t reg, uint8_t err) {
  if (addr == I2C_ADDR_CONTROL) {
    if (controlErrCount < 0xFFFF) controlErrCount++;
    controlLastErrMs = millis();
    pushI2CEvent(addr, reg, err);
  } else if (addr == I2C_ADDR_VOLUME) {
    if (volumeErrCount < 0xFFFF) volumeErrCount++;
    volumeLastErrMs = millis();
    pushI2CEvent(addr, reg, err);
  }
}

static bool isDeviceAvailable(uint8_t addr) {
  if (addr == I2C_ADDR_CONTROL) return controlChipAvailable;
  if (addr == I2C_ADDR_VOLUME)  return volumeChipAvailable;
  return true;  // onbekend device: niet blokkeren
}

void setDeviceAvailable(uint8_t addr, bool available) {
  if (addr == I2C_ADDR_CONTROL) controlChipAvailable = available;
  if (addr == I2C_ADDR_VOLUME)  volumeChipAvailable  = available;
}

bool writeRegister(uint8_t addr, uint8_t reg, uint8_t val) {
  if (!isDeviceAvailable(addr)) return false;
  for (uint8_t attempt = 0; attempt < I2C_MAX_RETRIES; attempt++) {
    Wire1.beginTransmission(addr);
    Wire1.write(reg);
    Wire1.write(val);
    uint8_t err = Wire1.endTransmission();
    if (err == 0) return true;  // Succes
    markI2CError(addr, reg, err);

    // Fout — log en wacht voor retry
    if (attempt < I2C_MAX_RETRIES - 1) {
      Serial.print("[I2C] Schrijffout addr=0x");
      Serial.print(addr, HEX);
      Serial.print(" reg=0x");
      Serial.print(reg, HEX);
      Serial.print(" err=");
      Serial.print(err);
      Serial.print(" poging ");
      Serial.println(attempt + 1);
      delay(I2C_RETRY_DELAY);
    }
  }
  // Alle retries mislukt
  Serial.print("[I2C] FOUT na ");
  Serial.print(I2C_MAX_RETRIES);
  Serial.print(" pogingen addr=0x");
  Serial.println(addr, HEX);
  return false;
}

bool scanI2C(uint8_t addr, const char* label) {
  Wire1.beginTransmission(addr);
  uint8_t err = Wire1.endTransmission();
  bool found = (err == 0);
  if (!found) markI2CError(addr, 0xFF, err);
  setDeviceAvailable(addr, found);
  Serial.print("[I2C] ");
  Serial.print(label);
  Serial.print(" (0x");
  Serial.print(addr, HEX);
  Serial.print("): ");
  Serial.println(found ? "OK" : "NIET GEVONDEN");
  return found;
}

uint16_t getI2CErrorCount(uint8_t addr) {
  if (addr == I2C_ADDR_CONTROL) return controlErrCount;
  if (addr == I2C_ADDR_VOLUME)  return volumeErrCount;
  return 0;
}

uint32_t getI2CLastErrorMs(uint8_t addr) {
  if (addr == I2C_ADDR_CONTROL) return controlLastErrMs;
  if (addr == I2C_ADDR_VOLUME)  return volumeLastErrMs;
  return 0;
}


uint8_t getI2CRecentEventCount() {
  return i2cEvtCount;
}

bool getI2CRecentEvent(uint8_t newestIndex, I2CRecentEvent* out) {
  if (!out || newestIndex >= i2cEvtCount) return false;
  int idx = (int)i2cEvtHead - 1 - (int)newestIndex;
  while (idx < 0) idx += I2C_EVT_CAP;
  *out = i2cEvents[idx];
  return true;
}

uint16_t getI2CErrorsLastHour(uint8_t addr) {
  uint32_t now = millis();
  uint16_t n = 0;
  for (uint8_t i = 0; i < i2cEvtCount; i++) {
    I2CRecentEvent e;
    if (!getI2CRecentEvent(i, &e)) continue;
    if (e.addr != addr) continue;
    if ((now - e.ms) <= 3600000UL) n++;
  }
  return n;
}

extern uint8_t controlPortB;  // Shadow register — beheerd door Controls.cpp

bool reinitMCP() {
  // Scan beide chips — setDeviceAvailable wordt intern aangeroepen door scanI2C
  bool foundVolume  = scanI2C(I2C_ADDR_VOLUME,  "Volume");
  bool foundControl = scanI2C(I2C_ADDR_CONTROL, "Control");

  // Configureer als outputs (MCP reset-default is input)
  if (foundVolume) {
    writeRegister(I2C_ADDR_VOLUME, IODIRA, 0x00);
    writeRegister(I2C_ADDR_VOLUME, IODIRB, 0x00);
  }
  if (foundControl) {
    writeRegister(I2C_ADDR_CONTROL, IODIRA, 0x00);
    writeRegister(I2C_ADDR_CONTROL, IODIRB, 0x00);
    // Shadow resetten — chip is in reset geweest, alle GPB bits zijn 0
    // applyRelayState() + applyGainAll() bouwen de correcte waarde daarna op
    controlPortB = 0x00;
  }

  Serial.println(foundVolume && foundControl
    ? "[MCP] Herinitialisatie geslaagd"
    : "[MCP] Herinitialisatie: een of meer chips niet gevonden");

  return foundVolume && foundControl;
}

// ── Runtime I2C health check ──────────────────────────────────────────────────
// Interval: elke 30 seconden controleren of beide chips bereikbaar zijn.
// Bij storing: markeer als unavailable zodat writes direct stoppen.
// Bij herstel: reinitMCP() + herstel audio-staat via extern callbacks.
// tickI2CHealth verwijderd — I2C is volledig event-driven.
// Writes vinden alleen plaats bij encoder/remote/touch acties.
