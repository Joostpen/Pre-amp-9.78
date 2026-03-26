/*
 * Config.h - Pre-Amplifier Configuration
 * All pin assignments, I2C addresses, and constants
 */

#ifndef CONFIG_H
#define CONFIG_H

// I2C Addresses (MCP23017)
#define I2C_ADDR_CONTROL 0x20  // Control chip (A0=0, A1=0, A2=0)
#define I2C_ADDR_VOLUME  0x21  // Volume chip (A0=1, A1=0, A2=0)

// MCP23017 Register addresses
#define IODIRA   0x00  // I/O Direction Register A (0=output, 1=input)
#define IODIRB   0x01  // I/O Direction Register B
#define GPIOA    0x12  // GPIO Port A
#define GPIOB    0x13  // GPIO Port B
#define OLATA    0x14  // Output Latch A — gebruik dit voor volume writes
#define OLATB    0x15  // Output Latch B — gebruik dit voor volume writes

// Volume range
#define VOL_OFF  0x00  // Volume volledig uit — één stap voorbij minimum
#define VOL_MIN  0x01  // Laagste normale volume (-115.0 dB)
#define VOL_MAX  0xFF  // +12.0dB

// Balance
#define BALANCE_MAX  24  // Max offset: 24 steps = 12dB per kanaal (0.5dB per stap)

// Right Rotary Encoder pins (Volume control)
#define ENC_R_A  24    // D24 - Encoder A signal
#define ENC_R_B  26    // D26 - Encoder B signal

// Left Rotary Encoder pins (Input selection)
#define ENC_L_A  30    // D30 - Encoder A signal (LA)
#define ENC_L_B  28    // D28 - Encoder B signal (LB)

// Encoder draairichting — zet op -1 om richting om te draaien, 1 = normaal
#define ENC_R_DIR  -1    // Rechter encoder (volume): -1 = rechtsom is omhoog
#define ENC_L_DIR  -1    // Linker encoder: -1 = richting omgedraaid

// Encoder sensitivity — single source of truth for all encoder + IR tuning
#define ENC_PPR_VOL    4   // Pulses per click: volume encoder
#define ENC_PPR_INP   32   // Pulses per click: input selector (rotary-switch feel)
#define ENC_PPR_GAIN   4   // Pulses per click: gain/offset sliders

// IR remote acceleration (held-down repeat)
#define IR_VOL_ACCEL_MS    1500  // Hold duration before acceleration kicks in
#define IR_VOL_ACCEL_STEP     3  // Step size after acceleration

// Standby control pins
#define PIN_BTN_STANDBY   53  // D53 - Push button R (input, active low)
#define PIN_LED_STANDBY   7   // D7  - LED R (output, on during standby)
#define PIN_STANDBY_OFF   46  // D46 - Standby off (output, HIGH = not in standby)
#define PIN_REMOTE_TRIG   48  // D48 - Remote trigger (output)

// MCP23017 Control chip (0x20) — GPIOB bit masks
#define MUTE_RELAY_BIT    0x20  // GPB5 on control chip (active low)

// MCP23017 Control chip (0x20) — GPIOA bit masks
// GPA7..GPA3: input selection (set in InputControl.cpp)
#define CTRL_GPA_STANDBY        0x04  // GPA2 - Standby (no use per spec)
#define CTRL_GPA_BYPASS_MF      0x02  // GPA1 - Transformer bypass MF (bypasses -6dB MF when active)
#define CTRL_GPA_BYPASS_LF      0x01  // GPA0 - Transformer bypass LF (bypasses -6dB LF when active)

// Mute control pins
#define PIN_BTN_MUTE      51  // D51 - Push button L (input, active low)
#define PIN_LED_MUTE      3   // D3  - LED L (output, on when muted)
// MUTE_RELAY_BIT defined above under GPIOB bit masks

// IR receiver
#define PIN_IR_RECV       52  // D52 - IR receiver

// Timing constants
#define DEBOUNCE_MS       50      // Button debounce time
#define LONG_PRESS_MS     3000    // Long press threshold (3 seconds)
#define REMOTE_TRIG_DELAY 10000   // Remote trigger delay (10 seconds)

// Watchdog
#define WATCHDOG_MS     8000  // Herstart na 8 seconden als loop() blokkeert

// Settings storage key — verhoog versienummer als Settings struct wijzigt
#define SETTINGS_KEY    "/kv/phi_pre10_v9"  // Bump bij elke Settings struct wijziging

// Input selection
#define INPUT_COUNT     5
#define INPUT_DAC       0   // GPA7 - XLR 1 (default)
#define INPUT_PHONO     1   // GPA6 - XLR 2
#define INPUT_TAPE      2   // GPA5 - XLR 3
#define INPUT_TUNER     3   // GPA4 - RCA 1
#define INPUT_SURROUND  4   // GPA3 - RCA 2

#endif
