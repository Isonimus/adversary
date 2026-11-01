// Regression tests for the operator-dashboard stored-XSS fix.
// Attacker-controlled fields (crafted SSIDs, filenames, wardriving CSV cells)
// were interpolated into innerHTML without escaping, so a beacon SSID like
// `<img src=x onerror=...>` executed in the operator's browser. The fix adds a
// render-side escapeHtml() and routes every such sink through it.
//
// Run: node --test scripts/dashboard-xss-escaping.test.mjs
//
// We load the SHIPPED dashboard.js in a vm sandbox (not a copy) so these tests
// fail if escapeHtml is removed or weakened. Loading is side-effect-free: the
// file only assigns window.onload at top level and instantiates on load, which
// we never trigger. A trailing line exposes the file-scoped const to the test.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const DASHBOARD_JS = join(dirname(fileURLToPath(import.meta.url)), '..', 'dashboard_dev', 'dashboard.js');
const source = readFileSync(DASHBOARD_JS, 'utf8');

function loadEscapeHtml() {
  const ctx = { window: {}, document: {} };
  ctx.globalThis = ctx;
  vm.createContext(ctx);
  // The appended line runs in the same script scope, so it can read the
  // file-scoped `const escapeHtml`. A ReferenceError here (helper deleted) is
  // itself a test failure, which is the point: pre-fix code had no escapeHtml.
  vm.runInContext(`${source}\n;globalThis.__escapeHtml = escapeHtml;`, ctx);
  assert.equal(typeof ctx.__escapeHtml, 'function', 'dashboard.js must define escapeHtml');
  return ctx.__escapeHtml;
}

test('escapeHtml neutralizes every HTML-significant character', () => {
  const escapeHtml = loadEscapeHtml();
  assert.equal(escapeHtml('<'), '&lt;');
  assert.equal(escapeHtml('>'), '&gt;');
  assert.equal(escapeHtml('&'), '&amp;');
  assert.equal(escapeHtml('"'), '&quot;');
  assert.equal(escapeHtml("'"), '&#39;');
});

test('escapeHtml defuses a script-injection SSID so no live tag survives', () => {
  const escapeHtml = loadEscapeHtml();
  const out = escapeHtml('<img src=x onerror=alert(document.cookie)>');
  assert.ok(!out.includes('<'), 'no raw < may survive');
  assert.ok(!out.includes('>'), 'no raw > may survive');
  assert.equal(out, '&lt;img src=x onerror=alert(document.cookie)&gt;');
});

test('escapeHtml leaves benign names and the WPA/RSSI substrings intact', () => {
  const escapeHtml = loadEscapeHtml();
  // Benign filenames/SSIDs must round-trip unchanged.
  assert.equal(escapeHtml('HomeNet_5G-handshake.pcap'), 'HomeNet_5G-handshake.pcap');
  // The CSV color-coding keys on these substrings AFTER escaping, so escaping
  // must not alter them (guards the "escape first, then wrap in span" order).
  assert.equal(escapeHtml('WPA2'), 'WPA2');
  assert.equal(escapeHtml('Open'), 'Open');
  assert.equal(escapeHtml('-73'), '-73');
});

test('escapeHtml coerces non-strings without throwing', () => {
  const escapeHtml = loadEscapeHtml();
  assert.equal(escapeHtml(42), '42');
  assert.equal(escapeHtml(null), 'null');
  assert.equal(escapeHtml(undefined), 'undefined');
});

// Source-level guards: the helper existing is not enough; each sink that was
// vulnerable must actually route through it. These fail on the pre-fix source
// (which interpolated the raw field) and pass after the fix.
test('every attacker-controlled innerHTML sink routes through escapeHtml', () => {
  const sinks = [
    '${escapeHtml(file.name)}',        // handshake / wardriving / capture file names
    '${escapeHtml(file.category)}',    // capture category
    '${escapeHtml(h)}',                // wardriving CSV header cells
    'let val = escapeHtml(cells[i]',   // wardriving CSV data cells (escape before span wrap)
    '${escapeHtml(data.ssid',          // handshake-detail SSID
    '${escapeHtml(data.bssid',         // handshake-detail BSSID
    '${escapeHtml(data.type)}',        // handshake-detail type
    '${escapeHtml(crackedPwd)}',       // recovered password
  ];
  for (const sink of sinks) {
    assert.ok(source.includes(sink), `sink must be escaped: ${sink}`);
  }

  // And the specific raw interpolations that were the vulnerability must be gone.
  const rawRegressions = [
    '<td>${file.name}</td>',
    '${file.category}</td>',
    '<strong>SSID:</strong> ${data.ssid',
    'let val = cells[i] ||',
  ];
  for (const raw of rawRegressions) {
    assert.ok(!source.includes(raw), `raw (unescaped) sink must not reappear: ${raw}`);
  }
});
