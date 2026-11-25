#!/usr/bin/env python3
"""
Acceptance tests for MacSetupKVM, run from a Windows PC.

Prerequisites:
  * The PC is paired with "MacSetup KVM" over Bluetooth (Settings > Bluetooth > Add device).
  * The PC's Wi-Fi is joined to the ESP32's network (MacSetupKVM). Wired Ethernet can stay on.
  * Python package `websockets` (installed with esptool).

Usage:
  python tools/ws_test.py            # full suite (opens a small test window; click into it when asked)
  python tools/ws_test.py --host 192.168.4.1 --rounds 3 --delay 25

What it checks:
  1. A wrong token is rejected by the WebSocket handshake.
  2. Every printable ASCII character (0x20..0x7E) types exactly right, N rounds in a row.
  3. Phone goes silent mid-typing (like iPhone Wi-Fi switched off): typing stops, no key stays held.
  4. Socket closed mid-typing (like Safari closed): typing stops, no key stays held.
  5. The device still works afterwards.
It never prints the token.
"""
import argparse
import ctypes
import pathlib
import queue
import re
import sys
import threading
import time
import tkinter as tk

try:
    from websockets.sync.client import connect
    from websockets.exceptions import InvalidStatus, InvalidHandshake
except ImportError:
    sys.exit("Need the 'websockets' package:  python -m pip install --user websockets")

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
user32 = ctypes.windll.user32
VK = {"Shift": 0x10, "Ctrl": 0x11, "Alt/Option": 0x12, "LWin/Cmd": 0x5B, "RWin": 0x5C}


def read_token() -> str:
    m = re.search(r'#define\s+WS_TOKEN\s+"([^"]+)"', (ROOT / "config.h").read_text(encoding="utf-8"))
    if not m:
        sys.exit("WS_TOKEN not found in config.h")
    return m.group(1)


class Device:
    """WebSocket session with heartbeat, like the phone page."""

    def __init__(self, host: str, token: str):
        self.ws = connect(f"ws://{host}/ws?t={token}", open_timeout=5, close_timeout=1)
        self.notes: "queue.Queue[str]" = queue.Queue()
        self.status = {}
        self.heartbeat = True
        self.alive = True
        threading.Thread(target=self._rx, daemon=True).start()
        threading.Thread(target=self._hb, daemon=True).start()

    def _rx(self):
        import json
        try:
            for raw in self.ws:
                m = json.loads(raw)
                if m.get("t") == "note":
                    self.notes.put(m["msg"])
                elif m.get("t") == "status":
                    self.status = m
        except Exception:
            pass
        self.alive = False

    def _hb(self):
        while self.alive:
            if self.heartbeat:
                try:
                    self.ws.send("p")
                except Exception:
                    return
            time.sleep(1)

    def send(self, s: str):
        self.ws.send(s)

    def type(self, text: str, delay: int, enter: bool):
        self.send(f"t {delay} {1 if enter else 0} {text}")

    def wait_note(self, pattern: str, timeout: float) -> str:
        end = time.time() + timeout
        while time.time() < end:
            try:
                n = self.notes.get(timeout=0.2)
            except queue.Empty:
                continue
            if re.search(pattern, n):
                return n
        return ""

    def kill(self):
        """Drop the TCP connection without a WebSocket close frame."""
        self.alive = False
        try:
            self.ws.socket.shutdown(2)
            self.ws.socket.close()
        except Exception:
            pass

    def close(self):
        self.alive = False
        try:
            self.ws.close()
        except Exception:
            pass


def held_modifiers():
    return [name for name, vk in VK.items() if user32.GetAsyncKeyState(vk) & 0x8000]


def layout_is_us() -> bool:
    # 0x04090409 = English (US) language + US layout. US-International (dead keys for ' " ` ~ ^) differs.
    hkl = user32.GetKeyboardLayout(0) & 0xFFFFFFFF
    return hkl == 0x04090409


