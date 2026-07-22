#include "html_export.hpp"

#include <cstdio>
#include <sstream>
#include <string>

namespace chronicle_cli {

namespace {

std::string json_escape(std::string const& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string json_wire_value(chronicle::io::WireValue const& v) {
    using chronicle::io::WireKind;
    switch (v.kind) {
        case WireKind::Int64: return std::to_string(v.i);
        case WireKind::UInt64: return std::to_string(v.u);
        case WireKind::Double: return std::to_string(v.d);
        case WireKind::Bool: return v.b ? "true" : "false";
        case WireKind::String: return "\"" + json_escape(v.s) + "\"";
    }
    return "null";
}

std::string op_kind_json_name(chronicle::ContainerOpKind k) {
    switch (k) {
        case chronicle::ContainerOpKind::Insert: return "insert";
        case chronicle::ContainerOpKind::Erase: return "erase";
        case chronicle::ContainerOpKind::Update: return "update";
        case chronicle::ContainerOpKind::Clear: return "clear";
    }
    return "?";
}

std::string shape_json_name(chronicle::io::StreamShape s) {
    using chronicle::io::StreamShape;
    switch (s) {
        case StreamShape::Scalar: return "scalar";
        case StreamShape::IndexedOp: return "indexed";
        case StreamShape::KeyedOp: return "keyed";
    }
    return "?";
}

} // namespace

std::string session_to_json(chronicle::io::LoadedSession const& session) {
    std::ostringstream os;
    os << "{\"streams\":[";
    bool first_stream = true;
    for (auto const& stream : session.streams) {
        if (!first_stream) {
            os << ",";
        }
        first_stream = false;
        os << "{\"name\":\"" << json_escape(stream.name) << "\",\"shape\":\""
           << shape_json_name(stream.shape) << "\",\"events\":[";
        bool first_event = true;
        for (auto const& event : stream.events) {
            if (!first_event) {
                os << ",";
            }
            first_event = false;
            os << "{\"version\":" << event.version << ",\"elapsed_ns\":" << event.elapsed_ns;
            if (stream.shape != chronicle::io::StreamShape::Scalar) {
                os << ",\"op\":\"" << op_kind_json_name(event.op_kind)
                   << "\",\"key\":" << json_wire_value(event.key_or_index);
            }
            os << ",\"value\":" << json_wire_value(event.value);
            // docs/adr/0010-call-site-capture.md: known() == false for plain
            // `field = value` assignment -- omit the field entirely rather
            // than emit a fabricated "unknown:0" the JS side might mistake
            // for real data.
            if (event.call_site.known()) {
                auto const& file = event.call_site.file;
                auto const slash = file.find_last_of("/\\");
                std::string const filename = slash == std::string::npos ? file : file.substr(slash + 1);
                os << ",\"callSite\":\"" << json_escape(filename) << ":" << event.call_site.line << "\"";
            }
            os << "}";
        }
        os << "]}";
    }
    os << "]}";
    return os.str();
}

// clang-format off
constexpr char const* kPageTemplate = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Chronicle session</title>
<style>
  :root {
    color-scheme: light dark;
    --bg: #ffffff; --fg: #1a1a1a; --muted: #666666; --border: #dddddd;
    --accent: #2563eb; --ins-bg: #dcfce7; --ins-fg: #14532d;
    --del-bg: #fee2e2; --del-fg: #7f1d1d; --upd-bg: #fef9c3; --upd-fg: #713f12;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #14161a; --fg: #e6e6e6; --muted: #9aa0a6; --border: #2c2f36;
      --accent: #60a5fa; --ins-bg: #123a24; --ins-fg: #86efac;
      --del-bg: #3a1414; --del-fg: #fca5a5; --upd-bg: #3a3312; --upd-fg: #fde68a;
    }
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; font-family: -apple-system, "Segoe UI", Roboto, sans-serif;
    background: var(--bg); color: var(--fg); display: flex; height: 100vh; overflow: hidden;
  }
  #sidebar { width: 260px; border-right: 1px solid var(--border); overflow-y: auto; padding: 12px; flex-shrink: 0; }
  #main { flex: 1; overflow-y: auto; padding: 16px 24px; }
  h1 { font-size: 15px; margin: 0 0 12px; color: var(--muted); font-weight: 600; text-transform: uppercase; letter-spacing: .04em; }
  .stream-item {
    padding: 8px 10px; border-radius: 6px; cursor: pointer; margin-bottom: 4px; font-size: 13px;
  }
  .stream-item:hover { background: color-mix(in srgb, var(--accent) 12%, transparent); }
  .stream-item.active { background: var(--accent); color: white; }
  .stream-item .shape { display: block; font-size: 11px; opacity: .75; margin-top: 2px; }
  .scrubber-row { display: flex; align-items: center; gap: 12px; margin: 8px 0 20px; }
  input[type=range] { flex: 1; }
  .step-label { font-variant-numeric: tabular-nums; color: var(--muted); font-size: 13px; min-width: 220px; }
  .panel { border: 1px solid var(--border); border-radius: 8px; padding: 14px 16px; margin-bottom: 20px; }
  .panel h2 { font-size: 13px; margin: 0 0 10px; color: var(--muted); text-transform: uppercase; letter-spacing: .04em; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; }
  th, td { text-align: left; padding: 4px 8px; border-bottom: 1px solid var(--border); }
  tr.current-row { outline: 2px solid var(--accent); outline-offset: -2px; }
  .op-insert { background: var(--ins-bg); color: var(--ins-fg); }
  .op-erase { background: var(--del-bg); color: var(--del-fg); }
  .op-update { background: var(--upd-bg); color: var(--upd-fg); }
  .glyph { font-weight: 700; margin-right: 4px; }
  code { font-family: ui-monospace, Consolas, monospace; }
  #empty { color: var(--muted); padding: 40px; text-align: center; }
</style>
</head>
<body>
<div id="sidebar">
  <h1>Streams</h1>
  <div id="stream-list"></div>
</div>
<div id="main">
  <div id="empty">No stream selected.</div>
  <div id="content" style="display:none">
    <div class="scrubber-row">
      <input type="range" id="scrubber" min="0" max="0" value="0" aria-label="Timeline position">
      <span class="step-label" id="step-label"></span>
    </div>
    <div class="panel">
      <h2>Reconstructed state at this step</h2>
      <div id="snapshot"></div>
    </div>
    <div class="panel">
      <h2>Event log</h2>
      <table>
        <thead><tr><th>#</th><th>Version</th><th>+ns</th><th>Change</th><th>Call site</th></tr></thead>
        <tbody id="event-log"></tbody>
      </table>
    </div>
  </div>
</div>
<script>
__CHRONICLE_SCRIPT__
</script>
</body>
</html>
)HTML";
// clang-format on

