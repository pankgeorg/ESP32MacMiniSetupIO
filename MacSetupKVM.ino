// =====================================================================
//  MacSetupKVM — phone-controlled Bluetooth keyboard + mouse
//  Target: classic ESP32 (ESP32-D0WD / WROOM-32 DevKit)
//
//  iPhone (Safari) --Wi-Fi/WebSocket--> ESP32 --Bluetooth LE HID--> Mac
//
//  Threads:
//   * AsyncTCP task : HTTP + WebSocket; parses messages, puts commands on a queue
//   * HID worker    : the ONLY task that sends HID reports; executes the queue
//   * loop()        : watchdog, status broadcast, LED, BOOT button
//
//  Never logs typed text. Never stores typed text (buffers are wiped).
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "config_check.h"
#include "keymap.h"
#include "ble_hid.h"
#include "web_ui.h"

#if HID_ENABLE_MOUSE
#define FW_VERSION "1.2.2"
#else
#define FW_VERSION "1.2.2-kbd"
#endif
#define LOGF(fmt, ...) Serial.printf("[main] " fmt "\n", ##__VA_ARGS__)

// Config sanity checks (password length, no CHANGE_ME placeholders) live in
// config_check.h, included above.

// ---------------------------------------------------------------------
//  Command queue
// ---------------------------------------------------------------------
enum CmdType : uint8_t { CMD_KEY, CMD_TEXT, CMD_MOVE, CMD_SCROLL, CMD_CLICK, CMD_RELEASE, CMD_FORGET };

struct Cmd {
  CmdType type;
  uint32_t gen;       // abort generation at enqueue time
  int16_t a, b;       // KEY: mods,usage  MOVE: dx,dy  SCROLL: v,h  CLICK: buttons,mods
  uint16_t delayMs;   // TEXT: per-character delay
  bool enter;         // TEXT: press Enter afterwards
  uint16_t len;       // TEXT: length
  char *text;         // TEXT: heap buffer, wiped + freed by the worker
};

static QueueHandle_t g_queue;
static volatile uint32_t g_gen = 1;        // bump => everything queued/running is abandoned
static volatile bool g_busy = false;       // worker is executing a command

// Active WebSocket client (only one controls the device at a time)
static volatile uint32_t g_activeClient = 0;
static volatile uint32_t g_lastRxMs = 0;
static volatile bool g_forceStatus = false;   // send status ASAP (new phone connected)

// One-line notes from the worker to the phone, sent by loop()
static portMUX_TYPE g_noteMux = portMUX_INITIALIZER_UNLOCKED;
static char g_note[96];
static volatile bool g_notePending = false;

static AsyncWebServer g_http(80);
static AsyncWebSocket g_ws("/ws");

static void postNote(const char *msg) {
  portENTER_CRITICAL(&g_noteMux);
  strlcpy(g_note, msg, sizeof(g_note));
  g_notePending = true;
  portEXIT_CRITICAL(&g_noteMux);
}

static bool enqueue(Cmd &c) {
  c.gen = g_gen;
  if (xQueueSend(g_queue, &c, 0) == pdTRUE) return true;
  if (c.text) { memset(c.text, 0, c.len); free(c.text); c.text = nullptr; }
  return false;
}

static void enqueueSimple(CmdType t, int16_t a = 0, int16_t b = 0) {
  Cmd c = {};
  c.type = t; c.a = a; c.b = b;
  enqueue(c);
}

// Abandon everything in flight and make sure nothing is held down.
static void abortAndRelease(const char *why) {
  g_gen = g_gen + 1;
  enqueueSimple(CMD_RELEASE);
  LOGF("release-all: %s", why);
}

// ---------------------------------------------------------------------
//  HID worker
// ---------------------------------------------------------------------
static void runText(const Cmd &c) {
  if (!BleHid::ready()) { postNote("Mac not connected - nothing typed"); return; }
  uint16_t typed = 0, skipped = 0;
  bool aborted = false;
  for (uint16_t i = 0; i < c.len; ++i) {
    if (c.gen != g_gen || !BleHid::ready()) { aborted = true; break; }
    if (BleHid::typeChar(c.text[i])) typed++; else skipped++;
    if (c.delayMs) vTaskDelay(pdMS_TO_TICKS(c.delayMs));
  }
  if (!aborted && c.enter) BleHid::tapKey(0, HID_KEY_ENTER);
  // Log counts only, never content.
  LOGF("typed %u/%u chars%s%s", typed, c.len, c.enter && !aborted ? " + Enter" : "",
       aborted ? " (ABORTED)" : "");
  char note[96];
  if (aborted) snprintf(note, sizeof(note), "Stopped after %u of %u characters", typed, c.len);
  else if (skipped) snprintf(note, sizeof(note), "Typed %u characters, skipped %u unsupported", typed, skipped);
  else snprintf(note, sizeof(note), "Typed %u characters%s", typed, c.enter ? " + Enter" : "");
  postNote(note);
}

