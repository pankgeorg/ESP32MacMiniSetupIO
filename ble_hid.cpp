#include "ble_hid.h"
#include "config.h"
#include "keymap.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_bt.h>
#include <esp_mac.h>

// Arduino-ESP32 core 4.x frees the Bluetooth controller's memory at boot
// unless a library declares it uses BLE (esp32-hal-misc.c: if (!bleInUse())).
// NimBLE-Arduino 2.5.2 predates that rule, so declare it here. Without this,
// esp_bt_controller_init() fails with ESP_ERR_INVALID_STATE.
// Core 3.x has no such header and keeps the memory anyway.
#if __has_include("esp32-hal-alloc-ble-mem.h")
#include "esp32-hal-alloc-ble-mem.h"
#endif

#define LOGF(fmt, ...) Serial.printf("[ble] " fmt "\n", ##__VA_ARGS__)

static const uint8_t REPORT_ID_KEYBOARD = 1;
static const uint8_t REPORT_ID_MOUSE    = 2;

// HID report descriptor: keyboard (ID 1) + 5-button mouse with wheel and horizontal pan (ID 2).
static uint8_t REPORT_MAP[] = {
  // ---- Keyboard ----
  0x05, 0x01,       // Usage Page (Generic Desktop)
  0x09, 0x06,       // Usage (Keyboard)
  0xA1, 0x01,       // Collection (Application)
  0x85, REPORT_ID_KEYBOARD,
  0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
  0x19, 0xE0,       //   Usage Minimum (Left Control)
  0x29, 0xE7,       //   Usage Maximum (Right GUI)
  0x15, 0x00,       //   Logical Minimum (0)
  0x25, 0x01,       //   Logical Maximum (1)
  0x75, 0x01,       //   Report Size (1)
  0x95, 0x08,       //   Report Count (8)
  0x81, 0x02,       //   Input (Data, Var, Abs)      ; modifier byte
  0x95, 0x01,       //   Report Count (1)
  0x75, 0x08,       //   Report Size (8)
  0x81, 0x01,       //   Input (Const)               ; reserved byte
  0x95, 0x05,       //   Report Count (5)
  0x75, 0x01,       //   Report Size (1)
  0x05, 0x08,       //   Usage Page (LEDs)
  0x19, 0x01,       //   Usage Minimum (Num Lock)
  0x29, 0x05,       //   Usage Maximum (Kana)
  0x91, 0x02,       //   Output (Data, Var, Abs)     ; LED report
  0x95, 0x01,       //   Report Count (1)
  0x75, 0x03,       //   Report Size (3)
  0x91, 0x01,       //   Output (Const)              ; LED padding
  0x95, 0x06,       //   Report Count (6)
  0x75, 0x08,       //   Report Size (8)
  0x15, 0x00,       //   Logical Minimum (0)
  0x25, 0x65,       //   Logical Maximum (101)
  0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
  0x19, 0x00,       //   Usage Minimum (0)
  0x29, 0x65,       //   Usage Maximum (101)
  0x81, 0x00,       //   Input (Data, Array)         ; 6 key slots
  0xC0,             // End Collection

#if HID_ENABLE_MOUSE
  // ---- Mouse ----
  0x05, 0x01,       // Usage Page (Generic Desktop)
  0x09, 0x02,       // Usage (Mouse)
  0xA1, 0x01,       // Collection (Application)
  0x85, REPORT_ID_MOUSE,
  0x09, 0x01,       //   Usage (Pointer)
  0xA1, 0x00,       //   Collection (Physical)
  0x05, 0x09,       //     Usage Page (Buttons)
  0x19, 0x01,       //     Usage Minimum (1)
  0x29, 0x05,       //     Usage Maximum (5)
  0x15, 0x00,       //     Logical Minimum (0)
  0x25, 0x01,       //     Logical Maximum (1)
  0x95, 0x05,       //     Report Count (5)
  0x75, 0x01,       //     Report Size (1)
  0x81, 0x02,       //     Input (Data, Var, Abs)    ; buttons
  0x95, 0x01,       //     Report Count (1)
  0x75, 0x03,       //     Report Size (3)
  0x81, 0x03,       //     Input (Const)             ; padding
  0x05, 0x01,       //     Usage Page (Generic Desktop)
  0x09, 0x30,       //     Usage (X)
  0x09, 0x31,       //     Usage (Y)
  0x09, 0x38,       //     Usage (Wheel)
  0x15, 0x81,       //     Logical Minimum (-127)
  0x25, 0x7F,       //     Logical Maximum (127)
  0x75, 0x08,       //     Report Size (8)
  0x95, 0x03,       //     Report Count (3)
  0x81, 0x06,       //     Input (Data, Var, Rel)
  0x05, 0x0C,       //     Usage Page (Consumer)
  0x0A, 0x38, 0x02, //     Usage (AC Pan)
  0x15, 0x81,       //     Logical Minimum (-127)
  0x25, 0x7F,       //     Logical Maximum (127)
  0x75, 0x08,       //     Report Size (8)
  0x95, 0x01,       //     Report Count (1)
  0x81, 0x06,       //     Input (Data, Var, Rel)    ; horizontal scroll
  0xC0,             //   End Collection
  0xC0,             // End Collection
#endif
};