// Shared between write_html_export (static, inline data) and serve.cpp
// (live, re-read per request -- see docs/adr/0016-interactive-browser-viewer.md):
// every function here is identical for both; `data_expr` is the only
// difference between the two callers, a JS expression yielding the
// {streams:[...]} object synchronously (both current callers just pass the
// same session_to_json() output -- serve.cpp calls it fresh per request
// instead of once at export time, but the JS itself doesn't know the
// difference).
//
// renderObjectGraph() (docs/10-roadmap.md's v1.0 "object/ownership graph
// view") groups streams by name prefix up to the last '.' -- "player_1.health"
// and "player_1.mana" become fields of object "player_1"; a name with no
// '.' is its own single-field object. This is a derived, structural
// grouping (from how streams happen to be named -- track()'s own naming
// convention, and chronicle-adapter-entt's "component.field" naming,
// ADR 0015), not a separate ownership model Chronicle tracks explicitly.
// Guarded by an #object-graph element check so it's a harmless no-op on
// the static export's page, which has no such element -- one shared
// bootstrap tail for both pages rather than two near-duplicates.
// clang-format off
std::string viewer_script(std::string const& data_expr) {
    return std::string(R"JS(
let DATA = )JS") + data_expr + R"JS(;
let currentStream = null;
let currentIndex = 0;

function opGlyph(op) {
  if (op === 'insert') return '+';
  if (op === 'erase') return '−';
  if (op === 'update') return '~';
  return '';
}
function opClass(op) {
  if (op === 'insert') return 'op-insert';
  if (op === 'erase') return 'op-erase';
  if (op === 'update' || op === 'clear') return 'op-update';
  return '';
}
function fmt(v) { return typeof v === 'string' ? '"' + v + '"' : String(v); }

function replayIndexed(events, uptoIndex) {
  let result = [];
  for (let i = 0; i <= uptoIndex; i++) {
    const e = events[i];
    const idx = e.key;
    if (e.op === 'insert') { if (idx >= result.length) result.push(e.value); else result.splice(idx, 0, e.value); }
    else if (e.op === 'update') { if (idx < result.length) result[idx] = e.value; }
    else if (e.op === 'erase') { if (idx < result.length) result.splice(idx, 1); }
    else if (e.op === 'clear') { result = []; }
  }
  return result;
}

function replayKeyed(events, uptoIndex) {
  let result = [];
  const findIdx = (k) => result.findIndex(kv => kv[0] === k);
  for (let i = 0; i <= uptoIndex; i++) {
    const e = events[i];
    if (e.op === 'insert' || e.op === 'update') {
      const idx = findIdx(e.key);
      if (idx >= 0) result[idx][1] = e.value; else result.push([e.key, e.value]);
    } else if (e.op === 'erase') {
      const idx = findIdx(e.key);
      if (idx >= 0) result.splice(idx, 1);
    } else if (e.op === 'clear') { result = []; }
  }
  return result;
}

function renderStreamList() {
  const list = document.getElementById('stream-list');
  list.innerHTML = '';
  DATA.streams.forEach((s, i) => {
    const div = document.createElement('div');
    div.className = 'stream-item' + (s === currentStream ? ' active' : '');
    div.tabIndex = 0;
    div.innerHTML = s.name + '<span class="shape">' + s.shape + ' &middot; ' + s.events.length + ' event(s)</span>';
    div.onclick = () => selectStream(s);
    list.appendChild(div);
  });
}

function objectGraphGroups() {
  const groups = new Map();
  DATA.streams.forEach(s => {
    const dot = s.name.lastIndexOf('.');
    const obj = dot >= 0 ? s.name.slice(0, dot) : s.name;
    const field = dot >= 0 ? s.name.slice(dot + 1) : s.name;
    if (!groups.has(obj)) groups.set(obj, []);
    groups.get(obj).push({ field, stream: s });
  });
  return groups;
}

function renderObjectGraph() {
  const container = document.getElementById('object-graph');
  if (!container) return;
  container.innerHTML = '';
  for (const [obj, fields] of objectGraphGroups()) {
    const details = document.createElement('details');
    details.open = fields.some(f => f.stream === currentStream);
    const summary = document.createElement('summary');
    summary.textContent = obj + ' (' + fields.length + ' field' + (fields.length === 1 ? '' : 's') + ')';
    details.appendChild(summary);
    fields.forEach(f => {
      const div = document.createElement('div');
      div.className = 'stream-item graph-field' + (f.stream === currentStream ? ' active' : '');
      div.textContent = f.field;
      div.onclick = () => selectStream(f.stream);
      details.appendChild(div);
    });
    container.appendChild(details);
  }
}

function selectStream(s) {
  currentStream = s;
  currentIndex = s.events.length ? s.events.length - 1 : 0;
  document.getElementById('empty').style.display = 'none';
  document.getElementById('content').style.display = 'block';
  const scrubber = document.getElementById('scrubber');
  scrubber.max = Math.max(0, s.events.length - 1);
  scrubber.value = currentIndex;
  renderStreamList();
  renderObjectGraph();
  render();
}

function render() {
  if (!currentStream) return;
  const events = currentStream.events;
  const scrubber = document.getElementById('scrubber');
  currentIndex = Math.min(Number(scrubber.value), events.length - 1);
  const ev = events[currentIndex];
  document.getElementById('step-label').textContent =
    'step ' + (currentIndex + 1) + ' / ' + events.length + '  ·  v' + ev.version + '  ·  +' + ev.elapsed_ns + 'ns';

  const snap = document.getElementById('snapshot');
  if (currentStream.shape === 'scalar') {
    snap.innerHTML = '<code>' + fmt(ev.value) + '</code>';
  } else if (currentStream.shape === 'indexed') {
    const state = replayIndexed(events, currentIndex);
    snap.innerHTML = state.length
      ? '<table><tbody>' + state.map((v, i) =>
          '<tr' + (i === ev.key && ev.op !== 'erase' ? ' class="current-row"' : '') + '><td>[' + i + ']</td><td><code>' + fmt(v) + '</code></td></tr>').join('') + '</tbody></table>'
      : '<em>(empty)</em>';
  } else {
    const state = replayKeyed(events, currentIndex);
    snap.innerHTML = state.length
      ? '<table><tbody>' + state.map(([k, v]) =>
          '<tr' + (k === ev.key && ev.op !== 'erase' ? ' class="current-row"' : '') + '><td><code>' + fmt(k) + '</code></td><td><code>' + fmt(v) + '</code></td></tr>').join('') + '</tbody></table>'
      : '<em>(empty)</em>';
  }

  const log = document.getElementById('event-log');
  log.innerHTML = events.map((e, i) => {
    let change;
    if (currentStream.shape === 'scalar') {
      change = fmt(e.value);
    } else {
      change = '<span class="glyph">' + opGlyph(e.op) + '</span>' + e.op + '[' + fmt(e.key) + ']' +
               (e.op === 'insert' || e.op === 'update' ? ' = ' + fmt(e.value) : '');
    }
    const rowClass = (i === currentIndex ? 'current-row ' : '') + (currentStream.shape !== 'scalar' ? opClass(e.op) : '');
    const callSite = e.callSite ? '<code>' + e.callSite + '</code>' : '';
    return '<tr class="' + rowClass + '"><td>' + i + '</td><td>' + e.version + '</td><td>' + e.elapsed_ns + '</td><td>' + change + '</td><td>' + callSite + '</td></tr>';
  }).join('');
}

document.getElementById('scrubber').addEventListener('input', render);
renderStreamList();
renderObjectGraph();
if (DATA.streams.length) selectStream(DATA.streams[0]);
)JS";
}
// clang-format on

