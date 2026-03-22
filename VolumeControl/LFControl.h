/*
 * LFControl.h - LF output volume and brightness control
 */
#ifndef LF_CONTROL_H
#define LF_CONTROL_H

#include <Arduino.h>

void initLFControl();
void applyLFVolume();       // Write lfVolume (0-31) to GPIOB bits 0-4
void setLFMinus6dB(bool);   // Toggle GPIOB bit 7
void setMFMinus6dB(bool);   // Toggle GPIOB bit 6
void applyBrightness();     // Software brightness redraw
uint8_t getLFGPIOBShadow();
void setLFGPIOBShadow(uint8_t);

#endif
