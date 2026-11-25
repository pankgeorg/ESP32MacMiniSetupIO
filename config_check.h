// Compile-time checks on config.h. Kept in a header on purpose: the Arduino
// builder inserts auto-generated prototypes before the first function in the
// .ino, so a helper function there would end up above the types it needs.
#pragma once
#include "config.h"

static_assert(sizeof(AP_PASSWORD) - 1 >= 8 && sizeof(AP_PASSWORD) - 1 <= 63,
              "AP_PASSWORD must be 8..63 characters for WPA2");
static_assert(sizeof(WS_TOKEN) - 1 >= 8, "WS_TOKEN should be at least 8 characters");

// Refuse to build with the placeholders from config.example.h.
constexpr bool configStartsWith(const char *s, const char *prefix) {
  return *prefix == 0 ? true : (*s == *prefix && configStartsWith(s + 1, prefix + 1));
}
static_assert(!configStartsWith(AP_PASSWORD, "CHANGE_ME"), "Set your own AP_PASSWORD in config.h (copy config.example.h)");
static_assert(!configStartsWith(WS_TOKEN, "CHANGE_ME"), "Set your own WS_TOKEN in config.h (copy config.example.h)");