void write_html_export(chronicle::io::LoadedSession const& session, std::ostream& out) {
    std::string page = kPageTemplate;
    std::string const placeholder = "__CHRONICLE_SCRIPT__";
    auto const pos = page.find(placeholder);
    if (pos != std::string::npos) {
        page.replace(pos, placeholder.size(), viewer_script(session_to_json(session)));
    }
    out << page;
}

// clang-format off
constexpr char const* kServePageTemplate = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Chronicle session (live)</title>
<style>
  :root {
    color-scheme: light dark;
    --bg: #ffffff; --fg: #1a1a1a; --muted: #666666; --border: #dddddd;
    --accent: #2563eb; --ins-bg: #dcfce7; --ins-fg: #14532d;
    --del-bg: #fee2e2; --del-fg: #7f1d1d; --upd-bg: #fef9c3; --upd-fg: #713f12;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #14161a; --fg: #e6e6e6; --muted: #9aa0a6; --border: #2c2f36;
      --accent: #60a5fa; --ins-bg: #123a24; --ins-fg: #86efac;
      --del-bg: #3a1414; --del-fg: #fca5a5; --upd-bg: #3a3312; --upd-fg: #fde68a;
    }
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; font-family: -apple-system, "Segoe UI", Roboto, sans-serif;
    background: var(--bg); color: var(--fg); display: flex; height: 100vh; overflow: hidden;
  }
  #sidebar { width: 280px; border-right: 1px solid var(--border); overflow-y: auto; padding: 12px; flex-shrink: 0; }
  #main { flex: 1; overflow-y: auto; padding: 16px 24px; }
  h1 { font-size: 15px; margin: 16px 0 12px; color: var(--muted); font-weight: 600; text-transform: uppercase; letter-spacing: .04em; }
  h1:first-child { margin-top: 0; }
  .stream-item {
    padding: 8px 10px; border-radius: 6px; cursor: pointer; margin-bottom: 4px; font-size: 13px;
  }
  .stream-item:hover { background: color-mix(in srgb, var(--accent) 12%, transparent); }
  .stream-item.active { background: var(--accent); color: white; }
  .stream-item .shape { display: block; font-size: 11px; opacity: .75; margin-top: 2px; }
  .graph-field { margin-left: 12px; }
  #object-graph summary { cursor: pointer; font-size: 13px; padding: 4px 0; }
  #object-graph details { margin-bottom: 6px; }
  .scrubber-row { display: flex; align-items: center; gap: 12px; margin: 8px 0 20px; }
  input[type=range] { flex: 1; }
  .step-label { font-variant-numeric: tabular-nums; color: var(--muted); font-size: 13px; min-width: 220px; }
  .panel { border: 1px solid var(--border); border-radius: 8px; padding: 14px 16px; margin-bottom: 20px; }
  .panel h2 { font-size: 13px; margin: 0 0 10px; color: var(--muted); text-transform: uppercase; letter-spacing: .04em; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; }
  th, td { text-align: left; padding: 4px 8px; border-bottom: 1px solid var(--border); }
  tr.current-row { outline: 2px solid var(--accent); outline-offset: -2px; }
  .op-insert { background: var(--ins-bg); color: var(--ins-fg); }
  .op-erase { background: var(--del-bg); color: var(--del-fg); }
  .op-update { background: var(--upd-bg); color: var(--upd-fg); }
  .glyph { font-weight: 700; margin-right: 4px; }
  code { font-family: ui-monospace, Consolas, monospace; }
  #empty { color: var(--muted); padding: 40px; text-align: center; }
  #topbar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
  #refresh-btn {
    font: inherit; font-size: 12px; padding: 6px 12px; border-radius: 6px; border: 1px solid var(--border);
    background: var(--bg); color: var(--fg); cursor: pointer;
  }
  #refresh-btn:hover { background: color-mix(in srgb, var(--accent) 12%, transparent); }
