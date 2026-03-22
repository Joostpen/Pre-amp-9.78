/*
 * VolumeControl.h - Volume and Balance Control
 */

#ifndef VOLUME_CONTROL_H
#define VOLUME_CONTROL_H

#include <Arduino.h>
#include "Config.h"

// State access (defined in main .ino)
extern uint8_t currentVolume;
extern int8_t balanceOffset;
extern bool isMuted;

// Volume functions
void setVolume(uint8_t left, uint8_t right);
void applyVolume();
void adjustVolume(int delta);

// Balance functions
void adjustBalanceLeft(bool isRepeat = false);
void adjustBalanceRight(bool isRepeat = false);
extern bool     balCenterDetentActive;
extern uint32_t balDetentLockoutMs;

#endif
