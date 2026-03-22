/*
 * Controls.cpp - Standby and Mute Control
 *
 * Relay state machine: all relay transitions go through applyRelayState().
 * isMuted and the hardware relay are always set together — they cannot drift.
 */

#include "Controls.h"
#include "MCP23017.h"
#include "VolumeControl.h"
#include "Display.h"
#include "GainControl.h"  // voor startupVolume, surroundInput
#include "InputControl.h" // voor setInput()
#include "Settings.h"     // warmStandbyEnabled setting

// State access
extern uint8_t currentVolume;
extern uint8_t startupVolume[INPUT_COUNT];
extern uint8_t startupVolume[INPUT_COUNT];
extern uint8_t currentInput;
extern uint8_t savedVolume[];
extern bool    inStandby;
extern bool    isMuted;
extern bool    warmStandbyEnabled;
extern bool    warmTrigRelayClosed;
extern bool    remoteTriggerEnabled;

// Internal state
static uint8_t storedVolume = 0x80;
uint8_t controlPortB = 0x00;  // Gedeeld met Display.cpp voor GPB sync

// Button state
static bool lastStandbyBtnState = HIGH;
static unsigned long lastStandbyDebounce = 0;
static bool lastMuteBtnState = HIGH;
static unsigned long lastMuteDebounce = 0;
static unsigned long muteBtnPressTime = 0;
static unsigned long controlsInitMs = 0;  // Boot-guard: negeer knoppen eerste 500ms
#define BTN_BOOT_GUARD_MS  500
static bool muteBtnHandled = false;

// Remote trigger state
static unsigned long standbyExitTime = 0;
static unsigned long standbyEnterTime   = 0;
static bool          enteredWarmStandby = false;  // hoe zijn we IN standby gegaan
static bool remoteTriggerFired = false;

// Warm standby: voeding blijft aan (standby relay gesloten)
// LED knippert langzaam: 4s cyclus met korte puls voor zichtbare status.
static const uint32_t STANDBY_LED_PERIOD_MS = 4000;
static const uint32_t STANDBY_LED_ON_MS     = 320;
static const uint32_t WARM_STANDBY_REINIT_MS = 30UL * 60UL * 1000UL; // 30 min

// Local functions
static void enterMenu();

// ── Single relay state machine ────────────────────────────────────────────────
// ALL relay transitions go through here. isMuted and hardware relay always match.
static void applyRelayState(bool muted) {
  isMuted = muted;
  if (muted) {
    controlPortB &= ~MUTE_RELAY_BIT;   // relay closed = muted
    digitalWrite(PIN_LED_MUTE, HIGH);
  } else {
    controlPortB |= MUTE_RELAY_BIT;    // relay open = unmuted
    digitalWrite(PIN_LED_MUTE, LOW);
  }
  writeRegister(I2C_ADDR_CONTROL, GPIOB, controlPortB);
}

void initControls() {
  // Standby pins
  pinMode(PIN_BTN_STANDBY, INPUT_PULLUP);
  pinMode(PIN_LED_STANDBY, OUTPUT);
  pinMode(PIN_STANDBY_OFF, OUTPUT);
  pinMode(PIN_REMOTE_TRIG, OUTPUT);
  digitalWrite(PIN_LED_STANDBY, LOW);
  digitalWrite(PIN_STANDBY_OFF, HIGH);  // Opstart: actief (niet in standby)
  digitalWrite(PIN_REMOTE_TRIG, LOW);

  // Lees werkelijke pin-staat na pinMode zodat de debounce niet op een
  // verkeerde aanname start — voorkomt valse standby-trigger bij boot.
  lastStandbyBtnState = digitalRead(PIN_BTN_STANDBY);
  lastStandbyDebounce = millis();
  controlsInitMs      = millis();       // Boot-guard start

  standbyExitTime = millis();
  remoteTriggerFired = false;           // Boot: trigger na REMOTE_TRIG_DELAY inschakelen

  // Mute pins
  pinMode(PIN_BTN_MUTE, INPUT_PULLUP);
  pinMode(PIN_LED_MUTE, OUTPUT);
  lastMuteBtnState = digitalRead(PIN_BTN_MUTE);
  lastMuteDebounce = millis();
  pinMode(PIN_LED_MUTE, OUTPUT);

  // Set relay to match muteOnStartup — single point of truth
  applyRelayState(muteOnStartup);
}

void handleStandbyLed() {
  if (!inStandby) {
    digitalWrite(PIN_LED_STANDBY, LOW);
    return;
  }

  if (!warmStandbyEnabled) {
    digitalWrite(PIN_LED_STANDBY, HIGH);
    return;
  }

  uint32_t phase = millis() % STANDBY_LED_PERIOD_MS;
  digitalWrite(PIN_LED_STANDBY, (phase < STANDBY_LED_ON_MS) ? HIGH : LOW);
}

void handleStandbyButton() {
  // Boot-guard: negeer knop de eerste BTN_BOOT_GUARD_MS na initialisatie
  if ((millis() - controlsInitMs) < BTN_BOOT_GUARD_MS) return;

  bool reading = digitalRead(PIN_BTN_STANDBY);

  if (reading != lastStandbyBtnState) {
    lastStandbyDebounce = millis();
  }

  if ((millis() - lastStandbyDebounce) > DEBOUNCE_MS) {
    static bool buttonState    = HIGH;
    static uint32_t pressStart = 0;
    static bool longHandled    = false;

    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        // Knop ingedrukt — start timer
        notifyTouch();
        pressStart   = millis();
        longHandled  = false;
      } else if (!longHandled) {
        // Knop losgelaten zonder long-press: normale toggle
        toggleStandby();
      }
    }

    // Long-press: warm standby IN — alleen als enabled en systeem actief
    // Vanuit standby (warm of koud) altijd korte press voor uitschakelen
    if (buttonState == LOW && !longHandled && warmStandbyEnabled && !inStandby) {
      if ((millis() - pressStart) >= LONG_PRESS_MS) {
        longHandled = true;
        enterWarmStandby();
      }
    }
  }

  lastStandbyBtnState = reading;
}

