# ESP32 Mac mini Setup IO

**Type through macOS Setup Assistant on a brand-new Mac mini, with no
keyboard or mouse, using a classic ESP32 and the browser on your phone or laptop.**

The ESP32 creates its own Wi-Fi network and serves a small web page. Keys
you press on that page are sent to the Mac as a **Bluetooth LE keyboard**.
You watch the Mac's monitor and drive Setup Assistant from your phone until
you can turn on Screen Sharing and SSH. After that the Mac is managed
remotely and the board can go back in the drawer.

```
Phone / laptop browser ──Wi-Fi (WebSocket)──► ESP32 ──Bluetooth LE HID──► Mac mini
                        SSID "MacSetupKVM"            "MacSetup KVM"
```

**Status:** used to set up a new Mac mini (macOS 27.0) from start to finish,
with a classic ESP32 DevKit (ESP32-D0WD-V3, CP210x USB-serial, 4 MB flash).
See [What was tested](#what-was-tested) for what was and wasn't verified.

> **Why Bluetooth and not USB?** The classic ESP32 has **no USB device
> hardware**. Its USB port goes to a USB-to-serial chip (CP210x/CH340), so it
> can never appear to a Mac as a USB keyboard. It *can* be a Bluetooth
> keyboard, which is what this firmware does. If you have an **ESP32-S3**,
> a native USB HID keyboard is simpler and more reliable for Setup Assistant
> (see [Lessons learned](#lessons-learned)).

---

## Contents

1. [Hardware](#1-hardware)
2. [Quick start](#2-quick-start)
3. [Configuration](#3-configuration)
4. [Using it for Mac Setup Assistant](#4-using-it-for-mac-setup-assistant)
5. [Lessons learned](#lessons-learned)
6. [Safety behaviour](#safety-behaviour)
7. [Troubleshooting](#troubleshooting)
8. [Security notes](#security-notes)
9. [How it works](#how-it-works)
10. [What was tested](#what-was-tested)

---

## 1. Hardware

- Any **classic ESP32** board (ESP32-WROOM-32 / "ESP32 DevKit"). It needs
  Bluetooth LE and Wi-Fi. The ESP32-C3/C6/S3 have different radios or USB.
  This code targets the classic ESP32.
- A **USB data cable**. Many USB cables only carry power: if the board's LED
  lights up but no COM port appears, try another cable.
- A phone or laptop with a browser, and a monitor on the Mac.

| Use | Plug the board's USB port into |
|---|---|
| Flashing / serial log | Your computer. It appears as *Silicon Labs CP210x* (or CH340) *USB to UART* |
| Using it with the Mac | **Any USB power source**: charger, power bank or computer. It doesn't need to be plugged into the Mac, because the Mac talks to it over Bluetooth. |

The board's single USB port always remains flashable. Nothing in this
firmware takes it over.

---

## 2. Quick start

Arduino requires the sketch folder to have the same name as the `.ino`
file, so **clone into a folder called `MacSetupKVM`**:

```bash
git clone https://github.com/pankgeorg/ESP32MacMiniSetupIO.git MacSetupKVM
cd MacSetupKVM
cp config.example.h config.h      # then edit config.h (see section 3)
```

### Toolchain (arduino-cli)

```bash
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "NimBLE-Arduino" "ESP Async WebServer" "Async TCP"
```

Versions used: arduino-esp32 core **4.0.0**, NimBLE-Arduino **2.5.2**,
ESP Async WebServer **3.12.1** (ESP32Async), Async TCP **3.6.1** (ESP32Async).
The code also guards for core 3.x.

### Build, flash, watch the log

```bash
# Huge APP partition: 3 MB app, no OTA (this firmware has no OTA anyway)
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" .
arduino-cli board list                                   # find the port (COMx / /dev/ttyUSB0)
arduino-cli upload  --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" -p COM5 .
arduino-cli monitor -p COM5 -c baudrate=115200
```

The serial log shows Wi-Fi, Bluetooth, pairing and watchdog events, and
counts of sent reports. **Typed text is never logged**, only character counts.

On Windows, the first full build of the ESP32 core takes a long time (around
15 minutes). After that, builds are incremental **as long as you keep the same
compile options**. Changing e.g. `--warnings` rebuilds everything.

### Getting into the bootloader

Boards with a CP210x/CH340 enter the bootloader automatically when you
flash. If the upload stalls at `Connecting.....`: hold **BOOT**, tap
**EN/RST**, release **BOOT** once writing starts.

---

## 3. Configuration

All settings are compile-time, in `config.h` (copied from
`config.example.h`; `config.h` is git-ignored). The build **refuses to
compile** while the `CHANGE_ME` placeholders are still there.

| Setting | Meaning |
|---|---|
| `AP_SSID` / `AP_PASSWORD` | Wi-Fi network the ESP32 creates (WPA2, 8–63 characters). Use a random password. |
| `WS_TOKEN` | Access token. The page only connects when opened as `http://192.168.4.1/#<WS_TOKEN>`. |
| `WIFI_USE_STA` | `0` = SoftAP (default). `1` = join `STA_SSID`; the page is then at `http://macsetup.local/#<WS_TOKEN>`. |
| `BLE_DEVICE_NAME` | Name the computer shows: `MacSetup KVM`. |
| `HID_ENABLE_MOUSE` | **Keep `0` (keyboard only) for macOS Setup Assistant.** `1` adds a mouse in the same device. That works on Windows, but on macOS Setup Assistant the pointer worked while every keystroke was ignored (see [Lessons learned](#lessons-learned)). |
| `BLE_ADDRESS_GENERATION` | Bluetooth identity. Increase it to make computers see a brand-new device, e.g. after changing `HID_ENABLE_MOUSE` (macOS caches what a paired device looks like) or when a computer kept an old pairing you can't delete. |
| `CLIENT_TIMEOUT_MS` | If the browser goes quiet this long (default 4 s), typing stops and every key is released. |

Generate a token, e.g.:

```bash
python -c "import secrets; print(secrets.token_urlsafe(15))"
```

**Tip:** open the full URL once on your phone and bookmark it. The token is
after the `#`, and browsers don't send that part to the server with the page
request.

---

## 4. Using it for Mac Setup Assistant

**Order: ESP32 first, then the phone, then the Mac.**

1. **Power the ESP32.** For 3 minutes after power-on it's in **pairing mode**:
   fast, limited-discoverable advertising, with the LED blinking at medium
   speed. If another computer it was paired with is nearby with Bluetooth
   on, it may connect there instead. Use **Settings → Forget paired
   computers** on the page, or hold **BOOT** for 3 s.
2. **Join the Wi-Fi** `MacSetupKVM` on your phone or laptop. iOS will say
   **"No Internet Connection"**. That's expected, so stay connected.
3. **Open** `http://192.168.4.1/#<WS_TOKEN>`. The top bar should show
   **Phone ↔ ESP32: connected**.
4. **Power on the Mac** (turn the volume down, because macOS soon starts
   reading a VoiceOver introduction aloud). With no input devices, Setup
   Assistant shows a keyboard panel and searches for a Bluetooth keyboard.
   If the page's pairing countdown ran out, tap **Start pairing mode (3 min)**.
   It can take a minute or two for the Mac to connect. On a new Mac mini the
   **power button is underneath**. Picking the Mac up can press it, and
   holding it for ~5 s or more shuts the Mac down.
5. **Pairing code.** When the Mac connects it asks for a 6-digit code: a
   **Pairing code** box pops up on the page. Type the code shown on the
   Mac there and tap **Send code** within ~30 s. The page then shows
   **Mac: paired & connected** and the LED stays on.
6. **Move through the screens with the keyboard:**

   | Key on the page | What it does in Setup Assistant |
   |---|---|
   | **⌃F7 Tab-to-all** | Tap once first, so Tab reaches buttons, not just lists and fields |
   | **Tab / ⇧Tab** | Move between controls |
   | **↑ / ↓** | Choose in lists (country, Wi-Fi network, …) |
   | **Space** | Press the selected button. On several screens Space did what ⏎ normally does |
   | **⏎** | Default button, when there is one |
   | **⌘F5 VoiceOver** | Turn VoiceOver off/on. (The bottom hint says *"Press Escape to hear how to set up your Mac with VoiceOver"*. Esc starts that guide, it doesn't skip screens) |
   | **F1–F12** | Under the collapsible section; combine with the latched ⌘ ⌃ ⌥ ⇧ |

   A short "drop" sound means the Mac received the key but it does nothing
   on that screen, e.g. ⏎ while *Continue* is still disabled.
7. **Keyboard Setup Assistant** may ask you to identify the keyboard: type
   `z` (key right of left Shift) and `/` (key left of right Shift), and choose
   **ANSI** if asked.
8. **Keep a U.S. / ABC input source.** The firmware sends US-ANSI key codes.
   If you pick a region whose keyboard gets added (e.g. Greek), make sure
   ABC/U.S. stays the active input source. **⌃Space** switches.
9. **Typing names and passwords:** use the **Type** box. **Send** types
   exactly what's in it, and **Send + ⏎** also presses Return. **Hide** masks the
   box. It clears after sending and nothing is stored. Only US-keyboard
   ASCII can be typed; iPhone "smart" quotes and dashes are converted back
   to plain `'` `"` `-` `--`, and anything else is refused before sending.
10. **"Transfer your data" → "Set up with iPhone or iPad"** (macOS 15.4+ with
    iOS 18.4+) brings your Apple Account and Wi-Fi networks over without
    typing. While the iPhone is busy with that, drive the page from a laptop
    on the `MacSetupKVM` Wi-Fi instead. The newest connection takes over.
11. **At the desktop:** ⌘Space → `Sharing` → turn on **Screen Sharing** and
    **Remote Login (SSH)**. The ⓘ next to each shows the Mac's address.
    Then remove *MacSetup KVM* in the Mac's Bluetooth settings and tap
    **Forget paired computers** on the page.

---

## Lessons learned

Things that cost real time and are worth knowing if you build something similar:

1. **macOS Setup Assistant ignored the keyboard of a combined keyboard+mouse
   BLE device.** With one HID report map containing both a keyboard and a
   mouse, the Mac paired, subscribed to both input reports and moved the
   pointer, but discarded every keystroke. macOS never wrote the keyboard LED
   output report, which suggests its keyboard driver never attached. Windows
   used both parts fine. **A keyboard-only report map worked immediately.**
   Similar "mouse works, keyboard doesn't on Mac" reports exist for composite
   devices on other stacks.
2. **Pair with a code.** The board pairs as an authenticated keyboard
   (`KEYBOARD_ONLY` I/O capability, MITM + LE Secure Connections). The Mac
   shows a 6-digit passkey and the phone page injects it. That's how real
   BLE keyboards pair with a Mac. "Just Works" pairing (no code) also
   connected, but on its own didn't make the keyboard work.
3. **Advertise as LE-only.** NimBLE's default advertising flags omit
   *"BR/EDR not supported"*, so the device looked dual-mode. With the flag
   set, plus limited-discoverable advertising at 20 ms ("pairing mode"), the
   Mac found it. Phones listed it either way.
4. **macOS caches a paired device's description.** After changing the report
   map, give the board a new Bluetooth address (`BLE_ADDRESS_GENERATION`).
   Otherwise the Mac keeps using the old description.
5. **arduino-esp32 core 4.x frees Bluetooth memory at boot** unless a library
   declares it uses BLE (`esp32-hal-misc.c`: `if (!bleInUse())`).
   NimBLE-Arduino 2.5.2 predates this, so `NimBLEDevice::init()` failed with
   `esp_bt_controller_init() -> ESP_ERR_INVALID_STATE`, and the next NimBLE
   call asserted and boot-looped. Fix: `#include "esp32-hal-alloc-ble-mem.h"`
   (done in `ble_hid.cpp`, guarded for core 3.x). Also: check `init()`'s
   return value.
6. **Never hold a key between messages.** Latched modifiers live in the page.
   Every key/click is one atomic press→release sequence on the device, so a
   dropped connection can't leave ⌘ stuck down.
7. **Don't use Arduino's `constrain()` on an expression with side effects.**
   It's a macro and evaluates its argument more than once, e.g.
   `constrain(strtol(p, &p, 10), …)` advances `p` several times.
8. **Arduino's auto-prototypes go before the first function in the `.ino`.**
   A small helper function placed above your `struct` declarations makes
   every generated prototype that uses that struct fail
   (`'enqueue' cannot be used as a function`). Put such helpers in a header.
9. **A USB port that only gives power** looks exactly like "the board is
   running but no COM port": the LED blinks, Windows sees nothing. The
   firmware can't hide the CP210x, so it's always the cable or port.

---

## Safety behaviour

- **STOP** (red, top right) aborts typing and releases every key and button.
- **No stuck keys by design:** each key tap is sent as *modifiers down → key
  down → key up → modifiers up* in one go. The latched ⌘/⌃/⌥/⇧ buttons only
  exist in the page.
- **Watchdog:** the page sends a heartbeat every second. If it's silent for
  4 s (Wi-Fi off, browser in background, phone locked), the ESP32 aborts
  typing and releases everything. The same happens when the WebSocket closes
  or Bluetooth disconnects.
- **Bluetooth drops:** if the Mac and board are far apart, the link can time
  out (`reason 0x208`). Typing stops safely, and the Mac reconnects by itself.
  Keep the board within a metre or two.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Page says *Can't connect. Is the token right?* | The `#token` in the URL doesn't match `WS_TOKEN`. Enter it in the box that appears. |
| Page keeps *reconnecting…*, controls greyed out | The phone left the board's Wi-Fi or locked. Rejoin `MacSetupKVM`. While greyed out, touches scroll the page instead of controlling it. |
| Phone keeps switching back to home Wi-Fi | Turn off *Auto-Join* for the home network during setup. |
| Mac doesn't find the board | The board may be connected to another paired computer (it stops advertising): turn that computer's Bluetooth off or **Forget paired computers**. Tap **Start pairing mode**, wait 1–2 minutes, and if needed tap the Mac's power button once (don't hold it). |
| Mac pairs, pointer works, keys do nothing | You're running with `HID_ENABLE_MOUSE 1`. Set it to `0`, **increase `BLE_ADDRESS_GENERATION`**, re-flash and pair again. |
| Pairing code box doesn't appear / code rejected | Keep the page open and connected. Enter the code within ~30 s. If it times out, tap **Start pairing mode** and let the Mac search again. |
| Tab only plays a sound | Tap **⌃F7 Tab-to-all**. In lists, use ↑/↓ and Space. |
| Letters fine but symbols wrong | The Mac's input source isn't U.S./ABC, or the keyboard type was set to ISO. Choose ANSI and ABC/U.S. |
| Characters missing when typing fast | Raise **Delay per character** in Settings (50–100 ms). |
| LED blinks but no COM port on the PC | A power-only cable/port. Use a data cable, preferably a port directly on the computer. |
| Boot loop with `assert failed: npl_freertos_mutex_pend` | Bluetooth failed to start (see Lessons learned #5). Current code logs the reason and keeps Wi-Fi running instead of rebooting. |
| Upload fails with *Failed to connect* | BOOT + EN/RST sequence (section 2), or another cable. |

---

## Security notes

- Joining the board's network needs the WPA2 password (`AP_PASSWORD`), max 2 clients.
- Sending keystrokes also needs `WS_TOKEN`: the WebSocket handshake is
  rejected without it, and rejections are logged.
- One controller at a time: the newest connection wins.
- Bluetooth pairing is authenticated with a 6-digit code entered through the
  token-protected page.
- No OTA, no file upload, nothing typed is stored. Text buffers are wiped
  after use, and the log shows only character counts.
- The Bluetooth identity uses Espressif's Bluetooth SIG company ID in the PnP
  ID. It never pretends to be an Apple device.
- Only power the board while you're using it.

---

## How it works

| File | What it is |
|---|---|
| `MacSetupKVM.ino` | Wi-Fi (SoftAP/STA), web server, WebSocket protocol + token check, command queue, HID worker task, watchdog, status LED, BOOT button |
| `ble_hid.cpp/.h` | Bluetooth LE HID (NimBLE): report map, authenticated pairing with passkey injection, pairing-mode advertising, LE-only flags, configurable identity, report counters |
| `keymap.h` | ASCII → US-ANSI HID usage codes (+ Shift) |
| `web_ui.h` | The single-page UI (HTML/CSS/JS embedded, no external resources, nothing stored in the browser) |
| `config.example.h` | Settings template. Copy to `config.h` |
| `config_check.h` | Compile-time checks on `config.h` (password length, no `CHANGE_ME` placeholders) |
| `tools/ws_test.py` | Automated tests from a Windows PC (see below) |

**Threads:** the AsyncTCP task parses WebSocket messages into a queue. A
single HID worker task is the only one that sends Bluetooth reports.
`loop()` runs the watchdog, status broadcasts, advertising/pairing state, the
LED and the BOOT button.

**WebSocket protocol** (text frames, one command each; see `MacSetupKVM.ino`):

| Message | Meaning |
|---|---|
| `p` | heartbeat (every second) |
| `k <mods> <usage>` | key tap with modifiers (HID usage, keyboard page) |
| `t <delay> <enter> <text>` | type text with per-character delay, optionally followed by Return |
| `m <dx> <dy>` / `s <v> <h>` / `c <buttons> <mods>` | mouse move / scroll / click (mouse builds only) |
| `x` | STOP: abort typing, release all |
| `P` | pairing mode for 3 minutes |
| `K <6 digits>` | pairing code shown on the computer |
| `f` | forget all paired computers |

The board pushes `{"t":"status",…}` (Bluetooth state, pairing-mode and
code countdowns, queue, firmware version) and `{"t":"note","msg":…}` messages.

---

## What was tested

| Test | Result |
|---|---|
| Hardware check with esptool | ✅ classic ESP32-D0WD-V3 rev 3.0/3.1, 4 MB flash, CP210x |
| Keyboard + mouse on Windows 11 | ✅ typing and pointer worked |
| macOS Setup Assistant, keyboard+mouse build | ❌ pointer worked, keys ignored |
| macOS Setup Assistant, **keyboard-only build**, code pairing | ✅ completed setup through to the desktop, enabled Screen Sharing and SSH |
| Reconnect after re-flashing / reboot | ✅ the Mac reconnected by itself (same identity) |
| Release-all on WebSocket disconnect | ✅ seen in the log on every disconnect |
| Every printable ASCII character, 3× in a row | ⚠️ not run. `tools/ws_test.py` automates it (PC paired + on the board's Wi-Fi) |
| Wi-Fi switched off mid-typing → no stuck keys | ⚠️ not run deliberately. `tools/ws_test.py` covers both "silent phone" and "socket dropped" |
| Mouse in Setup Assistant | ❌ not usable together with the keyboard (see Lessons learned #1) |

`tools/ws_test.py` (Python + `websockets`) types into its own window on a
Windows PC paired with the board, compares the result character by
character, simulates a silent phone and a dropped socket mid-typing, and
checks that no modifier key is left held.

---

## License

[MIT](LICENSE)