static NimBLEServer *s_server = nullptr;
static NimBLEHIDDevice *s_hid = nullptr;
static NimBLECharacteristic *s_kbIn = nullptr;
static NimBLECharacteristic *s_kbOut = nullptr;
static NimBLECharacteristic *s_msIn = nullptr;

// Written by the NimBLE host task, read by the HID worker / status code.
static bool s_failed = false;               // NimBLE init failed: never call into the stack
static volatile bool s_connected = false;
static volatile bool s_encrypted = false;
static volatile bool s_kbSubscribed = false;
static volatile uint16_t s_connHandle = BLE_HS_CONN_HANDLE_NONE;
static volatile uint16_t s_connInterval = 12;   // units of 1.25 ms (12 = 15 ms)

// Advertising / pairing mode. Only service() (called from loop()) touches the
// advertiser; other tasks just set s_pairRequest.
static NimBLEAdvertising *s_adv = nullptr;
static std::string s_name;
static const uint32_t PAIR_WINDOW_MS = 180000;  // Bluetooth spec max for limited-discoverable
static uint32_t s_pairUntil = 0;                 // millis(); 0 = not in pairing mode
static volatile bool s_pairRequest = false;

// Passkey entry. macOS wants keyboards to pair *with a code*: the Mac shows
// 6 digits and "the keyboard" (= the phone page) types them. The host task
// sets s_pkHandle when the Mac asks; loop() injects the code the page sent.
static volatile uint16_t s_pkHandle = BLE_HS_CONN_HANDLE_NONE;
static volatile uint32_t s_pkDeadline = 0;      // millis()
static volatile bool s_pinReady = false;
static volatile uint32_t s_pin = 0;
static const uint32_t PASSKEY_WINDOW_MS = 30000;  // SMP pairing timeout is 30 s

// ---------------------------------------------------------------------
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &ci) override {
    s_connHandle = ci.getConnHandle();
    s_connInterval = ci.getConnInterval();
    s_encrypted = ci.isEncrypted();
    s_connected = true;
    LOGF("host connected: %s (interval %.2f ms)", ci.getAddress().toString().c_str(),
         ci.getConnInterval() * 1.25f);
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &ci, int reason) override {
    s_pkHandle = BLE_HS_CONN_HANDLE_NONE;
    s_connected = false;
    s_encrypted = false;
    s_kbSubscribed = false;
    s_connHandle = BLE_HS_CONN_HANDLE_NONE;
    LOGF("host disconnected (reason 0x%02X) -> advertising again", reason);
  }
  void onAuthenticationComplete(NimBLEConnInfo &ci) override {
    s_pkHandle = BLE_HS_CONN_HANDLE_NONE;
    s_encrypted = ci.isEncrypted();
    LOGF("security: encrypted=%d bonded=%d authenticated=%d (keysize %d)", ci.isEncrypted(), ci.isBonded(),
         ci.isAuthenticated(), ci.getSecKeySize());
    if (!ci.isEncrypted()) LOGF("pairing FAILED - wrong/late code? Start pairing mode and try again");
  }
  void onPassKeyEntry(NimBLEConnInfo &ci) override {
    // Don't answer yet: the phone page shows a code box; loop() injects it.
    s_pkDeadline = millis() + PASSKEY_WINDOW_MS;
    s_pkHandle = ci.getConnHandle();
    LOGF("computer asks for the PAIRING CODE -> enter the 6 digits shown on its screen in the phone page");
  }
  void onConnParamsUpdate(NimBLEConnInfo &ci) override {
    s_connInterval = ci.getConnInterval();
    LOGF("connection interval now %.2f ms", ci.getConnInterval() * 1.25f);
  }
};

class ReportCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *c, NimBLEConnInfo &, uint16_t subValue) override {
    bool on = subValue & 0x0001;
    if (c == s_kbIn) {
      s_kbSubscribed = on;
      LOGF("keyboard reports %s", on ? "enabled by host" : "disabled by host");
    } else if (c == s_msIn) {
      LOGF("mouse reports %s", on ? "enabled by host" : "disabled by host");
    }
  }
  void onRead(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    LOGF("host read %s report", c == s_kbIn ? "keyboard input" : "mouse input");
  }
};

// Diagnostics: log what the host reads/writes on the HID service, so we can
// see whether it fetched the Report Map and adopted the device. Values here
// are protocol metadata (never typed text).
class GattLogCallbacks : public NimBLECharacteristicCallbacks {
 public:
  const char *nameOf(NimBLECharacteristic *c);
  void onRead(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    LOGF("host READ  %s", nameOf(c));
  }
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    NimBLEAttValue v = c->getValue();
    LOGF("host WROTE %s (%u bytes, first=0x%02X)", nameOf(c), (unsigned)v.size(), v.size() ? v[0] : 0);
  }
};

static ServerCallbacks s_serverCb;
static ReportCallbacks s_reportCb;
static GattLogCallbacks s_gattLogCb;

const char *GattLogCallbacks::nameOf(NimBLECharacteristic *c) {
  if (!s_hid) return "?";
  if (c == s_hid->getReportMap())     return "Report Map";
  if (c == s_hid->getHidInfo())       return "HID Information";
  if (c == s_hid->getPnp())           return "PnP ID";
  if (c == s_hid->getProtocolMode())  return "Protocol Mode";
  if (c == s_hid->getHidControl())    return "HID Control Point";
  if (c == s_kbOut)                   return "keyboard LED (output) report";
  return "other characteristic";
}

// Report counters (logged periodically by service()).
static volatile uint32_t s_sentKb = 0, s_sentMs = 0, s_failed_reports = 0;

// ---------------------------------------------------------------------
static uint32_t linkHoldMs() {
  // Hold a key at least one connection interval so press and release land
  // in different connection events on the host.
  uint32_t interval = (uint32_t)(s_connInterval * 5 / 4) + 2;
  return interval > KEY_HOLD_MS ? interval : KEY_HOLD_MS;
}

static bool sendReport(NimBLECharacteristic *chr, const uint8_t *data, size_t len) {
  if (!s_connected || chr == nullptr) return false;
  chr->setValue(data, len);
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (chr->notify(data, len, s_connHandle)) {
      if (chr == s_kbIn) s_sentKb = s_sentKb + 1; else s_sentMs = s_sentMs + 1;
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(5));   // out of buffers: wait for the stack to drain
    if (!s_connected) break;
  }
  s_failed_reports = s_failed_reports + 1;
  LOGF("report dropped (link congested)");
  return false;
}

static void sendKeyboard(uint8_t mods, uint8_t usage) {
  uint8_t r[8] = {mods, 0, usage, 0, 0, 0, 0, 0};
  sendReport(s_kbIn, r, sizeof(r));
}

static void sendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  if (!s_msIn) return;   // keyboard-only build
  uint8_t r[5] = {buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel, (uint8_t)pan};
  sendReport(s_msIn, r, sizeof(r));
}