static void hidWorker(void *) {
  Cmd c;
  for (;;) {
    if (xQueueReceive(g_queue, &c, portMAX_DELAY) != pdTRUE) continue;
    bool stale = (c.gen != g_gen) && c.type != CMD_RELEASE && c.type != CMD_FORGET;
    if (!stale) {
      g_busy = true;
      switch (c.type) {
        case CMD_KEY:     BleHid::tapKey((uint8_t)c.a, (uint8_t)c.b); break;
        case CMD_TEXT:    runText(c); break;
        case CMD_MOVE:    BleHid::move(c.a, c.b); break;
        case CMD_SCROLL:  BleHid::scroll(c.a, c.b); break;
        case CMD_CLICK:   BleHid::click((uint8_t)c.a, (uint8_t)c.b); break;
        case CMD_RELEASE: BleHid::releaseAll(); break;
        case CMD_FORGET:  BleHid::forgetAllHosts(); postNote("Forgot all paired computers"); break;
      }
      g_busy = false;
    }
    if (c.text) { memset(c.text, 0, c.len); free(c.text); }
  }
}

// ---------------------------------------------------------------------
//  WebSocket protocol (text frames, one command per frame)
//    p                      heartbeat
//    m <dx> <dy>            mouse move (relative)
//    s <v> <h>              scroll
//    c <buttons> <mods>     click (1=left 2=right 4=middle), optional modifiers
//    k <mods> <usage>       key tap with modifiers (HID usage, keyboard page)
//    t <delay> <enter> <text...>   type text
//    x                      STOP: abort typing + release all
//    f                      forget all paired computers
//    P                      pairing mode (fast limited-discoverable advertising, 3 min)
//    K <6 digits>           pairing code shown on the computer's screen
// ---------------------------------------------------------------------
static bool tokenMatches(const String &given) {
  const char *expected = WS_TOKEN;
  size_t n = strlen(expected);
  if (given.length() != n) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < n; ++i) diff |= (uint8_t)given[i] ^ (uint8_t)expected[i];
  return diff == 0;
}

static void replyNote(AsyncWebSocketClient *client, const char *msg) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"t\":\"note\",\"msg\":\"%s\"}", msg);
  client->text(buf);
}

// Parse the next integer and clamp it. (Not Arduino's constrain(): that is a
// macro and would evaluate strtol - and advance the pointer - several times.)
static long nextInt(char *&p, long lo, long hi) {
  long v = strtol(p, &p, 10);
  return v < lo ? lo : (v > hi ? hi : v);
}

