// Embedded single-page UI. No external resources, nothing written to browser storage.
#pragma once

static const char INDEX_HTML[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<title>Mac Setup KVM</title>
<style>
:root{
  --bg:#f2f2f5; --card:#fff; --ink:#1c1c1e; --mute:#6e6e73; --line:#d8d8dc;
  --key:#e9e9ee; --keyOn:#0a64d6; --keyOnInk:#fff; --ok:#1f9d55; --warn:#c77700; --bad:#d12f2f;
  --pad:#e3e7ee;
}
@media (prefers-color-scheme:dark){
  :root{ --bg:#000; --card:#1c1c1e; --ink:#f2f2f7; --mute:#98989f; --line:#38383a;
    --key:#2c2c2e; --keyOn:#2f7cf6; --pad:#25282e; }
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--ink);
  font:16px/1.35 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
body{touch-action:manipulation;padding-bottom:env(safe-area-inset-bottom)}
button{font:inherit;color:inherit;border:0;border-radius:12px;background:var(--key);
  min-height:48px;padding:0 10px;touch-action:manipulation;-webkit-user-select:none;user-select:none;
  -webkit-touch-callout:none}
button:active{filter:brightness(.85)}
button.on{background:var(--keyOn);color:var(--keyOnInk)}
button.primary{background:var(--keyOn);color:var(--keyOnInk);font-weight:600}
header{position:sticky;top:0;z-index:5;display:flex;gap:8px;align-items:center;
  padding:calc(env(safe-area-inset-top) + 8px) 12px 8px;background:var(--bg);border-bottom:1px solid var(--line)}
.st{flex:1;min-width:0;font-size:13px;line-height:1.25}
.st div{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;background:var(--bad);vertical-align:1px}
.dot.ok{background:var(--ok)} .dot.warn{background:var(--warn)}
#stop{background:var(--bad);color:#fff;font-weight:700;min-width:84px}
main{padding:10px 12px 24px;display:flex;flex-direction:column;gap:12px}
main.off .ctl{opacity:.4;pointer-events:none}
.card{background:var(--card);border-radius:16px;padding:12px}
.row{display:flex;gap:8px;margin-top:8px}
.row>*{flex:1}
input[type=text],input[type=number]{width:100%;font-size:17px;padding:12px;border-radius:12px;
  border:1px solid var(--line);background:var(--bg);color:var(--ink)}
input.masked{-webkit-text-security:disc}
#note{min-height:20px;margin-top:8px;font-size:14px;color:var(--mute)}
#note.err{color:var(--bad)}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.grid .w2{grid-column:span 2}
.mods{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:8px}
.lbl{font-size:12px;color:var(--mute);margin:0 0 6px 2px;text-transform:uppercase;letter-spacing:.04em}
#pad{height:42vh;min-height:220px;border-radius:14px;background:var(--pad);touch-action:none;
  display:flex;align-items:center;justify-content:center;text-align:center;color:var(--mute);font-size:13px;
  -webkit-user-select:none;user-select:none}
.nudge{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:8px}
.seg{display:flex;gap:6px;margin-top:8px}
.seg button{flex:1;min-height:40px}
details summary{font-weight:600;min-height:32px;display:flex;align-items:center}
.set{display:flex;align-items:center;gap:10px;margin:12px 0}
.set label{flex:1}
.set input[type=range]{flex:1.4}
.set output{width:44px;text-align:right;font-variant-numeric:tabular-nums}
.set input[type=checkbox]{width:24px;height:24px}
#tokenBox p{margin:0 0 8px}
.small{font-size:13px;color:var(--mute)}
</style>
</head>
<body>
<header>
  <div class="st">
    <div><span id="wsDot" class="dot"></span><span id="wsTxt">Phone: connecting…</span></div>
    <div><span id="bleDot" class="dot"></span><span id="bleTxt">Mac: unknown</span></div>
  </div>
  <button id="stop" title="Abort typing and release every key">STOP</button>
</header>

<main id="main" class="off">
  <section class="card" id="tokenBox" hidden>
    <p><b>Access token needed.</b> Open this page as <code>http://192.168.4.1/#TOKEN</code>, or enter it here.</p>
    <input type="text" id="tok" autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false" placeholder="token from config.h">
    <div class="row"><button class="primary" id="tokGo">Connect</button></div>
  </section>

  <section class="card ctl" id="pkBox" hidden style="outline:3px solid var(--keyOn)">
    <div class="lbl">Pairing code</div>
    <p style="margin:0 0 8px"><b>The computer shows a 6-digit code.</b> Type it here and tap Send code
      (<span id="pkLeft">30</span> s left).</p>
    <input type="text" id="pk" inputmode="numeric" pattern="[0-9]*" maxlength="6" autocomplete="off"
           placeholder="123456" style="font-size:28px;letter-spacing:.3em;text-align:center">
    <div class="row"><button class="primary" id="pkGo">Send code</button></div>
  </section>

  <section class="card ctl" id="pairBox" hidden>
    <div class="lbl">Bluetooth pairing</div>
    <div id="pairTxt">Not in pairing mode</div>
    <div class="row"><button class="primary" id="pairGo">Start pairing mode (3 min)</button></div>
    <p class="small" style="margin:8px 0 0">Start this, then make the Mac search for a keyboard
      (Setup Assistant: press the Mac's power button once when it asks for a keyboard).</p>
  </section>

  <section class="card ctl">
    <div class="lbl">Type</div>
    <input type="text" id="txt" autocomplete="off" autocorrect="off" autocapitalize="none"
           spellcheck="false" enterkeyhint="send" placeholder="Text to type on the Mac">
    <div class="row">
      <button class="primary" id="send">Send</button>
      <button id="sendEnter">Send + ⏎</button>
      <button id="mask" style="flex:.5" title="Hide text">Hide</button>
    </div>
    <div id="note"></div>
  </section>

  <section class="card ctl">
    <div class="lbl">Modifiers (tap to latch for the next key or click)</div>
    <div class="mods">
      <button data-mod="8">⌘ Cmd</button><button data-mod="1">⌃ Ctrl</button>
      <button data-mod="4">⌥ Opt</button><button data-mod="2">⇧ Shift</button>
    </div>
    <div class="grid" id="keys">
      <button data-k="0,41">Esc</button><button data-k="0,43">Tab</button>
      <button data-k="2,43">⇧Tab</button><button data-k="0,42">⌫</button>
      <button data-k="0,44" class="w2">Space</button><button data-k="0,76">Del ⌦</button>
      <button data-k="0,40" class="primary">⏎</button>
      <button data-k="0,80">←</button><button data-k="0,82">↑</button>
      <button data-k="0,81">↓</button><button data-k="0,79">→</button>
      <button data-k="8,4">⌘A</button><button data-k="8,25">⌘V</button>
      <button data-k="8,20">⌘Q</button><button data-k="8,44">⌘Space</button>
      <button data-k="8,62" class="w2">⌘F5 VoiceOver</button><button data-k="1,64" class="w2">⌃F7 Tab-to-all</button>
    </div>
    <details style="margin-top:8px">
      <summary>F1–F12 (combine with the modifiers above)</summary>
      <div class="grid" style="margin-top:8px">
        <button data-k="0,58">F1</button><button data-k="0,59">F2</button><button data-k="0,60">F3</button><button data-k="0,61">F4</button>
        <button data-k="0,62">F5</button><button data-k="0,63">F6</button><button data-k="0,64">F7</button><button data-k="0,65">F8</button>
        <button data-k="0,66">F9</button><button data-k="0,67">F10</button><button data-k="0,68">F11</button><button data-k="0,69">F12</button>
      </div>
    </details>
  </section>

  <section class="card ctl">
    <div class="lbl">Trackpad</div>
    <div id="pad">1 finger: move · tap: click<br>2 fingers: scroll · 2-finger tap: right-click</div>
    <div class="row">
      <button data-click="1">Click</button><button data-click="2">Right-click</button><button id="dbl">Double</button>
    </div>
    <div class="lbl" style="margin-top:12px">Nudge pointer</div>
    <div class="seg" id="steps">
      <button data-step="1">1</button><button data-step="10" class="on">10</button><button data-step="50">50</button>
    </div>
    <div class="nudge">
      <span></span><button data-n="0,-1">↑</button><span></span>
      <button data-n="-1,0">←</button><button data-n="0,1">↓</button><button data-n="1,0">→</button>
    </div>
  </section>

  <details class="card ctl">
    <summary>Settings</summary>
    <div class="set"><label for="delay">Delay per character</label>
      <input type="range" id="delay" min="0" max="200" step="5" value="25"><output id="delayV">25</output></div>
    <div class="set"><label for="sens">Pointer speed</label>
      <input type="range" id="sens" min="0.2" max="4" step="0.1" value="1.5"><output id="sensV">1.5</output></div>
    <div class="set"><label for="sspd">Scroll speed</label>
      <input type="range" id="sspd" min="0.2" max="3" step="0.1" value="1"><output id="sspdV">1</output></div>
    <div class="set"><label for="inv">Invert scroll direction</label><input type="checkbox" id="inv"></div>
    <div class="set"><label for="clr">Clear text after send</label><input type="checkbox" id="clr" checked></div>
    <div class="row"><button id="forget">Forget paired computers</button></div>
    <p class="small" id="info"></p>
  </details>
</main>

<script>
(() => {
"use strict";
const $ = s => document.querySelector(s);
const $$ = s => Array.from(document.querySelectorAll(s));

// ---------- connection ----------
let token = decodeURIComponent(location.hash.slice(1));
let ws = null, wsOpen = false, retry = 500, fails = 0, everOpened = false, timer = null;

function setWs(ok){
  $("#wsDot").className = "dot " + (ok ? "ok" : "");
  $("#wsTxt").textContent = ok ? "Phone ↔ ESP32: connected" : "Phone ↔ ESP32: reconnecting…";
  $("#main").classList.toggle("off", !ok);
  if (!ok) setBle(null);
}
function setBle(m){
  const d = $("#bleDot"), t = $("#bleTxt");
  if (!m){ d.className = "dot"; t.textContent = "Mac: unknown"; return; }
  const busy = m.busy || m.q ? " · typing…" : "";
  if (m.ble === "ready"){ d.className = "dot ok"; t.textContent = "Mac: paired & connected" + busy; }
  else if (m.ble === "connected"){ d.className = "dot warn"; t.textContent = "Mac: pairing…"; }
  else if (m.ble === "failed"){ d.className = "dot"; t.textContent = "Bluetooth failed to start (see serial log)"; }
  else { d.className = "dot"; t.textContent = "Mac: not connected (waiting to pair)"; }
  $("#info").textContent = "Remembered computers: " + m.bonds + " · firmware " + m.fw;
  $("#pairBox").hidden = (m.ble === "ready" || m.ble === "failed");
  const pkBox = $("#pkBox"), wasHidden = pkBox.hidden;
  pkBox.hidden = !(m.pk > 0);
  $("#pkLeft").textContent = m.pk || 0;
  if (wasHidden && !pkBox.hidden){ $("#pk").value = ""; pkBox.scrollIntoView({block:"start"}); $("#pk").focus(); }
  if (m.pair > 0){
    const s = m.pair % 60;
    $("#pairTxt").textContent = "Pairing mode ON - " + Math.floor(m.pair / 60) + ":" + (s < 10 ? "0" : "") + s + " left";
  } else $("#pairTxt").textContent = "Not in pairing mode (still visible, but less eager)";
}
function connect(){
  clearTimeout(timer);
  if (!token){ $("#tokenBox").hidden = false; $("#main").classList.remove("off"); return; }
  if (ws && (ws.readyState === 0 || ws.readyState === 1)) return;
  ws = new WebSocket("ws://" + location.host + "/ws?t=" + encodeURIComponent(token));
  ws.onopen = () => { wsOpen = true; everOpened = true; fails = 0; retry = 500; $("#tokenBox").hidden = true; setWs(true); };
  ws.onclose = () => {
    wsOpen = false; setWs(false);
    if (++fails >= 3 && !everOpened){ $("#tokenBox").hidden = false; note("Can't connect. Is the token right?", true); }
    timer = setTimeout(connect, retry); retry = Math.min(retry * 2, 4000);
  };
  ws.onerror = () => { try { ws.close(); } catch(e){} };
  ws.onmessage = e => {
    let m; try { m = JSON.parse(e.data); } catch(_) { return; }
    if (m.t === "status") setBle(m);
    else if (m.t === "note") note(m.msg, /not|stop|busy|long|fail|memory/i.test(m.msg));
  };
}
function send(s){
  if (ws && ws.readyState === 1){ ws.send(s); return true; }
  note("Not connected to the ESP32", true); return false;
}
setInterval(() => { if (ws && ws.readyState === 1) ws.send("p"); }, 1000);
document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible"){ retry = 500; connect(); }
});
$("#tokGo").onclick = () => {
  token = $("#tok").value.trim(); $("#tok").value = "";
  if (!token) return;
  history.replaceState(null, "", "#" + encodeURIComponent(token));
  everOpened = false; fails = 0; connect();
};

// ---------- notes ----------
let noteTimer;
function note(msg, err){
  const n = $("#note"); n.textContent = msg; n.className = err ? "err" : "";
  clearTimeout(noteTimer); noteTimer = setTimeout(() => { n.textContent = ""; }, 6000);
}

// ---------- modifiers ----------
let latched = 0;
function drawMods(){ $$("[data-mod]").forEach(b => b.classList.toggle("on", !!(latched & +b.dataset.mod))); }
$$("[data-mod]").forEach(b => b.onclick = () => { latched ^= +b.dataset.mod; drawMods(); });
function takeMods(){ const m = latched; latched = 0; drawMods(); return m; }

// ---------- keys ----------
$$("[data-k]").forEach(b => b.onclick = () => {
  const [m, u] = b.dataset.k.split(",").map(Number);
  send("k " + (m | takeMods()) + " " + u);
});

// ---------- typing ----------
const SMART = {"‘":"'","’":"'","‚":"'","“":'"',"”":'"',"„":'"',
               "–":"-","—":"--","…":"..."," ":" "};
function sendText(enter){
  const txt = $("#txt");
  let s = txt.value;
  const fixed = s.replace(/[‘’‚“”„–—… ]/g, c => SMART[c]);
  const bad = [...new Set([...fixed].filter(ch => { const c = ch.codePointAt(0); return c < 0x20 || c > 0x7e; }))];
  if (bad.length){ note("Can't type " + bad.join(" ") + " (US keyboard ASCII only). Nothing sent.", true); return; }
  if (fixed.length > 512){ note("Too long: max 512 characters per send", true); return; }
  if (!fixed){ if (enter) send("k 0 40"); return; }
  const d = +$("#delay").value;
  if (send("t " + d + " " + (enter ? 1 : 0) + " " + fixed)){
    note((fixed !== s ? "Smart punctuation converted. " : "") + "Sending " + fixed.length + " characters…");
    if ($("#clr").checked) txt.value = "";
  }
  s = null;
}
$("#send").onclick = () => sendText(false);
$("#sendEnter").onclick = () => sendText(true);
$("#txt").addEventListener("keydown", e => { if (e.key === "Enter"){ e.preventDefault(); sendText(false); } });
$("#mask").onclick = () => {
  const on = $("#txt").classList.toggle("masked");
  $("#mask").textContent = on ? "Show" : "Hide";
};
$("#stop").onclick = () => { latched = 0; drawMods(); send("x"); };

// ---------- clicks + nudge ----------
$$("[data-click]").forEach(b => b.onclick = () => send("c " + b.dataset.click + " " + takeMods()));
$("#dbl").onclick = () => { const m = takeMods(); send("c 1 " + m); send("c 1 " + m); };
let step = 10;
$$("[data-step]").forEach(b => b.onclick = () => {
  step = +b.dataset.step; $$("[data-step]").forEach(x => x.classList.toggle("on", x === b));
});
$$("[data-n]").forEach(b => b.onclick = () => {
  const [x, y] = b.dataset.n.split(",").map(Number);
  send("m " + x * step + " " + y * step);
});

// ---------- trackpad ----------
const pad = $("#pad");
let tStart = 0, maxT = 0, moved = 0, last = null;
let accX = 0, accY = 0, accV = 0, accH = 0;
function centroid(ts){
  let x = 0, y = 0; for (const t of ts){ x += t.clientX; y += t.clientY; }
  return { x: x / ts.length, y: y / ts.length, n: ts.length };
}
pad.addEventListener("touchstart", e => {
  e.preventDefault();
  if (e.touches.length === e.changedTouches.length){ tStart = performance.now(); maxT = 0; moved = 0; }
  maxT = Math.max(maxT, e.touches.length);
  last = centroid(e.touches);
}, { passive: false });
pad.addEventListener("touchmove", e => {
  e.preventDefault();
  const c = centroid(e.touches);
  if (!last || c.n !== last.n){ last = c; return; }
  const dx = c.x - last.x, dy = c.y - last.y;
  moved += Math.abs(dx) + Math.abs(dy);
  if (c.n === 1 && maxT === 1){ const k = +$("#sens").value; accX += dx * k; accY += dy * k; }
  else if (c.n >= 2){ accV += dy; accH += dx; }
  last = c;
}, { passive: false });
function touchEnd(e){
  e.preventDefault();
  if (e.touches.length === 0){
    if (performance.now() - tStart < 300 && moved < 12) send("c " + (maxT >= 2 ? 2 : 1) + " " + takeMods());
    last = null;
  } else last = centroid(e.touches);
}
pad.addEventListener("touchend", touchEnd, { passive: false });
pad.addEventListener("touchcancel", touchEnd, { passive: false });

setInterval(() => {
  const ix = Math.trunc(accX), iy = Math.trunc(accY);
  if (ix || iy){ accX -= ix; accY -= iy; send("m " + ix + " " + iy); }
  const px = 14 / +$("#sspd").value;          // finger travel (px) per scroll step
  const sv = Math.trunc(accV / px), sh = Math.trunc(accH / px);
  if (sv || sh){
    accV -= sv * px; accH -= sh * px;
    const k = $("#inv").checked ? 1 : -1;      // default: content follows the finger (macOS natural scrolling)
    send("s " + (k * sv) + " " + (-k * sh));
  }
}, 20);

// ---------- settings ----------
[["delay","delayV"],["sens","sensV"],["sspd","sspdV"]].forEach(([i,o]) => {
  const f = () => { $("#" + o).textContent = $("#" + i).value; };
  $("#" + i).addEventListener("input", f); f();
});
$("#pairGo").onclick = () => send("P");
function sendCode(){
  const v = $("#pk").value.replace(/\D/g, "");
  if (v.length !== 6){ note("The code has 6 digits", true); return; }
  if (send("K " + v)) $("#pk").value = "";
}
$("#pkGo").onclick = sendCode;
$("#pk").addEventListener("keydown", e => { if (e.key === "Enter"){ e.preventDefault(); sendCode(); } });
$("#forget").onclick = () => {
  if (confirm("Forget every paired computer? The Mac will have to pair again.")) send("f");
};

connect();
})();
</script>
</body>
</html>
)HTML";