static int8_t clamp127(int v) { return (int8_t)(v > 127 ? 127 : (v < -127 ? -127 : v)); }

// Build the advertisement ourselves so the flags are exactly right.
// NimBLE's default flags omit "BR/EDR not supported", which makes the board
// look like a dual-mode (classic + LE) device. A Mac pairing screen may then
// try to find it over classic Bluetooth, where it never answers.
//   pairing: LE Limited Discoverable | BR/EDR Not Supported, 20 ms interval
//   normal : LE General Discoverable | BR/EDR Not Supported, 152.5 ms interval
static void applyAdvertising(bool pairing) {
  if (!s_adv) return;
  s_adv->stop();
  s_adv->setDiscoverableMode(pairing ? BLE_GAP_DISC_MODE_LTD : BLE_GAP_DISC_MODE_GEN);

  NimBLEAdvertisementData d;
  d.setFlags((pairing ? BLE_HS_ADV_F_DISC_LTD : BLE_HS_ADV_F_DISC_GEN) | BLE_HS_ADV_F_BREDR_UNSUP);
  d.setAppearance(0x03C1);  // HID Keyboard
  d.setCompleteServices16({NimBLEUUID((uint16_t)0x1812)});  // HID service
  d.setName(s_name);
  s_adv->setAdvertisementData(d);

  // Apple's accessory guidelines: 20 ms while trying to be found, then
  // one of the longer recommended intervals (152.5 ms).
  uint16_t itvl = pairing ? 32 : 244;  // units of 0.625 ms
  s_adv->setMinInterval(itvl);
  s_adv->setMaxInterval(itvl);

  if (!s_connected) s_adv->start();
  LOGF("advertising: %s", pairing ? "PAIRING MODE (limited-discoverable, 20 ms) for 3 min" : "normal (general-discoverable)");
}

// ---------------------------------------------------------------------
namespace BleHid {

bool begin(const char *deviceName) {
  if (!NimBLEDevice::init(deviceName)) {
    // Don't touch any other NimBLE API: the host lock doesn't exist and the
    // next call would assert and reboot-loop the board.
    s_failed = true;
    esp_bt_controller_status_t st = esp_bt_controller_get_status();
    LOGF("NimBLE init FAILED. Controller status=%d (0=idle: init failed, 1=inited: enable failed, 2=enabled)", (int)st);
    if (st == ESP_BT_CONTROLLER_STATUS_IDLE) {
      esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
      cfg.mode = ESP_BT_MODE_BLE;
      esp_err_t e = esp_bt_controller_init(&cfg);
      LOGF("  diagnostic esp_bt_controller_init() -> %s (0x%x)", esp_err_to_name(e), (unsigned)e);
    } else if (st == ESP_BT_CONTROLLER_STATUS_INITED) {
      esp_err_t e = esp_bt_controller_enable(ESP_BT_MODE_BLE);
      LOGF("  diagnostic esp_bt_controller_enable() -> %s (0x%x)", esp_err_to_name(e), (unsigned)e);
    }
    return false;
  }
  NimBLEDevice::setPower(9);  // dBm, ESP32 maximum

  // Authenticated pairing, like a real Bluetooth keyboard: bonding + MITM +
  // LE Secure Connections, I/O capability "keyboard only". The computer shows
  // a 6-digit code and the "keyboard" (the phone page) enters it.
  // (Earlier versions used "Just Works" with no code: macOS then used the
  // pointer but ignored every keystroke.)
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);

  // Own identity: a fixed "static random" address derived from the chip's
  // Bluetooth MAC and BLE_ADDRESS_GENERATION. Changing the generation makes
  // computers see a brand-new keyboard (their old pairing no longer applies).
  {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    uint8_t rnd[6];   // little-endian, as NimBLE expects
    for (int i = 0; i < 6; ++i) rnd[i] = mac[5 - i] ^ (uint8_t)(0xA5 + 0x3B * (BLE_ADDRESS_GENERATION + i));
    rnd[5] |= 0xC0;   // static random address: two top bits set
    bool ok = NimBLEDevice::setOwnAddr(rnd) && NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    LOGF("bluetooth identity: %s (generation %d)%s", NimBLEDevice::getAddress().toString().c_str(),
         BLE_ADDRESS_GENERATION, ok ? "" : " - FAILED to set, using public address");
  }

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(&s_serverCb, false);
  s_server->advertiseOnDisconnect(true);