static void handleCommand(AsyncWebSocketClient *client, char *msg, size_t len) {
  if (len == 0) return;
  char *p = msg + 1;
  Cmd c = {};
  switch (msg[0]) {
    case 'p':
      return;
    case 'm':
      c.type = CMD_MOVE;
      c.a = (int16_t)nextInt(p, -2000, 2000);
      c.b = (int16_t)nextInt(p, -2000, 2000);
      if (!enqueue(c)) LOGF("queue full, move dropped");
      return;
    case 's':
      c.type = CMD_SCROLL;
      c.a = (int16_t)nextInt(p, -500, 500);
      c.b = (int16_t)nextInt(p, -500, 500);
      enqueue(c);
      return;
    case 'c':
      c.type = CMD_CLICK;
      c.a = (int16_t)(nextInt(p, 0, 0x1F));
      c.b = (int16_t)(nextInt(p, 0, 0x0F));
      if (!enqueue(c)) replyNote(client, "Busy - click dropped");
      return;
    case 'k':
      c.type = CMD_KEY;
      c.a = (int16_t)(nextInt(p, 0, 0x0F));
      c.b = (int16_t)(nextInt(p, 0, 0xFF));
      LOGF("key mods=0x%02X usage=0x%02X", c.a, c.b);
      if (!enqueue(c)) replyNote(client, "Busy - key dropped");
      return;
    case 't': {
      long d = nextInt(p, 0, MAX_CHAR_DELAY_MS);
      long e = nextInt(p, 0, 1);
      if (*p == ' ') p++;
      size_t tlen = len - (size_t)(p - msg);
      if (tlen == 0) return;
      if (tlen > MAX_TEXT_LEN) { replyNote(client, "Text too long"); return; }
      c.type = CMD_TEXT;
      c.delayMs = (uint16_t)d;
      c.enter = (e != 0);
      c.len = (uint16_t)tlen;
      c.text = (char *)malloc(tlen);
      if (!c.text) { replyNote(client, "Out of memory"); return; }
      memcpy(c.text, p, tlen);
      LOGF("type request: %u chars, delay %u ms%s", c.len, c.delayMs, c.enter ? ", +Enter" : "");
      if (!enqueue(c)) replyNote(client, "Busy - text not sent, try again");
      return;
    }
    case 'x':
      abortAndRelease("STOP pressed");
      replyNote(client, "Stopped, all keys released");
      return;
    case 'f':
      c.type = CMD_FORGET;
      enqueue(c);
      return;
    case 'K': {   // pairing code typed on the phone (not logged)
      long pin = nextInt(p, -1, 999999);
      if (pin < 0) { replyNote(client, "Pairing code must be 6 digits"); return; }
      replyNote(client, BleHid::submitPasskey((uint32_t)pin) ? "Code sent - watch the Mac"
                                                             : "The computer is not asking for a code right now");
      return;
    }
    case 'P':
      BleHid::requestPairingMode();
      LOGF("pairing mode requested from the page");
      replyNote(client, "Pairing mode on for 3 minutes");
      return;
    default:
      return;
  }
}

static void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type,
                      void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      g_activeClient = client->id();
      g_lastRxMs = millis();
      g_forceStatus = true;
      LOGF("phone connected (ws client %u from %s)", (unsigned)client->id(), client->remoteIP().toString().c_str());
      break;

    case WS_EVT_DISCONNECT:
      LOGF("ws client %u disconnected", (unsigned)client->id());
      if (client->id() == g_activeClient) {
        g_activeClient = 0;
        abortAndRelease("phone disconnected");
      }
      break;

    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      // Only single-frame text messages; anything else is ignored.
      if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;
      if (client->id() != g_activeClient) {
        // A newer connection has taken over; ignore stragglers.
        return;
      }
      g_lastRxMs = millis();
      if (len > MAX_TEXT_LEN + 32) { replyNote(client, "Message too long"); return; }
      char buf[MAX_TEXT_LEN + 33];
      memcpy(buf, data, len);
      buf[len] = 0;
      handleCommand(client, buf, len);
      memset(buf, 0, sizeof(buf));   // don't leave typed text on the stack
      break;
    }

    default:
      break;
  }
}

// ---------------------------------------------------------------------
//  Wi-Fi + HTTP
// ---------------------------------------------------------------------
static void startWifi() {
#if WIFI_USE_STA
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(STA_SSID, STA_PASSWORD);
  LOGF("joining Wi-Fi \"%s\"...", STA_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    LOGF("Wi-Fi OK, IP %s", WiFi.localIP().toString().c_str());
    if (MDNS.begin(MDNS_HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      LOGF("mDNS: http://%s.local/", MDNS_HOSTNAME);
    }
  } else {
    LOGF("Wi-Fi join FAILED (will keep retrying in background)");
  }
#else
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CLIENTS);
  LOGF("SoftAP \"%s\" %s, open http://%s/#<WS_TOKEN from config.h>", AP_SSID, ok ? "up" : "FAILED",
       WiFi.softAPIP().toString().c_str());
#endif
}

static void startHttp() {
  g_ws.handleHandshake([](AsyncWebServerRequest *req) {
    bool ok = req->hasParam("t") && tokenMatches(req->getParam("t")->value());
    if (!ok) LOGF("rejected WebSocket from %s (bad or missing token)", req->client()->remoteIP().toString().c_str());
    return ok;
  });
  g_ws.onEvent(onWsEvent);
  g_http.addHandler(&g_ws);

  g_http.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *res =
      req->beginResponse(200, "text/html; charset=utf-8", (const uint8_t *)INDEX_HTML, strlen(INDEX_HTML));
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });
  g_http.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "Not found"); });
  g_http.begin();
}