</style>
</head>
<body>
<div id="sidebar">
  <h1>Objects</h1>
  <div id="object-graph"></div>
  <h1>Streams</h1>
  <div id="stream-list"></div>
</div>
<div id="main">
  <div id="topbar">
    <span></span>
    <button id="refresh-btn" title="Re-read the session file and re-render">&#8635; Refresh</button>
  </div>
  <div id="empty">No stream selected.</div>
  <div id="content" style="display:none">
    <div class="scrubber-row">
      <input type="range" id="scrubber" min="0" max="0" value="0" aria-label="Timeline position">
      <span class="step-label" id="step-label"></span>
    </div>
    <div class="panel">
      <h2>Reconstructed state at this step</h2>
      <div id="snapshot"></div>
    </div>
    <div class="panel">
      <h2>Event log</h2>
      <table>
        <thead><tr><th>#</th><th>Version</th><th>+ns</th><th>Change</th><th>Call site</th></tr></thead>
        <tbody id="event-log"></tbody>
      </table>
    </div>
  </div>
</div>
<script>
__CHRONICLE_SCRIPT__

async function refreshData() {
  const res = await fetch('/api/session');
  DATA = await res.json();
  const prevName = currentStream ? currentStream.name : null;
  const found = prevName ? DATA.streams.find(s => s.name === prevName) : null;
  renderStreamList();
  renderObjectGraph();
  if (found) { selectStream(found); }
  else if (DATA.streams.length) { selectStream(DATA.streams[0]); }
  else { currentStream = null; document.getElementById('content').style.display = 'none'; document.getElementById('empty').style.display = 'block'; }
}
document.getElementById('refresh-btn').addEventListener('click', refreshData);
</script>
</body>
</html>
)HTML";
// clang-format on

void write_serve_page(chronicle::io::LoadedSession const& session, std::ostream& out) {
    std::string page = kServePageTemplate;
    std::string const placeholder = "__CHRONICLE_SCRIPT__";
    auto const pos = page.find(placeholder);
    if (pos != std::string::npos) {
        page.replace(pos, placeholder.size(), viewer_script(session_to_json(session)));
    }
    out << page;
}

} // namespace chronicle_cli