  s_hid = new NimBLEHIDDevice(s_server);
  s_hid->setManufacturer("MacSetupKVM (DIY)");
  // PnP ID: source 0x01 = Bluetooth SIG company ID; 0x02E5 = Espressif.
  // Deliberately NOT Apple's IDs.
  s_hid->setPnp(0x01, 0x02E5, 0x4B56, 0x0100);
  s_hid->setHidInfo(0x00, 0x02);  // country 0, flags: normally connectable
  s_hid->setReportMap(REPORT_MAP, sizeof(REPORT_MAP));

  s_kbIn = s_hid->getInputReport(REPORT_ID_KEYBOARD);
  s_kbOut = s_hid->getOutputReport(REPORT_ID_KEYBOARD);  // LED report from host (ignored)
#if HID_ENABLE_MOUSE
  s_msIn = s_hid->getInputReport(REPORT_ID_MOUSE);
  s_msIn->setCallbacks(&s_reportCb);
#else
  LOGF("keyboard-only build (HID_ENABLE_MOUSE=0): mouse commands are ignored");
#endif
  s_kbIn->setCallbacks(&s_reportCb);
  // Diagnostics: see what the host fetches/writes (Report Map, LED report, ...)
  s_hid->getReportMap()->setCallbacks(&s_gattLogCb);
  s_hid->getHidInfo()->setCallbacks(&s_gattLogCb);
  s_hid->getPnp()->setCallbacks(&s_gattLogCb);
  s_hid->getProtocolMode()->setCallbacks(&s_gattLogCb);
  s_hid->getHidControl()->setCallbacks(&s_gattLogCb);
  s_kbOut->setCallbacks(&s_gattLogCb);
  s_hid->setBatteryLevel(100);

  s_server->start();

  s_name = deviceName;
  s_adv = NimBLEDevice::getAdvertising();
  LOGF("starting as \"%s\" (%d paired host(s) remembered)", deviceName, NimBLEDevice::getNumBonds());
  s_pairUntil = millis() + PAIR_WINDOW_MS;   // be easy to find right after power-on
  applyAdvertising(true);
  return true;
}

void requestPairingMode() { if (!s_failed) s_pairRequest = true; }

int passkeySecondsLeft() {
  if (s_pkHandle == BLE_HS_CONN_HANDLE_NONE) return 0;
  int32_t left = (int32_t)(s_pkDeadline - millis());
  return left > 0 ? (left + 999) / 1000 : 0;
}

bool submitPasskey(uint32_t pin) {
  if (s_failed || pin > 999999 || passkeySecondsLeft() == 0) return false;
  s_pin = pin;
  s_pinReady = true;   // service() injects it from the loop task
  return true;
}

int pairingSecondsLeft() {
  if (!s_pairUntil) return 0;
  int32_t left = (int32_t)(s_pairUntil - millis());
  return left > 0 ? (left + 999) / 1000 : 0;
}

