// =====================================================================
//  MacSetupKVM — user configuration (TEMPLATE)
//
//  Copy this file to config.h and edit it:
//      cp config.example.h config.h        (PowerShell: Copy-Item config.example.h config.h)
//  config.h is git-ignored so your Wi-Fi password and token never get
//  committed. The firmware refuses to compile while the CHANGE_ME
//  placeholders below are still in place.
//  Everything here is compile-time: edit, then re-flash.
// =====================================================================
#pragma once

// ---------------------------------------------------------------------
//  Wi-Fi
// ---------------------------------------------------------------------
// 0 = SoftAP (default): the ESP32 creates its own Wi-Fi network and the
//     phone joins it directly. No home Wi-Fi needed. The UI is at
//     http://192.168.4.1/
// 1 = Station: the ESP32 joins an existing network and is reachable at
//     http://macsetup.local/ (mDNS) or at the IP printed on the serial log.
#define WIFI_USE_STA 0

#define AP_SSID       "MacSetupKVM"
#define AP_PASSWORD   "CHANGE_ME_wifi_password"   // WPA2, 8..63 characters
#define AP_CHANNEL    6
#define AP_MAX_CLIENTS 2                          // phone + one spare

#define STA_SSID      "your-home-wifi"
#define STA_PASSWORD  "your-home-wifi-password"
#define MDNS_HOSTNAME "macsetup"                  // -> http://macsetup.local/

// ---------------------------------------------------------------------
//  Access token
//  The WebSocket only accepts connections that carry this token, so a
//  second device that joins the Wi-Fi can load the page but cannot send
//  keystrokes. Open the UI as  http://192.168.4.1/#<WS_TOKEN>
//  (the part after # is never sent to the server as part of the page
//  request; the page reads it locally and uses it to open the WebSocket).
//  Use something random, e.g.  python -c "import secrets;print(secrets.token_urlsafe(15))"
// ---------------------------------------------------------------------
#define WS_TOKEN "CHANGE_ME_token"

// ---------------------------------------------------------------------
//  Bluetooth
// ---------------------------------------------------------------------
#define BLE_DEVICE_NAME "MacSetup KVM"   // name shown in the computer's Bluetooth list
// Bluetooth identity. Increase by 1 to make every computer see a brand-new
// keyboard (useful when a computer kept an old pairing you can't delete,
// or after changing HID_ENABLE_MOUSE - macOS caches what a device looks like).
#define BLE_ADDRESS_GENERATION 1
// 0 = keyboard only  (REQUIRED for macOS Setup Assistant - see README)
// 1 = keyboard + mouse in one device (works on Windows; on macOS Setup
//     Assistant the pointer worked but every keystroke was ignored)
#define HID_ENABLE_MOUSE 0

// ---------------------------------------------------------------------
//  Behaviour
// ---------------------------------------------------------------------
#define DEFAULT_CHAR_DELAY_MS 25    // UI default; per-send value comes from the page
#define MAX_CHAR_DELAY_MS     500
#define KEY_HOLD_MS           6     // minimum key hold (raised to one BLE connection interval)
#define CLICK_HOLD_MS         25    // how long a mouse button is held down
#define CLIENT_TIMEOUT_MS     4000  // no message (incl. heartbeat) for this long => release all + abort typing
#define MAX_TEXT_LEN          512   // longest string accepted in one "Send"

// ---------------------------------------------------------------------
//  Board pins (classic ESP32 DevKit / WROOM-32 defaults)
// ---------------------------------------------------------------------
#define STATUS_LED_PIN   2   // on-board blue LED on most DevKits; -1 to disable
#define FORGET_BONDS_PIN 0   // BOOT button: hold 3 s while running to forget all paired computers
