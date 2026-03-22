/*
 * Controls.h - Button and relay control interface
 */

#ifndef CONTROLS_H
#define CONTROLS_H

#include <Arduino.h>

// Gedeelde hardware state (gebruikt door GainControl voor GPB sync)
extern uint8_t controlPortB;

void initControls();
void handleStandbyButton();
void handleStandbyLed();
void handleMuteButton();
void handleRemoteTrigger();
void resetRemoteTrigger();   // Herstart trigger-sequentie (gebruik bij re-enable)
bool standbyExitDebounceOk(); // true als >= 2s verstreken na standby-exit
void toggleStandby();
void enterWarmStandby();   // Warm standby — voeding blijft aan
void toggleMute();

// Hardware mute relay direct aansturen zonder isMuted te wijzigen
// Gebruik voor input-wisseling en gain relay schakeling: mute → schakel → unmute
void muteRelay(bool mute);

// Relay + isMuted synchroon zetten — publieke wrapper voor applyRelayState()
// Gebruik na MCP reinitialisatie om hardware te synchroniseren met software staat
void syncRelayState(bool muted);

extern bool isWarmStandbyActive;  // true als systeem in warm standby is gegaan

#endif // CONTROLS_H