class Window:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("MacSetupKVM test - click in the box")
        self.root.geometry("820x420")
        self.root.attributes("-topmost", True)
        self.text = tk.Text(self.root, font=("Consolas", 11), wrap="none")
        self.text.pack(fill="both", expand=True)
        self.focused = threading.Event()
        self.text.bind("<FocusIn>", lambda e: self.focused.set())
        self.text.bind("<FocusOut>", lambda e: self.focused.clear())

    def content(self) -> str:
        out = {}
        self.root.after(0, lambda: out.setdefault("v", self.text.get("1.0", "end-1c")))
        t0 = time.time()
        while "v" not in out and time.time() - t0 < 2:
            time.sleep(0.05)
        return out.get("v", "")

    def clear(self):
        self.root.after(0, lambda: self.text.delete("1.0", "end"))
        time.sleep(0.2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--delay", type=int, default=25)
    args = ap.parse_args()
    token = read_token()
    results = []

    def report(name, ok, detail=""):
        results.append((name, ok))
        print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f" - {detail}" if detail else ""), flush=True)

    # 1. wrong token
    try:
        connect(f"ws://{args.host}/ws?t=wrong-token", open_timeout=5).close()
        report("wrong token rejected", False, "connection was accepted")
    except (InvalidStatus, InvalidHandshake) as e:
        report("wrong token rejected", True, type(e).__name__)
    except OSError as e:
        sys.exit(f"Cannot reach {args.host}: {e}. Is this PC on the MacSetupKVM Wi-Fi?")

    dev = Device(args.host, token)
    time.sleep(1.5)
    if dev.status.get("ble") != "ready":
        sys.exit(f"Bluetooth state is '{dev.status.get('ble')}', expected 'ready'. Pair this PC with 'MacSetup KVM' first.")
    print("Connected. Bluetooth: ready.")

    win = Window()
    threading.Thread(target=run_suite, args=(win, dev, args, token, report), daemon=True).start()
    win.root.mainloop()

    print("\nSummary:")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
    sys.exit(0 if results and all(ok for _, ok in results) else 1)


def run_suite(win: Window, dev: Device, args, token, report):
    try:
        print("Click inside the test window to give it keyboard focus...", flush=True)
        win.focused.wait()
        if not layout_is_us():
            print("WARNING: the active keyboard layout is not English (US). Press Win+Space to switch to ENG,"
                  " otherwise symbols will be translated wrongly.", flush=True)
            time.sleep(4)
        time.sleep(1)

        # 2. ASCII rounds
        for r in range(1, args.rounds + 1):
            win.clear()
            if not win.focused.is_set():
                print("Focus lost - click the test window again", flush=True)
                win.focused.wait()
            dev.type(ASCII, args.delay, enter=False)
            note = dev.wait_note(r"^(Typed|Stopped)", timeout=60)
            time.sleep(0.5)
            got = win.content()
            if got == ASCII:
                report(f"ASCII round {r}/{args.rounds}", True, note)
            else:
                pos = next((i for i, (a, b) in enumerate(zip(got, ASCII)) if a != b), min(len(got), len(ASCII)))
                report(f"ASCII round {r}/{args.rounds}", False,
                       f"first difference at index {pos}: expected {ASCII[pos:pos+8]!r}, got {got[pos:pos+8]!r}"
                       f" (len {len(got)} vs {len(ASCII)})")

        long_text = ("Aa!Zz@" * 60)[:300]   # lots of Shift presses

        # 3. silent phone (heartbeat stops, socket stays open)
        win.clear()
        dev.type(long_text, args.delay, enter=False)
        time.sleep(1.5)
        dev.heartbeat = False
        print("Heartbeat stopped (simulating iPhone Wi-Fi off), waiting for the watchdog...", flush=True)
        time.sleep(7)
        n = len(win.content())
        mods = held_modifiers()
        report("silent phone: typing aborted", 0 < n < len(long_text), f"{n}/{len(long_text)} chars typed")
        report("silent phone: no modifier held", not mods, ",".join(mods))
        dev.close()

        # 4. socket killed mid-typing
        dev = Device(args.host, token)
        time.sleep(1)
        win.clear()
        dev.type(long_text, args.delay, enter=False)
        time.sleep(1.5)
        dev.kill()
        time.sleep(3)
        n = len(win.content())
        mods = held_modifiers()
        report("socket dropped: typing aborted", 0 < n < len(long_text), f"{n}/{len(long_text)} chars typed")
        report("socket dropped: no modifier held", not mods, ",".join(mods))

        # 5. still works
        dev = Device(args.host, token)
        time.sleep(1)
        win.clear()
        dev.type("still OK", args.delay, enter=False)
        dev.wait_note(r"^Typed", timeout=10)
        time.sleep(0.5)
        report("works after drops", win.content() == "still OK", repr(win.content()))
        dev.close()
    except Exception as e:  # keep the window from hanging on errors
        report("suite crashed", False, repr(e))
    finally:
        win.root.after(1500, win.root.destroy)


if __name__ == "__main__":
    main()
