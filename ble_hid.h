// Bluetooth LE HID: one composite keyboard + mouse device.
//
// Design rule: the device never keeps a key or mouse button held between
// calls. Every public "press" function sends press AND release before it
// returns, so a lost WebSocket/phone can never leave Cmd (or anything else)
// stuck down. releaseAll() is still provided as a belt-and-braces reset.
//
// All send functions must be called from ONE task (the HID worker task).
#pragma once
#include <stdint.h>

enum class BleState : uint8_t {
  Advertising,   // waiting for a computer to connect / pair
  Connected,     // link up, not yet paired+subscribed
  Ready,         // paired, encrypted -> keys will arrive
  Failed,        // Bluetooth stack failed to start (see serial log)
};

namespace BleHid {
  bool begin(const char *deviceName);               // false: Bluetooth unavailable, Wi-Fi UI still runs

  BleState state();
  const char *stateName();
  bool ready();
  int bondCount();

  // Keyboard
  void tapKey(uint8_t mods, uint8_t usage);         // press + release
  bool typeChar(char c);                            // false if char unsupported

  // Mouse
  void move(int dx, int dy);                        // relative, any size (split into chunks)
  void scroll(int vertical, int horizontal);        // wheel / AC Pan detents
  void click(uint8_t buttons, uint8_t mods);        // press + release, optional modifiers

  // Pairing mode: fast, limited-discoverable advertising for 3 minutes.
  void requestPairingMode();                        // any task
  int pairingSecondsLeft();                         // 0 = not in pairing mode
  void service();                                   // call often from loop(); owns the advertiser

  // Pairing code (passkey entry): the computer shows 6 digits, the page sends them.
  int passkeySecondsLeft();                         // >0 while the computer is waiting for a code
  bool submitPasskey(uint32_t pin);                 // any task; false if nobody is asking

  void releaseAll();
  void forgetAllHosts();                            // delete bonds + drop current link
}