// Interne helper: standby-in met expliciete warm/koud keuze
static void enterStandbyMode(bool warm) {
  inStandby          = true;
  enteredWarmStandby = warm;
  isWarmStandbyActive = warm;
  standbyEnterTime   = millis();
  flushSettings();
  setVolume(0x00, 0x00);        // hardware volume naar 0 vóór relay schakelt
  applyRelayState(true);        // mute relay dicht
  standbyRelaysOff();           // alle audio relais uit, volume chips op 0x00
  digitalWrite(PIN_STANDBY_OFF, warm ? HIGH : LOW);
  bool keepTrig = warm ? warmTrigRelayClosed : false;
  digitalWrite(PIN_REMOTE_TRIG, keepTrig ? HIGH : LOW);
  fadeTransition();
  drawMainScreen();
}

// Warm standby: voeding blijft aan, relay en trigger behouden instellingen
void enterWarmStandby() {
  if (inStandby) return;
  enterStandbyMode(true);
}

void toggleStandby() {
  if (!inStandby) {
    // Korte press naar standby = altijd koud (warm alleen via long-press)
    enterStandbyMode(false);
    return;
  }

  // Uit standby — hoe we ingegaan zijn bepaalt het exit-pad
  inStandby = false;
  isWarmStandbyActive = false;
  digitalWrite(PIN_STANDBY_OFF, HIGH);
  standbyExitTime    = millis();
  remoteTriggerFired = false;
  currentVolume = (currentInput == surroundInput) ? 231 : startupVolume[currentInput];
  isMuted = muteOnStartup;

  if (enteredWarmStandby) {
    // Warm standby-exit: voeding was nooit uit, MCP mogelijk herinitialiseren
    bool doReinit = (standbyEnterTime > 0) &&
                    ((millis() - standbyEnterTime) >= WARM_STANDBY_REINIT_MS);
    if (doReinit) reinitMCP();
    syncRelayState(isMuted);
    applyGainAll();
    setInput(currentInput);
    applyVolume();
    drawMainScreen();
  } else {
    // Koud standby-exit: voeding was uit, volg volledige warmup-sequentie
    startWarmup();
  }
  enteredWarmStandby = false;
}

bool standbyExitDebounceOk() {
  return (standbyExitTime == 0) || ((millis() - standbyExitTime) >= 2000UL);
}

void resetRemoteTrigger() {
  // Herstart de trigger-sequentie: zet remoteTriggerFired terug zodat
  // handleRemoteTrigger() na de delay opnieuw HIGH schrijft.
  remoteTriggerFired = false;
  standbyExitTime    = millis();  // Reset delay-venster vanaf nu
}

void handleRemoteTrigger() {
  if (!inStandby && !remoteTriggerFired) {
    if (!remoteTriggerEnabled) {
      // Trigger disabled — zeker laag houden en niet verder firen
      digitalWrite(PIN_REMOTE_TRIG, LOW);
      return;
    }
    if ((millis() - standbyExitTime) >= REMOTE_TRIG_DELAY) {
      digitalWrite(PIN_REMOTE_TRIG, HIGH);
      remoteTriggerFired = true;
    }
  }
}

void handleMuteButton() {
  if ((millis() - controlsInitMs) < BTN_BOOT_GUARD_MS) return;

  bool reading = digitalRead(PIN_BTN_MUTE);

  if (reading != lastMuteBtnState) {
    lastMuteDebounce = millis();
  }

  if ((millis() - lastMuteDebounce) > DEBOUNCE_MS) {
    static bool buttonState = HIGH;

    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        notifyTouch();
        muteBtnPressTime = millis();
        muteBtnHandled = false;
      } else if (!muteBtnHandled) {
        toggleMute();
      }
    }

    // Long press detection
    if (buttonState == LOW && !muteBtnHandled) {
      if ((millis() - muteBtnPressTime) >= LONG_PRESS_MS) {
        muteBtnHandled = true;
        enterMenu();
      }
    }
  }

  lastMuteBtnState = reading;
}

void toggleMute() {
  if (isMuted) {
    // Unmute: herstel alle relais, dan volume, dan relay openen
    applyGainAll();
    setInput(currentInput);
    currentVolume = storedVolume;
    applyVolume();
    applyRelayState(false);
  } else {
    // Mute: volume naar 0, mute relay sluiten, dan alle overige relais uit
    storedVolume = currentVolume;
    setVolume(VOL_MIN, VOL_MIN);
    applyRelayState(true);   // mute relay dicht
    standbyRelaysOff();      // ingang, gain, LF/MF out relais uit
  }
  if (isMainScreen()) {
    updateVolumeDisplay();
  } else {
    drawMainScreen();
  }
}

// Publieke wrapper rond applyRelayState — voor gebruik buiten Controls.cpp
// Zet relay én isMuted synchroon (bijv. na MCP reinitialisatie)
void syncRelayState(bool muted) {
  applyRelayState(muted);
}

// Public relay-only function for other modules (e.g. input switch)
// Does NOT touch isMuted — hardware relay only
void muteRelay(bool mute) {
  if (mute) {
    controlPortB &= ~MUTE_RELAY_BIT;
  } else {
    controlPortB |= MUTE_RELAY_BIT;
  }
  writeRegister(I2C_ADDR_CONTROL, GPIOB, controlPortB);
}

static void enterMenu() {
  showMainMenu();
}
