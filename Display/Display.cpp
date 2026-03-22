/*
 * Display.cpp — PHI Reference Pre-10  v7
 *
 * Wijzigingen v7:
 *   - Kaartenmenu (2×3 grid) vervangt lijstmenu
 *   - Nieuw scherm SCR_VOLUME_GAIN: twee kolommen met sliders
 *       Links : Startup Vol · Balance · LF Gain · LF Out · MF Out
 *       Rechts: Input Offset ±6 dB per ingang
 *   - SCR_GAIN en SCR_BALANCE vervallen; vervangen door SCR_VOLUME_GAIN
 *   - Lichtere kleurenpalet voor menu/sub-schermen
 *   - Touch-first selectie; encoders hebben geen press-functie meer
 *   - Rechter encoder past geselecteerde parameter aan
 *   - adjustVGSelection() publiek voor Encoder.cpp
 *   - inputOffset[INPUT_COUNT] toegevoegd aan Settings (zie Settings.h v7)
 */

#include "Display.h"
#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"
#include <Arduino_GigaDisplay.h>   // GigaDisplayBacklight
#include "Config.h"
#include <math.h>
#include "Controls.h"
#include "VolumeControl.h"
#include "MCP23017.h"
#include "Settings.h"
#include "InputControl.h"
#include "Encoder.h"
#include "GainControl.h"
#include "IR_Remote.h"

// ── Fonts ─────────────────────────────────────────────────────────────────────
#include <Fonts/FreeSans9pt7b.h>

#include "AAFont.h"
#include "ArtifaktMedium14AA.h"
#include "ArtifaktMedium18AA.h"
#include "ArtifaktMedium24AA.h"
#include "ArtifaktMedium36AA.h"
#include "ArtifaktMedium48AA.h"
#include "Orbitron48AA.h"
#include "phi_logo_gray.h"

#define AA_VOL  (&ArtifaktMedium48AA)
#define AA_MED  (&ArtifaktMedium36AA)
#define AA_SM   (&ArtifaktMedium24AA)
#define AA_XS   (&ArtifaktMedium18AA)
#define AA_XXS  (&ArtifaktMedium14AA)

#define FW_VERSION  "v9-78"

GigaDisplay_GFX          display;
Arduino_GigaDisplayTouch touch;
GigaDisplayBacklight backlight;

extern uint8_t screenBrightness;

// Zet screenBrightness én hardware backlight synchroon
static inline void setScreenBrightness(uint8_t b) {
  screenBrightness = b;
  backlight.set((uint8_t)((uint32_t)b * 100 / 255));  // 0-255 → 0-100
}

// ── Non-blocking fade state machine ──────────────────────────────────────────
// Fade loopt over ~400ms in 20 stappen à 20ms, getiket vanuit updateDisplay().
// loop() blijft ongestoord doordraaien — encoders, IR en touch werken tijdens fade.
static uint8_t  fadeTo      = 0;
static uint8_t  fadeFrom    = 0;
static uint32_t fadeStartMs = 0;
static bool     fadeActive  = false;

static const uint8_t  FADE_STEPS   = 20;
static const uint16_t FADE_STEP_MS = 20;   // totaal: 400ms

static void fadeTobrightness(uint8_t target) {
  if (screenBrightness == target) { fadeActive = false; return; }
  fadeFrom    = screenBrightness;
  fadeTo      = target;
  fadeStartMs = millis();
  fadeActive  = true;
}

// Tick: aanroepen bovenaan updateDisplay() vóór alle andere logica
static void tickFade() {
  if (!fadeActive) return;
  uint32_t elapsed = millis() - fadeStartMs;
  uint8_t  step    = (uint8_t)min((uint32_t)FADE_STEPS,
                                   elapsed / FADE_STEP_MS + 1);
  int16_t val = (int16_t)fadeFrom
              + (int16_t)((int32_t)((int16_t)fadeTo - (int16_t)fadeFrom)
                          * step / FADE_STEPS);
  setScreenBrightness((uint8_t)constrain(val, 0, 255));
  if (step >= FADE_STEPS) {
    setScreenBrightness(fadeTo);
    fadeActive = false;
  }
}

// ── Helderheidsschaling ───────────────────────────────────────────────────────
uint16_t dimC(uint16_t c) {
  if (screenBrightness >= 255) return c;
  uint32_t s = screenBrightness;
  uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * s / 255);
  uint8_t g = (uint8_t)(((c >>  5) & 0x3F) * s / 255);
  uint8_t b = (uint8_t)(( c        & 0x1F) * s / 255);
  return (r << 11) | (g << 5) | b;
}

// ── Kleurenpalet ──────────────────────────────────────────────────────────────
// Hoofdscherm (ongewijzigd)
static const uint16_t C_BG        = 0x0000u;  // Zwart
static const uint16_t C_WHITE     = 0xFFFFu;
static const uint16_t C_HAIRLINE  = 0x2104u;  // Dunne lijn
static const uint16_t C_DARK      = 0x1082u;  // Boot/warmup kaart

// Accent kleuren
static const uint16_t ACCENT_WHITE      = 0xF79Eu;  // Pure White
static const uint16_t ACCENT_WARM_WHITE = 0xF759u;  // Ivory
static const uint16_t ACCENT_BLUE       = 0x8DDBu;  // Steel Blue
static const uint16_t ACCENT_AMBER      = 0xD481u;  // Amber Gold
static const uint16_t ACCENT_PHOSPHOR_G = 0x07E0u;  // Phosphor Green (P31)
static const uint16_t ACCENT_PHOSPHOR_Y = 0xAFE0u;  // Warm Phosphor (P4)
static const uint16_t ACCENT_VFD_CYAN   = 0x07F9u;  // VFD Cyan (Futaba/Noritake)
static const uint16_t ACCENT_AMBER_P43  = 0xFC60u;  // Amber P43 (Tektronix)
uint16_t accentColor = ACCENT_WARM_WHITE;
static const uint16_t C_VOL_MAX = 0xC8A0u;  // Rood-oranje — max volume waarschuwing
static inline uint16_t accent() { return accentColor; }
static const uint16_t ACCENT_PRESETS[8] = {
  ACCENT_WARM_WHITE, ACCENT_WHITE, ACCENT_BLUE, ACCENT_AMBER,
  ACCENT_PHOSPHOR_G, ACCENT_PHOSPHOR_Y, ACCENT_VFD_CYAN, ACCENT_AMBER_P43
};
static inline uint16_t volColor() { return accent(); }  // Altijd accentkleur, max vol geeft blink

static const uint16_t C_GRAY      = 0xAD75u;  // Hoofdscherm tekst
static const uint16_t C_GRAY_DIM  = 0x4A49u;  // Gedimde tekst

// Menu / sub-scherm — lichter dan v6
static const uint16_t C_CARD_BG   = 0x2104u;  // Kaart / rij achtergrond
static const uint16_t C_ROW_SEL   = 0x1A66u;  // Geselecteerde rij (gedempt petrol)
static const uint16_t C_LBL       = 0x738Eu;  // Label tekst normaal
static const uint16_t C_LBL_SEL   = 0xBDF7u;  // Label geselecteerd (soft silver)
static const uint16_t C_VAL       = 0x94B2u;  // Waarde neutraal (soft grey)
static const uint16_t C_VAL_SEL   = 0xDE59u;  // Waarde geselecteerd (champagne)
static const uint16_t C_SLIDER_FILL = 0xC554u; // Slider fill (premium bronze/champagne)
static const uint16_t C_SLIDER_TRACK = 0x18C3u; // Slider track (dark graphite)
static const uint16_t C_SEC_LBL   = 0x39C7u;  // Sectie-label
static const uint16_t C_STATUS_OK = 0x3D67u;  // Groen alleen voor statuswaarden (On/OK), gedempt
static const uint16_t C_STATUS_ERR = 0xF800u; // Rood voor fouten
static const uint16_t C_STATUS_OK_FILL = 0x02A1u; // Donker groen vlak voor OK-badges
static const uint16_t C_CHANGED    = 0x55AAu; // Gedempte jade voor user-changed waarden

// Knoppen (display stepper +/-)
static const uint16_t C_BTN_FILL  = 0xEF7Du;
static const uint16_t C_BTN_TEXT  = 0x3186u;

// Keyboard
static const uint16_t C_KB_FILL   = 0x2945u;
static const uint16_t C_KB_TEXT   = 0x9CF3u;
static const uint16_t C_KB_SHFT   = 0x1581u;

// ── UI spacing/style tokens (consistente premium rhythm) ─────────────────────
#define UI_PAD_L       12
#define UI_PAD_R       14
#define UI_CTRL_GAP    12
#define UI_CTRL_H_INSET 16
#define UI_CARD_R       4
#define UI_BTN_R        5

// ── Externe state ─────────────────────────────────────────────────────────────
extern uint8_t  currentVolume;
extern uint8_t  currentInput;
extern bool     isMuted;
extern bool     inStandby;
extern int8_t   balanceOffset;
extern char     inputNames[INPUT_COUNT][16];
extern uint8_t  diagRecoveryAttempts;
extern bool     diagBootDegraded;
extern bool     warmStandbyEnabled;
extern bool     warmTrigRelayClosed;
bool            isWarmStandbyActive = false;  // true als systeem IN warm standby gegaan is
extern bool     bypassLF;
extern bool     bypassMF;

// ── Schermtoestand ────────────────────────────────────────────────────────────
enum ScreenState {
  SCR_BOOT, SCR_WARMUP, SCR_MAIN,
  SCR_MENU,
  SCR_VOLUME_GAIN,
  SCR_TRANSFORMER,
  SCR_OFFSETS,
  SCR_NAMES,
  SCR_KB_EDIT,
  SCR_DISPLAY,
  SCR_THEME,
  SCR_SYS,
  SCR_DIAG,
  SCR_IR_LEARN,
  // SCR_FREQ_ANIM verwijderd — mute gebruikt SCR_MAIN
};
static ScreenState currentScreen = SCR_BOOT;

static uint32_t bootStartTime     = 0;
static uint32_t warmupStartTime   = 0;

// ── Volume ramp-up bij standby-exit ──────────────────────────────────────────
// Hardware volume loopt van 0 naar currentVolume, zonder currentVolume te wijzigen.
// Als gebruiker encoder draait onderbreekt adjustVolume() de ramp automatisch.
#define VOL_RAMP_STEP_MS   25     // ms per stap
#define VOL_RAMP_STEPS     60     // totaal aantal stappen → ~1.5 seconden
static bool     volRampActive  = false;
static uint8_t  volRampTarget  = 0;    // doelwaarde = currentVolume op moment van start
static uint8_t  volRampCurrent = 0;    // huidige hardware waarde tijdens ramp
static uint32_t volRampLastMs  = 0;

// Start een volume ramp van 0 naar target. Schrijft meteen 0x00 naar hardware.
static void startVolRamp(uint8_t target) {
  if (target == VOL_OFF) return;  // niets te rampen
  volRampTarget  = target;
  volRampCurrent = VOL_OFF;  // start van hardwarenul
  volRampLastMs  = millis();
  volRampActive  = true;
  setVolume(0, 0);  // hardware naar 0, currentVolume ongewijzigd
}

// Aanroepen vanuit updateDisplay() — één stap per interval.
static void tickVolRamp() {
  if (!volRampActive) return;
  uint32_t now = millis();
  if ((now - volRampLastMs) < VOL_RAMP_STEP_MS) return;
  volRampLastMs = now;

  // Stapgrootte: lineair, minimaal 1
  uint8_t step = (uint8_t)max(1, (int)volRampTarget / VOL_RAMP_STEPS);
  uint16_t next = (uint16_t)volRampCurrent + step;
  if (next >= volRampTarget) {
    volRampActive  = false;
    volRampCurrent = volRampTarget;
    applyVolume();  // eindstand via applyVolume() — neemt balans/offset mee
  } else {
    volRampCurrent = (uint8_t)next;
    // Direct naar hardware zonder balans/offset — ramp is puur hardware fade
    setVolume(volRampCurrent, volRampCurrent);
  }
}
static uint32_t standbyTextShowMs = 0;
#define STANDBY_TEXT_MS  8000
#define CALM_AFTER_MS    6000
#define BACKLIGHT_OFF_EXTRA_MS 1800000UL // +30 min na deep-dim: backlight volledig uit
#define MENU_AUTOCLOSE_MS 300000UL   // 5 min inactiviteit op menu-schermen

// ── Touch / activity state ────────────────────────────────────────────────────
static bool     fingerDown     = false;
static uint32_t lastTouchTime  = 0;
static uint32_t touchLockoutMs = 0;   // Start-timestamp van lockout
static uint32_t touchLockoutDur = 0;  // Duur van lockout in ms
static uint32_t irResetPressStartMs = 0;
static bool     irResetHoldTriggered = false;
static uint32_t lastActivityMs   = 0;
bool            mainCalmMode     = false;
static uint8_t  mainCalmBlend    = 0;     // 0..255 voor vloeiende calm-transition
static uint8_t  panelBlend       = 255;   // 0=volumezone+panel onzichtbaar, 255=volledig zichtbaar
static bool     fontIsLarge      = false; // huidige weergavestaat: groot of klein font
// Crossfade state: IDLE → FADE_OUT → SNAP → FADE_IN → IDLE
enum XfadeState : uint8_t { XF_IDLE, XF_FADE_OUT, XF_FADE_IN };
static XfadeState xfadeState     = XF_IDLE;
static bool     xfadeTargetLarge = false; // gewenste eindstaat na crossfade

// Hairline shimmer — periodieke highlight die over de lijn loopt
static bool     shimmerActive    = false;
static float    shimmerX         = 0.0f;  // huidige x-positie van shimmer centrum
static float    shimmerDir       = 1.0f;  // +1 = links→rechts, -1 = rechts→links
static uint32_t shimmerLastEndMs = 0;     // wanneer laatste shimmer eindigde
static uint32_t shimmerNextMs    = 0;     // wanneer volgende shimmer mag starten

// Max volume blink — 3× knipperen als Eff. Atten. >= 0 dB bereikt wordt
static uint8_t  volBlinkCount    = 0;
static bool     volBlinkOn       = true;   // true = volume zichtbaar (default)
static uint32_t volBlinkLastMs   = 0;
static bool     volBlinkNeedsRestore = false;  // true na blinkreeks die eindigde op 'uit'
#define VOL_BLINK_INTERVAL_MS    280
static bool     deepDimActive    = false; // Tweede dimfase actief op hoofdscherm
static bool     backlightOffActive = false; // Derde dimfase: backlight volledig uit
// Mute-dim state verwijderd — mute toont tekst op hoofdscherm
static bool     switchingInput = false;
static char     switchTargetName[16] = "";
#define FULL_BRIGHTNESS  255
#define TOUCH_RELEASE_MS  80
#define IR_RESET_HOLD_MS  900

extern uint8_t uiBrightnessPct;
extern uint8_t dimPercent;
static inline uint8_t activeBrightness() {
  return (uint8_t)((uint32_t)uiBrightnessPct * 255 / 100);
}
static inline uint8_t dimBrightness() {
  return (uint8_t)((uint32_t)activeBrightness() * dimPercent / 100);
}
static inline uint8_t deepDimBrightness() {
  uint8_t d = dimBrightness();
  // Deep dim = helft van dim niveau, minimaal 3, maximaal dim-10
  // Altijd merkbaar lager dan normaal dim, ook als dim al laag is
  uint8_t deep = (uint8_t)max(3, (int)d / 2);
  if (deep >= d && d > 3) deep = d - 3;  // altijd lager dan dim
  return deep;
}

// ── Display instellingen ──────────────────────────────────────────────────────
uint16_t dimDelaySec     = 30;
uint8_t  dimPercent      = 50;
uint8_t  deepDimDelayMin = 10;
uint8_t  uiBrightnessPct = 100;
#define VOL_UNITS_STEPS   0
#define VOL_UNITS_DB      1
#define VOL_UNITS_PERCENT 2

uint8_t  volUnitsMode = VOL_UNITS_DB;
// volumeCurve declared in VolumeControl.cpp, extern via Settings.h
uint8_t  mainAccentPct = 92;   // 70..100, govern accent op hoofdscherm

// ── Volume balk state ─────────────────────────────────────────────────────────

// ── Layout constanten ─────────────────────────────────────────────────────────
#define TBAR_Y       480  // touchbar removed — equals SCREEN_H

#define HDR_H         48
#define HDR_LINE_Y    47
#define HDR_BACK_X    12
#define HDR_BACK_Y     8
#define HDR_BACK_W   112
#define HDR_BACK_H    32
#define CONT_Y        56
#define CONT_H       (SCREEN_H - CONT_Y)  // full height below header

// Detail panel afmetingen vroeg gedefinieerd — gebruikt door showSwitchingScreen en drawDetailPanel
#define DP_TOP      385
// Eenvoudig detail panel (1 rij)
#define SDP_H        66
#define SDP_COLS      4
#define SDP_COL_W   200
#define SDP_PAD      12
#define SDP_LBL_Y    22   // baseline label (relatief t.o.v. DP_TOP)
#define SDP_VAL_Y    62   // baseline waarde — 40px onder label
#define DP_COL_W    200

// ── Volume & Gain scherm ──────────────────────────────────────────────────────
// selectie-IDs
#define VG_NONE    -1
#define VG_MAXVOL   3
#define VG_STARTUP  0
#define VG_BALANCE  1
#define VG_LFGAIN   2
#define VG_MUTEBOOT 4
#define VG_IO_BASE 10   // VG_IO_BASE+i = offset voor ingang i
#define VG_SV_BASE 20   // VG_SV_BASE+i = startup volume voor ingang i

static int8_t vgSelection = VG_NONE;
static int8_t dsSelection = VG_NONE;  // 0=Brightness 1=Dim delay 2=Dim level 3=Deep dim

// Linker kolom breedte
#define VG_COL_W   398
#define VG_SEP_X   400
#define VG_PAD      14
#define VG_LBL_W   200  // labelzone in slider-rijen

// Rij-hoogte en gap
#define ROW_H    46
#define ROW_G     8

// Rechter kolom
#define VGR_X       (VG_SEP_X + 4)
#define VGR_W       (SCREEN_W - VGR_X - 4)

// ── Keyboard state ────────────────────────────────────────────────────────────
static int8_t  namesSelection = -1;
static char    editBuffer[16] = "";
static uint8_t editLen        = 0;
static bool    kbShift        = false;

// ── IR learn state ───────────────────────────────────────────────────────────
static uint8_t  irLearnProtocol = 0;
static uint16_t irLearnAddress  = 0;
static uint8_t  irLearnCommand  = 0;
static uint32_t irLearnRaw      = 0;
static uint32_t irSavedFeedbackMs = 0;  // timestamp "Saved!" feedback, 0=inactief
#define IR_SAVED_FEEDBACK_MS  220        // zichtbaarheidsduur in ms
static bool     irLearnRepeat   = false;
static bool     irLearnHasData  = false;
static uint8_t  irLearnTarget   = IR_ACT_VOL_UP;
static bool     irLearnArmed    = false;

static const char* IR_TARGET_NAMES[IR_ACTION_COUNT] = {
  "Volume +", "Volume -", "Input +", "Input -",
  "Balance L", "Balance R", "Mute", "Standby",
  "Input 1", "Input 2", "Input 3", "Input 4", "Input 5"
};

static const char* KB_UPPER[] = { "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ" };
static const char* KB_LOWER[] = { "abcdefghijklm", "nopqrstuvwxyz" };
#define KB_NROWS 2
#define KB_Y_START  304
#define KB_KEY_H     40
#define KB_KEY_GAP    6
#define KB_ROW_GAP    8
#define KB_SP_Y    (KB_Y_START + 2*(KB_KEY_H + KB_ROW_GAP))
#define KB_SP_H     40
#define KB_SHIFT_X  10
#define KB_SHIFT_W  90
#define KB_SPC_X   110
#define KB_SPC_W   370
#define KB_DEL_X   490
#define KB_DEL_W   110
#define KB_OK_X    610
#define KB_OK_W    180

static int16_t kbKeyX(int row, int col) {
  int nk = strlen(KB_UPPER[row]);
  int16_t tw = nk * (KB_KEY_H + KB_KEY_GAP) - KB_KEY_GAP;
  return (SCREEN_W - tw) / 2 + col * (KB_KEY_H + KB_KEY_GAP);
}
static int16_t kbKeyY(int row) {
  return KB_Y_START + row * (KB_KEY_H + KB_ROW_GAP);
}

// ── Display scherm layout ─────────────────────────────────────────────────────
#define SR_H        52   // uniforme hoogte alle instellingsrijen
#define DS_BTN_G    6
#define DS_ROW0_Y  (CONT_Y + 4)
#define DS_ROW1_Y  (DS_ROW0_Y + SR_H + DS_BTN_G)
#define DS_ROW2_Y  (DS_ROW1_Y + SR_H + DS_BTN_G)
#define DS_ROW3_Y  (DS_ROW2_Y + SR_H + DS_BTN_G)
#define DS_ROW4_Y  (DS_ROW3_Y + SR_H + DS_BTN_G)
#define DS_ROW5_Y  (DS_ROW4_Y + SR_H + DS_BTN_G)
#define DS_ROW6_Y  (DS_ROW5_Y + SR_H + DS_BTN_G)
#define DS_LBL_W   300
#define DS_CTRL_X  346

// ── System scherm ─────────────────────────────────────────────────────────────
#define SYS_ROW_G    8
#define SYS_Y1      (CONT_Y + 4)
#define SYS_Y2      (SYS_Y1 + SR_H + SYS_ROW_G)
#define SYS_Y3      (SYS_Y2 + SR_H + SYS_ROW_G)
#define SYS_Y4      (SYS_Y3 + SR_H + SYS_ROW_G)
#define SYS_Y5      (SYS_Y4 + SR_H + SYS_ROW_G)
#define SYS_Y6      (SYS_Y5 + SR_H + SYS_ROW_G)
#define SYS_Y7      (SYS_Y6 + SR_H + SYS_ROW_G)
#define TOTAL_RAM_KB  512

// ════════════════════════════════════════════════════════════════════════════
//  Forward declarations
// ════════════════════════════════════════════════════════════════════════════
static void drawMainMenu();
static void drawVolumeGainScreen();
static void drawTransformerScreen();
static void drawOffsetsScreen();
static void drawNamesScreen();
static void drawKbEditScreen();
static void drawIRLearnScreen();
static void drawDiagnosticsScreen();
static void drawThemeMotionScreen();
static void drawSimpleDetailPanel();
static void redrawDetailAttenCard();
static void redrawDetailBalanceCard();
static float targetEffAttenDb();
static void drawMainStatusChips();
static void drawInputName();
static void drawMainPrimaryValue();
static void drawScreenHeader(const char* title);
static void clearAndDrawInputName();
static void clearPrimaryValueZone();
static bool isDefaultVGToggle(const char* label);
static uint16_t mainHairlineColor();
static void drawMainHairline();
static void drawDotMatrixString(const AAFont* font, const char* str,
                                int16_t x, int16_t baseline,
                                uint16_t fgColor, AAAlign align, int16_t areaW,
                                uint8_t pitch, uint8_t dotR, uint8_t charGap);

