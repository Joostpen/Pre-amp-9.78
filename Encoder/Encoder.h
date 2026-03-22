/*
 * Encoder.h - Rotary Encoder Handling
 */

#ifndef ENCODER_H
#define ENCODER_H

#include <Arduino.h>
#include "Config.h"

void initEncoders();
void handleEncoders();
void clearEncoderCounts();

#endif
