// US-ANSI layout: printable ASCII (0x20..0x7E) -> HID usage (Keyboard page 0x07).
// Bit 7 (SHIFT_FLAG) means "hold Left Shift". Table is indexed by (c - 0x20).
#pragma once
#include <stdint.h>

#define SHIFT_FLAG 0x80

// HID modifier bits (byte 0 of the keyboard report)
#define MOD_LCTRL  0x01
#define MOD_LSHIFT 0x02
#define MOD_LALT   0x04   // Option on a Mac
#define MOD_LGUI   0x08   // Command on a Mac

// Named keys used by the firmware
#define HID_KEY_ENTER     0x28
#define HID_KEY_ESCAPE    0x29
#define HID_KEY_BACKSPACE 0x2A
#define HID_KEY_TAB       0x2B
#define HID_KEY_SPACE     0x2C

static const uint8_t ASCII_TO_HID[] = {
  0x2C,              // ' '
  0x1E | SHIFT_FLAG, // !
  0x34 | SHIFT_FLAG, // "
  0x20 | SHIFT_FLAG, // #
  0x21 | SHIFT_FLAG, // $
  0x22 | SHIFT_FLAG, // %
  0x24 | SHIFT_FLAG, // &
  0x34,              // '
  0x26 | SHIFT_FLAG, // (
  0x27 | SHIFT_FLAG, // )
  0x25 | SHIFT_FLAG, // *
  0x2E | SHIFT_FLAG, // +
  0x36,              // ,
  0x2D,              // -
  0x37,              // .
  0x38,              // /
  0x27,              // 0
  0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, // 1..9
  0x33 | SHIFT_FLAG, // :
  0x33,              // ;
  0x36 | SHIFT_FLAG, // <
  0x2E,              // =
  0x37 | SHIFT_FLAG, // >
  0x38 | SHIFT_FLAG, // ?
  0x1F | SHIFT_FLAG, // @
  // A..Z
  0x04 | SHIFT_FLAG, 0x05 | SHIFT_FLAG, 0x06 | SHIFT_FLAG, 0x07 | SHIFT_FLAG, 0x08 | SHIFT_FLAG,
  0x09 | SHIFT_FLAG, 0x0A | SHIFT_FLAG, 0x0B | SHIFT_FLAG, 0x0C | SHIFT_FLAG, 0x0D | SHIFT_FLAG,
  0x0E | SHIFT_FLAG, 0x0F | SHIFT_FLAG, 0x10 | SHIFT_FLAG, 0x11 | SHIFT_FLAG, 0x12 | SHIFT_FLAG,
  0x13 | SHIFT_FLAG, 0x14 | SHIFT_FLAG, 0x15 | SHIFT_FLAG, 0x16 | SHIFT_FLAG, 0x17 | SHIFT_FLAG,
  0x18 | SHIFT_FLAG, 0x19 | SHIFT_FLAG, 0x1A | SHIFT_FLAG, 0x1B | SHIFT_FLAG, 0x1C | SHIFT_FLAG,
  0x1D | SHIFT_FLAG,
  0x2F,              // [
  0x31,              // backslash
  0x30,              // ]
  0x23 | SHIFT_FLAG, // ^
  0x2D | SHIFT_FLAG, // _
  0x35,              // `
  // a..z
  0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
  0x2F | SHIFT_FLAG, // {
  0x31 | SHIFT_FLAG, // |
  0x30 | SHIFT_FLAG, // }
  0x35 | SHIFT_FLAG, // ~
};
static_assert(sizeof(ASCII_TO_HID) == 95, "ASCII table must cover exactly 0x20..0x7E");

// Returns false if the character cannot be typed (non-ASCII, control chars
// other than \n and \t).
static inline bool asciiToHid(char c, uint8_t &usage, uint8_t &mods) {
  if (c == '\n') { usage = HID_KEY_ENTER; mods = 0; return true; }
  if (c == '\t') { usage = HID_KEY_TAB;   mods = 0; return true; }
  uint8_t u = (uint8_t)c;
  if (u < 0x20 || u > 0x7E) return false;
  uint8_t e = ASCII_TO_HID[u - 0x20];
  usage = e & 0x7F;
  mods  = (e & SHIFT_FLAG) ? MOD_LSHIFT : 0;
  return true;
}
