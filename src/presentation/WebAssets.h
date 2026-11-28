#pragma once

#include <Arduino.h>

// Static web assets. They live in flash and are only used by WebDashboard.
inline const char kCss[] PROGMEM = R"CSS(
:root{
  --bg:#0f1420;--panel:#171e2e;--panel2:#1e2739;--line:#2a3550;
  --text:#e7ecf5;--muted:#93a1bd;--accent:#4da3ff;
  --ok:#3ddc97;--warn:#ffb454;--warn1:#ffd666;--warn2:#ff963c;--err:#ff6b6b;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
  font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{display:flex;align-items:center;gap:20px;padding:14px 20px;
  background:var(--panel);border-bottom:1px solid var(--line);
  position:sticky;top:0;z-index:2;flex-wrap:wrap}
.brand{font-weight:700;letter-spacing:.4px}
.brand span{color:var(--accent)}
nav{display:flex;gap:6px;margin-left:auto}
nav a{color:var(--muted);text-decoration:none;padding:6px 12px;border-radius:8px}
nav a:hover{background:var(--panel2);color:var(--text)}
nav a.active{background:var(--accent);color:#08111f;font-weight:600}
main{max-width:1080px;margin:0 auto;padding:20px}
h2{margin:26px 0 10px;font-size:17px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px;overflow:hidden}
table{width:100%;border-collapse:collapse}
th,td{padding:10px 12px;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}
th{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);background:var(--panel2)}
tr:last-child td{border-bottom:none}
.chip{display:inline-block;padding:2px 8px;border-radius:999px;font-size:12px;font-weight:600}
.chip.ok{background:rgba(61,220,151,.14);color:var(--ok)}
.chip.warn1{background:rgba(255,214,102,.14);color:var(--warn1)}
.chip.warn2{background:rgba(255,150,60,.14);color:var(--warn2)}
.chip.err{background:rgba(255,107,107,.14);color:var(--err)}
.chip.idle{background:rgba(147,161,189,.14);color:var(--muted)}
.muted{color:var(--muted)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:13px}
.err-text{color:var(--err);font-size:12px;margin-top:3px;word-break:break-word;max-width:280px}
.err-text.warn1{color:var(--warn1)}
.err-text.warn2{color:var(--warn2)}
.bar{height:6px;background:var(--panel2);border-radius:4px;overflow:hidden;margin-top:6px;min-width:90px}
.bar>i{display:block;height:100%;background:var(--accent)}
.bar.hot>i{background:var(--err)}
.toolbar{display:flex;align-items:center;gap:14px;margin-bottom:14px;flex-wrap:wrap}
button{background:var(--accent);color:#08111f;border:0;border-radius:8px;
  padding:8px 14px;font-weight:600;cursor:pointer}
button:disabled{opacity:.5;cursor:default}
button.danger{background:transparent;color:var(--err);border:1px solid rgba(255,107,107,.4);
  padding:4px 10px;font-weight:500}
a{color:var(--accent)}
.actions{display:flex;gap:12px;align-items:center}
form.form{padding:16px;display:grid;gap:12px;
  grid-template-columns:repeat(auto-fit,minmax(180px,1fr));align-items:end}
label{display:flex;flex-direction:column;gap:5px;font-size:12px;color:var(--muted)}
input,select{background:#0d1320;border:1px solid var(--line);border-radius:8px;
  color:var(--text);padding:8px 10px;font:inherit;font-size:14px;width:100%}
input:focus,select:focus{outline:2px solid rgba(77,163,255,.35);border-color:var(--accent)}
.form-actions{grid-column:1/-1;display:flex;gap:12px;align-items:center}
.msg{padding:10px 14px;border-radius:10px;margin-bottom:14px;
  background:rgba(77,163,255,.12);border:1px solid rgba(77,163,255,.35)}
.empty{padding:22px;text-align:center;color:var(--muted)}
.polling{color:var(--warn)}
@media(max-width:640px){
  th,td{padding:8px}
  .hide-sm{display:none}
  main{padding:14px}
}
)CSS";

inline const char kDashboardHtml[] PROGMEM = R"HTML(
<div class="toolbar">
  <button id="poll">Poll now</button>
  <span id="status" class="muted">Loading&hellip;</span>