void service() {
  if (s_failed) return;
  if (s_pinReady) {
    s_pinReady = false;
    uint16_t h = s_pkHandle;
    if (h != BLE_HS_CONN_HANDLE_NONE && s_server) {
      NimBLEConnInfo ci = s_server->getPeerInfoByHandle(h);
      bool ok = NimBLEDevice::injectPassKey(ci, s_pin);
      LOGF("pairing code sent to the computer: %s", ok ? "OK, waiting for it to confirm" : "FAILED (too late?)");
    }
    s_pin = 0;
  }
  // Diagnostics: report counters whenever they change (at most every 2 s).
  static uint32_t lastLog = 0, lastKb = 0, lastMs = 0, lastFail = 0;
  if (millis() - lastLog > 2000 &&
      (s_sentKb != lastKb || s_sentMs != lastMs || s_failed_reports != lastFail)) {
    lastLog = millis();
    LOGF("reports sent OK: keyboard %u (+%u), mouse %u (+%u), failed %u",
         (unsigned)s_sentKb, (unsigned)(s_sentKb - lastKb), (unsigned)s_sentMs,
         (unsigned)(s_sentMs - lastMs), (unsigned)s_failed_reports);
    lastKb = s_sentKb; lastMs = s_sentMs; lastFail = s_failed_reports;
  }
  if (s_pairRequest) {
    s_pairRequest = false;
    s_pairUntil = millis() + PAIR_WINDOW_MS;
    applyAdvertising(true);   // if connected, takes effect when the link drops
  }
  if (s_pairUntil) {
    bool expired = (int32_t)(millis() - s_pairUntil) >= 0;
    bool paired = s_connected && s_encrypted;
    if (expired || paired) {
      s_pairUntil = 0;
      if (paired) LOGF("computer paired -> leaving pairing mode");
      applyAdvertising(false);
    }
  }
}

BleState state() {
  if (s_failed) return BleState::Failed;
  if (!s_connected) return BleState::Advertising;
  if (s_encrypted) return BleState::Ready;
  return BleState::Connected;
}

const char *stateName() {
  switch (state()) {
    case BleState::Advertising: return "advertising";
    case BleState::Connected:   return "connected";
    case BleState::Ready:       return "ready";
    case BleState::Failed:      return "failed";
  }
  return "?";
}

bool ready() { return state() == BleState::Ready; }

int bondCount() { return s_failed ? 0 : NimBLEDevice::getNumBonds(); }

void tapKey(uint8_t mods, uint8_t usage) {
  if (!ready()) return;
  // Modifiers go down first, in their own report, then the key, then the
  // key comes up, then the modifiers. Hosts never see a key without its
  // modifier, and nothing is left held when this returns.
  if (mods) sendKeyboard(mods, 0);
  if (usage) {
    sendKeyboard(mods, usage);
    vTaskDelay(pdMS_TO_TICKS(linkHoldMs()));
    if (mods) sendKeyboard(mods, 0);
  } else {
    vTaskDelay(pdMS_TO_TICKS(linkHoldMs()));
  }
  sendKeyboard(0, 0);
}

bool typeChar(char c) {
  uint8_t usage, mods;
  if (!asciiToHid(c, usage, mods)) return false;
  tapKey(mods, usage);
  return true;
}

void move(int dx, int dy) {
  if (!ready()) return;
  while (dx != 0 || dy != 0) {
    int8_t sx = clamp127(dx), sy = clamp127(dy);
    sendMouse(0, sx, sy, 0, 0);
    dx -= sx;
    dy -= sy;
  }
}

void scroll(int vertical, int horizontal) {
  if (!ready()) return;
  while (vertical != 0 || horizontal != 0) {
    int8_t sv = clamp127(vertical), sh = clamp127(horizontal);
    sendMouse(0, 0, 0, sv, sh);
    vertical -= sv;
    horizontal -= sh;
  }
}

void click(uint8_t buttons, uint8_t mods) {
  if (!ready() || buttons == 0) return;
  if (mods) sendKeyboard(mods, 0);
  sendMouse(buttons & 0x1F, 0, 0, 0, 0);
  uint32_t hold = linkHoldMs();
  vTaskDelay(pdMS_TO_TICKS(hold > CLICK_HOLD_MS ? hold : CLICK_HOLD_MS));
  sendMouse(0, 0, 0, 0, 0);
  if (mods) sendKeyboard(0, 0);
}

void releaseAll() {
  if (!s_connected) return;
  sendKeyboard(0, 0);
  sendMouse(0, 0, 0, 0, 0);
}

void forgetAllHosts() {
  if (s_failed) return;
  LOGF("forgetting all paired hosts");
  s_pairRequest = true;   // re-advertise in pairing mode (service() applies it)
  if (s_connected && s_server) s_server->disconnect(s_connHandle);
  NimBLEDevice::deleteAllBonds();
}

}  // namespace BleHid
