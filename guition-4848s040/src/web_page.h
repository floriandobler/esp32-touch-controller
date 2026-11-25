// Weboberfläche des Panels (eine Seite, ohne externe Abhängigkeiten).
#pragma once
#include <pgmspace.h>

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OSC-Panel</title>
<style>
:root{--bg:#0d1115;--card:#151b21;--line:#242d36;--text:#e4e9ee;--muted:#8a96a2;--accent:#ffb000;--ok:#3ccf7a;--bad:#ff5a4f;--in:#5aa9ff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
main{max-width:860px;margin:0 auto;padding:20px 16px 40px}
header{display:flex;align-items:center;gap:12px;margin-bottom:18px;flex-wrap:wrap}
h1{font-size:20px;margin:0;letter-spacing:.2px}
.pill{display:inline-flex;align-items:center;gap:7px;padding:4px 10px;border-radius:99px;background:var(--card);border:1px solid var(--line);font-size:13px;color:var(--muted)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--muted)}
.dot.ok{background:var(--ok)}.dot.bad{background:var(--bad)}.dot.warn{background:var(--accent)}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
@media(max-width:700px){.grid{grid-template-columns:1fr}}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px}
section.wide{grid-column:1/-1}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.8px;color:var(--muted);margin:0 0 12px;font-weight:600}
label{display:block;font-size:13px;color:var(--muted);margin:10px 0 4px}
input[type=text],input[type=number],input[type=password]{width:100%;padding:9px 10px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--text);font:inherit}
input:focus{outline:2px solid var(--accent);outline-offset:-1px;border-color:transparent}
.row{display:flex;gap:10px}.row>*{flex:1}
button{font:inherit;font-size:14px;padding:8px 14px;border-radius:8px;border:1px solid var(--line);background:#1e262e;color:var(--text);cursor:pointer}
button:hover{background:#26303a}
button.primary{background:var(--accent);border-color:var(--accent);color:#16120a;font-weight:600}
button.primary:hover{filter:brightness(1.08)}
button.danger{color:var(--bad)}
button.small{padding:5px 10px;font-size:13px}
button:disabled{opacity:.5;cursor:default}
.actions{margin-top:14px;display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.msg{font-size:13px;color:var(--muted)}.msg.ok{color:var(--ok)}.msg.bad{color:var(--bad)}
ul.layouts{list-style:none;margin:0;padding:0}
ul.layouts li{display:flex;align-items:center;gap:10px;padding:10px 0;border-top:1px solid var(--line)}
ul.layouts li:first-child{border-top:0}
.lname{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.lsize{color:var(--muted);font-size:13px;font-variant-numeric:tabular-nums}
.badge{font-size:12px;padding:2px 8px;border-radius:99px;background:rgba(255,176,0,.15);color:var(--accent)}
.drop{margin-top:12px;border:1.5px dashed var(--line);border-radius:10px;padding:20px;text-align:center;color:var(--muted);cursor:pointer;transition:border-color .15s,background .15s}
.drop:hover,.drop.over{border-color:var(--accent);background:rgba(255,176,0,.05);color:var(--text)}
.check{display:flex;align-items:center;gap:8px;margin-top:10px;font-size:14px;color:var(--muted)}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:0;font-size:14px}
dt{color:var(--muted)}dd{margin:0;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.log{height:260px;overflow:auto;background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:8px 10px;font:12.5px/1.55 ui-monospace,SFMono-Regular,Menlo,monospace}
.log div{white-space:pre-wrap;overflow-wrap:anywhere}
.log .t{color:#55616c;margin-right:8px}
.log .o{color:var(--accent)}.log .i{color:var(--in)}.log .x{color:var(--muted)}
.empty{color:var(--muted);font-size:14px;padding:6px 0}
.hint{font-size:13px;color:var(--muted);margin:8px 0 0}
footer{margin-top:18px;display:flex;justify-content:space-between;align-items:center;gap:12px;color:var(--muted);font-size:13px;flex-wrap:wrap}
</style>
</head>
<body>
<main>
<header>
  <h1>OSC-Panel</h1>
  <span class="pill"><span class="dot" id="netDot"></span><span id="netText">…</span></span>
</header>

<div class="grid">
  <section class="wide">
    <h2>Layouts</h2>
    <ul class="layouts" id="layouts"></ul>
    <div class="drop" id="drop">TouchOSC-Datei (.tosc) hierher ziehen oder <u>auswählen</u></div>
    <input type="file" id="file" accept=".tosc" hidden>
    <label class="check"><input type="checkbox" id="activate" checked> nach dem Hochladen sofort anzeigen</label>
    <p class="msg" id="layoutMsg"></p>
  </section>

  <section>
    <h2>OSC</h2>
    <form id="oscForm">
      <label for="host">Ziel (IP-Adresse oder Name)</label>
      <input type="text" id="host" name="host" required maxlength="63" autocomplete="off">
      <div class="row">
        <div><label for="tport">Ziel-Port</label><input type="number" id="tport" name="tport" min="1" max="65535" required></div>
        <div><label for="lport">Empfangs-Port</label><input type="number" id="lport" name="lport" min="1" max="65535" required></div>
      </div>
      <div class="actions"><button class="primary" type="submit">Speichern</button><span class="msg" id="oscMsg"></span></div>
    </form>
  </section>

  <section>
    <h2>WLAN</h2>
    <form id="wifiForm">
      <label for="ssid">Netzwerk</label>
      <input type="text" id="ssid" name="ssid" maxlength="32" autocomplete="off">
      <label for="password">Passwort</label>
      <input type="password" id="password" name="password" maxlength="63" autocomplete="new-password">
      <div class="actions"><button class="primary" type="submit">Verbinden</button><span class="msg" id="wifiMsg"></span></div>
      <p class="hint">Das Panel wechselt danach das Netz. Klappt die Verbindung nicht, öffnet es nach 20&nbsp;s das Einrichtungs-WLAN <b id="apName">OSC-Panel</b>.</p>
    </form>
  </section>

  <section>
    <h2>DMX (Art-Net)</h2>
    <form id="dmxForm">
      <input type="hidden" name="dmx" value="1">
      <label class="check"><input type="checkbox" id="dmxon" name="dmxon" value="1"> Art-Net-Ausgang an</label>
      <div class="row">
        <div><label for="dmxhost">Ziel-IP (leer = Broadcast)</label><input type="text" id="dmxhost" name="dmxhost" maxlength="15" autocomplete="off" placeholder="Broadcast"></div>
        <div><label for="dmxuni">Universe</label><input type="number" id="dmxuni" name="dmxuni" min="0" max="32767" required></div>
      </div>
      <div class="actions"><button class="primary" type="submit">Speichern</button><span class="msg" id="dmxMsg"></span></div>
      <p class="hint">OSC an <b>/dmx/&lt;Kanal&gt;</b> (1–512) geht als Art-Net raus: Float 0–1 → 0–255, Integer direkt 0–255. <b>/dmx/&lt;Universe&gt;/&lt;Kanal&gt;</b> für ein anderes Universe, mehrere Werte gehen an die folgenden Kanäle. Über WLAN ist die feste IP des Art-Net-Knotens zuverlässiger als Broadcast.</p>
    </form>
  </section>

  <section>
    <h2>Lampen (Tuya / Smart Life)</h2>
    <ul class="layouts" id="lamps"></ul>
    <div class="actions">
      <button type="button" id="lampPick">devices.json laden …</button>
      <input type="file" id="lampFile" accept=".json,application/json" hidden>
      <button class="small danger" id="lampClear" type="button">Alle entfernen</button>
      <span class="msg" id="lampMsg"></span>
    </div>
    <p class="hint">Die Datei aus dem tinytuya-Assistenten enthält ID, Schlüssel und IP jeder Lampe. Der Schlüssel bleibt im Panel und wird nicht wieder angezeigt. Steuerung per OSC: <b>/tuya/&lt;Nr&gt;/on</b>, <b>bright</b>, <b>temp</b>, <b>hue</b>, <b>sat</b>, <b>color</b> (Werte 0–1, Nr ab 1 oder <b>all</b>).</p>
  </section>

  <section class="wide">
    <h2>Monitor</h2>
    <div class="log" id="log"></div>
    <div class="actions">
      <button class="small" id="pause" type="button">Anhalten</button>
      <button class="small" id="clear" type="button">Leeren</button>
      <span class="msg" id="counts"></span>
    </div>
  </section>

  <section class="wide">
    <h2>System</h2>
    <dl id="sys"></dl>
  </section>
</div>

<footer>
  <span>Erreichbar unter <b id="mdns">osc-panel.local</b></span>
  <button class="small danger" id="restart" type="button">Neu starten</button>
</footer>
</main>

<script>
const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const kb = n => n < 1024 ? n + ' B' : n < 1048576 ? (n/1024).toFixed(1) + ' KB' : (n/1048576).toFixed(2) + ' MB';
let st = null, formsFilled = false;

function msg(el, text, cls) { el.textContent = text; el.className = 'msg' + (cls ? ' ' + cls : ''); }

async function api(path, opts) {
  const r = await fetch(path, opts);
  let j = {};
  try { j = await r.json(); } catch (e) {}
  if (!r.ok || j.ok === false) throw new Error(j.error || ('Fehler ' + r.status));
  return j;
}

function render() {
  if (!st) return;
  const dot = $('netDot');
  if (st.connected) {
    dot.className = 'dot ok';
    $('netText').textContent = st.ssid + ' · ' + st.ip + (st.rssi ? ' · ' + st.rssi + ' dBm' : '');
  } else if (st.ap) {
    dot.className = 'dot warn';
    $('netText').textContent = 'Einrichtungs-WLAN ' + st.apSsid + ' · ' + st.ip;
  } else {
    dot.className = 'dot bad';
    $('netText').textContent = 'nicht verbunden';
  }
  $('apName').textContent = st.apSsid;
  $('mdns').textContent = st.hostname + '.local';

  const ul = $('layouts');
  if (!st.layouts.length) ul.innerHTML = '<li class="empty">Noch kein Layout gespeichert.</li>';
  else ul.innerHTML = st.layouts.map(l => {
    const active = l.name === st.layout, n = encodeURIComponent(l.name);
    return '<li><span class="lname">' + esc(l.name.replace(/\.tosc$/, '')) + '</span>' +
      (active ? '<span class="badge">angezeigt</span>' : '') +
      '<span class="lsize">' + kb(l.size) + '</span>' +
      (active ? '' : '<button class="small" data-act="activate" data-n="' + n + '">Anzeigen</button>') +
      '<button class="small" data-act="download" data-n="' + n + '" title="Herunterladen">⬇</button>' +
      (active ? '' : '<button class="small danger" data-act="delete" data-n="' + n + '" title="Löschen">✕</button>') +
      '</li>';
  }).join('');

  if (!formsFilled) {
    $('host').value = st.host; $('tport').value = st.tport; $('lport').value = st.lport;
    $('ssid').value = st.ssid;
    $('dmxon').checked = st.dmxOn; $('dmxhost').value = st.dmxHost; $('dmxuni').value = st.dmxUni;
    $('password').placeholder = st.hasPassword ? 'unverändert' : '';
    formsFilled = true;
  }

  $('lamps').innerHTML = st.lamps.length ? st.lamps.map((l, i) =>
    '<li><span class="badge">' + (i + 1) + '</span><span class="lname">' + esc(l.name) + '</span>' +
    '<span class="lsize">' + esc(l.ip || 'IP unbekannt') + '</span>' +
    '<span class="dot ' + (l.online ? 'ok' : 'bad') + '" title="' + (l.online ? 'verbunden' : 'nicht verbunden') + '"></span></li>'
  ).join('') : '<li class="empty">Keine Lampen eingerichtet.</li>';

  const up = st.uptime, h = Math.floor(up / 3600), m = Math.floor(up % 3600 / 60);
  $('sys').innerHTML =
    '<dt>Layout</dt><dd>' + (st.layout ? esc(st.layout) + ' · ' + st.controls + ' Elemente' : '–') + '</dd>' +
    '<dt>Sendet an</dt><dd>' + esc(st.host) + ':' + st.tport + (st.resolved ? '' : ' <span style="color:var(--bad)">(nicht gefunden)</span>') + '</dd>' +
    '<dt>Empfängt auf</dt><dd>Port ' + st.lport + '</dd>' +
    '<dt>Art-Net</dt><dd>' + (st.dmxOn ? esc(st.dmxTarget) + ' · Universe ' + st.dmxUni + ' · ' + st.dmxSent + ' Pakete' : 'aus') + '</dd>' +
    '<dt>Speicher</dt><dd>' + kb(st.fsFree) + ' frei für Layouts · RAM ' + kb(st.heap) + ' · PSRAM ' + kb(st.psram) + '</dd>' +
    '<dt>Laufzeit</dt><dd>' + h + ' h ' + m + ' min</dd>';
  $('counts').textContent = st.sent + ' gesendet · ' + st.recv + ' empfangen' + (st.dropped ? ' · ' + st.dropped + ' verworfen' : '');
}

async function refresh() {
  try { st = await api('/api/status'); render(); }
  catch (e) { $('netDot').className = 'dot bad'; $('netText').textContent = 'Panel nicht erreichbar'; }
}

$('layouts').addEventListener('click', async e => {
  const b = e.target.closest('button'); if (!b) return;
  const name = decodeURIComponent(b.dataset.n), act = b.dataset.act;
  if (act === 'download') { location.href = '/api/layouts/download?name=' + b.dataset.n; return; }
  if (act === 'delete' && !confirm('„' + name + '“ löschen?')) return;
  b.disabled = true;
  try {
    await api('/api/layouts/' + act + '?name=' + b.dataset.n, {method: 'POST'});
    msg($('layoutMsg'), act === 'delete' ? 'Gelöscht.' : '„' + name + '“ wird angezeigt.', 'ok');
  } catch (err) { msg($('layoutMsg'), err.message, 'bad'); }
  refresh();
});

async function upload(file) {
  if (!file) return;
  if (!/\.tosc$/i.test(file.name)) { msg($('layoutMsg'), 'Bitte eine .tosc-Datei wählen.', 'bad'); return; }
  msg($('layoutMsg'), 'Lädt hoch …');
  const fd = new FormData(); fd.append('file', file, file.name);
  try {
    const j = await api('/api/layouts?activate=' + ($('activate').checked ? 1 : 0), {method: 'POST', body: fd});
    msg($('layoutMsg'), '„' + j.name + '“ gespeichert (' + j.controls + ' Elemente)' + (j.active ? ' und angezeigt.' : '.'), 'ok');
  } catch (err) { msg($('layoutMsg'), err.message, 'bad'); }
  $('file').value = '';
  refresh();
}
const drop = $('drop');
drop.onclick = () => $('file').click();
$('file').onchange = e => upload(e.target.files[0]);
['dragenter', 'dragover'].forEach(t => drop.addEventListener(t, e => { e.preventDefault(); drop.classList.add('over'); }));
['dragleave', 'drop'].forEach(t => drop.addEventListener(t, e => { e.preventDefault(); drop.classList.remove('over'); }));
drop.addEventListener('drop', e => upload(e.dataTransfer.files[0]));

$('oscForm').onsubmit = async e => {
  e.preventDefault();
  try {
    await api('/api/settings', {method: 'POST', body: new URLSearchParams(new FormData(e.target))});
    msg($('oscMsg'), 'Gespeichert.', 'ok'); formsFilled = false; refresh();
  } catch (err) { msg($('oscMsg'), err.message, 'bad'); }
};

$('dmxForm').onsubmit = async e => {
  e.preventDefault();
  try {
    await api('/api/settings', {method: 'POST', body: new URLSearchParams(new FormData(e.target))});
    msg($('dmxMsg'), 'Gespeichert.', 'ok'); formsFilled = false; refresh();
  } catch (err) { msg($('dmxMsg'), err.message, 'bad'); }
};

async function saveLamps(cfg, text) {
  try {
    await api('/api/lamps', {method: 'POST', body: new URLSearchParams({cfg})});
    msg($('lampMsg'), text, 'ok'); refresh();
  } catch (err) { msg($('lampMsg'), err.message, 'bad'); }
}
$('lampFile').onchange = async e => {
  const f = e.target.files[0]; e.target.value = '';
  if (!f) return;
  let list;
  try { list = JSON.parse(await f.text()); } catch (err) { msg($('lampMsg'), 'Keine gültige JSON-Datei', 'bad'); return; }
  if (!Array.isArray(list)) { msg($('lampMsg'), 'Erwartet die devices.json aus dem tinytuya-Assistenten', 'bad'); return; }
  const ok = list.filter(d => d.id && d.key && String(d.version || '3.3') === '3.3');
  const skipped = list.length - ok.length;
  if (!ok.length) { msg($('lampMsg'), 'Keine Lampe mit Protokoll 3.3 gefunden', 'bad'); return; }
  const clean = s => String(s || '').replace(/[\t\r\n]/g, ' ').trim();
  const cfg = ok.map(d => [clean(d.id), clean(d.key), clean(d.ip), clean(d.name)].join('\t')).join('\n');
  saveLamps(cfg, ok.length + ' Lampe(n) übernommen' + (skipped ? ', ' + skipped + ' übersprungen (anderes Protokoll)' : '') + '.');
};
$('lampPick').onclick = () => $('lampFile').click();
$('lampClear').onclick = () => { if (confirm('Alle Lampen entfernen?')) saveLamps('', 'Entfernt.'); };

$('wifiForm').onsubmit = async e => {
  e.preventDefault();
  const ssid = $('ssid').value;
  if (!confirm('Mit „' + ssid + '“ verbinden? Die Seite ist danach unter der neuen Adresse des Panels erreichbar (siehe Display).')) return;
  try {
    await api('/api/settings', {method: 'POST', body: new URLSearchParams(new FormData(e.target))});
    msg($('wifiMsg'), 'Verbindet … neue Adresse steht auf dem Display.', 'ok');
    $('password').value = '';
  } catch (err) { msg($('wifiMsg'), err.message, 'bad'); }
};

$('restart').onclick = async () => {
  if (!confirm('Panel neu starten?')) return;
  try { await api('/api/restart', {method: 'POST'}); } catch (e) {}
  $('netText').textContent = 'startet neu …';
  setTimeout(refresh, 6000);
};

// Monitor
let since = 0, paused = false, offset = null;
const logEl = $('log');
$('pause').onclick = () => { paused = !paused; $('pause').textContent = paused ? 'Weiter' : 'Anhalten'; };
$('clear').onclick = () => { logEl.innerHTML = ''; };
function fmt(ms) {
  const d = new Date(Date.now() - (offset - ms));
  return d.toTimeString().slice(0, 8) + '.' + String(d.getMilliseconds()).padStart(3, '0');
}
async function pollLog() {
  if (!paused && !document.hidden) {
    try {
      const j = await api('/api/log?since=' + since);
      offset = j.now;
      since = j.next;
      if (j.lines.length) {
        const atBottom = logEl.scrollTop + logEl.clientHeight >= logEl.scrollHeight - 4;
        const frag = document.createDocumentFragment();
        for (const [ms, dir, text] of j.lines) {
          const div = document.createElement('div');
          const cls = dir === '>' ? 'o' : dir === '<' ? 'i' : 'x';
          const arrow = dir === '>' ? '→' : dir === '<' ? '←' : '•';
          div.innerHTML = '<span class="t">' + fmt(ms) + '</span><span class="' + cls + '">' + arrow + ' ' + esc(text) + '</span>';
          frag.appendChild(div);
        }
        logEl.appendChild(frag);
        while (logEl.childElementCount > 500) logEl.firstChild.remove();
        if (atBottom) logEl.scrollTop = logEl.scrollHeight;
      }
    } catch (e) {}
  }
  setTimeout(pollLog, 400);
}

refresh();
setInterval(refresh, 3000);
pollLog();
</script>
</body>
</html>
)HTML";