</div>
<div class="card">
  <table>
    <thead><tr>
      <th>Server</th><th>Status</th><th>Ping</th><th>CPU</th><th>Load 1m</th><th>Memory</th>
      <th>Disk /</th><th class="hide-sm">Last poll</th>
    </tr></thead>
    <tbody id="rows"></tbody>
  </table>
  <div id="empty" class="empty" hidden>No servers configured. <a href="/servers">Add one</a>.</div>
</div>
<script>
(function () {
  var rows = document.getElementById('rows');
  var empty = document.getElementById('empty');
  var status = document.getElementById('status');
  var pollBtn = document.getElementById('poll');
  var failureThreshold = 3;

  function cell(text, className) {
    var td = document.createElement('td');
    if (className) td.className = className;
    td.textContent = text;
    return td;
  }

  function isNumber(value) {
    return value !== null && value !== undefined && !isNaN(value);
  }

  function num(value, digits) {
    return isNumber(value) ? Number(value).toFixed(digits) : '\u2014';
  }

  function ago(ms) {
    if (!isNumber(ms) || ms < 0) return 'never';
    var seconds = Math.round(ms / 1000);
    if (seconds < 60) return seconds + ' s ago';
    if (seconds < 3600) return Math.round(seconds / 60) + ' min ago';
    return Math.round(seconds / 3600) + ' h ago';
  }

  function serverCell(server) {
    var td = document.createElement('td');
    var name = document.createElement('div');
    name.className = 'mono';
    name.textContent = server.name || server.host;
    var host = document.createElement('div');
    host.className = 'muted mono';
    host.textContent = server.host + ':' + server.port;
    td.appendChild(name);
    td.appendChild(host);
    return td;
  }

  function failureClass(failures) {
    return failures >= 2 ? 'warn2' : 'warn1';
  }

  function statusCell(server) {
    var td = document.createElement('td');
    var cls = 'idle';
    var label = 'Pending';
    var failures = isNumber(server.failures) ? server.failures : 0;
    if (!server.enabled) {
      label = 'Paused';
    } else if (!server.valid) {
      label = 'Pending';
    } else if (server.ok) {
      cls = 'ok';
      label = 'OK';
    } else if (failures >= failureThreshold) {
      cls = 'err';
      label = 'Error';
    } else if (!server.everSucceeded) {
      label = 'n/a';
    } else {
      cls = failureClass(failures);
      label = 'OK';
    }
    var chip = document.createElement('span');
    chip.className = 'chip ' + cls;
    chip.textContent = label;
    td.appendChild(chip);
    if (server.error) {
      var err = document.createElement('div');
      err.className = 'err-text' +
                       (cls === 'warn1' || cls === 'warn2' ? ' ' + cls : '');
      err.textContent = (!server.ok && failures > 0 ? failures + ' failed: ' : '') +
                        server.error;
      td.appendChild(err);
    }
    return td;
  }

  function pingCell(server) {
    var td = document.createElement('td');
    var chip = document.createElement('span');
    var cls = 'idle';
    var label = 'Pending';
    var failures = isNumber(server.pingFailures) ? server.pingFailures : 0;
    if (!server.enabled) {
      label = 'Paused';
    } else if (!server.pingValid) {
      label = 'Pending';
    } else if (server.pingOk) {
      cls = 'ok';
      label = (isNumber(server.pingMs) ? Math.round(server.pingMs) : 0) + ' ms';
    } else if (failures >= failureThreshold) {
      cls = 'err';
      label = 'Unreachable';
    } else if (!server.pingEverReachable) {
      label = 'n/a';
    } else {
      cls = failureClass(failures);
      label = 'OK';
    }
    chip.className = 'chip ' + cls;
    chip.textContent = label;
    td.appendChild(chip);
    if (!server.pingOk && server.pingError) {
      var err = document.createElement('div');
      err.className = 'err-text' +
                       (cls === 'warn1' || cls === 'warn2' ? ' ' + cls : '');
      err.textContent = (failures > 0 ? failures + ' failed: ' : '') + server.pingError;
      td.appendChild(err);
    }
    return td;
  }

  function cpuCell(server) {
    var td = document.createElement('td');
    if (!isNumber(server.cpu)) {
      td.textContent = '\u2014';
      return td;
    }
    td.className = 'mono';
    td.textContent = Math.round(server.cpu) + ' %';
    return td;
  }

  function memoryCell(server) {
    var td = document.createElement('td');
    if (!isNumber(server.memUsed) || !isNumber(server.memTotal) || server.memTotal <= 0) {
      td.textContent = '\u2014';
      return td;
    }
    var pct = Math.min(100, Math.max(0, server.memUsed / server.memTotal * 100));
    var text = document.createElement('div');
    text.className = 'mono';
    text.textContent = Math.round(server.memUsed) + ' / ' + Math.round(server.memTotal) +
                       ' MB (' + pct.toFixed(0) + '%)';
    var bar = document.createElement('div');
    bar.className = 'bar' + (pct >= 90 ? ' hot' : '');
    var fill = document.createElement('i');
    fill.style.width = pct.toFixed(1) + '%';
    bar.appendChild(fill);
    td.appendChild(text);
    td.appendChild(bar);
    return td;
  }

  function formatSize(mb) {
    if (mb >= 1024) return (mb / 1024).toFixed(1) + ' GB';
    return Math.round(mb) + ' MB';
  }

  function diskCell(server) {
    var td = document.createElement('td');
    if (!isNumber(server.diskUsed) || !isNumber(server.diskTotal) || server.diskTotal <= 0) {
      td.textContent = '\u2014';
      return td;
    }
    var pct = Math.min(100, Math.max(0, server.diskUsed / server.diskTotal * 100));
    var text = document.createElement('div');
    text.className = 'mono';
    text.textContent = formatSize(server.diskUsed) + ' / ' + formatSize(server.diskTotal) +
                       ' (' + pct.toFixed(0) + '%)';
    var bar = document.createElement('div');
    bar.className = 'bar' + (pct >= 90 ? ' hot' : '');
    var fill = document.createElement('i');
    fill.style.width = pct.toFixed(1) + '%';
    bar.appendChild(fill);
    td.appendChild(text);
    td.appendChild(bar);
    return td;
  }

  function render(data) {
    failureThreshold = isNumber(data.failureThreshold) ? data.failureThreshold : 3;
    rows.replaceChildren();
    for (var i = 0; i < data.servers.length; i++) {
      var server = data.servers[i];
      var tr = document.createElement('tr');
      tr.appendChild(serverCell(server));
      tr.appendChild(statusCell(server));
      tr.appendChild(pingCell(server));
      tr.appendChild(cpuCell(server));
      tr.appendChild(cell(num(server.load, 2), 'mono'));
      tr.appendChild(memoryCell(server));
      tr.appendChild(diskCell(server));
      var last = cell(ago(server.ageMs), 'muted hide-sm');
      tr.appendChild(last);
      rows.appendChild(tr);
    }
    empty.hidden = data.servers.length > 0;
    if (data.polling) {
      status.textContent = 'Polling servers\u2026';
      status.className = 'polling';
    } else if (data.pinging) {
      status.textContent = 'Pinging servers\u2026';
      status.className = 'polling';
    } else {
      status.textContent = 'SSH every ' + data.sshIntervalSec + ' s \u00B7 ping every ' +
                           data.pingIntervalSec + ' s \u00B7 updated ' +
                           new Date().toLocaleTimeString();
      status.className = 'muted';
    }
  }

  function refresh() {
    fetch('/api/metrics', { cache: 'no-store' }).then(function (response) {
      if (!response.ok) throw new Error('HTTP ' + response.status);
      return response.json();
    }).then(render).catch(function () {
      status.textContent = 'Cannot reach the device';
      status.className = 'polling';
    });
  }

  pollBtn.addEventListener('click', function () {
    pollBtn.disabled = true;
    fetch('/api/poll', { method: 'POST' }).catch(function () {});
    setTimeout(refresh, 1200);
    setTimeout(function () { pollBtn.disabled = false; }, 4000);
  });

  refresh();
  setInterval(refresh, 5000);
})();
</script>
)HTML";