static uint16_t scale565(uint16_t c, uint8_t pct) {
  uint16_t r = (uint16_t)(((c >> 11) & 0x1F) * pct / 100);
  uint16_t g = (uint16_t)(((c >> 5)  & 0x3F) * pct / 100);
  uint16_t b = (uint16_t)(( c        & 0x1F) * pct / 100);
  if (r > 0x1F) r = 0x1F;
  if (g > 0x3F) g = 0x3F;
  if (b > 0x1F) b = 0x1F;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Schaal RGB565 kleur met 0–255 alpha (0=zwart, 255=ongewijzigd)
static uint16_t alpha565(uint16_t c, uint8_t a) {
  if (a == 255) return c;
  if (a == 0)   return 0;
  uint16_t r = (uint16_t)(((c >> 11) & 0x1F) * a / 255);
  uint16_t g = (uint16_t)(((c >>  5) & 0x3F) * a / 255);
  uint16_t b = (uint16_t)(( c        & 0x1F) * a / 255);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint8_t calmMixPct(uint8_t activePct, uint8_t calmPct) {
  int16_t d = (int16_t)calmPct - (int16_t)activePct;
  return (uint8_t)(activePct + (d * (int16_t)mainCalmBlend) / 255);
}

static uint16_t governedMainAccent() {
  uint8_t calm = (uint8_t)max(50, (int)mainAccentPct - 30);
  return scale565(accent(), calmMixPct(mainAccentPct, calm));
}

static uint8_t calmAnimStep(uint8_t remain) {
  return (uint8_t)max(1, (int)((remain + 14) / 15));
}

// Stapgrootte voor panelBlend animaties — groter = snellere fade
static uint8_t blendAnimStep(uint8_t cur, uint8_t tgt) {
  uint8_t diff = cur < tgt ? tgt - cur : cur - tgt;
  return diff > 32 ? 28 : diff > 12 ? 16 : 6;
}

static uint8_t calmAnimIntervalMs() {
  return 50;
}

static uint32_t profileCalmAfterMs() {
  return CALM_AFTER_MS;
}

static uint32_t profileBalanceVisibleMs() {
  return 4000;
}


static void drawStatusChip(int16_t x, int16_t y, int16_t w, int16_t h,
                           const char* txt, uint16_t txtC, uint16_t bgC) {
  display.fillRoundRect(x, y, w, h, 6, dimC(bgC));
  display.drawRoundRect(x, y, w, h, 6, dimC(C_HAIRLINE));
  AAFont_drawString(AA_XXS, txt, x, y + h - 8, dimC(txtC), dimC(bgC), AA_CENTER, w);
}

static uint16_t selectionFrameColor() {
  // Vast frame voorkomt zichtbare "flashing" aan de linkerzijde van geselecteerde rijen.
  return dimC(scale565(C_VAL_SEL, 92));
}

static void drawScreenHeader(const char* title) {
  uint16_t hdrBg = dimC(scale565(C_CARD_BG, 84));
  uint16_t backFill = dimC(scale565(C_DARK, 72));
  display.fillRect(0, 0, SCREEN_W, HDR_H, hdrBg);
  display.fillRect(0, HDR_LINE_Y, SCREEN_W, 1, dimC(C_HAIRLINE));
  // Subtiele premium accentlijn onder titelzone
  display.fillRect(SCREEN_W/2 - 96, HDR_LINE_Y - 2, 192, 1, dimC(scale565(accent(), 34)));
  AAFont_drawString(AA_SM, title, 0, 36,
                   dimC(C_LBL_SEL), hdrBg, AA_CENTER, SCREEN_W);
  // ← Back knop
  display.fillRoundRect(HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H, UI_BTN_R, backFill);
  display.drawRoundRect(HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H, UI_BTN_R, dimC(scale565(C_HAIRLINE, 68)));
  AAFont_drawString(AA_XXS, "< BACK", HDR_BACK_X, HDR_BACK_Y + 21,
                   dimC(C_GRAY), backFill, AA_CENTER, HDR_BACK_W);
}

static bool touchedHdrBack(int16_t tx, int16_t ty) {
  return (tx >= HDR_BACK_X && tx <= (HDR_BACK_X + HDR_BACK_W)
       && ty >= HDR_BACK_Y && ty <= (HDR_BACK_Y + HDR_BACK_H));
}

// Sectie-label (dunne tekst, geen lijn)
static void drawSecLabel(int16_t x, int16_t y, const char* label) {
  AAFont_drawString(AA_XXS, label, x, y,
                   dimC(C_SEC_LBL), C_BG, AA_LEFT, SCREEN_W - x - 4);
}

// Segmented balk — zelfde stijl als volumebalk hoofdscherm
// x,y = linkerbovenhoek balk; w = breedte; h = zichtbare hoogte (10px)
// pct = 0.0–1.0; bipolar = centrum = nul
static void drawSegBar(int16_t x, int16_t y, int16_t w,
                       float pct, bool sel, bool bipolar = false) {
  const int16_t STEP = 6, BAR = 4, H = 10;
  int16_t N = w / STEP;
  pct = constrain(pct, 0.0f, 1.0f);
  int16_t filled = (int16_t)(pct * N);
  uint16_t fillC  = sel ? dimC(C_VAL_SEL) : dimC(C_SLIDER_FILL);
  uint16_t emptyC = dimC(C_SLIDER_TRACK);

  for (int16_t i = 0; i < N; i++) {
    int16_t sx  = x + i * STEP;
    int16_t ex  = (i % 4 == 0) ? 4 : (i % 2 == 0) ? 2 : 0;
    int16_t bh  = BAR + ex, by = y + H/2 - bh/2;
    bool active;
    if (bipolar) {
      int16_t cx = N / 2;
      active = (filled > cx) ? (i >= cx && i < filled)
             : (filled < cx) ? (i >= filled && i < cx)
             : false;
    } else {
      active = (i < filled);
    }
    display.fillRect(sx, by, BAR, bh, active ? fillC : emptyC);
  }
  // Schuifknop — gecentreerd op filled*STEP
  int16_t tx = x + filled * STEP - 1;
  tx = constrain(tx, x, x + w - 4);
  // Middenlijn voor bipolaire balk — zelfde positie als thumb bij center
  if (bipolar) {
    int16_t cx = x + (N / 2) * STEP - 1;  // zelfde formule als thumb bij pct=0.5
    display.fillRect(cx, y - 3, 1, H + 6, dimC(C_HAIRLINE));
  }
  display.fillRect(tx, y, 3, H, sel ? dimC(C_VAL_SEL) : dimC(scale565(C_SLIDER_FILL, 78)));
}

// ── VGC kleurpalet — midnight steel blauw (v9-70) ────────────────────────────
static const uint16_t VGC_BG_NORM    = 0x0882u;  // #081010 kaart bg normaal
static const uint16_t VGC_BG_SEL     = 0x0908u;  // #082040 kaart bg geselecteerd
static const uint16_t VGC_FILL_NORM  = 0x31A8u;  // #2C3640 fill normaal — neutraal grijs
static const uint16_t VGC_FILL_SEL   = 0x422Au;  // #3A4652 fill geselecteerd — neutraal grijs lichter
static const uint16_t VGC_BDR_NORM   = 0x21A7u;  // #203438 gedimde border
static const uint16_t VGC_BDR_SEL    = 0x7E5Cu;  // #78c8e0 lichtblauw accent geselecteerd
static const uint16_t VGC_BDR_CHG    = 0x3B30u;  // #386480 gewijzigd
static const uint16_t VGC_LBL_NORM   = 0x8CB3u;  // #889498 label normaal
static const uint16_t VGC_LBL_SEL    = 0x7E5Cu;  // #78c8e0 label geselecteerd
static const uint16_t VGC_VAL_NORM   = 0xCE9Bu;  // #c8d0d8 waarde normaal
static const uint16_t VGC_VAL_SEL    = 0xF7BFu;  // #f0f4f8 waarde geselecteerd
static const uint16_t VGC_VAL_CHG    = 0xA65Cu;  // #a0c8e0 waarde gewijzigd
static const uint16_t VGC_UNIT_NORM  = 0x29E9u;  // #283c48 eenheid normaal
static const uint16_t VGC_UNIT_SEL   = 0x7E5Cu;  // #78c8e0 eenheid geselecteerd
static const uint16_t VGC_TOG_BG     = 0x0882u;  // #081010 toggle label-balk bg
static const uint16_t VGC_BTN_BG     = 0x10C4u;  // #101820 knop bg niet-gekozen
static const uint16_t VGC_BTN_BG_ON  = 0x0908u;  // #082040 knop bg gekozen
static const uint16_t VGC_BTN_BDR    = 0x21A7u;  // #203438 knop border niet-gekozen
static const uint16_t VGC_BTN_BDR_ON = 0x7E5Cu;  // #78c8e0 knop border gekozen
static const uint16_t VGC_BTN_TXT    = 0x5B4Eu;  // #586870 knop tekst niet-gekozen
static const uint16_t VGC_BTN_TXT_ON = 0xD77Fu;  // #d0ecf8 knop tekst gekozen
static const uint16_t VGC_BTN_BDR_CH = 0x3B30u;  // #386480 gewijzigd border
static const uint16_t VGC_BTN_TXT_CH = 0x7E5Cu;  // #78c8e0 gewijzigd tekst
static const uint16_t VGC_DIVIDER    = 0x1925u;  // #182428 verticale scheidingslijn

// SR_H: uniforme hoogte voor alle instellingsrijen — gedefinieerd bij Display layout hierboven

// drawSliderRow — filled-card stijl, zelfde patroon als drawFilledCard in Volume scherm
// zeroMarkPct >= 0: teken 0-streepje op die positie + bipolaire fill vanuit dat punt.
// bipolar=true + zeroMarkPct >= 0: asymmetrisch nulpunt.
// bipolar=true + zeroMarkPct < 0:  symmetrisch (midden = 0.5).
// bipolar=false + zeroMarkPct >= 0: lineaire fill + streepje + bipolaire fill.
static void drawSliderRow(int16_t x, int16_t y, int16_t w,
                          const char* label, int16_t /*lblW*/,
                          float pct, const char* valStr,
                          bool sel, bool bipolar = false, bool changed = false,
                          float zeroMarkPct = -1.0f) {
  uint16_t bg = sel ? dimC(VGC_BG_SEL) : dimC(VGC_BG_NORM);
  display.fillRoundRect(x, y, w, SR_H, UI_CARD_R, bg);

  uint16_t fillC = dimC(sel ? VGC_FILL_SEL : VGC_FILL_NORM);
  int16_t fx = x + 2, fy = y + 2, fw_max = w - 4, fh = SR_H - 4;
  int16_t clh = (SR_H * 4) / 5;

  // Bepaal nulpunt fractie
  float zeroPct = (zeroMarkPct >= 0.0f) ? zeroMarkPct
                : (bipolar)             ? 0.5f
                :                         -1.0f;

  if (zeroPct >= 0.0f) {
    // Bipolaire fill vanuit zeroPct (werkt voor symmetrisch en asymmetrisch)
    float dev = pct - zeroPct;
    int16_t zero_x = fx + (int16_t)(zeroPct * fw_max);
    if (dev > 0.005f) {
      float range = 1.0f - zeroPct;
      int16_t fw = (range > 0.001f) ? min((int16_t)(dev / range * (fw_max - (zero_x - fx))), (int16_t)(fw_max - (zero_x - fx))) : 0;
      if (fw > 0) display.fillRect(zero_x, fy, fw, fh, fillC);
    } else if (dev < -0.005f) {
      float range = zeroPct;
      int16_t fw = (range > 0.001f) ? min((int16_t)(-dev / range * (zero_x - fx)), (int16_t)(zero_x - fx)) : 0;
      if (fw > 0) display.fillRect(zero_x - fw, fy, fw, fh, fillC);
    }
    // 0-streepje — accent kleur, 2px breed
    display.fillRect(zero_x, y + (SR_H - clh) / 2, 2, clh, dimC(VGC_BDR_SEL));
  } else {
    // Gewone lineaire fill
    int16_t fw = min((int16_t)(pct * fw_max), fw_max);
    if (fw > 0) display.fillRect(fx, fy, fw, fh, fillC);
  }

  uint16_t bdr = sel ? dimC(VGC_BDR_SEL) : dimC(VGC_BDR_NORM);
  display.drawRoundRect(x,     y,     w,     SR_H,     UI_CARD_R, bdr);
  display.drawRoundRect(x + 1, y + 1, w - 2, SR_H - 2, UI_CARD_R, bdr);

  uint16_t lblC = dimC(sel ? VGC_LBL_SEL : VGC_LBL_NORM);
  AAFont_drawString(AA_XXS, label, x + UI_PAD_L, y + SR_H/2 + 7, lblC, bg, AA_LEFT, w / 2);

  uint16_t valC = sel ? dimC(VGC_LBL_SEL) : changed ? dimC(VGC_VAL_CHG) : dimC(scale565(VGC_LBL_NORM, 72));
  AAFont_drawString(AA_XXS, valStr, x + UI_PAD_L, y + SR_H/2 + 7,
                    valC, bg, AA_RIGHT, w - UI_PAD_L - UI_PAD_R);
}

// Toggle rij: [label | On/Off knoppen]
static void drawTogRow(int16_t x, int16_t y, int16_t w,
                       const char* label, bool on,
                       const char* onText = "-6 dB", const char* offText = "0 dB",
                       bool changed = false) {
  uint16_t bg   = dimC(C_CARD_BG);
  uint16_t lblC = dimC(C_LBL);
  display.fillRoundRect(x, y, w, ROW_H, UI_CARD_R, bg);

  // Label links, knoppen rechts uitgelijnd — consistent met alle andere rijen.
  const int16_t oW = 160, oG = 10;
  const int16_t controlsW = 2 * oW + oG;
  AAFont_drawString(AA_XXS, label, x + UI_PAD_L, y + ROW_H/2 + 7, lblC, bg, AA_LEFT, w - controlsW - (UI_PAD_L + UI_PAD_R));

  // Twee-knops toggle rechts (zelfde stijl als warm standby)
  const int16_t oH = ROW_H - UI_CTRL_H_INSET;
  const int16_t oY = y + (ROW_H - oH) / 2;
  const int16_t oX = x + w - controlsW - UI_PAD_R;

  uint16_t fOn  = on ? dimC(C_ROW_SEL) : dimC(C_DARK);
  uint16_t fOff = on ? dimC(C_DARK)    : dimC(C_ROW_SEL);
  uint16_t tOn  = on
                ? dimC((strcmp(onText, "On") == 0) ? C_STATUS_OK : (changed ? C_CHANGED : C_VAL_SEL))
                : dimC(C_LBL);
  uint16_t tOff = on
                ? dimC(C_LBL)
                : dimC((strcmp(offText, "Off") == 0) ? C_VAL_SEL : (changed ? C_CHANGED : C_VAL_SEL));

  display.fillRoundRect(oX,         oY, oW, oH, UI_BTN_R, fOn);
  display.fillRoundRect(oX + oW + oG, oY, oW, oH, UI_BTN_R, fOff);
  if (on) display.drawRoundRect(oX, oY, oW, oH, UI_BTN_R, dimC(C_LBL_SEL));
  else    display.drawRoundRect(oX + oW + oG, oY, oW, oH, UI_BTN_R, dimC(C_LBL_SEL));

  AAFont_drawString(AA_XXS, onText,  oX,          y + ROW_H/2 + 7, tOn,  fOn,  AA_CENTER, oW);
  AAFont_drawString(AA_XXS, offText, oX + oW + oG, y + ROW_H/2 + 7, tOff, fOff, AA_CENTER, oW);
}

static constexpr int16_t VG_SPLIT_HALF_GAP   = 16;
static constexpr int16_t VG_SPLIT_BTN_W      = 64;
static constexpr int16_t VG_SPLIT_BTN_GAP    = 8;
static constexpr int16_t VG_SPLIT_LABEL_GAP  = 8;
static constexpr int16_t VG_SPLIT_MIN_LABEL_W = 50;

static inline int16_t vgSplitControlsX(int16_t halfX, int16_t halfW) {
  const int16_t controlsW = 2 * VG_SPLIT_BTN_W + VG_SPLIT_BTN_GAP;
  return halfX + halfW - UI_PAD_R - controlsW;
}

static void drawStartupMuteUnitsRow(int16_t x, int16_t y, int16_t w) {
  uint16_t bg = dimC(C_CARD_BG);
  display.fillRoundRect(x, y, w, ROW_H, UI_CARD_R, bg);

  const int16_t halfW = (w - VG_SPLIT_HALF_GAP) / 2;
  const int16_t leftX = x;
  const int16_t rightX = x + halfW + VG_SPLIT_HALF_GAP;

  auto drawHalf = [&](int16_t hx, const char* label, bool on, const char* onText, const char* offText, int16_t labelInset = 0) {
    const int16_t oH = ROW_H - UI_CTRL_H_INSET;
    const int16_t oY = y + (ROW_H - oH) / 2;

    const int16_t oX = vgSplitControlsX(hx, halfW);
    const int16_t lblX = hx + UI_PAD_L + labelInset;
    const int16_t lblWRaw = oX - VG_SPLIT_LABEL_GAP - lblX;
    const int16_t lblW = (lblWRaw > VG_SPLIT_MIN_LABEL_W) ? lblWRaw : VG_SPLIT_MIN_LABEL_W;

    uint16_t labelC = dimC(C_LBL);
    AAFont_drawString(AA_XXS, label, lblX, y + ROW_H/2 + 7, labelC, bg, AA_LEFT, lblW);

    uint16_t fOn  = on ? dimC(C_ROW_SEL) : dimC(C_DARK);
    uint16_t fOff = on ? dimC(C_DARK)    : dimC(C_ROW_SEL);
    uint16_t tOn  = on ? dimC((strcmp(onText, "On") == 0) ? C_STATUS_OK : C_VAL_SEL) : dimC(C_LBL);
    uint16_t tOff = on ? dimC(C_LBL) : dimC((strcmp(offText, "Off") == 0) ? C_VAL_SEL : C_VAL_SEL);

    display.fillRoundRect(oX, oY, VG_SPLIT_BTN_W, oH, UI_BTN_R, fOn);
    display.fillRoundRect(oX + VG_SPLIT_BTN_W + VG_SPLIT_BTN_GAP, oY, VG_SPLIT_BTN_W, oH, UI_BTN_R, fOff);
    if (on) display.drawRoundRect(oX, oY, VG_SPLIT_BTN_W, oH, UI_BTN_R, dimC(C_LBL_SEL));
    else    display.drawRoundRect(oX + VG_SPLIT_BTN_W + VG_SPLIT_BTN_GAP, oY, VG_SPLIT_BTN_W, oH, UI_BTN_R, dimC(C_LBL_SEL));

    AAFont_drawString(AA_XXS, onText,  oX, y + ROW_H/2 + 7, tOn,  fOn,  AA_CENTER, VG_SPLIT_BTN_W);
    AAFont_drawString(AA_XXS, offText, oX + VG_SPLIT_BTN_W + VG_SPLIT_BTN_GAP, y + ROW_H/2 + 7, tOff, fOff, AA_CENTER, VG_SPLIT_BTN_W);
  };

  drawHalf(leftX,  "Boot/resume mute", muteOnStartup, "On", "Off");
  drawHalf(rightX, "Units",            volUnitsMode == VOL_UNITS_DB,      "dB", "Step/%", 6);
}

static void drawDualOutRow(int16_t x, int16_t y, int16_t w, bool changed = false) {
  (void)changed;
  uint16_t bg = dimC(C_CARD_BG);
  display.fillRoundRect(x, y, w, ROW_H, UI_CARD_R, bg);

  const int16_t halfW = (w - VG_SPLIT_HALF_GAP) / 2;
  const int16_t leftX = x;
  const int16_t rightX = x + halfW + VG_SPLIT_HALF_GAP;

  auto drawHalf = [&](int16_t hx, const char* label, bool on) {
    const int16_t oH = ROW_H - UI_CTRL_H_INSET;
    const int16_t oY = y + (ROW_H - oH) / 2;

    const int16_t oX = vgSplitControlsX(hx, halfW);
    const int16_t lblX = hx + UI_PAD_L;
    const int16_t lblWRaw = oX - VG_SPLIT_LABEL_GAP - lblX;
    const int16_t lblW = (lblWRaw > VG_SPLIT_MIN_LABEL_W) ? lblWRaw : VG_SPLIT_MIN_LABEL_W;

    AAFont_drawString(AA_XXS, label, lblX, y + ROW_H/2 + 7, dimC(C_LBL), bg, AA_LEFT, lblW);

    uint16_t fOn  = on ? dimC(C_ROW_SEL) : dimC(C_DARK);
    uint16_t fOff = on ? dimC(C_DARK)    : dimC(C_ROW_SEL);
    uint16_t tOn  = on ? dimC(C_VAL_SEL) : dimC(C_LBL);
    uint16_t tOff = on ? dimC(C_LBL) : dimC(C_VAL_SEL);

    display.fillRoundRect(oX, oY, VG_SPLIT_BTN_W, oH, UI_BTN_R, fOn);
    display.fillRoundRect(oX + VG_SPLIT_BTN_W + VG_SPLIT_BTN_GAP, oY, VG_SPLIT_BTN_W, oH, UI_BTN_R, fOff);
    if (on) display.drawRoundRect(oX, oY, VG_SPLIT_BTN_W, oH, UI_BTN_R, dimC(C_LBL_SEL));
    else    display.drawRoundRect(oX + VG_SPLIT_BTN_W + VG_SPLIT_BTN_GAP, oY, VG_SPLIT_BTN_W, oH, UI_BTN_R, dimC(C_LBL_SEL));

    AAFont_drawString(AA_XXS, "-6", oX, y + ROW_H/2 + 7, tOn,  fOn,  AA_CENTER, VG_SPLIT_BTN_W);
    AAFont_drawString(AA_XXS, "0",  oX + VG_SPLIT_BTN_W + VG_SPLIT_BTN_GAP, y + ROW_H/2 + 7, tOff, fOff, AA_CENTER, VG_SPLIT_BTN_W);
  };

  drawHalf(leftX,  "LF Out", gainLFOUT <= -12);
  drawHalf(rightX, "MF Out", gainMFOUT <= -12);
}

// ── fa_dim / FA_WARM forward-declared here so drawLogoScreen can use them ─────
static uint16_t fa_rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}
static uint16_t fa_dim(uint16_t col, uint8_t br) {
  if (br == 255) return col;
  if (br == 0)   return 0;
  uint32_t s = br;
  uint8_t r = (uint8_t)(((col >> 11) & 0x1F) * s / 255);
  uint8_t g = (uint8_t)(((col >>  5) & 0x3F) * s / 255);
  uint8_t b = (uint8_t)(((col >>  0) & 0x1F) * s / 255);
  return (r << 11) | (g << 5) | b;
}
static const uint16_t FA_WARM  = 0xF759u;  // Ivory — matches ACCENT_WARM_WHITE
static const uint16_t FA_AMBER = 0xD481u;  // Amber accent — golf 0 in boot/warmup/mute scherm

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 1 + 2 — Boot / Warmup
// ════════════════════════════════════════════════════════════════════════════
static void drawStandbyText() {
  display.startBuffering();
  display.fillScreen(C_BG);

  bool warm     = isWarmStandbyActive;
  bool keepTrig = warm ? warmTrigRelayClosed : false;

  const char* title      = warm ? "Entering hot standby" : "Entering cold standby";
  const char* powerState = warm ? "Power relay in standby: ON" : "Power relay in standby: OFF";
  const char* trigSetting = warm
    ? (warmTrigRelayClosed ? "Warm trigger: ON" : "Warm trigger: OFF")
    : "Cold trigger: always OFF";
  const char* trigResult  = keepTrig ? "Trigger output in standby: ON" : "Trigger output in standby: OFF";
  const char* muteSetting = muteOnStartup ? "Mute on startup/resume: ON" : "Mute on startup/resume: OFF";

  const int16_t cardW = 620;
  const int16_t cardH = 252;
  const int16_t cardX = (SCREEN_W - cardW) / 2;
  const int16_t cardY = 134;
  display.fillRoundRect(cardX, cardY, cardW, cardH, 8, dimC(C_CARD_BG));
  display.drawRoundRect(cardX, cardY, cardW, cardH, 8, dimC(scale565(C_HAIRLINE, 140)));

  AAFont_drawString(AA_XS, title, 0, cardY + 52,
                   dimC(governedMainAccent()), dimC(C_CARD_BG), AA_CENTER, SCREEN_W);
  display.fillRect(cardX + 32, cardY + 66, cardW - 64, 1, dimC(scale565(C_HAIRLINE, 120)));

  AAFont_drawString(AA_XXS, powerState, 0, cardY + 106,
                   dimC(C_LBL_SEL), dimC(C_CARD_BG), AA_CENTER, SCREEN_W);
  AAFont_drawString(AA_XXS, trigSetting, 0, cardY + 140,
                   dimC(C_GRAY), dimC(C_CARD_BG), AA_CENTER, SCREEN_W);
  AAFont_drawString(AA_XXS, trigResult, 0, cardY + 174,
                   keepTrig ? dimC(C_STATUS_OK) : dimC(C_GRAY_DIM), dimC(C_CARD_BG), AA_CENTER, SCREEN_W);
  AAFont_drawString(AA_XXS, muteSetting, 0, cardY + 208,
                   muteOnStartup ? dimC(C_STATUS_OK) : dimC(C_GRAY_DIM), dimC(C_CARD_BG), AA_CENTER, SCREEN_W);
  display.endBuffering();
}

// ════════════════════════════════════════════════════════════════════════════
//  Boot/Warmup scherm — live rendered
//
//  Layout (800×480):
//    Logo    : PHI_LOGO_GRAY 320×320 → 118×118 px, center x=400 cy=142
//    Tekst   : "PHI REFERENCE" Orbitron48 @1:1, baseline y=291, gecentreerd
//    Hairline: y=303, 390 px breed, gecentreerd
//    Subtitel: "PRE-10" Orbitron48 @9/16, baseline y=345, gecentreerd
//    Fill    : gradient y=195..wave_y, fade L/R over 170 px
//    Wave    : sin() freq=0.016 rad/px, amp=13, baseline y=400, 2 px dik
//    Status  : subText rechtsonder, Orbitron48 @5/16
//    Vignette: top 55 px en bottom 55 px
//
//  boot_bitmap.h is niet meer nodig — include mag verwijderd worden.
// ════════════════════════════════════════════════════════════════════════════

// Interne helper: grijswaarde 0-255 naar RGB565
static inline uint16_t grayRGB565(uint8_t lv) {
  return ((uint16_t)(lv >> 3) << 11) | ((uint16_t)(lv >> 2) << 5) | (lv >> 3);
}

// Logo schalen 320→destSz px, nearest-neighbor, zwarte pixels transparant
static void drawPhiLogo(int16_t destX, int16_t destY, int16_t destSz) {
  const int16_t SRC = PHI_LOGO_GRAY_W;  // 320
  display.startWrite();
  for (int16_t dy = 0; dy < destSz; dy++) {
    int16_t sy = (int32_t)dy * SRC / destSz;
    for (int16_t dx = 0; dx < destSz; dx++) {
      int16_t  sx = (int32_t)dx * SRC / destSz;
      uint16_t px = pgm_read_word(&PHI_LOGO_GRAY[(uint32_t)sy * SRC + sx]);
      if (px == 0x0000) continue;  // zwart → transparant, skip
      display.drawPixel(destX + dx, destY + dy, px);
    }
  }
  display.endWrite();
}

static void drawLogoScreen(const char* subText) {
  // ── Layout (800×480) ──────────────────────────────────────────────────────
  //   Logo    : 150×150 px, center x=400 cy=132
  //   PHI REF : Orbitron48 @1:1,   baseline y=289
  //   Hairline: y=301, 390 px breed
  //   PRE-10  : Orbitron48 @11/16, baseline y=352
  //   Fill    : y=220..wave_y, fade L/R over 160 px, max alpha≈50%
  //   Wave    : sin() freq=0.016, amp=13, baseline y=432, 2 px dik
  //   Status  : rechtsonder Orbitron48 @5/16
  //   Vignette: top 62 px zwart, bottom zachte fade y=455..480
  // ─────────────────────────────────────────────────────────────────────────
  static const int16_t LCX       = SCREEN_W / 2;   // 400
  static const int16_t LOGO_SZ   = 150;
  static const int16_t LOGO_CY   = 132;
  static const int16_t LOGO_X    = LCX - LOGO_SZ / 2;       // 325
  static const int16_t LOGO_Y    = LOGO_CY - LOGO_SZ / 2;   // 57
  static const int16_t ORB_YADV  = 60;
  static const int16_t TXT_BASE  = LOGO_CY + LOGO_SZ / 2 + 22 + ORB_YADV;  // 289
  static const int16_t HL_Y      = TXT_BASE + 12;    // 301
  static const int16_t HL_HALF   = 195;              // 390 px breed / 2
  static const uint8_t SUB_SCALE = 11;               // 11/16 ≈ 0.6875x
  static const int16_t SUB_BASE  = HL_Y + 10 + (int16_t)(ORB_YADV * SUB_SCALE / 16);  // 352
  static const uint8_t STAT_SCALE = 5;               // 5/16 ≈ 0.3125x
  static const int16_t STAT_BASE  = SCREEN_H - 28;
  static const int16_t WAVE_Y    = 432;
  static const int16_t WAVE_AMP  = 13;
  static const int16_t FILL_TOP  = 220;
  static const int16_t FADE_W    = 160;
  static const int16_t FILL_RNG  = WAVE_Y - FILL_TOP;   // 212
  static const int16_t VIG_TOP_H = 62;
  static const int16_t VIG_BOT_Y = 455;

  display.startBuffering();
  display.fillScreen(C_BG);

  // ── 1. Sinusoïde fill + lijn ─────────────────────────────────────────────
  display.startWrite();
  for (int16_t x = 0; x < SCREEN_W; x++) {
    int16_t edge  = x < FADE_W ? x : (x > SCREEN_W - 1 - FADE_W ? SCREEN_W - 1 - x : FADE_W);
    uint8_t env_h = (edge >= FADE_W) ? 255 : (uint8_t)((uint32_t)edge * 255 / FADE_W);
    if (env_h == 0) continue;

    int16_t wave_y = WAVE_Y + (int16_t)(sinf(x * 0.016f) * WAVE_AMP);
    int16_t bottom = (wave_y < SCREEN_H - 1) ? wave_y : SCREEN_H - 2;

    for (int16_t y = FILL_TOP; y <= bottom; y++) {
      uint32_t dy = (uint32_t)(y - FILL_TOP);
      uint8_t  lv = (uint8_t)(dy * 88 / FILL_RNG * env_h / 255);  // max lv≈88 (50%)
      if (lv == 0) continue;
      display.drawPixel(x, y, grayRGB565(lv));
    }

    uint8_t  wlv  = (uint8_t)((uint32_t)210 * env_h / 255);
    uint16_t wcol = grayRGB565(wlv);
    if (wave_y >= 0 && wave_y < SCREEN_H)          display.drawPixel(x, wave_y,     wcol);
    if (wave_y + 1 >= 0 && wave_y + 1 < SCREEN_H)  display.drawPixel(x, wave_y + 1, wcol);
  }
  display.endWrite();

  // ── 2. Vignette top — zwart blok ─────────────────────────────────────────
  display.fillRect(0, 0, SCREEN_W, VIG_TOP_H, C_BG);

  // ── 3. Vignette bottom — zachte kwadratische fade naar zwart ─────────────
  // Wave bottom ≈ y=447, vignette start op 455 — geen overlap
  for (int16_t y = VIG_BOT_Y; y < SCREEN_H; y++) {
    uint32_t t  = (uint32_t)(y - VIG_BOT_Y) * 255 / (SCREEN_H - VIG_BOT_Y);
    uint8_t  lv = (uint8_t)(t * t / 255);
    // Teken zwarte lijn met toenemende dekking (mix richting zwart)
    // Omdat we niet kunnen lezen: overschrijf rijen steeds dichter bij zwart
    // Boven drempel: laat fill doorschijnen; bij lv>200 volledig zwart
    if (lv > 200) display.drawFastHLine(0, y, SCREEN_W, C_BG);
  }

  // ── 4. Logo ──────────────────────────────────────────────────────────────
  drawPhiLogo(LOGO_X, LOGO_Y, LOGO_SZ);

  // ── 5. "PHI REFERENCE" Orbitron48 @1:1, gecentreerd ─────────────────────
  {
    int16_t tw = AAFont_stringWidthScaled(&Orbitron48AA, "PHI REFERENCE", 16);
    AAFont_drawStringScaled(&Orbitron48AA, "PHI REFERENCE",
                            LCX - tw / 2, TXT_BASE,
                            0xBEF7u, C_BG, 16);
  }

  // ── 6. Hairline ───────────────────────────────────────────────────────────
  display.drawFastHLine(LCX - HL_HALF, HL_Y, HL_HALF * 2, 0x2945u);

  // ── 7. "PRE-10" Orbitron48 @11/16, gecentreerd ───────────────────────────
  {
    int16_t tw = AAFont_stringWidthScaled(&Orbitron48AA, "PRE-10", SUB_SCALE);
    AAFont_drawStringScaled(&Orbitron48AA, "PRE-10",
                            LCX - tw / 2, SUB_BASE,
                            0x9CF3u, C_BG, SUB_SCALE);
  }

  // ── 8. Status rechtsonder ("Initializing" / "Warming up") ────────────────
  {
    int16_t tw = AAFont_stringWidthScaled(&Orbitron48AA, subText, STAT_SCALE);
    AAFont_drawStringScaled(&Orbitron48AA, subText,
                            SCREEN_W - 36 - tw, STAT_BASE,
                            0x39C7u, C_BG, STAT_SCALE);
  }

  display.endBuffering();
}

static void drawBootScreen()   { drawLogoScreen("Initializing"); }
static void drawWarmupScreen() { drawLogoScreen("Warming up"); }
// drawMuteScreen verwijderd — mute toont nu tekst op hoofdscherm

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 3 — Hoofdscherm
// ════════════════════════════════════════════════════════════════════════════
uint8_t  detailMode      = 1;     // 0=Uit 1=Aan (simpel panel)
bool     detailVisibleOnDim = true; // Panel zichtbaar houden bij auto-dim
bool     detailColorFollow = false; // Detail panel volgt accentkleur in theme menu
#define FONT_STANDARD  0
#define FONT_MATRIX    1
#define FONT_ORBITRON  2
uint8_t  mainFontMode = FONT_STANDARD;  // 0=standard, 1=matrix, 2=orbitron
static constexpr int16_t MAIN_MATRIX_NAME_SHIFT_UP_PX  = 8;  // ~1mm omhoog (2mm lager dan voorheen)
static constexpr int16_t MAIN_MATRIX_VALUE_SHIFT_UP_PX = 24; // behoud bestaande value-positie
static bool detailPanelSuppressedByDim = false;  // Runtime suppressie bij auto-dim

static inline bool detailPanelActive() {
  return detailMode > 0 && !detailPanelSuppressedByDim;
}

static inline bool largeVolumeFontActive() {
  return largeFontOnDim && !detailPanelActive();
}
// detailPanelSimple() verwijderd — alleen simpel panel bestaat nog
static uint32_t balShowMs      = 0;      // Timestamp balance bar zichtbaar
#define BAL_VISIBLE_MS  3000             // Balance bar zichtbaar voor 3 sec
static bool     mcpLinkOk      = true;   // MCP23017 I2C link status
static uint32_t diagLastRefreshMs = 0;  // Voor zichtbare refresh-feedback in diagnostics

void notifyActivity() {
  uint32_t now = millis();
  lastActivityMs = now;
  touchLockoutMs = now; touchLockoutDur = 500;  // 500ms lockout na elke activiteit
  if (mainCalmMode) mainCalmMode = false;

  bool needPanelRestore = detailPanelSuppressedByDim;
  detailPanelSuppressedByDim = false;
  deepDimActive = false;
  backlightOffActive = false;
  fadeActive = false;  // Annuleer lopende dim-fade zodat het scherm niet terugvalt

  if (screenBrightness != activeBrightness()) {
    // Scherm was gedimmed: brightness EERST omhoog zodat dimC() de juiste
    // kleurwaarden berekent bij hertekening.
    fingerDown = false;
    touchLockoutMs = millis(); touchLockoutDur = 300;
    clearEncoderCounts();  // Wis opgeslagen encoder-ruis van de dim-periode
    setScreenBrightness(activeBrightness());
    if (currentScreen == SCR_MAIN) drawMainScreen();
    if (!inStandby) {
      applyVolume();
      applyGainAll();
    }
  } else if (needPanelRestore && currentScreen == SCR_MAIN && detailMode > 0) {
    drawSimpleDetailPanel();
  }
}

void notifyActivityNoWake() {
  // Gebruik bij input-switch afronding: timer resetten zonder geforceerde wake.
  // mainCalmMode NIET resetten — dat doet alleen notifyActivity() bij echte wake.
  lastActivityMs = millis();
}

void notifyActivityKeepDimTimer() {
  // Calm wake via encoder: scherm hertekenen maar lastActivityMs NIET resetten.
  // Dim-timer loopt ongestoord door zodat dim na calm gewoon optreedt.
  if (mainCalmMode) {
    // Na wake uit calm eerst een korte touch-lockout:
    // voorkomt dat een eerder (genegeerde) touch op het gedimde hoofdscherm
    // direct als menu-tap wordt verwerkt zodra calm uitgaat.
    touchLockoutMs = millis(); touchLockoutDur = 350;

    // Tijdens calm werd touch niet gepolled; reset de state om spook-presses
    // of "vaste" fingerDown-status na wake te voorkomen.
    fingerDown = false;

    mainCalmMode = false;
    mainCalmBlend = 0;
    // fontScaleBlend en panelBlend niet hard resetten — animatieloop brengt ze
    // vloeiend terug naar target (0 resp. 255) zodra suppress opgeheven is.
    if (currentScreen == SCR_MAIN) {
      display.startBuffering();
      drawMainStatusChips();
      drawInputName();
      drawMainPrimaryValue();
      display.endBuffering();
    }
  }
}

void notifyTouch() {
  // Touchbar removed — touch simply resets the activity/dim timer
  notifyActivity();
}

static void clearMainNameArea() {
  // Ruime band: dekt ascenders/descenders en alle mogelijke inputnaamlengtes.
  display.fillRect(0, 36, SCREEN_W, 142, C_BG);
}

void showSwitchingScreen(const char* newName) {
  // Geen fillScreen — detail panel en rij 2 blijven staan.
  // Alleen de inputnaam-zone en volumezone worden gewist en herschreven.

  // Inputnaam-zone: alleen de naamtekstband wissen (minder flicker)
  clearMainNameArea();
  strncpy(switchTargetName, newName, sizeof(switchTargetName)-1);
  switchTargetName[sizeof(switchTargetName)-1] = 0;
  if (mainFontMode == FONT_MATRIX) drawDotMatrixString(AA_MED, switchTargetName, 0, 120 - MAIN_MATRIX_NAME_SHIFT_UP_PX, C_GRAY_DIM, AA_CENTER, SCREEN_W, 9, 3, 6);
  else AAFont_drawString(AA_MED, switchTargetName, 0, 106, dimC(C_GRAY_DIM), C_BG, AA_CENTER, SCREEN_W);
  drawMainHairline();

  // Volumezone: y=173..DP_TOP
  display.fillRect(0, 173, SCREEN_W, DP_TOP - 173, C_BG);
  AAFont_drawStringScaled(AA_VOL, "\xe2\x80\x94", 0, 300, dimC(C_GRAY_DIM), C_BG, 24, AA_CENTER, SCREEN_W);
}


static inline char toDotMatrixChar(char c, char nextC) {
  // Alle letters (hoofd- en kleine) worden direct getekend via de glyph tabel.
  // Uitzondering: '~' is de pseudo lowercase 'd' voor de "dB" eenheid —
  // die blijft als interne code voor de speciale 'd' glyph.
  (void)nextC;
  return c;
}

struct DotGlyph5x7 {
  char c;
  uint8_t rows[7]; // 5 low bits used (bit4 = leftmost pixel)
};

static const DotGlyph5x7 DOT_GLYPHS_5X7[] = {
  {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
  {'-', {0x00,0x00,0x00,0x0E,0x00,0x00,0x00}},
  {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
  {'/', {0x01,0x02,0x04,0x08,0x10,0x00,0x00}},
  {'+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
  {':', {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}},
  {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
  {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
  {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
  {'3', {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
  {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
  {'5', {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}},
  {'6', {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}},
  {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
  {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
  {'9', {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}},
  {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
  {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
  {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
  {'D', {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
  {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
  {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
  {'G', {0x0E,0x11,0x10,0x13,0x11,0x11,0x0E}},
  {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
  {'I', {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}},
  {'J', {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}},
  {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
  {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
  {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
  {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
  {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
  {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
  {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
  {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
  {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
  {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
  {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
  {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
  {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
  {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
  {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
  {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
  {'a', {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}},
  {'b', {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}},
  {'c', {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E}},
  {'d', {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}},
  {'e', {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}},
  {'f', {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}},
  {'g', {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}},
  {'h', {0x10,0x10,0x16,0x19,0x11,0x11,0x11}},
  {'i', {0x00,0x04,0x00,0x0C,0x04,0x04,0x0E}},
  {'j', {0x00,0x02,0x00,0x06,0x02,0x12,0x0C}},
  {'k', {0x10,0x10,0x12,0x14,0x18,0x14,0x12}},
  {'l', {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}},
  {'m', {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}},
  {'n', {0x00,0x00,0x16,0x19,0x11,0x11,0x11}},
  {'o', {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}},
  {'p', {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}},
  {'q', {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}},
  {'r', {0x00,0x00,0x16,0x19,0x10,0x10,0x10}},
  {'s', {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}},
  {'t', {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}},
  {'u', {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}},
  {'v', {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}},
  {'w', {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}},
  {'x', {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}},
  {'y', {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}},
  {'z', {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}},
};

static const uint8_t* glyph5x7(char c, char nextC) {
  c = toDotMatrixChar(c, nextC);
  for (size_t i = 0; i < sizeof(DOT_GLYPHS_5X7)/sizeof(DOT_GLYPHS_5X7[0]); ++i) {
    if (DOT_GLYPHS_5X7[i].c == c) return DOT_GLYPHS_5X7[i].rows;
  }
  return DOT_GLYPHS_5X7[0].rows; // space fallback
}

static inline int16_t dotMatrixWidth(const char* str, uint8_t pitch, uint8_t gap) {
  if (!str || !*str) return 0;
  int16_t n = 0;
  for (const char* p = str; *p; ++p) ++n;
  return n * (5 * pitch) + (n - 1) * gap;
}

static inline uint8_t safeCoreRadius(uint8_t pitch, uint8_t dotR) {
  // Voorkom overlap/touching: houd minimaal 1 px spatie binnen de pitch.
  // Daardoor ogen dots net iets kleiner en blijven lijnen losser.
  uint8_t maxR = (pitch > 2) ? (uint8_t)((pitch - 2) / 2) : 0;
  if (maxR == 0) return 0;
  return (dotR < maxR) ? dotR : maxR;
}

static inline void drawMatrixDot(int16_t px, int16_t py, uint16_t coreCol, uint16_t glowCol,
                                 uint8_t pitch, uint8_t dotR) {
  if (!_aaDisplay) return;
  uint8_t coreR = safeCoreRadius(pitch, dotR);

  if (coreR == 0) {
    _aaDisplay->drawPixel(px, py, coreCol);
    return;
  }

  // Zachte rand: één pixel buitenring in glow, gevulde kern in coreCol.
  // Geen halo-ring, geen axiale pixels, geen highlight — uniform en clean.
  _aaDisplay->fillCircle(px, py, coreR, coreCol);
  _aaDisplay->drawCircle(px, py, coreR, glowCol);
}

static void drawDotMatrixString(const AAFont* /*font*/, const char* str,
                                int16_t x, int16_t baseline,
                                uint16_t fgColor, AAAlign align = AA_LEFT, int16_t areaW = 0,
                                uint8_t pitch = 3, uint8_t dotR = 1, uint8_t charGap = 1) {
  if (!str || !_aaDisplay) return;
  int16_t startX = x;
  int16_t tw = dotMatrixWidth(str, pitch, charGap);
  if (align == AA_CENTER || align == AA_RIGHT) {
    if (align == AA_CENTER) startX = x + (areaW - tw) / 2;
    else                    startX = x + areaW - tw;
  }

  const int16_t topY = baseline - (6 * pitch); // row 6 valt exact op baseline
  int16_t curX = startX;
  uint16_t coreCol = dimC(scale565(fgColor, 100));
  uint16_t glowCol  = dimC(scale565(fgColor,  62));

  _aaDisplay->startWrite();

  for (const char* p = str; *p; ++p) {
    const uint8_t* rows = glyph5x7(*p, *(p + 1));
    for (uint8_t ry = 0; ry < 7; ++ry) {
      uint8_t rowBits = rows[ry] & 0x1F;
      for (uint8_t rx = 0; rx < 5; ++rx) {
        if (!(rowBits & (1u << (4 - rx)))) continue;
        int16_t px = curX + rx * pitch;
        int16_t py = topY + ry * pitch;
        drawMatrixDot(px, py, coreCol, glowCol, pitch, dotR);
      }
    }
    curX += (5 * pitch + charGap);
  }

  _aaDisplay->endWrite();
}

static void drawDotMatrixStringScaled(const AAFont* /*font*/, const char* str,
                                      int16_t x, int16_t baseline,
                                      uint16_t fgColor, AAAlign align, int16_t areaW,
                                      uint8_t scale_x16,
                                      uint8_t pitch = 4, uint8_t dotR = 1, uint8_t charGap = 2) {
  (void)scale_x16;
  drawDotMatrixString(nullptr, str, x, baseline, fgColor, align, areaW, pitch, dotR, charGap);
}

static void drawInputName() {
  // Animatievariant: geen fillRect. De AA-renderer overschrijft pixels direct
  // op C_BG waardoor er geen flicker is per calmBlend-frame.
  uint8_t namePct = (uint8_t)(62 + 38U * (255U - mainCalmBlend) / 255U);
  uint16_t nameColor = scale565(governedMainAccent(), namePct);
  if (mainFontMode == FONT_MATRIX) drawDotMatrixString(AA_MED, inputNames[currentInput], 0, 120 - MAIN_MATRIX_NAME_SHIFT_UP_PX, nameColor, AA_CENTER, SCREEN_W, 9, 3, 6);
  else if (mainFontMode == FONT_ORBITRON) AAFont_drawStringScaled(&Orbitron48AA, inputNames[currentInput], 0, 106, dimC(nameColor), C_BG, 20, AA_CENTER, SCREEN_W);
  else AAFont_drawString(AA_MED, inputNames[currentInput], 0, 106, dimC(nameColor), C_BG, AA_CENTER, SCREEN_W);
}

static void clearAndDrawInputName() {
  // Volledige clear + herteken. Gebruik bij inputwissel en volledige schermopbouw
  // zodat restanten van de vorige naam volledig worden gewist.
  clearMainNameArea();
  uint8_t namePct = (uint8_t)(62 + 38U * (255U - mainCalmBlend) / 255U);
  uint16_t nameColor = scale565(governedMainAccent(), namePct);
  if (mainFontMode == FONT_MATRIX) drawDotMatrixString(AA_MED, inputNames[currentInput], 0, 120 - MAIN_MATRIX_NAME_SHIFT_UP_PX, nameColor, AA_CENTER, SCREEN_W, 9, 3, 6);
  else if (mainFontMode == FONT_ORBITRON) AAFont_drawStringScaled(&Orbitron48AA, inputNames[currentInput], 0, 106, dimC(nameColor), C_BG, 20, AA_CENTER, SCREEN_W);
  else AAFont_drawString(AA_MED, inputNames[currentInput], 0, 106, dimC(nameColor), C_BG, AA_CENTER, SCREEN_W);
}

static void formatVolStr(char* buf, uint8_t vol) {
  if (vol == VOL_OFF) { strcpy(buf, "Off"); return; }
  if (volUnitsMode == VOL_UNITS_DB) {
    float dB = (vol * 0.5f) - 115.5f;
    sprintf(buf, "%.1f dB", dB);
  } else if (volUnitsMode == VOL_UNITS_PERCENT) {
    uint16_t pct = (uint16_t)(((uint32_t)vol * 100U + 127U) / 255U);
    sprintf(buf, "%u%%", (unsigned)pct);
  } else {
    sprintf(buf, "%u", (unsigned)vol);
  }
}


// Berekent pixelbreedte van een volumestring in AA_VOL scaled 1.5x
// op basis van de bekende xAdvance waarden (48pt, scale=24/16=1.5)
static int16_t volStrWidthPx(const char* s) {
  static const uint8_t xAdv[] = {
    // index = c - 0x20, Artifakt 48pt xAdvance waarden
    25,0,0,0,0,0,0,0,0,0,0,43,0,40,25,0, // sp ! " # $ % & ' ( ) * + , - . /
    59,44,49,54,55,53,55,47,58,54         // 0-9
  };
  int16_t w = 0;
  for (const char* p = s; *p; p++) {
    uint8_t idx = (uint8_t)(*p) - 0x20;
    uint8_t xa = (idx < sizeof(xAdv)) ? xAdv[idx] : 50;
    w += (int16_t)(xa * 3 / 2);  // scale 1.5x (24/16)
  }
  return w;
}

// Breedte berekening voor Orbitron @ scale_x16=32 (2x)
static int16_t volStrWidthPxOrbitron(const char* s) {
  static const uint8_t xAdv[] = {
    // index = c - 0x20, Orbitron 48pt xAdvance waarden
    12,0,0,0,0,0,0,0,0,0,0,0,0,25,11,0, // sp ! " # $ % & ' ( ) * + , - . /
    40,19,40,40,35,40,39,32,40,40        // 0-9
  };
  int16_t w = 0;
  for (const char* p = s; *p; p++) {
    uint8_t idx = (uint8_t)(*p) - 0x20;
    uint8_t xa = (idx < sizeof(xAdv)) ? xAdv[idx] : 40;
    w += (int16_t)(xa * 40 / 16);  // scale 2.5x (40/16)
  }
  return w;
}

static void formatVolNumStr(char* buf, uint8_t vol) {
  if (vol == VOL_OFF) { strcpy(buf, "Off"); return; }
  float dB = (vol * 0.5f) - 115.5f;
  sprintf(buf, "%.1f", dB);
}

static void clearPrimaryValueZone() {
  int16_t zoneBot = detailPanelActive() ? DP_TOP : SCREEN_H;
  display.fillRect(0, 155, SCREEN_W, zoneBot - 155, C_BG);
}

// Wist volumezone + panelzone — gebruikt door crossfade
static void clearMainContentZone() {
  display.fillRect(0, 155, SCREEN_W, SCREEN_H - 155, C_BG);
}

static void drawMainPrimaryValue() {
  // Animatievariant: geen fillRect. De AA-renderer overschrijft pixels direct
  // op C_BG waardoor er geen flicker is per calmBlend-frame.
  // Roep clearPrimaryValueZone() aan voor een echte waardewisseling
  // (mute toggle, bypass, volumesprong bij inputwissel).
  //
  // Schaal en positie op basis van fontIsLarge (binair, crossfade regelt de overgang).
  const bool    useLarge = fontIsLarge;
  const int16_t VAL_Y    = (useLarge ? 340 : 310) - MAIN_MATRIX_VALUE_SHIFT_UP_PX;
  const uint8_t scaleStd = useLarge ? 30 : 24;
  const uint8_t scaleOrb = useLarge ? 50 : 40;
  const uint8_t dotPitch = useLarge ? 17 : 14;
  const uint8_t dotR     = useLarge ?  6 :  5;
  const uint8_t dotGap   = useLarge ? 10 :  8;
  // Tijdens crossfade fadet de volume-alpha mee met panelBlend
  #define VOL_ALPHA(c) (xfadeState != XF_IDLE ? alpha565((c), panelBlend) : (c))

  if (mainFontMode != FONT_STANDARD) clearPrimaryValueZone();

  if (currentVolume == VOL_OFF && !isMuted) {
    uint16_t c = VOL_ALPHA(governedMainAccent());
    if      (mainFontMode == FONT_MATRIX)   drawDotMatrixStringScaled(AA_VOL, "Off", 0, VAL_Y, c, AA_CENTER, SCREEN_W, scaleStd, dotPitch, dotR, dotGap);
    else if (mainFontMode == FONT_ORBITRON) AAFont_drawStringScaled(&Orbitron48AA, "Off", 0, VAL_Y, VOL_ALPHA(dimC(governedMainAccent())), C_BG, scaleOrb, AA_CENTER, SCREEN_W);
    else                                    AAFont_drawStringScaled(AA_VOL, "Off", 0, VAL_Y, VOL_ALPHA(dimC(governedMainAccent())), C_BG, scaleStd, AA_CENTER, SCREEN_W);
  } else if (isMuted) {
    uint16_t c = VOL_ALPHA(governedMainAccent());
    if      (mainFontMode == FONT_MATRIX)   drawDotMatrixStringScaled(AA_VOL, "Mute", 0, VAL_Y, c, AA_CENTER, SCREEN_W, scaleStd, dotPitch, dotR, dotGap);
    else if (mainFontMode == FONT_ORBITRON) AAFont_drawStringScaled(&Orbitron48AA, "Mute", 0, VAL_Y, VOL_ALPHA(dimC(governedMainAccent())), C_BG, scaleOrb, AA_CENTER, SCREEN_W);
    else                                    AAFont_drawStringScaled(AA_VOL, "Mute", 0, VAL_Y, VOL_ALPHA(dimC(governedMainAccent())), C_BG, scaleStd, AA_CENTER, SCREEN_W);
  } else if (currentInput == surroundInput) {
    uint16_t c = VOL_ALPHA(governedMainAccent());
    if      (mainFontMode == FONT_MATRIX)   drawDotMatrixStringScaled(AA_VOL, "Bypass", 0, VAL_Y, c, AA_CENTER, SCREEN_W, scaleStd, dotPitch, dotR, dotGap);
    else if (mainFontMode == FONT_ORBITRON) AAFont_drawStringScaled(&Orbitron48AA, "Bypass", 0, VAL_Y, VOL_ALPHA(dimC(governedMainAccent())), C_BG, scaleOrb, AA_CENTER, SCREEN_W);
    else                                    AAFont_drawStringScaled(AA_VOL, "Bypass", 0, VAL_Y, VOL_ALPHA(dimC(governedMainAccent())), C_BG, scaleStd, AA_CENTER, SCREEN_W);
  } else {
    uint8_t  volPct = (uint8_t)(55 + 45U * (255U - mainCalmBlend) / 255U);
    uint16_t vCol   = VOL_ALPHA(scale565(volColor(), volPct));

    if (mainFontMode == FONT_MATRIX) {
      char vs[16]; formatVolStr(vs, currentVolume);
      drawDotMatrixStringScaled(AA_VOL, vs, 0, VAL_Y, vCol, AA_CENTER, SCREEN_W, scaleStd, dotPitch, dotR, dotGap);
    } else if (mainFontMode == FONT_ORBITRON && volUnitsMode == VOL_UNITS_DB) {
      char vs[16]; formatVolNumStr(vs, currentVolume);
      const int16_t DB_GAP = useLarge ? 30 : 24;
      const int16_t DB_W   = useLarge ? 75 : 60;
      int16_t numW = AAFont_stringWidthScaled(&Orbitron48AA, vs, scaleOrb);
      int16_t numX = (SCREEN_W - (numW + DB_GAP + DB_W)) / 2;
      AAFont_drawStringScaled(&Orbitron48AA, vs, numX, VAL_Y, VOL_ALPHA(dimC(vCol)), C_BG, scaleOrb, AA_LEFT, numW + 4);
      uint8_t dbPct = (uint8_t)(38 + 22U * (255U - mainCalmBlend) / 255U);
      AAFont_drawString(AA_SM, "dB", numX + numW + DB_GAP, VAL_Y, VOL_ALPHA(dimC(scale565(volColor(), dbPct))), C_BG, AA_LEFT, DB_W);
    } else if (mainFontMode == FONT_ORBITRON) {
      char vs[16]; formatVolStr(vs, currentVolume);
      AAFont_drawStringScaled(&Orbitron48AA, vs, 0, VAL_Y, VOL_ALPHA(dimC(vCol)), C_BG, scaleOrb, AA_CENTER, SCREEN_W);
    } else if (volUnitsMode == VOL_UNITS_DB) {
      char vs[16]; formatVolNumStr(vs, currentVolume);
      const int16_t DB_GAP = useLarge ? 22 : 18;
      const int16_t DB_W   = useLarge ? 65 : 52;
      int16_t numW = AAFont_stringWidthScaled(AA_VOL, vs, scaleStd);
      int16_t numX = (SCREEN_W - (numW + DB_GAP + DB_W)) / 2;
      AAFont_drawStringScaled(AA_VOL, vs, numX, VAL_Y, VOL_ALPHA(dimC(vCol)), C_BG, scaleStd, AA_LEFT, numW + 4);
      uint8_t dbPct = (uint8_t)(38 + 22U * (255U - mainCalmBlend) / 255U);
      AAFont_drawString(AA_SM, "dB", numX + numW + DB_GAP, VAL_Y, VOL_ALPHA(dimC(scale565(volColor(), dbPct))), C_BG, AA_LEFT, DB_W);
    } else {
      char vs[16]; formatVolStr(vs, currentVolume);
      AAFont_drawStringScaled(AA_VOL, vs, 0, VAL_Y, VOL_ALPHA(dimC(vCol)), C_BG, scaleStd, AA_CENTER, SCREEN_W);
    }
  }
  #undef VOL_ALPHA
}



// ── Balance bar overlay ───────────────────────────────────────────────────────
// Vervangt tijdelijk de volumezone (y=173..DP_TOP). Inputnaam en detailpanel
// blijven onaangeroerd. Cirkel animeert met ease-out per draw-aanroep.
//
// Layout:
//   As:        gecentreerd in volumezone op BAL_CY
//   Ticks:     center (0 dB) prominenter dan +-3/+-6 dB
//   dB-labels: onder de ticks
//   L / R:     aan de uiteinden van de balk
//   Cirkel:    beweegt met ease-out naar balanceOffset positie
//   Gevuld segment: center naar cirkel in accent (gedimmd)

#define BAL_BAR_W    620
#define BAL_BAR_X    ((SCREEN_W - BAL_BAR_W) / 2)
#define BAL_VOL_Y    173
#define BAL_VOL_BOT  DP_TOP

// As iets boven het midden van de zone — geeft ruimte aan labels onder de as
// en zorgt voor goede visuele balans tussen inputnaam en detailpanel.
// Zone = y=173..385 (212px). As op y=245 geeft 72px boven (voor ticks),
// 140px onder (voor labels, L/R en ademruimte naar detailpanel).
#define BAL_CY       245

// Segmented bar — 49 segmenten: 24 links + center + 24 rechts = 1 segment per 0.5dB stap
#define BAL_N_SEG       49
#define BAL_SEG_GAP      2
#define BAL_SEG_W       ((BAL_BAR_W - (BAL_N_SEG-1)*BAL_SEG_GAP) / BAL_N_SEG)  // ~11px
#define BAL_SEG_H_NORM  14   // hoogte normale segmenten (boven + onder as)
#define BAL_SEG_H_CTR   20   // center-segment iets hoger
#define BAL_AS_H         4   // as-lijn hoogte
#define BAL_IND_OVER     6   // indicator steekt boven/onder segmenten uit

// Geometrie t.o.v. BAL_CY (as-midden)
#define BAL_SEG_TOP_Y   (BAL_CY - BAL_AS_H/2 - BAL_SEG_H_CTR)
#define BAL_SEG_BOT_BOT (BAL_CY + BAL_AS_H/2 + BAL_SEG_H_CTR)

// Werkelijke breedte gesegmenteerde balk — voorkomt dat de as-lijn verder loopt dan de segmenten
#define BAL_ACTUAL_W  (BAL_N_SEG * BAL_SEG_W + (BAL_N_SEG - 1) * BAL_SEG_GAP)
#define BAL_WIPE_TOP    (BAL_SEG_TOP_Y - BAL_IND_OVER - 2)
#define BAL_WIPE_H      (BAL_SEG_BOT_BOT - BAL_SEG_TOP_Y + BAL_IND_OVER*2 + 4)

// Kleuren — afgeleid van accent() at runtime, hoog contrast
// Vaste define alleen voor mute-stop rood en indicator wit
#define BAL_SEG_MUTE    0x8800u   // rood-oranje bij mute-stop
#define BAL_IND_COL     0xFFFFu   // indicator: wit voor max contrast

// Runtime kleurhulpfuncties (inline, aanroepen in draw-functies)
static inline uint16_t balColEmpty()  { return dimC(scale565(VGC_BDR_NORM, 140)); }  // zichtbaar track
static inline uint16_t balColCtr()    { return dimC(scale565(accent(), 60)); }         // center: duidelijk accent
static inline uint16_t balColFill()   { return dimC(scale565(accent(), 96)); }         // gevuld: helder accent

static float balDisplayX = -1.0f;

static int16_t balTargetX() {
  // Clamp mute-eindstop (±BALANCE_MAX+1) naar de balkrand
  int8_t clamped = (int8_t)constrain(balanceOffset, -BALANCE_MAX, BALANCE_MAX);
  return (int16_t)(BAL_BAR_X + (clamped + BALANCE_MAX) * BAL_ACTUAL_W / (BALANCE_MAX * 2));
}

static int16_t balCenterX() {
  return (int16_t)(BAL_BAR_X + BAL_ACTUAL_W / 2);
}

static void drawBalanceStatic() {
  for (int16_t i = 0; i < BAL_N_SEG; i++) {
    int16_t sx   = BAL_BAR_X + i * (BAL_SEG_W + BAL_SEG_GAP);
    bool    isCtr = (i == BAL_N_SEG / 2);
    int16_t sh   = isCtr ? BAL_SEG_H_CTR : BAL_SEG_H_NORM;
    uint16_t sc  = isCtr ? balColCtr() : balColEmpty();
    display.fillRoundRect(sx, BAL_CY - BAL_AS_H/2 - sh, BAL_SEG_W, sh, 2, sc);
    display.fillRoundRect(sx, BAL_CY + BAL_AS_H/2,       BAL_SEG_W, sh, 2, sc);
  }
  display.fillRect(BAL_BAR_X, BAL_CY - BAL_AS_H/2, BAL_ACTUAL_W, BAL_AS_H, balColEmpty());
}

static void drawBalanceIndicator(int16_t cx) {
  // Wipe — volledig opnieuw tekenen
  display.fillRect(BAL_BAR_X, BAL_WIPE_TOP, BAL_BAR_W, BAL_WIPE_H, C_BG);

  bool isMuteStop = (balanceOffset <= -(BALANCE_MAX + 1)) || (balanceOffset >= (BALANCE_MAX + 1));
  bool inCenter   = (balanceOffset == 0);

  // Segment index direct van balanceOffset
  // Segment 0 = offset -BALANCE_MAX, segment ctrSeg = offset 0, segment N-1 = offset +BALANCE_MAX
  int16_t ctrSeg    = BAL_N_SEG / 2;  // 24
  int16_t clampedOff = (int16_t)constrain((int)balanceOffset, -BALANCE_MAX, BALANCE_MAX);
  int16_t curSeg    = ctrSeg + clampedOff;  // 0..48

  for (int16_t i = 0; i < BAL_N_SEG; i++) {
    int16_t sx   = BAL_BAR_X + i * (BAL_SEG_W + BAL_SEG_GAP);
    bool isCtr   = (i == ctrSeg);
    int16_t sh   = isCtr ? BAL_SEG_H_CTR : BAL_SEG_H_NORM;

    // Is dit segment gevuld? Vanuit center naar cursor.
    bool filled;
    if (inCenter) {
      filled = false;  // geen fill bij center
    } else if (balanceOffset > 0) {
      // Rechts: segmenten ctrSeg+1 t/m curSeg
      filled = (i > ctrSeg && i <= curSeg);
    } else {
      // Links: segmenten curSeg t/m ctrSeg-1
      filled = (i >= curSeg && i < ctrSeg);
    }

    // Mute-stop: segmenten aan de gemute kant dimmen
    bool muteDim = false;
    if (isMuteStop) {
      if (balanceOffset >= (BALANCE_MAX + 1))      muteDim = (i == 0);   // meest-links = gemute
      else if (balanceOffset <= -(BALANCE_MAX + 1)) muteDim = (i == BAL_N_SEG - 1);
    }

    uint16_t sc;
    if (muteDim)     sc = dimC(BAL_SEG_MUTE);
    else if (filled) sc = balColFill();
    else if (isCtr)  sc = balColCtr();
    else             sc = balColEmpty();

    display.fillRoundRect(sx, BAL_CY - BAL_AS_H/2 - sh, BAL_SEG_W, sh, 2, sc);
    display.fillRoundRect(sx, BAL_CY + BAL_AS_H/2,       BAL_SEG_W, sh, 2, sc);
  }

  // As
  display.fillRect(BAL_BAR_X, BAL_CY - BAL_AS_H/2, BAL_ACTUAL_W, BAL_AS_H, balColEmpty());

  // Indicator: witte verticale lijn gecentreerd op huidig segment
  int16_t indX   = BAL_BAR_X + curSeg * (BAL_SEG_W + BAL_SEG_GAP) + BAL_SEG_W / 2;
  int16_t indTop = BAL_CY - BAL_AS_H/2 - BAL_SEG_H_CTR - BAL_IND_OVER;
  int16_t indH   = BAL_SEG_H_CTR * 2 + BAL_AS_H + BAL_IND_OVER * 2;
  display.fillRect(indX - 1, indTop, 3, indH, dimC(BAL_IND_COL));
}

static void drawBalanceBar() {
  int16_t target = balTargetX();

  if (balDisplayX < 0.0f) {
    display.fillRect(0, BAL_VOL_Y, SCREEN_W, BAL_VOL_BOT - BAL_VOL_Y, C_BG);
    drawBalanceStatic();
  }

  // Direct positioneren voorkomt richting-lag bij omkeren met remote.
  // Center lock: bij balans = 0 exact op de centerlijn.
  if (balanceOffset == 0) target = balCenterX();

  balDisplayX = (float)target;

  drawBalanceIndicator(target);
}

static void hideBalanceBar() {
  balShowMs    = 0;
  balDisplayX  = -1.0f;   // Reset animatie — volgende showBalanceBar start schoon
  // Wis de volumezone
  display.fillRect(0, BAL_VOL_Y, SCREEN_W, BAL_VOL_BOT - BAL_VOL_Y, C_BG);
  // Herteken hairline (y=172, net boven volumezone)
  drawMainHairline();
  // Herteken volume-weergave
  drawMainPrimaryValue();
  // Herteken detailpanel — balanswaarde is gewijzigd tijdens de overlay,
  // cellen moeten gewist en opnieuw getekend worden om overlap te voorkomen.
  if (detailPanelActive()) drawSimpleDetailPanel();
}

// ── Detail panel — 2 rijen × 4 kolommen ─────────────────────────────────────
// Layout: rij 1 = Input | In. Offset | Eff. Atten. | Balance
//         rij 2 = LF Gain | LF Out   | MF Out      | Max Vol
//
// Elke cel: [label ···dots··· waarde], label links, waarde rechts.
// Scheidingslijn tussen de twee rijen; verticale lijnen tussen kolommen.

// Kleur voor de detailkader-lijnen — iets zichtbaarder dan C_HAIRLINE
static const uint16_t C_DETAIL_LINE = 0x2945u;  // ~#282828

static uint16_t detailPanelLineColor() {
  uint16_t base = detailColorFollow ? scale565(accent(), 46) : C_DETAIL_LINE;
  return dimC(base);
}

static uint16_t mainHairlineColor() {
  uint16_t base = detailColorFollow ? scale565(accent(), 52) : 0x4A49;
  return dimC(base);
}

// Stippen-hairline: 40 punten van x=80..720, gelijkmatig zichtbaar, groter in midden
static void drawMainHairline() {
  // Optie F: gevignetteerde lijn — 3px hoog, midden helderst, uitdovend naar randen
  const int16_t X0 = 60, X1 = 740;
  const int16_t Y  = 137;
  uint16_t baseCol = mainHairlineColor();
  uint8_t br = (uint8_t)((baseCol >> 11) & 0x1F) << 3;
  uint8_t bg = (uint8_t)((baseCol >>  5) & 0x3F) << 2;
  uint8_t bb = (uint8_t)( baseCol        & 0x1F) << 3;
  uint8_t bgR = (uint8_t)((C_BG >> 11) & 0x1F) << 3;
  uint8_t bgG = (uint8_t)((C_BG >>  5) & 0x3F) << 2;
  uint8_t bgB = (uint8_t)( C_BG        & 0x1F) << 3;

  // Drie horizontale lijnen: dy = -1, 0, +1
  // Elke pixel x krijgt een alpha op basis van afstand tot midden
  static const uint8_t ROW_BASE[3] = { 76, 178, 76 };  // 30% / 70% / 30% van 255

  for (int8_t dy = -1; dy <= 1; dy++) {
    uint8_t rowBase = ROW_BASE[dy + 1];
    for (int16_t x = X0; x <= X1; x++) {
      // Vignette: fade aan linker en rechter 15% van de breedte
      float rel = (float)(x - X0) / (float)(X1 - X0);  // 0..1
      float vig = (rel < 0.12f) ? (rel / 0.12f)
                : (rel > 0.88f) ? ((1.0f - rel) / 0.12f)
                : 1.0f;
      uint8_t alpha = (uint8_t)(rowBase * vig);
      uint8_t fr = (uint8_t)((br * alpha + bgR * (255 - alpha)) / 255);
      uint8_t fg = (uint8_t)((bg * alpha + bgG * (255 - alpha)) / 255);
      uint8_t fb = (uint8_t)((bb * alpha + bgB * (255 - alpha)) / 255);
      uint16_t col = ((fr >> 3) << 11) | ((fg >> 2) << 5) | (fb >> 3);
      display.drawPixel(x, Y + dy, col);
    }
  }
}

// Tekent shimmer-highlight over de hairline op huidige shimmerX positie.
// Breedte ±42px driehoek-envelop, 3 rijen hoog, kleur afgeleid van hairline accent.
static void drawShimmerOnHairline() {
  const int16_t X0 = 60, X1 = 740;
  const int16_t Y  = 137;
  const int16_t HW = 42;

  // Shimmer kleur: accentkleur direct (vóór dimC), gemengd naar wit voor zichtbaarheid.
  // mainHairlineColor() geeft gedimde kleur — te laag voor shimmer. Accent() geeft
  // de ongedimde basiskleur; die mix we met wit (200/255) voor een heldere flits.
  uint16_t acCol = detailColorFollow ? accent() : 0x8C71u;  // accent of neutraal grijs
  uint8_t sR = (uint8_t)(((acCol >> 11) & 0x1F) << 3);
  uint8_t sG = (uint8_t)(((acCol >>  5) & 0x3F) << 2);
  uint8_t sB = (uint8_t)(( acCol        & 0x1F) << 3);
  // Mix richting wit: shimmer is 60% wit + 40% accentkleur
  sR = (uint8_t)((255u * 3 + sR) / 4);
  sG = (uint8_t)((255u * 3 + sG) / 4);
  sB = (uint8_t)((255u * 3 + sB) / 4);

  uint8_t bgR = (uint8_t)((C_BG >> 11) & 0x1F) << 3;
  uint8_t bgG = (uint8_t)((C_BG >>  5) & 0x3F) << 2;
  uint8_t bgB = (uint8_t)( C_BG        & 0x1F) << 3;

  // Piek per rij: middelste rij helderst
  static const uint8_t ROW_PEAK[3] = { 80, 200, 80 };

  int16_t cx = (int16_t)shimmerX;
  int16_t x0 = cx - HW < X0 ? X0 : cx - HW;
  int16_t x1 = cx + HW > X1 ? X1 : cx + HW;

  display.startWrite();
  for (int8_t dy = -1; dy <= 1; dy++) {
    uint8_t peak = ROW_PEAK[dy + 1];
    for (int16_t x = x0; x <= x1; x++) {
      float dist = fabsf((float)(x - cx));
      float env  = 1.0f - dist / (float)HW;
      if (env <= 0.0f) continue;

      float rel = (float)(x - X0) / (float)(X1 - X0);
      float vig = (rel < 0.12f) ? rel / 0.12f : (rel > 0.88f) ? (1.0f - rel) / 0.12f : 1.0f;

      uint8_t alpha = (uint8_t)(peak * env * env * vig);
      if (alpha == 0) continue;

      uint8_t fr = (uint8_t)((sR * alpha + bgR * (255 - alpha)) / 255);
      uint8_t fg = (uint8_t)((sG * alpha + bgG * (255 - alpha)) / 255);
      uint8_t fb = (uint8_t)((sB * alpha + bgB * (255 - alpha)) / 255);
      display.drawPixel(x, Y + dy, ((fr >> 3) << 11) | ((fg >> 2) << 5) | (fb >> 3));
    }
  }
  display.endWrite();
}

static float targetEffAttenDb() {
  int8_t off = inputOffset[currentInput];
  return (currentVolume * 0.5f) - 115.5f + (off * 0.5f);
}

static void formatDetailBalance(char* out, size_t outLen, uint16_t* colorOut) {
  if (balanceOffset == 0) {
    snprintf(out, outLen, "Center");
    *colorOut = dimC(C_GRAY);
    return;
  }

  const int8_t BAL_MUTE = BALANCE_MAX + 1;
  if (balanceOffset <= -BAL_MUTE) {
    snprintf(out, outLen, "R MUTE");
    *colorOut = C_STATUS_ERR;
    return;
  }
  if (balanceOffset >= BAL_MUTE) {
    snprintf(out, outLen, "L MUTE");
    *colorOut = C_STATUS_ERR;
    return;
  }

  snprintf(out, outLen, "%c -%.1f dB", (balanceOffset < 0) ? 'R' : 'L', fabsf(balanceOffset * 0.5f));
  *colorOut = dimC(C_GRAY);
}

// Teken één cel: x=linker rand, y=bovenkant rij
// Tekst met scale_x16=13 (~11.5px effectief) zodat alles past op 200px breedte.
// Label links uitgelijnd, waarde rechts uitgelijnd — beide op dezelfde baseline.
#define DP_SCALE   13          // scale_x16 voor detail tekst (13/16 × 14px ≈ 11.4px)
#define DP_PAD     10          // horizontale cel-marge




static void drawMainStatusChips() {
  if (currentScreen != SCR_MAIN || inStandby) return;

  // Status-chip zone schoonmaken
  display.fillRect(SCREEN_W - 200, 10, 190, 40, C_BG);

  // Thermometer icoontje: klein, gedempte rood, dimt mee met mainCalmBlend.
  // Getekend als primitieven: smalle rechthoek (buis) + cirkel (bol) onderaan.
  if (warmStandbyEnabled && !isMuted) {
    static const uint16_t C_THERM = 0x8000u;  // Gedempte rood
    uint8_t bright = (uint8_t)(255U - mainCalmBlend * 180U / 255U);
    uint16_t col = fa_dim(C_THERM, bright);

    // Positie: rechtsboven, zelfde zone als de oude MAX chip
    const int16_t TX = SCREEN_W - 26;  // horizontaal centrum van icoontje
    const int16_t TY = 10;             // bovenkant

    // Buis: 5px breed, 18px hoog, afgeronde top via twee pixels
    const int16_t BW = 5;   // buisbreedte
    const int16_t BH = 18;  // buishoogte
    const int16_t BX = TX - BW/2;
    // Buis vulling
    display.fillRect(BX, TY + 2, BW, BH - 2, col);
    // Afgeronde top (2px smaller)
    display.fillRect(BX + 1, TY, BW - 2, 3, col);
    // Bol onderaan: 9px diameter
    const int16_t BR = 5;
    display.fillCircle(TX, TY + BH + BR - 1, BR, col);
    // Klein highlight stippeltje in bol (achtergrond kleur = subtiele glans)
    display.drawPixel(TX - 2, TY + BH + BR - 2, fa_dim(C_BG, 80));
  }
}



// ── Detail panel — twee kolommen, label links, waarde rechts op vaste x ──────
// AA_XXS label + AA_XS waarde op dezelfde baseline.
// Waarden links uitgelijnd per kolom op vaste x-positie.
// Negatief teken (-) telt niet mee in uitlijning.

// Vaste kolom posities (berekend op basis van max labelbreedte AA_XXS)
// Links:  label x=20,  max label='Offset'(79px)      → waarde x=117
// Rechts: label x=420, max label='Eff. Atten.'(137px) → waarde x=575
#define DP_LBL_L   20    // label x linker kolom
#define DP_VAL_L   127   // waarde x linker kolom (28px na langste label 'Offset')
#define DP_LBL_R   420   // label x rechter kolom
#define DP_VAL_R   579   // waarde x rechter kolom (28px na langste label 'Eff. Atten.')

// Rij baselines: AA_XXS cap=19px, ruimere gap tussen rijen
#define DP_ROW1    (DP_TOP + 10 + 19)         // 414
#define DP_ROW2    (DP_ROW1 + 18 + 19)        // 451

static void drawSimpleDetailPanel() {
  if (!detailPanelActive() && panelBlend == 0) return;

  // panelBlend 0=onzichtbaar, 255=volledig — alle kleuren schalen mee
  uint8_t pb = panelBlend;

  display.fillRect(0, DP_TOP, SCREEN_W, SCREEN_H - DP_TOP, C_BG);

  if (pb == 0) return;  // Gewist maar niets tekenen

  // Scheidingslijn boven panel — fadet mee met panelBlend
  uint16_t sepCol = alpha565(dimC(VGC_BG_SEL), pb);
  display.fillRect(0, DP_TOP, SCREEN_W, 1, sepCol);
  display.fillRect(SCREEN_W / 2, DP_TOP + 4, 1, SCREEN_H - DP_TOP - 8, sepCol);

  uint16_t valBase = detailColorFollow ? scale565(accent(), 68) : C_GRAY;
  uint16_t lblC = alpha565(dimC(scale565(C_GRAY_DIM, calmMixPct(100, 55))), pb);
  uint16_t valC = alpha565(dimC(scale565(valBase,    calmMixPct(82,  56))), pb);

  char portStr[10];   getPortStr(currentInput, portStr);
  char offsetStr[12];
  int8_t off = inputOffset[currentInput];
  if (off == 0) strcpy(offsetStr, "0 dB");
  else sprintf(offsetStr, "%+.1f dB", off * 0.5f);
  char attenStr[14];
  sprintf(attenStr, "%.1f dB", targetEffAttenDb());
  char balStr[14];
  uint16_t balValColor;
  formatDetailBalance(balStr, sizeof(balStr), &balValColor);
  uint16_t balC = (balValColor == C_STATUS_ERR || balValColor == dimC(C_STATUS_ERR))
                ? alpha565(balValColor, pb) : valC;

  // Linker kolom
  AAFont_drawString(AA_XXS, "Input",  DP_LBL_L, DP_ROW1, lblC, C_BG, AA_LEFT, 90);
  AAFont_drawString(AA_XXS, portStr,  DP_VAL_L, DP_ROW1, valC, C_BG, AA_LEFT, 240);
  AAFont_drawString(AA_XXS, "Offset", DP_LBL_L, DP_ROW2, lblC, C_BG, AA_LEFT, 90);
  AAFont_drawString(AA_XXS, offsetStr,DP_VAL_L, DP_ROW2, valC, C_BG, AA_LEFT, 240);

  // Rechter kolom
  AAFont_drawString(AA_XXS, "Eff. Atten.", DP_LBL_R, DP_ROW1, lblC, C_BG, AA_LEFT, 140);
  AAFont_drawString(AA_XXS, attenStr, DP_VAL_R, DP_ROW1, valC, C_BG, AA_LEFT, 185);
  AAFont_drawString(AA_XXS, "Balance",    DP_LBL_R, DP_ROW2, lblC, C_BG, AA_LEFT, 140);
  AAFont_drawString(AA_XXS, balStr,   DP_VAL_R, DP_ROW2, balC, C_BG, AA_LEFT, 185);
}


static void redrawDetailAttenCard() {
  if (!detailPanelActive() || currentScreen != SCR_MAIN) return;

  uint16_t valBase = detailColorFollow ? scale565(accent(), 68) : C_GRAY;
  uint16_t valC    = dimC(scale565(valBase, calmMixPct(82, 56)));

  char attenStr[14];
  sprintf(attenStr, "%.1f dB", targetEffAttenDb());

  // Wis alleen de waardezone
  display.fillRect(DP_VAL_R - 14, DP_ROW1 - 20, SCREEN_W - (DP_VAL_R - 14) - 4, 24, C_BG);
  AAFont_drawString(AA_XXS, attenStr, DP_VAL_R, DP_ROW1, valC, C_BG, AA_LEFT, 185);
}

static void redrawDetailBalanceCard() {
  if (!detailPanelActive() || currentScreen != SCR_MAIN) return;

  uint16_t valBase = detailColorFollow ? scale565(accent(), 68) : C_GRAY;
  uint16_t valC    = dimC(scale565(valBase, calmMixPct(82, 56)));

  char balStr[14];
  uint16_t balValColor;
  formatDetailBalance(balStr, sizeof(balStr), &balValColor);
  uint16_t drawCol = (balValColor == C_STATUS_ERR || balValColor == dimC(C_STATUS_ERR)) ? balValColor : valC;

  // Wis alleen de waardezone
  display.fillRect(DP_VAL_R - 14, DP_ROW2 - 20, SCREEN_W - (DP_VAL_R - 14) - 4, 24, C_BG);
  AAFont_drawString(AA_XXS, balStr, DP_VAL_R, DP_ROW2, drawCol, C_BG, AA_LEFT, 185);
}


void updateAfterInputSwitch() {
  // Gerichte update na inputwissel.
  // showSwitchingScreen() heeft alleen de naam- en volumezone gewist —
  // het detail panel staat nog volledig op het scherm.
  // Werk alleen de zones bij die daadwerkelijk veranderen.

  // 1. Inputnaam — eerst zone wissen om overlap met switching-font te voorkomen
  clearAndDrawInputName();
  drawMainHairline();

  // 2. Volumezone
  display.fillRect(0, 173, SCREEN_W, DP_TOP - 173, C_BG);
  drawMainPrimaryValue();

  // 3. Detail panel bijwerken
  if (detailPanelActive()) drawSimpleDetailPanel();
}

void drawMainScreen() {
  if (currentScreen == SCR_BOOT || currentScreen == SCR_WARMUP) return;
  currentScreen = SCR_MAIN;
  mainCalmMode  = false;
  mainCalmBlend = 0;
  fontIsLarge  = largeVolumeFontActive();
  xfadeState   = XF_IDLE;
  volBlinkCount = 0;
  volBlinkOn    = true;
  volBlinkNeedsRestore = false;
  panelBlend   = detailPanelActive() ? 255 : 0;
  if (inStandby) {
    if (screenBrightness == 0) {
      setScreenBrightness(activeBrightness());
      standbyTextShowMs = millis();
    } else if (standbyTextShowMs == 0) {
      standbyTextShowMs = millis();
      setScreenBrightness(activeBrightness());
    }
    if ((millis() - standbyTextShowMs) < STANDBY_TEXT_MS) drawStandbyText();
    else { display.fillScreen(C_BG); setScreenBrightness(0); }
    display.setFont(NULL);
    return;
  }
  standbyTextShowMs = 0;
  if (screenBrightness == 0) setScreenBrightness(activeBrightness());
  display.startBuffering();
  display.fillScreen(C_BG);
  drawInputName();
  drawMainStatusChips();
  drawMainHairline();
  drawMainPrimaryValue();
  drawSimpleDetailPanel();
  display.setFont(NULL);
  display.endBuffering();
}

void fadeTransition() {
  if (currentScreen == SCR_BOOT || currentScreen == SCR_WARMUP) return;
  uint8_t saved = screenBrightness;
  for (int b = (int)saved; b >= 0; b -= 42) { setScreenBrightness((uint8_t)max(0,b)); drawMainScreen(); delay(20); }
  setScreenBrightness(0); drawMainScreen(); delay(40);
  if (!inStandby) setScreenBrightness(saved);  // Bij standby-in: brightness blijft 0, updateDisplay() beheert dit
}

static void redrawVolumeZone() {
  if (inStandby) { drawMainScreen(); return; }
  if (volBlinkCount > 0 && !volBlinkOn) return;  // blink is in 'uit'-fase — niet overschrijven
  display.startBuffering();
  drawMainStatusChips();
  drawMainHairline();
  clearPrimaryValueZone();
  drawMainPrimaryValue();
  if (detailPanelActive()) redrawDetailAttenCard();
  display.endBuffering();
}

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 4 — Hoofdmenu (kaartengrid 3×2)
// ════════════════════════════════════════════════════════════════════════════
#define MC_COLS   3
#define MC_ROWS   2
#define MC_GAP   10
#define MC_MX    16
#define MC_MY     8
#define MC_W     ((SCREEN_W - 2*MC_MX - (MC_COLS-1)*MC_GAP) / MC_COLS)
#define MC_H     ((CONT_H   - 2*MC_MY - (MC_ROWS-1)*MC_GAP) / MC_ROWS)
#define MC_X(c)  (MC_MX + (c)*(MC_W + MC_GAP))
#define MC_Y(r)  (CONT_Y + MC_MY + (r)*(MC_H + MC_GAP))

struct MenuCard { const char* title; };
static const MenuCard CARDS[] = {
  { "Volume"   },
  { "Input levels" },
  { "Inputs"   },
  { "Display"  },
  { "System"   },
  { "IR Learn" },
};
#define MENU_N  6

static bool isDefaultDimSetting(const char* label) {
  if (strcmp(label, "Brightness") == 0) return uiBrightnessPct == SETTINGS_DEFAULT.uiBrightnessPct;
  if (strcmp(label, "Dim delay") == 0) return dimDelaySec     == SETTINGS_DEFAULT.dimDelaySec;
  if (strcmp(label, "Dim level") == 0) return dimPercent      == SETTINGS_DEFAULT.dimPercent;
  if (strcmp(label, "Deep dim delay")  == 0) return deepDimDelayMin == SETTINGS_DEFAULT.deepDimDelayMin;
  return true;
}

static bool isDefaultVGSlider(const char* label) {
  if (strcmp(label, "Max Vol") == 0)      return maxVolume == SETTINGS_DEFAULT.maxVolume;
  if (strcmp(label, "Balance") == 0)      return balanceOffset == SETTINGS_DEFAULT.balanceOffset;
  if (strcmp(label, "LF Gain") == 0)      return gainLF == SETTINGS_DEFAULT.gainLF;
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    if (strcmp(label, inputNames[i]) == 0) return inputOffset[i] == SETTINGS_DEFAULT.inputOffset[i];
  }
  return true;
}

static bool isDefaultVGToggle(const char* label) {
  if (strcmp(label, "LF Out") == 0)           return gainLFOUT == SETTINGS_DEFAULT.gainLFOUT;
  if (strcmp(label, "MF Out") == 0)           return gainMFOUT == SETTINGS_DEFAULT.gainMFOUT;
  if (strcmp(label, "LF / MF Out") == 0)      return gainLFOUT == SETTINGS_DEFAULT.gainLFOUT && gainMFOUT == SETTINGS_DEFAULT.gainMFOUT;
  if (strcmp(label, "Mute on startup/resume") == 0)  return muteOnStartup == SETTINGS_DEFAULT.muteOnStartup;
  if (strcmp(label, "Volume units") == 0)     return volUnitsMode == SETTINGS_DEFAULT.volUnitsMode;
  if (strcmp(label, "Control curve") == 0)    return volumeCurve == SETTINGS_DEFAULT.volumeCurve;
  return true;
}

static uint16_t statusColorFromText(const char* value, uint16_t fallback) {
  if (!value) return fallback;
  if (strcmp(value, "OK") == 0 || strcmp(value, "On") == 0 || strstr(value, "Normal") != nullptr) return dimC(VGC_BDR_SEL);
  if (strcmp(value, "FAIL") == 0 || strcmp(value, "Error") == 0 || strstr(value, "Degraded") != nullptr) return dimC(C_STATUS_ERR);
  return fallback;
}

static void drawMenuIcon(uint8_t idx, int16_t cx, int16_t cy) {
  uint16_t ico = dimC(scale565(C_LBL, 110));
  int16_t ix = cx + MC_W / 2;
  int16_t iy = cy + 58;

  switch (idx) {
    case 0: // Volume: knop + ring
      display.drawCircle(ix, iy, 22, ico);
      display.drawCircle(ix, iy, 10, ico);
      display.fillRect(ix + 20, iy - 2, 10, 4, ico);
      break;
    case 1: // Offsets: sliders
      for (int k = -1; k <= 1; k++) {
        int16_t y = iy + k * 12;
        display.drawFastHLine(ix - 28, y, 56, ico);
      }
      display.fillCircle(ix - 10, iy - 12, 4, ico);
      display.fillCircle(ix + 12, iy, 4, ico);
      display.fillCircle(ix - 2, iy + 12, 4, ico);
      break;
    case 2: // Inputs: 3 poorten
      display.drawRoundRect(ix - 28, iy - 16, 56, 32, 5, ico);
      display.fillCircle(ix - 14, iy, 4, ico);
      display.fillCircle(ix, iy, 4, ico);
      display.fillCircle(ix + 14, iy, 4, ico);
      break;
    case 3: // Display: scherm
      display.drawRoundRect(ix - 28, iy - 16, 56, 32, 5, ico);
      display.drawFastHLine(ix - 18, iy + 20, 36, ico);
      break;
    case 4: // System: gear-ish
      display.drawCircle(ix, iy, 14, ico);
      display.drawCircle(ix, iy, 5, ico);
      for (int a = 0; a < 8; a++) {
        float ang = a * 0.785398f;
        int16_t x0 = ix + (int16_t)(16 * cosf(ang));
        int16_t y0 = iy + (int16_t)(16 * sinf(ang));
        int16_t x1 = ix + (int16_t)(22 * cosf(ang));
        int16_t y1 = iy + (int16_t)(22 * sinf(ang));
        display.drawLine(x0, y0, x1, y1, ico);
      }
      break;
    default: // IR Learn: remote
      display.drawRoundRect(ix - 16, iy - 24, 32, 48, 7, ico);
      display.fillCircle(ix, iy - 13, 4, ico);
      display.drawFastHLine(ix - 9, iy + 3, 18, ico);
      display.drawFastHLine(ix - 9, iy + 12, 18, ico);
      break;
  }
}

static void drawMenuCard(uint8_t idx) {
  int16_t col = idx % MC_COLS, row = idx / MC_COLS;
  int16_t cx = MC_X(col), cy = MC_Y(row);
  display.fillRoundRect(cx, cy, MC_W, MC_H, 6, dimC(C_CARD_BG));
  // Amber border — zelfde stijl als submenus
  display.drawRoundRect(cx,     cy,     MC_W,     MC_H,     6, dimC(VGC_BDR_NORM));
  display.drawRoundRect(cx + 1, cy + 1, MC_W - 2, MC_H - 2, 6, dimC(VGC_BDR_NORM));
  if (idx >= MENU_N) return;
  // Subtiele detail-style lijnaccenten
  display.fillRect(cx + 10, cy + 10, MC_W - 20, 1, dimC(C_DETAIL_LINE));
  display.fillRect(cx + 10, cy + MC_H - 12, MC_W - 20, 1, dimC(C_DETAIL_LINE));

  drawMenuIcon(idx, cx, cy);

  AAFont_drawString(AA_XXS, CARDS[idx].title,
                   cx, cy + MC_H - 26,
                   dimC(scale565(C_LBL, 112)), dimC(C_CARD_BG), AA_CENTER, MC_W);
}

static void drawMainMenu() {
  currentScreen = SCR_MENU;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("SETTINGS");
  for (uint8_t i = 0; i < MC_COLS * MC_ROWS; i++) drawMenuCard(i);
  display.setFont(NULL);
  display.endBuffering();
}

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 5 — Volume & Gain
// ════════════════════════════════════════════════════════════════════════════
static void vgFormatVal(int8_t id, char* buf) {
  if (id == VG_MAXVOL) {
    float db = (maxVolume * 0.5f) - 115.5f;
    sprintf(buf, "%.1f dB", db);
  } else if (id == VG_BALANCE) {
    if (balanceOffset == 0) { strcpy(buf, "Center"); }
    else {
      float db = fabsf(balanceOffset * 0.5f);
      sprintf(buf, "%c -%.1f dB", balanceOffset < 0 ? 'R' : 'L', db);
    }
  } else if (id == VG_LFGAIN) {
    sprintf(buf, "%.1f dB", gainLF * 0.5f);
  } else if (id >= VG_IO_BASE && id < VG_IO_BASE + INPUT_COUNT) {
    int i = id - VG_IO_BASE;
    float db = inputOffset[i] * 0.5f;
    if (db == 0.0f) strcpy(buf, "0 dB");
    else sprintf(buf, "%+.1f dB", db);
  } else if (id >= VG_SV_BASE && id < VG_SV_BASE + INPUT_COUNT) {
    int i = id - VG_SV_BASE;
    float db = (startupVolume[i] * 0.5f) - 115.5f;
    sprintf(buf, "%.1f dB", db);
  }
}

// ── Volume/Gain scherm layout — SR_H rijen, zelfde stijl als Display scherm ──
// 4 slider-rijen + 1 toggle-rij, gap=14, pad=10
// 5×52 + 4×14 + 2×10 = 336px (past in CONT_H=424)
#define VG_ROW_GAP   14   // gap tussen rijen
#define VG_PAD_TOP   10   // padding boven eerste rij
#define VG_TOG_H     SR_H // toggle-rij zelfde hoogte als slider-rijen

static inline int16_t vgRowY(uint8_t rowIdx) {
  return CONT_Y + VG_PAD_TOP + rowIdx * (SR_H + VG_ROW_GAP);
}
// Behoud voor Offsets scherm (gebruikt nog VG_PAD als marge)
static inline uint16_t vgCardBg(bool sel) {
  return dimC(sel ? VGC_BG_SEL : VGC_BG_NORM);
}
// Verwijderde functies: vgCardW, vgCardH, vgCardX — niet meer nodig

// drawFilledCard: label linksboven met eenheid erin, grote waarde rechts-midden
static void drawFilledCard(int16_t x, int16_t y, int16_t w, int16_t h,
                           const char* label, const char* valStr, const char* unitStr,
                           float pct, bool bipolar, bool sel, bool changed) {
  uint16_t bg = vgCardBg(sel);
  display.fillRoundRect(x, y, w, h, UI_CARD_R, bg);

  // Positievulling — 2px inset
  uint16_t fillC = dimC(sel ? VGC_FILL_SEL : VGC_FILL_NORM);
  int16_t fx = x + 2, fy = y + 2, fw_max = w - 4, fh = h - 4;
  if (bipolar) {
    float dev = pct - 0.5f;
    int16_t mid = fx + fw_max / 2;
    if (dev > 0.01f) {
      int16_t fw = (int16_t)(dev * 2.0f * (fw_max / 2));
      if (fw > fw_max / 2) fw = fw_max / 2;
      display.fillRect(mid, fy, fw, fh, fillC);
    } else if (dev < -0.01f) {
      int16_t fw = (int16_t)(-dev * 2.0f * (fw_max / 2));
      if (fw > fw_max / 2) fw = fw_max / 2;
      display.fillRect(mid - fw, fy, fw, fh, fillC);
    }
    int16_t clh = (h * 4) / 5;
    int16_t cly = y + (h - clh) / 2;
    display.fillRect(mid, cly, 1, clh, dimC(VGC_BDR_NORM));
  } else {
    int16_t fw = (int16_t)(pct * fw_max);
    if (fw > fw_max) fw = fw_max;
    if (fw > 0) display.fillRect(fx, fy, fw, fh, fillC);
  }

  // Border 2px
  uint16_t bdr = sel ? dimC(VGC_BDR_SEL) : dimC(VGC_BDR_NORM);
  display.drawRoundRect(x,     y,     w,     h,     UI_CARD_R, bdr);
  display.drawRoundRect(x + 1, y + 1, w - 2, h - 2, UI_CARD_R, bdr);

  // Label linksboven — eenheid achter label als die er is: "Max Vol · dB"
  char lblBuf[32];
  if (unitStr && unitStr[0] != '\0') {
    snprintf(lblBuf, sizeof(lblBuf), "%s \xB7 %s", label, unitStr);  // · = 0xB7
  } else {
    strncpy(lblBuf, label, sizeof(lblBuf) - 1);
    lblBuf[sizeof(lblBuf) - 1] = '\0';
  }
  uint16_t lblC = dimC(sel ? VGC_LBL_SEL : VGC_LBL_NORM);
  AAFont_drawString(AA_XXS, lblBuf,
                   x + UI_PAD_L, y + 30,
                   lblC, bg, AA_LEFT, w - UI_PAD_L - UI_PAD_R);

  // Waarde — AA_SM, rechts uitgelijnd, verticaal gecentreerd
  // Gewijzigd: helderder grijs (geen kleur), geselecteerd: warm wit
  uint16_t valC = sel     ? dimC(VGC_VAL_SEL)
                : changed ? dimC(VGC_VAL_CHG)
                :           dimC(VGC_VAL_NORM);
  int16_t valBase = y + h / 2 + 21;
  int16_t innerW  = w - UI_PAD_L - UI_PAD_R;
  AAFont_drawString(AA_SM, valStr,
                   x + UI_PAD_L, valBase,
                   valC, bg, AA_RIGHT, innerW);
}

// ════════════════════════════════════════════════════════════════════════════
//  Gedeelde knop-helpers — gebruikt door alle menu-schermen
//  Uniforme hoogte: BTN_H = 2×SR_H + VG_ROW_GAP = 118px
//  Max breedte:     1/3 van LW = 256px
//  Achtergrond:     C_CARD_BG (zelfde als hoofdmenu kaarten)
//  Positie:         altijd onderaan: SCREEN_H - 16 - BTN_H
// ════════════════════════════════════════════════════════════════════════════
#define MENU_BTN_H  (2 * SR_H + VG_ROW_GAP)          // 118px
#define MENU_BTN_Y  (SCREEN_H - 16 - MENU_BTN_H)     // 346px
#define MENU_BTN_MAX_W  ((SCREEN_W - 32) / 3)         // 256px
#define MENU_BTN_GAP  8

// Bereken knopbreedte: max 1/3 voor n knoppen
static inline int16_t menuBtnW(int16_t n) {
  int16_t lw = SCREEN_W - 32;
  return min((int16_t)MENU_BTN_MAX_W, (int16_t)((lw - (n-1)*MENU_BTN_GAP) / n));
}
// X-positie van knop i
static inline int16_t menuBtnX(int16_t i, int16_t bw) {
  return 16 + i * (bw + MENU_BTN_GAP);
}

// Toggle-drukknop: label bovenin (gedimde kleur), waarde onderin (heldere kleur).
// Kader altijd VGC_BDR_NORM. Achtergrond C_CARD_BG (hoofdmenu tint).
// changed = waarde wijkt af van default → waarde in VGC_VAL_CHG.
static void drawMenuTogBtn(int16_t bx, int16_t by, int16_t bw, int16_t bh,
                            const char* label, const char* value, bool changed = false) {
  uint16_t bg  = dimC(C_CARD_BG);
  uint16_t bdr = dimC(VGC_BDR_NORM);
  uint16_t lc  = dimC(VGC_LBL_NORM);
  uint16_t vc  = changed ? dimC(VGC_VAL_CHG) : dimC(VGC_VAL_SEL);
  display.fillRoundRect(bx, by, bw, bh, UI_CARD_R, bg);
  display.drawRoundRect(bx,   by,   bw,   bh,   UI_CARD_R, bdr);
  display.drawRoundRect(bx+1, by+1, bw-2, bh-2, UI_CARD_R, bdr);
  int16_t lblY = by + bh/3 + 2;
  int16_t valY = by + bh*2/3 + 8;
  AAFont_drawString(AA_XXS, label, bx, lblY, lc, bg, AA_CENTER, bw);
  AAFont_drawString(AA_XXS, value, bx, valY, vc, bg, AA_CENTER, bw);
}

// Nav-drukknop: afwijkende tint, accent border, tekst gecentreerd.
// Gebruik voor knoppen die naar een submenu gaan.
static void drawMenuNavBtn(int16_t bx, int16_t by, int16_t bw, int16_t bh,
                            const char* label) {
  uint16_t bg  = dimC(0x1928u);
  uint16_t bdr = dimC(VGC_BDR_SEL);
  uint16_t tc  = dimC(VGC_BDR_SEL);
  display.fillRoundRect(bx, by, bw, bh, UI_CARD_R, bg);
  display.drawRoundRect(bx,   by,   bw,   bh,   UI_CARD_R, bdr);
  display.drawRoundRect(bx+1, by+1, bw-2, bh-2, UI_CARD_R, bdr);
  AAFont_drawString(AA_XXS, label, bx, by + bh/2 + 8, tc, bg, AA_CENTER, bw);
}

// Teken N knoppen onderaan het scherm
// btns[] = { label, value, isNav } triplets via lambda
// Aanroep: drawMenuBtnRow(N, [](int i, ...) { ... })
// Eenvoudigere variant: directe aanroep per knop via menuBtnW/menuBtnX/MENU_BTN_Y/MENU_BTN_H

// ── VG toggle-rij — 4 knoppen onderaan, uniforme stijl ───────────────────────
static void drawVGToggleRow(int16_t x, int16_t y, int16_t w) {
  (void)x; (void)y; (void)w;
  const int16_t N  = 5;
  const int16_t bw = menuBtnW(N);
  const int16_t by = MENU_BTN_Y;
  const int16_t bh = MENU_BTN_H;

  const char* rs_val = muteOnStartup         ? "on"    : "off";
  const char* un_val = (volUnitsMode == VOL_UNITS_DB)     ? "dB"
                     : (volUnitsMode == VOL_UNITS_PERCENT) ? "0-100"
                     :                                       "steps";
  const char* cv_val = (volumeCurve == VOL_CURVE_VELOCITY) ? "velocity"
                     : (volumeCurve == VOL_CURVE_ADAPTIVE)  ? "adaptive"
                     :                                         "linear";
  const char* es_val = (encSensitivity == 0) ? "low"
                     : (encSensitivity == 2) ? "high"
                     :                         "neutral";

  drawMenuTogBtn(menuBtnX(0,bw), by, bw, bh, "Res. mute", rs_val, muteOnStartup  != SETTINGS_DEFAULT.muteOnStartup);
  drawMenuTogBtn(menuBtnX(1,bw), by, bw, bh, "Units",     un_val, volUnitsMode   != SETTINGS_DEFAULT.volUnitsMode);
  drawMenuTogBtn(menuBtnX(2,bw), by, bw, bh, "Curve",     cv_val, volumeCurve    != SETTINGS_DEFAULT.volumeCurve);
  drawMenuTogBtn(menuBtnX(3,bw), by, bw, bh, "Encoder",   es_val, encSensitivity != SETTINGS_DEFAULT.encSensitivity);
  drawMenuNavBtn(menuBtnX(4,bw), by, bw, bh, "Adv.  \xBB");
}

static void vgSplitUnit(const char* valStr, char* numBuf, uint8_t numLen,
                        char* unitBuf, uint8_t unitLen) {
  const char* sp = strchr(valStr, ' ');
  if (!sp) {
    strncpy(numBuf, valStr, numLen - 1); numBuf[numLen-1] = '\0';
    unitBuf[0] = '\0';
    return;
  }
  const char* lastSp = strrchr(valStr, ' ');
  if (lastSp == sp) {
    size_t n = (size_t)(sp - valStr);
    if (n >= numLen) n = numLen - 1;
    memcpy(numBuf, valStr, n); numBuf[n] = '\0';
    strncpy(unitBuf, sp + 1, unitLen - 1); unitBuf[unitLen-1] = '\0';
  } else {
    size_t n = (size_t)(lastSp - valStr);
    if (n >= numLen) n = numLen - 1;
    memcpy(numBuf, valStr, n); numBuf[n] = '\0';
    strncpy(unitBuf, lastSp + 1, unitLen - 1); unitBuf[unitLen-1] = '\0';
  }
}

// Herteken alleen de gewijzigde rij — voorkomt flikkering bij encoder-gebruik
static void vgRedrawRow(int8_t id) {
  char buf[20];
  const int16_t LX = 16;
  const int16_t LW = SCREEN_W - 32;

  if (id == VG_MAXVOL || id == VG_BALANCE || id == VG_LFGAIN) {
    uint8_t rowIdx = (id == VG_MAXVOL)  ? 0
                   : (id == VG_BALANCE) ? 1
                   :                      2;  // VG_LFGAIN
    int16_t ry = vgRowY(rowIdx);
    display.fillRect(LX, ry, LW, SR_H, C_BG);

    bool sel = (vgSelection == id);
    bool bipolar = (id == VG_BALANCE);
    float pct;
    if      (id == VG_MAXVOL)  pct = maxVolume / 255.0f;
    else if (id == VG_BALANCE) pct = (balanceOffset + BALANCE_MAX) / (BALANCE_MAX * 2.0f);
    else                       pct = (gainLF + 7) / 31.0f;
    const char* lbl = (id == VG_MAXVOL)  ? "Max Vol"
                    : (id == VG_BALANCE) ? "Balance"
                    :                      "LF Gain";
    bool changed = !isDefaultVGSlider(
      (id == VG_MAXVOL)  ? "Max Vol" :
      (id == VG_BALANCE) ? "Balance" : "LF Gain");
    vgFormatVal(id, buf);
    float zMark = (id == VG_BALANCE) ? 0.5f
              : (id == VG_LFGAIN)  ? (7.0f / 31.0f)
              :                       -1.0f;
    drawSliderRow(LX, ry, LW, lbl, 0, pct, buf, sel, bipolar, changed, zMark);

  } else if (id >= VG_IO_BASE && id < VG_IO_BASE + INPUT_COUNT) {
    int i = id - VG_IO_BASE;
    const int16_t PAD    = VG_PAD;
    const int16_t TOTAL_W = SCREEN_W - PAD * 2;
    const int16_t DIV_X  = PAD + TOTAL_W / 2;
    const int16_t DIV_GAP = 8;
    const int16_t COL_W  = TOTAL_W / 2 - DIV_GAP;
    const int16_t GAP    = 10;
    const int16_t ROW_Y0 = CONT_Y + 42;
    int16_t ry = ROW_Y0 + i * (SR_H + GAP);
    display.fillRect(PAD, ry, COL_W, SR_H, C_BG);
    float pct = (inputOffset[i] + 12) / 24.0f;
    vgFormatVal(id, buf);
    drawSliderRow(PAD, ry, COL_W, inputNames[i], 0, pct, buf,
                  (vgSelection == id), true,
                  inputOffset[i] != SETTINGS_DEFAULT.inputOffset[i], 0.5f);

  } else if (id >= VG_SV_BASE && id < VG_SV_BASE + INPUT_COUNT) {
    int i = id - VG_SV_BASE;
    const int16_t PAD    = VG_PAD;
    const int16_t TOTAL_W = SCREEN_W - PAD * 2;
    const int16_t DIV_X  = PAD + TOTAL_W / 2;
    const int16_t DIV_GAP = 8;
    const int16_t COL_W  = TOTAL_W / 2 - DIV_GAP;
    const int16_t COL_R_X = DIV_X + DIV_GAP;
    const int16_t GAP    = 10;
    const int16_t ROW_Y0 = CONT_Y + 42;
    int16_t ry = ROW_Y0 + i * (SR_H + GAP);
    display.fillRect(COL_R_X, ry, COL_W, SR_H, C_BG);
    float pct = startupVolume[i] / 255.0f;
    vgFormatVal(id, buf);
    drawSliderRow(COL_R_X, ry, COL_W, "", 0, pct, buf,
                  (vgSelection == id), false,
                  startupVolume[i] != SETTINGS_DEFAULT.startupVolume[i]);
  }
}

static void drawVolumeGainScreen() {
  currentScreen = SCR_VOLUME_GAIN;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("VOLUME");

  char buf[20];
  const int16_t LX = 16;
  const int16_t LW = SCREEN_W - 32;

  // Rij 0: Max Vol
  vgFormatVal(VG_MAXVOL, buf);
  drawSliderRow(LX, vgRowY(0), LW, "Max Vol", 0,
                maxVolume / 255.0f, buf,
                vgSelection == VG_MAXVOL,
                false,
                maxVolume != SETTINGS_DEFAULT.maxVolume);

  // Rij 1: Balance (bipolair)
  vgFormatVal(VG_BALANCE, buf);
  drawSliderRow(LX, vgRowY(1), LW, "Balance", 0,
                (balanceOffset + BALANCE_MAX) / (BALANCE_MAX * 2.0f), buf,
                vgSelection == VG_BALANCE,
                true,
                balanceOffset != SETTINGS_DEFAULT.balanceOffset, 0.5f);

  // Rij 2: LF Gain
  vgFormatVal(VG_LFGAIN, buf);
  drawSliderRow(LX, vgRowY(2), LW, "LF Gain", 0,
                (gainLF + 7) / 31.0f, buf,
                vgSelection == VG_LFGAIN,
                false,
                gainLF != SETTINGS_DEFAULT.gainLF,
                7.0f / 31.0f);

  // Rij 4: 5 toggle-knoppen
  drawVGToggleRow(LX, vgRowY(3), LW);

  display.setFont(NULL);
  display.endBuffering();
}

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 5a — Transformer settings
// ════════════════════════════════════════════════════════════════════════════
// Vier toggle-rijen:
//  Rij 0: Lf out   — -6 dB (default, relay uit) / 0 dB (relay aan)
//  Rij 1: Mf out   — -6 dB (default, relay uit) / 0 dB (relay aan)
//  Rij 2: Bypass LF — off (default) / on  [grayed out als Lf out actief]
//  Rij 3: Bypass MF — off (default) / on  [grayed out als Mf out actief]

// ════════════════════════════════════════════════════════════════════════════
//  Transformer scherm — layout:
//  Rij 0: [Lf out: -6dB / 0dB]  [Bypass LF: off / on]   — LF naast elkaar
//  Rij 1: [Mf out: -6dB / 0dB]  [Bypass MF: off / on]   — MF naast elkaar
//
//  Elke knop is een grote drukknop die bij tap cyclet naar de volgende waarde.
//  Bypass-knop is grayed als de bijbehorende out-knop geen effect heeft
//  (maar nog steeds zichtbaar — de relay staat in welke stand hij staat).
// ════════════════════════════════════════════════════════════════════════════
static void drawTransformerScreen() {
  currentScreen = SCR_TRANSFORMER;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("Adv. sett.");

  const int16_t LX  = 16;
  const int16_t LW  = SCREEN_W - 32;
  const int16_t GAP = 8;
  const int16_t cW  = (LW - GAP) / 2;

  // 2 rijen van 2 knoppen — max 1/3 breed, links uitgelijnd
  // LF rij bovenaan, MF rij daaronder
  const int16_t ADV_N  = 2;
  const int16_t ADV_BW = menuBtnW(ADV_N);   // max 1/3 = 256px
  const int16_t ADV_BH = MENU_BTN_H;
  const int16_t ADV_GAP = MENU_BTN_GAP;
  const int16_t ADV_ROW0 = CONT_Y + VG_PAD_TOP;
  const int16_t ADV_ROW1 = ADV_ROW0 + ADV_BH + VG_ROW_GAP;

  // drawAdvBtn: drawMenuTogBtn met grayed support
  auto drawAdvBtn = [&](int16_t bx, int16_t rowY,
                         const char* label, const char* value,
                         bool changed, bool grayed) {
    if (grayed) {
      uint16_t bg  = dimC(VGC_BG_NORM);
      uint16_t bdr = dimC(scale565(VGC_BDR_NORM, 50));
      uint16_t lc  = dimC(scale565(VGC_LBL_NORM, 50));
      display.fillRoundRect(bx, rowY, ADV_BW, ADV_BH, UI_CARD_R, bg);
      display.drawRoundRect(bx,   rowY,   ADV_BW,   ADV_BH,   UI_CARD_R, bdr);
      display.drawRoundRect(bx+1, rowY+1, ADV_BW-2, ADV_BH-2, UI_CARD_R, bdr);
      int16_t lblY = rowY + ADV_BH/3 + 2;
      int16_t valY = rowY + ADV_BH*2/3 + 8;
      AAFont_drawString(AA_XXS, label, bx, lblY, lc, bg, AA_CENTER, ADV_BW);
      AAFont_drawString(AA_XXS, value, bx, valY, lc, bg, AA_CENTER, ADV_BW);
    } else {
      drawMenuTogBtn(bx, rowY, ADV_BW, ADV_BH, label, value, changed);
    }
  };

  // Rij 0: LF
  drawAdvBtn(menuBtnX(0, ADV_BW), ADV_ROW0, "Lf out",    bypassLF ? "-6 dB" : (gainLFOUT <= -12 ? "-6 dB" : "0 dB"), gainLFOUT != SETTINGS_DEFAULT.gainLFOUT, bypassLF);
  drawAdvBtn(menuBtnX(1, ADV_BW), ADV_ROW0, "Bypass LF", bypassLF ? "on" : "off",               bypassLF  != SETTINGS_DEFAULT.bypassLF,  false);

  // Rij 1: MF
  drawAdvBtn(menuBtnX(0, ADV_BW), ADV_ROW1, "Mf out",    bypassMF ? "-6 dB" : (gainMFOUT <= -12 ? "-6 dB" : "0 dB"), gainMFOUT != SETTINGS_DEFAULT.gainMFOUT, bypassMF);
  drawAdvBtn(menuBtnX(1, ADV_BW), ADV_ROW1, "Bypass MF", bypassMF ? "on" : "off",               bypassMF  != SETTINGS_DEFAULT.bypassMF,  false);

  display.setFont(NULL);
  display.endBuffering();
}

static void handleTransformerTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx, ty)) { drawVolumeGainScreen(); return; }

  const int16_t LX  = 16;
  const int16_t LW  = SCREEN_W - 32;
  const int16_t GAP = 8;
  const int16_t cW  = (LW - GAP) / 2;

  const int16_t ADV_BW  = menuBtnW(2);
  const int16_t ADV_BH  = MENU_BTN_H;
  const int16_t ADV_GAP2 = MENU_BTN_GAP;
  const int16_t ADV_ROW0 = CONT_Y + VG_PAD_TOP;
  const int16_t ADV_ROW1 = ADV_ROW0 + ADV_BH + VG_ROW_GAP;
  const int16_t bx0 = menuBtnX(0, ADV_BW);
  const int16_t bx1 = menuBtnX(1, ADV_BW);

  // Rij 0: LF
  if (ty >= ADV_ROW0 && ty < ADV_ROW0 + ADV_BH) {
    if (tx >= bx0 && tx < bx0 + ADV_BW) {
      if (!bypassLF) { gainLFOUT = (gainLFOUT <= -12) ? 0 : -12; applyGainLFOUT(); saveSettings(); }
    } else if (tx >= bx1 && tx < bx1 + ADV_BW) {
      bypassLF = !bypassLF; applyBypassLF(); saveSettings();
    }
    drawTransformerScreen(); return;
  }

  // Rij 1: MF
  if (ty >= ADV_ROW1 && ty < ADV_ROW1 + ADV_BH) {
    if (tx >= bx0 && tx < bx0 + ADV_BW) {
      if (!bypassMF) { gainMFOUT = (gainMFOUT <= -12) ? 0 : -12; applyGainMFOUT(); saveSettings(); }
    } else if (tx >= bx1 && tx < bx1 + ADV_BW) {
      bypassMF = !bypassMF; applyBypassMF(); saveSettings();
    }
    drawTransformerScreen(); return;
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 5b — Offsets
// ════════════════════════════════════════════════════════════════════════════
static void drawOffsetsScreen() {
  currentScreen = SCR_OFFSETS;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("INPUT LEVELS");

  // ── Layout constanten ────────────────────────────────────────────────────
  const int16_t PAD      = VG_PAD;
  const int16_t TOTAL_W  = SCREEN_W - PAD * 2;      // 772
  const int16_t DIV_X    = PAD + TOTAL_W / 2;       // 400
  const int16_t DIV_GAP  = 8;
  const int16_t COL_W    = TOTAL_W / 2 - DIV_GAP;   // 378px
  const int16_t COL_L_X  = PAD;
  const int16_t COL_R_X  = DIV_X + DIV_GAP;
  const int16_t GAP      = 10;
  const int16_t COL_HDR_H = 36;                       // hoogte kolomtitel-zone
  const int16_t ROW_Y0   = CONT_Y + COL_HDR_H + 6;

  // ── Kolomtitels boven de sliders ─────────────────────────────────────────
  uint16_t hdrC = dimC(VGC_LBL_NORM);
  int16_t  hdrBaseline = CONT_Y + COL_HDR_H - 6;   // ruimte boven en onder
  AAFont_drawString(AA_XS, "Offset", COL_L_X + UI_PAD_L, hdrBaseline,
                    hdrC, C_BG, AA_LEFT, COL_W - UI_PAD_L);
  AAFont_drawString(AA_XS, "Startup volume", COL_R_X + UI_PAD_L, hdrBaseline,
                    hdrC, C_BG, AA_LEFT, COL_W - UI_PAD_L);
  // Hairline onder kolomtitels
  display.fillRect(COL_L_X, CONT_Y + COL_HDR_H, COL_W, 1, dimC(scale565(C_HAIRLINE, 90)));
  display.fillRect(COL_R_X, CONT_Y + COL_HDR_H, COL_W, 1, dimC(scale565(C_HAIRLINE, 90)));

  // ── Verticale divider ────────────────────────────────────────────────────
  display.fillRect(DIV_X, CONT_Y, 1, SCREEN_H - CONT_Y - 8, dimC(scale565(C_HAIRLINE, 80)));

  // ── Slider rijen ─────────────────────────────────────────────────────────
  char buf[20];
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    int16_t ry    = ROW_Y0 + i * (SR_H + GAP);
    int8_t  io_id = VG_IO_BASE + i;
    int8_t  sv_id = VG_SV_BASE + i;

    float off_pct = (inputOffset[i] + 12) / 24.0f;
    vgFormatVal(io_id, buf);
    drawSliderRow(COL_L_X, ry, COL_W, inputNames[i], 0,
                  off_pct, buf, vgSelection == io_id, true,
                  inputOffset[i] != SETTINGS_DEFAULT.inputOffset[i], 0.5f);

    if (i != (uint8_t)surroundInput) {
      float sv_pct = startupVolume[i] / 255.0f;
      vgFormatVal(sv_id, buf);
      drawSliderRow(COL_R_X, ry, COL_W, inputNames[i], 0,
                    sv_pct, buf, vgSelection == sv_id, false,
                    startupVolume[i] != SETTINGS_DEFAULT.startupVolume[i]);
    } else {
      uint16_t bg = dimC(VGC_BG_NORM);
      display.fillRoundRect(COL_R_X, ry, COL_W, SR_H, UI_CARD_R, bg);
      AAFont_drawString(AA_XXS, "n/a", COL_R_X, ry + SR_H/2 + 7,
                        dimC(C_GRAY_DIM), bg, AA_CENTER, COL_W);
    }
  }

  display.setFont(NULL);
  display.endBuffering();
}

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 6 — Input Namen + QWERTY
// ════════════════════════════════════════════════════════════════════════════
static void drawNamesScreen() {
  currentScreen = SCR_NAMES;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("INPUTS");

  const int16_t LIST_Y   = CONT_Y + 4;
  const int16_t ROW_SH   = 46;
  const int16_t NAME_X   = 130;
  const int16_t NAME_W   = 490;
  const int16_t BP_X     = 632;
  const int16_t BP_W     = 148;
  const int16_t BP_H     = 32;

  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    int16_t ry  = LIST_Y + i * ROW_SH;
    bool    sel = (namesSelection == (int8_t)i);
    bool    bp  = (i == surroundInput);

    // Filled-card stijl — zelfde als slider/display rijen
    uint16_t bg  = dimC(sel ? VGC_BG_SEL : VGC_BG_NORM);
    uint16_t bdr = sel ? dimC(VGC_BDR_SEL) : dimC(VGC_BDR_NORM);
    display.fillRoundRect(0, ry, SCREEN_W, ROW_SH, UI_CARD_R, bg);
    display.drawRoundRect(0,     ry,     SCREEN_W,     ROW_SH,     UI_CARD_R, bdr);
    display.drawRoundRect(1, ry + 1, SCREEN_W - 2, ROW_SH - 2, UI_CARD_R, bdr);

    char portStr[8]; getPortStr(i, portStr);
    uint16_t prtC  = sel ? dimC(VGC_LBL_SEL) : dimC(VGC_LBL_NORM);
    uint16_t nameC = sel ? dimC(VGC_VAL_SEL) : dimC(VGC_VAL_NORM);

    AAFont_drawString(AA_XXS, portStr, 24, ry + ROW_SH/2 + 7, prtC, bg, AA_LEFT, 80);

    char shown[18];
    if (sel) snprintf(shown, sizeof(shown), "%s|", editBuffer);
    else     strncpy(shown, inputNames[i], sizeof(shown)-1);
    shown[17] = '\0';
    AAFont_drawString(AA_XXS, shown, NAME_X, ry + ROW_SH/2 + 7, nameC, bg, AA_LEFT, NAME_W);

    int16_t by = ry + (ROW_SH - BP_H) / 2;
    uint16_t bFill = bp ? dimC(VGC_BG_SEL)  : dimC(VGC_BG_NORM);
    uint16_t bText = bp ? dimC(VGC_VAL_SEL) : dimC(VGC_LBL_NORM);
    display.fillRoundRect(BP_X, by, BP_W, BP_H, 5, bFill);
    display.drawRoundRect(BP_X, by, BP_W, BP_H, 5, bp ? dimC(VGC_BDR_SEL) : dimC(VGC_BDR_NORM));
    AAFont_drawString(AA_XXS, bp ? "BYPASS" : "SET BYPASS",
                      BP_X, by + BP_H/2 + 7, bText, bFill, AA_CENTER, BP_W);
  }

  display.setFont(NULL);
  display.endBuffering();
}

static void kbHandleKey(char c) {
  if (editLen < 15) { editBuffer[editLen++] = c; editBuffer[editLen] = '\0'; }
  kbShift = false;
  if (currentScreen == SCR_KB_EDIT) drawKbEditScreen();
  else drawNamesScreen();
}
static void kbHandleDel() {
  if (editLen > 0) editBuffer[--editLen] = '\0';
  if (currentScreen == SCR_KB_EDIT) drawKbEditScreen();
  else drawNamesScreen();
}
static void kbHandleOK() {
  if (namesSelection >= 0) {
    strncpy(inputNames[namesSelection], editBuffer, 15);
    inputNames[namesSelection][15] = '\0';
  }
  namesSelection=-1; editLen=0; editBuffer[0]='\0'; kbShift=false;
  saveSettings(); drawNamesScreen();
}

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 6b — Groot QWERTY keyboard voor ingangsnaam bewerken
// ════════════════════════════════════════════════════════════════════════════
// Layout (800×480):
//  Naamveld  : y=64..124   (60px hoog, volle breedte)
//  Rij Q     : y=134..198  (64px toetsen)
//  Rij A     : y=208..272
//  Rij Z     : y=282..346
//  Onderste  : y=356..412  (SHIFT | SPACE | DEL | OK)

#define QKB_KEY_H   64
#define QKB_KEY_GAP  8
#define QKB_ROW_GAP 10
#define QKB_ROW0_Y  134
#define QKB_ROW1_Y  (QKB_ROW0_Y + QKB_KEY_H + QKB_ROW_GAP)
#define QKB_ROW2_Y  (QKB_ROW1_Y + QKB_KEY_H + QKB_ROW_GAP)
#define QKB_BOT_Y   (QKB_ROW2_Y + QKB_KEY_H + QKB_ROW_GAP)
#define QKB_BOT_H   56
#define QKB_NAME_Y  (CONT_Y + 4)
#define QKB_NAME_H  60

static const char* QKB_ROWS_UPPER[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
static const char* QKB_ROWS_LOWER[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };

static int16_t qkbKeyX(int row, int col) {
  int nk = strlen(QKB_ROWS_UPPER[row]);
  int16_t tw = nk * (QKB_KEY_H + QKB_KEY_GAP) - QKB_KEY_GAP;
  return (SCREEN_W - tw) / 2 + col * (QKB_KEY_H + QKB_KEY_GAP);
}
static int16_t qkbRowY(int row) {
  return (row == 0) ? QKB_ROW0_Y : (row == 1) ? QKB_ROW1_Y : QKB_ROW2_Y;
}

static void drawKbEditScreen() {
  currentScreen = SCR_KB_EDIT;
  display.startBuffering();
  display.fillScreen(C_BG);

  // Header met ingangnummer
  char hdr[24];
  if (namesSelection >= 0) {
    char portStr[8]; getPortStr((uint8_t)namesSelection, portStr);
    snprintf(hdr, sizeof(hdr), "Rename: %s", portStr);
  } else {
    strncpy(hdr, "Rename", sizeof(hdr));
  }
  drawScreenHeader(hdr);

  // Naamveld — toont huidige editBuffer met cursor
  {
    char shown[18];
    snprintf(shown, sizeof(shown), "%s|", editBuffer);
    uint16_t bg  = dimC(VGC_BG_SEL);
    uint16_t bdr = dimC(VGC_BDR_SEL);
    display.fillRoundRect(16, QKB_NAME_Y, SCREEN_W - 32, QKB_NAME_H, UI_CARD_R, bg);
    display.drawRoundRect(16, QKB_NAME_Y, SCREEN_W - 32, QKB_NAME_H, UI_CARD_R, bdr);
    AAFont_drawString(AA_SM, shown, 16, QKB_NAME_Y + QKB_NAME_H/2 + 14,
                      dimC(VGC_VAL_SEL), bg, AA_CENTER, SCREEN_W - 32);
  }

  // Toetsenbord
  const char** rows = kbShift ? QKB_ROWS_UPPER : QKB_ROWS_LOWER;
  uint16_t keyBg  = dimC(VGC_BG_NORM);
  uint16_t keyBdr = dimC(VGC_BDR_NORM);
  uint16_t keyTxt = dimC(VGC_VAL_NORM);

  for (int r = 0; r < 3; r++) {
    int nk = strlen(rows[r]);
    for (int c = 0; c < nk; c++) {
      char lbl[2] = { rows[r][c], '\0' };
      int16_t kx = qkbKeyX(r, c), ky = qkbRowY(r);
      display.fillRoundRect(kx, ky, QKB_KEY_H, QKB_KEY_H, 6, keyBg);
      display.drawRoundRect(kx, ky, QKB_KEY_H, QKB_KEY_H, 6, keyBdr);
      AAFont_drawString(AA_XS, lbl, kx, ky + QKB_KEY_H/2 + 9, keyTxt, keyBg, AA_CENTER, QKB_KEY_H);
    }
  }

  // Onderste balk: SHIFT | SPACE | DEL | OK
  // Breedte: 800-32=768px, 4 secties met 8px gaps = 3*8=24, per sectie (768-24)/4=186
  const int16_t BX = 16, BW = SCREEN_W - 32;
  const int16_t BGAP = 8;
  const int16_t BSEC = (BW - 3*BGAP) / 4;
  const int16_t bShX = BX;
  const int16_t bSpX = BX + BSEC + BGAP;
  const int16_t bDeX = BX + 2*(BSEC + BGAP);
  const int16_t bOkX = BX + 3*(BSEC + BGAP);

  uint16_t shBg  = kbShift ? dimC(VGC_BG_SEL)  : dimC(VGC_BG_NORM);
  uint16_t shBdr = kbShift ? dimC(VGC_BDR_SEL)  : dimC(VGC_BDR_NORM);
  uint16_t shTxt = kbShift ? dimC(VGC_VAL_SEL)  : dimC(VGC_VAL_NORM);

  auto drawBotBtn = [&](int16_t bx, const char* lbl, uint16_t bg, uint16_t bdr, uint16_t tc) {
    display.fillRoundRect(bx, QKB_BOT_Y, BSEC, QKB_BOT_H, 6, bg);
    display.drawRoundRect(bx, QKB_BOT_Y, BSEC, QKB_BOT_H, 6, bdr);
    AAFont_drawString(AA_XXS, lbl, bx, QKB_BOT_Y + QKB_BOT_H/2 + 8, tc, bg, AA_CENTER, BSEC);
  };

  drawBotBtn(bShX, "Shift",  shBg,             shBdr,             shTxt);
  drawBotBtn(bSpX, "Space",  dimC(VGC_BG_NORM), dimC(VGC_BDR_NORM), dimC(VGC_VAL_NORM));
  drawBotBtn(bDeX, "< Del",  dimC(VGC_BG_NORM), dimC(VGC_BDR_NORM), dimC(VGC_VAL_NORM));
  drawBotBtn(bOkX, "OK",     dimC(VGC_BG_SEL),  dimC(VGC_BDR_SEL),  dimC(VGC_VAL_SEL));

  display.setFont(NULL);
  display.endBuffering();
}

static void handleKbEditTouch(int16_t tx, int16_t ty) {
  // Back knop → terug naar namen scherm zonder opslaan
  if (touchedHdrBack(tx, ty)) {
    namesSelection=-1; editLen=0; editBuffer[0]='\0'; kbShift=false;
    drawNamesScreen(); return;
  }

  const char** rows = kbShift ? QKB_ROWS_UPPER : QKB_ROWS_LOWER;

  // Toetsrijen
  for (int r = 0; r < 3; r++) {
    int nk = strlen(rows[r]);
    int16_t ry = qkbRowY(r);
    if (ty >= ry && ty < ry + QKB_KEY_H) {
      for (int c = 0; c < nk; c++) {
        int16_t kx = qkbKeyX(r, c);
        if (tx >= kx && tx < kx + QKB_KEY_H) {
          kbHandleKey(rows[r][c]); return;
        }
      }
    }
  }

  // Onderste balk
  if (ty >= QKB_BOT_Y && ty < QKB_BOT_Y + QKB_BOT_H) {
    const int16_t BX = 16, BW = SCREEN_W - 32;
    const int16_t BGAP = 8;
    const int16_t BSEC = (BW - 3*BGAP) / 4;
    const int16_t bShX = BX;
    const int16_t bSpX = BX + BSEC + BGAP;
    const int16_t bDeX = BX + 2*(BSEC + BGAP);
    const int16_t bOkX = BX + 3*(BSEC + BGAP);

    if (tx >= bShX && tx < bShX + BSEC) { kbShift = !kbShift; drawKbEditScreen(); return; }
    if (tx >= bSpX && tx < bSpX + BSEC) { kbHandleKey(' '); return; }
    if (tx >= bDeX && tx < bDeX + BSEC) { kbHandleDel();    return; }
    if (tx >= bOkX && tx < bOkX + BSEC) { kbHandleOK();     return; }
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 7 — Display instellingen
// ════════════════════════════════════════════════════════════════════════════
// ── Gedeelde hulpfunctie: teken filled-card basis voor instellingsrijen ────
// x=16, w=SCREEN_W-32, h=SR_H. Retourneert baseline y voor tekst.
static int16_t drawSettingsCardBase(int16_t y, float fillPct = 0.0f) {
  const int16_t X = 16, W = SCREEN_W - 32;
  uint16_t bg  = dimC(VGC_BG_NORM);
  uint16_t bdr = dimC(VGC_BDR_NORM);
  display.fillRoundRect(X, y, W, SR_H, UI_CARD_R, bg);
  // Positievulling (0.0 = geen vulling)
  if (fillPct > 0.005f) {
    int16_t fw = min((int16_t)(fillPct * (W - 4)), W - 4);
    display.fillRect(X + 2, y + 2, fw, SR_H - 4, dimC(VGC_FILL_NORM));
  }
  display.drawRoundRect(X,     y,     W,     SR_H,     UI_CARD_R, bdr);
  display.drawRoundRect(X + 1, y + 1, W - 2, SR_H - 2, UI_CARD_R, bdr);
  return y + SR_H/2 + 7;  // label baseline — verticaal gecentreerd
}

// drawDimRow: filled-card + label + −/waarde/+ knoppen  (sel=true → amber highlight)
static void drawDimRow(int16_t y, const char* label, const char* val, float fillPct = 0.0f, bool sel = false) {
  const int16_t X = 16, W = SCREEN_W - 32;
  uint16_t bg  = dimC(sel ? VGC_BG_SEL  : VGC_BG_NORM);
  uint16_t bdr = dimC(sel ? VGC_BDR_SEL : VGC_BDR_NORM);

  // Kaartbasis — direct tekenen om sel-kleuren te gebruiken
  display.fillRoundRect(X, y, W, SR_H, UI_CARD_R, bg);
  if (fillPct > 0.005f) {
    int16_t fw = min((int16_t)(fillPct * (W - 4)), W - 4);
    display.fillRect(X + 2, y + 2, fw, SR_H - 4, dimC(sel ? VGC_FILL_SEL : VGC_FILL_NORM));
  }
  display.drawRoundRect(X,     y,     W,     SR_H,     UI_CARD_R, bdr);
  display.drawRoundRect(X + 1, y + 1, W - 2, SR_H - 2, UI_CARD_R, bdr);

  int16_t lblBl = y + SR_H/2 + 7;
  AAFont_drawString(AA_XXS, label, X + UI_PAD_L, lblBl, dimC(sel ? VGC_LBL_SEL : VGC_LBL_NORM), bg, AA_LEFT, DS_LBL_W);

  // Waarde rechts — encoder bedient de slider, geen ± knoppen
  int16_t bl = y + SR_H/2 + 9;
  bool def = isDefaultDimSetting(label);
  uint16_t valC = sel ? dimC(VGC_VAL_SEL) : (def ? dimC(VGC_VAL_NORM) : dimC(VGC_VAL_CHG));
  AAFont_drawString(AA_XS, val, X + UI_PAD_L, bl + 4, valC, bg, AA_RIGHT, W - UI_PAD_L - UI_PAD_R);
}

// drawDispOptRow: filled-card + label + twee toggle-knoppen (zelfde stijl als VG toggle-kaarten)
// Display toggle-rij: grote knop dubbele hoogte, label boven waarde onder
#define DISP_TOG_H (2 * SR_H + DS_BTN_G)  // 118px — gelijk aan volume menu knoppen
static void drawDispOptRow(int16_t y, const char* label,
                            const char* optA, const char* optB, bool aActive, bool changed = false) {
  const int16_t X = 16, W = SCREEN_W - 32;
  const int16_t PAD = 8;
  int16_t by = y + PAD, bh = DISP_TOG_H - PAD*2;
  uint16_t bg  = dimC(VGC_BG_NORM);
  uint16_t bdr = dimC(VGC_BDR_NORM);
  uint16_t lc  = dimC(VGC_LBL_NORM);
  uint16_t vc  = changed ? dimC(VGC_VAL_CHG) : dimC(VGC_VAL_SEL);
  display.fillRoundRect(X, y, W, DISP_TOG_H, UI_CARD_R, bg);
  display.fillRoundRect(X, by, W, bh, UI_CARD_R, bg);
  display.drawRoundRect(X,   by,   W,   bh,   UI_CARD_R, bdr);
  display.drawRoundRect(X+1, by+1, W-2, bh-2, UI_CARD_R, bdr);
  int16_t lblY = by + bh/3 + 4;
  int16_t valY = by + bh*2/3 + 8;
  // Toon label en actieve waarde
  const char* val = aActive ? optA : optB;
  AAFont_drawString(AA_XXS, label, X + UI_PAD_L, lblY, lc, bg, AA_CENTER, W);
  AAFont_drawString(AA_XXS, val,   X + UI_PAD_L, valY, vc, bg, AA_CENTER, W);
}

// drawDispOptRowXW: halve-breedte toggle knop, dubbele hoogte, label/waarde stijl
static void drawDispOptRowXW(int16_t x, int16_t w, int16_t y, const char* label,
                              const char* optA, const char* optB, bool aActive, bool changed = false) {
  const int16_t PAD = 8;
  int16_t by  = y + PAD;
  int16_t bh  = DISP_TOG_H - PAD*2;
  uint16_t bg  = dimC(VGC_BG_NORM);
  uint16_t bdr = dimC(VGC_BDR_NORM);  // kader altijd zelfde — drukknop
  uint16_t lc  = dimC(VGC_LBL_NORM);
  uint16_t vc  = changed ? dimC(VGC_VAL_CHG) : dimC(VGC_VAL_SEL);
  display.fillRoundRect(x, y, w, DISP_TOG_H, UI_CARD_R, bg);
  display.fillRoundRect(x, by, w, bh, UI_CARD_R, bg);
  display.drawRoundRect(x,   by,   w,   bh,   UI_CARD_R, bdr);
  display.drawRoundRect(x+1, by+1, w-2, bh-2, UI_CARD_R, bdr);
  int16_t lblY = by + bh/3 + 2;
  int16_t valY = by + bh*2/3 + 8;
  const char* val = aActive ? optA : optB;
  AAFont_drawString(AA_XXS, label, x, lblY, lc, bg, AA_CENTER, w);
  AAFont_drawString(AA_XXS, val,   x, valY, vc, bg, AA_CENTER, w);
}


// drawDispActionRow: filled-card + label + enkele actie-knop
static void drawDispActionRow(int16_t y, const char* label, const char* action) {
  const int16_t X = 16, W = SCREEN_W - 32;
  uint16_t bg = dimC(VGC_BG_NORM);
  int16_t lblBl = drawSettingsCardBase(y);
  AAFont_drawString(AA_XXS, label, X + UI_PAD_L, lblBl, dimC(VGC_LBL_NORM), bg, AA_LEFT, DS_LBL_W);

  const int16_t oW = 130, btnH = SR_H - 20, btnY = y + 10;
  const int16_t oX = X + W - UI_PAD_R - oW;
  int16_t bl = y + SR_H/2 + 9;
  display.fillRoundRect(oX, btnY, oW, btnH, UI_BTN_R, dimC(VGC_BG_SEL));
  display.drawRoundRect(oX, btnY, oW, btnH, UI_BTN_R, dimC(VGC_BDR_SEL));
  AAFont_drawString(AA_XXS, action, oX, bl, dimC(VGC_VAL_SEL), dimC(VGC_BG_SEL), AA_CENTER, oW);
}


static uint16_t hueStepToColor565(uint8_t step) {
  const uint8_t STEPS = 48;
  uint16_t h = (uint16_t)step * 1536 / STEPS;  // 0..1535
  uint8_t region = h / 256;
  uint8_t rem = h % 256;

  uint8_t r=0, g=0, b=0;
  switch (region) {
    case 0: r = 255; g = rem; b = 0; break;         // R -> Y
    case 1: r = 255 - rem; g = 255; b = 0; break;   // Y -> G
    case 2: r = 0; g = 255; b = rem; break;         // G -> C
    case 3: r = 0; g = 255 - rem; b = 255; break;   // C -> B
    case 4: r = rem; g = 0; b = 255; break;         // B -> M
    default: r = 255; g = 0; b = 255 - rem; break;  // M -> R
  }

  // Houd kleuren wat rustiger/warmer zodat ze bij hi-fi displays passen.
  r = (uint8_t)((uint16_t)r * 88 / 100);
  g = (uint8_t)((uint16_t)g * 80 / 100);
  b = (uint8_t)((uint16_t)b * 88 / 100);

  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint8_t accentHueStepFromColor(uint16_t col) {
  const uint8_t STEPS = 48;
  uint8_t tr = (uint8_t)((((col >> 11) & 0x1F) * 255) / 31);
  uint8_t tg = (uint8_t)((((col >> 5)  & 0x3F) * 255) / 63);
  uint8_t tb = (uint8_t)((( col        & 0x1F) * 255) / 31);

  uint8_t best = 0;
  uint32_t bestD = 0xFFFFFFFFu;
  for (uint8_t i = 0; i < STEPS; i++) {
    uint16_t c = hueStepToColor565(i);
    uint8_t r = (uint8_t)((((c >> 11) & 0x1F) * 255) / 31);
    uint8_t g = (uint8_t)((((c >> 5)  & 0x3F) * 255) / 63);
    uint8_t b = (uint8_t)((( c        & 0x1F) * 255) / 31);
    int16_t dr = (int16_t)tr - r;
    int16_t dg = (int16_t)tg - g;
    int16_t db = (int16_t)tb - b;
    uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}



static const char* accentStepLabel(uint8_t step) {
  if (step < 2 || step >= 46) return "Red";
  if (step < 6)  return "Red-Orange";
  if (step < 10) return "Orange";
  if (step < 14) return "Amber";
  if (step < 18) return "Yellow";
  if (step < 22) return "Lime";
  if (step < 26) return "Green";
  if (step < 30) return "Cyan";
  if (step < 34) return "Blue";
  if (step < 38) return "Indigo";
  if (step < 42) return "Violet";
  return "Magenta";
}

static void drawColorTuneRow(int16_t y) {
  const int16_t X = 16, W = SCREEN_W - 32;
  uint16_t bg = dimC(VGC_BG_NORM);
  int16_t lblBl = drawSettingsCardBase(y);
  AAFont_drawString(AA_XXS, "Color tune", X + UI_PAD_L, lblBl, dimC(VGC_LBL_NORM), bg, AA_LEFT, DS_LBL_W);

  const int16_t btnW = 44, btnH = SR_H - 20, btnY = y + 10;
  const int16_t plX  = X + W - UI_PAD_R - btnW;
  const int16_t barW = plX - DS_CTRL_X - btnW - 12;
  const int16_t barX = DS_CTRL_X + btnW + 6;
  const int16_t barH = btnH, barY = btnY;
  int16_t bl = y + SR_H/2 + 9;

  // − knop
  display.fillRoundRect(DS_CTRL_X, btnY, btnW, btnH, UI_BTN_R, dimC(VGC_BG_SEL));
  display.drawRoundRect(DS_CTRL_X, btnY, btnW, btnH, UI_BTN_R, dimC(VGC_BDR_NORM));
  AAFont_drawString(AA_XXS, "-", DS_CTRL_X, bl, dimC(VGC_LBL_SEL), dimC(VGC_BG_SEL), AA_CENTER, btnW);
  // + knop
  display.fillRoundRect(plX, btnY, btnW, btnH, UI_BTN_R, dimC(VGC_BG_SEL));
  display.drawRoundRect(plX, btnY, btnW, btnH, UI_BTN_R, dimC(VGC_BDR_NORM));
  AAFont_drawString(AA_XXS, "+", plX, bl, dimC(VGC_LBL_SEL), dimC(VGC_BG_SEL), AA_CENTER, btnW);

  // Hue-balk
  for (int16_t col = 0; col < barW; col++) {
    uint8_t s = (uint8_t)((uint32_t)col * 48 / barW);
    uint16_t c = dimC(hueStepToColor565(s));
    int16_t top = barY, bot = barY + barH - 1;
    if (col < 3 || col >= barW - 3) { top += 2; bot -= 2; }
    else if (col < 5 || col >= barW - 5) { top += 1; bot -= 1; }
    display.drawFastVLine(barX + col, top, bot - top + 1, c);
  }
  display.drawRoundRect(barX, barY, barW, barH, UI_BTN_R, dimC(VGC_BDR_NORM));

  uint8_t step = accentHueStepFromColor(accentColor);
  int16_t indX = barX + (int16_t)((uint32_t)step * barW / 48) + barW / 96;
  indX = constrain(indX, barX + 2, barX + barW - 3);
  display.drawFastVLine(indX,     barY + 2, barH - 4, dimC(C_WHITE));
  display.drawFastVLine(indX - 1, barY + 4, barH - 8, dimC(scale565(C_WHITE, 120)));
  display.drawFastVLine(indX + 1, barY + 4, barH - 8, dimC(scale565(C_WHITE, 120)));
  char lbl[16];
  snprintf(lbl, sizeof(lbl), "%s", accentStepLabel(step));
  int16_t lx = constrain(indX - 30, barX + 2, barX + barW - 62);
  AAFont_drawString(AA_XXS, lbl, lx, barY + barH - 3, dimC(C_WHITE), 0x0000, AA_LEFT, 60);
}

static void drawColorPresetRow(int16_t y, uint8_t baseIdx, const char* label) {
  const int16_t X = 16, W = SCREEN_W - 32;
  uint16_t bg = dimC(VGC_BG_NORM);
  int16_t lblBl = drawSettingsCardBase(y);
  if (label && label[0])
    AAFont_drawString(AA_XXS, label, X + UI_PAD_L, lblBl, dimC(VGC_LBL_NORM), bg, AA_LEFT, DS_LBL_W);

  const int16_t oW = 88, oH = SR_H - 20, oG = UI_CTRL_GAP;
  for (int i = 0; i < 4; i++) {
    int idx = baseIdx + i;
    int16_t ox = DS_CTRL_X + i * (oW + oG);
    bool act = (accentColor == ACCENT_PRESETS[idx]);
    uint16_t fill = act ? dimC(ACCENT_PRESETS[idx]) : dimC(scale565(ACCENT_PRESETS[idx], 74));
    display.fillRoundRect(ox, y + 10, oW, oH, UI_BTN_R, fill);
    uint16_t bdr = act ? dimC(C_WHITE) : dimC(VGC_BDR_NORM);
    display.drawRoundRect(ox, y + 10, oW, oH, UI_BTN_R, bdr);
  }
}



static void drawThemeMotionScreen() {
  currentScreen = SCR_THEME;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("THEME");

  // Rij 0: Accent strength slider — geselecteerd voor encoder (dsSelection=0)
  char buf[20];
  sprintf(buf, "%u%%", (unsigned)mainAccentPct);
  drawDimRow(DS_ROW0_Y, "Accent strength", buf, mainAccentPct / 100.0f, dsSelection == 0);

  // Rij 1+2: Color presets
  drawColorPresetRow(DS_ROW1_Y, 0, "Accent presets");
  drawColorPresetRow(DS_ROW2_Y, 4, "");

  // Rij 3: Hue tune balk
  drawColorTuneRow(DS_ROW3_Y);

  // Knoppen onderaan: Font | Detail color | Large font without panel
  const int16_t TN = 3;
  const int16_t tbw = menuBtnW(TN);
  const char* fnt_val = (mainFontMode == FONT_MATRIX)   ? "Matrix"
                     : (mainFontMode == FONT_ORBITRON) ? "Orbitron"
                     :                                    "Standard";
  const char* det_val = detailColorFollow ? "Follow" : "Standard";
  const char* lfd_val = largeFontOnDim ? "on" : "off";
  drawMenuTogBtn(menuBtnX(0, tbw), MENU_BTN_Y, tbw, MENU_BTN_H,
                 "Font", fnt_val, mainFontMode != SETTINGS_DEFAULT.mainFontMode);
  drawMenuTogBtn(menuBtnX(1, tbw), MENU_BTN_Y, tbw, MENU_BTN_H,
                 "Detail color", det_val, detailColorFollow != SETTINGS_DEFAULT.detailColorFollow);
  drawMenuTogBtn(menuBtnX(2, tbw), MENU_BTN_Y, tbw, MENU_BTN_H,
                 "Large w/o panel", lfd_val, largeFontOnDim != SETTINGS_DEFAULT.largeFontOnDim);

  // Kleurpreview rechts van de knoppen — vervalt bij 3 knoppen (geen ruimte meer)

  display.endBuffering();
}

void drawDimSettingsScreen() {
  currentScreen = SCR_DISPLAY;
  if (dsSelection > 3) dsSelection = VG_NONE;  // sanity — geen ongeldige selectie bewaren
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("DISPLAY");
  char buf[16];

  // Rijen 0–3: sliders met encoder-selectie
  sprintf(buf, "%d%%",   uiBrightnessPct); drawDimRow(DS_ROW0_Y, "Brightness", buf, uiBrightnessPct / 100.0f,                          dsSelection == 0);
  sprintf(buf, "%d sec", dimDelaySec);     drawDimRow(DS_ROW1_Y, "Dim delay",  buf, constrain(dimDelaySec / 120.0f, 0.0f, 1.0f),        dsSelection == 1);
  sprintf(buf, "%d%%",   dimPercent);      drawDimRow(DS_ROW2_Y, "Dim level",  buf, dimPercent / 100.0f,                                 dsSelection == 2);
  sprintf(buf, "%d min", deepDimDelayMin); drawDimRow(DS_ROW3_Y, "Deep dim delay",   buf, constrain(deepDimDelayMin / 30.0f, 0.0f, 1.0f),     dsSelection == 3);

  // 3 knoppen onderaan — shared helpers
  const int16_t DN  = 3;
  const int16_t dbw = menuBtnW(DN);
  drawMenuTogBtn(menuBtnX(0,dbw), MENU_BTN_Y, dbw, MENU_BTN_H, "Detail panel", detailMode == 1 ? "on" : "off", detailMode != SETTINGS_DEFAULT.detailMode);
  drawMenuTogBtn(menuBtnX(1,dbw), MENU_BTN_Y, dbw, MENU_BTN_H, "Panel in dim", detailVisibleOnDim ? "on" : "off", detailVisibleOnDim != SETTINGS_DEFAULT.detailVisibleOnDim);
  drawMenuNavBtn(menuBtnX(2,dbw), MENU_BTN_Y, dbw, MENU_BTN_H, "Color & style  \xBB");

  display.endBuffering();
}


// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 8 — Systeem
// ════════════════════════════════════════════════════════════════════════════
static void drawSysRow(int16_t y, const char* label, const char* value,
                       bool showBadge = false, bool ok = true, uint16_t valCol = 0) {
  const int16_t X = 16, W = SCREEN_W - 32;
  uint16_t bg = dimC(VGC_BG_NORM);
  drawSettingsCardBase(y);
  int16_t bl = y + SR_H/2 + 9;
  AAFont_drawString(AA_XXS, label, X + UI_PAD_L, y + SR_H/2 + 7, dimC(VGC_LBL_NORM), bg, AA_LEFT, 200);

  int16_t valW = showBadge ? W - 280 : W - 220;
  uint16_t vc = valCol ? valCol : dimC(VGC_VAL_NORM);
  AAFont_drawString(AA_XXS, value, X + 220, bl, vc, bg, AA_LEFT, valW);

  if (showBadge) {
    uint16_t sCol = ok ? dimC(C_STATUS_OK) : dimC(C_STATUS_ERR);
    int16_t cx = X + W - UI_PAD_R - 10;
    int16_t cy = y + SR_H / 2 - 2;
    display.fillCircle(cx, cy, 4, sCol);
  }
}

// Systeem toggle-rij: grote knop, dubbele hoogte, label boven waarde onder
#define SYS_TOG_H (2 * SR_H + SYS_ROW_G)  // 118px — gelijk aan volume menu knoppen
static void drawSysToggleRow(int16_t y, const char* label, bool enabled) {
  const int16_t X = 16, W = SCREEN_W - 32;
  const int16_t PAD = 8;
  int16_t by = y + PAD, bh = SYS_TOG_H - PAD*2;
  uint16_t bg  = dimC(VGC_BG_NORM);
  uint16_t bdr = dimC(VGC_BDR_NORM);  // kader altijd zelfde — drukknop
  uint16_t lc  = dimC(VGC_LBL_NORM);
  uint16_t vc  = dimC(VGC_VAL_SEL);   // waarde altijd helder, label zegt wat het is
  display.fillRoundRect(X, y, W, SYS_TOG_H, UI_CARD_R, bg);
  display.fillRoundRect(X, by, W, bh, UI_CARD_R, bg);
  display.drawRoundRect(X,   by,   W,   bh,   UI_CARD_R, bdr);
  display.drawRoundRect(X+1, by+1, W-2, bh-2, UI_CARD_R, bdr);
  int16_t lblY = by + bh/3 + 4;
  int16_t valY = by + bh*2/3 + 8;
  AAFont_drawString(AA_XXS, label,                   X + UI_PAD_L, lblY, lc, bg, AA_CENTER, W);
  AAFont_drawString(AA_XXS, enabled ? "on" : "off",  X + UI_PAD_L, valY, vc, bg, AA_CENTER, W);
}

void drawSystemScreen() {
  currentScreen = SCR_SYS;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("SYSTEM");
  char buf[48];

  // Info rijen bovenaan
  const int16_t SYS_ROW1 = CONT_Y + 4;
  const int16_t SYS_ROW2 = SYS_ROW1 + SR_H + SYS_ROW_G;
  const int16_t SYS_ROW3 = SYS_ROW2 + SR_H + SYS_ROW_G;

  drawSysRow(SYS_ROW1, "Firmware build", FW_VERSION "  " __DATE__ " " __TIME__);
  drawSysRow(SYS_ROW2, "Boot health", diagBootDegraded ? "Degraded boot detected" : "Normal boot",
             true, !diagBootDegraded);
  sprintf(buf, "%u", (unsigned)diagRecoveryAttempts);
  drawSysRow(SYS_ROW3, "Recovery tries", buf);

  // 4 knoppen onderaan
  const int16_t SYS_N2 = 4;
  const int16_t sbw = menuBtnW(SYS_N2);
  drawMenuTogBtn(menuBtnX(0,sbw), MENU_BTN_Y, sbw, MENU_BTN_H, "Warm stby", warmStandbyEnabled ? "Enabled" : "Disabled");
  // Warm trigger: grayed als warm standby niet actief is
  if (warmStandbyEnabled) {
    drawMenuTogBtn(menuBtnX(1,sbw), MENU_BTN_Y, sbw, MENU_BTN_H, "Warm trig", warmTrigRelayClosed ? "on" : "off");
  } else {
    // Grayed — zelfde stijl als adv.sett. grayed knoppen
    int16_t bx = menuBtnX(1,sbw), by = MENU_BTN_Y, bh = MENU_BTN_H;
    uint16_t bg  = dimC(C_CARD_BG);
    uint16_t bdr = dimC(scale565(VGC_BDR_NORM, 50));
    uint16_t lc  = dimC(scale565(VGC_LBL_NORM, 50));
    display.fillRoundRect(bx, by, sbw, bh, UI_CARD_R, bg);
    display.drawRoundRect(bx,   by,   sbw,   bh,   UI_CARD_R, bdr);
    display.drawRoundRect(bx+1, by+1, sbw-2, bh-2, UI_CARD_R, bdr);
    int16_t lblY = by + bh/3 + 2, valY = by + bh*2/3 + 8;
    AAFont_drawString(AA_XXS, "Warm trig", bx, lblY, lc, bg, AA_CENTER, sbw);
    AAFont_drawString(AA_XXS, warmTrigRelayClosed ? "on" : "off", bx, valY, lc, bg, AA_CENTER, sbw);
  }
  drawMenuTogBtn(menuBtnX(2,sbw), MENU_BTN_Y, sbw, MENU_BTN_H, "Rem. trigger", remoteTriggerEnabled ? "Enabled" : "Disabled");
  drawMenuNavBtn(menuBtnX(3,sbw), MENU_BTN_Y, sbw, MENU_BTN_H, "Diagnostics \xBB");
  display.endBuffering();
}

static void drawDiagnosticsScreen() {
  currentScreen = SCR_DIAG;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("DIAGNOSTICS");

  char buf[96];
  uint32_t sec=millis()/1000, m=sec/60; sec%=60;
  uint32_t h=m/60; m%=60; uint32_t d=h/24; h%=24;
  if (d>0) sprintf(buf,"%lud  %02lu:%02lu:%02lu",d,h,m,sec);
  else     sprintf(buf,"%02lu:%02lu:%02lu",h,m,sec);
  drawSysRow(SYS_Y1, "Uptime", buf);

  bool volOk  = scanI2C(I2C_ADDR_VOLUME,  "Volume");
  bool ctrlOk = scanI2C(I2C_ADDR_CONTROL, "Control");
  mcpLinkOk = volOk && ctrlOk;

  uint16_t volErr = getI2CErrorCount(I2C_ADDR_VOLUME);
  uint16_t ctlErr = getI2CErrorCount(I2C_ADDR_CONTROL);
  uint16_t volHr  = getI2CErrorsLastHour(I2C_ADDR_VOLUME);
  uint16_t ctlHr  = getI2CErrorsLastHour(I2C_ADDR_CONTROL);

  bool healthy = mcpLinkOk && (volErr == 0) && (ctlErr == 0);
  snprintf(buf, sizeof(buf), healthy ? "All checks pass" : "Issues detected — tap Now");
  drawSysRow(SYS_Y2, "Health", buf, false, healthy);

  sprintf(buf, "MCP23017  0x%02X", I2C_ADDR_VOLUME);
  drawSysRow(SYS_Y3, "I²""C Volume",  buf, true, volOk);
  sprintf(buf, "MCP23017  0x%02X", I2C_ADDR_CONTROL);
  drawSysRow(SYS_Y4, "I²""C Control", buf, true, ctrlOk);

  snprintf(buf, sizeof(buf), "V: %u total / %u in 1h   C: %u total / %u in 1h",
           (unsigned)volErr, (unsigned)volHr, (unsigned)ctlErr, (unsigned)ctlHr);
  drawSysRow(SYS_Y5, "I²""C errors", buf, false, true,
             (volHr == 0 && ctlHr == 0) ? dimC(VGC_VAL_NORM) : dimC(C_STATUS_ERR));

  I2CRecentEvent ev;
  if (getI2CRecentEvent(0, &ev)) {
    uint32_t ago = (millis() - ev.ms) / 1000;
    const char* dev = (ev.addr == I2C_ADDR_VOLUME) ? "Vol" : (ev.addr == I2C_ADDR_CONTROL) ? "Ctl" : "I2C";
    if (ev.reg == 0xFF) snprintf(buf, sizeof(buf), "%s scan err=%u  %lus ago", dev, (unsigned)ev.err, (unsigned long)ago);
    else                snprintf(buf, sizeof(buf), "%s reg=0x%02X err=%u  %lus ago", dev, ev.reg, (unsigned)ev.err, (unsigned long)ago);
  } else {
    snprintf(buf, sizeof(buf), "No I2C errors recorded since boot");
  }
  drawSysRow(SYS_Y6, "Last event", buf);

  // Refresh status — toon tijd van vorige refresh, niet de huidige
  // diagLastRefreshMs wordt VOOR het tekenen gezet, dus "0s ago" is altijd fout.
  // We tonen de tijd die verstreken is NADAT het scherm getekend is.
  // Oplossing: sla de tijd op NA het tekenen, in drawDiagnosticsScreen zelf niet.
  // Toon gewoon "auto-refresh every 5s" zonder de timing.
  drawDispActionRow(SYS_Y7, "Auto-refresh every 5s", "Now");
  display.endBuffering();
}

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN 9 — IR Learn
// ════════════════════════════════════════════════════════════════════════════
#define IR_BTN_H        SR_H
#define IR_ACT_Y        SYS_Y5
#define IR_ACT_GAP      20
#define IR_ACT_W        300
#define IR_LEARN_X      ((SCREEN_W - (IR_ACT_W * 2 + IR_ACT_GAP)) / 2)
#define IR_SAVE_X       (IR_LEARN_X + IR_ACT_W + IR_ACT_GAP)

// Reset bewust kleiner en uit de primaire leerflow gehouden.
#define IR_RST_W        150
#define IR_RST_H        36
#define IR_RST_X        (SCREEN_W - 16 - IR_RST_W)
#define IR_RST_Y        (SCREEN_H - 16 - IR_RST_H)

static void drawIRDataRow() {
  char buf[96];
  uint8_t i = irLearnTarget;
  snprintf(buf, sizeof(buf), "P:%u   A:0x%04X   C:0x%02X",
           (unsigned)irLearnProtocol, irLearnAddress, irLearnCommand);
  drawSysRow(SYS_Y2, "IR data", buf);

  bool hasCode = irProtocolMap[i] != IR_PROTOCOL_UNKNOWN;
  const char* status = irLearnArmed ? "Learning active — press remote button"
                                    : (hasCode ? "Code present" : "No code learned yet");
  uint16_t col = irLearnArmed ? dimC(C_VAL_SEL) : (hasCode ? dimC(C_STATUS_OK) : dimC(C_GRAY));
  drawSysRow(SYS_Y3, "Status", status, false, true, col);
}

static void drawIRActionButtons(bool saveEnabled) {
  // Learn code: primaire actie
  uint16_t lBg  = irLearnArmed ? dimC(scale565(accent(), 50)) : dimC(C_CARD_BG);
  uint16_t lBdr = irLearnArmed ? dimC(accent())               : dimC(C_VAL_SEL);
  uint16_t lTxt = irLearnArmed ? dimC(0xFFFFu)                : dimC(C_LBL_SEL);
  const char* lLabel = irLearnArmed ? "Armed" : "Learn";
  display.fillRoundRect(IR_LEARN_X, IR_ACT_Y, IR_ACT_W, IR_BTN_H, 6, lBg);
  display.drawRoundRect(IR_LEARN_X, IR_ACT_Y, IR_ACT_W, IR_BTN_H, 6, lBdr);
  AAFont_drawString(AA_XXS, lLabel, IR_LEARN_X, IR_ACT_Y + IR_BTN_H/2 + 9,
                    lTxt, lBg, AA_CENTER, IR_ACT_W);

  // Save code: secundaire actie — blauw palet ipv groen
  uint16_t sBg  = saveEnabled ? dimC(VGC_BG_SEL)   : dimC(scale565(C_CARD_BG, 80));
  uint16_t sBdr = saveEnabled ? dimC(VGC_BDR_SEL)   : dimC(C_HAIRLINE);
  uint16_t sTxt = saveEnabled ? dimC(VGC_VAL_SEL)   : dimC(scale565(C_LBL, 40));
  display.fillRoundRect(IR_SAVE_X, IR_ACT_Y, IR_ACT_W, IR_BTN_H, 6, sBg);
  display.drawRoundRect(IR_SAVE_X, IR_ACT_Y, IR_ACT_W, IR_BTN_H, 6, sBdr);
  AAFont_drawString(AA_XXS, "Save", IR_SAVE_X, IR_ACT_Y + IR_BTN_H/2 + 9,
                    sTxt, sBg, AA_CENTER, IR_ACT_W);
}

static void drawIRResetButton() {
  // Kleine, gedempte destructive actie buiten de leerflow.
  uint16_t bg  = dimC(scale565(C_CARD_BG, 78));
  uint16_t bdr = dimC(scale565(C_STATUS_ERR, 42));
  uint16_t txt = dimC(scale565(C_STATUS_ERR, 72));
  display.fillRoundRect(IR_RST_X, IR_RST_Y, IR_RST_W, IR_RST_H, 5, bg);
  display.drawRoundRect(IR_RST_X, IR_RST_Y, IR_RST_W, IR_RST_H, 5, bdr);
  AAFont_drawString(AA_XXS, "Reset", IR_RST_X, IR_RST_Y + IR_RST_H/2 + 8,
                    txt, bg, AA_CENTER, IR_RST_W);

  AAFont_drawString(AA_XXS, "Hold Reset for 1s to clear all codes", 16, IR_RST_Y + IR_RST_H/2 + 8,
                    dimC(scale565(C_GRAY, 70)), C_BG, AA_LEFT, SCREEN_W - 32 - IR_RST_W - 24);
}

static void drawIRLearnScreen() {
  currentScreen = SCR_IR_LEARN;
  display.startBuffering();
  display.fillScreen(C_BG);
  drawScreenHeader("IR LEARN");

  uint8_t i = irLearnTarget;
  bool hasCode = irProtocolMap[i] != IR_PROTOCOL_UNKNOWN;

  drawSysRow(SYS_Y1, "Function", IR_TARGET_NAMES[i]);
  drawIRDataRow();
  drawIRActionButtons(hasCode && !irLearnArmed);
  drawIRResetButton();
  display.endBuffering();
}

// ════════════════════════════════════════════════════════════════════════════
//  Touch handlers per scherm
// ════════════════════════════════════════════════════════════════════════════
static void handleMainTouch(int16_t tx, int16_t ty) {
  notifyTouch();
  // Any touch on main screen opens the menu
  showMainMenu();
}

static void handleMenuTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { restoreMainDisplay(); return; }
  for (uint8_t i = 0; i < MENU_N; i++) {
    int16_t col=i%MC_COLS, row=i/MC_COLS;
    int16_t cx=MC_X(col), cy=MC_Y(row);
    if (tx>=cx && tx<=cx+MC_W && ty>=cy && ty<=cy+MC_H) {
      switch (i) {
        case 0: vgSelection=VG_NONE; drawVolumeGainScreen(); break;
        case 1: vgSelection=VG_NONE; drawOffsetsScreen(); break;
        case 2:
          namesSelection=-1; editLen=0; editBuffer[0]='\0'; kbShift=false;
          drawNamesScreen(); break;
        case 3: drawDimSettingsScreen(); break;
        case 4: drawSystemScreen(); break;
        case 5: drawIRLearnScreen(); break;
      }
      return;
    }
  }
}

// Herteken alleen een toggle-rij

static void handleVolumeGainTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { vgSelection=VG_NONE; drawMainMenu(); return; }

  // Slider rijen 0–3: aantikken selecteert voor encoder
  static const int8_t rowId[3] = { VG_MAXVOL, VG_BALANCE, VG_LFGAIN };
  for (uint8_t r = 0; r < 3; r++) {
    int16_t ry = vgRowY(r);
    if (ty >= ry && ty < ry + SR_H) {
      int8_t hitId = rowId[r];
      int8_t prev  = vgSelection;
      vgSelection  = (vgSelection == hitId) ? VG_NONE : hitId;
      if (prev >= 0 && prev != vgSelection) vgRedrawRow(prev);
      if (vgSelection >= 0)                 vgRedrawRow(vgSelection);
      return;
    }
  }

  // Toggle-rij: 5 knoppen
  {
    const int16_t N  = 5;
    const int16_t bw = menuBtnW(N);

    auto redrawToggles = [&]() {
      display.startBuffering();
      display.fillRect(16, MENU_BTN_Y, SCREEN_W-32, MENU_BTN_H, C_BG);
      drawVGToggleRow(16, MENU_BTN_Y, SCREEN_W-32);
      display.endBuffering();
    };

    if (ty >= MENU_BTN_Y && ty < MENU_BTN_Y + MENU_BTN_H) {
      for (int8_t i = 0; i < N; i++) {
        int16_t bx = menuBtnX(i, bw);
        if (tx >= bx && tx < bx + bw) {
          switch (i) {
            case 0: muteOnStartup = !muteOnStartup; flushSettings(); redrawToggles(); return;
            case 1: volUnitsMode = (uint8_t)((volUnitsMode + 1) % 3); saveSettings(); redrawToggles(); return;
            case 2: volumeCurve = (uint8_t)((volumeCurve + 1) % 3); saveSettings(); redrawToggles(); return;
            case 3: encSensitivity = (uint8_t)((encSensitivity + 1) % 3); saveSettings(); redrawToggles(); return;
            case 4: drawTransformerScreen(); return;
          }
        }
      }
    }
  }
}


static void handleOffsetsTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { vgSelection=VG_NONE; drawMainMenu(); return; }

  const int16_t PAD     = VG_PAD;
  const int16_t TOTAL_W = SCREEN_W - PAD * 2;
  const int16_t DIV_X   = PAD + TOTAL_W / 2;
  const int16_t DIV_GAP = 8;
  const int16_t COL_W   = TOTAL_W / 2 - DIV_GAP;
  const int16_t COL_L_X = PAD;
  const int16_t COL_R_X = DIV_X + DIV_GAP;
  const int16_t GAP     = 10;
  const int16_t ROW_Y0  = CONT_Y + 42;

  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    int16_t ry = ROW_Y0 + i * (SR_H + GAP);
    if (ty < ry || ty >= ry + SR_H) continue;

    bool inLeft  = (tx >= COL_L_X && tx < DIV_X);
    bool inRight = (tx >= COL_R_X && tx < COL_R_X + COL_W);

    int8_t newSel = VG_NONE;
    if (inLeft)                              newSel = VG_IO_BASE + i;
    else if (inRight && i != surroundInput)  newSel = VG_SV_BASE + i;

    if (newSel == VG_NONE) return;

    int8_t prev = vgSelection;
    vgSelection = (vgSelection == newSel) ? VG_NONE : newSel;
    if (prev >= 0 && prev != vgSelection) vgRedrawRow(prev);
    if (vgSelection >= 0)                 vgRedrawRow(vgSelection);
    return;
  }
}
static void handleNamesTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) {
    namesSelection=-1; editLen=0; editBuffer[0]='\0'; kbShift=false;
    drawMainMenu(); return;
  }
  const int16_t LIST_Y=CONT_Y+4, ROW_SH=46;
  const int16_t NAME_X=130, NAME_W=490;
  const int16_t BP_X=632, BP_W=148;
  for (uint8_t i=0; i<INPUT_COUNT; i++) {
    int16_t ry=LIST_Y+i*ROW_SH;
    if (ty>=ry && ty<ry+ROW_SH) {
      // Dedicated bypass toggle column
      if (tx >= BP_X && tx <= BP_X + BP_W) {
        surroundInput = i;
        saveSettings();
        drawNamesScreen();
        return;
      }

      // Name area: tap opent groot QWERTY keyboard scherm
      if (tx < NAME_X || tx > NAME_X + NAME_W) return;

      // Sla eventuele lopende bewerking op
      if (namesSelection>=0 && namesSelection!=(int8_t)i && editLen>0) {
        strncpy(inputNames[namesSelection], editBuffer, 15);
        inputNames[namesSelection][15]='\0';
      }
      namesSelection=i;
      strncpy(editBuffer, inputNames[i], 15); editBuffer[15]='\0';
      editLen=strlen(editBuffer); kbShift=false;
      drawKbEditScreen(); return;
    }
  }
  if (namesSelection<0) return;
  const char** rows = kbShift ? KB_UPPER : KB_LOWER;
  for (int row=0; row<KB_NROWS; row++) {
    int nk=strlen(rows[row]);
    for (int col=0; col<nk; col++) {
      int16_t kx=kbKeyX(row,col), ky=kbKeyY(row);
      if (tx>=kx && tx<=kx+KB_KEY_H && ty>=ky && ty<=ky+KB_KEY_H) {
        kbHandleKey(rows[row][col]); return;
      }
    }
  }
  if (ty>=KB_SP_Y && ty<=KB_SP_Y+KB_SP_H) {
    if (tx>=KB_SHIFT_X&&tx<=KB_SHIFT_X+KB_SHIFT_W) { kbShift=!kbShift; drawNamesScreen(); return; }
    if (tx>=KB_SPC_X  &&tx<=KB_SPC_X+KB_SPC_W)     { kbHandleKey(' '); return; }
    if (tx>=KB_DEL_X  &&tx<=KB_DEL_X+KB_DEL_W)     { kbHandleDel();    return; }
    if (tx>=KB_OK_X   &&tx<=KB_OK_X+KB_OK_W)        { kbHandleOK();     return; }
  }
}

static void handleDisplayTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { dsSelection = VG_NONE; drawMainMenu(); return; }

  const int16_t LX = 16, LW = SCREEN_W - 32;

  // Rijen 0–3: aantikken selecteert slider voor encoder
  static const int16_t rowY[4] = { DS_ROW0_Y, DS_ROW1_Y, DS_ROW2_Y, DS_ROW3_Y };
  for (int r = 0; r < 4; r++) {
    if (ty >= rowY[r] && ty < rowY[r] + SR_H) {
      int8_t prev = dsSelection;
      dsSelection = (dsSelection == r) ? VG_NONE : (int8_t)r;
      if (prev != dsSelection) {
        saveSettings();
        drawDimSettingsScreen();
        if (r == 0 || prev == 0) setScreenBrightness(activeBrightness());
      }
      return;
    }
  }

  // Knoppen onderaan — shared helpers
  if (ty >= MENU_BTN_Y && ty < MENU_BTN_Y + MENU_BTN_H) {
    const int16_t DN  = 3;
    const int16_t dbw = menuBtnW(DN);
    for (int8_t i = 0; i < DN; i++) {
      if (tx >= menuBtnX(i,dbw) && tx < menuBtnX(i,dbw)+dbw) {
        switch(i) {
          case 0: detailMode = (detailMode==1)?0:1; detailPanelSuppressedByDim=false; saveSettings(); drawDimSettingsScreen(); return;
          case 1: detailVisibleOnDim = !detailVisibleOnDim; saveSettings(); drawDimSettingsScreen(); return;
          case 2: dsSelection = VG_NONE; drawThemeMotionScreen(); return;
        }
      }
    }
    return;
  }
}


static void handleThemeMotionTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { drawDimSettingsScreen(); return; }

  // Rij 0: Accent strength — tap selecteert voor encoder
  if (ty >= DS_ROW0_Y && ty < DS_ROW0_Y + SR_H) {
    int8_t prev = dsSelection;
    dsSelection = (dsSelection == 0) ? VG_NONE : 0;
    if (prev != dsSelection) { saveSettings(); drawThemeMotionScreen(); }
    return;
  }

  // Rij 1: Color presets 0..3
  if (ty >= DS_ROW1_Y && ty < DS_ROW1_Y + SR_H) {
    const int16_t oW=88, oG=UI_CTRL_GAP;
    for (int i=0;i<4;i++) {
      int16_t ox=DS_CTRL_X+i*(oW+oG);
      if (tx>=ox&&tx<=ox+oW) { accentColor=ACCENT_PRESETS[i]; saveSettings(); drawThemeMotionScreen(); break; }
    }
    return;
  }

  // Rij 2: Color presets 4..7
  if (ty >= DS_ROW2_Y && ty < DS_ROW2_Y + SR_H) {
    const int16_t oW=88, oG=UI_CTRL_GAP;
    for (int i=0;i<4;i++) {
      int16_t ox=DS_CTRL_X+i*(oW+oG);
      if (tx>=ox&&tx<=ox+oW) { accentColor=ACCENT_PRESETS[i+4]; saveSettings(); drawThemeMotionScreen(); break; }
    }
    return;
  }

  // Rij 3: Hue tune balk
  if (ty >= DS_ROW3_Y && ty < DS_ROW3_Y + SR_H) {
    const int16_t btnW_t = 44, gap_t = 6;
    const int16_t mnX_t  = DS_CTRL_X;
    const int16_t barX   = mnX_t + btnW_t + gap_t;
    const int16_t barW   = (16 + SCREEN_W - 32) - barX - gap_t - btnW_t - 16;
    const int16_t plX_t  = barX + barW + gap_t;
    bool ch = false;
    uint8_t step = accentHueStepFromColor(accentColor);
    if (tx >= mnX_t && tx <= mnX_t + btnW_t) {
      step = (uint8_t)((step + 47) % 48); ch = true;
    } else if (tx >= plX_t && tx <= plX_t + btnW_t) {
      step = (uint8_t)((step + 1) % 48); ch = true;
    } else if (tx >= barX && tx <= barX + barW) {
      step = (uint8_t)constrain((int32_t)(tx - barX) * 48 / barW, 0, 47); ch = true;
    }
    if (ch) { accentColor = hueStepToColor565(step); saveSettings(); drawThemeMotionScreen(); }
    return;
  }

  // Knoppen onderaan: Font | Detail color | Large on dim
  if (ty >= MENU_BTN_Y && ty < MENU_BTN_Y + MENU_BTN_H) {
    const int16_t TN  = 3;
    const int16_t tbw = menuBtnW(TN);
    if (tx >= menuBtnX(0,tbw) && tx < menuBtnX(0,tbw)+tbw) {
      mainFontMode = (uint8_t)((mainFontMode + 1) % 3); saveSettings(); drawThemeMotionScreen(); return;
    }
    if (tx >= menuBtnX(1,tbw) && tx < menuBtnX(1,tbw)+tbw) {
      detailColorFollow = !detailColorFollow; saveSettings(); drawThemeMotionScreen(); return;
    }
    if (tx >= menuBtnX(2,tbw) && tx < menuBtnX(2,tbw)+tbw) {
      largeFontOnDim = !largeFontOnDim; saveSettings(); drawThemeMotionScreen(); return;
    }
  }
}

static void handleSystemTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { drawMainMenu(); return; }

  // 4 knoppen onderaan
  const int16_t SYS_N3 = 4;
  const int16_t sbw2 = menuBtnW(SYS_N3);
  if (ty >= MENU_BTN_Y && ty < MENU_BTN_Y + MENU_BTN_H) {
    for (int8_t i = 0; i < SYS_N3; i++) {
      if (tx >= menuBtnX(i,sbw2) && tx < menuBtnX(i,sbw2)+sbw2) {
        switch(i) {
          case 0: warmStandbyEnabled = !warmStandbyEnabled; saveSettings(); drawSystemScreen(); return;
          case 1: if (warmStandbyEnabled) { warmTrigRelayClosed = !warmTrigRelayClosed; saveSettings(); drawSystemScreen(); } return;
          case 2:
            remoteTriggerEnabled = !remoteTriggerEnabled;
            if (!remoteTriggerEnabled) {
              digitalWrite(PIN_REMOTE_TRIG, LOW);
            } else {
              resetRemoteTrigger();  // Herstart delay-venster zodat trigger alsnog fired
            }
            saveSettings(); drawSystemScreen(); return;
          case 3: diagLastRefreshMs = millis(); drawDiagnosticsScreen(); return;
        }
      }
    }
  }
}

static void handleDiagnosticsTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { drawSystemScreen(); return; }

  // Now-knop: directe refresh met visuele feedback
  // drawDispActionRow tekent de knop rechts in SYS_Y7 rij
  const int16_t X = 16, W = SCREEN_W - 32;
  const int16_t oW = 130, btnH = SR_H - 20, btnY = SYS_Y7 + 10;
  const int16_t oX = X + W - UI_PAD_R - oW;
  if (ty >= SYS_Y7 && ty <= SYS_Y7 + SR_H && tx >= oX && tx <= oX + oW) {
    // Visuele feedback: knop flitst
    display.startBuffering();
    display.fillRoundRect(oX, btnY, oW, btnH, UI_BTN_R, dimC(VGC_BDR_SEL));
    AAFont_drawString(AA_XXS, "Refreshing...", oX, SYS_Y7 + SR_H/2 + 9,
                      dimC(VGC_BG_NORM), dimC(VGC_BDR_SEL), AA_CENTER, oW);
    display.endBuffering();
    drawDiagnosticsScreen();
    return;
  }
}



static void updateIRLearnRows() {
  // Herteken alleen de variabele IR-gegevens + actieknoppen.
  drawIRDataRow();

  uint8_t target = irLearnTarget;
  bool hasLearnedForTarget = irProtocolMap[target] != IR_PROTOCOL_UNKNOWN;
  drawIRActionButtons(hasLearnedForTarget && !irLearnArmed);
  drawIRResetButton();
}


static void performIRResetAllCodes() {
  resetIRMappingsToDefaults();
  irLearnArmed = false;
  flushSettings();
  drawIRLearnScreen();
}

static void handleIRLearnTouch(int16_t tx, int16_t ty) {
  if (touchedHdrBack(tx,ty)) { irLearnArmed = false; drawMainMenu(); return; }

  // Function rij — volgende slot
  if (ty >= SYS_Y1 && ty <= SYS_Y1 + SR_H) {
    irLearnTarget = (uint8_t)((irLearnTarget + 1) % IR_ACTION_COUNT);
    irLearnArmed = false;
    drawIRLearnScreen();
    return;
  }

  // Learn + Save op dezelfde rij
  if (ty >= IR_ACT_Y && ty <= IR_ACT_Y + IR_BTN_H) {
    uint8_t i = irLearnTarget;
    bool hasCode = irProtocolMap[i] != IR_PROTOCOL_UNKNOWN;

    if (tx >= IR_LEARN_X && tx <= IR_LEARN_X + IR_ACT_W) {
      irLearnArmed = !irLearnArmed;
      updateIRLearnRows();
      return;
    }

    if (tx >= IR_SAVE_X && tx <= IR_SAVE_X + IR_ACT_W) {
      if (hasCode && !irLearnArmed) {
        flushSettings();
        uint16_t bg = dimC(C_STATUS_OK_FILL);
        display.fillRoundRect(IR_SAVE_X, IR_ACT_Y, IR_ACT_W, IR_BTN_H, UI_BTN_R + 1, bg);
        display.drawRoundRect(IR_SAVE_X, IR_ACT_Y, IR_ACT_W, IR_BTN_H, UI_BTN_R + 1, dimC(C_STATUS_OK));
        AAFont_drawString(AA_XXS, "Saved!", IR_SAVE_X, IR_ACT_Y + IR_BTN_H/2 + 9,
                          dimC(C_WHITE), bg, AA_CENTER, IR_ACT_W);
        irSavedFeedbackMs = millis();  // updateDisplay wist na IR_SAVED_FEEDBACK_MS
      }
      return;
    }
  }

}

// ════════════════════════════════════════════════════════════════════════════
//  Encoder R — aanpassen geselecteerde parameter in Volume & Gain scherm
// ════════════════════════════════════════════════════════════════════════════
void adjustVGSelection(int delta) {
  if ((currentScreen != SCR_VOLUME_GAIN && currentScreen != SCR_OFFSETS) || vgSelection == VG_NONE) return;

  // Encoder-aanpassingen moeten ook persistent worden opgeslagen.
  // Voorheen werden sliderwaarden wel gewijzigd maar niet altijd naar flash gemarkeerd.
  uint8_t prevMaxVolume = maxVolume;
  uint8_t prevStartupVolume = startupVolume[0];  // dummy, tracked per-index below
  int8_t  prevBalanceOffset = balanceOffset;
  int8_t  prevGainLF = gainLF;
  int8_t  prevInputOffset = 0;
  bool    trackInputOffset = false;
  uint8_t prevSV = 0;
  bool    trackSV = false;
  if (vgSelection >= VG_IO_BASE && vgSelection < VG_IO_BASE + INPUT_COUNT) {
    int i = vgSelection - VG_IO_BASE;
    prevInputOffset = inputOffset[i];
    trackInputOffset = true;
  }
  if (vgSelection >= VG_SV_BASE && vgSelection < VG_SV_BASE + INPUT_COUNT) {
    int i = vgSelection - VG_SV_BASE;
    prevSV = startupVolume[i];
    trackSV = true;
  }

  // Pas waarde aan in één stap (delta kan meerdere stappen zijn bij snelle draai)
  switch (vgSelection) {
    case VG_MAXVOL:
      maxVolume = (uint8_t)constrain((int)maxVolume + delta, 0, 255);
      // Zorg dat startupVolumes en savedVolumes de nieuwe limiet respecteren
      for (uint8_t i = 0; i < INPUT_COUNT; i++) {
        if (startupVolume[i] > maxVolume) startupVolume[i] = maxVolume;
        if (i != surroundInput && savedVolume[i] > maxVolume) savedVolume[i] = maxVolume;
      }
      // Clip huidig volume direct als het boven de nieuwe limiet zit
      if (currentVolume > maxVolume && !isMuted) {
        currentVolume = maxVolume;
        applyVolume();
      }
      break;
    case VG_BALANCE:
      balanceOffset = (int8_t)constrain((int)balanceOffset + delta, -BALANCE_MAX, BALANCE_MAX);
      applyVolume();
      break;
    case VG_LFGAIN:
      gainLF = (int8_t)constrain((int)gainLF + delta, GAIN_MIN[0], GAIN_MAX[0]);
      applyGainLF();
      break;
    default:
      if (vgSelection >= VG_IO_BASE && vgSelection < VG_IO_BASE + INPUT_COUNT) {
        int i = vgSelection - VG_IO_BASE;
        inputOffset[i] = (int8_t)constrain((int)inputOffset[i] + delta, -12, 12);
        applyVolume();
      } else if (vgSelection >= VG_SV_BASE && vgSelection < VG_SV_BASE + INPUT_COUNT) {
        int i = vgSelection - VG_SV_BASE;
        startupVolume[i] = (uint8_t)constrain((int)startupVolume[i] + delta, 0, (int)maxVolume);
      }
      break;
  }

  bool changed = (maxVolume != prevMaxVolume)
              || (balanceOffset != prevBalanceOffset)
              || (gainLF != prevGainLF)
              || (trackInputOffset && inputOffset[vgSelection - VG_IO_BASE] != prevInputOffset)
              || (trackSV && startupVolume[vgSelection - VG_SV_BASE] != prevSV);
  if (changed) saveSettings();

  vgRedrawRow(vgSelection);  // Één hertekening na alle stappen
}

// Wrapper voor Encoder.cpp — backward-compatible
void adjustGainSelection(int delta) {
  adjustVGSelection(delta);
}

bool isGainScreenActive() {
  return ((currentScreen == SCR_VOLUME_GAIN || currentScreen == SCR_OFFSETS) && vgSelection != VG_NONE);
}

// ── Display scherm encoder ────────────────────────────────────────────────
void adjustDSSelection(int delta) {
  if (dsSelection == VG_NONE) return;
  // SCR_THEME: encoder stuurt accent strength (dsSelection=0)
  if (currentScreen == SCR_THEME && dsSelection == 0) {
    mainAccentPct = (uint8_t)constrain((int)mainAccentPct + delta * 5, 70, 100);
    saveSettings();
    char buf[20]; sprintf(buf, "%u%%", (unsigned)mainAccentPct);
    const int16_t LX = 16, LW = SCREEN_W - 32;
    display.startBuffering();
    display.fillRect(LX, DS_ROW0_Y, LW, SR_H, C_BG);
    drawDimRow(DS_ROW0_Y, "Accent strength", buf, mainAccentPct / 100.0f, true);
    // Herteken preview tekst
    const int16_t TN=2, tbw=menuBtnW(TN);
    int16_t prvX = menuBtnX(TN,tbw)+MENU_BTN_GAP, prvW=SCREEN_W-16-prvX;
    display.fillRect(prvX, MENU_BTN_Y, prvW, MENU_BTN_H, C_BG);
    AAFont_drawString(AA_XS, "Phi Reference", prvX, MENU_BTN_Y+MENU_BTN_H/2+8,
                      dimC(scale565(accentColor, 96)), C_BG, AA_LEFT, prvW);
    display.endBuffering();
    return;
  }
  if (currentScreen != SCR_DISPLAY) return;

  switch (dsSelection) {
    case 0: uiBrightnessPct = (uint8_t) constrain((int)uiBrightnessPct + delta * 5, 20, 100); break;
    case 1: dimDelaySec     = (uint16_t)constrain((int)dimDelaySec     + delta * 5, 10, 600); break;
    case 2: dimPercent      = (uint8_t) constrain((int)dimPercent      + delta * 5,  5,  95); break;
    case 3: deepDimDelayMin = (uint8_t) constrain((int)deepDimDelayMin + delta,      1,  61); break;
    default: return;
  }
  saveSettings();

  // Herbereken helderheid direct als brightness of dim level verandert
  if (dsSelection == 0) setScreenBrightness(activeBrightness());
  else if (dsSelection == 2) setScreenBrightness((uint8_t)((uint32_t)activeBrightness() * dimPercent / 100));

  // Herteken alleen de gewijzigde rij
  const int16_t LX = 16, LW = SCREEN_W - 32;
  static const int16_t rowY[4] = { DS_ROW0_Y, DS_ROW1_Y, DS_ROW2_Y, DS_ROW3_Y };
  static const char* rowLbl[4] = { "Brightness", "Dim delay", "Dim level", "Deep dim delay" };
  char buf[16];
  if      (dsSelection == 0) sprintf(buf, "%d%%",   uiBrightnessPct);
  else if (dsSelection == 1) sprintf(buf, "%d sec", dimDelaySec);
  else if (dsSelection == 2) sprintf(buf, "%d%%",   dimPercent);
  else if (deepDimDelayMin >= 61) strcpy(buf, "Off");
  else                       sprintf(buf, "%d min", deepDimDelayMin);

  float pct = 0.0f;
  if      (dsSelection == 0) pct = uiBrightnessPct / 100.0f;
  else if (dsSelection == 1) pct = constrain(dimDelaySec / 120.0f, 0.0f, 1.0f);
  else if (dsSelection == 2) pct = dimPercent / 100.0f;
  else                       pct = constrain(deepDimDelayMin / 61.0f, 0.0f, 1.0f);

  display.startBuffering();
  display.fillRect(LX, rowY[dsSelection], LW, SR_H, C_BG);
  drawDimRow(rowY[dsSelection], rowLbl[dsSelection], buf, pct, true);
  display.endBuffering();
}

bool isDimScreenActive() {
  return ((currentScreen == SCR_DISPLAY || currentScreen == SCR_THEME) && dsSelection != VG_NONE);
}

bool isMainScreen() {
  return (currentScreen == SCR_MAIN);
}

// ════════════════════════════════════════════════════════════════════════════
//  Public API
// ════════════════════════════════════════════════════════════════════════════
void setSwitchingInput(bool s) { switchingInput = s; }

void startWarmup() {
  inStandby = false;
  // MCP scan en reinitialisatie volgt pas na REMOTE_TRIG_DELAY in updateDisplay()
  // — voeding is nu net aangezet, chips nog niet stabiel
  currentScreen = SCR_WARMUP;
  warmupStartTime = millis();
  setScreenBrightness(activeBrightness());
  drawWarmupScreen();
}

// Publieke wrapper voor startVolRamp — aanroepbaar vanuit Controls.cpp
void beginVolRamp(uint8_t target) {
  startVolRamp(target);
}

// Onderbreek lopende ramp — encoder of andere volume-actie neemt over
void cancelVolRamp() {
  if (!volRampActive) return;
  volRampActive = false;
  applyVolume();  // hardware direct naar currentVolume (met balans/offset)
}

// ════════════════════════════════════════════════════════════════════════════
//  FAULT SCREEN — shown when hardware init fails
// ════════════════════════════════════════════════════════════════════════════
void drawFaultScreen(const char* msg, uint8_t retryCountdown) {
  display.fillScreen(C_BG);

  // Amber waarschuwingsbalk bovenaan
  const uint16_t C_AMBER = 0xFCC0u;
  display.fillRect(0, 0, SCREEN_W, 6, C_AMBER);

  // Title
  AAFont_drawString(AA_SM, "Connection fault - Pre amp control not found", 0, 148,
                   fa_dim(C_AMBER, 220), C_BG, AA_CENTER, SCREEN_W);

  // Hairline
  display.drawFastHLine(SCREEN_W/2 - 180, 192, 360, fa_dim(C_AMBER, 60));

  // Technical detail (msg parameter)
  AAFont_drawString(AA_XS, msg, 0, 212,
                   dimC(C_GRAY), C_BG, AA_CENTER, SCREEN_W);

  // Status text
  char buf[48];
  if (retryCountdown > 0) {
    snprintf(buf, sizeof(buf), "Retrying in %d s...", retryCountdown);
  } else {
    snprintf(buf, sizeof(buf), "Attempting to reconnect...");
  }
  AAFont_drawString(AA_XXS, buf, 0, 256,
                   dimC(C_GRAY_DIM), C_BG, AA_CENTER, SCREEN_W);

  // Progress dots
  const int16_t DOT_Y = 304;
  const int16_t DOT_R = 4;
  const int16_t DOT_GAP = 22;
  const uint8_t N_DOTS = 10;
  int16_t dotStartX = SCREEN_W/2 - (N_DOTS-1)*DOT_GAP/2;
  for (uint8_t i = 0; i < N_DOTS; i++) {
    uint8_t filled = (retryCountdown == 0) ? i < 3 :
                     (uint8_t)(i < (N_DOTS - retryCountdown % N_DOTS));
    uint16_t col = filled ? fa_dim(C_AMBER, 160) : fa_dim(C_GRAY_DIM, 80);
    display.fillCircle(dotStartX + i * DOT_GAP, DOT_Y, DOT_R, col);
  }

  // Bottom hint
  AAFont_drawString(AA_XXS, "Check control- and powersupply connections - system will attempt to reconnect",
                   0, SCREEN_H - 32,
                   fa_dim(C_GRAY_DIM, 130), C_BG, AA_CENTER, SCREEN_W);

  // Amber bottom bar
  display.fillRect(0, SCREEN_H - 6, SCREEN_W, 6, fa_dim(C_AMBER, 80));
}

void initDisplay() {
  AAFont_setDisplay(&display);
  display.begin(); display.setRotation(1);
  touch.begin();
  backlight.begin();
  bootStartTime = millis();
  screenBrightness = activeBrightness();
  setScreenBrightness(activeBrightness());
  // isMuted wordt NIET hier gezet — staat is eigendom van de relay state machine
  // in Controls.cpp (applyRelayState). initControls() heeft dit al correct gezet.
  currentScreen = SCR_BOOT;
  drawBootScreen();
}

void triggerVolBlink() {
  volBlinkCount        = 6;
  volBlinkOn           = true;   // start zichtbaar, eerste stap wist na interval
  volBlinkLastMs       = millis();
  volBlinkNeedsRestore = false;
}

void updateVolumeDisplay() {
  if (currentScreen == SCR_BOOT) return;
  if (currentScreen == SCR_MAIN) {
    drawMainStatusChips();
    if (balShowMs > 0) {
      drawBalanceBar();
      if (detailPanelActive()) redrawDetailAttenCard();
    } else {
      redrawVolumeZone();
    }
  }
}

bool isBalanceBarShowing() {
  return balShowMs > 0;
}

void showBalanceBar() {
  if (currentScreen != SCR_MAIN) return;
  if (balShowMs == 0) balDisplayX = -1.0f;  // Nieuwe sessie — start schoon
  balShowMs = millis();
  drawMainStatusChips();
  drawBalanceBar();
  if (detailPanelActive()) redrawDetailBalanceCard();
}

void updateInputDisplay() {
  if (currentScreen == SCR_MAIN && detailPanelActive()) drawSimpleDetailPanel();
}

void showBalanceScreen() {
  vgSelection = VG_BALANCE;
  drawVolumeGainScreen();
}

void showMainMenu() { drawMainMenu(); }

void showIRLearnScreen() { drawIRLearnScreen(); }

bool isIRLearnScreenActive() { return currentScreen == SCR_IR_LEARN; }

void onIRLearnSample(uint8_t protocol, uint16_t address, uint8_t command, uint32_t rawData, bool isRepeat) {
  irLearnProtocol = protocol;
  irLearnAddress  = address;
  irLearnCommand  = command;
  irLearnRaw      = rawData;
  irLearnRepeat   = isRepeat;
  irLearnHasData  = true;

  if (currentScreen == SCR_IR_LEARN && irLearnArmed && !isRepeat && protocol != IR_PROTOCOL_UNKNOWN) {
    uint8_t i = irLearnTarget;

    // Verwijder conflicterende mappings: als hetzelfde address+command al aan een
    // andere actie gekoppeld is, wint de nieuwe toewijzing. matchIRAction() loopt
    // sequentieel, dus een duplicate op een lager index zou de nieuwe actie altijd
    // overschaduwen. Reset conflicterende slots naar UNKNOWN zodat ze worden overgeslagen.
    for (uint8_t j = 0; j < IR_ACTION_COUNT; j++) {
      if (j != i &&
          irAddressMap[j] == address &&
          irCommandMap[j] == command) {
        irProtocolMap[j] = IR_PROTOCOL_UNKNOWN;
        irAddressMap[j]  = 0x0000;
        irCommandMap[j]  = 0x00;
      }
    }

    irProtocolMap[i] = protocol;
    irAddressMap[i]  = address;
    irCommandMap[i]  = command;
    irLearnArmed     = false;
    // Niet automatisch opslaan — gebruiker gebruikt Save code knop
  }

  if (currentScreen == SCR_IR_LEARN) updateIRLearnRows();
}

void restoreMainDisplay() {
  vgSelection = VG_NONE;
  detailPanelSuppressedByDim = false;  // zeker weten dat suppress gereset is
  drawMainScreen();
}


// ════════════════════════════════════════════════════════════════════════════
//  FREQ ANIMATION — flowing wave mesh background
// ════════════════════════════════════════════════════════════════════════════



struct FaWave { float sp, spd, amp, ph, eFreq, eSpd; uint8_t alpha; };
static const FaWave FA_WAVES[3] = {
  { 0.018f, 0.48f, 80.0f, 0.00f, 0.009f, 0.12f, 205 },
  { 0.024f, 0.34f, 50.0f, 2.10f, 0.011f, 0.15f, 115 },
  { 0.013f, 0.20f, 34.0f, 4.40f, 0.007f, 0.09f,  65 },
};

static float fa_wy(const FaWave& w, float x, float t) {
  float p = x * w.sp - t * w.spd + w.ph;
  return (sinf(p) + sinf(p*1.618f+1.2f)*0.38f + sinf(p*0.618f-0.8f)*0.22f) * w.amp;
}
static float fa_env(const FaWave& w, float x, float t) {
  return (sinf(x*w.eFreq - t*w.eSpd)*0.5f+0.5f) * (sinf(t*0.31f+w.ph)*0.18f+0.82f);
}


// drawFreqAnimScreen + handleFreqAnimTouch verwijderd

void updateDisplay() {
  uint32_t now = millis();

  tickFade();     // Non-blocking helderheid fade — altijd als eerste
  tickVolRamp();  // Non-blocking volume ramp-up na standby-exit

  if (currentScreen == SCR_BOOT) {
    // Static screen — nothing to update each frame
    if ((now - bootStartTime) >= BOOT_DURATION_MS) {
      currentVolume = savedVolume[currentInput]; applyVolume();  // Session-start volume = startup baseline
      currentScreen = SCR_MAIN; lastActivityMs = millis(); drawMainScreen();
    }
    return;
  }
  if (currentScreen == SCR_WARMUP) {
    // Wacht tot voeding stabiel is, initialiseer dan MCP chips opnieuw
    if ((now - warmupStartTime) >= REMOTE_TRIG_DELAY) {
      // MCP chips waren spanningloos tijdens standby — volledig herintialiseren
      mcpLinkOk = reinitMCP();
      syncRelayState(isMuted);
      applyGainAll();
      setInput(currentInput);   // herstelt ook bypass bits
      applyVolume();
      currentScreen = SCR_MAIN; lastActivityMs = millis(); drawMainScreen();
    }
    return;
  }

  static uint32_t lastSelAnimMs = 0;
  if ((currentScreen == SCR_VOLUME_GAIN || currentScreen == SCR_OFFSETS) && vgSelection >= 0
      && (now - lastSelAnimMs) >= 90) {
    lastSelAnimMs = now;
    vgRedrawRow(vgSelection);
  }

  // Diagnostics: automatisch verversen elke 5 seconden
  static uint32_t lastDiagRefreshMs = 0;
  if (currentScreen == SCR_DIAG && (now - lastDiagRefreshMs) >= 5000) {
    lastDiagRefreshMs = now;
    // Korte flash van de Now-knop als visuele refresh-indicator
    const int16_t X = 16, W = SCREEN_W - 32;
    const int16_t oW = 130, btnH = SR_H - 20, btnY = SYS_Y7 + 10;
    const int16_t oX = X + W - UI_PAD_R - oW;
    display.startBuffering();
    display.fillRoundRect(oX, btnY, oW, btnH, UI_BTN_R, dimC(VGC_BDR_SEL));
    AAFont_drawString(AA_XXS, "Refreshing...", oX, SYS_Y7 + SR_H/2 + 9,
                      dimC(VGC_BG_NORM), dimC(VGC_BDR_SEL), AA_CENTER, oW);
    display.endBuffering();
    drawDiagnosticsScreen();
  }

  // IR-learn "Saved!" feedback: wis na IR_SAVED_FEEDBACK_MS en herteken rijen
  if (irSavedFeedbackMs > 0 && currentScreen == SCR_IR_LEARN
      && (now - irSavedFeedbackMs) >= IR_SAVED_FEEDBACK_MS) {
    irSavedFeedbackMs = 0;
    updateIRLearnRows();
  }

  if (currentScreen==SCR_MAIN && balShowMs>0
      && (now-balShowMs)>=profileBalanceVisibleMs()) {
    hideBalanceBar();
  }

  if (currentScreen != SCR_MAIN && currentScreen != SCR_BOOT && currentScreen != SCR_WARMUP
      && lastActivityMs > 0 && (now - lastActivityMs) >= MENU_AUTOCLOSE_MS) {
    restoreMainDisplay();
  }

  if (currentScreen == SCR_MAIN && !mainCalmMode && !inStandby && lastActivityMs > 0
      && screenBrightness == activeBrightness()
      && (now - lastActivityMs) >= profileCalmAfterMs()
      && (now - lastActivityMs) < (uint32_t)dimDelaySec * 1000UL) {
    mainCalmMode = true;
  }

  static uint32_t lastCalmAnimMs = 0;
  if (currentScreen == SCR_MAIN && !inStandby && !switchingInput && screenBrightness == activeBrightness()
      && (now - lastCalmAnimMs) >= calmAnimIntervalMs()) {
    lastCalmAnimMs = now;
    // Enkelvoudig calm target: diep genoeg voor merkbaar effect op naam en volume
    uint8_t target = mainCalmMode ? 220 : 0;
    if (mainCalmBlend != target) {
      if (mainCalmBlend < target) {
        uint8_t remain = (uint8_t)(target - mainCalmBlend);
        uint8_t step = calmAnimStep(remain);
        uint16_t next = (uint16_t)mainCalmBlend + step;
        mainCalmBlend = (next > target) ? target : (uint8_t)next;
      } else {
        uint8_t remain = mainCalmBlend;
        uint8_t step = calmAnimStep(remain);
        mainCalmBlend = (mainCalmBlend > step) ? (uint8_t)(mainCalmBlend - step) : 0;
      }
      // Herteken alle elementen die meelopen met mainCalmBlend.
      // startBuffering/endBuffering zorgt voor één atomaire refresh per frame —
      // zonder dit triggert elke AAFont_drawString-aanroep een losse dsi_lcdDrawImage,
      // wat flicker geeft én de refresh thread (osPriorityHigh) zo vaak wekt dat
      // de main loop te weinig CPU krijgt om de dim-timer te halen.
      display.startBuffering();
      drawMainStatusChips();
      drawMainHairline();
      if (balShowMs == 0) {
        drawInputName();
        drawMainPrimaryValue();
      }
      display.endBuffering();
      // Detailpanel hier nooit per animatieframe hertekenen.
      // Ook in de "terug van calm" fase (mainCalmMode=false, blend nog actief)
      // gaf dat sporadisch knipperen direct na instellingen/wijzigingen.
      // Detailwaarden worden via gerichte updatepaden bijgewerkt.
    }
  }

  // Crossfade state machine — fade via zwart tussen klein en groot font.
  // Fase 1 FADE_OUT: panelBlend 255→0 (content verdwijnt), font nog oud
  // Snap:            fontIsLarge wisselt, content hertekend terwijl panelBlend=0
  // Fase 2 FADE_IN:  panelBlend 0→255 (nieuwe content verschijnt)
  static uint32_t lastBlendAnimMs = 0;
  if (currentScreen == SCR_MAIN && !inStandby && !switchingInput
      && (now - lastBlendAnimMs) >= 20) {
    lastBlendAnimMs = now;

    bool panelHidden = !detailPanelActive();
    bool wantLarge   = largeVolumeFontActive();

    // Trigger crossfade als gewenste staat verschilt van huidige en we niet al faden
    if (wantLarge != fontIsLarge && xfadeState == XF_IDLE) {
      xfadeTargetLarge = wantLarge;
      xfadeState       = XF_FADE_OUT;
    }

    switch (xfadeState) {
      case XF_FADE_OUT: {
        uint8_t step = blendAnimStep(panelBlend, 0);
        panelBlend = panelBlend > step ? panelBlend - step : 0;
        // Herteken content gedimmed
        if (balShowMs == 0) {
          display.startBuffering();
          clearMainContentZone();
          drawMainPrimaryValue();
          drawSimpleDetailPanel();
          if (shimmerActive) drawShimmerOnHairline(); else drawMainHairline();
          display.endBuffering();
        }
        if (panelBlend == 0) {
          fontIsLarge = xfadeTargetLarge;
          xfadeState  = XF_FADE_IN;
        }
        break;
      }
      case XF_FADE_IN: {
        uint8_t step = blendAnimStep(panelBlend, 255);
        uint16_t n = (uint16_t)panelBlend + step;
        panelBlend = n >= 255 ? 255 : (uint8_t)n;
        if (balShowMs == 0) {
          display.startBuffering();
          clearMainContentZone();
          drawMainPrimaryValue();
          drawSimpleDetailPanel();
          if (shimmerActive) drawShimmerOnHairline(); else drawMainHairline();
          display.endBuffering();
        }
        if (panelBlend == 255) xfadeState = XF_IDLE;
        break;
      }
      case XF_IDLE:
        break;
    }

    // Panelblend ook bijhouden als er geen crossfade loopt maar panel
    // aan/uit gaat zonder fontwissel (detailMode toggle, wake zonder fontchange)
    if (xfadeState == XF_IDLE) {
      uint8_t pbTarget = panelHidden ? 0 : 255;
      if (panelBlend != pbTarget) {
        uint8_t step = blendAnimStep(panelBlend, pbTarget);
        if (panelBlend < pbTarget) {
          uint16_t n = (uint16_t)panelBlend + step;
          panelBlend = n >= pbTarget ? pbTarget : (uint8_t)n;
        } else {
          panelBlend = panelBlend > step ? panelBlend - step : 0;
        }
        if (balShowMs == 0) {
          display.startBuffering();
          drawSimpleDetailPanel();
          display.endBuffering();
        }
      }
    }
  }

  // Volume blink — 3× knipperen bij max volume bereikt
  if (currentScreen == SCR_MAIN && !inStandby && volBlinkCount > 0
      && xfadeState == XF_IDLE && balShowMs == 0) {
    if ((now - volBlinkLastMs) >= VOL_BLINK_INTERVAL_MS) {
      volBlinkLastMs = now;
      volBlinkOn     = !volBlinkOn;
      volBlinkCount--;
      display.startBuffering();
      clearPrimaryValueZone();
      if (volBlinkOn) drawMainPrimaryValue();
      display.endBuffering();
      if (volBlinkCount == 0 && !volBlinkOn) {
        volBlinkNeedsRestore = true;  // reeks eindigde op 'uit', herstel nodig
      }
    }
  }
  // Herstel volume na blinkreeks die eindigde op 'uit'
  if (volBlinkNeedsRestore && xfadeState == XF_IDLE && balShowMs == 0
      && currentScreen == SCR_MAIN && !inStandby) {
    volBlinkNeedsRestore = false;
    volBlinkOn = true;
    display.startBuffering();
    clearPrimaryValueZone();
    drawMainPrimaryValue();
    display.endBuffering();
  }

  // Hairline shimmer — beweegt af en toe over de hairline (min 30s interval)
  // Alleen op hoofdscherm, niet in standby, niet tijdens dim-animatie
  if (currentScreen == SCR_MAIN && !inStandby && screenBrightness > 0) {
    static uint32_t lastShimmerFrameMs  = 0;
    static uint8_t  lastHairlineBright  = 255;  // track brightness voor sync hertekening
    const uint32_t  SHIMMER_FRAME_MS    = 22;
    const float     SHIMMER_SPEED       = 5.2f;
    const int16_t   X0 = 60, X1 = 740;
    const uint32_t  SHIMMER_MIN_GAP_MS  = 30000UL;
    const uint32_t  SHIMMER_MAX_GAP_MS  = 90000UL;

    // Herteken hairline zodra screenBrightness veranderd is — houdt dimming synchroon
    if (screenBrightness != lastHairlineBright && !shimmerActive) {
      lastHairlineBright = screenBrightness;
      display.startBuffering();
      drawMainHairline();
      display.endBuffering();
    }

    if (shimmerNextMs == 0) {
      shimmerNextMs = now + SHIMMER_MIN_GAP_MS
                    + (uint32_t)(((uint32_t)random(0, 1000) * (SHIMMER_MAX_GAP_MS - SHIMMER_MIN_GAP_MS)) / 1000UL);
    }

    if (!shimmerActive && now >= shimmerNextMs) {
      // Start nieuwe shimmer — willekeurig links of rechts
      shimmerDir = (random(0, 2) == 0) ? 1.0f : -1.0f;
      shimmerX   = (shimmerDir > 0) ? (float)(X0 - 50) : (float)(X1 + 50);
      shimmerActive = true;
    }

    if (shimmerActive && (now - lastShimmerFrameMs) >= SHIMMER_FRAME_MS) {
      lastShimmerFrameMs = now;
      shimmerX += shimmerDir * SHIMMER_SPEED;

      bool done = (shimmerDir > 0 && shimmerX > (float)(X1 + 50))
               || (shimmerDir < 0 && shimmerX < (float)(X0 - 50));

      // Alleen eigen flush als crossfade niet al een flush doet deze frame
      if (xfadeState == XF_IDLE) {
        display.startBuffering();
        drawMainHairline();
        if (!done) drawShimmerOnHairline();
        display.endBuffering();
      }
      lastHairlineBright = screenBrightness;

      if (done) {
        shimmerActive    = false;
        shimmerLastEndMs = now;
        shimmerNextMs = now + SHIMMER_MIN_GAP_MS
                      + (uint32_t)(((uint32_t)random(0, 1000) * (SHIMMER_MAX_GAP_MS - SHIMMER_MIN_GAP_MS)) / 1000UL);
      }
    }
  }

  if (inStandby && standbyTextShowMs>0
      && (now-standbyTextShowMs)>=STANDBY_TEXT_MS && screenBrightness>0) {
    setScreenBrightness(0);
    display.fillScreen(C_BG);
  }

  // Dim-entry en deep-dim: evalueren vóór de touch-poll early-return,
  // zodat de dim ook triggert als calm actief is (mainCalmMode=true).
  // Dim-entry vereist screenBrightness==activeBrightness() — tijdens calm
  // is die conditie true, want calm verandert alleen kleur, niet helderheid.
  {
    uint32_t nowDim = millis();

    if (currentScreen==SCR_MAIN && !inStandby && lastActivityMs>0
        && screenBrightness==activeBrightness() && !fadeActive
        && (nowDim-lastActivityMs)>=(uint32_t)dimDelaySec*1000UL) {
      clearEncoderCounts();
      mainCalmMode = false;
      mainCalmBlend = 0;
      deepDimActive = false;
      backlightOffActive = false;
      if (!detailVisibleOnDim && largeFontOnDim) {
        detailPanelSuppressedByDim = true;
        display.fillRect(0, DP_TOP, SCREEN_W, SCREEN_H - DP_TOP, C_BG);
        // Crossfade state machine animateert de overgang — geen directe redraw hier.
      }
      fadeTobrightness(dimBrightness());
    }

    if (deepDimDelayMin < 61
        && currentScreen==SCR_MAIN && !inStandby && lastActivityMs>0
        && !deepDimActive && !fadeActive && screenBrightness > 0
        && screenBrightness <= dimBrightness()
        && (nowDim-lastActivityMs)>=((uint32_t)dimDelaySec*1000UL
                                     + (uint32_t)deepDimDelayMin*60000UL)) {
      deepDimActive = true;
      fadeTobrightness(deepDimBrightness());
    }

    if (deepDimDelayMin < 61
        && currentScreen==SCR_MAIN && !inStandby && lastActivityMs>0
        && deepDimActive && !backlightOffActive && !fadeActive && screenBrightness > 0
        && (nowDim-lastActivityMs)>=((uint32_t)dimDelaySec*1000UL
                                     + (uint32_t)deepDimDelayMin*60000UL
                                     + BACKLIGHT_OFF_EXTRA_MS)) {
      backlightOffActive = true;
      fadeTobrightness(0);
    }
  }

  static uint32_t lastPollMs = 0;
  if (now - lastPollMs < 16) return;
  lastPollMs = now;

  // Touch niet pollen tijdens rust op het hoofdscherm of in standby.
  // In menu's altijd actief — touch is daar de primaire bediening.
  // Wake via encoder, remote of knop.
  if (inStandby) return;
  if (currentScreen == SCR_MAIN && (screenBrightness < activeBrightness() || mainCalmMode)) return;

  GDTpoint_t pts[5];
  uint8_t n = touch.getTouchPoints(pts);
  if (n > 0) {
    if (touchLockoutMs > 0 && (now - touchLockoutMs) < touchLockoutDur) { fingerDown = true; return; }
    lastTouchTime = now;
    bool wasUp = !fingerDown; fingerDown = true;
    if (currentScreen==SCR_BOOT || currentScreen==SCR_WARMUP) return;
    bool wasDimmed = (screenBrightness < activeBrightness());
    notifyTouch();
    // Touch op gedimmd scherm: alleen wekken, actie negeren.
    // Zo voorkomt een "wake tap" dat er onbedoeld menu-acties worden uitgevoerd.
    if (wasDimmed && currentScreen == SCR_MAIN) return;

    int16_t tx = (int16_t)pts[0].y;
    int16_t ty = (int16_t)(480 - pts[0].x);

    if (currentScreen == SCR_IR_LEARN) {
      bool inReset = (tx >= IR_RST_X && tx <= IR_RST_X + IR_RST_W &&
                      ty >= IR_RST_Y && ty <= IR_RST_Y + IR_RST_H);
      if (inReset) {
        if (wasUp) {
          irResetPressStartMs = now;
          irResetHoldTriggered = false;
        }
        if (!irResetHoldTriggered && irResetPressStartMs > 0
            && (now - irResetPressStartMs) >= IR_RESET_HOLD_MS) {
          irResetHoldTriggered = true;
          performIRResetAllCodes();
          return;
        }
      } else {
        irResetPressStartMs = 0;
        irResetHoldTriggered = false;
      }
    }

    if (wasUp) {
      switch (currentScreen) {
        case SCR_MAIN:        handleMainTouch(tx,ty);         break;
        case SCR_MENU:        handleMenuTouch(tx,ty);         break;
        case SCR_VOLUME_GAIN:  handleVolumeGainTouch(tx,ty);   break;
        case SCR_TRANSFORMER:  handleTransformerTouch(tx,ty);  break;
        case SCR_OFFSETS:      handleOffsetsTouch(tx,ty);      break;
        case SCR_NAMES:        handleNamesTouch(tx,ty);        break;
        case SCR_KB_EDIT:      handleKbEditTouch(tx,ty);       break;
        case SCR_DISPLAY:     handleDisplayTouch(tx,ty);      break;
        case SCR_THEME:       handleThemeMotionTouch(tx,ty);  break;
        case SCR_SYS:         handleSystemTouch(tx,ty);       break;
        case SCR_DIAG:        handleDiagnosticsTouch(tx,ty);  break;
        case SCR_IR_LEARN:    handleIRLearnTouch(tx,ty);      break;
        default: break;
      }
    }
  } else {
    if (fingerDown && (now-lastTouchTime)>TOUCH_RELEASE_MS) {
      fingerDown = false;
      irResetPressStartMs = 0;
      irResetHoldTriggered = false;
    }
  }

}
