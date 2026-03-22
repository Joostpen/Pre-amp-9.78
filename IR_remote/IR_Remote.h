/*
 * IR_Remote.h - Infrared Remote Control Configuration
 * PHI Reference Pre-10
 */

#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#include <Arduino.h>
// ── Remote identity defaults ────────────────────────────────────────────────
// Protocol constants for mapping (avoid direct IRremote dependency in headers)
#define IR_PROTOCOL_UNKNOWN   0x00
#define IR_PROTOCOL_ANY       0xFF

#define IR_ADDRESS          0x10

// ── Default command codes ───────────────────────────────────────────────────
#define IR_CMD_VOL_UP       0x10
#define IR_CMD_VOL_DOWN     0x11
#define IR_CMD_MUTE         0x0D
#define IR_CMD_STANDBY      0x0C
#define IR_CMD_INPUT_UP     0x20
#define IR_CMD_INPUT_DOWN   0x21
#define IR_CMD_BAL_LEFT     0x30
#define IR_CMD_BAL_RIGHT    0x31

// ── Learnable IR actions ────────────────────────────────────────────────────
enum IRActions : uint8_t {
  IR_ACT_VOL_UP = 0,
  IR_ACT_VOL_DOWN,
  IR_ACT_INPUT_UP,
  IR_ACT_INPUT_DOWN,
  IR_ACT_BAL_LEFT,
  IR_ACT_BAL_RIGHT,
  IR_ACT_MUTE,
  IR_ACT_STANDBY,
  IR_ACT_INPUT_1,   // Directe ingangsselectie 1..5
  IR_ACT_INPUT_2,
  IR_ACT_INPUT_3,
  IR_ACT_INPUT_4,
  IR_ACT_INPUT_5,
  IR_ACTION_COUNT
};

extern uint8_t  irProtocolMap[IR_ACTION_COUNT];
extern uint16_t irAddressMap[IR_ACTION_COUNT];
extern uint8_t  irCommandMap[IR_ACTION_COUNT];
void resetIRMappingsToDefaults();

// ── Repeat timing ───────────────────────────────────────────────────────────
#define IR_REPEAT_DELAY_MS   400
#define IR_VOL_REPEAT_MS      80
#define IR_BAL_REPEAT_MS     120

// ── Acceleration ────────────────────────────────────────────────────────────
// IR_VOL_ACCEL_MS / IR_VOL_ACCEL_STEP — defined in Config.h

#endif // IR_REMOTE_H
