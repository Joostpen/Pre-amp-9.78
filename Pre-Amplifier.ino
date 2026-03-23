/*
 * Pre-Amplifier Volume Control
 * Arduino Giga R1 WiFi with I2C Volume Control
 *
 * Hardware: Two MCP23017 I/O Expanders
 * - Chip 0x20: Control functions (input selection, mute relay)
 * - Chip 0x21: Volume control (L/R channels, 8-bit each)
 */

#include <Wire.h>
#include <IRremote.hpp>
#include "Config.h"
#include "IR_Remote.h"
#include "MCP23017.h"
#include "VolumeControl.h"
#include "InputControl.h"
#include "Encoder.h"
#include "Controls.h"
#include "Display.h"
#include "GainControl.h"
#include "Settings.h"
#include "mbed.h"       // Voor watchdog

// Input names (used by InputControl)
char inputNames[INPUT_COUNT][16] = {"DAC", "Phono", "Tape", "Tuner", "Surround"};

// Shared state (accessed by modules via extern)
uint8_t currentVolume = 0x80;
uint8_t currentInput = INPUT_DAC;
bool inStandby = false;  // Niet in standby bij opstart
bool isMuted   = true;   // Mute actieve staat
bool muteOnStartup = true; // Wordt overschreven door settings
bool warmStandbyEnabled = false; // true = power relay blijft aan in standby
bool warmTrigRelayClosed = false; // true = remote trigger actief houden in warm standby
bool bypassLF = false;  // true = transformer bypass LF actief (GPA0)
bool bypassMF = false;  // true = transformer bypass MF actief (GPA1)
int8_t balanceOffset = 0;
uint8_t screenBrightness = 255;   // Display helderheid (255 = max)
uint8_t diagRecoveryAttempts = 0; // Aantal I2C recovery pogingen tijdens boot
bool    diagBootDegraded    = false; // True als systeem in degraded mode opstart

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);


  // Initialize modules
  initEncoders();

  // Initialize I2C
  Wire1.begin();
  Wire1.setClock(100000);

  // Scan for chips
  bool foundControl = scanI2C(I2C_ADDR_CONTROL, "Control");
  bool foundVolume = scanI2C(I2C_ADDR_VOLUME, "Volume");

  // Registreer aanwezigheid zodat writes naar ontbrekende chips direct stoppen
  setDeviceAvailable(I2C_ADDR_CONTROL, foundControl);
  setDeviceAvailable(I2C_ADDR_VOLUME, foundVolume);

  // ── Recoverable fault: retry I2C up to 10× before degraded boot ─────────────
  bool displayInited = false;

  if (!foundVolume) {

    // Display must init first so we can show the fault screen
    initDisplay();
    displayInited = true;

    const uint8_t  MAX_RETRIES    = 10;
    const uint16_t RETRY_DELAY_MS = 10000;
    for (uint8_t attempt = 0; attempt < MAX_RETRIES && !foundVolume; attempt++) {
      diagRecoveryAttempts = attempt + 1;

      for (uint8_t s = RETRY_DELAY_MS / 1000; s > 0; s--) {
        drawFaultScreen("Volume control not responding", s);
        delay(1000);
      }

      drawFaultScreen("Volume control not responding", 0);
      Wire1.end(); delay(100); Wire1.begin(); Wire1.setClock(100000);
      foundVolume  = scanI2C(I2C_ADDR_VOLUME,  "Volume");
      foundControl = scanI2C(I2C_ADDR_CONTROL, "Control");
      setDeviceAvailable(I2C_ADDR_VOLUME,  foundVolume);
      setDeviceAvailable(I2C_ADDR_CONTROL, foundControl);
    }

    if (!foundVolume) {
      // Alle retries uitgeput — starten zonder audio
      diagBootDegraded = true;
      drawFaultScreen("Starting without audio control", 0);
      delay(3000);
      loadSettings();
      resetSessionInputVolumes();
      isMuted = true;  // Force muted — no chip to control anyway
    } else {
      // Recovery geslaagd — gewone audio-init nog uitvoeren
      diagBootDegraded = false;
    }
  }

  if (!diagBootDegraded) {
    // Normale pad — of recovery geslaagd
    // Configure MCP23017 chips altijd als outputs zodra ze gevonden zijn.
    // Dit moet ook gebeuren na recovery (displayInited kan dan al true zijn).
    if (foundVolume) {
      writeRegister(I2C_ADDR_VOLUME, IODIRA, 0x00);
      writeRegister(I2C_ADDR_VOLUME, IODIRB, 0x00);
    }
    if (foundControl) {
      writeRegister(I2C_ADDR_CONTROL, IODIRA, 0x00);
      writeRegister(I2C_ADDR_CONTROL, IODIRB, 0x00);
    }

    // Laad opgeslagen instellingen (voor initControls zodat volume/input kloppen)
    loadSettings();
    resetSessionInputVolumes();
    // isMuted wordt gezet door initControls() via applyRelayState(muteOnStartup)

    // Initialize controls (must be after I2C init)
    initControls();

    if (!displayInited) {
      initDisplay();
    }
  }


  // Initialize IR receiver
  IrReceiver.begin(PIN_IR_RECV, DISABLE_LED_FEEDBACK);


  // Cold boot: houd audio pad stil tijdens de bootvertraging.
  // Eerst mute dicht, daarna alle overige audio-relais uit. Herstel van input,
  // gain en volume gebeurt gefaseerd vanuit updateDisplay().
  if (!diagBootDegraded) {
    syncRelayState(true);
    standbyRelaysOff();
    resetRemoteTrigger();
  }
  updateVolumeDisplay();

  // Watchdog: herstart na 8 seconden als loop() niet meer reageert
  mbed::Watchdog::get_instance().start(WATCHDOG_MS);

}