// ---------------------------------------------------------------------
//  Status, watchdog, LED, BOOT button
// ---------------------------------------------------------------------
static void broadcastStatus(bool force) {
  static char last[160];
  static uint32_t lastSent = 0;
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"t\":\"status\",\"ble\":\"%s\",\"pair\":%d,\"pk\":%d,\"bonds\":%d,\"busy\":%d,\"q\":%u,\"fw\":\"%s\"}",
           BleHid::stateName(), BleHid::pairingSecondsLeft(), BleHid::passkeySecondsLeft(),
           BleHid::bondCount(), g_busy ? 1 : 0,
           (unsigned)uxQueueMessagesWaiting(g_queue), FW_VERSION);
  if (force || strcmp(buf, last) != 0 || millis() - lastSent > 3000) {
    if (g_ws.count() > 0) g_ws.textAll(buf);
    strlcpy(last, buf, sizeof(last));
    lastSent = millis();
  }
}

static void updateLed() {
#if STATUS_LED_PIN >= 0
  uint32_t t = millis();
  bool on;
  switch (BleHid::state()) {
    case BleState::Ready:     on = true; break;                 // solid: Mac paired
    case BleState::Connected: on = (t / 125) % 2; break;        // fast blink: pairing
    default:                                                     // advertising:
      on = BleHid::pairingSecondsLeft() ? (t / 250) % 2          //   medium blink = pairing mode
                                        : (t / 500) % 2;         //   slow blink   = normal
      break;
  }
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#endif
}

static void checkForgetButton() {
  static uint32_t pressedSince = 0;
  static bool fired = false;
  if (digitalRead(FORGET_BONDS_PIN) == LOW) {
    if (pressedSince == 0) pressedSince = millis();
    if (!fired && millis() - pressedSince > 3000) {
      fired = true;
      LOGF("BOOT held 3 s -> forgetting paired computers");
      enqueueSimple(CMD_FORGET);
    }
  } else {
    pressedSince = 0;
    fired = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  LOGF("MacSetupKVM %s (BLE keyboard+mouse) on %s rev %d", FW_VERSION, ESP.getChipModel(), ESP.getChipRevision());

#if STATUS_LED_PIN >= 0
  pinMode(STATUS_LED_PIN, OUTPUT);
#endif
  pinMode(FORGET_BONDS_PIN, INPUT_PULLUP);

  g_queue = xQueueCreate(48, sizeof(Cmd));
  xTaskCreatePinnedToCore(hidWorker, "hid", 6144, nullptr, 2, nullptr, 1);

  startWifi();
  if (!BleHid::begin(BLE_DEVICE_NAME)) LOGF("Bluetooth NOT available - the web page will say so");
  startHttp();
  LOGF("ready; free heap %u bytes", (unsigned)ESP.getFreeHeap());
}

void loop() {
  static uint32_t lastTick = 0;
  uint32_t now = millis();

  // Watchdog: the page sends a heartbeat every second. If the phone goes
  // quiet (Wi-Fi off, Safari backgrounded, screen locked), stop everything.
  uint32_t active = g_activeClient;
  if (active != 0 && now - g_lastRxMs > CLIENT_TIMEOUT_MS) {
    g_activeClient = 0;
    abortAndRelease("phone silent (heartbeat timeout)");
    AsyncWebSocketClient *cl = g_ws.client(active);
    if (cl) cl->close();
  }

  if (g_notePending) {
    char note[96];
    portENTER_CRITICAL(&g_noteMux);
    strlcpy(note, g_note, sizeof(note));
    g_notePending = false;
    portEXIT_CRITICAL(&g_noteMux);
    char buf[140];
    snprintf(buf, sizeof(buf), "{\"t\":\"note\",\"msg\":\"%s\"}", note);
    if (g_ws.count() > 0) g_ws.textAll(buf);
  }

  if (now - lastTick >= 250 || g_forceStatus) {
    lastTick = now;
    broadcastStatus(g_forceStatus);
    g_forceStatus = false;
    g_ws.cleanupClients(1);   // newest connection wins; older ones are closed
  }

  BleHid::service();
  checkForgetButton();
  updateLed();
  delay(20);
}
