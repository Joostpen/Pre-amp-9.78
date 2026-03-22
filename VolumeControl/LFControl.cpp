/*
 * LFControl.cpp - LF output volume and display brightness
 *
 * LF volume: chip 0x20 (I2C_ADDR_CONTROL), GPIOB
 *   bits 0-4: 5-bit binary volume (0=−3.5dB, 31=+12dB)
 *   bit 5:    mute relay (keep unchanged)
 *   bit 6:    −6dB MF output switch
 *   bit 7:    −6dB LF output switch
 *
 * Brightness: no hardware PWM pin available on this shield,
 *   so we dim by scaling all draw colors in software.
 *   applyBrightness() triggers a full display redraw via restoreMainDisplay().
 */

#include "LFControl.h"
#include "MCP23017.h"
#include "Config.h"
#include "Display.h"

extern uint8_t lfVolume;          // 0-31, stored as 5-bit value
extern uint8_t screenBrightness;  // 0-255

// Shadow of GPIOB on chip 0x20 so we don't disturb other bits
static uint8_t gpiobShadow = 0x00;

void initLFControl() {
  gpiobShadow = MUTE_RELAY_BIT;
  applyLFVolume();
}

void applyLFVolume() {
  // Preserve bits 5,6,7 (mute relay, MF -6dB, LF -6dB)
  // Write 5-bit LF volume into bits 0-4
  uint8_t val = (gpiobShadow & 0xE0) | (lfVolume & 0x1F);
  gpiobShadow = val;
  writeRegister(I2C_ADDR_CONTROL, GPIOB, val);

}


void setLFMinus6dB(bool enable) {
  if (enable) gpiobShadow |=  0x80;   // bit 7
  else        gpiobShadow &= ~0x80;
  writeRegister(I2C_ADDR_CONTROL, GPIOB, gpiobShadow);
}

void setMFMinus6dB(bool enable) {
  if (enable) gpiobShadow |=  0x40;   // bit 6
  else        gpiobShadow &= ~0x40;
  writeRegister(I2C_ADDR_CONTROL, GPIOB, gpiobShadow);
}

void applyBrightness() {
  // dimC() in Display.cpp applies screenBrightness at every draw call.
  // No explicit redraw needed here — the settings screen calls drawSettingsScreen()
  // directly after changing screenBrightness for live preview.
}

// Expose shadow so Controls.cpp can update mute relay bit without clobbering LF
uint8_t getLFGPIOBShadow() {
  return gpiobShadow;
}

void setLFGPIOBShadow(uint8_t val) {
  gpiobShadow = val;
}