void loop() {
  mbed::Watchdog::get_instance().kick();
  handleIR();
  handleStandbyButton();
  handleStandbyLed();
  handleRemoteTrigger();
  tickSettings();
  tickGainRestore();

  if (!inStandby) {
    handleMuteButton();
  }

  updateDisplay();

  if (!inStandby) {
    handleEncoders();
    tickInputSwitch();   // Rondt input-wissel af na stilstand encoder
  }
}

//------------------------------------------------------------------------------
// IR Remote Handler
// Must be in main file due to IRremote.hpp global state
//------------------------------------------------------------------------------

// Repeat state — tracks held button for timed repeat and acceleration
static uint8_t  irLastCmd       = 0xFF;   // Last command received
static int8_t   irLastAction    = -1;     // Last mapped action (for repeat fallback)
static uint32_t irCmdFirstMs    = 0;      // When first press arrived
static uint32_t irLastActionMs  = 0;      // When last action was taken

static int8_t matchIRAction(uint8_t protocol, uint16_t address, uint8_t command) {
  if (protocol == UNKNOWN || protocol == IR_PROTOCOL_UNKNOWN) return -1;
  for (uint8_t i = 0; i < IR_ACTION_COUNT; i++) {
    bool protoMatch = (irProtocolMap[i] == IR_PROTOCOL_ANY) || (irProtocolMap[i] == protocol);
    if (protoMatch &&
        irAddressMap[i]  == address &&
        irCommandMap[i]  == command) {
      return (int8_t)i;
    }
  }
  return -1;
}

