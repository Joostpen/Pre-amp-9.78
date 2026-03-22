/*
 * MCP23017.h - I2C I/O Expander Interface
 * PHI Reference Pre-10
 *
 * Alle schrijfoperaties hebben retry-logica.
 * Bij aanhoudende fouten: foutmelding op Serial.
 */
#ifndef MCP23017_H
#define MCP23017_H

#include <Arduino.h>
#include "Config.h"

// Maximaal aantal retries bij I2C schrijffout
#define I2C_MAX_RETRIES  3
#define I2C_RETRY_DELAY  5  // ms tussen retries

// I2C bus aan/uit — false = alle schrijfoperaties worden genegeerd (scherm gedimed)
// I2C is volledig event-driven — geen periodieke health check.

// Schrijf register met retry — geeft true bij succes
bool writeRegister(uint8_t addr, uint8_t reg, uint8_t val);

// I2C scan (voor setup diagnostics)
bool scanI2C(uint8_t addr, const char* label);

// Herinitialiseer MCP23017 chips na spanningsonderbreking (standby-exit)
// Configureert IODIRA/IODIRB als output en retourneert true als beide chips gevonden zijn
bool reinitMCP();

// Registreer of een I2C-device aanwezig is (gebruik tijdens setup na scan)
void setDeviceAvailable(uint8_t addr, bool available);

// Runtime I2C diagnostiek per chip
uint16_t getI2CErrorCount(uint8_t addr);   // Aantal fouten sinds boot
uint32_t getI2CLastErrorMs(uint8_t addr);  // millis() van laatste fout, 0 = nooit

// Recente I2C events (ringbuffer voor diagnostics-scherm)
struct I2CRecentEvent {
  uint8_t  addr;
  uint8_t  reg;   // 0xFF = scan/probe zonder register
  uint8_t  err;
  uint32_t ms;
};
uint8_t  getI2CRecentEventCount();
bool     getI2CRecentEvent(uint8_t newestIndex, I2CRecentEvent* out); // 0 = nieuwste
uint16_t getI2CErrorsLastHour(uint8_t addr);

// Periodieke runtime I2C health check — aanroepen vanuit loop()
// Detecteert vermiste chips en herinitialiseer bij herstel.


#endif