void handleIR() {
  uint32_t now = millis();

  if (!IrReceiver.decode()) {
    if (irLastCmd != 0xFF && (now - irLastActionMs) > 300) {
      irLastCmd = 0xFF;
      irLastAction = -1;
    }
    return;
  }

  bool isRepeat = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;
  uint8_t cmd   = IrReceiver.decodedIRData.command;

  // Fallback: als remote geen repeat-flag stuurt, detecteer herhaling op basis van
  // hetzelfde commando dat snel achter elkaar binnenkomt (binnen IR_SOFT_REPEAT_MS)
  #define IR_SOFT_REPEAT_MS  250
  if (!isRepeat && cmd == irLastCmd && (now - irLastActionMs) < IR_SOFT_REPEAT_MS) {
    isRepeat = true;
  }

  // Balance-acties krijgen geen soft-repeat filtering: elke losse druk moet
  // altijd als nieuwe stap doorkomen zodat de center detent betrouwbaar werkt.
  {
    int8_t previewAction = matchIRAction((uint8_t)IrReceiver.decodedIRData.protocol,
                                         IrReceiver.decodedIRData.address, cmd);
    if (previewAction == IR_ACT_BAL_LEFT || previewAction == IR_ACT_BAL_RIGHT) {
      isRepeat = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;
    }
  }

  // IR learn submenu: toon altijd laatste code en voer geen acties uit
  onIRLearnSample((uint8_t)IrReceiver.decodedIRData.protocol,
                  IrReceiver.decodedIRData.address,
                  IrReceiver.decodedIRData.command,
                  IrReceiver.decodedIRData.decodedRawData,
                  isRepeat);
  if (isIRLearnScreenActive()) {
    IrReceiver.resume();
    return;
  }

  int8_t action = matchIRAction((uint8_t)IrReceiver.decodedIRData.protocol,
                               IrReceiver.decodedIRData.address,
                               cmd);

  // Blokkeer alle acties (behalve standby) als het hoofdscherm niet actief is.
  // Voorkomt dat naijlende IR frames acties uitvoeren terwijl een menu of
  // instellingenscherm net verlaten is.
  if (!isMainScreen() && action != IR_ACT_STANDBY) {
    IrReceiver.resume();
    return;
  }

  // Veel remotes sturen bij ingedrukt houden een "repeat frame" zonder bruikbaar
  // command/adres. Fallback: gebruik de laatst gemapte actie.
  // Uitzondering: balance-acties nooit via fallback uitvoeren — de center detent
  // vereist dat elk event een echte intentionele druk is.
  if (action < 0 && isRepeat && irLastAction >= 0) {
    if (irLastAction != IR_ACT_BAL_LEFT && irLastAction != IR_ACT_BAL_RIGHT) {
      action = irLastAction;
    }
  }

  if (action < 0) {
    if (!isRepeat && IrReceiver.decodedIRData.protocol != UNKNOWN) {
    }
    IrReceiver.resume();
    return;
  }

  // Track first press timing for repeat delay and acceleration.
  // Belangrijk: veel protocollen sturen repeat-frames met command 0x00/0xFF,
  // dus op repeat NIET opnieuw initialiseren op basis van het command-byte.
  if (!isRepeat) {
    bool newPress = (action != irLastAction) || (cmd != irLastCmd) ||
                    ((now - irLastActionMs) > IR_SOFT_REPEAT_MS);
    if (newPress) {
      irCmdFirstMs = now;
    }
    irLastCmd      = cmd;
    irLastAction   = action;
    irLastActionMs = now;
  }

  // Helper: is enough time passed for the next repeat action?
  uint32_t heldMs    = now - irCmdFirstMs;
  uint32_t sinceLast = now - irLastActionMs;
  bool inRepeatWindow = isRepeat && heldMs >= IR_REPEAT_DELAY_MS;

  switch (action) {

    // ── Volume ─────────────────────────────────────────────────────────────
    case IR_ACT_VOL_UP:
    case IR_ACT_VOL_DOWN: {
      if (inStandby || isMuted) break;
      bool doAction = !isRepeat ||
                      (inRepeatWindow && sinceLast >= IR_VOL_REPEAT_MS);
      if (doAction) {
        notifyActivity();
        int step = (heldMs >= IR_VOL_ACCEL_MS) ? IR_VOL_ACCEL_STEP : 1;
        adjustVolume((action == IR_ACT_VOL_UP) ? step : -step);
        irLastActionMs = now;
      }
      break;
    }

    // ── Mute ───────────────────────────────────────────────────────────────
    case IR_ACT_MUTE:
      if (!isRepeat && !inStandby) {
        notifyActivity();
        toggleMute();
        irLastActionMs = now;
      }
      break;

    // ── Standby ────────────────────────────────────────────────────────────
    case IR_ACT_STANDBY:
      if (!isRepeat) {
        // Debounce: negeer standby-commando de eerste 2s na standby-exit
        // voorkomt dat power-on ruis van versterkers meteen weer standby triggert
        if (inStandby || standbyExitDebounceOk()) {
          toggleStandby();
          irLastActionMs = now;
        }
      }
      break;

    // ── Input selection ────────────────────────────────────────────────────
    // Single press only — switching has a relay delay, repeat makes no sense
    case IR_ACT_INPUT_UP:
      if (!isRepeat && !inStandby) {
        notifyActivity();
        adjustInput(1);
        irLastActionMs = now;
      }
      break;

    case IR_ACT_INPUT_DOWN:
      if (!isRepeat && !inStandby) {
        notifyActivity();
        adjustInput(-1);
        irLastActionMs = now;
      }
      break;

    // ── Directe ingangsselectie ────────────────────────────────────────────
    case IR_ACT_INPUT_1:
    case IR_ACT_INPUT_2:
    case IR_ACT_INPUT_3:
    case IR_ACT_INPUT_4:
    case IR_ACT_INPUT_5:
      if (!isRepeat && !inStandby) {
        uint8_t target = (uint8_t)(action - IR_ACT_INPUT_1);
        if (target < INPUT_COUNT) {
          notifyActivity();
          selectInput(target);
        }
        irLastActionMs = now;
      }
      break;

    // ── Balance ────────────────────────────────────────────────────────────
    case IR_ACT_BAL_LEFT:
    case IR_ACT_BAL_RIGHT: {
      if (inStandby) break;
      bool doAction = !isRepeat ||
                      (inRepeatWindow && sinceLast >= IR_BAL_REPEAT_MS);
      if (doAction) {
        notifyActivity();
        // Geef isRepeat mee zodat de detent repeat kan blokkeren bij center
        if (action == IR_ACT_BAL_LEFT)  adjustBalanceLeft(isRepeat);
        else                            adjustBalanceRight(isRepeat);
        irLastActionMs = now;
      }
      break;
    }

    // ── Unknown command from known remote ──────────────────────────────────
    default:
      if (!isRepeat) {
      }
      break;
  }

  IrReceiver.resume();
}
